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
typedef struct { unsigned __int128 f0; uint8_t f1; uint8_t pe[15]; } gm_t9p38b040grms;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint32_t f2; uint32_t f3; } gm_tmm67r2c94d5j;
typedef struct { uint64_t f0; uint32_t f1; uint8_t pe[4]; } gm_tn68el8d14vku;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
typedef struct gm_cs7e9ppsd384bj { uint32_t f0; gm_csguankko4lmhd f1; } gm_cs7e9ppsd384bj;
_Static_assert(sizeof(gm_cseahvg8v7p0vs) == 12 && _Alignof(gm_cseahvg8v7p0vs) == 4 && offsetof(gm_cseahvg8v7p0vs, f0) == 0 && offsetof(gm_cseahvg8v7p0vs, f1) == 4 && offsetof(gm_cseahvg8v7p0vs, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct gm_csv4kcdnuj265c { uint32_t f0; uint32_t f1; gm_csguankko4lmhd f2; } gm_csv4kcdnuj265c;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_b16_8 f0; int32_t f1; uint8_t pe[4]; } gm_t0bf4lga76ktk;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; gm_t9qkh0rsooc8d f2; uint8_t p3[3]; gm_tf5srh4mp4b3i f3; } gm_t0mglksrkfa5e;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t f2; uint8_t p3[6]; gm_b16_8 f3; void * f4; void * f5; gm_tl874t2atnlh3 f6; uint8_t pe[4]; } gm_t24301fas1b2a;
typedef struct { uint64_t f0; gm_b16_8 f1; uint16_t f2; uint8_t pe[6]; } gm_t3568dh2osse0;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; uint32_t f2; uint8_t pe[4]; } gm_t5g5vgung6h6s;
_Static_assert(sizeof(gm_t66j1im9ip128) == 32 && _Alignof(gm_t66j1im9ip128) == 8 && offsetof(gm_t66j1im9ip128, f0) == 0 && offsetof(gm_t66j1im9ip128, f1) == 8 && offsetof(gm_t66j1im9ip128, f2) == 16 && offsetof(gm_t66j1im9ip128, f3) == 24, "layout of `(types: usize, values: usize, fns: usize, consts: usize)`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9p38b040grms) == 32 && _Alignof(gm_t9p38b040grms) == 16 && offsetof(gm_t9p38b040grms, f0) == 0 && offsetof(gm_t9p38b040grms, f1) == 16, "layout of `?u128`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { uint64_t f0; uint64_t f1; gm_b16_8 f2; } gm_tbccvikg6b96e;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tbgd14esf0jvi;
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t pe[4]; } gm_tep2drsl9408b;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t f2; uint8_t p3[6]; void * f3; void * f4; gm_tl874t2atnlh3 f5; uint8_t pe[4]; } gm_tf2orqcrphmek;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_tf5srh4mp4b3i f0; uint8_t f1; uint8_t pe[7]; } gm_th3eueiipivq6;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
_Static_assert(sizeof(gm_tn68el8d14vku) == 16 && _Alignof(gm_tn68el8d14vku) == 8 && offsetof(gm_tn68el8d14vku, f0) == 0 && offsetof(gm_tn68el8d14vku, f1) == 8, "layout of `(i: usize, x: u32)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t f1; uint8_t pe[3]; } gm_toceffb697p0u;
typedef struct { gm_b16_8 f0; uint32_t f1; uint8_t pe[4]; } gm_tommf4j9upomc;
typedef struct { uint32_t f0; uint32_t f1; gm_t88rcucg1b7uk f2; } gm_tov4mtrfp5e7v;
typedef struct { uint32_t f0; gm_t88rcucg1b7uk f1; } gm_tplpbqf3duvn3;
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
_Static_assert(sizeof(gm_cs7e9ppsd384bj) == 8 && _Alignof(gm_cs7e9ppsd384bj) == 4 && offsetof(gm_cs7e9ppsd384bj, f0) == 0 && offsetof(gm_cs7e9ppsd384bj, f1) == 4, "layout of `WithSlot`");
_Static_assert(sizeof(gm_csv4kcdnuj265c) == 12 && _Alignof(gm_csv4kcdnuj265c) == 4 && offsetof(gm_csv4kcdnuj265c, f0) == 0 && offsetof(gm_csv4kcdnuj265c, f1) == 4 && offsetof(gm_csv4kcdnuj265c, f2) == 8, "layout of `BitField`");
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t0bf4lga76ktk) == 24 && _Alignof(gm_t0bf4lga76ktk) == 8 && offsetof(gm_t0bf4lga76ktk, f0) == 0 && offsetof(gm_t0bf4lga76ktk, f1) == 16, "layout of `(string, i32)`");
_Static_assert(sizeof(gm_t0mglksrkfa5e) == 40 && _Alignof(gm_t0mglksrkfa5e) == 8 && offsetof(gm_t0mglksrkfa5e, f0) == 0 && offsetof(gm_t0mglksrkfa5e, f1) == 16 && offsetof(gm_t0mglksrkfa5e, f2) == 20 && offsetof(gm_t0mglksrkfa5e, f3) == 24, "layout of `FieldDef`");
_Static_assert(sizeof(gm_t24301fas1b2a) == 72 && _Alignof(gm_t24301fas1b2a) == 8 && offsetof(gm_t24301fas1b2a, f0) == 0 && offsetof(gm_t24301fas1b2a, f1) == 16 && offsetof(gm_t24301fas1b2a, f2) == 17 && offsetof(gm_t24301fas1b2a, f3) == 24 && offsetof(gm_t24301fas1b2a, f4) == 40 && offsetof(gm_t24301fas1b2a, f5) == 48 && offsetof(gm_t24301fas1b2a, f6) == 56, "layout of `FieldDecl`");
_Static_assert(sizeof(gm_t3568dh2osse0) == 32 && _Alignof(gm_t3568dh2osse0) == 8 && offsetof(gm_t3568dh2osse0, f0) == 0 && offsetof(gm_t3568dh2osse0, f1) == 8 && offsetof(gm_t3568dh2osse0, f2) == 24, "layout of `(u64, string, u16)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_th2lqbo77pvsc f6; } gm_t4gut2c9fj1kt;
typedef struct { gm_tcmkompbl09il f0; uint8_t f1; uint8_t pe[7]; } gm_t57euh612ligr;
_Static_assert(sizeof(gm_t5g5vgung6h6s) == 32 && _Alignof(gm_t5g5vgung6h6s) == 8 && offsetof(gm_t5g5vgung6h6s, f0) == 0 && offsetof(gm_t5g5vgung6h6s, f1) == 8 && offsetof(gm_t5g5vgung6h6s, f2) == 24, "layout of `(u32, []u8, u32)`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
typedef struct { void * f0; gm_b16_8 f1; gm_th2lqbo77pvsc f2; gm_tl874t2atnlh3 f3; uint8_t pe[4]; } gm_t7bjbmukv26e7;
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
typedef struct { uint64_t f0; gm_t24301fas1b2a f1; } gm_t8uai8b3boj78;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tbccvikg6b96e) == 32 && _Alignof(gm_tbccvikg6b96e) == 8 && offsetof(gm_tbccvikg6b96e, f0) == 0 && offsetof(gm_tbccvikg6b96e, f1) == 8 && offsetof(gm_tbccvikg6b96e, f2) == 16, "layout of `Layout`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbgd14esf0jvi) == 32 && _Alignof(gm_tbgd14esf0jvi) == 8 && offsetof(gm_tbgd14esf0jvi, f0) == 0 && offsetof(gm_tbgd14esf0jvi, f1) == 16, "layout of `Edit`");
typedef struct { gm_th3eueiipivq6 f0; uint8_t f1; uint8_t pe[7]; } gm_tbm0n9ujhrlhf;
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
typedef struct { gm_tep2drsl9408b f0; uint8_t f1; uint8_t pe[7]; } gm_tdlpv0q69lov1;
typedef struct { gm_tnnjeerolos0a f0; gm_trqfvmjtt4sij f1; } gm_tdtrcq4ju76rh;
_Static_assert(sizeof(gm_tep2drsl9408b) == 24 && _Alignof(gm_tep2drsl9408b) == 8 && offsetof(gm_tep2drsl9408b, f0) == 0 && offsetof(gm_tep2drsl9408b, f1) == 16, "layout of `ValTy`");
_Static_assert(sizeof(gm_tf2orqcrphmek) == 56 && _Alignof(gm_tf2orqcrphmek) == 8 && offsetof(gm_tf2orqcrphmek, f0) == 0 && offsetof(gm_tf2orqcrphmek, f1) == 16 && offsetof(gm_tf2orqcrphmek, f2) == 17 && offsetof(gm_tf2orqcrphmek, f3) == 24 && offsetof(gm_tf2orqcrphmek, f4) == 32 && offsetof(gm_tf2orqcrphmek, f5) == 40, "layout of `Param`");
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `Toolchain`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_th3eueiipivq6) == 24 && _Alignof(gm_th3eueiipivq6) == 8 && offsetof(gm_th3eueiipivq6, f0) == 0 && offsetof(gm_th3eueiipivq6, f1) == 16, "layout of `(len: ?u64, is_const: bool)`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
typedef struct { gm_t08olq97rscn3 f0; gm_th2lqbo77pvsc f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tkacitepg5fk0;
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
typedef struct { gm_t08olq97rscn3 f0; void * f1; void * f2; gm_tl874t2atnlh3 f3; uint8_t pe[4]; } gm_tnljbma3s3h16;
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_toceffb697p0u) == 16 && _Alignof(gm_toceffb697p0u) == 4 && offsetof(gm_toceffb697p0u, f0) == 0 && offsetof(gm_toceffb697p0u, f1) == 12, "layout of `?Span`");
_Static_assert(sizeof(gm_tommf4j9upomc) == 24 && _Alignof(gm_tommf4j9upomc) == 8 && offsetof(gm_tommf4j9upomc, f0) == 0 && offsetof(gm_tommf4j9upomc, f1) == 16, "layout of `(string, u32)`");
_Static_assert(sizeof(gm_tov4mtrfp5e7v) == 12 && _Alignof(gm_tov4mtrfp5e7v) == 4 && offsetof(gm_tov4mtrfp5e7v, f0) == 0 && offsetof(gm_tov4mtrfp5e7v, f1) == 4 && offsetof(gm_tov4mtrfp5e7v, f2) == 8, "layout of `BitField`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t p2[7]; gm_tf5srh4mp4b3i f2; gm_tkn42u7na3t81 f3; } gm_tpautr4br7h3l;
_Static_assert(sizeof(gm_tplpbqf3duvn3) == 8 && _Alignof(gm_tplpbqf3duvn3) == 4 && offsetof(gm_tplpbqf3duvn3, f0) == 0 && offsetof(gm_tplpbqf3duvn3, f1) == 4, "layout of `WithSlot`");
typedef struct { gm_t08olq97rscn3 f0; void * f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tpv68m88f9lcr;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tnljbma3s3h16 v0; gm_t7bjbmukv26e7 v1; } f1; } gm_t03a3imodqi32;
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t57euh612ligr) == 32 && _Alignof(gm_t57euh612ligr) == 8 && offsetof(gm_t57euh612ligr, f0) == 0 && offsetof(gm_t57euh612ligr, f1) == 24, "layout of `?BitsLayout`");
_Static_assert(sizeof(gm_t7bjbmukv26e7) == 64 && _Alignof(gm_t7bjbmukv26e7) == 8 && offsetof(gm_t7bjbmukv26e7, f0) == 0 && offsetof(gm_t7bjbmukv26e7, f1) == 8 && offsetof(gm_t7bjbmukv26e7, f2) == 24 && offsetof(gm_t7bjbmukv26e7, f3) == 48, "layout of `WhenItems`");
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_t8uai8b3boj78) == 80 && _Alignof(gm_t8uai8b3boj78) == 8 && offsetof(gm_t8uai8b3boj78, f0) == 0 && offsetof(gm_t8uai8b3boj78, f1) == 8, "layout of `(i: usize, x: FieldDecl)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
_Static_assert(sizeof(gm_tbm0n9ujhrlhf) == 32 && _Alignof(gm_tbm0n9ujhrlhf) == 8 && offsetof(gm_tbm0n9ujhrlhf, f0) == 0 && offsetof(gm_tbm0n9ujhrlhf, f1) == 24, "layout of `?(len: ?u64, is_const: bool)`");
typedef struct { gm_b16_8 f0; gm_tkn42u7na3t81 f1; gm_tbci5ospk5nnn f2; } gm_tckdobunaulee;
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_tdlpv0q69lov1) == 32 && _Alignof(gm_tdlpv0q69lov1) == 8 && offsetof(gm_tdlpv0q69lov1, f0) == 0 && offsetof(gm_tdlpv0q69lov1, f1) == 24, "layout of `?ValTy`");
_Static_assert(sizeof(gm_tdtrcq4ju76rh) == 48 && _Alignof(gm_tdtrcq4ju76rh) == 8 && offsetof(gm_tdtrcq4ju76rh, f0) == 0 && offsetof(gm_tdtrcq4ju76rh, f1) == 8, "layout of `ParamData`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t24301fas1b2a v0; gm_t7bjbmukv26e7 v1; } f1; } gm_tfc3oljlofib6;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tpautr4br7h3l v0; gm_tcmkompbl09il v1; gm_b16_8 v2; } f1; } gm_ti0kgh3pc3in2;
_Static_assert(sizeof(gm_tkacitepg5fk0) == 72 && _Alignof(gm_tkacitepg5fk0) == 8 && offsetof(gm_tkacitepg5fk0, f0) == 0 && offsetof(gm_tkacitepg5fk0, f1) == 32 && offsetof(gm_tkacitepg5fk0, f2) == 56, "layout of `Attribute`");
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; } gm_tngtuiuskn938;
_Static_assert(sizeof(gm_tnljbma3s3h16) == 64 && _Alignof(gm_tnljbma3s3h16) == 8 && offsetof(gm_tnljbma3s3h16, f0) == 0 && offsetof(gm_tnljbma3s3h16, f1) == 32 && offsetof(gm_tnljbma3s3h16, f2) == 40 && offsetof(gm_tnljbma3s3h16, f3) == 48, "layout of `Variant`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t88rcucg1b7uk v0; gm_t428ue2u3hf5g v1; } f1; } gm_toa2vhe3l4hfr;
_Static_assert(sizeof(gm_tpautr4br7h3l) == 48 && _Alignof(gm_tpautr4br7h3l) == 8 && offsetof(gm_tpautr4br7h3l, f0) == 0 && offsetof(gm_tpautr4br7h3l, f1) == 16 && offsetof(gm_tpautr4br7h3l, f2) == 24 && offsetof(gm_tpautr4br7h3l, f3) == 40, "layout of `StructDef`");
_Static_assert(sizeof(gm_tpv68m88f9lcr) == 56 && _Alignof(gm_tpv68m88f9lcr) == 8 && offsetof(gm_tpv68m88f9lcr, f0) == 0 && offsetof(gm_tpv68m88f9lcr, f1) == 32 && offsetof(gm_tpv68m88f9lcr, f2) == 40, "layout of `Constraint`");
_Static_assert(sizeof(gm_t03a3imodqi32) == 72 && _Alignof(gm_t03a3imodqi32) == 8 && offsetof(gm_t03a3imodqi32, f0) == 0 && offsetof(gm_t03a3imodqi32, f1) == 8, "layout of `UnionMember`");
typedef struct { uint64_t f0; gm_tckdobunaulee f1; } gm_t3l8idb2m6c1r;
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_ti0kgh3pc3in2 f2; gm_b16_8 f3; gm_b16_8 f4; uint8_t f5; uint8_t p6[7]; gm_b16_8 f6; gm_b16_8 f7; uint8_t f8; uint8_t p9[3]; gm_tl874t2atnlh3 f9; gm_tf5srh4mp4b3i f10; } gm_t6art7bprjiqb;
_Static_assert(sizeof(gm_tckdobunaulee) == 72 && _Alignof(gm_tckdobunaulee) == 8 && offsetof(gm_tckdobunaulee, f0) == 0 && offsetof(gm_tckdobunaulee, f1) == 16 && offsetof(gm_tckdobunaulee, f2) == 24, "layout of `VariantDef`");
_Static_assert(sizeof(gm_tfc3oljlofib6) == 80 && _Alignof(gm_tfc3oljlofib6) == 8 && offsetof(gm_tfc3oljlofib6, f0) == 0 && offsetof(gm_tfc3oljlofib6, f1) == 8, "layout of `StructMember`");
_Static_assert(sizeof(gm_ti0kgh3pc3in2) == 56 && _Alignof(gm_ti0kgh3pc3in2) == 8 && offsetof(gm_ti0kgh3pc3in2, f0) == 0 && offsetof(gm_ti0kgh3pc3in2, f1) == 8, "layout of `AdtKind`");
_Static_assert(sizeof(gm_tngtuiuskn938) == 96 && _Alignof(gm_tngtuiuskn938) == 8 && offsetof(gm_tngtuiuskn938, f0) == 0 && offsetof(gm_tngtuiuskn938, f1) == 48, "layout of `Rat`");
_Static_assert(sizeof(gm_toa2vhe3l4hfr) == 40 && _Alignof(gm_toa2vhe3l4hfr) == 8 && offsetof(gm_toa2vhe3l4hfr, f0) == 0 && offsetof(gm_toa2vhe3l4hfr, f1) == 8, "layout of `GenericArg`");
typedef struct { gm_tngtuiuskn938 f0; gm_tngtuiuskn938 f1; } gm_tu15p0hi2qlv6;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; gm_tngtuiuskn938 v1; uint8_t v2; gm_tu15p0hi2qlv6 v3; } f1; } gm_t0ialelq3ql1k;
_Static_assert(sizeof(gm_t3l8idb2m6c1r) == 80 && _Alignof(gm_t3l8idb2m6c1r) == 8 && offsetof(gm_t3l8idb2m6c1r, f0) == 0 && offsetof(gm_t3l8idb2m6c1r, f1) == 8, "layout of `(i: usize, x: VariantDef)`");
_Static_assert(sizeof(gm_t6art7bprjiqb) == 184 && _Alignof(gm_t6art7bprjiqb) == 8 && offsetof(gm_t6art7bprjiqb, f0) == 0 && offsetof(gm_t6art7bprjiqb, f1) == 16 && offsetof(gm_t6art7bprjiqb, f2) == 24 && offsetof(gm_t6art7bprjiqb, f3) == 80 && offsetof(gm_t6art7bprjiqb, f4) == 96 && offsetof(gm_t6art7bprjiqb, f5) == 112 && offsetof(gm_t6art7bprjiqb, f6) == 120 && offsetof(gm_t6art7bprjiqb, f7) == 136 && offsetof(gm_t6art7bprjiqb, f8) == 152 && offsetof(gm_t6art7bprjiqb, f9) == 156 && offsetof(gm_t6art7bprjiqb, f10) == 168, "layout of `AdtInfo`");
_Static_assert(sizeof(gm_tu15p0hi2qlv6) == 192 && _Alignof(gm_tu15p0hi2qlv6) == 8 && offsetof(gm_tu15p0hi2qlv6, f0) == 0 && offsetof(gm_tu15p0hi2qlv6, f1) == 96, "layout of `(re: Rat, im: Rat)`");
_Static_assert(sizeof(gm_t0ialelq3ql1k) == 200 && _Alignof(gm_t0ialelq3ql1k) == 8 && offsetof(gm_t0ialelq3ql1k, f0) == 0 && offsetof(gm_t0ialelq3ql1k, f1) == 8, "layout of `Untyped`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t0ialelq3ql1k v0; } f1; } gm_t5vbkdq81qfs7;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t88rcucg1b7uk v0; uint32_t v1; uint32_t v2; gm_tiilmfd16v3fc v3; gm_tnpifv20irnph v4; uint32_t v5; gm_tcc1rq4n1avb2 v6; gm_t88rcucg1b7uk v7; gm_t0ialelq3ql1k v8; uint64_t v9; gm_t88rcucg1b7uk v10; } f1; } gm_t6qooom8qa2do;
_Static_assert(sizeof(gm_t5vbkdq81qfs7) == 208 && _Alignof(gm_t5vbkdq81qfs7) == 8 && offsetof(gm_t5vbkdq81qfs7, f0) == 0 && offsetof(gm_t5vbkdq81qfs7, f1) == 8, "layout of `UntypedResult`");
_Static_assert(sizeof(gm_t6qooom8qa2do) == 208 && _Alignof(gm_t6qooom8qa2do) == 8 && offsetof(gm_t6qooom8qa2do, f0) == 0 && offsetof(gm_t6qooom8qa2do, f1) == 8, "layout of `DeclValue`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t6qooom8qa2do v0; } f1; } gm_tagrkbqh054h7;
typedef struct { gm_t5vbkdq81qfs7 f0; uint8_t f1; uint8_t pe[7]; } gm_tkv1vap90vht2;
_Static_assert(sizeof(gm_tagrkbqh054h7) == 216 && _Alignof(gm_tagrkbqh054h7) == 8 && offsetof(gm_tagrkbqh054h7, f0) == 0 && offsetof(gm_tagrkbqh054h7, f1) == 8, "layout of `DeclState`");
_Static_assert(sizeof(gm_tkv1vap90vht2) == 216 && _Alignof(gm_tkv1vap90vht2) == 8 && offsetof(gm_tkv1vap90vht2, f0) == 0 && offsetof(gm_tkv1vap90vht2, f1) == 208, "layout of `?UntypedResult`");
struct gm_fr_add_420lj6o {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* b */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_add_label_50fj7vv {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_adt_luat93d {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_adt_mentions_param_0jsjgma {
    gm_frame h;
    gm_ti0kgh3pc3in2 l2; /* kind */
    uint8_t ret;
};
struct gm_fr_attr_power_of_two_gj02n2t {
    gm_frame h;
    gm_t5vbkdq81qfs7 l5; /* r */
    gm_t0ialelq3ql1k l6; /* v */
    gm_tbci5ospk5nnn l7; /* i */
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_bit_width_b7ofh2h {
    gm_frame h;
    gm_tpautr4br7h3l l5; /* def */
    gm_tckdobunaulee l11; /* v */
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_bits_layout_u6kkccp {
    gm_frame h;
    gm_tpautr4br7h3l l3; /* sd */
    gm_trqfvmjtt4sij l6; /* out */
    gm_t0mglksrkfa5e l9; /* fd */
    gm_t57euh612ligr ret;
};
struct gm_fr_c_union_fields_btdvc8b {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_check_bits_struct_ophej5b {
    gm_frame h;
    gm_t0mglksrkfa5e l10; /* f */
    gm_b0_1 ret;
};
struct gm_fr_contains_by_value_pcdi4kq {
    gm_frame h;
    gm_ti0kgh3pc3in2 l5; /* kind */
    uint8_t ret;
};
struct gm_fr_copy_pmkg67r {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_trqfvmjtt4sij l1; /* l */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_declare_adt_iefkbkk {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_declare_adt_in_74tov03 {
    gm_frame h;
    gm_trqfvmjtt4sij l16; /* generics */
    gm_trqfvmjtt4sij l17; /* defaults */
    gm_tf2orqcrphmek l20; /* p */
    gm_t6qooom8qa2do l27; /* v */
    gm_ti0kgh3pc3in2 l28; /* kind */
    gm_tpv68m88f9lcr l34; /* c */
    gm_trqfvmjtt4sij l35; /* stack */
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_declare_local_adt_6j4pdai {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_emit_dph5v1e {
    gm_frame h;
    gm_t4gut2c9fj1kt l1; /* d */
    gm_b0_1 ret;
};
struct gm_fr_empty_adt_tdp3hro {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_empty_slice_3jt8vgk {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_3jt8vgk_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_8fnsr5h {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_8q3kspo_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_92arfe8 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_atlcs4j {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_c06andg {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_d53jqcq {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_d914i5u {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_dijpe9t {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_evi8iki {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_f1ngfj0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_fh7svmj {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_g3tddrb {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_hu049sk {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ito0pap {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ose1ba9 {
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
struct gm_fr_empty_slice_s1lbe5s {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_u4e74ha {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_struct_b5i94nj {
    gm_frame h;
    gm_ti0kgh3pc3in2 ret;
};
struct gm_fr_eq_60fjn21 {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* b */
    uint8_t ret;
};
struct gm_fr_error_89glhjr {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_error_ki8i3k6 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_field_bits_2ktfm1k {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_flatten_2js7j92 {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* out */
    gm_tnpifv20irnph ret;
};
struct gm_fr_fresh_adt_u9rnf9f {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_fresh_param_442g4m9 {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_has_param_u9t0efs {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_instance_args_15ek7cf {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* out */
    gm_b16_8 ret;
};
struct gm_fr_instantiate_adt_lbjcui1 {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* gargs */
    gm_t6art7bprjiqb l8; /* info */
    gm_trqfvmjtt4sij l10; /* copy */
    gm_t6art7bprjiqb l12; /* inst_info */
    gm_trqfvmjtt4sij l13; /* map */
    gm_tpautr4br7h3l l18; /* def */
    gm_trqfvmjtt4sij l21; /* vs */
    gm_tckdobunaulee l24; /* v */
    gm_ti0kgh3pc3in2 l28; /* kind */
    gm_trqfvmjtt4sij l33; /* all */
    gm_trqfvmjtt4sij l34; /* infinite */
    gm_trqfvmjtt4sij l38; /* seen */
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_int_ci6airg {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_int_fits_imbqv0c {
    gm_frame h;
    gm_tbci5ospk5nnn l1; /* v */
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
struct gm_fr_is_infinitely_sized_mhhcucl {
    gm_frame h;
    gm_ti0kgh3pc3in2 l5; /* kind */
    uint8_t ret;
};
struct gm_fr_is_integer_8d4f9ek {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_negative_k87pp3n {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    uint8_t ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_list_084fcd6 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_5kg5cjm_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_7s4om10 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_au01ire {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_g6uul2f {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_go2qg0b {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_gst7tuh {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_ja7tv3o {
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
struct gm_fr_list_ol3ji66 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_pmp06kq_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_s83jn2q {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_spqn36o {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_vmvl81b {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_many_ptr_l4v11f8 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_member_count_otu9tc0 {
    gm_frame h;
    gm_ti0kgh3pc3in2 l0; /* kind */
    gm_tpautr4br7h3l l1; /* def */
    uint64_t ret;
};
struct gm_fr_member_ty_esvuguv {
    gm_frame h;
    gm_ti0kgh3pc3in2 l0; /* kind */
    gm_tpautr4br7h3l l2; /* def */
    gm_tkn42u7na3t81 ret;
};
struct gm_fr_neg_agd5c5k {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* r */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_new_adt_info_5pl2srl {
    gm_frame h;
    gm_ti0kgh3pc3in2 l2; /* kind */
    gm_t6art7bprjiqb ret;
};
struct gm_fr_new_param_7010dt4 {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_one_08qg1op {
    gm_frame h;
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_optional_rh6okj3 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_param_kind_of_1jngakt {
    gm_frame h;
    gm_tnnjeerolos0a ret;
};
struct gm_fr_param_n8kgsdm {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_pending_layout_ikknuv1 {
    gm_frame h;
    gm_ti0kgh3pc3in2 l3; /* kind */
    gm_t9805qtonbsag ret;
};
struct gm_fr_pop_570g79n {
    gm_frame h;
    gm_t9805qtonbsag ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_ptr_bits_glps5i7 {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_ptr_t1h2m2d {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_push_type_alias_rr9mg3i {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_remove_in_progress_lrie478 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_report_eardf07 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_restore_scope_edqq1v8 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_result_o9ras10 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_scope_mark_i3b2mvo {
    gm_frame h;
    gm_t66j1im9ip128 ret;
};
struct gm_fr_set_state_3j300fi {
    gm_frame h;
    gm_tagrkbqh054h7 l2; /* v */
    gm_b0_1 ret;
};
struct gm_fr_set_template_o09q96o {
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
struct gm_fr_slice_5dt9121 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_struct_fields_chqu3it {
    gm_frame h;
    gm_tpautr4br7h3l l3; /* def */
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_struct_kind_5l1hs46 {
    gm_frame h;
    gm_tkacitepg5fk0 l10; /* a */
    gm_trqfvmjtt4sij l13; /* fields */
    gm_trqfvmjtt4sij l14; /* defaults */
    gm_trqfvmjtt4sij l15; /* default_exprs */
    gm_trqfvmjtt4sij l17; /* members */
    gm_t24301fas1b2a l21; /* f */
    gm_tkacitepg5fk0 l26; /* a */
    gm_t0mglksrkfa5e l34; /* x */
    gm_ti0kgh3pc3in2 ret;
};
struct gm_fr_subst_a2s1mb3 {
    gm_frame h;
    gm_t53at80pfdus3 l4; /* k */
    gm_trqfvmjtt4sij l14; /* new_args */
    gm_toa2vhe3l4hfr l17; /* a */
    gm_tddtjfggdat0d l19; /* sig */
    gm_tddtjfggdat0d l20; /* sig */
    gm_trqfvmjtt4sij l51; /* out */
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_subst_effects_jnlbu5m {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* refs */
    gm_trqfvmjtt4sij l7; /* args */
    gm_toa2vhe3l4hfr l10; /* a */
    gm_toa2vhe3l4hfr l12; /* v */
    gm_tnpifv20irnph ret;
};
struct gm_fr_subst_fields_pqf9lb0 {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* out */
    gm_t0mglksrkfa5e l6; /* f */
    gm_b16_8 ret;
};
struct gm_fr_subst_sig_9ngh936 {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* params */
    gm_tddtjfggdat0d ret;
};
struct gm_fr_taken_struct_members_1ltbb18 {
    gm_frame h;
    gm_tfc3oljlofib6 l6; /* m */
    gm_t24301fas1b2a l7; /* f */
    gm_t7bjbmukv26e7 l8; /* w */
    gm_b0_1 ret;
};
struct gm_fr_taken_union_members_pavth81 {
    gm_frame h;
    gm_t03a3imodqi32 l6; /* m */
    gm_tnljbma3s3h16 l7; /* v */
    gm_t7bjbmukv26e7 l8; /* w */
    gm_b0_1 ret;
};
struct gm_fr_template_9o92cvv {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_to_decimal_8bshn5b {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_trqfvmjtt4sij l1; /* out */
    gm_b16_8 ret;
};
struct gm_fr_to_u128_haka99r {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_t9p38b040grms ret;
};
struct gm_fr_to_u64_m01t1da {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tf5srh4mp4b3i ret;
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
struct gm_fr_u8_ty_afkvj25 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_union_kind_j2gcmto {
    gm_frame h;
    gm_tkacitepg5fk0 l7; /* a */
    gm_trqfvmjtt4sij l12; /* variants */
    gm_trqfvmjtt4sij l13; /* spans */
    gm_tbci5ospk5nnn l14; /* next */
    gm_trqfvmjtt4sij l15; /* members */
    gm_tnljbma3s3h16 l18; /* v */
    gm_tckdobunaulee l21; /* x */
    gm_tbci5ospk5nnn l24; /* tag */
    gm_t0ialelq3ql1k l26; /* c */
    gm_tbci5ospk5nnn l27; /* i */
    gm_trqfvmjtt4sij l28; /* fields */
    gm_tckdobunaulee l32; /* v */
    gm_tckdobunaulee l39; /* v */
    gm_tckdobunaulee l43; /* w */
    gm_t4gut2c9fj1kt l44; /* d */
    gm_ti0kgh3pc3in2 ret;
};
struct gm_fr_union_variants_a7khaqp {
    gm_frame h;
    gm_tdlpv0q69lov1 ret;
};
struct gm_fr_zero_s25l990 {
    gm_frame h;
    gm_tbci5ospk5nnn ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline __attribute__((aligned(4))) void gm_f_add_420lj6o(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_add_label_50fj7vv(void * a0, gm_cseahvg8v7p0vs a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_adt_luat93d(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) uint8_t gm_f_adt_mentions_param_0jsjgma(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_array_th8sdi3(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_attr_power_of_two_gj02n2t(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_bit_width_b7ofh2h(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_bits_layout_u6kkccp(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) uint64_t gm_f_bits_lr3of4i(const void * a0);
__attribute__((aligned(4))) void gm_f_c_union_fields_btdvc8b(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_th2lqbo77pvsc gm_f_c_union_fields_btdvc8b_c(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_bits_struct_ophej5b(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_default_4bf68q8(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_t88rcucg1b7uk a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_len_exprs_m8s6m0h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_toceffb697p0u a2);
__attribute__((aligned(4))) uint8_t gm_f_contains_by_value_pcdi4kq(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, uint32_t a2, void * a3);
inline __attribute__((aligned(4))) void gm_f_copy_pmkg67r(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_declare_adt_iefkbkk(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_tf5srh4mp4b3i a2, gm_b16_8 a3, void * a4);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_declare_adt_in_74tov03(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_tf5srh4mp4b3i a2, gm_b16_8 a3, void * a4, uint8_t a5);
__attribute__((aligned(4))) gm_b0_1 gm_f_declare_constraint_s1juggf(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_declare_local_adt_6j4pdai(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2, void * a3);
__attribute__((aligned(4))) gm_tnpifv20irnph gm_f_effect_set_vtcpipr(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_emit_dph5v1e(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_empty_adt_tdp3hro(void * a0, uint32_t a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_3jt8vgk(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_3jt8vgk_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8fnsr5h(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8q3kspo_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_92arfe8(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_atlcs4j(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_c06andg(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d53jqcq(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d914i5u(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_dijpe9t(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_evi8iki(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_f1ngfj0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_fh7svmj(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g3tddrb(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_hu049sk(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ito0pap(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ose1ba9(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_r81t436(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rcd2jln(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_s1lbe5s(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_u4e74ha(void);
inline __attribute__((aligned(4))) void gm_f_empty_struct_b5i94nj(void *gm_out);
__attribute__((aligned(4))) gm_ti0kgh3pc3in2 gm_f_empty_struct_b5i94nj_c(void);
__attribute__((aligned(4))) gm_b0_1 gm_f_end_constraint_defer_52jt3m9(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_eq_60fjn21(const void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_89glhjr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_error_ki8i3k6(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_74ac4hb(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_7hs67uj(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_lfqasgq(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint32_t gm_f_field_bits_2ktfm1k(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_tnpifv20irnph gm_f_flatten_2js7j92(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) void gm_f_format_0au88qf(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_45bh632(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_5iu9hua(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_72n0e07(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_9kb6o1u(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_a5p8et1(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_atintr3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_bc9gr0b(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_bsjq7um(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_c5ago2c(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_fiq5e0p(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_nag8m5u(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_otdvqpt(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_slub3h9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_uaa99rp(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) uint32_t gm_f_fresh_adt_u9rnf9f(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint32_t gm_f_fresh_param_442g4m9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_from_u64_ud13q5v(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_get_6qnso5a(void * a0, const void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_has_param_u9t0efs(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_index_of_41b084m(gm_b16_8 a0, uint32_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_instance_args_15ek7cf(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_instantiate_adt_lbjcui1(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_int_ci6airg(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, uint16_t a2);
__attribute__((aligned(4))) uint8_t gm_f_int_fits_imbqv0c(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_intern_ia19mhl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_into_slice_hq0te56(void *gm_out, void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_effects_kind_uutbf9u(void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_error_8k27gd1(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_is_infinitely_sized_mhhcucl(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, void * a2);
inline __attribute__((aligned(4))) uint8_t gm_f_is_integer_8d4f9ek(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_negative_k87pp3n(const void * a0);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_key_u64_ljklb56(const void * a0);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_layout_a5ovnrm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_len_expr_i134fvg(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, gm_t88rcucg1b7uk a2, gm_t88rcucg1b7uk a3);
inline __attribute__((aligned(4))) void gm_f_list_084fcd6(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_5kg5cjm_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_7s4om10(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_au01ire(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_g6uul2f(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_go2qg0b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_gst7tuh(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_ja7tv3o(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_nof7vfj(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_oalq03d(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_ol3ji66(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_pmp06kq_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_s83jn2q(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_spqn36o(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_vmvl81b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) int32_t gm_f_mag_cmp_s39veq6(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_mag_to_u128_f843dal(void *gm_out, const void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_many_ptr_l4v11f8(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) uint64_t gm_f_member_count_otu9tc0(const void * a0);
__attribute__((aligned(4))) uint64_t gm_f_member_count_otu9tc0_c(gm_ti0kgh3pc3in2 a0);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_member_ty_esvuguv(const void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_member_ty_esvuguv_c(gm_ti0kgh3pc3in2 a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_neg_agd5c5k(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_new_adt_info_5pl2srl(void *gm_out, gm_b16_8 a0, uint64_t a1, const void * a2, gm_cseahvg8v7p0vs a3, gm_tf5srh4mp4b3i a4);
__attribute__((aligned(4))) gm_t6art7bprjiqb gm_f_new_adt_info_5pl2srl_c(gm_b16_8 a0, uint64_t a1, gm_ti0kgh3pc3in2 a2, gm_cseahvg8v7p0vs a3, gm_tf5srh4mp4b3i a4);
__attribute__((aligned(4))) uint32_t gm_f_new_param_7010dt4(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tnnjeerolos0a a2);
inline __attribute__((aligned(4))) void gm_f_one_08qg1op(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_optional_rh6okj3(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_tnnjeerolos0a gm_f_param_kind_of_1jngakt(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_param_n8kgsdm(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_pending_layout_ikknuv1(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_t9805qtonbsag gm_f_pop_570g79n(void * a0);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
inline __attribute__((aligned(4))) uint32_t gm_f_ptr_bits_glps5i7(void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_ptr_t1h2m2d(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_0mk7udh(void * a0, gm_cseahvg8v7p0vs a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_1j8vbii(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_83inprj(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_bth3krn(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_d1r4h19(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_ghqse9f(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_h69n5fi(void * a0, gm_cs7e9ppsd384bj a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_hv483nm(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_im9d4ro(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_j84ih6c(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_k3l9nic(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_kiclmcn(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_p52mcnt(void * a0, gm_csv4kcdnuj265c a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_r2k6rjf_2(void * a0, void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_s5vr2a2(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_sp4si0f(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_push_type_alias_rr9mg3i(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_u4kbe6d(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_vd1290p(void * a0, uint8_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_vrmo7c5(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_f1iksvi(void * a0, const void * a1, uint32_t a2);
__attribute__((aligned(4))) uint32_t gm_f_remove_10muhi8(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_remove_in_progress_lrie478(void * a0, uint32_t a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_type_jjodbei(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
inline __attribute__((aligned(4))) void gm_f_restore_scope_edqq1v8(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_result_o9ras10(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) void gm_f_scope_mark_i3b2mvo(void *gm_out, void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_set_state_3j300fi(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2);
inline __attribute__((aligned(4))) void gm_f_set_template_o09q96o(void * a0, uint32_t a1, uint32_t a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_8uijkbs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_signed_add_ek0dpmo(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint8_t a0, gm_b16_8 a1, uint8_t a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_slice_5dt9121(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void gm_f_soa_shape_of_ohenvjc(void *gm_out, void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_state_a5bnsjb(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_struct_fields_chqu3it(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_th2lqbo77pvsc gm_f_struct_fields_chqu3it_c(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_struct_kind_5l1hs46(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, uint32_t a2, void * a3, gm_b16_8 a4);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_subst_a2s1mb3(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tnpifv20irnph gm_f_subst_effects_jnlbu5m(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_subst_fields_pqf9lb0(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_subst_sig_9ngh936(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_subst_soa_2chqte3(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, const void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_taken_struct_members_1ltbb18(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2, void * a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_taken_union_members_pavth81(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2, void * a3);
inline __attribute__((aligned(4))) uint32_t gm_f_template_9o92cvv(void * a0, uint32_t a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_to_decimal_8bshn5b(void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_to_u128_haka99r(void *gm_out, const void * a0);
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_to_u64_m01t1da(const void * a0);
inline __attribute__((aligned(4))) void gm_f_truncate_7r782uc(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_truncate_daelje0(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_truncate_dsb08pk(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_truncate_upeumbt(void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_try_untyped_fj0h8i5(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) uint8_t gm_f_type_size_ok_tr1llg4(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_tl874t2atnlh3 a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_u8_ty_afkvj25(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_union_kind_j2gcmto(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) void gm_f_union_variants_a7khaqp(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_tdlpv0q69lov1 gm_f_union_variants_a7khaqp_c(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_when_cond_9a46a5h(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_write_decimal_ke367ab(void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1);
inline __attribute__((aligned(4))) void gm_f_zero_s25l990(void *gm_out, void *env, gm_node *cx, gm_node *inst);
extern const uint8_t gm_s2737i1iq57nf[5];
extern const uint8_t gm_s2ph9f020upd2[7];
extern const uint8_t gm_s3lik4tvvjana[65];
extern const uint8_t gm_s4ko774bb2ea9[56];
extern const uint8_t gm_s6vvtrmtnt3j1[5];
extern const uint8_t gm_s8h39pil8lgh0[41];
extern const uint8_t gm_sacmu7hr5dknu[88];
extern const uint8_t gm_sbe7dr7t400bh[16];
extern const uint8_t gm_sbse0oko82bp9[42];
extern const uint8_t gm_seoeuc6nik756[40];
extern const uint8_t gm_sf3irv2ans6de[57];
extern const uint8_t gm_sf7l7ca9coehi[85];
extern const uint8_t gm_sfdbfrv46lihd[2];
extern const uint8_t gm_sgbumttiou4de[8];
extern const uint8_t gm_shncvaenjonsu[41];
extern const uint8_t gm_sisjr6ep7j5j0[79];
extern const uint8_t gm_sj1k4t2de5144[6];
extern const uint8_t gm_slpp558cs8j3v[36];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src3k278vcmqgdr[];
extern const char gm_src4o48ksu43djd[];
extern const char gm_srcgfglnv8p059r[];
extern const char gm_srclv0dcbbfhves[];
extern const char gm_srclv3pj5es0f8o[];
extern const char gm_srcrgc9bkquf6nr[];
extern const char gm_srcsffver18si54[];
extern const uint8_t gm_su38t05utsk53[67];
extern const uint8_t gm_suuv0qpi4v4e4[8];
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
inline __attribute__((aligned(4))) void gm_f_add_420lj6o(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1) {
    struct gm_fr_add_420lj6o F;
    GM_STACK_CHECK(&F, 66u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    uint8_t t1 = GM_LD(uint8_t, (((uint8_t *)&F.l0) + 0));
    gm_b16_8 t2 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l0) + 8) + 0));
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)&t2));
    uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
    gm_b16_8 t5 = GM_LD(gm_b16_8, ((((uint8_t *)a1) + 8) + 0));
    gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)&t5));
    gm_f_signed_add_ek0dpmo((void *)&(*(gm_tbci5ospk5nnn *)gm_out), (void *)NULL, cx, NULL, t1, t3, t4, t6);
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_adt_luat93d(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2) {
    struct gm_fr_adt_luat93d F;
    void * gm_l0; /* t */
    uint32_t gm_l1; /* id */
    gm_b16_8 gm_l2; /* args */
    GM_STACK_CHECK(&F, 148u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t1 = gm_l0;
    gm_tiilmfd16v3fc t2;
    memset((uint8_t *)&t2 + 4, 0, 4);
    uint32_t t3 = gm_l1;
    GM_ST(uint32_t, (uint8_t *)&t2 + 0, t3);
    gm_b16_8 t4 = gm_l2;
    GM_ST(gm_b16_8, (uint8_t *)&t2 + 8, t4);
    gm_t53at80pfdus3 t5;
    GM_ST(uint8_t, (uint8_t *)&t5 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t5 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t5 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t5 + 32, 0);
    GM_ST(uint64_t, (uint8_t *)&t5 + 40, 0);
    GM_ST(uint64_t, (uint8_t *)&t5 + 48, 0);
    GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)17ULL));
    GM_ST(gm_tiilmfd16v3fc, ((uint8_t *)&t5 + 8), t2);
    gm_t88rcucg1b7uk t6 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t1, &t5);
    if (gm_unw.kind) {
        goto gm_lad;
    }
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
inline __attribute__((aligned(4))) void gm_f_copy_pmkg67r(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_copy_pmkg67r F;
    GM_STACK_CHECK(&F, 43u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_go2qg0b((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3 = GM_LD(gm_b16_8, ((((uint8_t *)a0) + 8) + 0));
        gm_b16_8 t4 = GM_LD(gm_b16_8, ((uint8_t *)&t3));
        gm_tftb1odc739t5 t5 = gm_f_extend_7hs67uj(t2, t4);
        if (((((uint8_t *)&t5))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t5) + 1, gm_srclv3pj5es0f8o, 330, 5);
        gm_b0_1 t6 = GM_LD(gm_b0_1, (((uint8_t *)&t5) + 1));
    }
    {
        memset((uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 1, 0, 7);
        uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 0, t7);
        memcpy((uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 8, ((uint8_t *)&F.l1), 40);
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_declare_adt_iefkbkk(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_tf5srh4mp4b3i a2, gm_b16_8 a3, void * a4) {
    struct gm_fr_declare_adt_iefkbkk F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    gm_tf5srh4mp4b3i gm_l2; /* decl */
    gm_b16_8 gm_l3; /* name */
    void * gm_l4; /* lit */
    GM_STACK_CHECK(&F, 124u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    void * t1 = gm_l0;
    uint64_t t2 = gm_l1;
    gm_tf5srh4mp4b3i t3 = gm_l2;
    gm_b16_8 t4 = gm_l3;
    void * t5 = gm_l4;
    gm_t88rcucg1b7uk t6 = gm_f_declare_adt_in_74tov03((void *)NULL, cx, NULL, t1, t2, t3, t4, t5, ((uint8_t)0));
    if (gm_unw.kind) {
        goto gm_lad;
    }
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_3jt8vgk(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_3jt8vgk F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_3jt8vgk_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_3jt8vgk_2 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8q3kspo_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_8q3kspo_2 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_atlcs4j(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_atlcs4j F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 72)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_c06andg(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_c06andg F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 72)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d53jqcq(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_d53jqcq F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 80)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_dijpe9t(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_dijpe9t F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_evi8iki(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_evi8iki F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_f1ngfj0(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_f1ngfj0 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_fh7svmj(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_fh7svmj F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 40)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g3tddrb(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_g3tddrb F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 88)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_hu049sk(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_hu049sk F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 72)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ito0pap(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_ito0pap F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ose1ba9(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_ose1ba9 F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 40)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_s1lbe5s(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_s1lbe5s F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_u4e74ha(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_u4e74ha F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 72)));
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
inline __attribute__((aligned(4))) void gm_f_empty_struct_b5i94nj(void *gm_out) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_struct_b5i94nj F;
    GM_STACK_CHECK_SHALLOW(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_tpautr4br7h3l t1;
    memset((uint8_t *)&t1 + 17, 0, 7);
    gm_b16_8 t2 = gm_f_empty_slice_ose1ba9();
    GM_ST(gm_b16_8, (uint8_t *)&t1 + 0, t2);
    GM_ST(uint8_t, (uint8_t *)&t1 + 16, ((uint8_t)0));
    gm_tf5srh4mp4b3i t3;
    memset(&t3, 0, sizeof t3);
    GM_ST(gm_tf5srh4mp4b3i, (uint8_t *)&t1 + 24, t3);
    gm_tkn42u7na3t81 t4;
    memset(&t4, 0, sizeof t4);
    GM_ST(gm_tkn42u7na3t81, (uint8_t *)&t1 + 40, t4);
    GM_ST(uint8_t, (uint8_t *)&(*(gm_ti0kgh3pc3in2 *)gm_out) + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&(*(gm_ti0kgh3pc3in2 *)gm_out) + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&(*(gm_ti0kgh3pc3in2 *)gm_out) + 4, 0);
    GM_ST(uint8_t, (uint8_t *)&(*(gm_ti0kgh3pc3in2 *)gm_out) + 0, ((uint8_t)0ULL));
    GM_ST(gm_tpautr4br7h3l, (uint8_t *)&(*(gm_ti0kgh3pc3in2 *)gm_out) + 8, t1);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) uint8_t gm_f_eq_60fjn21(const void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_eq_60fjn21 F;
    GM_STACK_CHECK_SHALLOW(&F, 75u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint8_t t1 = 0;
    uint8_t t2 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    uint8_t t3 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
    uint8_t t4 = ((t2 == t3));
    if (t4) {
        gm_b16_8 t5 = GM_LD(gm_b16_8, ((((uint8_t *)a0) + 8) + 0));
        gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)&t5));
        gm_b16_8 t7 = GM_LD(gm_b16_8, ((((uint8_t *)a1) + 8) + 0));
        gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&t7));
        int32_t t9 = gm_f_mag_cmp_s39veq6(t6, t8);
        uint8_t t10 = ((t9 == ((int32_t)0LL)));
        t1 = t10;
    } else t1 = 0;
    F.ret = t1;
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
inline __attribute__((aligned(4))) uint32_t gm_f_fresh_adt_u9rnf9f(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_fresh_adt_u9rnf9f F;
    void * gm_l0; /* t */
    gm_b16_8 gm_l1; /* name */
    uint32_t gm_l2; /* id */
    GM_STACK_CHECK(&F, 117u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uintptr_t, ((((uint8_t *)t2) + 136) + 0) + 8);
        uint32_t t4 = (uint32_t)t3;
        gm_l2 = t4;
    }
    {
        void * t5 = gm_l0;
        void * t6 = (void *)(((uint8_t *)t5) + 136);
        gm_b16_8 t7 = gm_l1;
        gm_tftb1odc739t5 t8 = gm_f_push_3ef746b(t6, t7);
        if (((((uint8_t *)&t8))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t8) + 1, gm_src0ih77f43mhf2, 263, 5);
        gm_b0_1 t9 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
    }
    {
        void * t10 = gm_l0;
        void * t11 = (void *)(((uint8_t *)t10) + 176);
        uint32_t t12 = gm_l2;
        gm_tftb1odc739t5 t13 = gm_f_push_im9d4ro(t11, t12);
        if (((((uint8_t *)&t13))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t13) + 1, gm_src0ih77f43mhf2, 264, 5);
        gm_b0_1 t14 = GM_LD(gm_b0_1, (((uint8_t *)&t13) + 1));
    }
    {
        uint32_t t15 = gm_l2;
        uint8_t t16 = 0;
        F.ret = t15;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint32_t gm_f_fresh_param_442g4m9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_fresh_param_442g4m9 F;
    void * gm_l0; /* t */
    gm_b16_8 gm_l1; /* name */
    GM_STACK_CHECK(&F, 95u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 256);
        gm_b16_8 t4 = gm_l1;
        gm_tftb1odc739t5 t5 = gm_f_push_3ef746b(t3, t4);
        if (((((uint8_t *)&t5))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t5) + 1, gm_src0ih77f43mhf2, 286, 5);
        gm_b0_1 t6 = GM_LD(gm_b0_1, (((uint8_t *)&t5) + 1));
    }
    {
        void * t7 = gm_l0;
        uint64_t t8 = GM_LD(uintptr_t, ((((uint8_t *)t7) + 256) + 0) + 8);
        uint64_t r9; bool o10 = __builtin_sub_overflow(t8, ((uint64_t)1ULL), &r9);
        if (o10) gm_trap("integer overflow in `-`", gm_src0ih77f43mhf2, 287, 13);
        uint64_t t11 = r9;
        uint32_t t12 = (uint32_t)t11;
        uint8_t t13 = 0;
        F.ret = t12;
        goto gm_fret;
    }
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_int_ci6airg(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, uint16_t a2) {
    struct gm_fr_int_ci6airg F;
    void * gm_l0; /* t */
    uint8_t gm_l1; /* signed */
    uint16_t gm_l2; /* bits */
    GM_STACK_CHECK(&F, 94u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t1 = gm_l0;
    uint8_t t2 = gm_l1;
    uint16_t t3 = gm_l2;
    gm_tjcj4j2gbh6c8 t4;
    GM_ST(uint8_t, (uint8_t *)&t4 + 1, 0);
    GM_ST(uint8_t, ((uint8_t *)&t4 + 0), t2);
    GM_ST(uint16_t, ((uint8_t *)&t4 + 2), t3);
    gm_t53at80pfdus3 t5;
    memset(&t5, 0, sizeof t5);
    GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)0ULL));
    GM_ST(gm_tjcj4j2gbh6c8, ((uint8_t *)&t5 + 8), t4);
    gm_t88rcucg1b7uk t6 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t1, &t5);
    if (gm_unw.kind) {
        goto gm_lad;
    }
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_integer_8d4f9ek(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_integer_8d4f9ek F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    GM_STACK_CHECK_SHALLOW(&F, 86u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t1 = gm_l0;
    gm_t88rcucg1b7uk t2 = gm_l1;
    gm_t53at80pfdus3 t3;
    gm_f_kind_jcnt4hm((void *)&t3, t1, GM_CONV(gm_csguankko4lmhd, t2));
    uint8_t t4;
    uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&t3) + 0));
    if (t9 != ((uint8_t)0ULL)) goto pa8;
    goto po7;
pa8:;
    uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)&t3) + 0));
    if (t11 != ((uint8_t)1ULL)) goto pa10;
    goto po7;
pa10:;
    uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)&t3) + 0));
    if (t13 != ((uint8_t)2ULL)) goto pa12;
    goto po7;
pa12:;
    goto mn6;
po7:;
    t4 = ((uint8_t)1);
    goto me5;
mn6:;
    t4 = ((uint8_t)0);
    goto me5;
mn14:;
    gm_trap("no match arm matched the scrutinee", gm_src0ih77f43mhf2, 374, 58);
me5:;
    F.ret = t4;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_negative_k87pp3n(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_negative_k87pp3n F;
    GM_STACK_CHECK_SHALLOW(&F, 3u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint8_t t1 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
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
inline __attribute__((aligned(4))) void gm_f_list_084fcd6(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_084fcd6 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_92arfe8();
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
inline __attribute__((aligned(4))) void gm_f_list_5kg5cjm_2(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_5kg5cjm_2 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_3jt8vgk_2();
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
inline __attribute__((aligned(4))) void gm_f_list_au01ire(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_au01ire F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_f1ngfj0();
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
inline __attribute__((aligned(4))) void gm_f_list_g6uul2f(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_g6uul2f F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_hu049sk();
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
inline __attribute__((aligned(4))) void gm_f_list_gst7tuh(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_gst7tuh F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_c06andg();
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
inline __attribute__((aligned(4))) void gm_f_list_ja7tv3o(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_ja7tv3o F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_dijpe9t();
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
inline __attribute__((aligned(4))) void gm_f_list_ol3ji66(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_ol3ji66 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_evi8iki();
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
inline __attribute__((aligned(4))) void gm_f_list_pmp06kq_2(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_pmp06kq_2 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_8q3kspo_2();
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
inline __attribute__((aligned(4))) void gm_f_list_spqn36o(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_spqn36o F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_ose1ba9();
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
inline __attribute__((aligned(4))) void gm_f_list_vmvl81b(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_vmvl81b F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_fh7svmj();
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_many_ptr_l4v11f8(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_many_ptr_l4v11f8 F;
    void * gm_l0; /* t */
    uint8_t gm_l1; /* is_const */
    gm_t88rcucg1b7uk gm_l2; /* elem */
    GM_STACK_CHECK(&F, 102u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t1 = gm_l0;
    uint8_t t2 = gm_l1;
    gm_t88rcucg1b7uk t3 = gm_l2;
    gm_tbngattl1fo51 t4;
    GM_ST(uint8_t, (uint8_t *)&t4 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t4 + 2, 0);
    GM_ST(uint8_t, ((uint8_t *)&t4 + 0), t2);
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t4 + 4), t3);
    gm_t53at80pfdus3 t5;
    memset(&t5, 0, sizeof t5);
    GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)9ULL));
    GM_ST(gm_tbngattl1fo51, ((uint8_t *)&t5 + 8), t4);
    gm_t88rcucg1b7uk t6 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t1, &t5);
    if (gm_unw.kind) {
        goto gm_lad;
    }
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
inline __attribute__((aligned(4))) void gm_f_neg_agd5c5k(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_neg_agd5c5k F;
    GM_STACK_CHECK(&F, 12u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    {
        gm_f_copy_pmkg67r((void *)&F.l1, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l0)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint8_t t2 = 0;
        uint8_t t3 = GM_LD(uint8_t, (((uint8_t *)&F.l0) + 0));
        uint8_t t4 = (uint8_t)!t3;
        if (t4) {
            uint64_t t5 = GM_LD(uintptr_t, ((((uint8_t *)&F.l0) + 8) + 0) + 8);
            uint8_t t6 = (t5 > ((uint64_t)0ULL));
            t2 = t6;
        } else t2 = 0;
        GM_ST(uint8_t, (((uint8_t *)&F.l1) + 0), t2);
    }
    {
        memmove(gm_out, ((uint8_t *)&F.l1), 48);
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
inline __attribute__((aligned(4))) void gm_f_one_08qg1op(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_one_08qg1op F;
    GM_STACK_CHECK(&F, 0u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_f_from_u64_ud13q5v((void *)&(*(gm_tbci5ospk5nnn *)gm_out), (void *)NULL, cx, NULL, ((uint64_t)1ULL));
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_optional_rh6okj3(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_optional_rh6okj3 F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* inner */
    GM_STACK_CHECK(&F, 92u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    gm_t88rcucg1b7uk t2 = gm_l1;
    gm_t53at80pfdus3 t3;
    memset(&t3, 0, sizeof t3);
    GM_ST(uint8_t, ((uint8_t *)&t3 + 0), ((uint8_t)13ULL));
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t3 + 8), t2);
    gm_t88rcucg1b7uk t4 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t1, &t3);
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_param_n8kgsdm(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_param_n8kgsdm F;
    void * gm_l0; /* t */
    uint32_t gm_l1; /* id */
    GM_STACK_CHECK(&F, 92u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    uint32_t t2 = gm_l1;
    gm_t53at80pfdus3 t3;
    memset(&t3, 0, sizeof t3);
    GM_ST(uint8_t, ((uint8_t *)&t3 + 0), ((uint8_t)19ULL));
    GM_ST(uint32_t, ((uint8_t *)&t3 + 8), t2);
    gm_t88rcucg1b7uk t4 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t1, &t3);
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
inline __attribute__((aligned(4))) uint32_t gm_f_ptr_bits_glps5i7(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_ptr_bits_glps5i7 F;
    void * gm_l0; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    uint64_t t2 = GM_LD(uint64_t, (((uint8_t *)t1) + 0));
    uint64_t r3; bool o4 = __builtin_mul_overflow(t2, ((uint64_t)8ULL), &r3);
    if (o4) gm_trap("integer overflow in `*`", gm_src4o48ksu43djd, 91, 49);
    uint64_t t5 = r3;
    uint32_t t6 = (uint32_t)t5;
    F.ret = t6;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_ptr_t1h2m2d(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_ptr_t1h2m2d F;
    void * gm_l0; /* t */
    uint8_t gm_l1; /* is_const */
    gm_t88rcucg1b7uk gm_l2; /* pointee */
    GM_STACK_CHECK(&F, 102u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t1 = gm_l0;
    uint8_t t2 = gm_l1;
    gm_t88rcucg1b7uk t3 = gm_l2;
    gm_tbngattl1fo51 t4;
    GM_ST(uint8_t, (uint8_t *)&t4 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t4 + 2, 0);
    GM_ST(uint8_t, ((uint8_t *)&t4 + 0), t2);
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t4 + 4), t3);
    gm_t53at80pfdus3 t5;
    memset(&t5, 0, sizeof t5);
    GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)8ULL));
    GM_ST(gm_tbngattl1fo51, ((uint8_t *)&t5 + 8), t4);
    gm_t88rcucg1b7uk t6 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t1, &t5);
    if (gm_unw.kind) {
        goto gm_lad;
    }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_push_type_alias_rr9mg3i(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_push_type_alias_rr9mg3i F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l1; /* name */
    gm_t88rcucg1b7uk gm_l2; /* ty */
    GM_STACK_CHECK(&F, 90u);
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
        if (((((uint8_t *)&t7))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t7) + 1, gm_srcsffver18si54, 63, 5);
        gm_b0_1 t8 = GM_LD(gm_b0_1, (((uint8_t *)&t7) + 1));
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_remove_in_progress_lrie478(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_remove_in_progress_lrie478 F;
    void * gm_l0; /* s */
    uint32_t gm_l1; /* id */
    uint64_t gm_l2; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 116u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        gm_b16_8 t4 = GM_LD(gm_b16_8, ((((uint8_t *)t3) + 1728) + 0));
        gm_b16_8 t5 = GM_LD(gm_b16_8, ((uint8_t *)&t4));
        uint32_t t6 = gm_l1;
        gm_tf5srh4mp4b3i t7 = gm_f_index_of_41b084m(t5, t6);
        if (!((((uint8_t *)&t7))[8] != 0)) goto ie2;
        uint64_t t8 = GM_LD(uint64_t, ((uint8_t *)&t7));
        gm_l2 = t8;
        {
            {
                void * t11 = gm_l0;
                void * t12 = (void *)(((uint8_t *)t11) + 1728);
                uint64_t t13 = gm_l2;
                uint32_t t14 = gm_f_remove_10muhi8(t12, t13);
            }
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_result_o9ras10(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_result_o9ras10 F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* err */
    gm_t88rcucg1b7uk gm_l2; /* ok */
    GM_STACK_CHECK(&F, 108u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t1 = gm_l0;
    gm_t88rcucg1b7uk t2 = gm_l1;
    gm_t88rcucg1b7uk t3 = gm_l2;
    gm_tgglga8ro3isc t4;
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t4 + 0), t2);
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t4 + 4), t3);
    gm_t53at80pfdus3 t5;
    memset(&t5, 0, sizeof t5);
    GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)14ULL));
    GM_ST(gm_tgglga8ro3isc, ((uint8_t *)&t5 + 8), t4);
    gm_t88rcucg1b7uk t6 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t1, &t5);
    if (gm_unw.kind) {
        goto gm_lad;
    }
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
inline __attribute__((aligned(4))) void gm_f_scope_mark_i3b2mvo(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_scope_mark_i3b2mvo F;
    void * gm_l0; /* s */
    GM_STACK_CHECK_SHALLOW(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    uint64_t t2 = GM_LD(uintptr_t, ((((uint8_t *)t1) + 1488) + 0) + 8);
    void * t3 = gm_l0;
    uint64_t t4 = GM_LD(uintptr_t, ((((uint8_t *)t3) + 1528) + 0) + 8);
    void * t5 = gm_l0;
    uint64_t t6 = GM_LD(uintptr_t, ((((uint8_t *)t5) + 1608) + 0) + 8);
    void * t7 = gm_l0;
    uint64_t t8 = GM_LD(uintptr_t, ((((uint8_t *)t7) + 1568) + 0) + 8);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_t66j1im9ip128 *)gm_out) + 0, t2);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_t66j1im9ip128 *)gm_out) + 8, t4);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_t66j1im9ip128 *)gm_out) + 16, t6);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_t66j1im9ip128 *)gm_out) + 24, t8);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_set_state_3j300fi(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2) {
    struct gm_fr_set_state_3j300fi F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* id */
    GM_STACK_CHECK(&F, 496u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    memcpy(&F.l2, a2, sizeof F.l2);
    {
        {
            void * t3 = gm_l0;
            uint64_t t4 = gm_l1;
            gm_tagrkbqh054h7 t5;
            gm_f_state_a5bnsjb((void *)&t5, (void *)NULL, cx, NULL, t3, t4);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint64_t t6 = gm_l1;
        uint64_t t7 = (uintptr_t)t6;
        void * t8 = gm_l0;
        uint64_t t9 = GM_LD(uintptr_t, ((((uint8_t *)t8) + 304) + 0) + 8);
        if (t7 >= (uintptr_t)(t9)) gm_trap_index(t7, t9, gm_srclv0dcbbfhves, 752, 5);
        gm_tagrkbqh054h7 t10 = F.l2;
        GM_ST(gm_tagrkbqh054h7, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t8) + 304) + 0)) + t7 * 216), t10);
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
inline __attribute__((aligned(4))) void gm_f_set_template_o09q96o(void * a0, uint32_t a1, uint32_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_set_template_o09q96o F;
    void * gm_l0; /* t */
    uint32_t gm_l1; /* id */
    uint32_t gm_l2; /* template */
    GM_STACK_CHECK_SHALLOW(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        uint32_t t2 = gm_l1;
        uint64_t t3 = (uint64_t)t2;
        uint64_t t4 = (uintptr_t)t3;
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uintptr_t, ((((uint8_t *)t5) + 176) + 0) + 8);
        if (t4 >= (uintptr_t)(t6)) gm_trap_index(t4, t6, gm_src0ih77f43mhf2, 272, 5);
        uint32_t t7 = gm_l2;
        GM_ST(uint32_t, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t5) + 176) + 0)) + t4 * 4), t7);
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_slice_5dt9121(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_slice_5dt9121 F;
    void * gm_l0; /* t */
    uint8_t gm_l1; /* is_const */
    gm_t88rcucg1b7uk gm_l2; /* elem */
    GM_STACK_CHECK(&F, 102u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t1 = gm_l0;
    uint8_t t2 = gm_l1;
    gm_t88rcucg1b7uk t3 = gm_l2;
    gm_tbngattl1fo51 t4;
    GM_ST(uint8_t, (uint8_t *)&t4 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t4 + 2, 0);
    GM_ST(uint8_t, ((uint8_t *)&t4 + 0), t2);
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t4 + 4), t3);
    gm_t53at80pfdus3 t5;
    memset(&t5, 0, sizeof t5);
    GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)10ULL));
    GM_ST(gm_tbngattl1fo51, ((uint8_t *)&t5 + 8), t4);
    gm_t88rcucg1b7uk t6 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t1, &t5);
    if (gm_unw.kind) {
        goto gm_lad;
    }
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_to_decimal_8bshn5b(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_to_decimal_8bshn5b F;
    GM_STACK_CHECK(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b0_1 t3 = gm_f_write_decimal_ke367ab((void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l0)), t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t4 = (void *)((uint8_t *)&F.l1);
        gm_tabb710d9mq2e t5;
        gm_f_into_slice_hq0te56((void *)&t5, t4);
        if (((((uint8_t *)&t5))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t5) + 8, gm_srclv3pj5es0f8o, 624, 12);
        gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)&t5) + 8));
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
inline __attribute__((aligned(4))) void gm_f_to_u128_haka99r(void *gm_out, const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_to_u128_haka99r F;
    GM_STACK_CHECK_SHALLOW(&F, 34u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        uint8_t t3 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
        if (!t3) goto ie2;
        {
            gm_t9p38b040grms t5;
            memset(&t5, 0, sizeof t5);
            uint8_t t6 = 0;
            F.ret = t5;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_f_mag_to_u128_f843dal((void *)&(*(gm_t9p38b040grms *)gm_out), &(*(gm_tbci5ospk5nnn *)((uint8_t *)a0)));
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_to_u64_m01t1da(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_to_u64_m01t1da F;
    unsigned __int128 gm_l1; /* v */
    GM_STACK_CHECK_SHALLOW(&F, 202u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_t9p38b040grms t2;
        gm_f_to_u128_haka99r((void *)&t2, &(*(gm_tbci5ospk5nnn *)((uint8_t *)a0)));
        unsigned __int128 t3;
        if (((((uint8_t *)&t2))[16] != 0)) {
            unsigned __int128 t4 = GM_LD(unsigned __int128, (((uint8_t *)&t2) + 0));
            t3 = t4;
        } else {
            gm_tf5srh4mp4b3i t5;
            memset(&t5, 0, sizeof t5);
            uint8_t t6 = 0;
            F.ret = t5;
            goto gm_fret;
        }
        gm_l1 = t3;
    }
    {
        unsigned __int128 t7 = gm_l1;
        gm_tf5srh4mp4b3i t8;
        uint64_t t9 = (uint64_t)t7;
        gm_tf5srh4mp4b3i t10;
        GM_ST(uint8_t, (uint8_t *)&t10 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t10 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t10 + 12, 0);
        GM_ST(uint64_t, ((uint8_t *)&t10), t9);
        ((uint8_t *)&t10)[8] = 1;
        gm_tf5srh4mp4b3i t11;
        memset(&t11, 0, sizeof t11);
        t8 = ((unsigned __int128)(t7) <= (((unsigned __int128)0ULL << 64) | 18446744073709551615ULL)) ? t10 : t11;
        uint8_t t12 = 0;
        F.ret = t8;
        goto gm_fret;
    }
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
            if (t12 > t14 || t14 > (uintptr_t)t10) gm_trap_slice(t12, t14, t10, gm_srcrgc9bkquf6nr, 122, 36);
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
            if (t12 > t14 || t14 > (uintptr_t)t10) gm_trap_slice(t12, t14, t10, gm_srcrgc9bkquf6nr, 122, 36);
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
            if (t12 > t14 || t14 > (uintptr_t)t10) gm_trap_slice(t12, t14, t10, gm_srcrgc9bkquf6nr, 122, 36);
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
            if (t12 > t14 || t14 > (uintptr_t)t10) gm_trap_slice(t12, t14, t10, gm_srcrgc9bkquf6nr, 122, 36);
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_u8_ty_afkvj25(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_u8_ty_afkvj25 F;
    void * gm_l0; /* t */
    GM_STACK_CHECK(&F, 28u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_t88rcucg1b7uk t2 = gm_f_int_ci6airg((void *)NULL, cx, NULL, t1, ((uint8_t)0), ((uint16_t)8ULL));
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
inline __attribute__((aligned(4))) void gm_f_zero_s25l990(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_zero_s25l990 F;
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memset((uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 1, 0, 7);
    gm_trqfvmjtt4sij t1;
    gm_f_list_go2qg0b((void *)&t1, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(uint8_t, (uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 0, ((uint8_t)0));
    GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 8, t1);
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
inline __attribute__((aligned(4))) uint8_t gm_f_adt_mentions_param_0jsjgma(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_adt_mentions_param_0jsjgma F;
    void * gm_l0; /* s */
    uint32_t gm_l1; /* id */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* i */
    gm_t88rcucg1b7uk gm_l6; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 238u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint32_t t2 = gm_l1;
        uint64_t t3 = (uint64_t)t2;
        uint64_t t4 = (uintptr_t)t3;
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uintptr_t, ((((uint8_t *)t5) + 1088) + 0) + 8);
        if (t4 >= (uintptr_t)(t6)) gm_trap_index(t4, t6, gm_src3k278vcmqgdr, 73, 13);
        gm_ti0kgh3pc3in2 t7 = GM_LD(gm_ti0kgh3pc3in2, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t5) + 1088) + 0)) + t4 * 184) + 24));
        F.l2 = t7;
    }
    {
        {
            gm_l4 = ((uint64_t)0ULL);
        }
        {
            uint64_t t9 = gm_f_member_count_otu9tc0(&(*(gm_ti0kgh3pc3in2 *)((uint8_t *)&F.l2)));
            gm_l3 = t9;
        }
        {
            for (;;) {
                uint64_t t11 = gm_l4;
                uint64_t t12 = gm_l3;
                uint8_t t13 = (t11 < t12);
                if (!t13) goto lb10;
                {
                    uint64_t t16 = gm_l4;
                    uint64_t t19 = GM_LD(uint64_t, ((uint8_t *)&t16));
                    gm_l5 = t19;
                    {
                        uint64_t t21 = gm_l5;
                        gm_tkn42u7na3t81 t22 = gm_f_member_ty_esvuguv(&(*(gm_ti0kgh3pc3in2 *)((uint8_t *)&F.l2)), t21);
                        gm_t88rcucg1b7uk t23;
                        if (((((uint8_t *)&t22))[4] != 0)) {
                            gm_t88rcucg1b7uk t24 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t22) + 0));
                            t23 = t24;
                        } else {
                            goto lc10;
                        }
                        gm_l6 = t23;
                    }
                    {
                        void * t26 = gm_l0;
                        void * t27 = (void *)(((uint8_t *)t26) + 8);
                        gm_t88rcucg1b7uk t28 = gm_l6;
                        uint8_t t29 = gm_f_has_param_u9t0efs(t27, GM_CONV(gm_csguankko4lmhd, t28));
                        if (!t29) goto ie25;
                        {
                            uint8_t t31 = 0;
                            F.ret = ((uint8_t)1);
                            goto gm_fret;
                        }
                        goto ix25;
ie25:;
ix25:;
                    }
                    goto me17;
mn18:;
                    gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 74, 5);
me17:;
                }
lc10:;
                {
                    uint64_t t33 = gm_l4;
                    uint64_t r34; bool o35 = __builtin_add_overflow(t33, ((uint64_t)1ULL), &r34);
                    uint64_t t36 = r34;
                    gm_l4 = t36;
                }
            }
lb10:;
        }
    }
    {
        uint8_t t37 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_attr_power_of_two_gj02n2t(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2) {
    struct gm_fr_attr_power_of_two_gj02n2t F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* a */
    gm_b16_8 gm_l3; /* args */
    void * gm_l4; /* arg */
    uint64_t gm_l8; /* n */
    GM_STACK_CHECK(&F, 965u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l2;
        gm_th2lqbo77pvsc t3 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)t2) + 32));
        gm_b16_8 t4;
        if (((((uint8_t *)&t3))[16] != 0)) {
            gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&t3) + 0));
            t4 = t5;
        } else {
            gm_b16_8 t6 = gm_f_empty_slice_g3tddrb();
            t4 = t6;
        }
        gm_l3 = t4;
    }
    {
        uint64_t t8 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        uint8_t t9 = (!(t8 == ((uint64_t)1ULL)));
        if (!t9) goto ie7;
        {
            void * t11 = gm_l0;
            void * t12 = gm_l2;
            gm_tl874t2atnlh3 t13 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t12) + 56));
            gm_b16_8 t14;
            GM_ST(void *, (uint8_t *)&t14, (void *)(gm_sbse0oko82bp9));
            GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(41));
            gm_b0_1 t15 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t11, t13, t14);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_tf5srh4mp4b3i t16;
            memset(&t16, 0, sizeof t16);
            uint8_t t17 = 0;
            F.ret = t16;
            goto gm_fret;
        }
        goto ix7;
ie7:;
ix7:;
    }
    {
        uint64_t t18 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t19 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        if (t18 >= (uintptr_t)(t19)) gm_trap_index(t18, t19, gm_src3k278vcmqgdr, 449, 13);
        void * t20 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t18 * 88);
        gm_l4 = t20;
    }
    {
        void * t22 = gm_l0;
        uint64_t t23 = gm_l1;
        void * t24 = gm_l4;
        gm_tkv1vap90vht2 t25;
        gm_f_try_untyped_fj0h8i5((void *)&t25, (void *)NULL, cx, NULL, t22, t23, t24);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!((((uint8_t *)&t25))[208] != 0)) goto ie21;
        F.l5 = (*(gm_t5vbkdq81qfs7 *)(((uint8_t *)&t25) + 0));
        {
            uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)&F.l5) + 0));
            if (t28 != ((uint8_t)0ULL)) goto ie27;
            gm_t0ialelq3ql1k t29 = GM_LD(gm_t0ialelq3ql1k, (((uint8_t *)&F.l5) + 8));
            F.l6 = t29;
            uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)&F.l6) + 0));
            if (t30 != ((uint8_t)0ULL)) goto ie27;
            gm_tbci5ospk5nnn t31 = GM_LD(gm_tbci5ospk5nnn, (((uint8_t *)&F.l6) + 8));
            F.l7 = t31;
            {
                gm_tf5srh4mp4b3i t34 = gm_f_to_u64_m01t1da(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l7)));
                if (!((((uint8_t *)&t34))[8] != 0)) goto ie33;
                uint64_t t35 = GM_LD(uint64_t, ((uint8_t *)&t34));
                gm_l8 = t35;
                {
                    uint64_t t38 = gm_l8;
                    uint8_t t39 = (t38 > ((uint64_t)0ULL));
                    if (!t39) goto ie37;
                    uint64_t t40 = gm_l8;
                    uint64_t t41 = gm_l8;
                    uint64_t r42; bool o43 = __builtin_sub_overflow(t41, ((uint64_t)1ULL), &r42);
                    if (o43) gm_trap("integer overflow in `-`", gm_src3k278vcmqgdr, 453, 35);
                    uint64_t t44 = r42;
                    uint64_t t45 = (uint64_t)(t40 & t44);
                    uint8_t t46 = ((t45 == ((uint64_t)0ULL)));
                    if (!t46) goto ie37;
                    {
                        uint64_t t48 = gm_l8;
                        gm_tf5srh4mp4b3i t49;
                        GM_ST(uint8_t, (uint8_t *)&t49 + 9, 0);
                        GM_ST(uint16_t, (uint8_t *)&t49 + 10, 0);
                        GM_ST(uint32_t, (uint8_t *)&t49 + 12, 0);
                        GM_ST(uint64_t, ((uint8_t *)&t49), t48);
                        ((uint8_t *)&t49)[8] = 1;
                        uint8_t t50 = 0;
                        F.ret = t49;
                        goto gm_fret;
                    }
                    goto ix37;
ie37:;
ix37:;
                }
                goto ix33;
ie33:;
ix33:;
            }
            {
                void * t51 = gm_l0;
                void * t52 = gm_l4;
                gm_tl874t2atnlh3 t53 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t52) + 72));
                gm_b16_8 t54;
                GM_ST(void *, (uint8_t *)&t54, (void *)(gm_slpp558cs8j3v));
                GM_ST(uintptr_t, (uint8_t *)&t54 + 8, (uintptr_t)(35));
                gm_b0_1 t55 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t51, t53, t54);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                gm_tf5srh4mp4b3i t56;
                memset(&t56, 0, sizeof t56);
                uint8_t t57 = 0;
                F.ret = t56;
                goto gm_fret;
            }
            goto ix27;
ie27:;
ix27:;
        }
        goto ix21;
ie21:;
ix21:;
    }
    {
        void * t58 = gm_l0;
        void * t59 = gm_l4;
        gm_tl874t2atnlh3 t60 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t59) + 72));
        gm_b16_8 t61;
        GM_ST(void *, (uint8_t *)&t61, (void *)(gm_shncvaenjonsu));
        GM_ST(uintptr_t, (uint8_t *)&t61 + 8, (uintptr_t)(40));
        gm_b0_1 t62 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t58, t60, t61);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_tf5srh4mp4b3i t63;
        memset(&t63, 0, sizeof t63);
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
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_bit_width_b7ofh2h(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_bit_width_b7ofh2h F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* t */
    uint16_t gm_l2; /* bits */
    uint32_t gm_l3; /* id */
    void * gm_l4; /* info */
    gm_t88rcucg1b7uk gm_l6; /* u */
    gm_t88rcucg1b7uk gm_l7; /* tag */
    gm_b16_8 gm_l8; /* variants */
    gm_b16_8 gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    GM_STACK_CHECK(&F, 674u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 8);
        gm_t88rcucg1b7uk t4 = gm_l1;
        gm_t53at80pfdus3 t5;
        gm_f_kind_jcnt4hm((void *)&t5, t3, GM_CONV(gm_csguankko4lmhd, t4));
        uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        switch (t12) { case ((uint8_t)0ULL): goto ma8; case ((uint8_t)1ULL): goto ma10; case ((uint8_t)2ULL): goto ma10; case ((uint8_t)3ULL): goto ma10; case ((uint8_t)4ULL): goto ma7; case ((uint8_t)5ULL): goto ma10; case ((uint8_t)6ULL): goto ma10; case ((uint8_t)7ULL): goto ma10; case ((uint8_t)8ULL): goto ma10; case ((uint8_t)9ULL): goto ma10; case ((uint8_t)10ULL): goto ma10; case ((uint8_t)11ULL): goto ma10; case ((uint8_t)12ULL): goto ma10; case ((uint8_t)13ULL): goto ma10; case ((uint8_t)14ULL): goto ma10; case ((uint8_t)15ULL): goto ma10; case ((uint8_t)16ULL): goto ma10; case ((uint8_t)17ULL): goto ma9; case ((uint8_t)18ULL): goto ma10; case ((uint8_t)19ULL): goto ma10; case ((uint8_t)20ULL): goto ma10; case ((uint8_t)21ULL): goto ma10; case ((uint8_t)22ULL): goto ma10; case ((uint8_t)23ULL): goto ma10; case ((uint8_t)24ULL): goto ma10; case ((uint8_t)25ULL): goto ma10; case ((uint8_t)26ULL): goto ma10; default: goto ma10; }
ma7:;
        uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t14 != ((uint8_t)4ULL)) goto mn13;
        gm_tf5srh4mp4b3i t15;
        GM_ST(uint8_t, (uint8_t *)&t15 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t15 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t15 + 12, 0);
        GM_ST(uint64_t, ((uint8_t *)&t15), ((uint64_t)1ULL));
        ((uint8_t *)&t15)[8] = 1;
        uint8_t t16 = 0;
        F.ret = t15;
        goto gm_fret;
        goto me6;
mn13:;
ma8:;
        uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t18 != ((uint8_t)0ULL)) goto mn17;
        uint16_t t19 = GM_LD(uint16_t, ((((uint8_t *)&t5) + 8) + 2));
        gm_l2 = t19;
        uint16_t t20 = gm_l2;
        uint64_t t21 = (uint64_t)t20;
        gm_tf5srh4mp4b3i t22;
        GM_ST(uint8_t, (uint8_t *)&t22 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t22 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t22 + 12, 0);
        GM_ST(uint64_t, ((uint8_t *)&t22), t21);
        ((uint8_t *)&t22)[8] = 1;
        uint8_t t23 = 0;
        F.ret = t22;
        goto gm_fret;
        goto me6;
mn17:;
ma9:;
        uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t25 != ((uint8_t)17ULL)) goto mn24;
        uint32_t t26 = GM_LD(uint32_t, ((((uint8_t *)&t5) + 8) + 0));
        gm_l3 = t26;
        {
            uint32_t t28 = gm_l3;
            uint64_t t29 = (uint64_t)t28;
            uint64_t t30 = (uintptr_t)t29;
            void * t31 = gm_l0;
            uint64_t t32 = GM_LD(uintptr_t, ((((uint8_t *)t31) + 1088) + 0) + 8);
            if (t30 >= (uintptr_t)(t32)) gm_trap_index(t30, t32, gm_src3k278vcmqgdr, 283, 22);
            void * t33 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t31) + 1088) + 0)) + t30 * 184);
            gm_l4 = t33;
        }
        {
            void * t34 = gm_l4;
            uint8_t t40 = GM_LD(uint8_t, ((((uint8_t *)t34) + 24) + 0));
            switch (t40) { case ((uint8_t)0ULL): goto ma36; case ((uint8_t)1ULL): goto ma37; case ((uint8_t)2ULL): goto ma38; default: goto ma38; }
ma36:;
            uint8_t t42 = GM_LD(uint8_t, ((((uint8_t *)t34) + 24) + 0));
            if (t42 != ((uint8_t)0ULL)) goto mn41;
            gm_tpautr4br7h3l t43 = GM_LD(gm_tpautr4br7h3l, ((((uint8_t *)t34) + 24) + 8));
            F.l5 = t43;
            {
                gm_tkn42u7na3t81 t45 = GM_LD(gm_tkn42u7na3t81, (((uint8_t *)&F.l5) + 40));
                gm_t88rcucg1b7uk t46;
                if (((((uint8_t *)&t45))[4] != 0)) {
                    gm_t88rcucg1b7uk t47 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t45) + 0));
                    t46 = t47;
                } else {
                    gm_tf5srh4mp4b3i t48;
                    memset(&t48, 0, sizeof t48);
                    uint8_t t49 = 0;
                    F.ret = t48;
                    goto gm_fret;
                }
                gm_l6 = t46;
            }
            {
                void * t50 = gm_l0;
                gm_t88rcucg1b7uk t51 = gm_l6;
                gm_tf5srh4mp4b3i t52 = gm_f_bit_width_b7ofh2h(t50, GM_CONV(gm_csguankko4lmhd, t51));
                uint8_t t53 = 0;
                F.ret = t52;
                goto gm_fret;
            }
            goto me35;
mn41:;
ma37:;
            uint8_t t55 = GM_LD(uint8_t, ((((uint8_t *)t34) + 24) + 0));
            if (t55 != ((uint8_t)1ULL)) goto mn54;
            gm_t88rcucg1b7uk t56 = GM_LD(gm_t88rcucg1b7uk, (((((uint8_t *)t34) + 24) + 8) + 0));
            gm_l7 = t56;
            gm_b16_8 t57 = GM_LD(gm_b16_8, (((((uint8_t *)t34) + 24) + 8) + 8));
            gm_l8 = t57;
            {
                void * t60 = gm_l4;
                uint8_t t61 = GM_LD(uint8_t, (((uint8_t *)t60) + 152));
                uint8_t t62 = (uint8_t)!t61;
                if (!t62) goto ie59;
                {
                    gm_tf5srh4mp4b3i t64;
                    memset(&t64, 0, sizeof t64);
                    uint8_t t65 = 0;
                    F.ret = t64;
                    goto gm_fret;
                }
                goto ix59;
ie59:;
ix59:;
            }
            {
                {
                    gm_b16_8 t67 = gm_l8;
                    gm_l9 = t67;
                }
                {
                    gm_l10 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t69 = gm_l10;
                        uint64_t t70 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                        uint8_t t71 = (t69 < t70);
                        if (!t71) goto lb68;
                        {
                            uint64_t t74 = gm_l10;
                            uint64_t t75 = (uintptr_t)t74;
                            uint64_t t76 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                            gm_tckdobunaulee t79 = GM_LD(gm_tckdobunaulee, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t75 * 72));
                            F.l11 = t79;
                            {
                                uint8_t t82;
                                if ((((((uint8_t *)&F.l11) + 16))[4] != 0)) goto mn84;
                                t82 = ((uint8_t)0);
                                goto me83;
mn84:;
                                t82 = ((uint8_t)1);
                                goto me83;
mn85:;
                                gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 293, 28);
me83:;
                                if (!t82) goto ie81;
                                {
                                    gm_tf5srh4mp4b3i t87;
                                    memset(&t87, 0, sizeof t87);
                                    uint8_t t88 = 0;
                                    F.ret = t87;
                                    goto gm_fret;
                                }
                                goto ix81;
ie81:;
ix81:;
                            }
                            goto me77;
mn78:;
                            gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 292, 21);
me77:;
                        }
lc68:;
                        {
                            uint64_t t90 = gm_l10;
                            uint64_t r91; bool o92 = __builtin_add_overflow(t90, ((uint64_t)1ULL), &r91);
                            uint64_t t93 = r91;
                            gm_l10 = t93;
                        }
                    }
lb68:;
                }
            }
            {
                void * t94 = gm_l0;
                gm_t88rcucg1b7uk t95 = gm_l7;
                gm_tf5srh4mp4b3i t96 = gm_f_bit_width_b7ofh2h(t94, GM_CONV(gm_csguankko4lmhd, t95));
                uint8_t t97 = 0;
                F.ret = t96;
                goto gm_fret;
            }
            goto me35;
mn54:;
ma38:;
            gm_tf5srh4mp4b3i t99;
            memset(&t99, 0, sizeof t99);
            uint8_t t100 = 0;
            F.ret = t99;
            goto gm_fret;
            goto me35;
mn98:;
ma39:;
            gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 284, 13);
me35:;
        }
        goto me6;
mn24:;
ma10:;
        gm_tf5srh4mp4b3i t102;
        memset(&t102, 0, sizeof t102);
        uint8_t t103 = 0;
        F.ret = t102;
        goto gm_fret;
        goto me6;
mn101:;
ma11:;
        gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 279, 5);
me6:;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_bits_layout_u6kkccp(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_bits_layout_u6kkccp F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* t */
    uint32_t gm_l2; /* id */
    gm_t88rcucg1b7uk gm_l4; /* u */
    uint32_t gm_l5; /* off */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint32_t gm_l10; /* w */
    GM_STACK_CHECK(&F, 568u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_t88rcucg1b7uk t5 = gm_l1;
        gm_t53at80pfdus3 t6;
        gm_f_kind_jcnt4hm((void *)&t6, t4, GM_CONV(gm_csguankko4lmhd, t5));
        uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t7 != ((uint8_t)17ULL)) goto ie2;
        uint32_t t8 = GM_LD(uint32_t, ((((uint8_t *)&t6) + 8) + 0));
        gm_l2 = t8;
        {
            uint32_t t11 = gm_l2;
            uint64_t t12 = (uint64_t)t11;
            uint64_t t13 = (uintptr_t)t12;
            void * t14 = gm_l0;
            uint64_t t15 = GM_LD(uintptr_t, ((((uint8_t *)t14) + 1088) + 0) + 8);
            if (t13 >= (uintptr_t)(t15)) gm_trap_index(t13, t15, gm_src3k278vcmqgdr, 328, 20);
            uint8_t t16 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t14) + 1088) + 0)) + t13 * 184) + 24) + 0));
            if (t16 != ((uint8_t)0ULL)) goto ie10;
            gm_tpautr4br7h3l t17 = GM_LD(gm_tpautr4br7h3l, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t14) + 1088) + 0)) + t13 * 184) + 24) + 8));
            F.l3 = t17;
            {
                gm_tkn42u7na3t81 t19 = GM_LD(gm_tkn42u7na3t81, (((uint8_t *)&F.l3) + 40));
                gm_t88rcucg1b7uk t20;
                if (((((uint8_t *)&t19))[4] != 0)) {
                    gm_t88rcucg1b7uk t21 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t19) + 0));
                    t20 = t21;
                } else {
                    gm_t57euh612ligr t22;
                    memset(&t22, 0, sizeof t22);
                    uint8_t t23 = 0;
                    F.ret = t22;
                    goto gm_fret;
                }
                gm_l4 = t20;
            }
            {
                gm_l5 = ((uint32_t)0ULL);
            }
            {
                gm_f_list_ol3ji66((void *)&F.l6, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                {
                    gm_b16_8 t25 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
                    gm_l7 = t25;
                }
                {
                    gm_l8 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t27 = gm_l8;
                        uint64_t t28 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                        uint8_t t29 = (t27 < t28);
                        if (!t29) goto lb26;
                        {
                            uint64_t t32 = gm_l8;
                            uint64_t t33 = (uintptr_t)t32;
                            uint64_t t34 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                            gm_t0mglksrkfa5e t37 = GM_LD(gm_t0mglksrkfa5e, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t33 * 40));
                            F.l9 = t37;
                            {
                                void * t39 = gm_l0;
                                gm_t88rcucg1b7uk t40 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l9) + 16));
                                uint32_t t41 = gm_f_field_bits_2ktfm1k((void *)NULL, cx, NULL, t39, t40);
                                if (gm_unw.kind) {
                                    goto gm_ll30;
                                }
                                gm_l10 = t41;
                            }
                            {
                                void * t42 = (void *)((uint8_t *)&F.l6);
                                uint32_t t43 = gm_l5;
                                uint32_t t44 = gm_l10;
                                gm_t88rcucg1b7uk t45 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l9) + 16));
                                gm_tov4mtrfp5e7v t46;
                                GM_ST(uint32_t, ((uint8_t *)&t46 + 0), t43);
                                GM_ST(uint32_t, ((uint8_t *)&t46 + 4), t44);
                                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t46 + 8), t45);
                                gm_tftb1odc739t5 t47 = gm_f_push_p52mcnt(t42, GM_CONV(gm_csv4kcdnuj265c, t46));
                                if (((((uint8_t *)&t47))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t47) + 1, gm_src3k278vcmqgdr, 334, 9);
                                gm_b0_1 t48 = GM_LD(gm_b0_1, (((uint8_t *)&t47) + 1));
                            }
                            {
                                uint32_t t49 = gm_l5;
                                uint32_t t50 = gm_l10;
                                uint32_t r51; bool o52 = __builtin_add_overflow(t49, t50, &r51);
                                if (o52) gm_trap("integer overflow in `+`", gm_src3k278vcmqgdr, 335, 9);
                                uint32_t t53 = r51;
                                gm_l5 = t53;
                            }
                            goto me35;
mn36:;
                            gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 332, 5);
me35:;
                        }
lc26:;
                        {
                            uint64_t t55 = gm_l8;
                            uint64_t r56; bool o57 = __builtin_add_overflow(t55, ((uint64_t)1ULL), &r56);
                            uint64_t t58 = r56;
                            gm_l8 = t58;
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
            }
            {
                gm_tcmkompbl09il t59;
                memset((uint8_t *)&t59 + 4, 0, 4);
                gm_t88rcucg1b7uk t60 = gm_l4;
                GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t59 + 0, t60);
                gm_b16_8 t61 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
                gm_b16_8 t62 = GM_LD(gm_b16_8, ((uint8_t *)&t61));
                GM_ST(gm_b16_8, (uint8_t *)&t59 + 8, t62);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t57euh612ligr *)gm_out) + 25, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t57euh612ligr *)gm_out) + 26, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t57euh612ligr *)gm_out) + 28, 0);
                ((uint8_t *)&(*(gm_t57euh612ligr *)gm_out))[24] = 1;
                GM_ST(gm_tcmkompbl09il, (uint8_t *)&(*(gm_t57euh612ligr *)gm_out) + 0, t59);
                goto gm_fdone;
            }
            goto ix10;
ie10:;
            {
                gm_t57euh612ligr t64;
                memset(&t64, 0, sizeof t64);
                uint8_t t65 = 0;
                F.ret = t64;
                goto gm_fret;
            }
ix10:;
        }
        goto ix2;
ie2:;
        {
            gm_t57euh612ligr t67;
            memset(&t67, 0, sizeof t67);
            uint8_t t68 = 0;
            F.ret = t67;
            goto gm_fret;
        }
ix2:;
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
inline __attribute__((aligned(4))) void gm_f_c_union_fields_btdvc8b(void *gm_out, void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_c_union_fields_btdvc8b F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    uint32_t gm_l2; /* id */
    gm_b16_8 gm_l3; /* fields */
    GM_STACK_CHECK_SHALLOW(&F, 232u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    {
        void * t3 = gm_l0;
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_t88rcucg1b7uk t5 = gm_l1;
        gm_t53at80pfdus3 t6;
        gm_f_kind_jcnt4hm((void *)&t6, t4, GM_CONV(gm_csguankko4lmhd, t5));
        uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t7 != ((uint8_t)17ULL)) goto ie2;
        uint32_t t8 = GM_LD(uint32_t, ((((uint8_t *)&t6) + 8) + 0));
        gm_l2 = t8;
        {
            uint32_t t11 = gm_l2;
            uint64_t t12 = (uint64_t)t11;
            uint64_t t13 = (uintptr_t)t12;
            void * t14 = gm_l0;
            uint64_t t15 = GM_LD(uintptr_t, ((((uint8_t *)t14) + 1088) + 0) + 8);
            if (t13 >= (uintptr_t)(t15)) gm_trap_index(t13, t15, gm_src3k278vcmqgdr, 503, 24);
            uint8_t t16 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t14) + 1088) + 0)) + t13 * 184) + 24) + 0));
            if (t16 != ((uint8_t)2ULL)) goto ie10;
            gm_b16_8 t17 = GM_LD(gm_b16_8, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t14) + 1088) + 0)) + t13 * 184) + 24) + 8));
            gm_l3 = t17;
            {
                gm_b16_8 t19 = gm_l3;
                GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
                ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 0, t19);
                goto gm_fdone;
            }
            goto ix10;
ie10:;
            {
                gm_th2lqbo77pvsc t21;
                memset(&t21, 0, sizeof t21);
                uint8_t t22 = 0;
                F.ret = t21;
                goto gm_fret;
            }
ix10:;
        }
        goto ix2;
ie2:;
        {
            gm_th2lqbo77pvsc t24;
            memset(&t24, 0, sizeof t24);
            uint8_t t25 = 0;
            F.ret = t24;
            goto gm_fret;
        }
ix2:;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_th2lqbo77pvsc gm_f_c_union_fields_btdvc8b_c(void * a0, gm_csguankko4lmhd a1) { gm_th2lqbo77pvsc gm_r; gm_f_c_union_fields_btdvc8b(&gm_r, a0, a1); return gm_r; }
__attribute__((aligned(4))) gm_b0_1 gm_f_check_bits_struct_ophej5b(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3) {
    struct gm_fr_check_bits_struct_ophej5b F;
    void * gm_l0; /* s */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_t88rcucg1b7uk gm_l2; /* underlying */
    gm_b16_8 gm_l3; /* fields */
    gm_b16_8 gm_l4; /* msg */
    uint8_t gm_l5; /* signed */
    uint16_t gm_l6; /* width */
    uint64_t gm_l7; /* total */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    uint64_t gm_l11; /* w */
    GM_STACK_CHECK(&F, 815u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        gm_t88rcucg1b7uk t3 = gm_l2;
        gm_b16_8 t4 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t5;
        GM_ST(gm_b16_8, ((uint8_t *)&t5 + 0), t4);
        gm_tabb710d9mq2e t6;
        gm_f_format_a5p8et1((void *)&t6, (void *)NULL, cx, NULL, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t6))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t6) + 8, gm_src3k278vcmqgdr, 259, 12);
        gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&t6) + 8));
        gm_l4 = t7;
    }
    {
        void * t9 = gm_l0;
        void * t10 = (void *)(((uint8_t *)t9) + 8);
        gm_t88rcucg1b7uk t11 = gm_l2;
        gm_t53at80pfdus3 t12;
        gm_f_kind_jcnt4hm((void *)&t12, t10, GM_CONV(gm_csguankko4lmhd, t11));
        uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)&t12) + 0));
        if (t13 != ((uint8_t)0ULL)) goto ie8;
        uint8_t t14 = GM_LD(uint8_t, ((((uint8_t *)&t12) + 8) + 0));
        gm_l5 = t14;
        uint16_t t15 = GM_LD(uint16_t, ((((uint8_t *)&t12) + 8) + 2));
        gm_l6 = t15;
        {
            uint8_t t18 = gm_l5;
            if (!t18) goto ie17;
            {
                void * t20 = gm_l0;
                gm_tl874t2atnlh3 t21 = gm_l1;
                gm_b16_8 t22 = gm_l4;
                gm_b16_8 t23 = GM_LD(gm_b16_8, ((uint8_t *)&t22));
                gm_b0_1 t24 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t20, t21, t23);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix17;
ie17:;
ix17:;
        }
        {
            gm_l7 = ((uint64_t)0ULL);
        }
        {
            {
                gm_b16_8 t26 = gm_l3;
                gm_l8 = t26;
            }
            {
                gm_l9 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t28 = gm_l9;
                    uint64_t t29 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                    uint8_t t30 = (t28 < t29);
                    if (!t30) goto lb27;
                    {
                        uint64_t t33 = gm_l9;
                        uint64_t t34 = (uintptr_t)t33;
                        uint64_t t35 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                        gm_t0mglksrkfa5e t38 = GM_LD(gm_t0mglksrkfa5e, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l8)) + t34 * 40));
                        F.l10 = t38;
                        {
                            void * t41 = gm_l0;
                            gm_t88rcucg1b7uk t42 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l10) + 16));
                            gm_tf5srh4mp4b3i t43 = gm_f_bit_width_b7ofh2h(t41, GM_CONV(gm_csguankko4lmhd, t42));
                            if (!((((uint8_t *)&t43))[8] != 0)) goto ie40;
                            uint64_t t44 = GM_LD(uint64_t, ((uint8_t *)&t43));
                            gm_l11 = t44;
                            {
                                uint64_t t46 = gm_l7;
                                uint64_t t47 = gm_l11;
                                uint64_t r48; bool o49 = __builtin_add_overflow(t46, t47, &r48);
                                if (o49) gm_trap("integer overflow in `+`", gm_src3k278vcmqgdr, 268, 13);
                                uint64_t t50 = r48;
                                gm_l7 = t50;
                            }
                            goto ix40;
ie40:;
                            {
                                void * t52 = gm_l0;
                                gm_tl874t2atnlh3 t53 = gm_l1;
                                gm_b16_8 t54 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 0));
                                gm_tnpifv20irnph t55;
                                GM_ST(gm_b16_8, ((uint8_t *)&t55 + 0), t54);
                                gm_tabb710d9mq2e t56;
                                gm_f_format_bc9gr0b((void *)&t56, (void *)NULL, cx, NULL, t55);
                                if (gm_unw.kind) {
                                    goto gm_ll31;
                                }
                                if (((((uint8_t *)&t56))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t56) + 8, gm_src3k278vcmqgdr, 270, 27);
                                gm_b16_8 t57 = GM_LD(gm_b16_8, (((uint8_t *)&t56) + 8));
                                gm_b16_8 t58 = GM_LD(gm_b16_8, ((uint8_t *)&t57));
                                gm_b0_1 t59 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t52, t53, t58);
                                if (gm_unw.kind) {
                                    goto gm_ll31;
                                }
                            }
ix40:;
                        }
                        goto me36;
mn37:;
                        gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 266, 5);
me36:;
                    }
lc27:;
                    {
                        uint64_t t61 = gm_l9;
                        uint64_t r62; bool o63 = __builtin_add_overflow(t61, ((uint64_t)1ULL), &r62);
                        uint64_t t64 = r62;
                        gm_l9 = t64;
                    }
                }
gm_ll31:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb27; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc27; }
                }
                goto gm_lad;
lb27:;
            }
        }
        {
            uint64_t t66 = gm_l7;
            uint16_t t67 = gm_l6;
            uint64_t t68 = (uint64_t)t67;
            uint8_t t69 = (!(t66 == t68));
            if (!t69) goto ie65;
            {
                void * t71 = gm_l0;
                gm_tl874t2atnlh3 t72 = gm_l1;
                gm_t3568dh2osse0 t73;
                memset((uint8_t *)&t73 + 26, 0, 6);
                uint64_t t74 = gm_l7;
                GM_ST(uint64_t, (uint8_t *)&t73 + 0, t74);
                void * t75 = gm_l0;
                gm_t88rcucg1b7uk t76 = gm_l2;
                gm_b16_8 t77 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t75, t76);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t73 + 8, t77);
                uint16_t t78 = gm_l6;
                GM_ST(uint16_t, (uint8_t *)&t73 + 24, t78);
                gm_tabb710d9mq2e t79;
                gm_f_format_c5ago2c((void *)&t79, (void *)NULL, cx, NULL, &t73);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t79))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t79) + 8, gm_src3k278vcmqgdr, 274, 23);
                gm_b16_8 t80 = GM_LD(gm_b16_8, (((uint8_t *)&t79) + 8));
                gm_b16_8 t81 = GM_LD(gm_b16_8, ((uint8_t *)&t80));
                gm_b0_1 t82 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t71, t72, t81);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix65;
ie65:;
ix65:;
        }
        goto ix8;
ie8:;
        {
            {
                void * t85 = gm_l0;
                gm_tl874t2atnlh3 t86 = gm_l1;
                gm_b16_8 t87 = gm_l4;
                gm_b16_8 t88 = GM_LD(gm_b16_8, ((uint8_t *)&t87));
                gm_b0_1 t89 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t85, t86, t88);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                gm_b0_1 t90;
                uint8_t t91 = 0;
                F.ret = t90;
                goto gm_fret;
            }
        }
ix8:;
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
__attribute__((aligned(4))) uint8_t gm_f_contains_by_value_pcdi4kq(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, uint32_t a2, void * a3) {
    struct gm_fr_contains_by_value_pcdi4kq F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    uint32_t gm_l2; /* target_ */
    void * gm_l3; /* seen */
    uint32_t gm_l4; /* id */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* i */
    gm_t88rcucg1b7uk gm_l9; /* t */
    gm_t88rcucg1b7uk gm_l10; /* elem */
    gm_t88rcucg1b7uk gm_l11; /* elem */
    gm_t88rcucg1b7uk gm_l12; /* elem */
    gm_b16_8 gm_l13; /* elems */
    gm_b16_8 gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    gm_t88rcucg1b7uk gm_l16; /* e */
    gm_t88rcucg1b7uk gm_l17; /* err */
    gm_t88rcucg1b7uk gm_l18; /* ok */
    GM_STACK_CHECK(&F, 700u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 8);
        gm_t88rcucg1b7uk t4 = gm_l1;
        gm_t53at80pfdus3 t5;
        gm_f_kind_jcnt4hm((void *)&t5, t3, GM_CONV(gm_csguankko4lmhd, t4));
        uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        switch (t13) { case ((uint8_t)0ULL): goto ma11; case ((uint8_t)1ULL): goto ma11; case ((uint8_t)2ULL): goto ma11; case ((uint8_t)3ULL): goto ma11; case ((uint8_t)4ULL): goto ma11; case ((uint8_t)5ULL): goto ma11; case ((uint8_t)6ULL): goto ma11; case ((uint8_t)7ULL): goto ma11; case ((uint8_t)8ULL): goto ma11; case ((uint8_t)9ULL): goto ma11; case ((uint8_t)10ULL): goto ma11; case ((uint8_t)11ULL): goto ma8; case ((uint8_t)12ULL): goto ma9; case ((uint8_t)13ULL): goto ma8; case ((uint8_t)14ULL): goto ma10; case ((uint8_t)15ULL): goto ma11; case ((uint8_t)16ULL): goto ma11; case ((uint8_t)17ULL): goto ma7; case ((uint8_t)18ULL): goto ma11; case ((uint8_t)19ULL): goto ma11; case ((uint8_t)20ULL): goto ma8; case ((uint8_t)21ULL): goto ma11; case ((uint8_t)22ULL): goto ma11; case ((uint8_t)23ULL): goto ma11; case ((uint8_t)24ULL): goto ma11; case ((uint8_t)25ULL): goto ma11; case ((uint8_t)26ULL): goto ma11; default: goto ma11; }
ma7:;
        uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t15 != ((uint8_t)17ULL)) goto mn14;
        uint32_t t16 = GM_LD(uint32_t, ((((uint8_t *)&t5) + 8) + 0));
        gm_l4 = t16;
        {
            uint32_t t19 = gm_l4;
            uint32_t t20 = gm_l2;
            uint8_t t21 = ((t19 == t20));
            if (!t21) goto ie18;
            void * t22 = gm_l3;
            uint64_t t23 = GM_LD(uintptr_t, (((uint8_t *)t22) + 0) + 8);
            uint8_t t24 = (t23 > ((uint64_t)0ULL));
            if (!t24) goto ie18;
            {
                uint8_t t26 = 0;
                F.ret = ((uint8_t)1);
                goto gm_fret;
            }
            goto ix18;
ie18:;
ix18:;
        }
        {
            uint32_t t28 = gm_l4;
            void * t29 = gm_l3;
            gm_b16_8 t30 = GM_LD(gm_b16_8, (((uint8_t *)t29) + 0));
            uint8_t t31 = 0;
            for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t30) + 8)); i++) if (gm_eq467d5cm1d5(((uint8_t *)GM_LD(void *, ((uint8_t *)&t30))) + i * 4, ((uint8_t *)&t28))) { t31 = 1; break; }
            uint8_t t32 = (uint8_t)(t31);
            if (!t32) goto ie27;
            {
                uint8_t t34 = 0;
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            goto ix27;
ie27:;
ix27:;
        }
        {
            void * t35 = gm_l3;
            uint32_t t36 = gm_l4;
            gm_tftb1odc739t5 t37 = gm_f_push_im9d4ro(t35, t36);
            if (((((uint8_t *)&t37))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t37) + 1, gm_src3k278vcmqgdr, 650, 13);
            gm_b0_1 t38 = GM_LD(gm_b0_1, (((uint8_t *)&t37) + 1));
        }
        {
            uint32_t t39 = gm_l4;
            uint64_t t40 = (uint64_t)t39;
            uint64_t t41 = (uintptr_t)t40;
            void * t42 = gm_l0;
            uint64_t t43 = GM_LD(uintptr_t, ((((uint8_t *)t42) + 1088) + 0) + 8);
            if (t41 >= (uintptr_t)(t43)) gm_trap_index(t41, t43, gm_src3k278vcmqgdr, 651, 21);
            gm_ti0kgh3pc3in2 t44 = GM_LD(gm_ti0kgh3pc3in2, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t42) + 1088) + 0)) + t41 * 184) + 24));
            F.l5 = t44;
        }
        {
            {
                gm_l7 = ((uint64_t)0ULL);
            }
            {
                uint64_t t46 = gm_f_member_count_otu9tc0(&(*(gm_ti0kgh3pc3in2 *)((uint8_t *)&F.l5)));
                gm_l6 = t46;
            }
            {
                for (;;) {
                    uint64_t t48 = gm_l7;
                    uint64_t t49 = gm_l6;
                    uint8_t t50 = (t48 < t49);
                    if (!t50) goto lb47;
                    {
                        uint64_t t53 = gm_l7;
                        uint64_t t56 = GM_LD(uint64_t, ((uint8_t *)&t53));
                        gm_l8 = t56;
                        {
                            uint64_t t58 = gm_l8;
                            gm_tkn42u7na3t81 t59 = gm_f_member_ty_esvuguv(&(*(gm_ti0kgh3pc3in2 *)((uint8_t *)&F.l5)), t58);
                            gm_t88rcucg1b7uk t60;
                            if (((((uint8_t *)&t59))[4] != 0)) {
                                gm_t88rcucg1b7uk t61 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t59) + 0));
                                t60 = t61;
                            } else {
                                goto lc47;
                            }
                            gm_l9 = t60;
                        }
                        {
                            void * t63 = gm_l0;
                            gm_t88rcucg1b7uk t64 = gm_l9;
                            uint32_t t65 = gm_l2;
                            void * t66 = gm_l3;
                            uint8_t t67 = gm_f_contains_by_value_pcdi4kq((void *)NULL, cx, NULL, t63, t64, t65, t66);
                            if (gm_unw.kind) {
                                goto gm_ll51;
                            }
                            if (!t67) goto ie62;
                            {
                                uint8_t t69 = 0;
                                F.ret = ((uint8_t)1);
                                goto gm_fret;
                            }
                            goto ix62;
ie62:;
ix62:;
                        }
                        goto me54;
mn55:;
                        gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 652, 13);
me54:;
                    }
lc47:;
                    {
                        uint64_t t71 = gm_l7;
                        uint64_t r72; bool o73 = __builtin_add_overflow(t71, ((uint64_t)1ULL), &r72);
                        uint64_t t74 = r72;
                        gm_l7 = t74;
                    }
                }
gm_ll51:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb47; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc47; }
                }
                goto gm_lad;
lb47:;
            }
        }
        {
            uint8_t t75 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto me6;
mn14:;
ma8:;
        uint8_t t79 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t79 != ((uint8_t)11ULL)) goto pa78;
        gm_t88rcucg1b7uk t80 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t5) + 8) + 8));
        gm_l10 = t80;
        goto po77;
pa78:;
        uint8_t t82 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t82 != ((uint8_t)20ULL)) goto pa81;
        gm_t88rcucg1b7uk t83 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t5) + 8) + 4));
        gm_l10 = t83;
        goto po77;
pa81:;
        uint8_t t85 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t85 != ((uint8_t)13ULL)) goto pa84;
        gm_t88rcucg1b7uk t86 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t5) + 8));
        gm_l10 = t86;
        goto po77;
pa84:;
        goto mn76;
po77:;
        void * t87 = gm_l0;
        gm_t88rcucg1b7uk t88 = gm_l10;
        uint32_t t89 = gm_l2;
        void * t90 = gm_l3;
        uint8_t t91 = gm_f_contains_by_value_pcdi4kq((void *)NULL, cx, NULL, t87, t88, t89, t90);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t92 = 0;
        F.ret = t91;
        goto gm_fret;
        goto me6;
mn76:;
ma9:;
        uint8_t t94 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t94 != ((uint8_t)12ULL)) goto mn93;
        gm_b16_8 t95 = GM_LD(gm_b16_8, ((((uint8_t *)&t5) + 8) + 0));
        gm_l13 = t95;
        {
            {
                gm_b16_8 t98 = gm_l13;
                gm_l14 = t98;
            }
            {
                gm_l15 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t100 = gm_l15;
                    uint64_t t101 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                    uint8_t t102 = (t100 < t101);
                    if (!t102) goto lb99;
                    {
                        uint64_t t105 = gm_l15;
                        uint64_t t106 = (uintptr_t)t105;
                        uint64_t t107 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                        gm_t88rcucg1b7uk t110 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l14)) + t106 * 4));
                        gm_l16 = t110;
                        {
                            void * t113 = gm_l0;
                            gm_t88rcucg1b7uk t114 = gm_l16;
                            uint32_t t115 = gm_l2;
                            void * t116 = gm_l3;
                            uint8_t t117 = gm_f_contains_by_value_pcdi4kq((void *)NULL, cx, NULL, t113, t114, t115, t116);
                            if (gm_unw.kind) {
                                goto gm_ll103;
                            }
                            if (!t117) goto ie112;
                            {
                                uint8_t t119 = 0;
                                F.ret = ((uint8_t)1);
                                goto gm_fret;
                            }
                            goto ix112;
ie112:;
ix112:;
                        }
                        goto me108;
mn109:;
                        gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 660, 13);
me108:;
                    }
lc99:;
                    {
                        uint64_t t121 = gm_l15;
                        uint64_t r122; bool o123 = __builtin_add_overflow(t121, ((uint64_t)1ULL), &r122);
                        uint64_t t124 = r122;
                        gm_l15 = t124;
                    }
                }
gm_ll103:;
                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb99; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc99; }
                }
                goto gm_lad;
lb99:;
            }
        }
        {
            uint8_t t125 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto me6;
mn93:;
ma10:;
        uint8_t t127 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t127 != ((uint8_t)14ULL)) goto mn126;
        gm_t88rcucg1b7uk t128 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t5) + 8) + 0));
        gm_l17 = t128;
        gm_t88rcucg1b7uk t129 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t5) + 8) + 4));
        gm_l18 = t129;
        uint8_t t130 = 0;
        void * t131 = gm_l0;
        gm_t88rcucg1b7uk t132 = gm_l17;
        uint32_t t133 = gm_l2;
        void * t134 = gm_l3;
        uint8_t t135 = gm_f_contains_by_value_pcdi4kq((void *)NULL, cx, NULL, t131, t132, t133, t134);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t135) {
            void * t136 = gm_l0;
            gm_t88rcucg1b7uk t137 = gm_l18;
            uint32_t t138 = gm_l2;
            void * t139 = gm_l3;
            uint8_t t140 = gm_f_contains_by_value_pcdi4kq((void *)NULL, cx, NULL, t136, t137, t138, t139);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            t130 = t140;
        } else t130 = 1;
        uint8_t t141 = 0;
        F.ret = t130;
        goto gm_fret;
        goto me6;
mn126:;
ma11:;
        uint8_t t143 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
        goto me6;
mn142:;
ma12:;
        gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 646, 5);
me6:;
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
extern __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_declare_adt_iefkbkk(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_tf5srh4mp4b3i a2, gm_b16_8 a3, void * a4);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_declare_adt_in_74tov03(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_tf5srh4mp4b3i a2, gm_b16_8 a3, void * a4, uint8_t a5) {
    struct gm_fr_declare_adt_in_74tov03 F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    gm_tf5srh4mp4b3i gm_l2; /* decl */
    gm_b16_8 gm_l3; /* name */
    void * gm_l4; /* lit */
    uint8_t gm_l5; /* local */
    gm_b16_8 gm_l6; /* attrs */
    void * gm_l7; /* inner */
    gm_b16_8 gm_l8; /* a */
    void * gm_l9; /* i */
    uint32_t gm_l10; /* id */
    gm_t88rcucg1b7uk gm_l11; /* ty */
    void * gm_l12; /* st */
    void * gm_l13; /* u */
    gm_th2lqbo77pvsc gm_l14; /* params */
    gm_t66j1im9ip128 gm_l15; /* mark */
    gm_b16_8 gm_l18; /* <tmp> */
    uint64_t gm_l19; /* <tmp> */
    gm_tnnjeerolos0a gm_l21; /* kind */
    gm_b16_8 gm_l22; /* <tmp> */
    uint64_t gm_l23; /* <tmp> */
    gm_t08olq97rscn3 gm_l24; /* n */
    uint32_t gm_l25; /* pid */
    uint64_t gm_l26; /* d */
    gm_b16_8 gm_l29; /* wheres */
    void * gm_l30; /* st */
    void * gm_l31; /* u */
    gm_b16_8 gm_l32; /* <tmp> */
    uint64_t gm_l33; /* <tmp> */
    GM_STACK_CHECK(&F, 4080u);
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
        gm_b16_8 t2 = gm_f_empty_slice_atlcs4j();
        gm_l6 = t2;
    }
    {
        void * t3 = gm_l4;
        gm_l7 = t3;
    }
    {
        void * t5 = gm_l4;
        uint8_t t6 = GM_LD(uint8_t, ((((uint8_t *)t5) + 8) + 0));
        if (t6 != ((uint8_t)35ULL)) goto ie4;
        gm_b16_8 t7 = GM_LD(gm_b16_8, (((((uint8_t *)t5) + 8) + 8) + 0));
        gm_l8 = t7;
        void * t8 = GM_LD(void *, (((((uint8_t *)t5) + 8) + 8) + 16));
        gm_l9 = t8;
        {
            gm_b16_8 t10 = gm_l8;
            gm_l6 = t10;
        }
        {
            void * t11 = gm_l9;
            gm_l7 = t11;
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        void * t12 = gm_l0;
        void * t13 = (void *)(((uint8_t *)t12) + 8);
        gm_b16_8 t14 = gm_l3;
        uint32_t t15 = gm_f_fresh_adt_u9rnf9f((void *)NULL, cx, NULL, t13, t14);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l10 = t15;
    }
    {
        void * t16 = gm_l0;
        void * t17 = (void *)(((uint8_t *)t16) + 1088);
        gm_b16_8 t18 = gm_l3;
        uint64_t t19 = gm_l1;
        gm_ti0kgh3pc3in2 t20;
        gm_f_empty_struct_b5i94nj((void *)&t20);
        void * t21 = gm_l4;
        gm_tl874t2atnlh3 t22 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t21) + 72));
        gm_tf5srh4mp4b3i t23 = gm_l2;
        gm_t6art7bprjiqb t24;
        gm_f_new_adt_info_5pl2srl((void *)&t24, t18, t19, &t20, GM_CONV(gm_cseahvg8v7p0vs, t22), t23);
        gm_tftb1odc739t5 t25 = gm_f_push_j84ih6c(t17, &t24);
        if (((((uint8_t *)&t25))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t25) + 1, gm_src3k278vcmqgdr, 103, 5);
        gm_b0_1 t26 = GM_LD(gm_b0_1, (((uint8_t *)&t25) + 1));
    }
    {
        void * t27 = gm_l0;
        void * t28 = (void *)(((uint8_t *)t27) + 8);
        uint32_t t29 = gm_l10;
        gm_b16_8 t30 = gm_f_empty_slice_fh7svmj();
        gm_b16_8 t31 = GM_LD(gm_b16_8, ((uint8_t *)&t30));
        gm_t88rcucg1b7uk t32 = gm_f_adt_luat93d((void *)NULL, cx, NULL, t28, t29, t31);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l11 = t32;
    }
    {
        uint8_t t34 = gm_l5;
        if (!t34) goto ie33;
        {
            void * t36 = gm_l0;
            gm_b16_8 t37 = gm_l3;
            gm_t88rcucg1b7uk t38 = gm_l11;
            gm_b0_1 t39 = gm_f_push_type_alias_rr9mg3i((void *)NULL, cx, NULL, t36, t37, t38);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix33;
ie33:;
ix33:;
    }
    {
        void * t40 = gm_l7;
        gm_th2lqbo77pvsc t41;
        uint8_t t47 = GM_LD(uint8_t, ((((uint8_t *)t40) + 8) + 0));
        switch (t47) { case ((uint8_t)0ULL): goto ma45; case ((uint8_t)1ULL): goto ma45; case ((uint8_t)2ULL): goto ma45; case ((uint8_t)3ULL): goto ma45; case ((uint8_t)4ULL): goto ma45; case ((uint8_t)5ULL): goto ma45; case ((uint8_t)6ULL): goto ma45; case ((uint8_t)7ULL): goto ma45; case ((uint8_t)8ULL): goto ma45; case ((uint8_t)9ULL): goto ma45; case ((uint8_t)10ULL): goto ma45; case ((uint8_t)11ULL): goto ma45; case ((uint8_t)12ULL): goto ma45; case ((uint8_t)13ULL): goto ma45; case ((uint8_t)14ULL): goto ma45; case ((uint8_t)15ULL): goto ma45; case ((uint8_t)16ULL): goto ma45; case ((uint8_t)17ULL): goto ma45; case ((uint8_t)18ULL): goto ma45; case ((uint8_t)19ULL): goto ma45; case ((uint8_t)20ULL): goto ma45; case ((uint8_t)21ULL): goto ma45; case ((uint8_t)22ULL): goto ma45; case ((uint8_t)23ULL): goto ma45; case ((uint8_t)24ULL): goto ma45; case ((uint8_t)25ULL): goto ma45; case ((uint8_t)26ULL): goto ma45; case ((uint8_t)27ULL): goto ma45; case ((uint8_t)28ULL): goto ma45; case ((uint8_t)29ULL): goto ma45; case ((uint8_t)30ULL): goto ma45; case ((uint8_t)31ULL): goto ma45; case ((uint8_t)32ULL): goto ma43; case ((uint8_t)33ULL): goto ma44; case ((uint8_t)34ULL): goto ma45; case ((uint8_t)35ULL): goto ma45; case ((uint8_t)36ULL): goto ma45; case ((uint8_t)37ULL): goto ma45; case ((uint8_t)38ULL): goto ma45; case ((uint8_t)39ULL): goto ma45; case ((uint8_t)40ULL): goto ma45; case ((uint8_t)41ULL): goto ma45; case ((uint8_t)42ULL): goto ma45; case ((uint8_t)43ULL): goto ma45; case ((uint8_t)44ULL): goto ma45; case ((uint8_t)45ULL): goto ma45; case ((uint8_t)46ULL): goto ma45; case ((uint8_t)47ULL): goto ma45; case ((uint8_t)48ULL): goto ma45; case ((uint8_t)49ULL): goto ma45; case ((uint8_t)50ULL): goto ma45; default: goto ma45; }
ma43:;
        uint8_t t49 = GM_LD(uint8_t, ((((uint8_t *)t40) + 8) + 0));
        if (t49 != ((uint8_t)32ULL)) goto mn48;
        void * t50 = GM_LD(void *, ((((uint8_t *)t40) + 8) + 8));
        gm_l12 = t50;
        void * t51 = gm_l12;
        gm_th2lqbo77pvsc t52 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)t51) + 0));
        t41 = t52;
        goto me42;
mn48:;
ma44:;
        uint8_t t54 = GM_LD(uint8_t, ((((uint8_t *)t40) + 8) + 0));
        if (t54 != ((uint8_t)33ULL)) goto mn53;
        void * t55 = GM_LD(void *, ((((uint8_t *)t40) + 8) + 8));
        gm_l13 = t55;
        void * t56 = gm_l13;
        gm_th2lqbo77pvsc t57 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)t56) + 8));
        t41 = t57;
        goto me42;
mn53:;
ma45:;
        gm_th2lqbo77pvsc t59;
        memset(&t59, 0, sizeof t59);
        t41 = t59;
        goto me42;
mn58:;
ma46:;
        gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 108, 28);
me42:;
        gm_l14 = t41;
    }
    {
        void * t60 = gm_l0;
        gm_f_scope_mark_i3b2mvo((void *)&gm_l15, t60);
    }
    {
        gm_f_list_go2qg0b((void *)&F.l16, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_list_pmp06kq_2((void *)&F.l17, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_th2lqbo77pvsc t62 = gm_l14;
            gm_b16_8 t63;
            if (((((uint8_t *)&t62))[16] != 0)) {
                gm_b16_8 t64 = GM_LD(gm_b16_8, (((uint8_t *)&t62) + 0));
                t63 = t64;
            } else {
                gm_b16_8 t65 = gm_f_empty_slice_ito0pap();
                t63 = t65;
            }
            gm_l18 = t63;
        }
        {
            gm_l19 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t67 = gm_l19;
                uint64_t t68 = GM_LD(uintptr_t, ((uint8_t *)&gm_l18) + 8);
                uint8_t t69 = (t67 < t68);
                if (!t69) goto lb66;
                {
                    uint64_t t72 = gm_l19;
                    uint64_t t73 = (uintptr_t)t72;
                    uint64_t t74 = GM_LD(uintptr_t, ((uint8_t *)&gm_l18) + 8);
                    gm_tf2orqcrphmek t77 = GM_LD(gm_tf2orqcrphmek, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l18)) + t73 * 56));
                    F.l20 = t77;
                    {
                        void * t79 = gm_l0;
                        uint64_t t80 = gm_l1;
                        void * t81 = (void *)((uint8_t *)&F.l20);
                        gm_tnnjeerolos0a t82 = gm_f_param_kind_of_1jngakt((void *)NULL, cx, NULL, t79, t80, t81);
                        if (gm_unw.kind) {
                            goto gm_ll70;
                        }
                        gm_l21 = t82;
                    }
                    {
                        {
                            gm_b16_8 t84 = GM_LD(gm_b16_8, (((uint8_t *)&F.l20) + 0));
                            gm_l22 = t84;
                        }
                        {
                            gm_l23 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t86 = gm_l23;
                                uint64_t t87 = GM_LD(uintptr_t, ((uint8_t *)&gm_l22) + 8);
                                uint8_t t88 = (t86 < t87);
                                if (!t88) goto lb85;
                                {
                                    uint64_t t91 = gm_l23;
                                    uint64_t t92 = (uintptr_t)t91;
                                    uint64_t t93 = GM_LD(uintptr_t, ((uint8_t *)&gm_l22) + 8);
                                    gm_t08olq97rscn3 t96 = GM_LD(gm_t08olq97rscn3, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l22)) + t92 * 32));
                                    gm_l24 = t96;
                                    {
                                        void * t98 = gm_l0;
                                        gm_b16_8 t99 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l24) + 0));
                                        gm_tnnjeerolos0a t100 = gm_l21;
                                        uint32_t t101 = gm_f_new_param_7010dt4((void *)NULL, cx, NULL, t98, t99, t100);
                                        if (gm_unw.kind) {
                                            goto gm_ll89;
                                        }
                                        gm_l25 = t101;
                                    }
                                    {
                                        void * t102 = (void *)((uint8_t *)&F.l16);
                                        uint32_t t103 = gm_l25;
                                        gm_tftb1odc739t5 t104 = gm_f_push_im9d4ro(t102, t103);
                                        if (((((uint8_t *)&t104))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t104) + 1, gm_src3k278vcmqgdr, 120, 13);
                                        gm_b0_1 t105 = GM_LD(gm_b0_1, (((uint8_t *)&t104) + 1));
                                    }
                                    {
                                        void * t106 = (void *)((uint8_t *)&F.l17);
                                        void * t107 = GM_LD(void *, (((uint8_t *)&F.l20) + 32));
                                        gm_tftb1odc739t5 t108 = gm_f_push_r2k6rjf_2(t106, t107);
                                        if (((((uint8_t *)&t108))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t108) + 1, gm_src3k278vcmqgdr, 121, 13);
                                        gm_b0_1 t109 = GM_LD(gm_b0_1, (((uint8_t *)&t108) + 1));
                                    }
                                    {
                                        uint8_t t112 = GM_LD(uint8_t, (((uint8_t *)&gm_l21) + 0));
                                        if (t112 != ((uint8_t)2ULL)) goto mn111;
                                        void * t113 = gm_l0;
                                        void * t114 = (void *)(((uint8_t *)t113) + 1528);
                                        gm_tommf4j9upomc t115;
                                        memset((uint8_t *)&t115 + 20, 0, 4);
                                        gm_b16_8 t116 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l24) + 0));
                                        GM_ST(gm_b16_8, (uint8_t *)&t115 + 0, t116);
                                        uint32_t t117 = gm_l25;
                                        GM_ST(uint32_t, (uint8_t *)&t115 + 16, t117);
                                        gm_tftb1odc739t5 t118 = gm_f_push_sp4si0f(t114, &t115);
                                        if (((((uint8_t *)&t118))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t118) + 1, gm_src3k278vcmqgdr, 123, 30);
                                        gm_b0_1 t119 = GM_LD(gm_b0_1, (((uint8_t *)&t118) + 1));
                                        goto me110;
mn111:;
                                        void * t121 = gm_l0;
                                        void * t122 = (void *)(((uint8_t *)t121) + 1488);
                                        gm_tep2drsl9408b t123;
                                        memset((uint8_t *)&t123 + 20, 0, 4);
                                        gm_b16_8 t124 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l24) + 0));
                                        GM_ST(gm_b16_8, (uint8_t *)&t123 + 0, t124);
                                        void * t125 = gm_l0;
                                        void * t126 = (void *)(((uint8_t *)t125) + 8);
                                        uint32_t t127 = gm_l25;
                                        gm_t88rcucg1b7uk t128 = gm_f_param_n8kgsdm((void *)NULL, cx, NULL, t126, t127);
                                        if (gm_unw.kind) {
                                            goto gm_ll89;
                                        }
                                        GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t123 + 16, t128);
                                        gm_tftb1odc739t5 t129 = gm_f_push_hv483nm(t122, &t123);
                                        if (((((uint8_t *)&t129))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t129) + 1, gm_src3k278vcmqgdr, 124, 22);
                                        gm_b0_1 t130 = GM_LD(gm_b0_1, (((uint8_t *)&t129) + 1));
                                        goto me110;
mn120:;
                                        gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 122, 13);
me110:;
                                    }
                                    goto me94;
mn95:;
                                    gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 118, 9);
me94:;
                                }
lc85:;
                                {
                                    uint64_t t132 = gm_l23;
                                    uint64_t r133; bool o134 = __builtin_add_overflow(t132, ((uint64_t)1ULL), &r133);
                                    uint64_t t135 = r133;
                                    gm_l23 = t135;
                                }
                            }
gm_ll89:;
                            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb85; }
                                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc85; }
                            }
                            goto gm_ll70;
lb85:;
                        }
                    }
                    goto me75;
mn76:;
                    gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 116, 5);
me75:;
                }
lc66:;
                {
                    uint64_t t137 = gm_l19;
                    uint64_t r138; bool o139 = __builtin_add_overflow(t137, ((uint64_t)1ULL), &r138);
                    uint64_t t140 = r138;
                    gm_l19 = t140;
                }
            }
gm_ll70:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb66; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc66; }
            }
            goto gm_lad;
lb66:;
        }
    }
    {
        uint32_t t141 = gm_l10;
        uint64_t t142 = (uint64_t)t141;
        uint64_t t143 = (uintptr_t)t142;
        void * t144 = gm_l0;
        uint64_t t145 = GM_LD(uintptr_t, ((((uint8_t *)t144) + 1088) + 0) + 8);
        if (t143 >= (uintptr_t)(t145)) gm_trap_index(t143, t145, gm_src3k278vcmqgdr, 128, 5);
        gm_b16_8 t146 = GM_LD(gm_b16_8, (((uint8_t *)&F.l16) + 0));
        GM_ST(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t144) + 1088) + 0)) + t143 * 184) + 120), t146);
    }
    {
        uint32_t t147 = gm_l10;
        uint64_t t148 = (uint64_t)t147;
        uint64_t t149 = (uintptr_t)t148;
        void * t150 = gm_l0;
        uint64_t t151 = GM_LD(uintptr_t, ((((uint8_t *)t150) + 1088) + 0) + 8);
        if (t149 >= (uintptr_t)(t151)) gm_trap_index(t149, t151, gm_src3k278vcmqgdr, 129, 5);
        gm_b16_8 t152 = GM_LD(gm_b16_8, (((uint8_t *)&F.l17) + 0));
        GM_ST(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t150) + 1088) + 0)) + t149 * 184) + 136), t152);
    }
    {
        if (!((((uint8_t *)&gm_l2))[8] != 0)) goto ie153;
        uint64_t t154 = GM_LD(uint64_t, ((uint8_t *)&gm_l2));
        gm_l26 = t154;
        {
            gm_t6qooom8qa2do t156;
            uint64_t t158 = GM_LD(uintptr_t, (((uint8_t *)&F.l16) + 0) + 8);
            uint8_t t159 = ((t158 == ((uint64_t)0ULL)));
            if (!t159) goto ie157;
            gm_t88rcucg1b7uk t161 = gm_l11;
            gm_t6qooom8qa2do t162;
            memset(&t162, 0, sizeof t162);
            GM_ST(uint8_t, ((uint8_t *)&t162 + 0), ((uint8_t)0ULL));
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t162 + 8), t161);
            gm_t6qooom8qa2do t163 = t162;
            t156 = t163;
            goto ix157;
ie157:;
            uint32_t t165 = gm_l10;
            gm_t6qooom8qa2do t166;
            memset(&t166, 0, sizeof t166);
            GM_ST(uint8_t, ((uint8_t *)&t166 + 0), ((uint8_t)1ULL));
            GM_ST(uint32_t, ((uint8_t *)&t166 + 8), t165);
            gm_t6qooom8qa2do t167 = t166;
            t156 = t167;
ix157:;
            F.l27 = t156;
        }
        {
            void * t168 = gm_l0;
            uint64_t t169 = gm_l26;
            gm_t6qooom8qa2do t170 = F.l27;
            gm_tagrkbqh054h7 t171;
            GM_ST(uint8_t, (uint8_t *)&t171 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t171 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t171 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t171 + 0), ((uint8_t)2ULL));
            GM_ST(gm_t6qooom8qa2do, ((uint8_t *)&t171 + 8), t170);
            gm_b0_1 t172 = gm_f_set_state_3j300fi((void *)NULL, cx, NULL, t168, t169, &t171);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix153;
ie153:;
ix153:;
    }
    {
        void * t173 = gm_l0;
        void * t174 = (void *)(((uint8_t *)t173) + 1728);
        uint32_t t175 = gm_l10;
        gm_tftb1odc739t5 t176 = gm_f_push_im9d4ro(t174, t175);
        if (((((uint8_t *)&t176))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t176) + 1, gm_src3k278vcmqgdr, 135, 5);
        gm_b0_1 t177 = GM_LD(gm_b0_1, (((uint8_t *)&t176) + 1));
    }
    {
        void * t178 = gm_l0;
        void * t179 = gm_l0;
        uint32_t t180 = GM_LD(uint32_t, (((uint8_t *)t179) + 1768));
        uint32_t r181; bool o182 = __builtin_add_overflow(t180, ((uint32_t)1ULL), &r181);
        if (o182) gm_trap("integer overflow in `+`", gm_src3k278vcmqgdr, 136, 5);
        uint32_t t183 = r181;
        GM_ST(uint32_t, (((uint8_t *)t178) + 1768), t183);
    }
    {
        gm_f_empty_struct_b5i94nj((void *)&F.l28);
    }
    {
        gm_b16_8 t184 = gm_f_empty_slice_3jt8vgk();
        gm_l29 = t184;
    }
    {
        void * t185 = gm_l7;
        uint8_t t191 = GM_LD(uint8_t, ((((uint8_t *)t185) + 8) + 0));
        switch (t191) { case ((uint8_t)0ULL): goto ma189; case ((uint8_t)1ULL): goto ma189; case ((uint8_t)2ULL): goto ma189; case ((uint8_t)3ULL): goto ma189; case ((uint8_t)4ULL): goto ma189; case ((uint8_t)5ULL): goto ma189; case ((uint8_t)6ULL): goto ma189; case ((uint8_t)7ULL): goto ma189; case ((uint8_t)8ULL): goto ma189; case ((uint8_t)9ULL): goto ma189; case ((uint8_t)10ULL): goto ma189; case ((uint8_t)11ULL): goto ma189; case ((uint8_t)12ULL): goto ma189; case ((uint8_t)13ULL): goto ma189; case ((uint8_t)14ULL): goto ma189; case ((uint8_t)15ULL): goto ma189; case ((uint8_t)16ULL): goto ma189; case ((uint8_t)17ULL): goto ma189; case ((uint8_t)18ULL): goto ma189; case ((uint8_t)19ULL): goto ma189; case ((uint8_t)20ULL): goto ma189; case ((uint8_t)21ULL): goto ma189; case ((uint8_t)22ULL): goto ma189; case ((uint8_t)23ULL): goto ma189; case ((uint8_t)24ULL): goto ma189; case ((uint8_t)25ULL): goto ma189; case ((uint8_t)26ULL): goto ma189; case ((uint8_t)27ULL): goto ma189; case ((uint8_t)28ULL): goto ma189; case ((uint8_t)29ULL): goto ma189; case ((uint8_t)30ULL): goto ma189; case ((uint8_t)31ULL): goto ma189; case ((uint8_t)32ULL): goto ma187; case ((uint8_t)33ULL): goto ma188; case ((uint8_t)34ULL): goto ma189; case ((uint8_t)35ULL): goto ma189; case ((uint8_t)36ULL): goto ma189; case ((uint8_t)37ULL): goto ma189; case ((uint8_t)38ULL): goto ma189; case ((uint8_t)39ULL): goto ma189; case ((uint8_t)40ULL): goto ma189; case ((uint8_t)41ULL): goto ma189; case ((uint8_t)42ULL): goto ma189; case ((uint8_t)43ULL): goto ma189; case ((uint8_t)44ULL): goto ma189; case ((uint8_t)45ULL): goto ma189; case ((uint8_t)46ULL): goto ma189; case ((uint8_t)47ULL): goto ma189; case ((uint8_t)48ULL): goto ma189; case ((uint8_t)49ULL): goto ma189; case ((uint8_t)50ULL): goto ma189; default: goto ma189; }
ma187:;
        uint8_t t193 = GM_LD(uint8_t, ((((uint8_t *)t185) + 8) + 0));
        if (t193 != ((uint8_t)32ULL)) goto mn192;
        void * t194 = GM_LD(void *, ((((uint8_t *)t185) + 8) + 8));
        gm_l30 = t194;
        {
            void * t196 = gm_l0;
            uint64_t t197 = gm_l1;
            uint32_t t198 = gm_l10;
            void * t199 = gm_l30;
            gm_b16_8 t200 = gm_l6;
            gm_b16_8 t201 = GM_LD(gm_b16_8, ((uint8_t *)&t200));
            gm_f_struct_kind_5l1hs46((void *)&(*(gm_ti0kgh3pc3in2 *)((uint8_t *)&F.l28)), (void *)NULL, cx, NULL, t196, t197, t198, t199, t201);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t202 = gm_l30;
            gm_b16_8 t203 = GM_LD(gm_b16_8, (((uint8_t *)t202) + 24));
            gm_l29 = t203;
        }
        goto me186;
mn192:;
ma188:;
        uint8_t t205 = GM_LD(uint8_t, ((((uint8_t *)t185) + 8) + 0));
        if (t205 != ((uint8_t)33ULL)) goto mn204;
        void * t206 = GM_LD(void *, ((((uint8_t *)t185) + 8) + 8));
        gm_l31 = t206;
        {
            void * t209 = gm_l31;
            void * t210 = GM_LD(void *, (((uint8_t *)t209) + 0));
            uint8_t t211;
            if ((GM_LD(void *, ((uint8_t *)&t210)) != 0)) goto mn213;
            t211 = ((uint8_t)0);
            goto me212;
mn213:;
            t211 = ((uint8_t)1);
            goto me212;
mn214:;
            gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 145, 16);
me212:;
            if (!t211) goto ie208;
            {
                uint32_t t216 = gm_l10;
                uint64_t t217 = (uint64_t)t216;
                uint64_t t218 = (uintptr_t)t217;
                void * t219 = gm_l0;
                uint64_t t220 = GM_LD(uintptr_t, ((((uint8_t *)t219) + 1088) + 0) + 8);
                if (t218 >= (uintptr_t)(t220)) gm_trap_index(t218, t220, gm_src3k278vcmqgdr, 145, 41);
                GM_ST(uint8_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t219) + 1088) + 0)) + t218 * 184) + 152), ((uint8_t)1));
            }
            goto ix208;
ie208:;
ix208:;
        }
        {
            void * t221 = gm_l0;
            uint64_t t222 = gm_l1;
            void * t223 = gm_l31;
            gm_b16_8 t224 = gm_l6;
            gm_b16_8 t225 = GM_LD(gm_b16_8, ((uint8_t *)&t224));
            gm_f_union_kind_j2gcmto((void *)&(*(gm_ti0kgh3pc3in2 *)((uint8_t *)&F.l28)), (void *)NULL, cx, NULL, t221, t222, t223, t225);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t226 = gm_l31;
            gm_b16_8 t227 = GM_LD(gm_b16_8, (((uint8_t *)t226) + 32));
            gm_l29 = t227;
        }
        goto me186;
mn204:;
ma189:;
        {
            void * t230 = gm_l0;
            void * t231 = gm_l4;
            gm_tl874t2atnlh3 t232 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t231) + 72));
            gm_b16_8 t233;
            GM_ST(void *, (uint8_t *)&t233, (void *)(gm_s4ko774bb2ea9));
            GM_ST(uintptr_t, (uint8_t *)&t233 + 8, (uintptr_t)(55));
            gm_b0_1 t234 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t230, t232, t233);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t235 = gm_l0;
            uint32_t t236 = gm_l10;
            gm_f_remove_in_progress_lrie478(t235, t236);
        }
        {
            void * t237 = gm_l0;
            gm_b0_1 t238 = gm_f_end_constraint_defer_52jt3m9((void *)NULL, cx, NULL, t237);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t239 = gm_l0;
            void * t240 = (void *)(((uint8_t *)t239) + 8);
            gm_t88rcucg1b7uk t241 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t240);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t242 = 0;
            F.ret = t241;
            goto gm_fret;
        }
        goto me186;
mn228:;
ma190:;
        gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 139, 5);
me186:;
    }
    {
        void * t243 = gm_l0;
        uint32_t t244 = gm_l10;
        gm_f_remove_in_progress_lrie478(t243, t244);
    }
    {
        uint32_t t245 = gm_l10;
        uint64_t t246 = (uint64_t)t245;
        uint64_t t247 = (uintptr_t)t246;
        void * t248 = gm_l0;
        uint64_t t249 = GM_LD(uintptr_t, ((((uint8_t *)t248) + 1088) + 0) + 8);
        if (t247 >= (uintptr_t)(t249)) gm_trap_index(t247, t249, gm_src3k278vcmqgdr, 157, 5);
        gm_ti0kgh3pc3in2 t250 = F.l28;
        GM_ST(gm_ti0kgh3pc3in2, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t248) + 1088) + 0)) + t247 * 184) + 24), t250);
    }
    {
        {
            gm_b16_8 t252 = gm_l29;
            gm_l32 = t252;
        }
        {
            gm_l33 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t254 = gm_l33;
                uint64_t t255 = GM_LD(uintptr_t, ((uint8_t *)&gm_l32) + 8);
                uint8_t t256 = (t254 < t255);
                if (!t256) goto lb253;
                {
                    uint64_t t259 = gm_l33;
                    uint64_t t260 = (uintptr_t)t259;
                    uint64_t t261 = GM_LD(uintptr_t, ((uint8_t *)&gm_l32) + 8);
                    gm_tpv68m88f9lcr t264 = GM_LD(gm_tpv68m88f9lcr, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l32)) + t260 * 56));
                    F.l34 = t264;
                    {
                        void * t266 = gm_l0;
                        uint64_t t267 = gm_l1;
                        void * t268 = (void *)((uint8_t *)&F.l34);
                        gm_b0_1 t269 = gm_f_declare_constraint_s1juggf((void *)NULL, cx, NULL, t266, t267, t268);
                        if (gm_unw.kind) {
                            goto gm_ll257;
                        }
                    }
                    goto me262;
mn263:;
                    gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 158, 5);
me262:;
                }
lc253:;
                {
                    uint64_t t271 = gm_l33;
                    uint64_t r272; bool o273 = __builtin_add_overflow(t271, ((uint64_t)1ULL), &r272);
                    uint64_t t274 = r272;
                    gm_l33 = t274;
                }
            }
gm_ll257:;
            if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb253; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc253; }
            }
            goto gm_lad;
lb253:;
        }
    }
    {
        void * t275 = gm_l0;
        gm_b0_1 t276 = gm_f_end_constraint_defer_52jt3m9((void *)NULL, cx, NULL, t275);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t277 = gm_l0;
        gm_f_restore_scope_edqq1v8(t277, &(*(gm_t66j1im9ip128 *)((uint8_t *)&gm_l15)));
    }
    {
        uint64_t t279 = GM_LD(uintptr_t, (((uint8_t *)&F.l16) + 0) + 8);
        uint8_t t280 = (t279 > ((uint64_t)0ULL));
        if (!t280) goto ie278;
        {
            gm_t88rcucg1b7uk t282 = gm_l11;
            uint8_t t283 = 0;
            F.ret = t282;
            goto gm_fret;
        }
        goto ix278;
ie278:;
ix278:;
    }
    {
        gm_f_list_go2qg0b((void *)&F.l35, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t285 = gm_l0;
        gm_t88rcucg1b7uk t286 = gm_l11;
        void * t287 = (void *)((uint8_t *)&F.l35);
        uint8_t t288 = gm_f_is_infinitely_sized_mhhcucl((void *)NULL, cx, NULL, t285, t286, t287);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t288) goto ie284;
        {
            void * t290 = gm_l0;
            void * t291 = gm_l4;
            gm_tl874t2atnlh3 t292 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t291) + 72));
            gm_b16_8 t293 = gm_l3;
            gm_tnpifv20irnph t294;
            GM_ST(gm_b16_8, ((uint8_t *)&t294 + 0), t293);
            gm_tabb710d9mq2e t295;
            gm_f_format_0au88qf((void *)&t295, (void *)NULL, cx, NULL, t294);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t295))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t295) + 8, gm_src3k278vcmqgdr, 164, 27);
            gm_b16_8 t296 = GM_LD(gm_b16_8, (((uint8_t *)&t295) + 8));
            gm_b16_8 t297 = GM_LD(gm_b16_8, ((uint8_t *)&t296));
            gm_b0_1 t298 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t290, t292, t297);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t299 = gm_l0;
            uint32_t t300 = gm_l10;
            gm_f_empty_adt_tdp3hro(t299, t300);
        }
        {
            void * t301 = gm_l0;
            void * t302 = (void *)(((uint8_t *)t301) + 8);
            gm_t88rcucg1b7uk t303 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t302);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t304 = 0;
            F.ret = t303;
            goto gm_fret;
        }
        goto ix284;
ie284:;
ix284:;
    }
    {
        void * t306 = gm_l0;
        gm_t88rcucg1b7uk t307 = gm_l11;
        void * t308 = gm_l4;
        gm_tl874t2atnlh3 t309 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t308) + 72));
        uint8_t t310 = gm_f_type_size_ok_tr1llg4((void *)NULL, cx, NULL, t306, t307, t309);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t311 = (uint8_t)!t310;
        if (!t311) goto ie305;
        {
            void * t313 = gm_l0;
            uint32_t t314 = gm_l10;
            gm_f_empty_adt_tdp3hro(t313, t314);
        }
        {
            void * t315 = gm_l0;
            void * t316 = (void *)(((uint8_t *)t315) + 8);
            gm_t88rcucg1b7uk t317 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t316);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t318 = 0;
            F.ret = t317;
            goto gm_fret;
        }
        goto ix305;
ie305:;
ix305:;
    }
    {
        gm_t88rcucg1b7uk t319 = gm_l11;
        uint8_t t320 = 0;
        F.ret = t319;
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
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_declare_local_adt_6j4pdai(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2, void * a3) {
    struct gm_fr_declare_local_adt_6j4pdai F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    gm_b16_8 gm_l2; /* name */
    void * gm_l3; /* lit */
    void * gm_l4; /* inner */
    void * gm_l5; /* i */
    void * gm_l6; /* st */
    void * gm_l7; /* u */
    uint8_t gm_l8; /* generic */
    gm_t88rcucg1b7uk gm_l9; /* ty */
    uint32_t gm_l10; /* id */
    GM_STACK_CHECK(&F, 586u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l3;
        gm_l4 = t2;
    }
    {
        void * t4 = gm_l3;
        uint8_t t5 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t5 != ((uint8_t)35ULL)) goto ie3;
        void * t6 = GM_LD(void *, (((((uint8_t *)t4) + 8) + 8) + 16));
        gm_l5 = t6;
        {
            void * t8 = gm_l5;
            gm_l4 = t8;
        }
        goto ix3;
ie3:;
ix3:;
    }
    {
        void * t9 = gm_l4;
        uint8_t t10;
        uint8_t t16 = GM_LD(uint8_t, ((((uint8_t *)t9) + 8) + 0));
        switch (t16) { case ((uint8_t)0ULL): goto ma14; case ((uint8_t)1ULL): goto ma14; case ((uint8_t)2ULL): goto ma14; case ((uint8_t)3ULL): goto ma14; case ((uint8_t)4ULL): goto ma14; case ((uint8_t)5ULL): goto ma14; case ((uint8_t)6ULL): goto ma14; case ((uint8_t)7ULL): goto ma14; case ((uint8_t)8ULL): goto ma14; case ((uint8_t)9ULL): goto ma14; case ((uint8_t)10ULL): goto ma14; case ((uint8_t)11ULL): goto ma14; case ((uint8_t)12ULL): goto ma14; case ((uint8_t)13ULL): goto ma14; case ((uint8_t)14ULL): goto ma14; case ((uint8_t)15ULL): goto ma14; case ((uint8_t)16ULL): goto ma14; case ((uint8_t)17ULL): goto ma14; case ((uint8_t)18ULL): goto ma14; case ((uint8_t)19ULL): goto ma14; case ((uint8_t)20ULL): goto ma14; case ((uint8_t)21ULL): goto ma14; case ((uint8_t)22ULL): goto ma14; case ((uint8_t)23ULL): goto ma14; case ((uint8_t)24ULL): goto ma14; case ((uint8_t)25ULL): goto ma14; case ((uint8_t)26ULL): goto ma14; case ((uint8_t)27ULL): goto ma14; case ((uint8_t)28ULL): goto ma14; case ((uint8_t)29ULL): goto ma14; case ((uint8_t)30ULL): goto ma14; case ((uint8_t)31ULL): goto ma14; case ((uint8_t)32ULL): goto ma12; case ((uint8_t)33ULL): goto ma13; case ((uint8_t)34ULL): goto ma14; case ((uint8_t)35ULL): goto ma14; case ((uint8_t)36ULL): goto ma14; case ((uint8_t)37ULL): goto ma14; case ((uint8_t)38ULL): goto ma14; case ((uint8_t)39ULL): goto ma14; case ((uint8_t)40ULL): goto ma14; case ((uint8_t)41ULL): goto ma14; case ((uint8_t)42ULL): goto ma14; case ((uint8_t)43ULL): goto ma14; case ((uint8_t)44ULL): goto ma14; case ((uint8_t)45ULL): goto ma14; case ((uint8_t)46ULL): goto ma14; case ((uint8_t)47ULL): goto ma14; case ((uint8_t)48ULL): goto ma14; case ((uint8_t)49ULL): goto ma14; case ((uint8_t)50ULL): goto ma14; default: goto ma14; }
ma12:;
        uint8_t t18 = GM_LD(uint8_t, ((((uint8_t *)t9) + 8) + 0));
        if (t18 != ((uint8_t)32ULL)) goto mn17;
        void * t19 = GM_LD(void *, ((((uint8_t *)t9) + 8) + 8));
        gm_l6 = t19;
        void * t20 = gm_l6;
        uint8_t t21;
        if ((((((uint8_t *)t20) + 0))[16] != 0)) goto mn23;
        t21 = ((uint8_t)0);
        goto me22;
mn23:;
        t21 = ((uint8_t)1);
        goto me22;
mn24:;
        gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 53, 24);
me22:;
        t10 = t21;
        goto me11;
mn17:;
ma13:;
        uint8_t t26 = GM_LD(uint8_t, ((((uint8_t *)t9) + 8) + 0));
        if (t26 != ((uint8_t)33ULL)) goto mn25;
        void * t27 = GM_LD(void *, ((((uint8_t *)t9) + 8) + 8));
        gm_l7 = t27;
        void * t28 = gm_l7;
        uint8_t t29;
        if ((((((uint8_t *)t28) + 8))[16] != 0)) goto mn31;
        t29 = ((uint8_t)0);
        goto me30;
mn31:;
        t29 = ((uint8_t)1);
        goto me30;
mn32:;
        gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 54, 22);
me30:;
        t10 = t29;
        goto me11;
mn25:;
ma14:;
        t10 = ((uint8_t)0);
        goto me11;
mn33:;
ma15:;
        gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 52, 16);
me11:;
        gm_l8 = t10;
    }
    {
        uint8_t t35 = gm_l8;
        if (!t35) goto ie34;
        {
            void * t37 = gm_l0;
            void * t38 = gm_l3;
            gm_tl874t2atnlh3 t39 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t38) + 72));
            gm_b16_8 t40;
            GM_ST(void *, (uint8_t *)&t40, (void *)(gm_sf7l7ca9coehi));
            GM_ST(uintptr_t, (uint8_t *)&t40 + 8, (uintptr_t)(84));
            gm_b0_1 t41 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t37, t39, t40);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t42 = gm_l0;
            gm_b16_8 t43 = gm_l2;
            void * t44 = gm_l0;
            void * t45 = (void *)(((uint8_t *)t44) + 8);
            gm_t88rcucg1b7uk t46 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t45);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b0_1 t47 = gm_f_push_type_alias_rr9mg3i((void *)NULL, cx, NULL, t42, t43, t46);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t48 = gm_l0;
            void * t49 = (void *)(((uint8_t *)t48) + 8);
            gm_t88rcucg1b7uk t50 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t49);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t51 = 0;
            F.ret = t50;
            goto gm_fret;
        }
        goto ix34;
ie34:;
ix34:;
    }
    {
        void * t52 = gm_l0;
        uint64_t t53 = gm_l1;
        gm_tf5srh4mp4b3i t54;
        memset(&t54, 0, sizeof t54);
        gm_b16_8 t55 = gm_l2;
        void * t56 = gm_l3;
        gm_t88rcucg1b7uk t57 = gm_f_declare_adt_in_74tov03((void *)NULL, cx, NULL, t52, t53, t54, t55, t56, ((uint8_t)1));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l9 = t57;
    }
    {
        void * t59 = gm_l0;
        void * t60 = (void *)(((uint8_t *)t59) + 8);
        gm_t88rcucg1b7uk t61 = gm_l9;
        gm_t53at80pfdus3 t62;
        gm_f_kind_jcnt4hm((void *)&t62, t60, GM_CONV(gm_csguankko4lmhd, t61));
        uint8_t t63 = GM_LD(uint8_t, (((uint8_t *)&t62) + 0));
        if (t63 != ((uint8_t)17ULL)) goto ie58;
        uint32_t t64 = GM_LD(uint32_t, ((((uint8_t *)&t62) + 8) + 0));
        gm_l10 = t64;
        void * t65 = gm_l0;
        uint32_t t66 = gm_l10;
        uint8_t t67 = gm_f_adt_mentions_param_0jsjgma(t65, t66);
        if (!t67) goto ie58;
        {
            void * t69 = gm_l0;
            void * t70 = gm_l3;
            gm_tl874t2atnlh3 t71 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t70) + 72));
            gm_b16_8 t72 = gm_l2;
            gm_tnpifv20irnph t73;
            GM_ST(gm_b16_8, ((uint8_t *)&t73 + 0), t72);
            gm_tabb710d9mq2e t74;
            gm_f_format_otdvqpt((void *)&t74, (void *)NULL, cx, NULL, t73);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t74))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t74) + 8, gm_src3k278vcmqgdr, 64, 27);
            gm_b16_8 t75 = GM_LD(gm_b16_8, (((uint8_t *)&t74) + 8));
            gm_b16_8 t76 = GM_LD(gm_b16_8, ((uint8_t *)&t75));
            gm_b0_1 t77 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t69, t71, t76);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t78 = gm_l0;
            uint32_t t79 = gm_l10;
            gm_f_empty_adt_tdp3hro(t78, t79);
        }
        {
            void * t80 = gm_l0;
            void * t81 = (void *)(((uint8_t *)t80) + 8);
            gm_t88rcucg1b7uk t82 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t81);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t83 = 0;
            F.ret = t82;
            goto gm_fret;
        }
        goto ix58;
ie58:;
ix58:;
    }
    {
        gm_t88rcucg1b7uk t84 = gm_l9;
        uint8_t t85 = 0;
        F.ret = t84;
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
inline __attribute__((aligned(4))) void gm_f_empty_adt_tdp3hro(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_adt_tdp3hro F;
    void * gm_l0; /* s */
    uint32_t gm_l1; /* id */
    GM_STACK_CHECK_SHALLOW(&F, 212u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint32_t t2 = gm_l1;
        uint64_t t3 = (uint64_t)t2;
        uint64_t t4 = (uintptr_t)t3;
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uintptr_t, ((((uint8_t *)t5) + 1088) + 0) + 8);
        if (t4 >= (uintptr_t)(t6)) gm_trap_index(t4, t6, gm_src3k278vcmqgdr, 182, 5);
        uint8_t *t7 = (((uint8_t *)GM_LD(void *, ((((uint8_t *)t5) + 1088) + 0)) + t4 * 184) + 24);
        gm_f_empty_struct_b5i94nj((void *)&(*(gm_ti0kgh3pc3in2 *)t7));
    }
    {
        uint32_t t8 = gm_l1;
        uint64_t t9 = (uint64_t)t8;
        uint64_t t10 = (uintptr_t)t9;
        void * t11 = gm_l0;
        uint64_t t12 = GM_LD(uintptr_t, ((((uint8_t *)t11) + 1088) + 0) + 8);
        if (t10 >= (uintptr_t)(t12)) gm_trap_index(t10, t12, gm_src3k278vcmqgdr, 183, 5);
        GM_ST(uint8_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t11) + 1088) + 0)) + t10 * 184) + 112), ((uint8_t)0));
    }
    {
        uint32_t t13 = gm_l1;
        uint64_t t14 = (uint64_t)t13;
        uint64_t t15 = (uintptr_t)t14;
        void * t16 = gm_l0;
        uint64_t t17 = GM_LD(uintptr_t, ((((uint8_t *)t16) + 1088) + 0) + 8);
        if (t15 >= (uintptr_t)(t17)) gm_trap_index(t15, t17, gm_src3k278vcmqgdr, 184, 5);
        uint8_t *t18 = (((uint8_t *)GM_LD(void *, ((((uint8_t *)t16) + 1088) + 0)) + t15 * 184) + 80);
        gm_b16_8 t19 = gm_f_empty_slice_s1lbe5s();
        GM_ST(gm_b16_8, t18, t19);
    }
    {
        uint32_t t20 = gm_l1;
        uint64_t t21 = (uint64_t)t20;
        uint64_t t22 = (uintptr_t)t21;
        void * t23 = gm_l0;
        uint64_t t24 = GM_LD(uintptr_t, ((((uint8_t *)t23) + 1088) + 0) + 8);
        if (t22 >= (uintptr_t)(t24)) gm_trap_index(t22, t24, gm_src3k278vcmqgdr, 185, 5);
        uint8_t *t25 = (((uint8_t *)GM_LD(void *, ((((uint8_t *)t23) + 1088) + 0)) + t22 * 184) + 96);
        gm_b16_8 t26 = gm_f_empty_slice_8q3kspo_2();
        GM_ST(gm_b16_8, t25, t26);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
extern __attribute__((aligned(4))) void gm_f_empty_struct_b5i94nj(void *gm_out);
__attribute__((aligned(4))) gm_ti0kgh3pc3in2 gm_f_empty_struct_b5i94nj_c(void) { gm_ti0kgh3pc3in2 gm_r; gm_f_empty_struct_b5i94nj(&gm_r); return gm_r; }
inline __attribute__((aligned(4))) uint32_t gm_f_field_bits_2ktfm1k(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_field_bits_2ktfm1k F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* t */
    uint64_t gm_l2; /* w */
    GM_STACK_CHECK(&F, 142u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        gm_t88rcucg1b7uk t4 = gm_l1;
        gm_tf5srh4mp4b3i t5 = gm_f_bit_width_b7ofh2h(t3, GM_CONV(gm_csguankko4lmhd, t4));
        if (!((((uint8_t *)&t5))[8] != 0)) goto ie2;
        uint64_t t6 = GM_LD(uint64_t, ((uint8_t *)&t5));
        gm_l2 = t6;
        {
            uint64_t t8 = gm_l2;
            uint32_t t9 = (uint32_t)t8;
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
        gm_t88rcucg1b7uk t12 = gm_l1;
        gm_tbccvikg6b96e t13;
        gm_f_layout_a5ovnrm((void *)&t13, (void *)NULL, cx, NULL, t11, t12);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)&t13) + 0));
        uint64_t r15; bool o16 = __builtin_mul_overflow(((uint64_t)8ULL), t14, &r15);
        if (o16) gm_trap("integer overflow in `*`", gm_src3k278vcmqgdr, 308, 13);
        uint64_t t17 = r15;
        uint32_t t18 = (uint32_t)t17;
        uint8_t t19 = 0;
        F.ret = t18;
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
inline __attribute__((aligned(4))) gm_tnpifv20irnph gm_f_flatten_2js7j92(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1) {
    struct gm_fr_flatten_2js7j92 F;
    void * gm_l0; /* s */
    gm_tnpifv20irnph gm_l1; /* set */
    uint8_t gm_l2; /* spliced */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_tiilmfd16v3fc gm_l6; /* e */
    gm_t88rcucg1b7uk gm_l7; /* t */
    gm_tnpifv20irnph gm_l8; /* inner */
    GM_STACK_CHECK(&F, 454u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_l2 = ((uint8_t)0);
    }
    {
        gm_f_list_084fcd6((void *)&F.l3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t3 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l1) + 0));
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
                    gm_tiilmfd16v3fc t15 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t11 * 24));
                    gm_l6 = t15;
                    {
                        uint32_t t18 = GM_LD(uint32_t, (((uint8_t *)&gm_l6) + 0));
                        uint8_t t19 = ((t18 == ((uint32_t)0ULL)));
                        if (!t19) goto ie17;
                        uint64_t t20 = GM_LD(uintptr_t, (((uint8_t *)&gm_l6) + 8) + 8);
                        uint8_t t21 = (t20 > ((uint64_t)0ULL));
                        if (!t21) goto ie17;
                        {
                            uint64_t t24 = (uintptr_t)((uint64_t)0ULL);
                            uint64_t t25 = GM_LD(uintptr_t, (((uint8_t *)&gm_l6) + 8) + 8);
                            if (t24 >= (uintptr_t)(t25)) gm_trap_index(t24, t25, gm_src3k278vcmqgdr, 780, 28);
                            uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)&gm_l6) + 8)) + t24 * 40) + 0));
                            if (t26 != ((uint8_t)0ULL)) goto ie23;
                            gm_t88rcucg1b7uk t27 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, (((uint8_t *)&gm_l6) + 8)) + t24 * 40) + 8));
                            gm_l7 = t27;
                            void * t28 = gm_l0;
                            void * t29 = (void *)(((uint8_t *)t28) + 8);
                            gm_t88rcucg1b7uk t30 = gm_l7;
                            gm_t53at80pfdus3 t31;
                            gm_f_kind_jcnt4hm((void *)&t31, t29, GM_CONV(gm_csguankko4lmhd, t30));
                            uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)&t31) + 0));
                            if (t32 != ((uint8_t)24ULL)) goto ie23;
                            gm_tnpifv20irnph t33 = GM_LD(gm_tnpifv20irnph, (((uint8_t *)&t31) + 8));
                            gm_l8 = t33;
                            {
                                void * t35 = (void *)((uint8_t *)&F.l3);
                                gm_b16_8 t36 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l8) + 0));
                                gm_tftb1odc739t5 t37 = gm_f_extend_74ac4hb(t35, t36);
                                if (((((uint8_t *)&t37))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t37) + 1, gm_src3k278vcmqgdr, 781, 17);
                                gm_b0_1 t38 = GM_LD(gm_b0_1, (((uint8_t *)&t37) + 1));
                            }
                            {
                                gm_l2 = ((uint8_t)1);
                            }
                            {
                                goto lc4;
                            }
                            goto ix23;
ie23:;
ix23:;
                        }
                        goto ix17;
ie17:;
ix17:;
                    }
                    {
                        void * t39 = (void *)((uint8_t *)&F.l3);
                        gm_tftb1odc739t5 t40 = gm_f_push_d1r4h19(t39, &(*(gm_tiilmfd16v3fc *)((uint8_t *)&gm_l6)));
                        if (((((uint8_t *)&t40))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t40) + 1, gm_src3k278vcmqgdr, 786, 9);
                        gm_b0_1 t41 = GM_LD(gm_b0_1, (((uint8_t *)&t40) + 1));
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 778, 5);
me13:;
                }
lc4:;
                {
                    uint64_t t43 = gm_l5;
                    uint64_t r44; bool o45 = __builtin_add_overflow(t43, ((uint64_t)1ULL), &r44);
                    uint64_t t46 = r44;
                    gm_l5 = t46;
                }
            }
lb4:;
        }
    }
    {
        uint8_t t48 = gm_l2;
        uint8_t t49 = (uint8_t)!t48;
        if (!t49) goto ie47;
        {
            gm_tnpifv20irnph t51 = gm_l1;
            uint8_t t52 = 0;
            F.ret = t51;
            goto gm_fret;
        }
        goto ix47;
ie47:;
ix47:;
    }
    {
        gm_b16_8 t53 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
        gm_b16_8 t54 = GM_LD(gm_b16_8, ((uint8_t *)&t53));
        gm_tnpifv20irnph t55 = gm_f_effect_set_vtcpipr((void *)NULL, cx, NULL, t54);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t56 = 0;
        F.ret = t55;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_instance_args_15ek7cf(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_instance_args_15ek7cf F;
    void * gm_l0; /* s */
    uint32_t gm_l1; /* inst */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    gm_t5g5vgung6h6s gm_l5; /* i */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    gm_t88rcucg1b7uk gm_l8; /* a */
    GM_STACK_CHECK(&F, 398u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_f_list_vmvl81b((void *)&F.l2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            void * t3 = gm_l0;
            gm_b16_8 t4 = GM_LD(gm_b16_8, ((((uint8_t *)t3) + 1168) + 0));
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
                    gm_t5g5vgung6h6s t16 = GM_LD(gm_t5g5vgung6h6s, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t12 * 32));
                    gm_l5 = t16;
                    {
                        uint32_t t19 = GM_LD(uint32_t, (((uint8_t *)&gm_l5) + 24));
                        uint32_t t20 = gm_l1;
                        uint8_t t21 = ((t19 == t20));
                        if (!t21) goto ie18;
                        {
                            {
                                gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 8));
                                gm_l6 = t24;
                            }
                            {
                                gm_l7 = ((uint64_t)0ULL);
                            }
                            {
                                for (;;) {
                                    uint64_t t26 = gm_l7;
                                    uint64_t t27 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                                    uint8_t t28 = (t26 < t27);
                                    if (!t28) goto lb25;
                                    {
                                        uint64_t t31 = gm_l7;
                                        uint64_t t32 = (uintptr_t)t31;
                                        uint64_t t33 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                                        gm_t88rcucg1b7uk t36 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t32 * 4));
                                        gm_l8 = t36;
                                        {
                                            void * t38 = (void *)((uint8_t *)&F.l2);
                                            gm_t88rcucg1b7uk t39 = gm_l8;
                                            gm_toa2vhe3l4hfr t40;
                                            memset(&t40, 0, sizeof t40);
                                            GM_ST(uint8_t, ((uint8_t *)&t40 + 0), ((uint8_t)0ULL));
                                            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t40 + 8), t39);
                                            gm_tftb1odc739t5 t41 = gm_f_push_s5vr2a2(t38, &t40);
                                            if (((((uint8_t *)&t41))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t41) + 1, gm_src3k278vcmqgdr, 631, 31);
                                            gm_b0_1 t42 = GM_LD(gm_b0_1, (((uint8_t *)&t41) + 1));
                                        }
                                        goto me34;
mn35:;
                                        gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 631, 13);
me34:;
                                    }
lc25:;
                                    {
                                        uint64_t t44 = gm_l7;
                                        uint64_t r45; bool o46 = __builtin_add_overflow(t44, ((uint64_t)1ULL), &r45);
                                        uint64_t t47 = r45;
                                        gm_l7 = t47;
                                    }
                                }
lb25:;
                            }
                        }
                        {
                            goto lb5;
                        }
                        goto ix18;
ie18:;
ix18:;
                    }
                    goto me14;
mn15:;
                    gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 629, 5);
me14:;
                }
lc5:;
                {
                    uint64_t t49 = gm_l4;
                    uint64_t r50; bool o51 = __builtin_add_overflow(t49, ((uint64_t)1ULL), &r50);
                    uint64_t t52 = r50;
                    gm_l4 = t52;
                }
            }
lb5:;
        }
    }
    {
        gm_b16_8 t53 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
        uint8_t t54 = 0;
        F.ret = t53;
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
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_instantiate_adt_lbjcui1(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2) {
    struct gm_fr_instantiate_adt_lbjcui1 F;
    void * gm_l0; /* s */
    uint32_t gm_l1; /* template */
    gm_b16_8 gm_l2; /* args */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_t88rcucg1b7uk gm_l6; /* a */
    uint32_t gm_l7; /* inst */
    uint32_t gm_l9; /* inst */
    gm_t88rcucg1b7uk gm_l11; /* ty */
    gm_b16_8 gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    uint64_t gm_l16; /* i */
    uint32_t gm_l17; /* p */
    gm_t88rcucg1b7uk gm_l19; /* tag */
    gm_b16_8 gm_l20; /* variants */
    gm_b16_8 gm_l22; /* <tmp> */
    uint64_t gm_l23; /* <tmp> */
    gm_tkn42u7na3t81 gm_l25; /* payload */
    gm_t88rcucg1b7uk gm_l26; /* p */
    gm_b16_8 gm_l27; /* fields */
    uint8_t gm_l29; /* concrete */
    gm_b16_8 gm_l30; /* <tmp> */
    uint64_t gm_l31; /* <tmp> */
    gm_t88rcucg1b7uk gm_l32; /* a */
    gm_b16_8 gm_l35; /* <tmp> */
    uint64_t gm_l36; /* <tmp> */
    gm_t5g5vgung6h6s gm_l37; /* i */
    gm_t88rcucg1b7uk gm_l39; /* t */
    gm_b16_8 gm_l40; /* <tmp> */
    uint64_t gm_l41; /* <tmp> */
    gm_t5g5vgung6h6s gm_l42; /* i */
    gm_t88rcucg1b7uk gm_l43; /* t */
    gm_b16_8 gm_l44; /* <tmp> */
    uint64_t gm_l45; /* <tmp> */
    gm_t5g5vgung6h6s gm_l46; /* i */
    gm_t88rcucg1b7uk gm_l47; /* t */
    GM_STACK_CHECK(&F, 4042u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        gm_f_list_vmvl81b((void *)&F.l3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t3 = gm_l2;
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
                    gm_t88rcucg1b7uk t15 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t11 * 4));
                    gm_l6 = t15;
                    {
                        void * t17 = (void *)((uint8_t *)&F.l3);
                        gm_t88rcucg1b7uk t18 = gm_l6;
                        gm_toa2vhe3l4hfr t19;
                        memset(&t19, 0, sizeof t19);
                        GM_ST(uint8_t, ((uint8_t *)&t19 + 0), ((uint8_t)0ULL));
                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t19 + 8), t18);
                        gm_tftb1odc739t5 t20 = gm_f_push_s5vr2a2(t17, &t19);
                        if (((((uint8_t *)&t20))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t20) + 1, gm_src3k278vcmqgdr, 549, 21);
                        gm_b0_1 t21 = GM_LD(gm_b0_1, (((uint8_t *)&t20) + 1));
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 549, 5);
me13:;
                }
lc4:;
                {
                    uint64_t t23 = gm_l5;
                    uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
                    uint64_t t26 = r24;
                    gm_l5 = t26;
                }
            }
lb4:;
        }
    }
    {
        void * t28 = gm_l0;
        void * t29 = (void *)(((uint8_t *)t28) + 1208);
        gm_tiilmfd16v3fc t30;
        memset((uint8_t *)&t30 + 4, 0, 4);
        uint32_t t31 = gm_l1;
        GM_ST(uint32_t, (uint8_t *)&t30 + 0, t31);
        gm_b16_8 t32 = gm_l2;
        GM_ST(gm_b16_8, (uint8_t *)&t30 + 8, t32);
        gm_t9805qtonbsag t33 = gm_f_get_6qnso5a(t29, &t30);
        if (!((((uint8_t *)&t33))[4] != 0)) goto ie27;
        uint32_t t34 = GM_LD(uint32_t, ((uint8_t *)&t33));
        gm_l7 = t34;
        {
            void * t36 = gm_l0;
            void * t37 = (void *)(((uint8_t *)t36) + 8);
            uint32_t t38 = gm_l7;
            gm_b16_8 t39 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_b16_8 t40 = GM_LD(gm_b16_8, ((uint8_t *)&t39));
            gm_t88rcucg1b7uk t41 = gm_f_adt_luat93d((void *)NULL, cx, NULL, t37, t38, t40);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t42 = 0;
            F.ret = t41;
            goto gm_fret;
        }
        goto ix27;
ie27:;
ix27:;
    }
    {
        void * t44 = gm_l0;
        uint32_t t45 = GM_LD(uint32_t, (((uint8_t *)t44) + 1816));
        uint8_t t46 = (t45 >= ((uint32_t)64ULL));
        if (!t46) goto ie43;
        {
            void * t49 = gm_l0;
            uint8_t t50 = GM_LD(uint8_t, (((uint8_t *)t49) + 1864));
            uint8_t t51 = (uint8_t)!t50;
            if (!t51) goto ie48;
            {
                void * t53 = gm_l0;
                GM_ST(uint8_t, (((uint8_t *)t53) + 1864), ((uint8_t)1));
            }
            {
                void * t54 = gm_l0;
                uint32_t t55 = gm_l1;
                uint64_t t56 = (uint64_t)t55;
                uint64_t t57 = (uintptr_t)t56;
                void * t58 = gm_l0;
                uint64_t t59 = GM_LD(uintptr_t, ((((uint8_t *)t58) + 1088) + 0) + 8);
                if (t57 >= (uintptr_t)(t59)) gm_trap_index(t57, t59, gm_src3k278vcmqgdr, 556, 21);
                gm_tl874t2atnlh3 t60 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t58) + 1088) + 0)) + t57 * 184) + 156));
                gm_t0bf4lga76ktk t61;
                memset((uint8_t *)&t61 + 20, 0, 4);
                uint32_t t62 = gm_l1;
                uint64_t t63 = (uint64_t)t62;
                uint64_t t64 = (uintptr_t)t63;
                void * t65 = gm_l0;
                uint64_t t66 = GM_LD(uintptr_t, ((((uint8_t *)t65) + 1088) + 0) + 8);
                if (t64 >= (uintptr_t)(t66)) gm_trap_index(t64, t66, gm_src3k278vcmqgdr, 556, 156);
                gm_b16_8 t67 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t65) + 1088) + 0)) + t64 * 184) + 0));
                GM_ST(gm_b16_8, (uint8_t *)&t61 + 0, t67);
                GM_ST(int32_t, (uint8_t *)&t61 + 16, ((int32_t)64LL));
                gm_tabb710d9mq2e t68;
                gm_f_format_bsjq7um((void *)&t68, (void *)NULL, cx, NULL, &t61);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t68))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t68) + 8, gm_src3k278vcmqgdr, 556, 57);
                gm_b16_8 t69 = GM_LD(gm_b16_8, (((uint8_t *)&t68) + 8));
                gm_b16_8 t70 = GM_LD(gm_b16_8, ((uint8_t *)&t69));
                gm_b0_1 t71 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t54, t60, t70);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix48;
ie48:;
ix48:;
        }
        {
            void * t72 = gm_l0;
            void * t73 = (void *)(((uint8_t *)t72) + 8);
            gm_t88rcucg1b7uk t74 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t73);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t75 = 0;
            F.ret = t74;
            goto gm_fret;
        }
        goto ix43;
ie43:;
ix43:;
    }
    {
        uint32_t t76 = gm_l1;
        uint64_t t77 = (uint64_t)t76;
        uint64_t t78 = (uintptr_t)t77;
        void * t79 = gm_l0;
        uint64_t t80 = GM_LD(uintptr_t, ((((uint8_t *)t79) + 1088) + 0) + 8);
        if (t78 >= (uintptr_t)(t80)) gm_trap_index(t78, t80, gm_src3k278vcmqgdr, 560, 13);
        gm_t6art7bprjiqb t81 = GM_LD(gm_t6art7bprjiqb, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t79) + 1088) + 0)) + t78 * 184));
        F.l8 = t81;
    }
    {
        void * t82 = gm_l0;
        void * t83 = (void *)(((uint8_t *)t82) + 8);
        gm_b16_8 t84 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 0));
        uint32_t t85 = gm_f_fresh_adt_u9rnf9f((void *)NULL, cx, NULL, t83, t84);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l9 = t85;
    }
    {
        void * t86 = gm_l0;
        void * t87 = (void *)(((uint8_t *)t86) + 8);
        uint32_t t88 = gm_l9;
        uint32_t t89 = gm_l1;
        gm_f_set_template_o09q96o(t87, t88, t89);
    }
    {
        gm_f_list_nof7vfj((void *)&F.l10, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t90 = (void *)((uint8_t *)&F.l10);
        gm_b16_8 t91 = gm_l2;
        gm_tftb1odc739t5 t92 = gm_f_extend_lfqasgq(t90, t91);
        if (((((uint8_t *)&t92))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t92) + 1, gm_src3k278vcmqgdr, 564, 5);
        gm_b0_1 t93 = GM_LD(gm_b0_1, (((uint8_t *)&t92) + 1));
    }
    {
        void * t94 = gm_l0;
        void * t95 = (void *)(((uint8_t *)t94) + 1168);
        gm_t5g5vgung6h6s t96;
        memset((uint8_t *)&t96 + 4, 0, 4);
        memset((uint8_t *)&t96 + 28, 0, 4);
        uint32_t t97 = gm_l1;
        GM_ST(uint32_t, (uint8_t *)&t96 + 0, t97);
        gm_b16_8 t98 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t96 + 8, t98);
        uint32_t t99 = gm_l9;
        GM_ST(uint32_t, (uint8_t *)&t96 + 24, t99);
        gm_tftb1odc739t5 t100 = gm_f_push_u4kbe6d(t95, &t96);
        if (((((uint8_t *)&t100))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t100) + 1, gm_src3k278vcmqgdr, 565, 5);
        gm_b0_1 t101 = GM_LD(gm_b0_1, (((uint8_t *)&t100) + 1));
    }
    {
        void * t102 = gm_l0;
        void * t103 = (void *)(((uint8_t *)t102) + 1208);
        gm_tiilmfd16v3fc t104;
        memset((uint8_t *)&t104 + 4, 0, 4);
        uint32_t t105 = gm_l1;
        GM_ST(uint32_t, (uint8_t *)&t104 + 0, t105);
        gm_b16_8 t106 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 0));
        gm_b16_8 t107 = GM_LD(gm_b16_8, ((uint8_t *)&t106));
        GM_ST(gm_b16_8, (uint8_t *)&t104 + 8, t107);
        uint32_t t108 = gm_l9;
        gm_tftb1odc739t5 t109 = gm_f_put_f1iksvi(t103, &t104, t108);
        if (((((uint8_t *)&t109))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t109) + 1, gm_src3k278vcmqgdr, 566, 5);
        gm_b0_1 t110 = GM_LD(gm_b0_1, (((uint8_t *)&t109) + 1));
    }
    {
        void * t111 = gm_l0;
        void * t112 = (void *)(((uint8_t *)t111) + 8);
        uint32_t t113 = gm_l9;
        gm_b16_8 t114 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
        gm_b16_8 t115 = GM_LD(gm_b16_8, ((uint8_t *)&t114));
        gm_t88rcucg1b7uk t116 = gm_f_adt_luat93d((void *)NULL, cx, NULL, t112, t113, t115);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l11 = t116;
    }
    {
        gm_b16_8 t117 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 0));
        uint64_t t118 = GM_LD(uint64_t, (((uint8_t *)&F.l8) + 16));
        gm_ti0kgh3pc3in2 t119;
        gm_f_empty_struct_b5i94nj((void *)&t119);
        gm_tl874t2atnlh3 t120 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l8) + 156));
        gm_tf5srh4mp4b3i t121;
        memset(&t121, 0, sizeof t121);
        gm_f_new_adt_info_5pl2srl((void *)&F.l12, t117, t118, &t119, GM_CONV(gm_cseahvg8v7p0vs, t120), t121);
    }
    {
        gm_b16_8 t122 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 80));
        GM_ST(gm_b16_8, (((uint8_t *)&F.l12) + 80), t122);
    }
    {
        gm_b16_8 t123 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 96));
        GM_ST(gm_b16_8, (((uint8_t *)&F.l12) + 96), t123);
    }
    {
        uint8_t t124 = GM_LD(uint8_t, (((uint8_t *)&F.l8) + 112));
        GM_ST(uint8_t, (((uint8_t *)&F.l12) + 112), t124);
    }
    {
        uint8_t t125 = GM_LD(uint8_t, (((uint8_t *)&F.l8) + 152));
        GM_ST(uint8_t, (((uint8_t *)&F.l12) + 152), t125);
    }
    {
        void * t126 = gm_l0;
        void * t127 = (void *)(((uint8_t *)t126) + 1088);
        gm_tftb1odc739t5 t128 = gm_f_push_j84ih6c(t127, &(*(gm_t6art7bprjiqb *)((uint8_t *)&F.l12)));
        if (((((uint8_t *)&t128))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t128) + 1, gm_src3k278vcmqgdr, 573, 5);
        gm_b0_1 t129 = GM_LD(gm_b0_1, (((uint8_t *)&t128) + 1));
    }
    {
        gm_f_list_oalq03d((void *)&F.l13, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t131 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 120));
            gm_b16_8 t132 = GM_LD(gm_b16_8, ((uint8_t *)&t131));
            gm_l14 = t132;
        }
        {
            gm_l15 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t134 = gm_l15;
                uint64_t t135 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                uint8_t t136 = (t134 < t135);
                if (!t136) goto lb133;
                {
                    uint64_t t139 = gm_l15;
                    uint64_t t140 = gm_l15;
                    uint64_t t141 = (uintptr_t)t140;
                    uint64_t t142 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                    uint32_t t143 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l14)) + t141 * 4));
                    gm_tn68el8d14vku t144;
                    GM_ST(uint32_t, (uint8_t *)&t144 + 12, 0);
                    GM_ST(uint64_t, ((uint8_t *)&t144 + 0), t139);
                    GM_ST(uint32_t, ((uint8_t *)&t144 + 8), t143);
                    uint64_t t147 = GM_LD(uint64_t, (((uint8_t *)&t144) + 0));
                    gm_l16 = t147;
                    uint32_t t148 = GM_LD(uint32_t, (((uint8_t *)&t144) + 8));
                    gm_l17 = t148;
                    {
                        uint64_t t151 = gm_l16;
                        uint64_t t152 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                        uint8_t t153 = (t151 < t152);
                        if (!t153) goto ie150;
                        {
                            void * t155 = (void *)((uint8_t *)&F.l13);
                            uint32_t t156 = gm_l17;
                            uint64_t t157 = gm_l16;
                            uint64_t t158 = (uintptr_t)t157;
                            uint64_t t159 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                            if (t158 >= (uintptr_t)(t159)) gm_trap_index(t158, t159, gm_src3k278vcmqgdr, 576, 56);
                            gm_t88rcucg1b7uk t160 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t158 * 4));
                            gm_tplpbqf3duvn3 t161;
                            GM_ST(uint32_t, ((uint8_t *)&t161 + 0), t156);
                            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t161 + 4), t160);
                            gm_tftb1odc739t5 t162 = gm_f_push_h69n5fi(t155, GM_CONV(gm_cs7e9ppsd384bj, t161));
                            if (((((uint8_t *)&t162))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t162) + 1, gm_src3k278vcmqgdr, 576, 27);
                            gm_b0_1 t163 = GM_LD(gm_b0_1, (((uint8_t *)&t162) + 1));
                        }
                        goto ix150;
ie150:;
ix150:;
                    }
                    goto me145;
mn146:;
                    gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 575, 5);
me145:;
                }
lc133:;
                {
                    uint64_t t165 = gm_l15;
                    uint64_t r166; bool o167 = __builtin_add_overflow(t165, ((uint64_t)1ULL), &r166);
                    uint64_t t168 = r166;
                    gm_l15 = t168;
                }
            }
lb133:;
        }
    }
    {
        void * t169 = gm_l0;
        gm_b16_8 t170 = GM_LD(gm_b16_8, (((uint8_t *)&F.l13) + 0));
        gm_b16_8 t171 = GM_LD(gm_b16_8, ((uint8_t *)&t170));
        gm_toceffb697p0u t172;
        memset(&t172, 0, sizeof t172);
        gm_b0_1 t173 = gm_f_check_len_exprs_m8s6m0h((void *)NULL, cx, NULL, t169, t171, t172);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t174 = gm_l0;
        void * t175 = gm_l0;
        uint32_t t176 = GM_LD(uint32_t, (((uint8_t *)t175) + 1816));
        uint32_t r177; bool o178 = __builtin_add_overflow(t176, ((uint32_t)1ULL), &r177);
        if (o178) gm_trap("integer overflow in `+`", gm_src3k278vcmqgdr, 579, 5);
        uint32_t t179 = r177;
        GM_ST(uint32_t, (((uint8_t *)t174) + 1816), t179);
    }
    {
        gm_ti0kgh3pc3in2 t180;
        uint8_t t186 = GM_LD(uint8_t, ((((uint8_t *)&F.l8) + 24) + 0));
        switch (t186) { case ((uint8_t)0ULL): goto ma182; case ((uint8_t)1ULL): goto ma183; case ((uint8_t)2ULL): goto ma184; default: goto ma185; }
ma182:;
        uint8_t t188 = GM_LD(uint8_t, ((((uint8_t *)&F.l8) + 24) + 0));
        if (t188 != ((uint8_t)0ULL)) goto mn187;
        gm_tpautr4br7h3l t189 = GM_LD(gm_tpautr4br7h3l, ((((uint8_t *)&F.l8) + 24) + 8));
        F.l18 = t189;
        gm_tpautr4br7h3l t190;
        memset((uint8_t *)&t190 + 17, 0, 7);
        void * t191 = gm_l0;
        gm_b16_8 t192 = GM_LD(gm_b16_8, (((uint8_t *)&F.l18) + 0));
        gm_b16_8 t193 = GM_LD(gm_b16_8, ((uint8_t *)&t192));
        gm_b16_8 t194 = GM_LD(gm_b16_8, (((uint8_t *)&F.l13) + 0));
        gm_b16_8 t195 = GM_LD(gm_b16_8, ((uint8_t *)&t194));
        gm_b16_8 t196 = gm_f_subst_fields_pqf9lb0((void *)NULL, cx, NULL, t191, t193, t195);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t190 + 0, t196);
        uint8_t t197 = GM_LD(uint8_t, (((uint8_t *)&F.l18) + 16));
        GM_ST(uint8_t, (uint8_t *)&t190 + 16, t197);
        gm_tf5srh4mp4b3i t198 = GM_LD(gm_tf5srh4mp4b3i, (((uint8_t *)&F.l18) + 24));
        GM_ST(gm_tf5srh4mp4b3i, (uint8_t *)&t190 + 24, t198);
        gm_tkn42u7na3t81 t199 = GM_LD(gm_tkn42u7na3t81, (((uint8_t *)&F.l18) + 40));
        GM_ST(gm_tkn42u7na3t81, (uint8_t *)&t190 + 40, t199);
        gm_ti0kgh3pc3in2 t200;
        GM_ST(uint8_t, (uint8_t *)&t200 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t200 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t200 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t200 + 0), ((uint8_t)0ULL));
        GM_ST(gm_tpautr4br7h3l, ((uint8_t *)&t200 + 8), t190);
        t180 = t200;
        goto me181;
mn187:;
ma183:;
        uint8_t t202 = GM_LD(uint8_t, ((((uint8_t *)&F.l8) + 24) + 0));
        if (t202 != ((uint8_t)1ULL)) goto mn201;
        gm_t88rcucg1b7uk t203 = GM_LD(gm_t88rcucg1b7uk, (((((uint8_t *)&F.l8) + 24) + 8) + 0));
        gm_l19 = t203;
        gm_b16_8 t204 = GM_LD(gm_b16_8, (((((uint8_t *)&F.l8) + 24) + 8) + 8));
        gm_l20 = t204;
        {
            gm_f_list_g6uul2f((void *)&F.l21, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_b16_8 t207 = gm_l20;
                gm_l22 = t207;
            }
            {
                gm_l23 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t209 = gm_l23;
                    uint64_t t210 = GM_LD(uintptr_t, ((uint8_t *)&gm_l22) + 8);
                    uint8_t t211 = (t209 < t210);
                    if (!t211) goto lb208;
                    {
                        uint64_t t214 = gm_l23;
                        uint64_t t215 = (uintptr_t)t214;
                        uint64_t t216 = GM_LD(uintptr_t, ((uint8_t *)&gm_l22) + 8);
                        gm_tckdobunaulee t219 = GM_LD(gm_tckdobunaulee, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l22)) + t215 * 72));
                        F.l24 = t219;
                        {
                            gm_tkn42u7na3t81 t221;
                            memset(&t221, 0, sizeof t221);
                            gm_l25 = t221;
                        }
                        {
                            if (!(((((uint8_t *)&F.l24) + 16))[4] != 0)) goto ie222;
                            gm_t88rcucg1b7uk t223 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l24) + 16));
                            gm_l26 = t223;
                            {
                                void * t225 = gm_l0;
                                gm_t88rcucg1b7uk t226 = gm_l26;
                                gm_b16_8 t227 = GM_LD(gm_b16_8, (((uint8_t *)&F.l13) + 0));
                                gm_b16_8 t228 = GM_LD(gm_b16_8, ((uint8_t *)&t227));
                                gm_t88rcucg1b7uk t229 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t225, t226, t228);
                                if (gm_unw.kind) {
                                    goto gm_ll212;
                                }
                                gm_tkn42u7na3t81 t230;
                                GM_ST(uint8_t, (uint8_t *)&t230 + 5, 0);
                                GM_ST(uint16_t, (uint8_t *)&t230 + 6, 0);
                                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t230), t229);
                                ((uint8_t *)&t230)[4] = 1;
                                gm_l25 = t230;
                            }
                            goto ix222;
ie222:;
ix222:;
                        }
                        {
                            void * t231 = (void *)((uint8_t *)&F.l21);
                            gm_tckdobunaulee t232;
                            gm_b16_8 t233 = GM_LD(gm_b16_8, (((uint8_t *)&F.l24) + 0));
                            GM_ST(gm_b16_8, (uint8_t *)&t232 + 0, t233);
                            gm_tkn42u7na3t81 t234 = gm_l25;
                            GM_ST(gm_tkn42u7na3t81, (uint8_t *)&t232 + 16, t234);
                            memcpy((uint8_t *)&t232 + 24, (((uint8_t *)&F.l24) + 24), 48);
                            gm_tftb1odc739t5 t235 = gm_f_push_1j8vbii(t231, &t232);
                            if (((((uint8_t *)&t235))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t235) + 1, gm_src3k278vcmqgdr, 587, 17);
                            gm_b0_1 t236 = GM_LD(gm_b0_1, (((uint8_t *)&t235) + 1));
                        }
                        goto me217;
mn218:;
                        gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 584, 13);
me217:;
                    }
lc208:;
                    {
                        uint64_t t238 = gm_l23;
                        uint64_t r239; bool o240 = __builtin_add_overflow(t238, ((uint64_t)1ULL), &r239);
                        uint64_t t241 = r239;
                        gm_l23 = t241;
                    }
                }
gm_ll212:;
                if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb208; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc208; }
                }
                goto gm_lad;
lb208:;
            }
        }
        gm_tcmkompbl09il t242;
        memset((uint8_t *)&t242 + 4, 0, 4);
        gm_t88rcucg1b7uk t243 = gm_l19;
        GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t242 + 0, t243);
        gm_b16_8 t244 = GM_LD(gm_b16_8, (((uint8_t *)&F.l21) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t242 + 8, t244);
        gm_ti0kgh3pc3in2 t245;
        GM_ST(uint8_t, (uint8_t *)&t245 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t245 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t245 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t245 + 32, 0);
        GM_ST(uint64_t, (uint8_t *)&t245 + 40, 0);
        GM_ST(uint64_t, (uint8_t *)&t245 + 48, 0);
        GM_ST(uint8_t, ((uint8_t *)&t245 + 0), ((uint8_t)1ULL));
        GM_ST(gm_tcmkompbl09il, ((uint8_t *)&t245 + 8), t242);
        gm_ti0kgh3pc3in2 t246 = t245;
        t180 = t246;
        goto me181;
mn201:;
ma184:;
        uint8_t t248 = GM_LD(uint8_t, ((((uint8_t *)&F.l8) + 24) + 0));
        if (t248 != ((uint8_t)2ULL)) goto mn247;
        gm_b16_8 t249 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l8) + 24) + 8));
        gm_l27 = t249;
        void * t250 = gm_l0;
        gm_b16_8 t251 = gm_l27;
        gm_b16_8 t252 = GM_LD(gm_b16_8, ((uint8_t *)&t251));
        gm_b16_8 t253 = GM_LD(gm_b16_8, (((uint8_t *)&F.l13) + 0));
        gm_b16_8 t254 = GM_LD(gm_b16_8, ((uint8_t *)&t253));
        gm_b16_8 t255 = gm_f_subst_fields_pqf9lb0((void *)NULL, cx, NULL, t250, t252, t254);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_ti0kgh3pc3in2 t256;
        memset(&t256, 0, sizeof t256);
        GM_ST(uint8_t, ((uint8_t *)&t256 + 0), ((uint8_t)2ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t256 + 8), t255);
        t180 = t256;
        goto me181;
mn247:;
ma185:;
        gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 580, 21);
me181:;
        F.l28 = t180;
    }
    {
        uint32_t t257 = gm_l9;
        uint64_t t258 = (uint64_t)t257;
        uint64_t t259 = (uintptr_t)t258;
        void * t260 = gm_l0;
        uint64_t t261 = GM_LD(uintptr_t, ((((uint8_t *)t260) + 1088) + 0) + 8);
        if (t259 >= (uintptr_t)(t261)) gm_trap_index(t259, t261, gm_src3k278vcmqgdr, 593, 5);
        gm_ti0kgh3pc3in2 t262 = F.l28;
        GM_ST(gm_ti0kgh3pc3in2, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t260) + 1088) + 0)) + t259 * 184) + 24), t262);
    }
    {
        void * t263 = gm_l0;
        void * t264 = gm_l0;
        uint32_t t265 = GM_LD(uint32_t, (((uint8_t *)t264) + 1816));
        uint32_t r266; bool o267 = __builtin_sub_overflow(t265, ((uint32_t)1ULL), &r266);
        if (o267) gm_trap("integer overflow in `-`", gm_src3k278vcmqgdr, 594, 5);
        uint32_t t268 = r266;
        GM_ST(uint32_t, (((uint8_t *)t263) + 1816), t268);
    }
    {
        gm_l29 = ((uint8_t)1);
    }
    {
        {
            gm_b16_8 t270 = gm_l2;
            gm_l30 = t270;
        }
        {
            gm_l31 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t272 = gm_l31;
                uint64_t t273 = GM_LD(uintptr_t, ((uint8_t *)&gm_l30) + 8);
                uint8_t t274 = (t272 < t273);
                if (!t274) goto lb271;
                {
                    uint64_t t277 = gm_l31;
                    uint64_t t278 = (uintptr_t)t277;
                    uint64_t t279 = GM_LD(uintptr_t, ((uint8_t *)&gm_l30) + 8);
                    gm_t88rcucg1b7uk t282 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l30)) + t278 * 4));
                    gm_l32 = t282;
                    {
                        void * t285 = gm_l0;
                        void * t286 = (void *)(((uint8_t *)t285) + 8);
                        gm_t88rcucg1b7uk t287 = gm_l32;
                        uint8_t t288 = gm_f_has_param_u9t0efs(t286, GM_CONV(gm_csguankko4lmhd, t287));
                        if (!t288) goto ie284;
                        {
                            gm_l29 = ((uint8_t)0);
                        }
                        goto ix284;
ie284:;
ix284:;
                    }
                    goto me280;
mn281:;
                    gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 596, 5);
me280:;
                }
lc271:;
                {
                    uint64_t t291 = gm_l31;
                    uint64_t r292; bool o293 = __builtin_add_overflow(t291, ((uint64_t)1ULL), &r292);
                    uint64_t t294 = r292;
                    gm_l31 = t294;
                }
            }
lb271:;
        }
    }
    {
        uint8_t t296 = gm_l29;
        if (!t296) goto ie295;
        {
            void * t298 = gm_l0;
            void * t299 = (void *)(((uint8_t *)t298) + 1824);
            gm_t5g5vgung6h6s t300;
            memset((uint8_t *)&t300 + 4, 0, 4);
            memset((uint8_t *)&t300 + 28, 0, 4);
            uint32_t t301 = gm_l1;
            GM_ST(uint32_t, (uint8_t *)&t300 + 0, t301);
            gm_b16_8 t302 = gm_f_empty_slice_rcd2jln();
            GM_ST(gm_b16_8, (uint8_t *)&t300 + 8, t302);
            uint32_t t303 = gm_l9;
            GM_ST(uint32_t, (uint8_t *)&t300 + 24, t303);
            gm_tftb1odc739t5 t304 = gm_f_push_u4kbe6d(t299, &t300);
            if (((((uint8_t *)&t304))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t304) + 1, gm_src3k278vcmqgdr, 599, 19);
            gm_b0_1 t305 = GM_LD(gm_b0_1, (((uint8_t *)&t304) + 1));
        }
        goto ix295;
ie295:;
ix295:;
    }
    {
        void * t307 = gm_l0;
        uint32_t t308 = GM_LD(uint32_t, (((uint8_t *)t307) + 1816));
        uint8_t t309 = ((t308 == ((uint32_t)0ULL)));
        if (!t309) goto ie306;
        {
            void * t311 = gm_l0;
            gm_trqfvmjtt4sij t312 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)t311) + 1824));
            F.l33 = t312;
        }
        {
            void * t313 = gm_l0;
            uint8_t *t314 = (((uint8_t *)t313) + 1824);
            gm_f_list_au01ire((void *)&(*(gm_trqfvmjtt4sij *)t314), (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_f_list_go2qg0b((void *)&F.l34, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_b16_8 t316 = GM_LD(gm_b16_8, (((uint8_t *)&F.l33) + 0));
                gm_l35 = t316;
            }
            {
                gm_l36 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t318 = gm_l36;
                    uint64_t t319 = GM_LD(uintptr_t, ((uint8_t *)&gm_l35) + 8);
                    uint8_t t320 = (t318 < t319);
                    if (!t320) goto lb317;
                    {
                        uint64_t t323 = gm_l36;
                        uint64_t t324 = (uintptr_t)t323;
                        uint64_t t325 = GM_LD(uintptr_t, ((uint8_t *)&gm_l35) + 8);
                        gm_t5g5vgung6h6s t328 = GM_LD(gm_t5g5vgung6h6s, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l35)) + t324 * 32));
                        gm_l37 = t328;
                        {
                            gm_f_list_go2qg0b((void *)&F.l38, (void *)NULL, cx, NULL);
                            if (gm_unw.kind) {
                                goto gm_ll321;
                            }
                        }
                        {
                            void * t330 = gm_l0;
                            void * t331 = (void *)(((uint8_t *)t330) + 8);
                            uint32_t t332 = GM_LD(uint32_t, (((uint8_t *)&gm_l37) + 24));
                            void * t333 = gm_l0;
                            uint32_t t334 = GM_LD(uint32_t, (((uint8_t *)&gm_l37) + 24));
                            gm_b16_8 t335 = gm_f_instance_args_15ek7cf((void *)NULL, cx, NULL, t333, t334);
                            if (gm_unw.kind) {
                                goto gm_ll321;
                            }
                            gm_b16_8 t336 = GM_LD(gm_b16_8, ((uint8_t *)&t335));
                            gm_t88rcucg1b7uk t337 = gm_f_adt_luat93d((void *)NULL, cx, NULL, t331, t332, t336);
                            if (gm_unw.kind) {
                                goto gm_ll321;
                            }
                            gm_l39 = t337;
                        }
                        {
                            void * t339 = gm_l0;
                            gm_t88rcucg1b7uk t340 = gm_l39;
                            uint32_t t341 = GM_LD(uint32_t, (((uint8_t *)&gm_l37) + 24));
                            void * t342 = (void *)((uint8_t *)&F.l38);
                            uint8_t t343 = gm_f_contains_by_value_pcdi4kq((void *)NULL, cx, NULL, t339, t340, t341, t342);
                            if (gm_unw.kind) {
                                goto gm_ll321;
                            }
                            if (!t343) goto ie338;
                            {
                                void * t345 = (void *)((uint8_t *)&F.l34);
                                uint32_t t346 = GM_LD(uint32_t, (((uint8_t *)&gm_l37) + 24));
                                gm_tftb1odc739t5 t347 = gm_f_push_im9d4ro(t345, t346);
                                if (((((uint8_t *)&t347))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t347) + 1, gm_src3k278vcmqgdr, 609, 56);
                                gm_b0_1 t348 = GM_LD(gm_b0_1, (((uint8_t *)&t347) + 1));
                            }
                            goto ix338;
ie338:;
ix338:;
                        }
                        goto me326;
mn327:;
                        gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 606, 9);
me326:;
                    }
lc317:;
                    {
                        uint64_t t350 = gm_l36;
                        uint64_t r351; bool o352 = __builtin_add_overflow(t350, ((uint64_t)1ULL), &r351);
                        uint64_t t353 = r351;
                        gm_l36 = t353;
                    }
                }
gm_ll321:;
                if (gm_unw.frame == &F.h && gm_unw.label == 4) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb317; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc317; }
                }
                goto gm_lad;
lb317:;
            }
        }
        {
            {
                gm_b16_8 t355 = GM_LD(gm_b16_8, (((uint8_t *)&F.l33) + 0));
                gm_l40 = t355;
            }
            {
                gm_l41 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t357 = gm_l41;
                    uint64_t t358 = GM_LD(uintptr_t, ((uint8_t *)&gm_l40) + 8);
                    uint8_t t359 = (t357 < t358);
                    if (!t359) goto lb356;
                    {
                        uint64_t t362 = gm_l41;
                        uint64_t t363 = (uintptr_t)t362;
                        uint64_t t364 = GM_LD(uintptr_t, ((uint8_t *)&gm_l40) + 8);
                        gm_t5g5vgung6h6s t367 = GM_LD(gm_t5g5vgung6h6s, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l40)) + t363 * 32));
                        gm_l42 = t367;
                        {
                            uint32_t t370 = GM_LD(uint32_t, (((uint8_t *)&gm_l42) + 24));
                            gm_b16_8 t371 = GM_LD(gm_b16_8, (((uint8_t *)&F.l34) + 0));
                            uint8_t t372 = 0;
                            for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t371) + 8)); i++) if (gm_eq467d5cm1d5(((uint8_t *)GM_LD(void *, ((uint8_t *)&t371))) + i * 4, ((uint8_t *)&t370))) { t372 = 1; break; }
                            uint8_t t373 = (uint8_t)(!t372);
                            if (!t373) goto ie369;
                            {
                                goto lc356;
                            }
                            goto ix369;
ie369:;
ix369:;
                        }
                        {
                            void * t375 = gm_l0;
                            void * t376 = (void *)(((uint8_t *)t375) + 8);
                            uint32_t t377 = GM_LD(uint32_t, (((uint8_t *)&gm_l42) + 24));
                            void * t378 = gm_l0;
                            uint32_t t379 = GM_LD(uint32_t, (((uint8_t *)&gm_l42) + 24));
                            gm_b16_8 t380 = gm_f_instance_args_15ek7cf((void *)NULL, cx, NULL, t378, t379);
                            if (gm_unw.kind) {
                                goto gm_ll360;
                            }
                            gm_b16_8 t381 = GM_LD(gm_b16_8, ((uint8_t *)&t380));
                            gm_t88rcucg1b7uk t382 = gm_f_adt_luat93d((void *)NULL, cx, NULL, t376, t377, t381);
                            if (gm_unw.kind) {
                                goto gm_ll360;
                            }
                            gm_l43 = t382;
                        }
                        {
                            void * t383 = gm_l0;
                            uint32_t t384 = GM_LD(uint32_t, (((uint8_t *)&gm_l42) + 0));
                            uint64_t t385 = (uint64_t)t384;
                            uint64_t t386 = (uintptr_t)t385;
                            void * t387 = gm_l0;
                            uint64_t t388 = GM_LD(uintptr_t, ((((uint8_t *)t387) + 1088) + 0) + 8);
                            if (t386 >= (uintptr_t)(t388)) gm_trap_index(t386, t388, gm_src3k278vcmqgdr, 614, 21);
                            gm_tl874t2atnlh3 t389 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t387) + 1088) + 0)) + t386 * 184) + 156));
                            void * t390 = gm_l0;
                            gm_t88rcucg1b7uk t391 = gm_l43;
                            gm_b16_8 t392 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t390, t391);
                            if (gm_unw.kind) {
                                goto gm_ll360;
                            }
                            gm_tnpifv20irnph t393;
                            GM_ST(gm_b16_8, ((uint8_t *)&t393 + 0), t392);
                            gm_tabb710d9mq2e t394;
                            gm_f_format_0au88qf((void *)&t394, (void *)NULL, cx, NULL, t393);
                            if (gm_unw.kind) {
                                goto gm_ll360;
                            }
                            if (((((uint8_t *)&t394))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t394) + 8, gm_src3k278vcmqgdr, 614, 59);
                            gm_b16_8 t395 = GM_LD(gm_b16_8, (((uint8_t *)&t394) + 8));
                            gm_b16_8 t396 = GM_LD(gm_b16_8, ((uint8_t *)&t395));
                            gm_b0_1 t397 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t383, t389, t396);
                            if (gm_unw.kind) {
                                goto gm_ll360;
                            }
                        }
                        {
                            void * t398 = gm_l0;
                            uint32_t t399 = GM_LD(uint32_t, (((uint8_t *)&gm_l42) + 24));
                            gm_f_empty_adt_tdp3hro(t398, t399);
                        }
                        goto me365;
mn366:;
                        gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 611, 9);
me365:;
                    }
lc356:;
                    {
                        uint64_t t401 = gm_l41;
                        uint64_t r402; bool o403 = __builtin_add_overflow(t401, ((uint64_t)1ULL), &r402);
                        uint64_t t404 = r402;
                        gm_l41 = t404;
                    }
                }
gm_ll360:;
                if (gm_unw.frame == &F.h && gm_unw.label == 5) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb356; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc356; }
                }
                goto gm_lad;
lb356:;
            }
        }
        {
            {
                gm_b16_8 t406 = GM_LD(gm_b16_8, (((uint8_t *)&F.l33) + 0));
                gm_l44 = t406;
            }
            {
                gm_l45 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t408 = gm_l45;
                    uint64_t t409 = GM_LD(uintptr_t, ((uint8_t *)&gm_l44) + 8);
                    uint8_t t410 = (t408 < t409);
                    if (!t410) goto lb407;
                    {
                        uint64_t t413 = gm_l45;
                        uint64_t t414 = (uintptr_t)t413;
                        uint64_t t415 = GM_LD(uintptr_t, ((uint8_t *)&gm_l44) + 8);
                        gm_t5g5vgung6h6s t418 = GM_LD(gm_t5g5vgung6h6s, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l44)) + t414 * 32));
                        gm_l46 = t418;
                        {
                            uint32_t t421 = GM_LD(uint32_t, (((uint8_t *)&gm_l46) + 24));
                            gm_b16_8 t422 = GM_LD(gm_b16_8, (((uint8_t *)&F.l34) + 0));
                            uint8_t t423 = 0;
                            for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t422) + 8)); i++) if (gm_eq467d5cm1d5(((uint8_t *)GM_LD(void *, ((uint8_t *)&t422))) + i * 4, ((uint8_t *)&t421))) { t423 = 1; break; }
                            uint8_t t424 = (uint8_t)(t423);
                            if (!t424) goto ie420;
                            {
                                goto lc407;
                            }
                            goto ix420;
ie420:;
ix420:;
                        }
                        {
                            void * t426 = gm_l0;
                            void * t427 = (void *)(((uint8_t *)t426) + 8);
                            uint32_t t428 = GM_LD(uint32_t, (((uint8_t *)&gm_l46) + 24));
                            void * t429 = gm_l0;
                            uint32_t t430 = GM_LD(uint32_t, (((uint8_t *)&gm_l46) + 24));
                            gm_b16_8 t431 = gm_f_instance_args_15ek7cf((void *)NULL, cx, NULL, t429, t430);
                            if (gm_unw.kind) {
                                goto gm_ll411;
                            }
                            gm_b16_8 t432 = GM_LD(gm_b16_8, ((uint8_t *)&t431));
                            gm_t88rcucg1b7uk t433 = gm_f_adt_luat93d((void *)NULL, cx, NULL, t427, t428, t432);
                            if (gm_unw.kind) {
                                goto gm_ll411;
                            }
                            gm_l47 = t433;
                        }
                        {
                            void * t435 = gm_l0;
                            gm_t88rcucg1b7uk t436 = gm_l47;
                            uint32_t t437 = GM_LD(uint32_t, (((uint8_t *)&gm_l46) + 0));
                            uint64_t t438 = (uint64_t)t437;
                            uint64_t t439 = (uintptr_t)t438;
                            void * t440 = gm_l0;
                            uint64_t t441 = GM_LD(uintptr_t, ((((uint8_t *)t440) + 1088) + 0) + 8);
                            if (t439 >= (uintptr_t)(t441)) gm_trap_index(t439, t441, gm_src3k278vcmqgdr, 620, 38);
                            gm_tl874t2atnlh3 t442 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t440) + 1088) + 0)) + t439 * 184) + 156));
                            uint8_t t443 = gm_f_type_size_ok_tr1llg4((void *)NULL, cx, NULL, t435, t436, t442);
                            if (gm_unw.kind) {
                                goto gm_ll411;
                            }
                            uint8_t t444 = (uint8_t)!t443;
                            if (!t444) goto ie434;
                            {
                                void * t446 = gm_l0;
                                uint32_t t447 = GM_LD(uint32_t, (((uint8_t *)&gm_l46) + 24));
                                gm_f_empty_adt_tdp3hro(t446, t447);
                            }
                            goto ix434;
ie434:;
ix434:;
                        }
                        goto me416;
mn417:;
                        gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 617, 9);
me416:;
                    }
lc407:;
                    {
                        uint64_t t449 = gm_l45;
                        uint64_t r450; bool o451 = __builtin_add_overflow(t449, ((uint64_t)1ULL), &r450);
                        uint64_t t452 = r450;
                        gm_l45 = t452;
                    }
                }
gm_ll411:;
                if (gm_unw.frame == &F.h && gm_unw.label == 6) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb407; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc407; }
                }
                goto gm_lad;
lb407:;
            }
        }
        goto ix306;
ie306:;
ix306:;
    }
    {
        gm_t88rcucg1b7uk t453 = gm_l11;
        uint8_t t454 = 0;
        F.ret = t453;
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
__attribute__((aligned(4))) uint8_t gm_f_int_fits_imbqv0c(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_int_fits_imbqv0c F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l2; /* t */
    uint8_t gm_l3; /* signed */
    uint64_t gm_l4; /* bits */
    uint8_t gm_l5; /* sg */
    uint16_t gm_l6; /* b */
    GM_STACK_CHECK(&F, 474u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    gm_l2 = a2;
    {
        gm_l3 = ((uint8_t)0);
    }
    {
        gm_l4 = ((uint64_t)0ULL);
    }
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 8);
        gm_t88rcucg1b7uk t4 = gm_l2;
        gm_t53at80pfdus3 t5;
        gm_f_kind_jcnt4hm((void *)&t5, t3, GM_CONV(gm_csguankko4lmhd, t4));
        uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        switch (t12) { case ((uint8_t)0ULL): goto ma7; case ((uint8_t)1ULL): goto ma8; case ((uint8_t)2ULL): goto ma9; case ((uint8_t)3ULL): goto ma10; case ((uint8_t)4ULL): goto ma10; case ((uint8_t)5ULL): goto ma10; case ((uint8_t)6ULL): goto ma10; case ((uint8_t)7ULL): goto ma10; case ((uint8_t)8ULL): goto ma10; case ((uint8_t)9ULL): goto ma10; case ((uint8_t)10ULL): goto ma10; case ((uint8_t)11ULL): goto ma10; case ((uint8_t)12ULL): goto ma10; case ((uint8_t)13ULL): goto ma10; case ((uint8_t)14ULL): goto ma10; case ((uint8_t)15ULL): goto ma10; case ((uint8_t)16ULL): goto ma10; case ((uint8_t)17ULL): goto ma10; case ((uint8_t)18ULL): goto ma10; case ((uint8_t)19ULL): goto ma10; case ((uint8_t)20ULL): goto ma10; case ((uint8_t)21ULL): goto ma10; case ((uint8_t)22ULL): goto ma10; case ((uint8_t)23ULL): goto ma10; case ((uint8_t)24ULL): goto ma10; case ((uint8_t)25ULL): goto ma10; case ((uint8_t)26ULL): goto ma10; default: goto ma10; }
ma7:;
        uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t14 != ((uint8_t)0ULL)) goto mn13;
        uint8_t t15 = GM_LD(uint8_t, ((((uint8_t *)&t5) + 8) + 0));
        gm_l5 = t15;
        uint16_t t16 = GM_LD(uint16_t, ((((uint8_t *)&t5) + 8) + 2));
        gm_l6 = t16;
        {
            uint8_t t18 = gm_l5;
            gm_l3 = t18;
        }
        {
            uint16_t t19 = gm_l6;
            uint64_t t20 = (uint64_t)t19;
            gm_l4 = t20;
        }
        goto me6;
mn13:;
ma8:;
        uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t22 != ((uint8_t)1ULL)) goto mn21;
        {
            void * t24 = gm_l0;
            void * t25 = GM_LD(void *, (((uint8_t *)t24) + 0));
            void * t26 = (void *)(((uint8_t *)t25) + 88);
            uint32_t t27 = gm_f_ptr_bits_glps5i7(t26);
            uint64_t t28 = (uint64_t)t27;
            gm_l4 = t28;
        }
        goto me6;
mn21:;
ma9:;
        uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t30 != ((uint8_t)2ULL)) goto mn29;
        {
            gm_l3 = ((uint8_t)1);
        }
        {
            void * t32 = gm_l0;
            void * t33 = GM_LD(void *, (((uint8_t *)t32) + 0));
            void * t34 = (void *)(((uint8_t *)t33) + 88);
            uint32_t t35 = gm_f_ptr_bits_glps5i7(t34);
            uint64_t t36 = (uint64_t)t35;
            gm_l4 = t36;
        }
        goto me6;
mn29:;
ma10:;
        uint8_t t38 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
        goto me6;
mn37:;
ma11:;
        gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 423, 5);
me6:;
    }
    {
        uint8_t t40 = gm_f_is_negative_k87pp3n(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l1)));
        if (!t40) goto ie39;
        {
            uint8_t t43 = gm_l3;
            uint8_t t44 = (uint8_t)!t43;
            if (!t44) goto ie42;
            {
                uint8_t t46 = 0;
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            goto ix42;
ie42:;
ix42:;
        }
        {
            gm_tbci5ospk5nnn t47 = F.l1;
            gm_tbci5ospk5nnn t48;
            gm_f_one_08qg1op((void *)&t48, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tbci5ospk5nnn t49;
            gm_f_add_420lj6o((void *)&t49, (void *)NULL, cx, NULL, &t47, &t48);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tbci5ospk5nnn t50;
            gm_f_neg_agd5c5k((void *)&t50, (void *)NULL, cx, NULL, &t49);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint64_t t51 = gm_f_bits_lr3of4i(&t50);
            uint64_t t52 = gm_l4;
            uint64_t r53; bool o54 = __builtin_sub_overflow(t52, ((uint64_t)1ULL), &r53);
            if (o54) gm_trap("integer overflow in `-`", gm_src3k278vcmqgdr, 438, 52);
            uint64_t t55 = r53;
            uint8_t t56 = (t51 <= t55);
            uint8_t t57 = 0;
            F.ret = t56;
            goto gm_fret;
        }
        goto ix39;
ie39:;
ix39:;
    }
    {
        uint64_t t58 = gm_f_bits_lr3of4i(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l1)));
        uint64_t t59;
        uint8_t t61 = gm_l3;
        if (!t61) goto ie60;
        uint64_t t63 = gm_l4;
        uint64_t r64; bool o65 = __builtin_sub_overflow(t63, ((uint64_t)1ULL), &r64);
        if (o65) gm_trap("integer overflow in `-`", gm_src3k278vcmqgdr, 440, 37);
        uint64_t t66 = r64;
        uint64_t t67 = t66;
        t59 = t67;
        goto ix60;
ie60:;
        uint64_t t69 = gm_l4;
        uint64_t t70 = t69;
        t59 = t70;
ix60:;
        uint8_t t71 = (t58 <= t59);
        uint8_t t72 = 0;
        F.ret = t71;
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
__attribute__((aligned(4))) uint8_t gm_f_is_infinitely_sized_mhhcucl(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, void * a2) {
    struct gm_fr_is_infinitely_sized_mhhcucl F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    void * gm_l2; /* stack */
    uint32_t gm_l3; /* id */
    uint8_t gm_l4; /* r */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* i */
    gm_t88rcucg1b7uk gm_l9; /* t */
    gm_t88rcucg1b7uk gm_l10; /* elem */
    gm_t88rcucg1b7uk gm_l11; /* elem */
    gm_t88rcucg1b7uk gm_l12; /* elem */
    gm_b16_8 gm_l13; /* elems */
    gm_b16_8 gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    gm_t88rcucg1b7uk gm_l16; /* e */
    gm_t88rcucg1b7uk gm_l17; /* err */
    gm_t88rcucg1b7uk gm_l18; /* ok */
    GM_STACK_CHECK(&F, 666u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 8);
        gm_t88rcucg1b7uk t4 = gm_l1;
        gm_t53at80pfdus3 t5;
        gm_f_kind_jcnt4hm((void *)&t5, t3, GM_CONV(gm_csguankko4lmhd, t4));
        uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        switch (t13) { case ((uint8_t)0ULL): goto ma11; case ((uint8_t)1ULL): goto ma11; case ((uint8_t)2ULL): goto ma11; case ((uint8_t)3ULL): goto ma11; case ((uint8_t)4ULL): goto ma11; case ((uint8_t)5ULL): goto ma11; case ((uint8_t)6ULL): goto ma11; case ((uint8_t)7ULL): goto ma11; case ((uint8_t)8ULL): goto ma11; case ((uint8_t)9ULL): goto ma11; case ((uint8_t)10ULL): goto ma11; case ((uint8_t)11ULL): goto ma8; case ((uint8_t)12ULL): goto ma9; case ((uint8_t)13ULL): goto ma8; case ((uint8_t)14ULL): goto ma10; case ((uint8_t)15ULL): goto ma11; case ((uint8_t)16ULL): goto ma11; case ((uint8_t)17ULL): goto ma7; case ((uint8_t)18ULL): goto ma11; case ((uint8_t)19ULL): goto ma11; case ((uint8_t)20ULL): goto ma8; case ((uint8_t)21ULL): goto ma11; case ((uint8_t)22ULL): goto ma11; case ((uint8_t)23ULL): goto ma11; case ((uint8_t)24ULL): goto ma11; case ((uint8_t)25ULL): goto ma11; case ((uint8_t)26ULL): goto ma11; default: goto ma11; }
ma7:;
        uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t15 != ((uint8_t)17ULL)) goto mn14;
        uint32_t t16 = GM_LD(uint32_t, ((((uint8_t *)&t5) + 8) + 0));
        gm_l3 = t16;
        {
            uint32_t t19 = gm_l3;
            void * t20 = gm_l2;
            gm_b16_8 t21 = GM_LD(gm_b16_8, (((uint8_t *)t20) + 0));
            uint8_t t22 = 0;
            for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t21) + 8)); i++) if (gm_eq467d5cm1d5(((uint8_t *)GM_LD(void *, ((uint8_t *)&t21))) + i * 4, ((uint8_t *)&t19))) { t22 = 1; break; }
            uint8_t t23 = (uint8_t)(t22);
            if (!t23) goto ie18;
            {
                uint8_t t25 = 0;
                F.ret = ((uint8_t)1);
                goto gm_fret;
            }
            goto ix18;
ie18:;
ix18:;
        }
        {
            void * t26 = gm_l2;
            uint32_t t27 = gm_l3;
            gm_tftb1odc739t5 t28 = gm_f_push_im9d4ro(t26, t27);
            if (((((uint8_t *)&t28))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t28) + 1, gm_src3k278vcmqgdr, 468, 13);
            gm_b0_1 t29 = GM_LD(gm_b0_1, (((uint8_t *)&t28) + 1));
        }
        {
            gm_l4 = ((uint8_t)0);
        }
        {
            uint32_t t30 = gm_l3;
            uint64_t t31 = (uint64_t)t30;
            uint64_t t32 = (uintptr_t)t31;
            void * t33 = gm_l0;
            uint64_t t34 = GM_LD(uintptr_t, ((((uint8_t *)t33) + 1088) + 0) + 8);
            if (t32 >= (uintptr_t)(t34)) gm_trap_index(t32, t34, gm_src3k278vcmqgdr, 470, 21);
            gm_ti0kgh3pc3in2 t35 = GM_LD(gm_ti0kgh3pc3in2, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t33) + 1088) + 0)) + t32 * 184) + 24));
            F.l5 = t35;
        }
        {
            {
                gm_l7 = ((uint64_t)0ULL);
            }
            {
                uint64_t t37 = gm_f_member_count_otu9tc0(&(*(gm_ti0kgh3pc3in2 *)((uint8_t *)&F.l5)));
                gm_l6 = t37;
            }
            {
                for (;;) {
                    uint64_t t39 = gm_l7;
                    uint64_t t40 = gm_l6;
                    uint8_t t41 = (t39 < t40);
                    if (!t41) goto lb38;
                    {
                        uint64_t t44 = gm_l7;
                        uint64_t t47 = GM_LD(uint64_t, ((uint8_t *)&t44));
                        gm_l8 = t47;
                        {
                            uint64_t t49 = gm_l8;
                            gm_tkn42u7na3t81 t50 = gm_f_member_ty_esvuguv(&(*(gm_ti0kgh3pc3in2 *)((uint8_t *)&F.l5)), t49);
                            gm_t88rcucg1b7uk t51;
                            if (((((uint8_t *)&t50))[4] != 0)) {
                                gm_t88rcucg1b7uk t52 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t50) + 0));
                                t51 = t52;
                            } else {
                                goto lc38;
                            }
                            gm_l9 = t51;
                        }
                        {
                            void * t54 = gm_l0;
                            gm_t88rcucg1b7uk t55 = gm_l9;
                            void * t56 = gm_l2;
                            uint8_t t57 = gm_f_is_infinitely_sized_mhhcucl((void *)NULL, cx, NULL, t54, t55, t56);
                            if (gm_unw.kind) {
                                goto gm_ll42;
                            }
                            if (!t57) goto ie53;
                            {
                                gm_l4 = ((uint8_t)1);
                            }
                            {
                                goto lb38;
                            }
                            goto ix53;
ie53:;
ix53:;
                        }
                        goto me45;
mn46:;
                        gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 471, 13);
me45:;
                    }
lc38:;
                    {
                        uint64_t t60 = gm_l7;
                        uint64_t r61; bool o62 = __builtin_add_overflow(t60, ((uint64_t)1ULL), &r61);
                        uint64_t t63 = r61;
                        gm_l7 = t63;
                    }
                }
gm_ll42:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb38; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc38; }
                }
                goto gm_lad;
lb38:;
            }
        }
        {
            {
                void * t65 = gm_l2;
                gm_t9805qtonbsag t66 = gm_f_pop_570g79n(t65);
            }
        }
        {
            uint8_t t67 = gm_l4;
            uint8_t t68 = 0;
            F.ret = t67;
            goto gm_fret;
        }
        goto me6;
mn14:;
ma8:;
        uint8_t t72 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t72 != ((uint8_t)11ULL)) goto pa71;
        gm_t88rcucg1b7uk t73 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t5) + 8) + 8));
        gm_l10 = t73;
        goto po70;
pa71:;
        uint8_t t75 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t75 != ((uint8_t)20ULL)) goto pa74;
        gm_t88rcucg1b7uk t76 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t5) + 8) + 4));
        gm_l10 = t76;
        goto po70;
pa74:;
        uint8_t t78 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t78 != ((uint8_t)13ULL)) goto pa77;
        gm_t88rcucg1b7uk t79 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t5) + 8));
        gm_l10 = t79;
        goto po70;
pa77:;
        goto mn69;
po70:;
        void * t80 = gm_l0;
        gm_t88rcucg1b7uk t81 = gm_l10;
        void * t82 = gm_l2;
        uint8_t t83 = gm_f_is_infinitely_sized_mhhcucl((void *)NULL, cx, NULL, t80, t81, t82);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t84 = 0;
        F.ret = t83;
        goto gm_fret;
        goto me6;
mn69:;
ma9:;
        uint8_t t86 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t86 != ((uint8_t)12ULL)) goto mn85;
        gm_b16_8 t87 = GM_LD(gm_b16_8, ((((uint8_t *)&t5) + 8) + 0));
        gm_l13 = t87;
        {
            {
                gm_b16_8 t90 = gm_l13;
                gm_l14 = t90;
            }
            {
                gm_l15 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t92 = gm_l15;
                    uint64_t t93 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                    uint8_t t94 = (t92 < t93);
                    if (!t94) goto lb91;
                    {
                        uint64_t t97 = gm_l15;
                        uint64_t t98 = (uintptr_t)t97;
                        uint64_t t99 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                        gm_t88rcucg1b7uk t102 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l14)) + t98 * 4));
                        gm_l16 = t102;
                        {
                            void * t105 = gm_l0;
                            gm_t88rcucg1b7uk t106 = gm_l16;
                            void * t107 = gm_l2;
                            uint8_t t108 = gm_f_is_infinitely_sized_mhhcucl((void *)NULL, cx, NULL, t105, t106, t107);
                            if (gm_unw.kind) {
                                goto gm_ll95;
                            }
                            if (!t108) goto ie104;
                            {
                                uint8_t t110 = 0;
                                F.ret = ((uint8_t)1);
                                goto gm_fret;
                            }
                            goto ix104;
ie104:;
ix104:;
                        }
                        goto me100;
mn101:;
                        gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 483, 13);
me100:;
                    }
lc91:;
                    {
                        uint64_t t112 = gm_l15;
                        uint64_t r113; bool o114 = __builtin_add_overflow(t112, ((uint64_t)1ULL), &r113);
                        uint64_t t115 = r113;
                        gm_l15 = t115;
                    }
                }
gm_ll95:;
                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb91; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc91; }
                }
                goto gm_lad;
lb91:;
            }
        }
        {
            uint8_t t116 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto me6;
mn85:;
ma10:;
        uint8_t t118 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t118 != ((uint8_t)14ULL)) goto mn117;
        gm_t88rcucg1b7uk t119 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t5) + 8) + 0));
        gm_l17 = t119;
        gm_t88rcucg1b7uk t120 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t5) + 8) + 4));
        gm_l18 = t120;
        uint8_t t121 = 0;
        void * t122 = gm_l0;
        gm_t88rcucg1b7uk t123 = gm_l17;
        void * t124 = gm_l2;
        uint8_t t125 = gm_f_is_infinitely_sized_mhhcucl((void *)NULL, cx, NULL, t122, t123, t124);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t125) {
            void * t126 = gm_l0;
            gm_t88rcucg1b7uk t127 = gm_l18;
            void * t128 = gm_l2;
            uint8_t t129 = gm_f_is_infinitely_sized_mhhcucl((void *)NULL, cx, NULL, t126, t127, t128);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            t121 = t129;
        } else t121 = 1;
        uint8_t t130 = 0;
        F.ret = t121;
        goto gm_fret;
        goto me6;
mn117:;
ma11:;
        uint8_t t132 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
        goto me6;
mn131:;
ma12:;
        gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 465, 5);
me6:;
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
inline __attribute__((aligned(4))) uint64_t gm_f_member_count_otu9tc0(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_member_count_otu9tc0 F;
    gm_b16_8 gm_l2; /* fields */
    gm_b16_8 gm_l3; /* variants */
    GM_STACK_CHECK_SHALLOW(&F, 164u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint64_t t1;
    uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    switch (t7) { case ((uint8_t)0ULL): goto ma3; case ((uint8_t)1ULL): goto ma5; case ((uint8_t)2ULL): goto ma4; default: goto ma6; }
ma3:;
    uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t9 != ((uint8_t)0ULL)) goto mn8;
    gm_tpautr4br7h3l t10 = GM_LD(gm_tpautr4br7h3l, (((uint8_t *)a0) + 8));
    F.l1 = t10;
    uint64_t t11 = GM_LD(uintptr_t, (((uint8_t *)&F.l1) + 0) + 8);
    t1 = t11;
    goto me2;
mn8:;
ma4:;
    uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t13 != ((uint8_t)2ULL)) goto mn12;
    gm_b16_8 t14 = GM_LD(gm_b16_8, (((uint8_t *)a0) + 8));
    gm_l2 = t14;
    uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
    t1 = t15;
    goto me2;
mn12:;
ma5:;
    uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t17 != ((uint8_t)1ULL)) goto mn16;
    gm_b16_8 t18 = GM_LD(gm_b16_8, ((((uint8_t *)a0) + 8) + 8));
    gm_l3 = t18;
    uint64_t t19 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
    t1 = t19;
    goto me2;
mn16:;
ma6:;
    gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 82, 47);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint64_t gm_f_member_count_otu9tc0_c(gm_ti0kgh3pc3in2 a0) { return gm_f_member_count_otu9tc0(&a0); }
inline __attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_member_ty_esvuguv(const void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_member_ty_esvuguv F;
    uint64_t gm_l1; /* i */
    gm_b16_8 gm_l3; /* fields */
    gm_b16_8 gm_l4; /* variants */
    GM_STACK_CHECK_SHALLOW(&F, 252u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l1 = a1;
    gm_tkn42u7na3t81 t1;
    uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    switch (t7) { case ((uint8_t)0ULL): goto ma3; case ((uint8_t)1ULL): goto ma5; case ((uint8_t)2ULL): goto ma4; default: goto ma6; }
ma3:;
    uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t9 != ((uint8_t)0ULL)) goto mn8;
    gm_tpautr4br7h3l t10 = GM_LD(gm_tpautr4br7h3l, (((uint8_t *)a0) + 8));
    F.l2 = t10;
    uint64_t t11 = gm_l1;
    uint64_t t12 = (uintptr_t)t11;
    uint64_t t13 = GM_LD(uintptr_t, (((uint8_t *)&F.l2) + 0) + 8);
    if (t12 >= (uintptr_t)(t13)) gm_trap_index(t12, t13, gm_src3k278vcmqgdr, 90, 21);
    gm_t88rcucg1b7uk t14 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l2) + 0)) + t12 * 40) + 16));
    gm_tkn42u7na3t81 t15;
    GM_ST(uint8_t, (uint8_t *)&t15 + 5, 0);
    GM_ST(uint16_t, (uint8_t *)&t15 + 6, 0);
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t15), t14);
    ((uint8_t *)&t15)[4] = 1;
    t1 = t15;
    goto me2;
mn8:;
ma4:;
    uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t17 != ((uint8_t)2ULL)) goto mn16;
    gm_b16_8 t18 = GM_LD(gm_b16_8, (((uint8_t *)a0) + 8));
    gm_l3 = t18;
    uint64_t t19 = gm_l1;
    uint64_t t20 = (uintptr_t)t19;
    uint64_t t21 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
    if (t20 >= (uintptr_t)(t21)) gm_trap_index(t20, t21, gm_src3k278vcmqgdr, 91, 24);
    gm_t88rcucg1b7uk t22 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t20 * 40) + 16));
    gm_tkn42u7na3t81 t23;
    GM_ST(uint8_t, (uint8_t *)&t23 + 5, 0);
    GM_ST(uint16_t, (uint8_t *)&t23 + 6, 0);
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t23), t22);
    ((uint8_t *)&t23)[4] = 1;
    t1 = t23;
    goto me2;
mn16:;
ma5:;
    uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t25 != ((uint8_t)1ULL)) goto mn24;
    gm_b16_8 t26 = GM_LD(gm_b16_8, ((((uint8_t *)a0) + 8) + 8));
    gm_l4 = t26;
    uint64_t t27 = gm_l1;
    uint64_t t28 = (uintptr_t)t27;
    uint64_t t29 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
    if (t28 >= (uintptr_t)(t29)) gm_trap_index(t28, t29, gm_src3k278vcmqgdr, 92, 28);
    gm_tkn42u7na3t81 t30 = GM_LD(gm_tkn42u7na3t81, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t28 * 72) + 16));
    t1 = t30;
    goto me2;
mn24:;
ma6:;
    gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 89, 52);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_member_ty_esvuguv_c(gm_ti0kgh3pc3in2 a0, uint64_t a1) { return gm_f_member_ty_esvuguv(&a0, a1); }
inline __attribute__((aligned(4))) void gm_f_new_adt_info_5pl2srl(void *gm_out, gm_b16_8 a0, uint64_t a1, const void * a2, gm_cseahvg8v7p0vs a3, gm_tf5srh4mp4b3i a4) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_new_adt_info_5pl2srl F;
    gm_b16_8 gm_l0; /* name */
    uint64_t gm_l1; /* module */
    gm_tl874t2atnlh3 gm_l3; /* span */
    gm_tf5srh4mp4b3i gm_l4; /* decl */
    GM_STACK_CHECK_SHALLOW(&F, 224u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l3 = GM_CONV(gm_tl874t2atnlh3, a3);
    gm_l4 = a4;
    memset((uint8_t *)&(*(gm_t6art7bprjiqb *)gm_out) + 113, 0, 7);
    memset((uint8_t *)&(*(gm_t6art7bprjiqb *)gm_out) + 153, 0, 3);
    gm_b16_8 t1 = gm_l0;
    uint64_t t2 = gm_l1;
    gm_ti0kgh3pc3in2 t3 = GM_LD(gm_ti0kgh3pc3in2, ((uint8_t *)a2));
    gm_b16_8 t4 = gm_f_empty_slice_s1lbe5s();
    gm_b16_8 t5 = gm_f_empty_slice_8q3kspo_2();
    gm_b16_8 t6 = gm_f_empty_slice_d914i5u();
    gm_b16_8 t7 = gm_f_empty_slice_8q3kspo_2();
    gm_tl874t2atnlh3 t8 = gm_l3;
    gm_tf5srh4mp4b3i t9 = gm_l4;
    GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t6art7bprjiqb *)gm_out) + 0, t1);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_t6art7bprjiqb *)gm_out) + 16, t2);
    GM_ST(gm_ti0kgh3pc3in2, (uint8_t *)&(*(gm_t6art7bprjiqb *)gm_out) + 24, t3);
    GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t6art7bprjiqb *)gm_out) + 80, t4);
    GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t6art7bprjiqb *)gm_out) + 96, t5);
    GM_ST(uint8_t, (uint8_t *)&(*(gm_t6art7bprjiqb *)gm_out) + 112, ((uint8_t)0));
    GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t6art7bprjiqb *)gm_out) + 120, t6);
    GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t6art7bprjiqb *)gm_out) + 136, t7);
    GM_ST(uint8_t, (uint8_t *)&(*(gm_t6art7bprjiqb *)gm_out) + 152, ((uint8_t)0));
    GM_ST(gm_tl874t2atnlh3, (uint8_t *)&(*(gm_t6art7bprjiqb *)gm_out) + 156, t8);
    GM_ST(gm_tf5srh4mp4b3i, (uint8_t *)&(*(gm_t6art7bprjiqb *)gm_out) + 168, t9);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_t6art7bprjiqb gm_f_new_adt_info_5pl2srl_c(gm_b16_8 a0, uint64_t a1, gm_ti0kgh3pc3in2 a2, gm_cseahvg8v7p0vs a3, gm_tf5srh4mp4b3i a4) { gm_t6art7bprjiqb gm_r; gm_f_new_adt_info_5pl2srl(&gm_r, a0, a1, &a2, a3, a4); return gm_r; }
inline __attribute__((aligned(4))) uint32_t gm_f_new_param_7010dt4(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tnnjeerolos0a a2) {
    struct gm_fr_new_param_7010dt4 F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l1; /* name */
    gm_tnnjeerolos0a gm_l2; /* kind */
    uint32_t gm_l3; /* id */
    GM_STACK_CHECK(&F, 159u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 8);
        gm_b16_8 t4 = gm_l1;
        uint32_t t5 = gm_f_fresh_param_442g4m9((void *)NULL, cx, NULL, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l3 = t5;
    }
    {
        void * t6 = gm_l0;
        void * t7 = (void *)(((uint8_t *)t6) + 1128);
        gm_tdtrcq4ju76rh t8;
        gm_tnnjeerolos0a t9 = gm_l2;
        GM_ST(gm_tnnjeerolos0a, (uint8_t *)&t8 + 0, t9);
        gm_f_list_5kg5cjm_2((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&t8 + 8)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tftb1odc739t5 t10 = gm_f_push_bth3krn(t7, &t8);
        if (((((uint8_t *)&t10))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t10) + 1, gm_src3k278vcmqgdr, 37, 5);
        gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
    }
    {
        uint32_t t12 = gm_l3;
        uint8_t t13 = 0;
        F.ret = t12;
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
inline __attribute__((aligned(4))) gm_tnnjeerolos0a gm_f_param_kind_of_1jngakt(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2) {
    struct gm_fr_param_kind_of_1jngakt F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* p */
    void * gm_l3; /* t */
    gm_t08olq97rscn3 gm_l4; /* i */
    GM_STACK_CHECK(&F, 291u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l2;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 24));
        void * t4;
        if ((GM_LD(void *, ((uint8_t *)&t3)) != 0)) {
            void * t5 = GM_LD(void *, (((uint8_t *)&t3) + 0));
            t4 = t5;
        } else {
            {
                void * t7 = gm_l0;
                void * t8 = gm_l2;
                gm_tl874t2atnlh3 t9 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t8) + 40));
                gm_b16_8 t10;
                GM_ST(void *, (uint8_t *)&t10, (void *)(gm_sf3irv2ans6de));
                GM_ST(uintptr_t, (uint8_t *)&t10 + 8, (uintptr_t)(56));
                gm_b0_1 t11 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t7, t9, t10);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            void * t12 = gm_l0;
            void * t13 = (void *)(((uint8_t *)t12) + 8);
            gm_t88rcucg1b7uk t14 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t13);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tnnjeerolos0a t15;
            GM_ST(uint8_t, (uint8_t *)&t15 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t15 + 2, 0);
            GM_ST(uint8_t, ((uint8_t *)&t15 + 0), ((uint8_t)2ULL));
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t15 + 4), t14);
            uint8_t t16 = 0;
            F.ret = t15;
            goto gm_fret;
        }
        gm_l3 = t4;
    }
    {
        void * t18 = gm_l3;
        uint8_t t19 = gm_f_is_effects_kind_uutbf9u(t18);
        if (!t19) goto ie17;
        {
            gm_tnnjeerolos0a t21;
            GM_ST(uint8_t, (uint8_t *)&t21 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t21 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t21 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t21 + 0), ((uint8_t)1ULL));
            uint8_t t22 = 0;
            F.ret = t21;
            goto gm_fret;
        }
        goto ix17;
ie17:;
ix17:;
    }
    {
        void * t24 = gm_l3;
        uint8_t t25 = GM_LD(uint8_t, ((((uint8_t *)t24) + 8) + 0));
        if (t25 != ((uint8_t)6ULL)) goto ie23;
        gm_t08olq97rscn3 t26 = GM_LD(gm_t08olq97rscn3, ((((uint8_t *)t24) + 8) + 8));
        gm_l4 = t26;
        uint8_t t27 = (((GM_LD(uintptr_t, (((uint8_t *)&gm_l4) + 0) + 8) == 4) && memcmp(GM_LD(void *, (((uint8_t *)&gm_l4) + 0)), gm_s2737i1iq57nf, 4) == 0));
        if (!t27) goto ie23;
        {
            gm_tnnjeerolos0a t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t29 + 0), ((uint8_t)0ULL));
            uint8_t t30 = 0;
            F.ret = t29;
            goto gm_fret;
        }
        goto ix23;
ie23:;
ix23:;
    }
    {
        void * t31 = gm_l0;
        uint64_t t32 = gm_l1;
        void * t33 = gm_l3;
        gm_t88rcucg1b7uk t34 = gm_f_resolve_type_jjodbei((void *)NULL, cx, NULL, t31, t32, t33);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnnjeerolos0a t35;
        GM_ST(uint8_t, (uint8_t *)&t35 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t35 + 2, 0);
        GM_ST(uint8_t, ((uint8_t *)&t35 + 0), ((uint8_t)2ULL));
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t35 + 4), t34);
        uint8_t t36 = 0;
        F.ret = t35;
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
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_pending_layout_ikknuv1(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pending_layout_ikknuv1 F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    uint32_t gm_l2; /* id */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* i */
    gm_t88rcucg1b7uk gm_l7; /* t */
    uint32_t gm_l8; /* p */
    gm_t8nckeqfenl8a gm_l9; /* a */
    gm_tgglga8ro3isc gm_l10; /* a */
    gm_t88rcucg1b7uk gm_l11; /* t */
    gm_t6j6smj7qoctu gm_l12; /* t */
    gm_b16_8 gm_l13; /* <tmp> */
    uint64_t gm_l14; /* <tmp> */
    gm_t88rcucg1b7uk gm_l15; /* e */
    uint32_t gm_l16; /* p */
    gm_tgglga8ro3isc gm_l17; /* r */
    uint32_t gm_l18; /* p */
    GM_STACK_CHECK(&F, 855u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    {
        void * t3 = gm_l0;
        uint64_t t4 = GM_LD(uintptr_t, ((((uint8_t *)t3) + 1728) + 0) + 8);
        uint8_t t5 = ((t4 == ((uint64_t)0ULL)));
        if (!t5) goto ie2;
        {
            gm_t9805qtonbsag t7;
            memset(&t7, 0, sizeof t7);
            uint8_t t8 = 0;
            F.ret = t7;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t9 = gm_l0;
        void * t10 = (void *)(((uint8_t *)t9) + 8);
        gm_t88rcucg1b7uk t11 = gm_l1;
        gm_t53at80pfdus3 t12;
        gm_f_kind_jcnt4hm((void *)&t12, t10, GM_CONV(gm_csguankko4lmhd, t11));
        uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)&t12) + 0));
        switch (t22) { case ((uint8_t)0ULL): goto ma20; case ((uint8_t)1ULL): goto ma20; case ((uint8_t)2ULL): goto ma20; case ((uint8_t)3ULL): goto ma20; case ((uint8_t)4ULL): goto ma20; case ((uint8_t)5ULL): goto ma20; case ((uint8_t)6ULL): goto ma20; case ((uint8_t)7ULL): goto ma20; case ((uint8_t)8ULL): goto ma20; case ((uint8_t)9ULL): goto ma20; case ((uint8_t)10ULL): goto ma20; case ((uint8_t)11ULL): goto ma15; case ((uint8_t)12ULL): goto ma18; case ((uint8_t)13ULL): goto ma17; case ((uint8_t)14ULL): goto ma19; case ((uint8_t)15ULL): goto ma20; case ((uint8_t)16ULL): goto ma20; case ((uint8_t)17ULL): goto ma14; case ((uint8_t)18ULL): goto ma20; case ((uint8_t)19ULL): goto ma20; case ((uint8_t)20ULL): goto ma16; case ((uint8_t)21ULL): goto ma20; case ((uint8_t)22ULL): goto ma20; case ((uint8_t)23ULL): goto ma20; case ((uint8_t)24ULL): goto ma20; case ((uint8_t)25ULL): goto ma20; case ((uint8_t)26ULL): goto ma20; default: goto ma20; }
ma14:;
        uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)&t12) + 0));
        if (t24 != ((uint8_t)17ULL)) goto mn23;
        uint32_t t25 = GM_LD(uint32_t, ((((uint8_t *)&t12) + 8) + 0));
        gm_l2 = t25;
        {
            void * t28 = gm_l0;
            gm_b16_8 t29 = GM_LD(gm_b16_8, ((((uint8_t *)t28) + 1728) + 0));
            gm_b16_8 t30 = GM_LD(gm_b16_8, ((uint8_t *)&t29));
            uint32_t t31 = gm_l2;
            gm_tf5srh4mp4b3i t32 = gm_f_index_of_41b084m(t30, t31);
            uint8_t t33;
            if (((((uint8_t *)&t32))[8] != 0)) goto mn35;
            t33 = ((uint8_t)0);
            goto me34;
mn35:;
            t33 = ((uint8_t)1);
            goto me34;
mn36:;
            gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 797, 16);
me34:;
            if (!t33) goto ie27;
            {
                uint32_t t38 = gm_l2;
                gm_t9805qtonbsag t39;
                GM_ST(uint8_t, (uint8_t *)&t39 + 5, 0);
                GM_ST(uint16_t, (uint8_t *)&t39 + 6, 0);
                GM_ST(uint32_t, ((uint8_t *)&t39), t38);
                ((uint8_t *)&t39)[4] = 1;
                uint8_t t40 = 0;
                F.ret = t39;
                goto gm_fret;
            }
            goto ix27;
ie27:;
ix27:;
        }
        {
            uint32_t t41 = gm_l2;
            uint64_t t42 = (uint64_t)t41;
            uint64_t t43 = (uintptr_t)t42;
            void * t44 = gm_l0;
            uint64_t t45 = GM_LD(uintptr_t, ((((uint8_t *)t44) + 1088) + 0) + 8);
            if (t43 >= (uintptr_t)(t45)) gm_trap_index(t43, t45, gm_src3k278vcmqgdr, 798, 21);
            gm_ti0kgh3pc3in2 t46 = GM_LD(gm_ti0kgh3pc3in2, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t44) + 1088) + 0)) + t43 * 184) + 24));
            F.l3 = t46;
        }
        {
            {
                gm_l5 = ((uint64_t)0ULL);
            }
            {
                uint64_t t48 = gm_f_member_count_otu9tc0(&(*(gm_ti0kgh3pc3in2 *)((uint8_t *)&F.l3)));
                gm_l4 = t48;
            }
            {
                for (;;) {
                    uint64_t t50 = gm_l5;
                    uint64_t t51 = gm_l4;
                    uint8_t t52 = (t50 < t51);
                    if (!t52) goto lb49;
                    {
                        uint64_t t55 = gm_l5;
                        uint64_t t58 = GM_LD(uint64_t, ((uint8_t *)&t55));
                        gm_l6 = t58;
                        {
                            uint64_t t60 = gm_l6;
                            gm_tkn42u7na3t81 t61 = gm_f_member_ty_esvuguv(&(*(gm_ti0kgh3pc3in2 *)((uint8_t *)&F.l3)), t60);
                            gm_t88rcucg1b7uk t62;
                            if (((((uint8_t *)&t61))[4] != 0)) {
                                gm_t88rcucg1b7uk t63 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t61) + 0));
                                t62 = t63;
                            } else {
                                goto lc49;
                            }
                            gm_l7 = t62;
                        }
                        {
                            void * t65 = gm_l0;
                            gm_t88rcucg1b7uk t66 = gm_l7;
                            gm_t9805qtonbsag t67 = gm_f_pending_layout_ikknuv1(t65, GM_CONV(gm_csguankko4lmhd, t66));
                            if (!((((uint8_t *)&t67))[4] != 0)) goto ie64;
                            uint32_t t68 = GM_LD(uint32_t, ((uint8_t *)&t67));
                            gm_l8 = t68;
                            {
                                uint32_t t70 = gm_l8;
                                gm_t9805qtonbsag t71;
                                GM_ST(uint8_t, (uint8_t *)&t71 + 5, 0);
                                GM_ST(uint16_t, (uint8_t *)&t71 + 6, 0);
                                GM_ST(uint32_t, ((uint8_t *)&t71), t70);
                                ((uint8_t *)&t71)[4] = 1;
                                uint8_t t72 = 0;
                                F.ret = t71;
                                goto gm_fret;
                            }
                            goto ix64;
ie64:;
ix64:;
                        }
                        goto me56;
mn57:;
                        gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 799, 13);
me56:;
                    }
lc49:;
                    {
                        uint64_t t74 = gm_l5;
                        uint64_t r75; bool o76 = __builtin_add_overflow(t74, ((uint64_t)1ULL), &r75);
                        uint64_t t77 = r75;
                        gm_l5 = t77;
                    }
                }
lb49:;
            }
        }
        {
            gm_t9805qtonbsag t78;
            memset(&t78, 0, sizeof t78);
            uint8_t t79 = 0;
            F.ret = t78;
            goto gm_fret;
        }
        goto me13;
mn23:;
ma15:;
        uint8_t t81 = GM_LD(uint8_t, (((uint8_t *)&t12) + 0));
        if (t81 != ((uint8_t)11ULL)) goto mn80;
        gm_l9 = (*(gm_t8nckeqfenl8a *)(((uint8_t *)&t12) + 8));
        void * t82 = gm_l0;
        gm_t88rcucg1b7uk t83 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l9) + 8));
        gm_t9805qtonbsag t84 = gm_f_pending_layout_ikknuv1(t82, GM_CONV(gm_csguankko4lmhd, t83));
        uint8_t t85 = 0;
        F.ret = t84;
        goto gm_fret;
        goto me13;
mn80:;
ma16:;
        uint8_t t87 = GM_LD(uint8_t, (((uint8_t *)&t12) + 0));
        if (t87 != ((uint8_t)20ULL)) goto mn86;
        gm_tgglga8ro3isc t88 = GM_LD(gm_tgglga8ro3isc, (((uint8_t *)&t12) + 8));
        gm_l10 = t88;
        void * t89 = gm_l0;
        gm_t88rcucg1b7uk t90 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l10) + 4));
        gm_t9805qtonbsag t91 = gm_f_pending_layout_ikknuv1(t89, GM_CONV(gm_csguankko4lmhd, t90));
        uint8_t t92 = 0;
        F.ret = t91;
        goto gm_fret;
        goto me13;
mn86:;
ma17:;
        uint8_t t94 = GM_LD(uint8_t, (((uint8_t *)&t12) + 0));
        if (t94 != ((uint8_t)13ULL)) goto mn93;
        gm_t88rcucg1b7uk t95 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t12) + 8));
        gm_l11 = t95;
        void * t96 = gm_l0;
        gm_t88rcucg1b7uk t97 = gm_l11;
        gm_t9805qtonbsag t98 = gm_f_pending_layout_ikknuv1(t96, GM_CONV(gm_csguankko4lmhd, t97));
        uint8_t t99 = 0;
        F.ret = t98;
        goto gm_fret;
        goto me13;
mn93:;
ma18:;
        uint8_t t101 = GM_LD(uint8_t, (((uint8_t *)&t12) + 0));
        if (t101 != ((uint8_t)12ULL)) goto mn100;
        gm_l12 = (*(gm_t6j6smj7qoctu *)(((uint8_t *)&t12) + 8));
        {
            {
                gm_b16_8 t104 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l12) + 0));
                gm_l13 = t104;
            }
            {
                gm_l14 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t106 = gm_l14;
                    uint64_t t107 = GM_LD(uintptr_t, ((uint8_t *)&gm_l13) + 8);
                    uint8_t t108 = (t106 < t107);
                    if (!t108) goto lb105;
                    {
                        uint64_t t111 = gm_l14;
                        uint64_t t112 = (uintptr_t)t111;
                        uint64_t t113 = GM_LD(uintptr_t, ((uint8_t *)&gm_l13) + 8);
                        gm_t88rcucg1b7uk t116 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l13)) + t112 * 4));
                        gm_l15 = t116;
                        {
                            void * t119 = gm_l0;
                            gm_t88rcucg1b7uk t120 = gm_l15;
                            gm_t9805qtonbsag t121 = gm_f_pending_layout_ikknuv1(t119, GM_CONV(gm_csguankko4lmhd, t120));
                            if (!((((uint8_t *)&t121))[4] != 0)) goto ie118;
                            uint32_t t122 = GM_LD(uint32_t, ((uint8_t *)&t121));
                            gm_l16 = t122;
                            {
                                uint32_t t124 = gm_l16;
                                gm_t9805qtonbsag t125;
                                GM_ST(uint8_t, (uint8_t *)&t125 + 5, 0);
                                GM_ST(uint16_t, (uint8_t *)&t125 + 6, 0);
                                GM_ST(uint32_t, ((uint8_t *)&t125), t124);
                                ((uint8_t *)&t125)[4] = 1;
                                uint8_t t126 = 0;
                                F.ret = t125;
                                goto gm_fret;
                            }
                            goto ix118;
ie118:;
ix118:;
                        }
                        goto me114;
mn115:;
                        gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 809, 13);
me114:;
                    }
lc105:;
                    {
                        uint64_t t128 = gm_l14;
                        uint64_t r129; bool o130 = __builtin_add_overflow(t128, ((uint64_t)1ULL), &r129);
                        uint64_t t131 = r129;
                        gm_l14 = t131;
                    }
                }
lb105:;
            }
        }
        {
            gm_t9805qtonbsag t132;
            memset(&t132, 0, sizeof t132);
            uint8_t t133 = 0;
            F.ret = t132;
            goto gm_fret;
        }
        goto me13;
mn100:;
ma19:;
        uint8_t t135 = GM_LD(uint8_t, (((uint8_t *)&t12) + 0));
        if (t135 != ((uint8_t)14ULL)) goto mn134;
        gm_tgglga8ro3isc t136 = GM_LD(gm_tgglga8ro3isc, (((uint8_t *)&t12) + 8));
        gm_l17 = t136;
        {
            void * t139 = gm_l0;
            gm_t88rcucg1b7uk t140 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l17) + 0));
            gm_t9805qtonbsag t141 = gm_f_pending_layout_ikknuv1(t139, GM_CONV(gm_csguankko4lmhd, t140));
            if (!((((uint8_t *)&t141))[4] != 0)) goto ie138;
            uint32_t t142 = GM_LD(uint32_t, ((uint8_t *)&t141));
            gm_l18 = t142;
            {
                uint32_t t144 = gm_l18;
                gm_t9805qtonbsag t145;
                GM_ST(uint8_t, (uint8_t *)&t145 + 5, 0);
                GM_ST(uint16_t, (uint8_t *)&t145 + 6, 0);
                GM_ST(uint32_t, ((uint8_t *)&t145), t144);
                ((uint8_t *)&t145)[4] = 1;
                uint8_t t146 = 0;
                F.ret = t145;
                goto gm_fret;
            }
            goto ix138;
ie138:;
ix138:;
        }
        {
            void * t147 = gm_l0;
            gm_t88rcucg1b7uk t148 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l17) + 4));
            gm_t9805qtonbsag t149 = gm_f_pending_layout_ikknuv1(t147, GM_CONV(gm_csguankko4lmhd, t148));
            uint8_t t150 = 0;
            F.ret = t149;
            goto gm_fret;
        }
        goto me13;
mn134:;
ma20:;
        gm_t9805qtonbsag t152;
        memset(&t152, 0, sizeof t152);
        uint8_t t153 = 0;
        F.ret = t152;
        goto gm_fret;
        goto me13;
mn151:;
ma21:;
        gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 795, 5);
me13:;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) void gm_f_remove_in_progress_lrie478(void * a0, uint32_t a1);
inline __attribute__((aligned(4))) void gm_f_struct_fields_chqu3it(void *gm_out, void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_struct_fields_chqu3it F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    uint32_t gm_l2; /* id */
    GM_STACK_CHECK_SHALLOW(&F, 248u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    {
        void * t3 = gm_l0;
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_t88rcucg1b7uk t5 = gm_l1;
        gm_t53at80pfdus3 t6;
        gm_f_kind_jcnt4hm((void *)&t6, t4, GM_CONV(gm_csguankko4lmhd, t5));
        uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t7 != ((uint8_t)17ULL)) goto ie2;
        uint32_t t8 = GM_LD(uint32_t, ((((uint8_t *)&t6) + 8) + 0));
        gm_l2 = t8;
        {
            uint32_t t11 = gm_l2;
            uint64_t t12 = (uint64_t)t11;
            uint64_t t13 = (uintptr_t)t12;
            void * t14 = gm_l0;
            uint64_t t15 = GM_LD(uintptr_t, ((((uint8_t *)t14) + 1088) + 0) + 8);
            if (t13 >= (uintptr_t)(t15)) gm_trap_index(t13, t15, gm_src3k278vcmqgdr, 496, 21);
            uint8_t t16 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t14) + 1088) + 0)) + t13 * 184) + 24) + 0));
            if (t16 != ((uint8_t)0ULL)) goto ie10;
            gm_tpautr4br7h3l t17 = GM_LD(gm_tpautr4br7h3l, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t14) + 1088) + 0)) + t13 * 184) + 24) + 8));
            F.l3 = t17;
            {
                gm_b16_8 t19 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
                ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 0, t19);
                goto gm_fdone;
            }
            goto ix10;
ie10:;
            {
                gm_th2lqbo77pvsc t21;
                memset(&t21, 0, sizeof t21);
                uint8_t t22 = 0;
                F.ret = t21;
                goto gm_fret;
            }
ix10:;
        }
        goto ix2;
ie2:;
        {
            gm_th2lqbo77pvsc t24;
            memset(&t24, 0, sizeof t24);
            uint8_t t25 = 0;
            F.ret = t24;
            goto gm_fret;
        }
ix2:;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_th2lqbo77pvsc gm_f_struct_fields_chqu3it_c(void * a0, gm_csguankko4lmhd a1) { gm_th2lqbo77pvsc gm_r; gm_f_struct_fields_chqu3it(&gm_r, a0, a1); return gm_r; }
__attribute__((aligned(4))) void gm_f_struct_kind_5l1hs46(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, uint32_t a2, void * a3, gm_b16_8 a4) {
    struct gm_fr_struct_kind_5l1hs46 F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    uint32_t gm_l2; /* id */
    void * gm_l3; /* st */
    gm_b16_8 gm_l4; /* attrs */
    uint8_t gm_l5; /* packed */
    gm_tf5srh4mp4b3i gm_l6; /* align */
    gm_tkn42u7na3t81 gm_l7; /* bits */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    gm_b16_8 gm_l11; /* args */
    gm_b16_8 gm_l12; /* other */
    uint8_t gm_l16; /* has_using */
    gm_b16_8 gm_l18; /* <tmp> */
    uint64_t gm_l19; /* <tmp> */
    uint64_t gm_l20; /* i */
    gm_t88rcucg1b7uk gm_l22; /* ty */
    gm_tf5srh4mp4b3i gm_l23; /* field_align */
    gm_b16_8 gm_l24; /* <tmp> */
    uint64_t gm_l25; /* <tmp> */
    gm_b16_8 gm_l27; /* other */
    void * gm_l28; /* d */
    gm_b16_8 gm_l29; /* <tmp> */
    uint64_t gm_l30; /* <tmp> */
    gm_t08olq97rscn3 gm_l31; /* n */
    gm_b16_8 gm_l32; /* <tmp> */
    uint64_t gm_l33; /* <tmp> */
    gm_t88rcucg1b7uk gm_l35; /* b */
    GM_STACK_CHECK(&F, 2638u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        gm_l5 = ((uint8_t)0);
    }
    {
        gm_tf5srh4mp4b3i t2;
        memset(&t2, 0, sizeof t2);
        gm_l6 = t2;
    }
    {
        gm_tkn42u7na3t81 t3;
        memset(&t3, 0, sizeof t3);
        gm_l7 = t3;
    }
    {
        {
            gm_b16_8 t5 = gm_l4;
            gm_l8 = t5;
        }
        {
            gm_l9 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t7 = gm_l9;
                uint64_t t8 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                uint8_t t9 = (t7 < t8);
                if (!t9) goto lb6;
                {
                    uint64_t t12 = gm_l9;
                    uint64_t t13 = (uintptr_t)t12;
                    uint64_t t14 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                    gm_tkacitepg5fk0 t17 = GM_LD(gm_tkacitepg5fk0, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l8)) + t13 * 72));
                    F.l10 = t17;
                    {
                        if (!((GM_LD(uintptr_t, ((((uint8_t *)&F.l10) + 0) + 0) + 8) == 6) && memcmp(GM_LD(void *, ((((uint8_t *)&F.l10) + 0) + 0)), gm_s2ph9f020upd2, 6) == 0)) goto mn20;
                        {
                            gm_l5 = ((uint8_t)1);
                        }
                        goto me19;
mn20:;
                        if (!((GM_LD(uintptr_t, ((((uint8_t *)&F.l10) + 0) + 0) + 8) == 5) && memcmp(GM_LD(void *, ((((uint8_t *)&F.l10) + 0) + 0)), gm_sj1k4t2de5144, 5) == 0)) goto mn22;
                        {
                            void * t24 = gm_l0;
                            uint64_t t25 = gm_l1;
                            void * t26 = (void *)((uint8_t *)&F.l10);
                            gm_tf5srh4mp4b3i t27 = gm_f_attr_power_of_two_gj02n2t((void *)NULL, cx, NULL, t24, t25, t26);
                            if (gm_unw.kind) {
                                goto gm_ll10;
                            }
                            gm_l6 = t27;
                        }
                        goto me19;
mn22:;
                        if (!((GM_LD(uintptr_t, ((((uint8_t *)&F.l10) + 0) + 0) + 8) == 4) && memcmp(GM_LD(void *, ((((uint8_t *)&F.l10) + 0) + 0)), gm_s6vvtrmtnt3j1, 4) == 0)) goto mn28;
                        {
                            gm_th2lqbo77pvsc t30 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)&F.l10) + 32));
                            gm_b16_8 t31;
                            if (((((uint8_t *)&t30))[16] != 0)) {
                                gm_b16_8 t32 = GM_LD(gm_b16_8, (((uint8_t *)&t30) + 0));
                                t31 = t32;
                            } else {
                                gm_b16_8 t33 = gm_f_empty_slice_g3tddrb();
                                t31 = t33;
                            }
                            gm_l11 = t31;
                        }
                        {
                            uint64_t t35 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                            uint8_t t36 = ((t35 == ((uint64_t)1ULL)));
                            if (!t36) goto ie34;
                            {
                                void * t38 = gm_l0;
                                uint64_t t39 = gm_l1;
                                uint64_t t40 = (uintptr_t)((uint64_t)0ULL);
                                uint64_t t41 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                                if (t40 >= (uintptr_t)(t41)) gm_trap_index(t40, t41, gm_src3k278vcmqgdr, 199, 52);
                                void * t42 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l11)) + t40 * 88);
                                gm_t88rcucg1b7uk t43 = gm_f_resolve_type_jjodbei((void *)NULL, cx, NULL, t38, t39, t42);
                                if (gm_unw.kind) {
                                    goto gm_ll10;
                                }
                                gm_tkn42u7na3t81 t44;
                                GM_ST(uint8_t, (uint8_t *)&t44 + 5, 0);
                                GM_ST(uint16_t, (uint8_t *)&t44 + 6, 0);
                                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t44), t43);
                                ((uint8_t *)&t44)[4] = 1;
                                gm_l7 = t44;
                            }
                            goto ix34;
ie34:;
                            {
                                void * t46 = gm_l0;
                                gm_tl874t2atnlh3 t47 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l10) + 56));
                                gm_b16_8 t48;
                                GM_ST(void *, (uint8_t *)&t48, (void *)(gm_s3lik4tvvjana));
                                GM_ST(uintptr_t, (uint8_t *)&t48 + 8, (uintptr_t)(64));
                                gm_b0_1 t49 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t46, t47, t48);
                                if (gm_unw.kind) {
                                    goto gm_ll10;
                                }
                            }
ix34:;
                        }
                        goto me19;
mn28:;
                        gm_b16_8 t51 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l10) + 0) + 0));
                        gm_l12 = t51;
                        void * t52 = gm_l0;
                        gm_tl874t2atnlh3 t53 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l10) + 56));
                        gm_b16_8 t54 = gm_l12;
                        gm_tnpifv20irnph t55;
                        GM_ST(gm_b16_8, ((uint8_t *)&t55 + 0), t54);
                        gm_tabb710d9mq2e t56;
                        gm_f_format_5iu9hua((void *)&t56, (void *)NULL, cx, NULL, t55);
                        if (gm_unw.kind) {
                            goto gm_ll10;
                        }
                        if (((((uint8_t *)&t56))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t56) + 8, gm_src3k278vcmqgdr, 204, 38);
                        gm_b16_8 t57 = GM_LD(gm_b16_8, (((uint8_t *)&t56) + 8));
                        gm_b16_8 t58 = GM_LD(gm_b16_8, ((uint8_t *)&t57));
                        gm_b0_1 t59 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t52, t53, t58);
                        if (gm_unw.kind) {
                            goto gm_ll10;
                        }
                        goto me19;
mn50:;
                        gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 193, 9);
me19:;
                    }
                    goto me15;
mn16:;
                    gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 192, 5);
me15:;
                }
lc6:;
                {
                    uint64_t t61 = gm_l9;
                    uint64_t r62; bool o63 = __builtin_add_overflow(t61, ((uint64_t)1ULL), &r62);
                    uint64_t t64 = r62;
                    gm_l9 = t64;
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
        gm_f_list_spqn36o((void *)&F.l13, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_list_s83jn2q((void *)&F.l14, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_list_pmp06kq_2((void *)&F.l15, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_l16 = ((uint8_t)0);
    }
    {
        gm_f_list_gst7tuh((void *)&F.l17, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t65 = gm_l0;
        uint64_t t66 = gm_l1;
        void * t67 = gm_l3;
        gm_b16_8 t68 = GM_LD(gm_b16_8, (((uint8_t *)t67) + 40));
        gm_b16_8 t69 = GM_LD(gm_b16_8, ((uint8_t *)&t68));
        void * t70 = (void *)((uint8_t *)&F.l17);
        gm_b0_1 t71 = gm_f_taken_struct_members_1ltbb18((void *)NULL, cx, NULL, t65, t66, t69, t70);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t73 = GM_LD(gm_b16_8, (((uint8_t *)&F.l17) + 0));
            gm_b16_8 t74 = GM_LD(gm_b16_8, ((uint8_t *)&t73));
            gm_l18 = t74;
        }
        {
            gm_l19 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t76 = gm_l19;
                uint64_t t77 = GM_LD(uintptr_t, ((uint8_t *)&gm_l18) + 8);
                uint8_t t78 = (t76 < t77);
                if (!t78) goto lb75;
                {
                    gm_t8uai8b3boj78 t81;
                    uint64_t t82 = gm_l19;
                    GM_ST(uint64_t, (uint8_t *)&t81 + 0, t82);
                    uint64_t t83 = gm_l19;
                    uint64_t t84 = (uintptr_t)t83;
                    uint64_t t85 = GM_LD(uintptr_t, ((uint8_t *)&gm_l18) + 8);
                    memcpy((uint8_t *)&t81 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l18)) + t84 * 72), 72);
                    uint64_t t88 = GM_LD(uint64_t, (((uint8_t *)&t81) + 0));
                    gm_l20 = t88;
                    F.l21 = (*(gm_t24301fas1b2a *)(((uint8_t *)&t81) + 8));
                    {
                        void * t90 = gm_l0;
                        uint64_t t91 = gm_l1;
                        void * t92 = GM_LD(void *, (((uint8_t *)&F.l21) + 40));
                        gm_t88rcucg1b7uk t93 = gm_f_resolve_type_jjodbei((void *)NULL, cx, NULL, t90, t91, t92);
                        if (gm_unw.kind) {
                            goto gm_ll79;
                        }
                        gm_l22 = t93;
                    }
                    {
                        void * t95 = gm_l0;
                        void * t96 = (void *)(((uint8_t *)t95) + 8);
                        gm_t88rcucg1b7uk t97 = gm_l22;
                        gm_t53at80pfdus3 t98;
                        gm_f_kind_jcnt4hm((void *)&t98, t96, GM_CONV(gm_csguankko4lmhd, t97));
                        uint8_t t99;
                        uint8_t t102 = GM_LD(uint8_t, (((uint8_t *)&t98) + 0));
                        if (t102 != ((uint8_t)15ULL)) goto mn101;
                        t99 = ((uint8_t)1);
                        goto me100;
mn101:;
                        t99 = ((uint8_t)0);
                        goto me100;
mn103:;
                        gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 215, 12);
me100:;
                        if (!t99) goto ie94;
                        {
                            void * t105 = gm_l0;
                            void * t106 = GM_LD(void *, (((uint8_t *)&F.l21) + 40));
                            gm_tl874t2atnlh3 t107 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t106) + 72));
                            gm_b16_8 t108;
                            GM_ST(void *, (uint8_t *)&t108, (void *)(gm_sacmu7hr5dknu));
                            GM_ST(uintptr_t, (uint8_t *)&t108 + 8, (uintptr_t)(87));
                            gm_b0_1 t109 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t105, t107, t108);
                            if (gm_unw.kind) {
                                goto gm_ll79;
                            }
                        }
                        goto ix94;
ie94:;
ix94:;
                    }
                    {
                        gm_tf5srh4mp4b3i t110;
                        memset(&t110, 0, sizeof t110);
                        gm_l23 = t110;
                    }
                    {
                        {
                            gm_b16_8 t112 = GM_LD(gm_b16_8, (((uint8_t *)&F.l21) + 0));
                            gm_l24 = t112;
                        }
                        {
                            gm_l25 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t114 = gm_l25;
                                uint64_t t115 = GM_LD(uintptr_t, ((uint8_t *)&gm_l24) + 8);
                                uint8_t t116 = (t114 < t115);
                                if (!t116) goto lb113;
                                {
                                    uint64_t t119 = gm_l25;
                                    uint64_t t120 = (uintptr_t)t119;
                                    uint64_t t121 = GM_LD(uintptr_t, ((uint8_t *)&gm_l24) + 8);
                                    gm_tkacitepg5fk0 t124 = GM_LD(gm_tkacitepg5fk0, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l24)) + t120 * 72));
                                    F.l26 = t124;
                                    {
                                        if (!((GM_LD(uintptr_t, ((((uint8_t *)&F.l26) + 0) + 0) + 8) == 5) && memcmp(GM_LD(void *, ((((uint8_t *)&F.l26) + 0) + 0)), gm_sj1k4t2de5144, 5) == 0)) goto mn127;
                                        {
                                            void * t129 = gm_l0;
                                            uint64_t t130 = gm_l1;
                                            void * t131 = (void *)((uint8_t *)&F.l26);
                                            gm_tf5srh4mp4b3i t132 = gm_f_attr_power_of_two_gj02n2t((void *)NULL, cx, NULL, t129, t130, t131);
                                            if (gm_unw.kind) {
                                                goto gm_ll117;
                                            }
                                            gm_l23 = t132;
                                        }
                                        goto me126;
mn127:;
                                        gm_b16_8 t134 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l26) + 0) + 0));
                                        gm_l27 = t134;
                                        void * t135 = gm_l0;
                                        gm_tl874t2atnlh3 t136 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l26) + 56));
                                        gm_b16_8 t137 = gm_l27;
                                        gm_tnpifv20irnph t138;
                                        GM_ST(gm_b16_8, ((uint8_t *)&t138 + 0), t137);
                                        gm_tabb710d9mq2e t139;
                                        gm_f_format_slub3h9((void *)&t139, (void *)NULL, cx, NULL, t138);
                                        if (gm_unw.kind) {
                                            goto gm_ll117;
                                        }
                                        if (((((uint8_t *)&t139))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t139) + 8, gm_src3k278vcmqgdr, 222, 42);
                                        gm_b16_8 t140 = GM_LD(gm_b16_8, (((uint8_t *)&t139) + 8));
                                        gm_b16_8 t141 = GM_LD(gm_b16_8, ((uint8_t *)&t140));
                                        gm_b0_1 t142 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t135, t136, t141);
                                        if (gm_unw.kind) {
                                            goto gm_ll117;
                                        }
                                        goto me126;
mn133:;
                                        gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 220, 13);
me126:;
                                    }
                                    goto me122;
mn123:;
                                    gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 219, 9);
me122:;
                                }
lc113:;
                                {
                                    uint64_t t144 = gm_l25;
                                    uint64_t r145; bool o146 = __builtin_add_overflow(t144, ((uint64_t)1ULL), &r145);
                                    uint64_t t147 = r145;
                                    gm_l25 = t147;
                                }
                            }
gm_ll117:;
                            if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb113; }
                                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc113; }
                            }
                            goto gm_ll79;
lb113:;
                        }
                    }
                    {
                        uint8_t t149 = GM_LD(uint8_t, (((uint8_t *)&F.l21) + 17));
                        if (!t149) goto ie148;
                        {
                            uint8_t t152 = 0;
                            uint64_t t153 = gm_l20;
                            uint8_t t154 = (!(t153 == ((uint64_t)0ULL)));
                            if (!t154) {
                                uint64_t t155 = GM_LD(uintptr_t, (((uint8_t *)&F.l21) + 24) + 8);
                                uint8_t t156 = (!(t155 == ((uint64_t)1ULL)));
                                t152 = t156;
                            } else t152 = 1;
                            if (!t152) goto ie151;
                            {
                                void * t158 = gm_l0;
                                gm_tl874t2atnlh3 t159 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l21) + 56));
                                gm_b16_8 t160;
                                GM_ST(void *, (uint8_t *)&t160, (void *)(gm_sisjr6ep7j5j0));
                                GM_ST(uintptr_t, (uint8_t *)&t160 + 8, (uintptr_t)(78));
                                gm_b0_1 t161 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t158, t159, t160);
                                if (gm_unw.kind) {
                                    goto gm_ll79;
                                }
                            }
                            goto ix151;
ie151:;
                            void * t163 = gm_l0;
                            void * t164 = (void *)(((uint8_t *)t163) + 8);
                            gm_t88rcucg1b7uk t165 = gm_l22;
                            gm_t53at80pfdus3 t166;
                            gm_f_kind_jcnt4hm((void *)&t166, t164, GM_CONV(gm_csguankko4lmhd, t165));
                            uint8_t t167;
                            uint8_t t170 = GM_LD(uint8_t, (((uint8_t *)&t166) + 0));
                            if (t170 != ((uint8_t)17ULL)) goto mn169;
                            t167 = ((uint8_t)0);
                            goto me168;
mn169:;
                            t167 = ((uint8_t)1);
                            goto me168;
mn171:;
                            gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 228, 23);
me168:;
                            if (!t167) goto ie162;
                            void * t172 = gm_l0;
                            void * t173 = (void *)(((uint8_t *)t172) + 8);
                            gm_t88rcucg1b7uk t174 = gm_l22;
                            uint8_t t175 = gm_f_is_error_8k27gd1(t173, GM_CONV(gm_csguankko4lmhd, t174));
                            uint8_t t176 = (uint8_t)!t175;
                            if (!t176) goto ie162;
                            {
                                void * t178 = gm_l0;
                                gm_tl874t2atnlh3 t179 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l21) + 56));
                                void * t180 = gm_l0;
                                gm_t88rcucg1b7uk t181 = gm_l22;
                                gm_b16_8 t182 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t180, t181);
                                if (gm_unw.kind) {
                                    goto gm_ll79;
                                }
                                gm_tnpifv20irnph t183;
                                GM_ST(gm_b16_8, ((uint8_t *)&t183 + 0), t182);
                                gm_tabb710d9mq2e t184;
                                gm_f_format_fiq5e0p((void *)&t184, (void *)NULL, cx, NULL, t183);
                                if (gm_unw.kind) {
                                    goto gm_ll79;
                                }
                                if (((((uint8_t *)&t184))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t184) + 8, gm_src3k278vcmqgdr, 229, 33);
                                gm_b16_8 t185 = GM_LD(gm_b16_8, (((uint8_t *)&t184) + 8));
                                gm_b16_8 t186 = GM_LD(gm_b16_8, ((uint8_t *)&t185));
                                gm_b0_1 t187 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t178, t179, t186);
                                if (gm_unw.kind) {
                                    goto gm_ll79;
                                }
                            }
                            goto ix162;
ie162:;
                            {
                                gm_l16 = ((uint8_t)1);
                            }
ix162:;
ix151:;
                        }
                        goto ix148;
ie148:;
ix148:;
                    }
                    {
                        void * t190 = GM_LD(void *, (((uint8_t *)&F.l21) + 48));
                        if (!(GM_LD(void *, ((uint8_t *)&t190)) != 0)) goto ie189;
                        void * t191 = GM_LD(void *, ((uint8_t *)&t190));
                        gm_l28 = t191;
                        {
                            void * t193 = gm_l0;
                            uint64_t t194 = gm_l1;
                            void * t195 = gm_l28;
                            gm_t88rcucg1b7uk t196 = gm_l22;
                            gm_b0_1 t197 = gm_f_check_default_4bf68q8((void *)NULL, cx, NULL, t193, t194, t195, t196);
                            if (gm_unw.kind) {
                                goto gm_ll79;
                            }
                        }
                        goto ix189;
ie189:;
ix189:;
                    }
                    {
                        {
                            gm_b16_8 t199 = GM_LD(gm_b16_8, (((uint8_t *)&F.l21) + 24));
                            gm_l29 = t199;
                        }
                        {
                            gm_l30 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t201 = gm_l30;
                                uint64_t t202 = GM_LD(uintptr_t, ((uint8_t *)&gm_l29) + 8);
                                uint8_t t203 = (t201 < t202);
                                if (!t203) goto lb200;
                                {
                                    uint64_t t206 = gm_l30;
                                    uint64_t t207 = (uintptr_t)t206;
                                    uint64_t t208 = GM_LD(uintptr_t, ((uint8_t *)&gm_l29) + 8);
                                    gm_t08olq97rscn3 t211 = GM_LD(gm_t08olq97rscn3, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l29)) + t207 * 32));
                                    gm_l31 = t211;
                                    {
                                        uint8_t t214 = (!((GM_LD(uintptr_t, (((uint8_t *)&gm_l31) + 0) + 8) == 1) && memcmp(GM_LD(void *, (((uint8_t *)&gm_l31) + 0)), gm_sfdbfrv46lihd, 1) == 0));
                                        if (!t214) goto ie213;
                                        {
                                            {
                                                gm_b16_8 t217 = GM_LD(gm_b16_8, (((uint8_t *)&F.l13) + 0));
                                                gm_l32 = t217;
                                            }
                                            {
                                                gm_l33 = ((uint64_t)0ULL);
                                            }
                                            {
                                                for (;;) {
                                                    uint64_t t219 = gm_l33;
                                                    uint64_t t220 = GM_LD(uintptr_t, ((uint8_t *)&gm_l32) + 8);
                                                    uint8_t t221 = (t219 < t220);
                                                    if (!t221) goto lb218;
                                                    {
                                                        uint64_t t224 = gm_l33;
                                                        uint64_t t225 = (uintptr_t)t224;
                                                        uint64_t t226 = GM_LD(uintptr_t, ((uint8_t *)&gm_l32) + 8);
                                                        gm_t0mglksrkfa5e t229 = GM_LD(gm_t0mglksrkfa5e, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l32)) + t225 * 40));
                                                        F.l34 = t229;
                                                        {
                                                            gm_b16_8 t232 = GM_LD(gm_b16_8, (((uint8_t *)&F.l34) + 0));
                                                            gm_b16_8 t233 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l31) + 0));
                                                            uint8_t t234 = (gm_eq0iqiubfm7e(((uint8_t *)&t232), ((uint8_t *)&t233)));
                                                            if (!t234) goto ie231;
                                                            {
                                                                void * t236 = gm_l0;
                                                                gm_tl874t2atnlh3 t237 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l31) + 16));
                                                                gm_b16_8 t238 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l31) + 0));
                                                                gm_tnpifv20irnph t239;
                                                                GM_ST(gm_b16_8, ((uint8_t *)&t239 + 0), t238);
                                                                gm_tabb710d9mq2e t240;
                                                                gm_f_format_atintr3((void *)&t240, (void *)NULL, cx, NULL, t239);
                                                                if (gm_unw.kind) {
                                                                    goto gm_ll222;
                                                                }
                                                                if (((((uint8_t *)&t240))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t240) + 8, gm_src3k278vcmqgdr, 241, 41);
                                                                gm_b16_8 t241 = GM_LD(gm_b16_8, (((uint8_t *)&t240) + 8));
                                                                gm_b16_8 t242 = GM_LD(gm_b16_8, ((uint8_t *)&t241));
                                                                gm_b0_1 t243 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t236, t237, t242);
                                                                if (gm_unw.kind) {
                                                                    goto gm_ll222;
                                                                }
                                                            }
                                                            {
                                                                goto lb218;
                                                            }
                                                            goto ix231;
ie231:;
ix231:;
                                                        }
                                                        goto me227;
mn228:;
                                                        gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 239, 17);
me227:;
                                                    }
lc218:;
                                                    {
                                                        uint64_t t245 = gm_l33;
                                                        uint64_t r246; bool o247 = __builtin_add_overflow(t245, ((uint64_t)1ULL), &r246);
                                                        uint64_t t248 = r246;
                                                        gm_l33 = t248;
                                                    }
                                                }
gm_ll222:;
                                                if (gm_unw.frame == &F.h && gm_unw.label == 4) {
                                                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb218; }
                                                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc218; }
                                                }
                                                goto gm_ll204;
lb218:;
                                            }
                                        }
                                        goto ix213;
ie213:;
ix213:;
                                    }
                                    {
                                        void * t249 = (void *)((uint8_t *)&F.l13);
                                        gm_t0mglksrkfa5e t250;
                                        memset((uint8_t *)&t250 + 21, 0, 3);
                                        gm_b16_8 t251 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l31) + 0));
                                        GM_ST(gm_b16_8, (uint8_t *)&t250 + 0, t251);
                                        gm_t88rcucg1b7uk t252 = gm_l22;
                                        GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t250 + 16, t252);
                                        gm_t9qkh0rsooc8d t253 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&F.l21) + 16));
                                        GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t250 + 20, t253);
                                        gm_tf5srh4mp4b3i t254 = gm_l23;
                                        GM_ST(gm_tf5srh4mp4b3i, (uint8_t *)&t250 + 24, t254);
                                        gm_tftb1odc739t5 t255 = gm_f_push_vrmo7c5(t249, &t250);
                                        if (((((uint8_t *)&t255))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t255) + 1, gm_src3k278vcmqgdr, 246, 13);
                                        gm_b0_1 t256 = GM_LD(gm_b0_1, (((uint8_t *)&t255) + 1));
                                    }
                                    {
                                        void * t257 = (void *)((uint8_t *)&F.l14);
                                        void * t258 = GM_LD(void *, (((uint8_t *)&F.l21) + 48));
                                        uint8_t t259;
                                        if ((GM_LD(void *, ((uint8_t *)&t258)) != 0)) goto mn261;
                                        t259 = ((uint8_t)0);
                                        goto me260;
mn261:;
                                        t259 = ((uint8_t)1);
                                        goto me260;
mn262:;
                                        gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 247, 27);
me260:;
                                        gm_tftb1odc739t5 t263 = gm_f_push_vd1290p(t257, t259);
                                        if (((((uint8_t *)&t263))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t263) + 1, gm_src3k278vcmqgdr, 247, 13);
                                        gm_b0_1 t264 = GM_LD(gm_b0_1, (((uint8_t *)&t263) + 1));
                                    }
                                    {
                                        void * t265 = (void *)((uint8_t *)&F.l15);
                                        void * t266 = GM_LD(void *, (((uint8_t *)&F.l21) + 48));
                                        gm_tftb1odc739t5 t267 = gm_f_push_r2k6rjf_2(t265, t266);
                                        if (((((uint8_t *)&t267))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t267) + 1, gm_src3k278vcmqgdr, 248, 13);
                                        gm_b0_1 t268 = GM_LD(gm_b0_1, (((uint8_t *)&t267) + 1));
                                    }
                                    goto me209;
mn210:;
                                    gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 237, 9);
me209:;
                                }
lc200:;
                                {
                                    uint64_t t270 = gm_l30;
                                    uint64_t r271; bool o272 = __builtin_add_overflow(t270, ((uint64_t)1ULL), &r271);
                                    uint64_t t273 = r271;
                                    gm_l30 = t273;
                                }
                            }
gm_ll204:;
                            if (gm_unw.frame == &F.h && gm_unw.label == 3) {
                                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb200; }
                                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc200; }
                            }
                            goto gm_ll79;
lb200:;
                        }
                    }
                    goto me86;
mn87:;
                    gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 213, 5);
me86:;
                }
lc75:;
                {
                    uint64_t t275 = gm_l19;
                    uint64_t r276; bool o277 = __builtin_add_overflow(t275, ((uint64_t)1ULL), &r276);
                    uint64_t t278 = r276;
                    gm_l19 = t278;
                }
            }
gm_ll79:;
            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb75; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc75; }
            }
            goto gm_lad;
lb75:;
        }
    }
    {
        if (!((((uint8_t *)&gm_l7))[4] != 0)) goto ie279;
        gm_t88rcucg1b7uk t280 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l7));
        gm_l35 = t280;
        {
            void * t282 = gm_l0;
            void * t283 = gm_l3;
            gm_tl874t2atnlh3 t284 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t283) + 56));
            gm_t88rcucg1b7uk t285 = gm_l35;
            gm_b16_8 t286 = GM_LD(gm_b16_8, (((uint8_t *)&F.l13) + 0));
            gm_b16_8 t287 = GM_LD(gm_b16_8, ((uint8_t *)&t286));
            gm_b0_1 t288 = gm_f_check_bits_struct_ophej5b((void *)NULL, cx, NULL, t282, t284, t285, t287);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix279;
ie279:;
ix279:;
    }
    {
        uint32_t t289 = gm_l2;
        uint64_t t290 = (uint64_t)t289;
        uint64_t t291 = (uintptr_t)t290;
        void * t292 = gm_l0;
        uint64_t t293 = GM_LD(uintptr_t, ((((uint8_t *)t292) + 1088) + 0) + 8);
        if (t291 >= (uintptr_t)(t293)) gm_trap_index(t291, t293, gm_src3k278vcmqgdr, 252, 5);
        gm_b16_8 t294 = GM_LD(gm_b16_8, (((uint8_t *)&F.l14) + 0));
        GM_ST(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t292) + 1088) + 0)) + t291 * 184) + 80), t294);
    }
    {
        uint32_t t295 = gm_l2;
        uint64_t t296 = (uint64_t)t295;
        uint64_t t297 = (uintptr_t)t296;
        void * t298 = gm_l0;
        uint64_t t299 = GM_LD(uintptr_t, ((((uint8_t *)t298) + 1088) + 0) + 8);
        if (t297 >= (uintptr_t)(t299)) gm_trap_index(t297, t299, gm_src3k278vcmqgdr, 253, 5);
        gm_b16_8 t300 = GM_LD(gm_b16_8, (((uint8_t *)&F.l15) + 0));
        GM_ST(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t298) + 1088) + 0)) + t297 * 184) + 96), t300);
    }
    {
        uint32_t t301 = gm_l2;
        uint64_t t302 = (uint64_t)t301;
        uint64_t t303 = (uintptr_t)t302;
        void * t304 = gm_l0;
        uint64_t t305 = GM_LD(uintptr_t, ((((uint8_t *)t304) + 1088) + 0) + 8);
        if (t303 >= (uintptr_t)(t305)) gm_trap_index(t303, t305, gm_src3k278vcmqgdr, 254, 5);
        uint8_t t306 = gm_l16;
        GM_ST(uint8_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t304) + 1088) + 0)) + t303 * 184) + 112), t306);
    }
    {
        gm_tpautr4br7h3l t307;
        memset((uint8_t *)&t307 + 17, 0, 7);
        gm_b16_8 t308 = GM_LD(gm_b16_8, (((uint8_t *)&F.l13) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t307 + 0, t308);
        uint8_t t309 = gm_l5;
        GM_ST(uint8_t, (uint8_t *)&t307 + 16, t309);
        gm_tf5srh4mp4b3i t310 = gm_l6;
        GM_ST(gm_tf5srh4mp4b3i, (uint8_t *)&t307 + 24, t310);
        gm_tkn42u7na3t81 t311 = gm_l7;
        GM_ST(gm_tkn42u7na3t81, (uint8_t *)&t307 + 40, t311);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_ti0kgh3pc3in2 *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_ti0kgh3pc3in2 *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_ti0kgh3pc3in2 *)gm_out) + 4, 0);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_ti0kgh3pc3in2 *)gm_out) + 0, ((uint8_t)0ULL));
        GM_ST(gm_tpautr4br7h3l, (uint8_t *)&(*(gm_ti0kgh3pc3in2 *)gm_out) + 8, t307);
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
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_subst_a2s1mb3(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2) {
    struct gm_fr_subst_a2s1mb3 F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* map */
    void * gm_l3; /* tys */
    uint32_t gm_l5; /* p */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    gm_tplpbqf3duvn3 gm_l8; /* m */
    uint32_t gm_l9; /* id */
    gm_b16_8 gm_l10; /* args */
    gm_th3eueiipivq6 gm_l11; /* shape */
    gm_t88rcucg1b7uk gm_l12; /* elem */
    gm_t88rcucg1b7uk gm_l13; /* x */
    gm_b16_8 gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    gm_t88rcucg1b7uk gm_l18; /* x */
    uint8_t gm_l21; /* c */
    gm_t88rcucg1b7uk gm_l22; /* x */
    uint8_t gm_l23; /* c */
    gm_t88rcucg1b7uk gm_l24; /* x */
    uint8_t gm_l25; /* c */
    gm_t88rcucg1b7uk gm_l26; /* x */
    uint64_t gm_l27; /* len */
    gm_t88rcucg1b7uk gm_l28; /* elem */
    gm_tnnjeerolos0a gm_l29; /* kind */
    gm_tnnjeerolos0a gm_l30; /* ak */
    gm_t88rcucg1b7uk gm_l31; /* e */
    gm_t88rcucg1b7uk gm_l32; /* len */
    gm_t88rcucg1b7uk gm_l33; /* elem */
    gm_t88rcucg1b7uk gm_l34; /* l */
    gm_t88rcucg1b7uk gm_l35; /* el */
    gm_t428ue2u3hf5g gm_l36; /* key */
    uint64_t gm_l37; /* n */
    gm_t9qkh0rsooc8d gm_l38; /* op */
    gm_t88rcucg1b7uk gm_l39; /* lhs */
    gm_t88rcucg1b7uk gm_l40; /* rhs */
    gm_t88rcucg1b7uk gm_l41; /* a */
    gm_t88rcucg1b7uk gm_l42; /* b */
    gm_t88rcucg1b7uk gm_l43; /* x */
    gm_t88rcucg1b7uk gm_l44; /* x */
    gm_t88rcucg1b7uk gm_l45; /* err */
    gm_t88rcucg1b7uk gm_l46; /* ok */
    gm_t88rcucg1b7uk gm_l47; /* e */
    gm_t88rcucg1b7uk gm_l48; /* o */
    gm_b16_8 gm_l49; /* elems */
    gm_b16_8 gm_l50; /* names */
    gm_b16_8 gm_l52; /* <tmp> */
    uint64_t gm_l53; /* <tmp> */
    gm_t88rcucg1b7uk gm_l54; /* e */
    gm_tnpifv20irnph gm_l55; /* set */
    GM_STACK_CHECK(&F, 2979u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        uint8_t t3 = 0;
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        uint8_t t5 = ((t4 == ((uint64_t)0ULL)));
        if (!t5) {
            void * t6 = gm_l0;
            void * t7 = (void *)(((uint8_t *)t6) + 8);
            gm_t88rcucg1b7uk t8 = gm_l1;
            uint8_t t9 = gm_f_has_param_u9t0efs(t7, GM_CONV(gm_csguankko4lmhd, t8));
            uint8_t t10 = (uint8_t)!t9;
            t3 = t10;
        } else t3 = 1;
        if (!t3) goto ie2;
        {
            gm_t88rcucg1b7uk t12 = gm_l1;
            uint8_t t13 = 0;
            F.ret = t12;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t14 = gm_l0;
        void * t15 = (void *)(((uint8_t *)t14) + 8);
        gm_l3 = t15;
    }
    {
        void * t16 = gm_l3;
        gm_t88rcucg1b7uk t17 = gm_l1;
        gm_f_kind_jcnt4hm((void *)&F.l4, t16, GM_CONV(gm_csguankko4lmhd, t17));
    }
    {
        uint8_t t36 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
        switch (t36) { case ((uint8_t)0ULL): goto ma34; case ((uint8_t)1ULL): goto ma34; case ((uint8_t)2ULL): goto ma34; case ((uint8_t)3ULL): goto ma34; case ((uint8_t)4ULL): goto ma34; case ((uint8_t)5ULL): goto ma34; case ((uint8_t)6ULL): goto ma34; case ((uint8_t)7ULL): goto ma34; case ((uint8_t)8ULL): goto ma23; case ((uint8_t)9ULL): goto ma24; case ((uint8_t)10ULL): goto ma25; case ((uint8_t)11ULL): goto ma26; case ((uint8_t)12ULL): goto ma32; case ((uint8_t)13ULL): goto ma29; case ((uint8_t)14ULL): goto ma31; case ((uint8_t)15ULL): goto ma21; case ((uint8_t)16ULL): goto ma22; case ((uint8_t)17ULL): goto ma20; case ((uint8_t)18ULL): goto ma30; case ((uint8_t)19ULL): goto ma19; case ((uint8_t)20ULL): goto ma27; case ((uint8_t)21ULL): goto ma34; case ((uint8_t)22ULL): goto ma28; case ((uint8_t)23ULL): goto ma34; case ((uint8_t)24ULL): goto ma33; case ((uint8_t)25ULL): goto ma34; case ((uint8_t)26ULL): goto ma34; default: goto ma34; }
ma19:;
        uint8_t t38 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
        if (t38 != ((uint8_t)19ULL)) goto mn37;
        uint32_t t39 = GM_LD(uint32_t, (((uint8_t *)&F.l4) + 8));
        gm_l5 = t39;
        {
            {
                gm_b16_8 t42 = gm_l2;
                gm_l6 = t42;
            }
            {
                gm_l7 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t44 = gm_l7;
                    uint64_t t45 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    uint8_t t46 = (t44 < t45);
                    if (!t46) goto lb43;
                    {
                        uint64_t t49 = gm_l7;
                        uint64_t t50 = (uintptr_t)t49;
                        uint64_t t51 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                        gm_tplpbqf3duvn3 t54 = GM_LD(gm_tplpbqf3duvn3, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t50 * 8));
                        gm_l8 = t54;
                        {
                            uint32_t t57 = GM_LD(uint32_t, (((uint8_t *)&gm_l8) + 0));
                            uint32_t t58 = gm_l5;
                            uint8_t t59 = ((t57 == t58));
                            if (!t59) goto ie56;
                            {
                                gm_t88rcucg1b7uk t61 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l8) + 4));
                                uint8_t t62 = 0;
                                F.ret = t61;
                                goto gm_fret;
                            }
                            goto ix56;
ie56:;
ix56:;
                        }
                        goto me52;
mn53:;
                        gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 683, 13);
me52:;
                    }
lc43:;
                    {
                        uint64_t t64 = gm_l7;
                        uint64_t r65; bool o66 = __builtin_add_overflow(t64, ((uint64_t)1ULL), &r65);
                        uint64_t t67 = r65;
                        gm_l7 = t67;
                    }
                }
lb43:;
            }
        }
        {
            gm_t88rcucg1b7uk t68 = gm_l1;
            uint8_t t69 = 0;
            F.ret = t68;
            goto gm_fret;
        }
        goto me18;
mn37:;
ma20:;
        uint8_t t71 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
        if (t71 != ((uint8_t)17ULL)) goto mn70;
        uint32_t t72 = GM_LD(uint32_t, ((((uint8_t *)&F.l4) + 8) + 0));
        gm_l9 = t72;
        gm_b16_8 t73 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l4) + 8) + 8));
        gm_l10 = t73;
        {
            uint64_t t76 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
            uint8_t t77 = ((t76 == ((uint64_t)0ULL)));
            if (!t77) goto ie75;
            {
                gm_t88rcucg1b7uk t79 = gm_l1;
                uint8_t t80 = 0;
                F.ret = t79;
                goto gm_fret;
            }
            goto ix75;
ie75:;
ix75:;
        }
        {
            void * t82 = gm_l0;
            void * t83 = gm_l3;
            uint32_t t84 = gm_l9;
            uint32_t t85 = gm_f_template_9o92cvv(t83, t84);
            gm_tbm0n9ujhrlhf t86;
            gm_f_soa_shape_of_ohenvjc((void *)&t86, t82, t85);
            if (!((((uint8_t *)&t86))[24] != 0)) goto ie81;
            gm_l11 = (*(gm_th3eueiipivq6 *)(((uint8_t *)&t86) + 0));
            {
                void * t88 = gm_l3;
                gm_t88rcucg1b7uk t89 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t88);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l12 = t89;
            }
            {
                uint64_t t91 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t92 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                if (t91 >= (uintptr_t)(t92)) gm_trap_index(t91, t92, gm_src3k278vcmqgdr, 693, 32);
                uint8_t t93 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l10)) + t91 * 40) + 0));
                if (t93 != ((uint8_t)0ULL)) goto ie90;
                gm_t88rcucg1b7uk t94 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l10)) + t91 * 40) + 8));
                gm_l13 = t94;
                {
                    void * t96 = gm_l0;
                    gm_t88rcucg1b7uk t97 = gm_l13;
                    gm_b16_8 t98 = gm_l2;
                    gm_t88rcucg1b7uk t99 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t96, t97, t98);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l12 = t99;
                }
                goto ix90;
ie90:;
ix90:;
            }
            {
                void * t100 = gm_l0;
                gm_t88rcucg1b7uk t101 = gm_l12;
                gm_t88rcucg1b7uk t102 = gm_f_subst_soa_2chqte3((void *)NULL, cx, NULL, t100, t101, &(*(gm_th3eueiipivq6 *)((uint8_t *)&gm_l11)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t103 = 0;
                F.ret = t102;
                goto gm_fret;
            }
            goto ix81;
ie81:;
ix81:;
        }
        {
            gm_f_list_nof7vfj((void *)&F.l14, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_b16_8 t105 = gm_l10;
                gm_l15 = t105;
            }
            {
                gm_l16 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t107 = gm_l16;
                    uint64_t t108 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                    uint8_t t109 = (t107 < t108);
                    if (!t109) goto lb106;
                    {
                        uint64_t t112 = gm_l16;
                        uint64_t t113 = (uintptr_t)t112;
                        uint64_t t114 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                        gm_toa2vhe3l4hfr t117 = GM_LD(gm_toa2vhe3l4hfr, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l15)) + t113 * 40));
                        F.l17 = t117;
                        {
                            void * t119 = (void *)((uint8_t *)&F.l14);
                            gm_t88rcucg1b7uk t120;
                            uint8_t t123 = GM_LD(uint8_t, (((uint8_t *)&F.l17) + 0));
                            if (t123 != ((uint8_t)0ULL)) goto mn122;
                            gm_t88rcucg1b7uk t124 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l17) + 8));
                            gm_l18 = t124;
                            void * t125 = gm_l0;
                            gm_t88rcucg1b7uk t126 = gm_l18;
                            gm_b16_8 t127 = gm_l2;
                            gm_t88rcucg1b7uk t128 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t125, t126, t127);
                            if (gm_unw.kind) {
                                goto gm_ll110;
                            }
                            t120 = t128;
                            goto me121;
mn122:;
                            uint8_t t130 = GM_LD(uint8_t, (((uint8_t *)&F.l17) + 0));
                            if (t130 != ((uint8_t)1ULL)) goto mn129;
                            void * t131 = gm_l0;
                            void * t132 = (void *)(((uint8_t *)t131) + 8);
                            gm_t88rcucg1b7uk t133 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t132);
                            if (gm_unw.kind) {
                                goto gm_ll110;
                            }
                            t120 = t133;
                            goto me121;
mn129:;
                            gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 698, 31);
me121:;
                            gm_tftb1odc739t5 t134 = gm_f_push_k3l9nic(t119, GM_CONV(gm_csguankko4lmhd, t120));
                            if (((((uint8_t *)&t134))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t134) + 1, gm_src3k278vcmqgdr, 698, 17);
                            gm_b0_1 t135 = GM_LD(gm_b0_1, (((uint8_t *)&t134) + 1));
                        }
                        goto me115;
mn116:;
                        gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 697, 13);
me115:;
                    }
lc106:;
                    {
                        uint64_t t137 = gm_l16;
                        uint64_t r138; bool o139 = __builtin_add_overflow(t137, ((uint64_t)1ULL), &r138);
                        uint64_t t140 = r138;
                        gm_l16 = t140;
                    }
                }
gm_ll110:;
                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb106; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc106; }
                }
                goto gm_lad;
lb106:;
            }
        }
        {
            void * t141 = gm_l0;
            void * t142 = gm_l0;
            void * t143 = (void *)(((uint8_t *)t142) + 8);
            uint32_t t144 = gm_l9;
            uint32_t t145 = gm_f_template_9o92cvv(t143, t144);
            gm_b16_8 t146 = GM_LD(gm_b16_8, (((uint8_t *)&F.l14) + 0));
            gm_b16_8 t147 = GM_LD(gm_b16_8, ((uint8_t *)&t146));
            gm_t88rcucg1b7uk t148 = gm_f_instantiate_adt_lbjcui1((void *)NULL, cx, NULL, t141, t145, t147);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t149 = 0;
            F.ret = t148;
            goto gm_fret;
        }
        goto me18;
mn70:;
ma21:;
        uint8_t t151 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
        if (t151 != ((uint8_t)15ULL)) goto mn150;
        gm_tddtjfggdat0d t152 = GM_LD(gm_tddtjfggdat0d, (((uint8_t *)&F.l4) + 8));
        F.l19 = t152;
        void * t153 = gm_l0;
        void * t154 = (void *)(((uint8_t *)t153) + 8);
        void * t155 = gm_l0;
        void * t156 = (void *)((uint8_t *)&F.l19);
        gm_b16_8 t157 = gm_l2;
        gm_tddtjfggdat0d t158;
        gm_f_subst_sig_9ngh936((void *)&t158, (void *)NULL, cx, NULL, t155, t156, t157);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t53at80pfdus3 t159;
        GM_ST(uint8_t, (uint8_t *)&t159 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t159 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t159 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t159 + 0), ((uint8_t)15ULL));
        GM_ST(gm_tddtjfggdat0d, ((uint8_t *)&t159 + 8), t158);
        gm_t88rcucg1b7uk t160 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t154, &t159);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t161 = 0;
        F.ret = t160;
        goto gm_fret;
        goto me18;
mn150:;
ma22:;
        uint8_t t163 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
        if (t163 != ((uint8_t)16ULL)) goto mn162;
        gm_tddtjfggdat0d t164 = GM_LD(gm_tddtjfggdat0d, (((uint8_t *)&F.l4) + 8));
        F.l20 = t164;
        void * t165 = gm_l0;
        void * t166 = (void *)(((uint8_t *)t165) + 8);
        void * t167 = gm_l0;
        void * t168 = (void *)((uint8_t *)&F.l20);
        gm_b16_8 t169 = gm_l2;
        gm_tddtjfggdat0d t170;
        gm_f_subst_sig_9ngh936((void *)&t170, (void *)NULL, cx, NULL, t167, t168, t169);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t53at80pfdus3 t171;
        GM_ST(uint8_t, (uint8_t *)&t171 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t171 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t171 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t171 + 0), ((uint8_t)16ULL));
        GM_ST(gm_tddtjfggdat0d, ((uint8_t *)&t171 + 8), t170);
        gm_t88rcucg1b7uk t172 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t166, &t171);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t173 = 0;
        F.ret = t172;
        goto gm_fret;
        goto me18;
mn162:;
ma23:;
        uint8_t t175 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
        if (t175 != ((uint8_t)8ULL)) goto mn174;
        uint8_t t176 = GM_LD(uint8_t, ((((uint8_t *)&F.l4) + 8) + 0));
        gm_l21 = t176;
        gm_t88rcucg1b7uk t177 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l4) + 8) + 4));
        gm_l22 = t177;
        void * t178 = gm_l0;
        void * t179 = (void *)(((uint8_t *)t178) + 8);
        uint8_t t180 = gm_l21;
        void * t181 = gm_l0;
        gm_t88rcucg1b7uk t182 = gm_l22;
        gm_b16_8 t183 = gm_l2;
        gm_t88rcucg1b7uk t184 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t181, t182, t183);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t88rcucg1b7uk t185 = gm_f_ptr_t1h2m2d((void *)NULL, cx, NULL, t179, t180, t184);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t186 = 0;
        F.ret = t185;
        goto gm_fret;
        goto me18;
mn174:;
ma24:;
        uint8_t t188 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
        if (t188 != ((uint8_t)9ULL)) goto mn187;
        uint8_t t189 = GM_LD(uint8_t, ((((uint8_t *)&F.l4) + 8) + 0));
        gm_l23 = t189;
        gm_t88rcucg1b7uk t190 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l4) + 8) + 4));
        gm_l24 = t190;
        void * t191 = gm_l0;
        void * t192 = (void *)(((uint8_t *)t191) + 8);
        uint8_t t193 = gm_l23;
        void * t194 = gm_l0;
        gm_t88rcucg1b7uk t195 = gm_l24;
        gm_b16_8 t196 = gm_l2;
        gm_t88rcucg1b7uk t197 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t194, t195, t196);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t88rcucg1b7uk t198 = gm_f_many_ptr_l4v11f8((void *)NULL, cx, NULL, t192, t193, t197);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t199 = 0;
        F.ret = t198;
        goto gm_fret;
        goto me18;
mn187:;
ma25:;
        uint8_t t201 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
        if (t201 != ((uint8_t)10ULL)) goto mn200;
        uint8_t t202 = GM_LD(uint8_t, ((((uint8_t *)&F.l4) + 8) + 0));
        gm_l25 = t202;
        gm_t88rcucg1b7uk t203 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l4) + 8) + 4));
        gm_l26 = t203;
        void * t204 = gm_l0;
        void * t205 = (void *)(((uint8_t *)t204) + 8);
        uint8_t t206 = gm_l25;
        void * t207 = gm_l0;
        gm_t88rcucg1b7uk t208 = gm_l26;
        gm_b16_8 t209 = gm_l2;
        gm_t88rcucg1b7uk t210 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t207, t208, t209);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t88rcucg1b7uk t211 = gm_f_slice_5dt9121((void *)NULL, cx, NULL, t205, t206, t210);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t212 = 0;
        F.ret = t211;
        goto gm_fret;
        goto me18;
mn200:;
ma26:;
        uint8_t t214 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
        if (t214 != ((uint8_t)11ULL)) goto mn213;
        uint64_t t215 = GM_LD(uint64_t, ((((uint8_t *)&F.l4) + 8) + 0));
        gm_l27 = t215;
        gm_t88rcucg1b7uk t216 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l4) + 8) + 8));
        gm_l28 = t216;
        gm_tnnjeerolos0a t217 = GM_LD(gm_tnnjeerolos0a, ((((uint8_t *)&F.l4) + 8) + 12));
        gm_l29 = t217;
        {
            gm_tnnjeerolos0a t219 = gm_l29;
            gm_l30 = t219;
        }
        {
            uint8_t t221 = GM_LD(uint8_t, (((uint8_t *)&gm_l29) + 0));
            if (t221 != ((uint8_t)1ULL)) goto ie220;
            gm_t88rcucg1b7uk t222 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l29) + 4));
            gm_l31 = t222;
            {
                void * t224 = gm_l0;
                gm_t88rcucg1b7uk t225 = gm_l31;
                gm_b16_8 t226 = gm_l2;
                gm_t88rcucg1b7uk t227 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t224, t225, t226);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tnnjeerolos0a t228;
                GM_ST(uint8_t, (uint8_t *)&t228 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t228 + 2, 0);
                GM_ST(uint8_t, ((uint8_t *)&t228 + 0), ((uint8_t)1ULL));
                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t228 + 4), t227);
                gm_l30 = t228;
            }
            goto ix220;
ie220:;
ix220:;
        }
        {
            void * t229 = gm_l0;
            void * t230 = (void *)(((uint8_t *)t229) + 8);
            gm_t8nckeqfenl8a t231;
            memset((uint8_t *)&t231 + 20, 0, 4);
            uint64_t t232 = gm_l27;
            GM_ST(uint64_t, (uint8_t *)&t231 + 0, t232);
            void * t233 = gm_l0;
            gm_t88rcucg1b7uk t234 = gm_l28;
            gm_b16_8 t235 = gm_l2;
            gm_t88rcucg1b7uk t236 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t233, t234, t235);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t231 + 8, t236);
            gm_tnnjeerolos0a t237 = gm_l30;
            GM_ST(gm_tnnjeerolos0a, (uint8_t *)&t231 + 12, t237);
            gm_t53at80pfdus3 t238;
            GM_ST(uint8_t, (uint8_t *)&t238 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t238 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t238 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t238 + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&t238 + 40, 0);
            GM_ST(uint64_t, (uint8_t *)&t238 + 48, 0);
            GM_ST(uint8_t, ((uint8_t *)&t238 + 0), ((uint8_t)11ULL));
            GM_ST(gm_t8nckeqfenl8a, ((uint8_t *)&t238 + 8), t231);
            gm_t88rcucg1b7uk t239 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t230, &t238);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t240 = 0;
            F.ret = t239;
            goto gm_fret;
        }
        goto me18;
mn213:;
ma27:;
        uint8_t t242 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
        if (t242 != ((uint8_t)20ULL)) goto mn241;
        gm_t88rcucg1b7uk t243 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l4) + 8) + 0));
        gm_l32 = t243;
        gm_t88rcucg1b7uk t244 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l4) + 8) + 4));
        gm_l33 = t244;
        {
            void * t246 = gm_l0;
            gm_t88rcucg1b7uk t247 = gm_l32;
            gm_b16_8 t248 = gm_l2;
            gm_t88rcucg1b7uk t249 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t246, t247, t248);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l34 = t249;
        }
        {
            void * t250 = gm_l0;
            gm_t88rcucg1b7uk t251 = gm_l33;
            gm_b16_8 t252 = gm_l2;
            gm_t88rcucg1b7uk t253 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t250, t251, t252);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l35 = t253;
        }
        {
            void * t254 = gm_l0;
            void * t255 = (void *)(((uint8_t *)t254) + 8);
            gm_t88rcucg1b7uk t256 = gm_l34;
            gm_t53at80pfdus3 t257;
            gm_f_kind_jcnt4hm((void *)&t257, t255, GM_CONV(gm_csguankko4lmhd, t256));
            uint8_t t263 = GM_LD(uint8_t, (((uint8_t *)&t257) + 0));
            switch (t263) { case ((uint8_t)0ULL): goto ma261; case ((uint8_t)1ULL): goto ma261; case ((uint8_t)2ULL): goto ma261; case ((uint8_t)3ULL): goto ma261; case ((uint8_t)4ULL): goto ma261; case ((uint8_t)5ULL): goto ma261; case ((uint8_t)6ULL): goto ma261; case ((uint8_t)7ULL): goto ma261; case ((uint8_t)8ULL): goto ma261; case ((uint8_t)9ULL): goto ma261; case ((uint8_t)10ULL): goto ma261; case ((uint8_t)11ULL): goto ma261; case ((uint8_t)12ULL): goto ma261; case ((uint8_t)13ULL): goto ma261; case ((uint8_t)14ULL): goto ma261; case ((uint8_t)15ULL): goto ma261; case ((uint8_t)16ULL): goto ma261; case ((uint8_t)17ULL): goto ma261; case ((uint8_t)18ULL): goto ma261; case ((uint8_t)19ULL): goto ma261; case ((uint8_t)20ULL): goto ma261; case ((uint8_t)21ULL): goto ma259; case ((uint8_t)22ULL): goto ma261; case ((uint8_t)23ULL): goto ma261; case ((uint8_t)24ULL): goto ma261; case ((uint8_t)25ULL): goto ma261; case ((uint8_t)26ULL): goto ma260; default: goto ma261; }
ma259:;
            uint8_t t265 = GM_LD(uint8_t, (((uint8_t *)&t257) + 0));
            if (t265 != ((uint8_t)21ULL)) goto mn264;
            gm_l36 = (*(gm_t428ue2u3hf5g *)(((uint8_t *)&t257) + 8));
            {
                gm_tf5srh4mp4b3i t267 = gm_f_key_u64_ljklb56(&(*(gm_t428ue2u3hf5g *)((uint8_t *)&gm_l36)));
                uint64_t t268;
                if (((((uint8_t *)&t267))[8] != 0)) {
                    uint64_t t269 = GM_LD(uint64_t, (((uint8_t *)&t267) + 0));
                    t268 = t269;
                } else {
                    void * t270 = gm_l0;
                    void * t271 = (void *)(((uint8_t *)t270) + 8);
                    gm_t88rcucg1b7uk t272 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t271);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t273 = 0;
                    F.ret = t272;
                    goto gm_fret;
                }
                gm_l37 = t268;
            }
            {
                void * t274 = gm_l0;
                void * t275 = (void *)(((uint8_t *)t274) + 8);
                uint64_t t276 = gm_l37;
                gm_t88rcucg1b7uk t277 = gm_l35;
                gm_t88rcucg1b7uk t278 = gm_f_array_th8sdi3((void *)NULL, cx, NULL, t275, t276, t277);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t279 = 0;
                F.ret = t278;
                goto gm_fret;
            }
            goto me258;
mn264:;
ma260:;
            uint8_t t281 = GM_LD(uint8_t, (((uint8_t *)&t257) + 0));
            if (t281 != ((uint8_t)26ULL)) goto mn280;
            void * t282 = gm_l0;
            void * t283 = (void *)(((uint8_t *)t282) + 8);
            gm_t88rcucg1b7uk t284 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t283);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t285 = 0;
            F.ret = t284;
            goto gm_fret;
            goto me258;
mn280:;
ma261:;
            void * t287 = gm_l0;
            void * t288 = (void *)(((uint8_t *)t287) + 8);
            gm_t88rcucg1b7uk t289 = gm_l34;
            gm_t88rcucg1b7uk t290 = gm_l35;
            gm_tgglga8ro3isc t291;
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t291 + 0), t289);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t291 + 4), t290);
            gm_t53at80pfdus3 t292;
            memset(&t292, 0, sizeof t292);
            GM_ST(uint8_t, ((uint8_t *)&t292 + 0), ((uint8_t)20ULL));
            GM_ST(gm_tgglga8ro3isc, ((uint8_t *)&t292 + 8), t291);
            gm_t88rcucg1b7uk t293 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t288, &t292);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t294 = 0;
            F.ret = t293;
            goto gm_fret;
            goto me258;
mn286:;
ma262:;
            gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 718, 13);
me258:;
        }
        goto me18;
mn241:;
ma28:;
        uint8_t t296 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
        if (t296 != ((uint8_t)22ULL)) goto mn295;
        gm_t9qkh0rsooc8d t297 = GM_LD(gm_t9qkh0rsooc8d, ((((uint8_t *)&F.l4) + 8) + 0));
        gm_l38 = t297;
        gm_t88rcucg1b7uk t298 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l4) + 8) + 4));
        gm_l39 = t298;
        gm_t88rcucg1b7uk t299 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l4) + 8) + 8));
        gm_l40 = t299;
        {
            void * t301 = gm_l0;
            gm_t88rcucg1b7uk t302 = gm_l39;
            gm_b16_8 t303 = gm_l2;
            gm_t88rcucg1b7uk t304 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t301, t302, t303);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l41 = t304;
        }
        {
            void * t305 = gm_l0;
            gm_t88rcucg1b7uk t306 = gm_l40;
            gm_b16_8 t307 = gm_l2;
            gm_t88rcucg1b7uk t308 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t305, t306, t307);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l42 = t308;
        }
        {
            void * t309 = gm_l0;
            void * t310 = (void *)(((uint8_t *)t309) + 8);
            gm_t9qkh0rsooc8d t311 = gm_l38;
            gm_t88rcucg1b7uk t312 = gm_l41;
            gm_t88rcucg1b7uk t313 = gm_l42;
            gm_t88rcucg1b7uk t314 = gm_f_len_expr_i134fvg((void *)NULL, cx, NULL, t310, t311, t312, t313);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t315 = 0;
            F.ret = t314;
            goto gm_fret;
        }
        goto me18;
mn295:;
ma29:;
        uint8_t t317 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
        if (t317 != ((uint8_t)13ULL)) goto mn316;
        gm_t88rcucg1b7uk t318 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l4) + 8));
        gm_l43 = t318;
        void * t319 = gm_l0;
        void * t320 = (void *)(((uint8_t *)t319) + 8);
        void * t321 = gm_l0;
        gm_t88rcucg1b7uk t322 = gm_l43;
        gm_b16_8 t323 = gm_l2;
        gm_t88rcucg1b7uk t324 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t321, t322, t323);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t88rcucg1b7uk t325 = gm_f_optional_rh6okj3((void *)NULL, cx, NULL, t320, t324);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t326 = 0;
        F.ret = t325;
        goto gm_fret;
        goto me18;
mn316:;
ma30:;
        uint8_t t328 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
        if (t328 != ((uint8_t)18ULL)) goto mn327;
        gm_t88rcucg1b7uk t329 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l4) + 8));
        gm_l44 = t329;
        void * t330 = gm_l0;
        void * t331 = (void *)(((uint8_t *)t330) + 8);
        void * t332 = gm_l0;
        gm_t88rcucg1b7uk t333 = gm_l44;
        gm_b16_8 t334 = gm_l2;
        gm_t88rcucg1b7uk t335 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t332, t333, t334);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t53at80pfdus3 t336;
        memset(&t336, 0, sizeof t336);
        GM_ST(uint8_t, ((uint8_t *)&t336 + 0), ((uint8_t)18ULL));
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t336 + 8), t335);
        gm_t88rcucg1b7uk t337 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t331, &t336);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t338 = 0;
        F.ret = t337;
        goto gm_fret;
        goto me18;
mn327:;
ma31:;
        uint8_t t340 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
        if (t340 != ((uint8_t)14ULL)) goto mn339;
        gm_t88rcucg1b7uk t341 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l4) + 8) + 0));
        gm_l45 = t341;
        gm_t88rcucg1b7uk t342 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l4) + 8) + 4));
        gm_l46 = t342;
        {
            void * t344 = gm_l0;
            gm_t88rcucg1b7uk t345 = gm_l45;
            gm_b16_8 t346 = gm_l2;
            gm_t88rcucg1b7uk t347 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t344, t345, t346);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l47 = t347;
        }
        {
            void * t348 = gm_l0;
            gm_t88rcucg1b7uk t349 = gm_l46;
            gm_b16_8 t350 = gm_l2;
            gm_t88rcucg1b7uk t351 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t348, t349, t350);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l48 = t351;
        }
        {
            void * t352 = gm_l0;
            void * t353 = (void *)(((uint8_t *)t352) + 8);
            gm_t88rcucg1b7uk t354 = gm_l47;
            gm_t88rcucg1b7uk t355 = gm_l48;
            gm_t88rcucg1b7uk t356 = gm_f_result_o9ras10((void *)NULL, cx, NULL, t353, t354, t355);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t357 = 0;
            F.ret = t356;
            goto gm_fret;
        }
        goto me18;
mn339:;
ma32:;
        uint8_t t359 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
        if (t359 != ((uint8_t)12ULL)) goto mn358;
        gm_b16_8 t360 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l4) + 8) + 0));
        gm_l49 = t360;
        gm_b16_8 t361 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l4) + 8) + 16));
        gm_l50 = t361;
        {
            gm_f_list_nof7vfj((void *)&F.l51, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_b16_8 t364 = gm_l49;
                gm_l52 = t364;
            }
            {
                gm_l53 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t366 = gm_l53;
                    uint64_t t367 = GM_LD(uintptr_t, ((uint8_t *)&gm_l52) + 8);
                    uint8_t t368 = (t366 < t367);
                    if (!t368) goto lb365;
                    {
                        uint64_t t371 = gm_l53;
                        uint64_t t372 = (uintptr_t)t371;
                        uint64_t t373 = GM_LD(uintptr_t, ((uint8_t *)&gm_l52) + 8);
                        gm_t88rcucg1b7uk t376 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l52)) + t372 * 4));
                        gm_l54 = t376;
                        {
                            void * t378 = (void *)((uint8_t *)&F.l51);
                            void * t379 = gm_l0;
                            gm_t88rcucg1b7uk t380 = gm_l54;
                            gm_b16_8 t381 = gm_l2;
                            gm_t88rcucg1b7uk t382 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t379, t380, t381);
                            if (gm_unw.kind) {
                                goto gm_ll369;
                            }
                            gm_tftb1odc739t5 t383 = gm_f_push_k3l9nic(t378, GM_CONV(gm_csguankko4lmhd, t382));
                            if (((((uint8_t *)&t383))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t383) + 1, gm_src3k278vcmqgdr, 741, 30);
                            gm_b0_1 t384 = GM_LD(gm_b0_1, (((uint8_t *)&t383) + 1));
                        }
                        goto me374;
mn375:;
                        gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 741, 13);
me374:;
                    }
lc365:;
                    {
                        uint64_t t386 = gm_l53;
                        uint64_t r387; bool o388 = __builtin_add_overflow(t386, ((uint64_t)1ULL), &r387);
                        uint64_t t389 = r387;
                        gm_l53 = t389;
                    }
                }
gm_ll369:;
                if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb365; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc365; }
                }
                goto gm_lad;
lb365:;
            }
        }
        {
            void * t390 = gm_l0;
            void * t391 = (void *)(((uint8_t *)t390) + 8);
            gm_t6j6smj7qoctu t392;
            gm_b16_8 t393 = GM_LD(gm_b16_8, (((uint8_t *)&F.l51) + 0));
            gm_b16_8 t394 = GM_LD(gm_b16_8, ((uint8_t *)&t393));
            GM_ST(gm_b16_8, (uint8_t *)&t392 + 0, t394);
            gm_b16_8 t395 = gm_l50;
            GM_ST(gm_b16_8, (uint8_t *)&t392 + 16, t395);
            gm_t53at80pfdus3 t396;
            GM_ST(uint8_t, (uint8_t *)&t396 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t396 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t396 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t396 + 40, 0);
            GM_ST(uint64_t, (uint8_t *)&t396 + 48, 0);
            GM_ST(uint8_t, ((uint8_t *)&t396 + 0), ((uint8_t)12ULL));
            GM_ST(gm_t6j6smj7qoctu, ((uint8_t *)&t396 + 8), t392);
            gm_t88rcucg1b7uk t397 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t391, &t396);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t398 = 0;
            F.ret = t397;
            goto gm_fret;
        }
        goto me18;
mn358:;
ma33:;
        uint8_t t400 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
        if (t400 != ((uint8_t)24ULL)) goto mn399;
        gm_tnpifv20irnph t401 = GM_LD(gm_tnpifv20irnph, (((uint8_t *)&F.l4) + 8));
        gm_l55 = t401;
        void * t402 = gm_l0;
        void * t403 = (void *)(((uint8_t *)t402) + 8);
        void * t404 = gm_l0;
        void * t405 = (void *)((uint8_t *)&gm_l55);
        gm_b16_8 t406 = gm_l2;
        gm_tnpifv20irnph t407 = gm_f_subst_effects_jnlbu5m((void *)NULL, cx, NULL, t404, t405, t406);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t53at80pfdus3 t408;
        memset(&t408, 0, sizeof t408);
        GM_ST(uint8_t, ((uint8_t *)&t408 + 0), ((uint8_t)24ULL));
        GM_ST(gm_tnpifv20irnph, ((uint8_t *)&t408 + 8), t407);
        gm_t88rcucg1b7uk t409 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t403, &t408);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t410 = 0;
        F.ret = t409;
        goto gm_fret;
        goto me18;
mn399:;
ma34:;
        gm_t88rcucg1b7uk t412 = gm_l1;
        uint8_t t413 = 0;
        F.ret = t412;
        goto gm_fret;
        goto me18;
mn411:;
ma35:;
        gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 681, 5);
me18:;
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
__attribute__((aligned(4))) gm_tnpifv20irnph gm_f_subst_effects_jnlbu5m(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2) {
    struct gm_fr_subst_effects_jnlbu5m F;
    void * gm_l0; /* s */
    void * gm_l1; /* set */
    gm_b16_8 gm_l2; /* map */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_tiilmfd16v3fc gm_l6; /* e */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    gm_t88rcucg1b7uk gm_l11; /* t */
    GM_STACK_CHECK(&F, 708u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        gm_f_list_084fcd6((void *)&F.l3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            void * t3 = gm_l1;
            gm_b16_8 t4 = GM_LD(gm_b16_8, (((uint8_t *)t3) + 0));
            gm_l4 = t4;
        }
        {
            gm_l5 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t6 = gm_l5;
                uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                uint8_t t8 = (t6 < t7);
                if (!t8) goto lb5;
                {
                    uint64_t t11 = gm_l5;
                    uint64_t t12 = (uintptr_t)t11;
                    uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    gm_tiilmfd16v3fc t16 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t12 * 24));
                    gm_l6 = t16;
                    {
                        gm_f_list_vmvl81b((void *)&F.l7, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_ll9;
                        }
                    }
                    {
                        {
                            gm_b16_8 t19 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l6) + 8));
                            gm_l8 = t19;
                        }
                        {
                            gm_l9 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t21 = gm_l9;
                                uint64_t t22 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                                uint8_t t23 = (t21 < t22);
                                if (!t23) goto lb20;
                                {
                                    uint64_t t26 = gm_l9;
                                    uint64_t t27 = (uintptr_t)t26;
                                    uint64_t t28 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                                    gm_toa2vhe3l4hfr t31 = GM_LD(gm_toa2vhe3l4hfr, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l8)) + t27 * 40));
                                    F.l10 = t31;
                                    {
                                        void * t33 = (void *)((uint8_t *)&F.l7);
                                        gm_toa2vhe3l4hfr t34;
                                        uint8_t t37 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
                                        if (t37 != ((uint8_t)0ULL)) goto mn36;
                                        gm_t88rcucg1b7uk t38 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l10) + 8));
                                        gm_l11 = t38;
                                        void * t39 = gm_l0;
                                        gm_t88rcucg1b7uk t40 = gm_l11;
                                        gm_b16_8 t41 = gm_l2;
                                        gm_t88rcucg1b7uk t42 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t39, t40, t41);
                                        if (gm_unw.kind) {
                                            goto gm_ll24;
                                        }
                                        gm_toa2vhe3l4hfr t43;
                                        memset(&t43, 0, sizeof t43);
                                        GM_ST(uint8_t, ((uint8_t *)&t43 + 0), ((uint8_t)0ULL));
                                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t43 + 8), t42);
                                        t34 = t43;
                                        goto me35;
mn36:;
                                        gm_toa2vhe3l4hfr t45 = GM_LD(gm_toa2vhe3l4hfr, ((uint8_t *)&F.l10));
                                        F.l12 = t45;
                                        gm_toa2vhe3l4hfr t46 = F.l12;
                                        t34 = t46;
                                        goto me35;
mn44:;
                                        gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 764, 23);
me35:;
                                        gm_tftb1odc739t5 t47 = gm_f_push_s5vr2a2(t33, &t34);
                                        if (((((uint8_t *)&t47))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t47) + 1, gm_src3k278vcmqgdr, 764, 13);
                                        gm_b0_1 t48 = GM_LD(gm_b0_1, (((uint8_t *)&t47) + 1));
                                    }
                                    goto me29;
mn30:;
                                    gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 763, 9);
me29:;
                                }
lc20:;
                                {
                                    uint64_t t50 = gm_l9;
                                    uint64_t r51; bool o52 = __builtin_add_overflow(t50, ((uint64_t)1ULL), &r51);
                                    uint64_t t53 = r51;
                                    gm_l9 = t53;
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
                    {
                        void * t54 = (void *)((uint8_t *)&F.l3);
                        gm_tiilmfd16v3fc t55;
                        memset((uint8_t *)&t55 + 4, 0, 4);
                        uint32_t t56 = GM_LD(uint32_t, (((uint8_t *)&gm_l6) + 0));
                        GM_ST(uint32_t, (uint8_t *)&t55 + 0, t56);
                        gm_b16_8 t57 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 0));
                        gm_b16_8 t58 = GM_LD(gm_b16_8, ((uint8_t *)&t57));
                        GM_ST(gm_b16_8, (uint8_t *)&t55 + 8, t58);
                        gm_tftb1odc739t5 t59 = gm_f_push_d1r4h19(t54, &t55);
                        if (((((uint8_t *)&t59))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t59) + 1, gm_src3k278vcmqgdr, 769, 9);
                        gm_b0_1 t60 = GM_LD(gm_b0_1, (((uint8_t *)&t59) + 1));
                    }
                    goto me14;
mn15:;
                    gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 761, 5);
me14:;
                }
lc5:;
                {
                    uint64_t t62 = gm_l5;
                    uint64_t r63; bool o64 = __builtin_add_overflow(t62, ((uint64_t)1ULL), &r63);
                    uint64_t t65 = r63;
                    gm_l5 = t65;
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
        void * t66 = gm_l0;
        gm_b16_8 t67 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
        gm_b16_8 t68 = GM_LD(gm_b16_8, ((uint8_t *)&t67));
        gm_tnpifv20irnph t69 = gm_f_effect_set_vtcpipr((void *)NULL, cx, NULL, t68);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t70 = gm_f_flatten_2js7j92((void *)NULL, cx, NULL, t66, t69);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t71 = 0;
        F.ret = t70;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_subst_fields_pqf9lb0(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2) {
    struct gm_fr_subst_fields_pqf9lb0 F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l1; /* fields */
    gm_b16_8 gm_l2; /* map */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    GM_STACK_CHECK(&F, 341u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        gm_f_list_spqn36o((void *)&F.l3, (void *)NULL, cx, NULL);
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
                    gm_t0mglksrkfa5e t15 = GM_LD(gm_t0mglksrkfa5e, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t11 * 40));
                    F.l6 = t15;
                    {
                        void * t17 = (void *)((uint8_t *)&F.l3);
                        gm_t0mglksrkfa5e t18;
                        memset((uint8_t *)&t18 + 21, 0, 3);
                        gm_b16_8 t19 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
                        GM_ST(gm_b16_8, (uint8_t *)&t18 + 0, t19);
                        void * t20 = gm_l0;
                        gm_t88rcucg1b7uk t21 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l6) + 16));
                        gm_b16_8 t22 = gm_l2;
                        gm_t88rcucg1b7uk t23 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t20, t21, t22);
                        if (gm_unw.kind) {
                            goto gm_ll8;
                        }
                        GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t18 + 16, t23);
                        gm_t9qkh0rsooc8d t24 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&F.l6) + 20));
                        GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t18 + 20, t24);
                        gm_tf5srh4mp4b3i t25 = GM_LD(gm_tf5srh4mp4b3i, (((uint8_t *)&F.l6) + 24));
                        GM_ST(gm_tf5srh4mp4b3i, (uint8_t *)&t18 + 24, t25);
                        gm_tftb1odc739t5 t26 = gm_f_push_vrmo7c5(t17, &t18);
                        if (((((uint8_t *)&t26))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t26) + 1, gm_src3k278vcmqgdr, 640, 23);
                        gm_b0_1 t27 = GM_LD(gm_b0_1, (((uint8_t *)&t26) + 1));
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 640, 5);
me13:;
                }
lc4:;
                {
                    uint64_t t29 = gm_l5;
                    uint64_t r30; bool o31 = __builtin_add_overflow(t29, ((uint64_t)1ULL), &r30);
                    uint64_t t32 = r30;
                    gm_l5 = t32;
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
        gm_b16_8 t33 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
        uint8_t t34 = 0;
        F.ret = t33;
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
inline __attribute__((aligned(4))) void gm_f_subst_sig_9ngh936(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2) {
    struct gm_fr_subst_sig_9ngh936 F;
    void * gm_l0; /* s */
    void * gm_l1; /* sig */
    gm_b16_8 gm_l2; /* map */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_t88rcucg1b7uk gm_l6; /* p */
    gm_t88rcucg1b7uk gm_l7; /* ret */
    gm_tnpifv20irnph gm_l8; /* effects */
    GM_STACK_CHECK(&F, 364u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        gm_f_list_nof7vfj((void *)&F.l3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            void * t3 = gm_l1;
            gm_b16_8 t4 = GM_LD(gm_b16_8, (((uint8_t *)t3) + 0));
            gm_l4 = t4;
        }
        {
            gm_l5 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t6 = gm_l5;
                uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                uint8_t t8 = (t6 < t7);
                if (!t8) goto lb5;
                {
                    uint64_t t11 = gm_l5;
                    uint64_t t12 = (uintptr_t)t11;
                    uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    gm_t88rcucg1b7uk t16 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t12 * 4));
                    gm_l6 = t16;
                    {
                        void * t18 = (void *)((uint8_t *)&F.l3);
                        void * t19 = gm_l0;
                        gm_t88rcucg1b7uk t20 = gm_l6;
                        gm_b16_8 t21 = gm_l2;
                        gm_t88rcucg1b7uk t22 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t19, t20, t21);
                        if (gm_unw.kind) {
                            goto gm_ll9;
                        }
                        gm_tftb1odc739t5 t23 = gm_f_push_k3l9nic(t18, GM_CONV(gm_csguankko4lmhd, t22));
                        if (((((uint8_t *)&t23))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t23) + 1, gm_src3k278vcmqgdr, 751, 27);
                        gm_b0_1 t24 = GM_LD(gm_b0_1, (((uint8_t *)&t23) + 1));
                    }
                    goto me14;
mn15:;
                    gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 751, 5);
me14:;
                }
lc5:;
                {
                    uint64_t t26 = gm_l5;
                    uint64_t r27; bool o28 = __builtin_add_overflow(t26, ((uint64_t)1ULL), &r27);
                    uint64_t t29 = r27;
                    gm_l5 = t29;
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
        void * t30 = gm_l0;
        void * t31 = gm_l1;
        gm_t88rcucg1b7uk t32 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t31) + 16));
        gm_b16_8 t33 = gm_l2;
        gm_t88rcucg1b7uk t34 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t30, t32, t33);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l7 = t34;
    }
    {
        void * t35 = gm_l0;
        void * t36 = gm_l1;
        void * t37 = (void *)(((uint8_t *)t36) + 24);
        gm_b16_8 t38 = gm_l2;
        gm_tnpifv20irnph t39 = gm_f_subst_effects_jnlbu5m((void *)NULL, cx, NULL, t35, t37, t38);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l8 = t39;
    }
    {
        memset((uint8_t *)&(*(gm_tddtjfggdat0d *)gm_out) + 20, 0, 4);
        memset((uint8_t *)&(*(gm_tddtjfggdat0d *)gm_out) + 41, 0, 7);
        gm_b16_8 t40 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
        gm_b16_8 t41 = GM_LD(gm_b16_8, ((uint8_t *)&t40));
        gm_t88rcucg1b7uk t42 = gm_l7;
        gm_tnpifv20irnph t43 = gm_l8;
        void * t44 = gm_l1;
        uint8_t t45 = GM_LD(uint8_t, (((uint8_t *)t44) + 40));
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tddtjfggdat0d *)gm_out) + 0, t41);
        GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&(*(gm_tddtjfggdat0d *)gm_out) + 16, t42);
        GM_ST(gm_tnpifv20irnph, (uint8_t *)&(*(gm_tddtjfggdat0d *)gm_out) + 24, t43);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tddtjfggdat0d *)gm_out) + 40, t45);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_taken_struct_members_1ltbb18(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2, void * a3) {
    struct gm_fr_taken_struct_members_1ltbb18 F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    gm_b16_8 gm_l2; /* members */
    void * gm_l3; /* out */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    GM_STACK_CHECK(&F, 569u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        {
            gm_b16_8 t3 = gm_l2;
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
                    gm_tfc3oljlofib6 t15 = GM_LD(gm_tfc3oljlofib6, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t11 * 80));
                    F.l6 = t15;
                    {
                        uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&F.l6) + 0));
                        if (t19 != ((uint8_t)0ULL)) goto mn18;
                        gm_t24301fas1b2a t20 = GM_LD(gm_t24301fas1b2a, (((uint8_t *)&F.l6) + 8));
                        F.l7 = t20;
                        void * t21 = gm_l3;
                        gm_tftb1odc739t5 t22 = gm_f_push_ghqse9f(t21, &(*(gm_t24301fas1b2a *)((uint8_t *)&F.l7)));
                        if (((((uint8_t *)&t22))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t22) + 1, gm_src3k278vcmqgdr, 518, 26);
                        gm_b0_1 t23 = GM_LD(gm_b0_1, (((uint8_t *)&t22) + 1));
                        goto me17;
mn18:;
                        uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)&F.l6) + 0));
                        if (t25 != ((uint8_t)1ULL)) goto mn24;
                        gm_t7bjbmukv26e7 t26 = GM_LD(gm_t7bjbmukv26e7, (((uint8_t *)&F.l6) + 8));
                        F.l8 = t26;
                        void * t27 = gm_l0;
                        void * t28 = GM_LD(void *, (((uint8_t *)t27) + 0));
                        uint64_t t29 = gm_l1;
                        void * t30 = GM_LD(void *, (((uint8_t *)&F.l8) + 0));
                        gm_tqs5p4n2tq19v t31 = gm_f_when_cond_9a46a5h((void *)NULL, cx, NULL, t28, t29, t30);
                        if (gm_unw.kind) {
                            goto gm_ll8;
                        }
                        if (!((((uint8_t *)&t31))[1] != 0)) goto mn33;
                        uint8_t t34 = GM_LD(uint8_t, ((uint8_t *)&t31));
                        if (!(t34 == ((uint8_t)1))) goto mn33;
                        void * t35 = gm_l0;
                        uint64_t t36 = gm_l1;
                        gm_b16_8 t37 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 8));
                        gm_b16_8 t38 = GM_LD(gm_b16_8, ((uint8_t *)&t37));
                        void * t39 = gm_l3;
                        gm_b0_1 t40 = gm_f_taken_struct_members_1ltbb18((void *)NULL, cx, NULL, t35, t36, t38, t39);
                        if (gm_unw.kind) {
                            goto gm_ll8;
                        }
                        goto me32;
mn33:;
                        if (!((((uint8_t *)&t31))[1] != 0)) goto mn41;
                        uint8_t t42 = GM_LD(uint8_t, ((uint8_t *)&t31));
                        if (!(t42 == ((uint8_t)0))) goto mn41;
                        void * t43 = gm_l0;
                        uint64_t t44 = gm_l1;
                        gm_th2lqbo77pvsc t45 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)&F.l8) + 24));
                        gm_b16_8 t46;
                        if (((((uint8_t *)&t45))[16] != 0)) {
                            gm_b16_8 t47 = GM_LD(gm_b16_8, (((uint8_t *)&t45) + 0));
                            t46 = t47;
                        } else {
                            gm_b16_8 t48 = gm_f_empty_slice_d53jqcq();
                            t46 = t48;
                        }
                        gm_b16_8 t49 = GM_LD(gm_b16_8, ((uint8_t *)&t46));
                        void * t50 = gm_l3;
                        gm_b0_1 t51 = gm_f_taken_struct_members_1ltbb18((void *)NULL, cx, NULL, t43, t44, t49, t50);
                        if (gm_unw.kind) {
                            goto gm_ll8;
                        }
                        goto me32;
mn41:;
                        if (((((uint8_t *)&t31))[1] != 0)) goto mn52;
                        goto me32;
mn52:;
                        gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 519, 25);
me32:;
                        goto me17;
mn24:;
                        gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 517, 9);
me17:;
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 516, 5);
me13:;
                }
lc4:;
                {
                    uint64_t t55 = gm_l5;
                    uint64_t r56; bool o57 = __builtin_add_overflow(t55, ((uint64_t)1ULL), &r56);
                    uint64_t t58 = r56;
                    gm_l5 = t58;
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
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_taken_union_members_pavth81(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2, void * a3) {
    struct gm_fr_taken_union_members_pavth81 F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    gm_b16_8 gm_l2; /* members */
    void * gm_l3; /* out */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    GM_STACK_CHECK(&F, 553u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        {
            gm_b16_8 t3 = gm_l2;
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
                    gm_t03a3imodqi32 t15 = GM_LD(gm_t03a3imodqi32, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t11 * 72));
                    F.l6 = t15;
                    {
                        uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&F.l6) + 0));
                        if (t19 != ((uint8_t)0ULL)) goto mn18;
                        gm_tnljbma3s3h16 t20 = GM_LD(gm_tnljbma3s3h16, (((uint8_t *)&F.l6) + 8));
                        F.l7 = t20;
                        void * t21 = gm_l3;
                        gm_tftb1odc739t5 t22 = gm_f_push_83inprj(t21, &(*(gm_tnljbma3s3h16 *)((uint8_t *)&F.l7)));
                        if (((((uint8_t *)&t22))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t22) + 1, gm_src3k278vcmqgdr, 532, 28);
                        gm_b0_1 t23 = GM_LD(gm_b0_1, (((uint8_t *)&t22) + 1));
                        goto me17;
mn18:;
                        uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)&F.l6) + 0));
                        if (t25 != ((uint8_t)1ULL)) goto mn24;
                        gm_t7bjbmukv26e7 t26 = GM_LD(gm_t7bjbmukv26e7, (((uint8_t *)&F.l6) + 8));
                        F.l8 = t26;
                        void * t27 = gm_l0;
                        void * t28 = GM_LD(void *, (((uint8_t *)t27) + 0));
                        uint64_t t29 = gm_l1;
                        void * t30 = GM_LD(void *, (((uint8_t *)&F.l8) + 0));
                        gm_tqs5p4n2tq19v t31 = gm_f_when_cond_9a46a5h((void *)NULL, cx, NULL, t28, t29, t30);
                        if (gm_unw.kind) {
                            goto gm_ll8;
                        }
                        if (!((((uint8_t *)&t31))[1] != 0)) goto mn33;
                        uint8_t t34 = GM_LD(uint8_t, ((uint8_t *)&t31));
                        if (!(t34 == ((uint8_t)1))) goto mn33;
                        void * t35 = gm_l0;
                        uint64_t t36 = gm_l1;
                        gm_b16_8 t37 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 8));
                        gm_b16_8 t38 = GM_LD(gm_b16_8, ((uint8_t *)&t37));
                        void * t39 = gm_l3;
                        gm_b0_1 t40 = gm_f_taken_union_members_pavth81((void *)NULL, cx, NULL, t35, t36, t38, t39);
                        if (gm_unw.kind) {
                            goto gm_ll8;
                        }
                        goto me32;
mn33:;
                        if (!((((uint8_t *)&t31))[1] != 0)) goto mn41;
                        uint8_t t42 = GM_LD(uint8_t, ((uint8_t *)&t31));
                        if (!(t42 == ((uint8_t)0))) goto mn41;
                        void * t43 = gm_l0;
                        uint64_t t44 = gm_l1;
                        gm_th2lqbo77pvsc t45 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)&F.l8) + 24));
                        gm_b16_8 t46;
                        if (((((uint8_t *)&t45))[16] != 0)) {
                            gm_b16_8 t47 = GM_LD(gm_b16_8, (((uint8_t *)&t45) + 0));
                            t46 = t47;
                        } else {
                            gm_b16_8 t48 = gm_f_empty_slice_u4e74ha();
                            t46 = t48;
                        }
                        gm_b16_8 t49 = GM_LD(gm_b16_8, ((uint8_t *)&t46));
                        void * t50 = gm_l3;
                        gm_b0_1 t51 = gm_f_taken_union_members_pavth81((void *)NULL, cx, NULL, t43, t44, t49, t50);
                        if (gm_unw.kind) {
                            goto gm_ll8;
                        }
                        goto me32;
mn41:;
                        if (((((uint8_t *)&t31))[1] != 0)) goto mn52;
                        goto me32;
mn52:;
                        gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 533, 25);
me32:;
                        goto me17;
mn24:;
                        gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 531, 9);
me17:;
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 530, 5);
me13:;
                }
lc4:;
                {
                    uint64_t t55 = gm_l5;
                    uint64_t r56; bool o57 = __builtin_add_overflow(t55, ((uint64_t)1ULL), &r56);
                    uint64_t t58 = r56;
                    gm_l5 = t58;
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
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_union_kind_j2gcmto(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_b16_8 a3) {
    struct gm_fr_union_kind_j2gcmto F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* u */
    gm_b16_8 gm_l3; /* attrs */
    uint8_t gm_l4; /* c_union */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    gm_b16_8 gm_l8; /* other */
    gm_tkn42u7na3t81 gm_l9; /* explicit_tag */
    void * gm_l10; /* t */
    gm_t88rcucg1b7uk gm_l11; /* tag */
    gm_b16_8 gm_l16; /* <tmp> */
    uint64_t gm_l17; /* <tmp> */
    gm_b16_8 gm_l19; /* <tmp> */
    uint64_t gm_l20; /* <tmp> */
    gm_tkn42u7na3t81 gm_l22; /* payload */
    void * gm_l23; /* p */
    void * gm_l25; /* val */
    gm_b16_8 gm_l29; /* <tmp> */
    uint64_t gm_l30; /* <tmp> */
    uint64_t gm_l31; /* i */
    gm_t88rcucg1b7uk gm_l33; /* p */
    uint64_t gm_l34; /* n */
    gm_t88rcucg1b7uk gm_l35; /* tag */
    gm_b16_8 gm_l36; /* <tmp> */
    uint64_t gm_l37; /* <tmp> */
    uint64_t gm_l38; /* i */
    gm_b16_8 gm_l40; /* <tmp> */
    uint64_t gm_l41; /* <tmp> */
    uint64_t gm_l42; /* j */
    GM_STACK_CHECK(&F, 3600u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        gm_l4 = ((uint8_t)0);
    }
    {
        {
            gm_b16_8 t3 = gm_l3;
            gm_l5 = t3;
        }
        {
            gm_l6 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t5 = gm_l6;
                uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                uint8_t t7 = (t5 < t6);
                if (!t7) goto lb4;
                {
                    uint64_t t10 = gm_l6;
                    uint64_t t11 = (uintptr_t)t10;
                    uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                    gm_tkacitepg5fk0 t15 = GM_LD(gm_tkacitepg5fk0, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t11 * 72));
                    F.l7 = t15;
                    {
                        if (!((GM_LD(uintptr_t, ((((uint8_t *)&F.l7) + 0) + 0) + 8) == 7) && memcmp(GM_LD(void *, ((((uint8_t *)&F.l7) + 0) + 0)), gm_suuv0qpi4v4e4, 7) == 0)) goto mn18;
                        {
                            gm_l4 = ((uint8_t)1);
                        }
                        goto me17;
mn18:;
                        gm_b16_8 t21 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l7) + 0) + 0));
                        gm_l8 = t21;
                        void * t22 = gm_l0;
                        gm_tl874t2atnlh3 t23 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l7) + 56));
                        gm_b16_8 t24 = gm_l8;
                        gm_tnpifv20irnph t25;
                        GM_ST(gm_b16_8, ((uint8_t *)&t25 + 0), t24);
                        gm_tabb710d9mq2e t26;
                        gm_f_format_45bh632((void *)&t26, (void *)NULL, cx, NULL, t25);
                        if (gm_unw.kind) {
                            goto gm_ll8;
                        }
                        if (((((uint8_t *)&t26))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t26) + 8, gm_src3k278vcmqgdr, 345, 38);
                        gm_b16_8 t27 = GM_LD(gm_b16_8, (((uint8_t *)&t26) + 8));
                        gm_b16_8 t28 = GM_LD(gm_b16_8, ((uint8_t *)&t27));
                        gm_b0_1 t29 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t22, t23, t28);
                        if (gm_unw.kind) {
                            goto gm_ll8;
                        }
                        goto me17;
mn20:;
                        gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 343, 9);
me17:;
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 342, 5);
me13:;
                }
lc4:;
                {
                    uint64_t t31 = gm_l6;
                    uint64_t r32; bool o33 = __builtin_add_overflow(t31, ((uint64_t)1ULL), &r32);
                    uint64_t t34 = r32;
                    gm_l6 = t34;
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
        gm_tkn42u7na3t81 t35;
        memset(&t35, 0, sizeof t35);
        gm_l9 = t35;
    }
    {
        void * t37 = gm_l2;
        void * t38 = GM_LD(void *, (((uint8_t *)t37) + 0));
        if (!(GM_LD(void *, ((uint8_t *)&t38)) != 0)) goto ie36;
        void * t39 = GM_LD(void *, ((uint8_t *)&t38));
        gm_l10 = t39;
        {
            void * t41 = gm_l0;
            uint64_t t42 = gm_l1;
            void * t43 = gm_l10;
            gm_t88rcucg1b7uk t44 = gm_f_resolve_type_jjodbei((void *)NULL, cx, NULL, t41, t42, t43);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l11 = t44;
        }
        {
            void * t46 = gm_l0;
            void * t47 = (void *)(((uint8_t *)t46) + 8);
            gm_t88rcucg1b7uk t48 = gm_l11;
            uint8_t t49 = gm_f_is_integer_8d4f9ek(t47, GM_CONV(gm_csguankko4lmhd, t48));
            uint8_t t50 = (uint8_t)!t49;
            if (!t50) goto ie45;
            void * t51 = gm_l0;
            void * t52 = (void *)(((uint8_t *)t51) + 8);
            gm_t88rcucg1b7uk t53 = gm_l11;
            uint8_t t54 = gm_f_is_error_8k27gd1(t52, GM_CONV(gm_csguankko4lmhd, t53));
            uint8_t t55 = (uint8_t)!t54;
            if (!t55) goto ie45;
            {
                void * t57 = gm_l0;
                void * t58 = gm_l2;
                gm_tl874t2atnlh3 t59 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t58) + 64));
                void * t60 = gm_l0;
                gm_t88rcucg1b7uk t61 = gm_l11;
                gm_b16_8 t62 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t60, t61);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tnpifv20irnph t63;
                GM_ST(gm_b16_8, ((uint8_t *)&t63 + 0), t62);
                gm_tabb710d9mq2e t64;
                gm_f_format_9kb6o1u((void *)&t64, (void *)NULL, cx, NULL, t63);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t64))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t64) + 8, gm_src3k278vcmqgdr, 352, 29);
                gm_b16_8 t65 = GM_LD(gm_b16_8, (((uint8_t *)&t64) + 8));
                gm_b16_8 t66 = GM_LD(gm_b16_8, ((uint8_t *)&t65));
                gm_b0_1 t67 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t57, t59, t66);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix45;
ie45:;
ix45:;
        }
        {
            gm_tkn42u7na3t81 t68;
            void * t70 = gm_l0;
            void * t71 = (void *)(((uint8_t *)t70) + 8);
            gm_t88rcucg1b7uk t72 = gm_l11;
            uint8_t t73 = gm_f_is_integer_8d4f9ek(t71, GM_CONV(gm_csguankko4lmhd, t72));
            if (!t73) goto ie69;
            gm_t88rcucg1b7uk t75 = gm_l11;
            gm_t88rcucg1b7uk t76 = t75;
            gm_tkn42u7na3t81 t77;
            GM_ST(uint8_t, (uint8_t *)&t77 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t77 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t77), t76);
            ((uint8_t *)&t77)[4] = 1;
            t68 = t77;
            goto ix69;
ie69:;
            void * t79 = gm_l0;
            void * t80 = (void *)(((uint8_t *)t79) + 8);
            gm_t88rcucg1b7uk t81 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t80);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t88rcucg1b7uk t82 = t81;
            gm_tkn42u7na3t81 t83;
            GM_ST(uint8_t, (uint8_t *)&t83 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t83 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t83), t82);
            ((uint8_t *)&t83)[4] = 1;
            t68 = t83;
ix69:;
            gm_l9 = t68;
        }
        goto ix36;
ie36:;
ix36:;
    }
    {
        gm_f_list_g6uul2f((void *)&F.l12, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_list_7s4om10((void *)&F.l13, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_zero_s25l990((void *)&F.l14, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_list_ja7tv3o((void *)&F.l15, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t84 = gm_l0;
        uint64_t t85 = gm_l1;
        void * t86 = gm_l2;
        gm_b16_8 t87 = GM_LD(gm_b16_8, (((uint8_t *)t86) + 48));
        gm_b16_8 t88 = GM_LD(gm_b16_8, ((uint8_t *)&t87));
        void * t89 = (void *)((uint8_t *)&F.l15);
        gm_b0_1 t90 = gm_f_taken_union_members_pavth81((void *)NULL, cx, NULL, t84, t85, t88, t89);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t92 = GM_LD(gm_b16_8, (((uint8_t *)&F.l15) + 0));
            gm_l16 = t92;
        }
        {
            gm_l17 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t94 = gm_l17;
                uint64_t t95 = GM_LD(uintptr_t, ((uint8_t *)&gm_l16) + 8);
                uint8_t t96 = (t94 < t95);
                if (!t96) goto lb93;
                {
                    uint64_t t99 = gm_l17;
                    uint64_t t100 = (uintptr_t)t99;
                    uint64_t t101 = GM_LD(uintptr_t, ((uint8_t *)&gm_l16) + 8);
                    gm_tnljbma3s3h16 t104 = GM_LD(gm_tnljbma3s3h16, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l16)) + t100 * 64));
                    F.l18 = t104;
                    {
                        {
                            gm_b16_8 t107 = GM_LD(gm_b16_8, (((uint8_t *)&F.l12) + 0));
                            gm_l19 = t107;
                        }
                        {
                            gm_l20 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t109 = gm_l20;
                                uint64_t t110 = GM_LD(uintptr_t, ((uint8_t *)&gm_l19) + 8);
                                uint8_t t111 = (t109 < t110);
                                if (!t111) goto lb108;
                                {
                                    uint64_t t114 = gm_l20;
                                    uint64_t t115 = (uintptr_t)t114;
                                    uint64_t t116 = GM_LD(uintptr_t, ((uint8_t *)&gm_l19) + 8);
                                    gm_tckdobunaulee t119 = GM_LD(gm_tckdobunaulee, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l19)) + t115 * 72));
                                    F.l21 = t119;
                                    {
                                        gm_b16_8 t122 = GM_LD(gm_b16_8, (((uint8_t *)&F.l21) + 0));
                                        gm_b16_8 t123 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l18) + 0) + 0));
                                        uint8_t t124 = (gm_eq0iqiubfm7e(((uint8_t *)&t122), ((uint8_t *)&t123)));
                                        if (!t124) goto ie121;
                                        {
                                            void * t126 = gm_l0;
                                            gm_tl874t2atnlh3 t127 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)&F.l18) + 0) + 16));
                                            gm_b16_8 t128 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l18) + 0) + 0));
                                            gm_tnpifv20irnph t129;
                                            GM_ST(gm_b16_8, ((uint8_t *)&t129 + 0), t128);
                                            gm_tabb710d9mq2e t130;
                                            gm_f_format_72n0e07((void *)&t130, (void *)NULL, cx, NULL, t129);
                                            if (gm_unw.kind) {
                                                goto gm_ll112;
                                            }
                                            if (((((uint8_t *)&t130))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t130) + 8, gm_src3k278vcmqgdr, 365, 38);
                                            gm_b16_8 t131 = GM_LD(gm_b16_8, (((uint8_t *)&t130) + 8));
                                            gm_b16_8 t132 = GM_LD(gm_b16_8, ((uint8_t *)&t131));
                                            gm_b0_1 t133 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t126, t127, t132);
                                            if (gm_unw.kind) {
                                                goto gm_ll112;
                                            }
                                        }
                                        {
                                            goto lb108;
                                        }
                                        goto ix121;
ie121:;
ix121:;
                                    }
                                    goto me117;
mn118:;
                                    gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 363, 9);
me117:;
                                }
lc108:;
                                {
                                    uint64_t t135 = gm_l20;
                                    uint64_t r136; bool o137 = __builtin_add_overflow(t135, ((uint64_t)1ULL), &r136);
                                    uint64_t t138 = r136;
                                    gm_l20 = t138;
                                }
                            }
gm_ll112:;
                            if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb108; }
                                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc108; }
                            }
                            goto gm_ll97;
lb108:;
                        }
                    }
                    {
                        gm_tkn42u7na3t81 t139;
                        memset(&t139, 0, sizeof t139);
                        gm_l22 = t139;
                    }
                    {
                        void * t141 = GM_LD(void *, (((uint8_t *)&F.l18) + 32));
                        if (!(GM_LD(void *, ((uint8_t *)&t141)) != 0)) goto ie140;
                        void * t142 = GM_LD(void *, ((uint8_t *)&t141));
                        gm_l23 = t142;
                        {
                            void * t144 = gm_l0;
                            uint64_t t145 = gm_l1;
                            void * t146 = gm_l23;
                            gm_t88rcucg1b7uk t147 = gm_f_resolve_type_jjodbei((void *)NULL, cx, NULL, t144, t145, t146);
                            if (gm_unw.kind) {
                                goto gm_ll97;
                            }
                            gm_tkn42u7na3t81 t148;
                            GM_ST(uint8_t, (uint8_t *)&t148 + 5, 0);
                            GM_ST(uint16_t, (uint8_t *)&t148 + 6, 0);
                            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t148), t147);
                            ((uint8_t *)&t148)[4] = 1;
                            gm_l22 = t148;
                        }
                        goto ix140;
ie140:;
ix140:;
                    }
                    {
                        gm_tbci5ospk5nnn t149 = F.l14;
                        F.l24 = t149;
                    }
                    {
                        void * t151 = GM_LD(void *, (((uint8_t *)&F.l18) + 40));
                        if (!(GM_LD(void *, ((uint8_t *)&t151)) != 0)) goto ie150;
                        void * t152 = GM_LD(void *, ((uint8_t *)&t151));
                        gm_l25 = t152;
                        {
                            uint8_t t155;
                            if (((((uint8_t *)&gm_l9))[4] != 0)) goto mn157;
                            t155 = ((uint8_t)1);
                            goto me156;
mn157:;
                            t155 = ((uint8_t)0);
                            goto me156;
mn158:;
                            gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 373, 16);
me156:;
                            if (!t155) goto ie154;
                            {
                                void * t160 = gm_l0;
                                void * t161 = gm_l25;
                                gm_tl874t2atnlh3 t162 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t161) + 72));
                                gm_b16_8 t163;
                                GM_ST(void *, (uint8_t *)&t163, (void *)(gm_su38t05utsk53));
                                GM_ST(uintptr_t, (uint8_t *)&t163 + 8, (uintptr_t)(66));
                                gm_b0_1 t164 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t160, t162, t163);
                                if (gm_unw.kind) {
                                    goto gm_ll97;
                                }
                            }
                            goto ix154;
ie154:;
ix154:;
                        }
                        {
                            void * t165 = gm_l0;
                            uint64_t t166 = gm_l1;
                            void * t167 = gm_l25;
                            gm_tkv1vap90vht2 t168;
                            gm_f_try_untyped_fj0h8i5((void *)&t168, (void *)NULL, cx, NULL, t165, t166, t167);
                            if (gm_unw.kind) {
                                goto gm_ll97;
                            }
                            if (!((((uint8_t *)&t168))[208] != 0)) goto mn170;
                            uint8_t t171 = GM_LD(uint8_t, (((uint8_t *)&t168) + 0));
                            if (t171 != ((uint8_t)0ULL)) goto mn170;
                            F.l26 = (*(gm_t0ialelq3ql1k *)(((uint8_t *)&t168) + 8));
                            {
                                uint8_t t174 = GM_LD(uint8_t, (((uint8_t *)&F.l26) + 0));
                                if (t174 != ((uint8_t)0ULL)) goto ie173;
                                gm_tbci5ospk5nnn t175 = GM_LD(gm_tbci5ospk5nnn, (((uint8_t *)&F.l26) + 8));
                                F.l27 = t175;
                                {
                                    gm_tbci5ospk5nnn t177 = F.l27;
                                    F.l24 = t177;
                                }
                                goto ix173;
ie173:;
                                {
                                    void * t179 = gm_l0;
                                    void * t180 = gm_l25;
                                    gm_tl874t2atnlh3 t181 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t180) + 72));
                                    gm_b16_8 t182;
                                    GM_ST(void *, (uint8_t *)&t182, (void *)(gm_seoeuc6nik756));
                                    GM_ST(uintptr_t, (uint8_t *)&t182 + 8, (uintptr_t)(39));
                                    gm_b0_1 t183 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t179, t181, t182);
                                    if (gm_unw.kind) {
                                        goto gm_ll97;
                                    }
                                }
ix173:;
                            }
                            goto me169;
mn170:;
                            if (!((((uint8_t *)&t168))[208] != 0)) goto mn184;
                            uint8_t t185 = GM_LD(uint8_t, (((uint8_t *)&t168) + 0));
                            if (t185 != ((uint8_t)1ULL)) goto mn184;
                            goto me169;
mn184:;
                            if (((((uint8_t *)&t168))[208] != 0)) goto mn187;
                            void * t188 = gm_l0;
                            void * t189 = gm_l25;
                            gm_tl874t2atnlh3 t190 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t189) + 72));
                            gm_b16_8 t191;
                            GM_ST(void *, (uint8_t *)&t191, (void *)(gm_seoeuc6nik756));
                            GM_ST(uintptr_t, (uint8_t *)&t191 + 8, (uintptr_t)(39));
                            gm_b0_1 t192 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t188, t190, t191);
                            if (gm_unw.kind) {
                                goto gm_ll97;
                            }
                            goto me169;
mn187:;
                            gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 374, 13);
me169:;
                        }
                        goto ix150;
ie150:;
ix150:;
                    }
                    {
                        gm_tbci5ospk5nnn t193 = F.l24;
                        gm_tbci5ospk5nnn t194;
                        gm_f_one_08qg1op((void *)&t194, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_ll97;
                        }
                        gm_f_add_420lj6o((void *)&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l14)), (void *)NULL, cx, NULL, &t193, &t194);
                        if (gm_unw.kind) {
                            goto gm_ll97;
                        }
                    }
                    {
                        void * t195 = (void *)((uint8_t *)&F.l12);
                        gm_tckdobunaulee t196;
                        gm_b16_8 t197 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l18) + 0) + 0));
                        GM_ST(gm_b16_8, (uint8_t *)&t196 + 0, t197);
                        gm_tkn42u7na3t81 t198 = gm_l22;
                        GM_ST(gm_tkn42u7na3t81, (uint8_t *)&t196 + 16, t198);
                        memcpy((uint8_t *)&t196 + 24, ((uint8_t *)&F.l24), 48);
                        gm_tftb1odc739t5 t199 = gm_f_push_1j8vbii(t195, &t196);
                        if (((((uint8_t *)&t199))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t199) + 1, gm_src3k278vcmqgdr, 387, 9);
                        gm_b0_1 t200 = GM_LD(gm_b0_1, (((uint8_t *)&t199) + 1));
                    }
                    {
                        void * t201 = (void *)((uint8_t *)&F.l13);
                        gm_tl874t2atnlh3 t202 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l18) + 48));
                        gm_tftb1odc739t5 t203 = gm_f_push_0mk7udh(t201, GM_CONV(gm_cseahvg8v7p0vs, t202));
                        if (((((uint8_t *)&t203))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t203) + 1, gm_src3k278vcmqgdr, 388, 9);
                        gm_b0_1 t204 = GM_LD(gm_b0_1, (((uint8_t *)&t203) + 1));
                    }
                    goto me102;
mn103:;
                    gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 362, 5);
me102:;
                }
lc93:;
                {
                    uint64_t t206 = gm_l17;
                    uint64_t r207; bool o208 = __builtin_add_overflow(t206, ((uint64_t)1ULL), &r207);
                    uint64_t t209 = r207;
                    gm_l17 = t209;
                }
            }
gm_ll97:;
            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb93; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc93; }
            }
            goto gm_lad;
lb93:;
        }
    }
    {
        uint8_t t211 = gm_l4;
        if (!t211) goto ie210;
        {
            gm_f_list_spqn36o((void *)&F.l28, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_b16_8 t214 = GM_LD(gm_b16_8, (((uint8_t *)&F.l12) + 0));
                gm_b16_8 t215 = GM_LD(gm_b16_8, ((uint8_t *)&t214));
                gm_l29 = t215;
            }
            {
                gm_l30 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t217 = gm_l30;
                    uint64_t t218 = GM_LD(uintptr_t, ((uint8_t *)&gm_l29) + 8);
                    uint8_t t219 = (t217 < t218);
                    if (!t219) goto lb216;
                    {
                        gm_t3l8idb2m6c1r t222;
                        uint64_t t223 = gm_l30;
                        GM_ST(uint64_t, (uint8_t *)&t222 + 0, t223);
                        uint64_t t224 = gm_l30;
                        uint64_t t225 = (uintptr_t)t224;
                        uint64_t t226 = GM_LD(uintptr_t, ((uint8_t *)&gm_l29) + 8);
                        memcpy((uint8_t *)&t222 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l29)) + t225 * 72), 72);
                        uint64_t t229 = GM_LD(uint64_t, (((uint8_t *)&t222) + 0));
                        gm_l31 = t229;
                        F.l32 = (*(gm_tckdobunaulee *)(((uint8_t *)&t222) + 8));
                        {
                            if (!(((((uint8_t *)&F.l32) + 16))[4] != 0)) goto ie231;
                            gm_t88rcucg1b7uk t232 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l32) + 16));
                            gm_l33 = t232;
                            {
                                void * t234 = (void *)((uint8_t *)&F.l28);
                                gm_t0mglksrkfa5e t235;
                                memset((uint8_t *)&t235 + 21, 0, 3);
                                gm_b16_8 t236 = GM_LD(gm_b16_8, (((uint8_t *)&F.l32) + 0));
                                GM_ST(gm_b16_8, (uint8_t *)&t235 + 0, t236);
                                gm_t88rcucg1b7uk t237 = gm_l33;
                                GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t235 + 16, t237);
                                gm_t9qkh0rsooc8d t238;
                                GM_ST(uint8_t, ((uint8_t *)&t238 + 0), ((uint8_t)2ULL));
                                GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t235 + 20, t238);
                                gm_tf5srh4mp4b3i t239;
                                memset(&t239, 0, sizeof t239);
                                GM_ST(gm_tf5srh4mp4b3i, (uint8_t *)&t235 + 24, t239);
                                gm_tftb1odc739t5 t240 = gm_f_push_vrmo7c5(t234, &t235);
                                if (((((uint8_t *)&t240))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t240) + 1, gm_src3k278vcmqgdr, 394, 17);
                                gm_b0_1 t241 = GM_LD(gm_b0_1, (((uint8_t *)&t240) + 1));
                            }
                            goto ix231;
ie231:;
                            {
                                void * t243 = gm_l0;
                                uint64_t t244 = gm_l31;
                                uint64_t t245 = (uintptr_t)t244;
                                uint64_t t246 = GM_LD(uintptr_t, (((uint8_t *)&F.l13) + 0) + 8);
                                if (t245 >= (uintptr_t)(t246)) gm_trap_index(t245, t246, gm_src3k278vcmqgdr, 396, 25);
                                gm_tl874t2atnlh3 t247 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l13) + 0)) + t245 * 12));
                                gm_b16_8 t248;
                                GM_ST(void *, (uint8_t *)&t248, (void *)(gm_s8h39pil8lgh0));
                                GM_ST(uintptr_t, (uint8_t *)&t248 + 8, (uintptr_t)(40));
                                gm_b0_1 t249 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t243, t247, t248);
                                if (gm_unw.kind) {
                                    goto gm_ll220;
                                }
                            }
ix231:;
                        }
                        goto me227;
mn228:;
                        gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 392, 9);
me227:;
                    }
lc216:;
                    {
                        uint64_t t251 = gm_l30;
                        uint64_t r252; bool o253 = __builtin_add_overflow(t251, ((uint64_t)1ULL), &r252);
                        uint64_t t254 = r252;
                        gm_l30 = t254;
                    }
                }
gm_ll220:;
                if (gm_unw.frame == &F.h && gm_unw.label == 3) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb216; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc216; }
                }
                goto gm_lad;
lb216:;
            }
        }
        {
            gm_b16_8 t255 = GM_LD(gm_b16_8, (((uint8_t *)&F.l28) + 0));
            memset(&(*(gm_ti0kgh3pc3in2 *)gm_out), 0, sizeof (*(gm_ti0kgh3pc3in2 *)gm_out));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_ti0kgh3pc3in2 *)gm_out) + 0, ((uint8_t)2ULL));
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_ti0kgh3pc3in2 *)gm_out) + 8, t255);
            goto gm_fdone;
        }
        goto ix210;
ie210:;
ix210:;
    }
    {
        uint64_t t256 = GM_LD(uintptr_t, (((uint8_t *)&F.l12) + 0) + 8);
        gm_l34 = t256;
    }
    {
        gm_tkn42u7na3t81 t257 = gm_l9;
        gm_t88rcucg1b7uk t258;
        if (((((uint8_t *)&t257))[4] != 0)) {
            gm_t88rcucg1b7uk t259 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t257) + 0));
            t258 = t259;
        } else {
            gm_t88rcucg1b7uk t260;
            uint64_t t262 = gm_l34;
            uint8_t t263 = (t262 <= ((uint64_t)256ULL));
            if (!t263) goto ie261;
            void * t265 = gm_l0;
            void * t266 = (void *)(((uint8_t *)t265) + 8);
            gm_t88rcucg1b7uk t267 = gm_f_u8_ty_afkvj25((void *)NULL, cx, NULL, t266);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t88rcucg1b7uk t268 = t267;
            t260 = t268;
            goto ix261;
ie261:;
            gm_t88rcucg1b7uk t269;
            uint64_t t271 = gm_l34;
            uint8_t t272 = (t271 <= ((uint64_t)65536ULL));
            if (!t272) goto ie270;
            void * t274 = gm_l0;
            void * t275 = (void *)(((uint8_t *)t274) + 8);
            gm_t88rcucg1b7uk t276 = gm_f_int_ci6airg((void *)NULL, cx, NULL, t275, ((uint8_t)0), ((uint16_t)16ULL));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t88rcucg1b7uk t277 = t276;
            t269 = t277;
            goto ix270;
ie270:;
            void * t279 = gm_l0;
            void * t280 = (void *)(((uint8_t *)t279) + 8);
            gm_t88rcucg1b7uk t281 = gm_f_int_ci6airg((void *)NULL, cx, NULL, t280, ((uint8_t)0), ((uint16_t)32ULL));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t88rcucg1b7uk t282 = t281;
            t269 = t282;
ix270:;
            t260 = t269;
ix261:;
            t258 = t260;
        }
        gm_l35 = t258;
    }
    {
        {
            gm_b16_8 t284 = GM_LD(gm_b16_8, (((uint8_t *)&F.l12) + 0));
            gm_b16_8 t285 = GM_LD(gm_b16_8, ((uint8_t *)&t284));
            gm_l36 = t285;
        }
        {
            gm_l37 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t287 = gm_l37;
                uint64_t t288 = GM_LD(uintptr_t, ((uint8_t *)&gm_l36) + 8);
                uint8_t t289 = (t287 < t288);
                if (!t289) goto lb286;
                {
                    gm_t3l8idb2m6c1r t292;
                    uint64_t t293 = gm_l37;
                    GM_ST(uint64_t, (uint8_t *)&t292 + 0, t293);
                    uint64_t t294 = gm_l37;
                    uint64_t t295 = (uintptr_t)t294;
                    uint64_t t296 = GM_LD(uintptr_t, ((uint8_t *)&gm_l36) + 8);
                    memcpy((uint8_t *)&t292 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l36)) + t295 * 72), 72);
                    uint64_t t299 = GM_LD(uint64_t, (((uint8_t *)&t292) + 0));
                    gm_l38 = t299;
                    F.l39 = (*(gm_tckdobunaulee *)(((uint8_t *)&t292) + 8));
                    {
                        void * t302 = gm_l0;
                        void * t303 = (void *)(((uint8_t *)t302) + 8);
                        gm_t88rcucg1b7uk t304 = gm_l35;
                        uint8_t t305 = gm_f_is_error_8k27gd1(t303, GM_CONV(gm_csguankko4lmhd, t304));
                        uint8_t t306 = (uint8_t)!t305;
                        if (!t306) goto ie301;
                        void * t307 = gm_l0;
                        gm_t88rcucg1b7uk t308 = gm_l35;
                        uint8_t t309 = gm_f_int_fits_imbqv0c((void *)NULL, cx, NULL, t307, &(*(gm_tbci5ospk5nnn *)(((uint8_t *)&F.l39) + 24)), t308);
                        if (gm_unw.kind) {
                            goto gm_ll290;
                        }
                        uint8_t t310 = (uint8_t)!t309;
                        if (!t310) goto ie301;
                        {
                            void * t312 = gm_l0;
                            uint64_t t313 = gm_l38;
                            uint64_t t314 = (uintptr_t)t313;
                            uint64_t t315 = GM_LD(uintptr_t, (((uint8_t *)&F.l13) + 0) + 8);
                            if (t314 >= (uintptr_t)(t315)) gm_trap_index(t314, t315, gm_src3k278vcmqgdr, 405, 21);
                            gm_tl874t2atnlh3 t316 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l13) + 0)) + t314 * 12));
                            gm_tfeab279c16rr t317;
                            gm_b16_8 t318 = gm_f_to_decimal_8bshn5b((void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)(((uint8_t *)&F.l39) + 24)));
                            if (gm_unw.kind) {
                                goto gm_ll290;
                            }
                            GM_ST(gm_b16_8, (uint8_t *)&t317 + 0, t318);
                            gm_b16_8 t319 = GM_LD(gm_b16_8, (((uint8_t *)&F.l39) + 0));
                            GM_ST(gm_b16_8, (uint8_t *)&t317 + 16, t319);
                            void * t320 = gm_l0;
                            gm_t88rcucg1b7uk t321 = gm_l35;
                            gm_b16_8 t322 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t320, t321);
                            if (gm_unw.kind) {
                                goto gm_ll290;
                            }
                            GM_ST(gm_b16_8, (uint8_t *)&t317 + 32, t322);
                            gm_tabb710d9mq2e t323;
                            gm_f_format_nag8m5u((void *)&t323, (void *)NULL, cx, NULL, &t317);
                            if (gm_unw.kind) {
                                goto gm_ll290;
                            }
                            if (((((uint8_t *)&t323))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t323) + 8, gm_src3k278vcmqgdr, 405, 37);
                            gm_b16_8 t324 = GM_LD(gm_b16_8, (((uint8_t *)&t323) + 8));
                            gm_b16_8 t325 = GM_LD(gm_b16_8, ((uint8_t *)&t324));
                            gm_b0_1 t326 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t312, t316, t325);
                            if (gm_unw.kind) {
                                goto gm_ll290;
                            }
                        }
                        goto ix301;
ie301:;
ix301:;
                    }
                    {
                        {
                            uint64_t t328 = GM_LD(uintptr_t, (((uint8_t *)&F.l12) + 0) + 8);
                            void * t329 = ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l12) + 0)));
                            uint64_t t330 = (uintptr_t)((uint64_t)0ULL);
                            uint64_t t331 = gm_l38;
                            uint64_t t332 = (uintptr_t)t331;
                            if (t330 > t332 || t332 > (uintptr_t)t328) gm_trap_slice(t330, t332, t328, gm_src3k278vcmqgdr, 407, 31);
                            gm_b16_8 t333;
                            GM_ST(void *, (uint8_t *)&t333, (void *)(((uint8_t *)t329 + t330 * 72)));
                            GM_ST(uintptr_t, (uint8_t *)&t333 + 8, (uintptr_t)(t332 - t330));
                            gm_b16_8 t334 = GM_LD(gm_b16_8, ((uint8_t *)&t333));
                            gm_l40 = t334;
                        }
                        {
                            gm_l41 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t336 = gm_l41;
                                uint64_t t337 = GM_LD(uintptr_t, ((uint8_t *)&gm_l40) + 8);
                                uint8_t t338 = (t336 < t337);
                                if (!t338) goto lb335;
                                {
                                    gm_t3l8idb2m6c1r t341;
                                    uint64_t t342 = gm_l41;
                                    GM_ST(uint64_t, (uint8_t *)&t341 + 0, t342);
                                    uint64_t t343 = gm_l41;
                                    uint64_t t344 = (uintptr_t)t343;
                                    uint64_t t345 = GM_LD(uintptr_t, ((uint8_t *)&gm_l40) + 8);
                                    memcpy((uint8_t *)&t341 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l40)) + t344 * 72), 72);
                                    uint64_t t348 = GM_LD(uint64_t, (((uint8_t *)&t341) + 0));
                                    gm_l42 = t348;
                                    F.l43 = (*(gm_tckdobunaulee *)(((uint8_t *)&t341) + 8));
                                    {
                                        uint8_t t351 = gm_f_eq_60fjn21(&(*(gm_tbci5ospk5nnn *)(((uint8_t *)&F.l43) + 24)), &(*(gm_tbci5ospk5nnn *)(((uint8_t *)&F.l39) + 24)));
                                        if (!t351) goto ie350;
                                        {
                                            uint64_t t353 = gm_l38;
                                            uint64_t t354 = (uintptr_t)t353;
                                            uint64_t t355 = GM_LD(uintptr_t, (((uint8_t *)&F.l13) + 0) + 8);
                                            if (t354 >= (uintptr_t)(t355)) gm_trap_index(t354, t355, gm_src3k278vcmqgdr, 409, 33);
                                            gm_tl874t2atnlh3 t356 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l13) + 0)) + t354 * 12));
                                            gm_tfeab279c16rr t357;
                                            gm_b16_8 t358 = GM_LD(gm_b16_8, (((uint8_t *)&F.l43) + 0));
                                            GM_ST(gm_b16_8, (uint8_t *)&t357 + 0, t358);
                                            gm_b16_8 t359 = GM_LD(gm_b16_8, (((uint8_t *)&F.l39) + 0));
                                            GM_ST(gm_b16_8, (uint8_t *)&t357 + 16, t359);
                                            gm_b16_8 t360 = gm_f_to_decimal_8bshn5b((void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)(((uint8_t *)&F.l39) + 24)));
                                            if (gm_unw.kind) {
                                                goto gm_ll339;
                                            }
                                            GM_ST(gm_b16_8, (uint8_t *)&t357 + 32, t360);
                                            gm_tabb710d9mq2e t361;
                                            gm_f_format_uaa99rp((void *)&t361, (void *)NULL, cx, NULL, &t357);
                                            if (gm_unw.kind) {
                                                goto gm_ll339;
                                            }
                                            if (((((uint8_t *)&t361))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t361) + 8, gm_src3k278vcmqgdr, 409, 49);
                                            gm_b16_8 t362 = GM_LD(gm_b16_8, (((uint8_t *)&t361) + 8));
                                            gm_b16_8 t363 = GM_LD(gm_b16_8, ((uint8_t *)&t362));
                                            gm_f_error_s6hn871((void *)&F.l44, (void *)NULL, cx, NULL, t356, t363);
                                            if (gm_unw.kind) {
                                                goto gm_ll339;
                                            }
                                        }
                                        {
                                            void * t364 = (void *)((uint8_t *)&F.l44);
                                            uint64_t t365 = gm_l42;
                                            uint64_t t366 = (uintptr_t)t365;
                                            uint64_t t367 = GM_LD(uintptr_t, (((uint8_t *)&F.l13) + 0) + 8);
                                            if (t366 >= (uintptr_t)(t367)) gm_trap_index(t366, t367, gm_src3k278vcmqgdr, 410, 29);
                                            gm_tl874t2atnlh3 t368 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l13) + 0)) + t366 * 12));
                                            gm_b16_8 t369;
                                            GM_ST(void *, (uint8_t *)&t369, (void *)(gm_sbe7dr7t400bh));
                                            GM_ST(uintptr_t, (uint8_t *)&t369 + 8, (uintptr_t)(15));
                                            gm_f_add_label_50fj7vv(t364, GM_CONV(gm_cseahvg8v7p0vs, t368), t369);
                                        }
                                        {
                                            void * t370 = gm_l0;
                                            gm_b0_1 t371 = gm_f_emit_dph5v1e((void *)NULL, cx, NULL, t370, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l44)));
                                            if (gm_unw.kind) {
                                                goto gm_ll339;
                                            }
                                        }
                                        {
                                            goto lb335;
                                        }
                                        goto ix350;
ie350:;
ix350:;
                                    }
                                    goto me346;
mn347:;
                                    gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 407, 9);
me346:;
                                }
lc335:;
                                {
                                    uint64_t t373 = gm_l41;
                                    uint64_t r374; bool o375 = __builtin_add_overflow(t373, ((uint64_t)1ULL), &r374);
                                    uint64_t t376 = r374;
                                    gm_l41 = t376;
                                }
                            }
gm_ll339:;
                            if (gm_unw.frame == &F.h && gm_unw.label == 5) {
                                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb335; }
                                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc335; }
                            }
                            goto gm_ll290;
lb335:;
                        }
                    }
                    goto me297;
mn298:;
                    gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 403, 5);
me297:;
                }
lc286:;
                {
                    uint64_t t378 = gm_l37;
                    uint64_t r379; bool o380 = __builtin_add_overflow(t378, ((uint64_t)1ULL), &r379);
                    uint64_t t381 = r379;
                    gm_l37 = t381;
                }
            }
gm_ll290:;
            if (gm_unw.frame == &F.h && gm_unw.label == 4) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb286; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc286; }
            }
            goto gm_lad;
lb286:;
        }
    }
    {
        gm_tcmkompbl09il t382;
        memset((uint8_t *)&t382 + 4, 0, 4);
        gm_t88rcucg1b7uk t383 = gm_l35;
        GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t382 + 0, t383);
        gm_b16_8 t384 = GM_LD(gm_b16_8, (((uint8_t *)&F.l12) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t382 + 8, t384);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_ti0kgh3pc3in2 *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_ti0kgh3pc3in2 *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_ti0kgh3pc3in2 *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_ti0kgh3pc3in2 *)gm_out) + 32, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_ti0kgh3pc3in2 *)gm_out) + 40, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_ti0kgh3pc3in2 *)gm_out) + 48, 0);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_ti0kgh3pc3in2 *)gm_out) + 0, ((uint8_t)1ULL));
        GM_ST(gm_tcmkompbl09il, (uint8_t *)&(*(gm_ti0kgh3pc3in2 *)gm_out) + 8, t382);
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
inline __attribute__((aligned(4))) void gm_f_union_variants_a7khaqp(void *gm_out, void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_union_variants_a7khaqp F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    uint32_t gm_l2; /* id */
    gm_t88rcucg1b7uk gm_l3; /* tag */
    gm_b16_8 gm_l4; /* variants */
    GM_STACK_CHECK_SHALLOW(&F, 284u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    {
        void * t3 = gm_l0;
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_t88rcucg1b7uk t5 = gm_l1;
        gm_t53at80pfdus3 t6;
        gm_f_kind_jcnt4hm((void *)&t6, t4, GM_CONV(gm_csguankko4lmhd, t5));
        uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t7 != ((uint8_t)17ULL)) goto ie2;
        uint32_t t8 = GM_LD(uint32_t, ((((uint8_t *)&t6) + 8) + 0));
        gm_l2 = t8;
        {
            uint32_t t11 = gm_l2;
            uint64_t t12 = (uint64_t)t11;
            uint64_t t13 = (uintptr_t)t12;
            void * t14 = gm_l0;
            uint64_t t15 = GM_LD(uintptr_t, ((((uint8_t *)t14) + 1088) + 0) + 8);
            if (t13 >= (uintptr_t)(t15)) gm_trap_index(t13, t15, gm_src3k278vcmqgdr, 510, 30);
            uint8_t t16 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t14) + 1088) + 0)) + t13 * 184) + 24) + 0));
            if (t16 != ((uint8_t)1ULL)) goto ie10;
            gm_t88rcucg1b7uk t17 = GM_LD(gm_t88rcucg1b7uk, (((((uint8_t *)GM_LD(void *, ((((uint8_t *)t14) + 1088) + 0)) + t13 * 184) + 24) + 8) + 0));
            gm_l3 = t17;
            gm_b16_8 t18 = GM_LD(gm_b16_8, (((((uint8_t *)GM_LD(void *, ((((uint8_t *)t14) + 1088) + 0)) + t13 * 184) + 24) + 8) + 8));
            gm_l4 = t18;
            {
                gm_tep2drsl9408b t20;
                memset((uint8_t *)&t20 + 20, 0, 4);
                gm_b16_8 t21 = gm_l4;
                GM_ST(gm_b16_8, (uint8_t *)&t20 + 0, t21);
                gm_t88rcucg1b7uk t22 = gm_l3;
                GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t20 + 16, t22);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tdlpv0q69lov1 *)gm_out) + 25, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tdlpv0q69lov1 *)gm_out) + 26, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tdlpv0q69lov1 *)gm_out) + 28, 0);
                ((uint8_t *)&(*(gm_tdlpv0q69lov1 *)gm_out))[24] = 1;
                GM_ST(gm_tep2drsl9408b, (uint8_t *)&(*(gm_tdlpv0q69lov1 *)gm_out) + 0, t20);
                goto gm_fdone;
            }
            goto ix10;
ie10:;
            {
                gm_tdlpv0q69lov1 t24;
                memset(&t24, 0, sizeof t24);
                uint8_t t25 = 0;
                F.ret = t24;
                goto gm_fret;
            }
ix10:;
        }
        goto ix2;
ie2:;
        {
            gm_tdlpv0q69lov1 t27;
            memset(&t27, 0, sizeof t27);
            uint8_t t28 = 0;
            F.ret = t27;
            goto gm_fret;
        }
ix2:;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_tdlpv0q69lov1 gm_f_union_variants_a7khaqp_c(void * a0, gm_csguankko4lmhd a1) { gm_tdlpv0q69lov1 gm_r; gm_f_union_variants_a7khaqp(&gm_r, a0, a1); return gm_r; }

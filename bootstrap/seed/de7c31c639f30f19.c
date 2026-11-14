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
typedef struct { void * f0; uint64_t f1; uint64_t f2; uint32_t f3; uint32_t f4; } gm_t2i0cb8fcu1gb;
typedef struct { uint64_t f0; uint64_t f1; } gm_t2lqu8jsj511d;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint64_t f0; } gm_t4ce0jd3tdro4;
typedef struct { uint64_t f0; uint64_t f1; uint64_t f2; uint64_t f3; } gm_t66j1im9ip128;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint64_t a[8]; } gm_tl4e4vqe807ge;
typedef struct { void * f0; uint64_t f1; } gm_ttf6i6v1da8cm;
typedef struct { gm_t9qkh0rsooc8d f0; gm_t9qkh0rsooc8d f1; } gm_t0am46iohlpol;
_Static_assert(sizeof(gm_t2i0cb8fcu1gb) == 32 && _Alignof(gm_t2i0cb8fcu1gb) == 8 && offsetof(gm_t2i0cb8fcu1gb, f0) == 0 && offsetof(gm_t2i0cb8fcu1gb, f1) == 8 && offsetof(gm_t2i0cb8fcu1gb, f2) == 16 && offsetof(gm_t2i0cb8fcu1gb, f3) == 24 && offsetof(gm_t2i0cb8fcu1gb, f4) == 28, "layout of `Page`");
_Static_assert(sizeof(gm_t2lqu8jsj511d) == 16 && _Alignof(gm_t2lqu8jsj511d) == 8 && offsetof(gm_t2lqu8jsj511d, f0) == 0 && offsetof(gm_t2lqu8jsj511d, f1) == 8, "layout of `(u64, u64)`");
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
_Static_assert(sizeof(gm_t4ce0jd3tdro4) == 8 && _Alignof(gm_t4ce0jd3tdro4) == 8 && offsetof(gm_t4ce0jd3tdro4, f0) == 0, "layout of `Instant`");
typedef struct { uint64_t f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_t617vr72bd7b3;
_Static_assert(sizeof(gm_t66j1im9ip128) == 32 && _Alignof(gm_t66j1im9ip128) == 8 && offsetof(gm_t66j1im9ip128, f0) == 0 && offsetof(gm_t66j1im9ip128, f1) == 8 && offsetof(gm_t66j1im9ip128, f2) == 16 && offsetof(gm_t66j1im9ip128, f3) == 24, "layout of `(types: usize, values: usize, fns: usize, consts: usize)`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { gm_ttf6i6v1da8cm f0; uint8_t f1; uint8_t pe[7]; } gm_taordiq61jj46;
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
typedef struct { void * f0; uint64_t f1; uint8_t f2; uint8_t f3; uint8_t f4; gm_t9qkh0rsooc8d f5; uint8_t pe[4]; } gm_tbls6nq5r2dk1;
typedef struct { uint64_t f0; gm_b16_8 f1; } gm_tf1bjg81u0glv;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { uint64_t f0; uint64_t f1; gm_t9qkh0rsooc8d f2; uint8_t f3; uint8_t pe[6]; } gm_tilak255t5m34;
_Static_assert(sizeof(gm_tl4e4vqe807ge) == 64 && _Alignof(gm_tl4e4vqe807ge) == 8 && offsetof(gm_tl4e4vqe807ge, a) == 0, "layout of `[8]usize`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; void * v1; } f1; } gm_tq7s98ihonkju;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
_Static_assert(sizeof(gm_ttf6i6v1da8cm) == 16 && _Alignof(gm_ttf6i6v1da8cm) == 8 && offsetof(gm_ttf6i6v1da8cm, f0) == 0 && offsetof(gm_ttf6i6v1da8cm, f1) == 8, "layout of `(slot: ^Slot, base: u64)`");
_Static_assert(sizeof(gm_t0am46iohlpol) == 2 && _Alignof(gm_t0am46iohlpol) == 1 && offsetof(gm_t0am46iohlpol, f0) == 0 && offsetof(gm_t0am46iohlpol, f1) == 1, "layout of `(Command, Command)`");
typedef struct { gm_trqfvmjtt4sij f0; uint64_t f1; uint64_t f2; gm_tl4e4vqe807ge f3; gm_trqfvmjtt4sij f4; uint64_t f5; uint64_t f6; } gm_t4m55nu62fs91;
_Static_assert(sizeof(gm_t617vr72bd7b3) == 16 && _Alignof(gm_t617vr72bd7b3) == 8 && offsetof(gm_t617vr72bd7b3, f0) == 0 && offsetof(gm_t617vr72bd7b3, f1) == 8, "layout of `DirEntry`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_taordiq61jj46) == 24 && _Alignof(gm_taordiq61jj46) == 8 && offsetof(gm_taordiq61jj46, f0) == 0 && offsetof(gm_taordiq61jj46, f1) == 16, "layout of `?(slot: ^Slot, base: u64)`");
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
_Static_assert(sizeof(gm_tbls6nq5r2dk1) == 24 && _Alignof(gm_tbls6nq5r2dk1) == 8 && offsetof(gm_tbls6nq5r2dk1, f0) == 0 && offsetof(gm_tbls6nq5r2dk1, f1) == 8 && offsetof(gm_tbls6nq5r2dk1, f2) == 16 && offsetof(gm_tbls6nq5r2dk1, f3) == 17 && offsetof(gm_tbls6nq5r2dk1, f4) == 18 && offsetof(gm_tbls6nq5r2dk1, f5) == 19, "layout of `Slot`");
_Static_assert(sizeof(gm_tf1bjg81u0glv) == 24 && _Alignof(gm_tf1bjg81u0glv) == 8 && offsetof(gm_tf1bjg81u0glv, f0) == 0 && offsetof(gm_tf1bjg81u0glv, f1) == 8, "layout of `(i: usize, x: string)`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tilak255t5m34) == 24 && _Alignof(gm_tilak255t5m34) == 8 && offsetof(gm_tilak255t5m34, f0) == 0 && offsetof(gm_tilak255t5m34, f1) == 8 && offsetof(gm_tilak255t5m34, f2) == 16 && offsetof(gm_tilak255t5m34, f3) == 17, "layout of `(base: u64, size: u64, kind: AllocKind, live: bool)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t2lqu8jsj511d v0; gm_t617vr72bd7b3 v1; gm_t66j1im9ip128 v2; gm_t2lqu8jsj511d v3; uint64_t v4; gm_t617vr72bd7b3 v5; } f1; } gm_tpekhfs32op0f;
_Static_assert(sizeof(gm_tq7s98ihonkju) == 16 && _Alignof(gm_tq7s98ihonkju) == 8 && offsetof(gm_tq7s98ihonkju, f0) == 0 && offsetof(gm_tq7s98ihonkju, f1) == 8, "layout of `AllocError!^HandlerDecl`");
typedef struct { gm_tilak255t5m34 f0; uint8_t f1; uint8_t pe[7]; } gm_tqvpud5umqqp1;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tpekhfs32op0f v0; uint64_t v1; } f1; } gm_t3j6egd6cfnk5;
_Static_assert(sizeof(gm_t4m55nu62fs91) == 176 && _Alignof(gm_t4m55nu62fs91) == 8 && offsetof(gm_t4m55nu62fs91, f0) == 0 && offsetof(gm_t4m55nu62fs91, f1) == 40 && offsetof(gm_t4m55nu62fs91, f2) == 48 && offsetof(gm_t4m55nu62fs91, f3) == 56 && offsetof(gm_t4m55nu62fs91, f4) == 120 && offsetof(gm_t4m55nu62fs91, f5) == 160 && offsetof(gm_t4m55nu62fs91, f6) == 168, "layout of `Memory`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tpekhfs32op0f v0; gm_b16_8 v1; } f1; } gm_t934k1j7u70ct;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tpekhfs32op0f v0; } f1; } gm_tgfdnufetdnfc;
_Static_assert(sizeof(gm_tpekhfs32op0f) == 40 && _Alignof(gm_tpekhfs32op0f) == 8 && offsetof(gm_tpekhfs32op0f, f0) == 0 && offsetof(gm_tpekhfs32op0f, f1) == 8, "layout of `MemError`");
_Static_assert(sizeof(gm_tqvpud5umqqp1) == 32 && _Alignof(gm_tqvpud5umqqp1) == 8 && offsetof(gm_tqvpud5umqqp1, f0) == 0 && offsetof(gm_tqvpud5umqqp1, f1) == 24, "layout of `?(base: u64, size: u64, kind: AllocKind, live: bool)`");
_Static_assert(sizeof(gm_t3j6egd6cfnk5) == 48 && _Alignof(gm_t3j6egd6cfnk5) == 8 && offsetof(gm_t3j6egd6cfnk5, f0) == 0 && offsetof(gm_t3j6egd6cfnk5, f1) == 8, "layout of `MemError!u64`");
_Static_assert(sizeof(gm_t934k1j7u70ct) == 48 && _Alignof(gm_t934k1j7u70ct) == 8 && offsetof(gm_t934k1j7u70ct, f0) == 0 && offsetof(gm_t934k1j7u70ct, f1) == 8, "layout of `MemError![]u8`");
_Static_assert(sizeof(gm_tgfdnufetdnfc) == 48 && _Alignof(gm_tgfdnufetdnfc) == 8 && offsetof(gm_tgfdnufetdnfc, f0) == 0 && offsetof(gm_tgfdnufetdnfc, f1) == 8, "layout of `MemError!()`");
struct gm_fr_aligned_q6sor4h {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_alloc_bfo8187 {
    gm_frame h;
    gm_t3j6egd6cfnk5 ret;
};
struct gm_fr_copy_from_f7j6p05 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_describe_t62jhnt {
    gm_frame h;
    gm_tqvpud5umqqp1 ret;
};
struct gm_fr_destroy_l0inh8t {
    gm_frame h;
    gm_tb40qn7hk0tl9 l1; /* site */
    gm_b0_1 ret;
};
struct gm_fr_drop_oldest_o9uaetp {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_empty_slice_73v4ttb {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_m3oe7t3 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_fault_u31b92l {
    gm_frame h;
    gm_tpekhfs32op0f ret;
};
struct gm_fr_init_9fuf99v {
    gm_frame h;
    gm_tgfdnufetdnfc ret;
};
struct gm_fr_kill_3kcavfd {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_list_5dhgg4i {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_bcj49nd {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_locate_ciotffk {
    gm_frame h;
    gm_taordiq61jj46 ret;
};
struct gm_fr_mem_deinit_0088958 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_mem_message_e0go901 {
    gm_frame h;
    gm_tpekhfs32op0f l0; /* e */
    gm_b16_8 ret;
};
struct gm_fr_memory_dqom95e {
    gm_frame h;
    gm_t4m55nu62fs91 ret;
};
struct gm_fr_next_pages_p5tqfl1 {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_page_of_artfs32 {
    gm_frame h;
    void * ret;
};
struct gm_fr_push_20a7t0m {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_ucgmlo2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_read_u6r4vns {
    gm_frame h;
    gm_t934k1j7u70ct ret;
};
struct gm_fr_regrow_568t7p1 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_k4j2gj3 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_release_page_66inn9t {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_truncate_ucmmehv {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_us_cuv01fp {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_view_qalkbv1 {
    gm_frame h;
    void * ret;
};
struct gm_fr_write_5gub0ul {
    gm_frame h;
    gm_tgfdnufetdnfc ret;
};
struct gm_fr_write_view_pebit6h {
    gm_frame h;
    gm_t934k1j7u70ct ret;
};
inline bool gm_eqv4kh4qul78(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint8_t gm_f_aligned_q6sor4h(uint64_t a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_alloc_bfo8187(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, uint64_t a2, gm_t9qkh0rsooc8d a3, uint8_t a4);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_copy_from_f7j6p05(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_deinit_d5p9g61(void * a0);
__attribute__((aligned(4))) void gm_f_deinit_u4v62un(void * a0);
__attribute__((aligned(4))) void gm_f_describe_t62jhnt(void *gm_out, void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_tqvpud5umqqp1 gm_f_describe_t62jhnt_c(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_destroy_l0inh8t(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_drop_oldest_o9uaetp(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_73v4ttb(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_m3oe7t3(void);
__attribute__((aligned(4))) void gm_f_fault_u31b92l(void *gm_out, void * a0, uint64_t a1, uint64_t a2, uint64_t a3, uint8_t a4);
__attribute__((aligned(4))) gm_tpekhfs32op0f gm_f_fault_u31b92l_c(void * a0, uint64_t a1, uint64_t a2, uint64_t a3, uint8_t a4);
__attribute__((aligned(4))) void gm_f_format_173gk69(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_bovj9ap(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t2lqu8jsj511d a0);
__attribute__((aligned(4))) void gm_f_format_hvaqp8b(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t2lqu8jsj511d a0);
__attribute__((aligned(4))) void gm_f_format_itetl3f(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_n0787fu(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_o05ssh3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_q07lmui(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_free_oobi9fu(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_free_rkuh2rm(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1);
__attribute__((aligned(4))) void gm_f_init_9fuf99v(void *gm_out, void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tgfdnufetdnfc gm_f_init_9fuf99v_c(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) uint8_t gm_f_kill_3kcavfd(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_list_5dhgg4i(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_bcj49nd(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_locate_ciotffk(void *gm_out, void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_taordiq61jj46 gm_f_locate_ciotffk_c(void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_make_fill_0p8dma0(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0, const void * a1, const void * a2);
__attribute__((aligned(4))) void gm_f_make_fill_jpvm0kc(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0, uint8_t a1, const void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_mem_deinit_0088958(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_mem_message_e0go901(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_memory_dqom95e(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0, uint64_t a1);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_skgqa77(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_next_pages_p5tqfl1(void * a0, uint64_t a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void * gm_f_page_of_artfs32(void * a0, uint64_t a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_20a7t0m(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_ucgmlo2(void * a0, uint64_t a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_read_u6r4vns(void *gm_out, void * a0, uint64_t a1, uint64_t a2, uint64_t a3);
__attribute__((aligned(4))) gm_t934k1j7u70ct gm_f_read_u6r4vns_c(void * a0, uint64_t a1, uint64_t a2, uint64_t a3);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_568t7p1(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_k4j2gj3(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
__attribute__((aligned(4))) uint64_t gm_f_release_page_66inn9t(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_truncate_ucmmehv(void * a0, uint64_t a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_us_cuv01fp(uint64_t a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void * gm_f_view_qalkbv1(void * a0, uint64_t a1, uint64_t a2, uint8_t a3);
__attribute__((aligned(4))) void gm_f_write_5gub0ul(void *gm_out, void * a0, uint64_t a1, gm_b16_8 a2, uint64_t a3);
__attribute__((aligned(4))) gm_tgfdnufetdnfc gm_f_write_5gub0ul_c(void * a0, uint64_t a1, gm_b16_8 a2, uint64_t a3);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_write_view_pebit6h(void *gm_out, void * a0, uint64_t a1, uint64_t a2, uint64_t a3);
__attribute__((aligned(4))) gm_t934k1j7u70ct gm_f_write_view_pebit6h_c(void * a0, uint64_t a1, uint64_t a2, uint64_t a3);
extern uint8_t gm_c0n6vj9dmoi[8] __attribute__((aligned(8)));
extern uint8_t gm_c2ftoibr66a[8] __attribute__((aligned(8)));
extern uint8_t gm_c2t30gdg5i0[8] __attribute__((aligned(8)));
extern uint8_t gm_c6p4chcutca[8] __attribute__((aligned(8)));
extern uint8_t gm_ckasdtpni1c[8] __attribute__((aligned(8)));
extern uint8_t gm_cmml7o4if0q[8] __attribute__((aligned(8)));
extern uint8_t gm_k0ddgrnig17[40] __attribute__((aligned(8)));
extern uint8_t gm_k2afmplo64l[40] __attribute__((aligned(8)));
extern uint8_t gm_k3ho3jie8mm[40] __attribute__((aligned(8)));
extern uint8_t gm_ke9p7o91039[40] __attribute__((aligned(8)));
extern uint8_t gm_keudd4gct2b[40] __attribute__((aligned(8)));
extern uint8_t gm_kgubkej3sov[40] __attribute__((aligned(8)));
extern uint8_t gm_kib6nfi69s7[40] __attribute__((aligned(8)));
extern uint8_t gm_kjq4sbqctak[40] __attribute__((aligned(8)));
extern uint8_t gm_km03kohh9a4[40] __attribute__((aligned(8)));
extern uint8_t gm_koniosmdv9m[40] __attribute__((aligned(8)));
extern uint8_t gm_kpmakm2t9q3[40] __attribute__((aligned(8)));
extern uint8_t gm_krsnqpdcjf4[40] __attribute__((aligned(8)));
extern uint8_t gm_ks0d3nb0eoq[40] __attribute__((aligned(8)));
extern const uint8_t gm_sacs352aeakr8[18];
extern const uint8_t gm_sagd3osht35og[13];
extern const uint8_t gm_sdvkaj73n9goh[24];
extern const uint8_t gm_skt7g4v99o4j0[42];
extern const uint8_t gm_sqofr58g480ri[39];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcc24pnihhgkug[];
extern const char gm_srcq7ohh8h77bn7[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_suklu1i9clb5l[48];
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eqv4kh4qul78(const uint8_t *a, const uint8_t *b) {
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_copy_from_f7j6p05(gm_b16_8 a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_copy_from_f7j6p05 F;
    gm_b16_8 gm_l0; /* s */
    gm_b16_8 gm_l1; /* src */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t2 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t4 = ((t2 == t3));
        if (!t4) {
            gm_b16_8 t5;
            GM_ST(void *, (uint8_t *)&t5, (void *)(gm_sqofr58g480ri));
            GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(38));
            gm_buf mb = {0}; size_t ml = GM_LD(uintptr_t, ((uint8_t *)&t5) + 8); const uint8_t *mp = GM_LD(void *, ((uint8_t *)&t5)); if (ml == 16 && memcmp(mp, "assertion failed", 16) == 0) gm_buf_push(&mb, mp, ml); else { gm_buf_push(&mb, "assertion failed: ", 18); gm_buf_push(&mb, mp, ml); } gm_trap_bytes(mb.data, mb.len, gm_srcc24pnihhgkug, 24, 5);
        }
    }
    {
        gm_b16_8 t6 = gm_l0;
        gm_b16_8 t7 = gm_l1;
        if (GM_LD(uintptr_t, ((uint8_t *)&t6) + 8) != 0) memmove(GM_LD(void *, ((uint8_t *)&t6)), GM_LD(void *, ((uint8_t *)&t7)), GM_LD(uintptr_t, ((uint8_t *)&t6) + 8) * 1);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_destroy_l0inh8t(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_destroy_l0inh8t F;
    void * gm_l0; /* p */
    gm_t3mf58gv9kf3n gm_l2; /* a */
    GM_STACK_CHECK(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_node *pn2 = gm_find(cx, 587034433);
        if (!pn2) gm_trap("no handler installed for effect `Alloc`", gm_src2ah81i3aa8hm, 708, 10);
        gm_t3mf58gv9kf3n t3;
        t3 = ((gm_t3mf58gv9kf3n (*)(void *, gm_node *, gm_node *))pn2->clauses[0])(pn2->env, pn2->hctx, pn2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l2 = t3;
    }
    {
    }
    {
        void * t4 = GM_LD(void *, (((uint8_t *)&gm_l2) + 8));
        void * t5 = GM_LD(void *, (((uint8_t *)t4) + 16));
        void * t6 = GM_LD(void *, (((uint8_t *)&gm_l2) + 0));
        void * t7 = gm_l0;
        void * t8 = t7;
        void * t9 = t5;
        gm_b0_1 t10;
        ((void (*)(void *, void *, uint64_t, uint64_t))GM_PTR_UNTAG(t9))(t6, t8, ((uint64_t)24ULL), ((uint64_t)8ULL));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_73v4ttb(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_73v4ttb F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_m3oe7t3(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_m3oe7t3 F;
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
inline __attribute__((aligned(4))) void gm_f_list_5dhgg4i(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_5dhgg4i F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_73v4ttb();
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
inline __attribute__((aligned(4))) void gm_f_list_bcj49nd(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_bcj49nd F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_m3oe7t3();
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline void * gm_f_page_of_artfs32(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_page_of_artfs32 F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* addr */
    uint64_t gm_l2; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 171u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t2 = gm_l1;
        void * t3 = gm_l0;
        uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t3) + 40));
        uint64_t r5; bool o6 = __builtin_sub_overflow(t2, t4, &r5);
        uint64_t t7 = r5;
        uint64_t t8 = GM_LD(uint64_t, gm_c6p4chcutca);
        unsigned __int128 n9 = (unsigned __int128)t8;
        if (n9 >= 64) gm_trap_shift((uint64_t)n9, "u64", gm_srcq7ohh8h77bn7, 178, 10);
        uint64_t t10 = (uint64_t)(t7 >> (int)n9);
        gm_l2 = t10;
    }
    {
        uint64_t t12 = gm_l2;
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
        uint64_t t22 = gm_l2;
        uint64_t t23 = gm_f_us_cuv01fp(t22);
        uint64_t t24 = (uintptr_t)t23;
        void * t25 = (void *)((uint8_t *)t21 + t24 * 32);
        uint8_t t26 = 0;
        F.ret = t25;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_20a7t0m(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_20a7t0m F;
    void * gm_l0; /* l */
    gm_t2i0cb8fcu1gb gm_l1; /* x */
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
            gm_b16_8 t11 = gm_f_empty_slice_m3oe7t3();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_568t7p1(t10, ((uint64_t)1ULL), t12);
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
        gm_t2i0cb8fcu1gb t21 = GM_LD(gm_t2i0cb8fcu1gb, ((uint8_t *)a1));
        GM_ST(gm_t2i0cb8fcu1gb, ((uint8_t *)t20 + t18 * 32), t21);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_ucgmlo2(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_ucgmlo2 F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* x */
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
            gm_b16_8 t11 = gm_f_empty_slice_73v4ttb();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_k4j2gj3(t10, ((uint64_t)1ULL), t12);
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
        uint64_t t21 = gm_l1;
        GM_ST(uint64_t, ((uint8_t *)t20 + t18 * 8), t21);
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_568t7p1(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_568t7p1 F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_k4j2gj3(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_k4j2gj3 F;
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
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)8ULL), ((uint64_t)8ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_truncate_ucmmehv(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_truncate_ucmmehv F;
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
            GM_ST(void *, (uint8_t *)&t15, (void *)(((uint8_t *)t11 + t12 * 8)));
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
extern __attribute__((aligned(4))) uint8_t gm_f_aligned_q6sor4h(uint64_t a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_alloc_bfo8187(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, uint64_t a2, gm_t9qkh0rsooc8d a3, uint8_t a4) {
    struct gm_fr_alloc_bfo8187 F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* size */
    uint64_t gm_l2; /* align */
    gm_t9qkh0rsooc8d gm_l3; /* kind */
    uint8_t gm_l4; /* mutable */
    gm_b16_8 gm_l5; /* data */
    gm_tbls6nq5r2dk1 gm_l6; /* slot */
    uint64_t gm_l7; /* need */
    uint64_t gm_l8; /* shift */
    uint64_t gm_l9; /* c */
    uint32_t gm_l10; /* per */
    uint64_t gm_l11; /* addr */
    gm_b16_8 gm_l12; /* slots */
    void * gm_l13; /* pg */
    uint32_t gm_l14; /* i */
    void * gm_l15; /* ss */
    uint64_t gm_l16; /* n */
    uint64_t gm_l17; /* addr */
    void * gm_l18; /* one */
    uint64_t gm_l19; /* <tmp> */
    uint64_t gm_l20; /* <tmp> */
    GM_STACK_CHECK(&F, 2068u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        uint64_t t3 = gm_l1;
        unsigned __int128 n4 = (unsigned __int128)((uint64_t)1ULL);
        if (n4 >= 64) gm_trap_shift((uint64_t)n4, "usize", gm_srcq7ohh8h77bn7, 125, 19);
        uint64_t t5 = (uint64_t)(((uint64_t)18446744073709551615ULL) >> (int)n4);
        uint64_t t6 = (uint64_t)t5;
        uint8_t t7 = (t3 > t6);
        if (!t7) goto ie2;
        {
            uint64_t t9 = gm_l1;
            gm_t9qkh0rsooc8d t10 = gm_l3;
            gm_t617vr72bd7b3 t11;
            GM_ST(uint8_t, (uint8_t *)&t11 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t11 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t11 + 12, 0);
            GM_ST(uint64_t, ((uint8_t *)&t11 + 0), t9);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t11 + 8), t10);
            gm_tpekhfs32op0f t12;
            GM_ST(uint8_t, (uint8_t *)&t12 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t12 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t12 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t12 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t12 + 32, 0);
            GM_ST(uint8_t, ((uint8_t *)&t12 + 0), ((uint8_t)7ULL));
            GM_ST(gm_t617vr72bd7b3, ((uint8_t *)&t12 + 8), t11);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t3j6egd6cfnk5 *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t3j6egd6cfnk5 *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t3j6egd6cfnk5 *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_t3j6egd6cfnk5 *)gm_out))[0] = 1;
            GM_ST(gm_tpekhfs32op0f, (uint8_t *)&(*(gm_t3j6egd6cfnk5 *)gm_out) + 8, t12);
            goto gm_fdone;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_b16_8 t13 = gm_f_empty_slice_ijtp9lo();
        gm_l5 = t13;
    }
    {
        uint64_t t15 = gm_l1;
        uint8_t t16 = (t15 > ((uint64_t)0ULL));
        if (!t16) goto ie14;
        {
            uint64_t t18 = gm_l1;
            uint64_t t19 = gm_f_us_cuv01fp(t18);
            gm_tabb710d9mq2e t20;
            gm_f_make_fill_jpvm0kc((void *)&t20, (void *)NULL, cx, NULL, t19, ((uint8_t)0ULL), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kib6nfi69s7)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t21;
            if (!((((uint8_t *)&t20))[0] != 0)) {
                gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&t20) + 8));
                t21 = t22;
            } else {
                uint64_t t23 = gm_l1;
                gm_t9qkh0rsooc8d t24 = gm_l3;
                gm_t617vr72bd7b3 t25;
                GM_ST(uint8_t, (uint8_t *)&t25 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t25 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t25 + 12, 0);
                GM_ST(uint64_t, ((uint8_t *)&t25 + 0), t23);
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t25 + 8), t24);
                gm_tpekhfs32op0f t26;
                GM_ST(uint8_t, (uint8_t *)&t26 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t26 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t26 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t26 + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&t26 + 32, 0);
                GM_ST(uint8_t, ((uint8_t *)&t26 + 0), ((uint8_t)7ULL));
                GM_ST(gm_t617vr72bd7b3, ((uint8_t *)&t26 + 8), t25);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t3j6egd6cfnk5 *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t3j6egd6cfnk5 *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t3j6egd6cfnk5 *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_t3j6egd6cfnk5 *)gm_out))[0] = 1;
                GM_ST(gm_tpekhfs32op0f, (uint8_t *)&(*(gm_t3j6egd6cfnk5 *)gm_out) + 8, t26);
                goto gm_fdone;
            }
            gm_l5 = t21;
        }
        goto ix14;
ie14:;
ix14:;
    }
    {
        memset((uint8_t *)&gm_l6 + 20, 0, 4);
        void * t27 = GM_LD(void *, ((uint8_t *)&gm_l5));
        GM_ST(void *, (uint8_t *)&gm_l6 + 0, t27);
        uint64_t t28 = gm_l1;
        GM_ST(uint64_t, (uint8_t *)&gm_l6 + 8, t28);
        GM_ST(uint8_t, (uint8_t *)&gm_l6 + 16, ((uint8_t)1));
        GM_ST(uint8_t, (uint8_t *)&gm_l6 + 17, ((uint8_t)1));
        uint8_t t29 = gm_l4;
        GM_ST(uint8_t, (uint8_t *)&gm_l6 + 18, t29);
        gm_t9qkh0rsooc8d t30 = gm_l3;
        GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&gm_l6 + 19, t30);
    }
    {
        uint64_t t31 = gm_l1;
        uint64_t r32; bool o33 = __builtin_add_overflow(t31, ((uint64_t)1ULL), &r32);
        if (o33) gm_trap("integer overflow in `+`", gm_srcq7ohh8h77bn7, 130, 14);
        uint64_t t34 = r32;
        uint64_t t35 = gm_l2;
        uint64_t t36 = (t34 > t35 ? t34 : t35);
        gm_l7 = t36;
    }
    {
        uint64_t t38 = gm_l7;
        uint64_t t39 = GM_LD(uint64_t, gm_c0n6vj9dmoi);
        unsigned __int128 n40 = (unsigned __int128)t39;
        if (n40 >= 64) gm_trap_shift((uint64_t)n40, "u64", gm_srcq7ohh8h77bn7, 131, 17);
        uint64_t r41 = (uint64_t)((uint64_t)((uint64_t)1ULL) << (int)n40);
        bool o42 = (uint64_t)(r41 >> (int)n40) != ((uint64_t)1ULL);
        if (o42) gm_trap("integer overflow in `<<`", gm_srcq7ohh8h77bn7, 131, 17);
        uint64_t t43 = r41;
        uint8_t t44 = (t38 <= t43);
        if (!t44) goto ie37;
        {
            uint64_t t46 = GM_LD(uint64_t, gm_cmml7o4if0q);
            gm_l8 = t46;
        }
        {
            for (;;) {
                uint64_t t48 = gm_l8;
                unsigned __int128 n49 = (unsigned __int128)t48;
                if (n49 >= 64) gm_trap_shift((uint64_t)n49, "u64", gm_srcq7ohh8h77bn7, 133, 16);
                uint64_t r50 = (uint64_t)((uint64_t)((uint64_t)1ULL) << (int)n49);
                bool o51 = (uint64_t)(r50 >> (int)n49) != ((uint64_t)1ULL);
                if (o51) gm_trap("integer overflow in `<<`", gm_srcq7ohh8h77bn7, 133, 16);
                uint64_t t52 = r50;
                uint64_t t53 = gm_l7;
                uint8_t t54 = (t52 < t53);
                if (!t54) goto lb47;
                {
                    uint64_t t57 = gm_l8;
                    uint64_t r58; bool o59 = __builtin_add_overflow(t57, ((uint64_t)1ULL), &r58);
                    if (o59) gm_trap("integer overflow in `+`", gm_srcq7ohh8h77bn7, 133, 42);
                    uint64_t t60 = r58;
                    gm_l8 = t60;
                }
lc47:;
            }
lb47:;
        }
        {
            uint64_t t61 = gm_l8;
            uint64_t t62 = GM_LD(uint64_t, gm_cmml7o4if0q);
            uint64_t r63; bool o64 = __builtin_sub_overflow(t61, t62, &r63);
            if (o64) gm_trap("integer overflow in `-`", gm_srcq7ohh8h77bn7, 134, 17);
            uint64_t t65 = r63;
            uint64_t t66 = gm_f_us_cuv01fp(t65);
            gm_l9 = t66;
        }
        {
            uint64_t t67 = GM_LD(uint64_t, gm_ckasdtpni1c);
            uint64_t t68 = gm_l8;
            unsigned __int128 n69 = (unsigned __int128)t68;
            if (n69 >= 64) gm_trap_shift((uint64_t)n69, "u64", gm_srcq7ohh8h77bn7, 135, 17);
            uint64_t t70 = (uint64_t)(t67 >> (int)n69);
            uint32_t t71 = (uint32_t)t70;
            gm_l10 = t71;
        }
        {
            uint8_t t73 = 0;
            uint64_t t74 = gm_l9;
            uint64_t t75 = (uintptr_t)t74;
            void * t76 = gm_l0;
            if (t75 >= (uintptr_t)(8)) gm_trap_index(t75, 8, gm_srcq7ohh8h77bn7, 136, 12);
            uint64_t t77 = GM_LD(uint64_t, ((((uint8_t *)t76) + 56) + t75 * 8));
            uint8_t t78 = ((t77 == ((uint64_t)0ULL)));
            if (!t78) {
                uint64_t t79 = gm_l9;
                uint64_t t80 = (uintptr_t)t79;
                void * t81 = gm_l0;
                if (t80 >= (uintptr_t)(8)) gm_trap_index(t80, 8, gm_srcq7ohh8h77bn7, 136, 44);
                uint64_t t82 = GM_LD(uint64_t, ((((uint8_t *)t81) + 56) + t80 * 8));
                uint64_t r83; bool o84 = __builtin_sub_overflow(t82, ((uint64_t)1ULL), &r83);
                if (o84) gm_trap("integer overflow in `-`", gm_srcq7ohh8h77bn7, 136, 44);
                uint64_t t85 = r83;
                uint64_t t86 = (uintptr_t)t85;
                void * t87 = gm_l0;
                uint64_t t88 = GM_LD(uintptr_t, ((((uint8_t *)t87) + 0) + 0) + 8);
                if (t86 >= (uintptr_t)(t88)) gm_trap_index(t86, t88, gm_srcq7ohh8h77bn7, 136, 30);
                uint32_t t89 = GM_LD(uint32_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t87) + 0) + 0)) + t86 * 32) + 24));
                uint32_t t90 = gm_l10;
                uint8_t t91 = ((t89 == t90));
                t73 = t91;
            } else t73 = 1;
            if (!t73) goto ie72;
            {
                void * t93 = gm_l0;
                gm_tf5srh4mp4b3i t94 = gm_f_next_pages_p5tqfl1(t93, ((uint64_t)1ULL));
                uint64_t t95;
                if (((((uint8_t *)&t94))[8] != 0)) {
                    uint64_t t96 = GM_LD(uint64_t, (((uint8_t *)&t94) + 0));
                    t95 = t96;
                } else {
                    {
                        gm_b16_8 t98 = gm_l5;
                        gm_b16_8 t99 = GM_LD(gm_b16_8, ((uint8_t *)&t98));
                        gm_b0_1 t100 = gm_f_free_rkuh2rm((void *)NULL, cx, NULL, t99, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_koniosmdv9m)));
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    gm_tpekhfs32op0f t101;
                    memset(&t101, 0, sizeof t101);
                    GM_ST(uint8_t, ((uint8_t *)&t101 + 0), ((uint8_t)6ULL));
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t3j6egd6cfnk5 *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t3j6egd6cfnk5 *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t3j6egd6cfnk5 *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_t3j6egd6cfnk5 *)gm_out))[0] = 1;
                    GM_ST(gm_tpekhfs32op0f, (uint8_t *)&(*(gm_t3j6egd6cfnk5 *)gm_out) + 8, t101);
                    goto gm_fdone;
                }
                gm_l11 = t95;
            }
            {
                uint32_t t102 = gm_l10;
                uint64_t t103 = (uint64_t)t102;
                gm_tbls6nq5r2dk1 t104;
                memset((uint8_t *)&t104 + 20, 0, 4);
                void * t105 = GM_LD(void *, ((uint8_t *)&gm_l5));
                GM_ST(void *, (uint8_t *)&t104 + 0, t105);
                GM_ST(uint64_t, (uint8_t *)&t104 + 8, ((uint64_t)0ULL));
                GM_ST(uint8_t, (uint8_t *)&t104 + 16, ((uint8_t)0));
                GM_ST(uint8_t, (uint8_t *)&t104 + 17, ((uint8_t)0));
                GM_ST(uint8_t, (uint8_t *)&t104 + 18, ((uint8_t)0));
                gm_t9qkh0rsooc8d t106;
                GM_ST(uint8_t, ((uint8_t *)&t106 + 0), ((uint8_t)2ULL));
                GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t104 + 19, t106);
                gm_tabb710d9mq2e t107;
                gm_f_make_fill_0p8dma0((void *)&t107, (void *)NULL, cx, NULL, t103, &t104, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kjq4sbqctak)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_b16_8 t108;
                if (!((((uint8_t *)&t107))[0] != 0)) {
                    gm_b16_8 t109 = GM_LD(gm_b16_8, (((uint8_t *)&t107) + 8));
                    t108 = t109;
                } else {
                    {
                        gm_b16_8 t111 = gm_l5;
                        gm_b16_8 t112 = GM_LD(gm_b16_8, ((uint8_t *)&t111));
                        gm_b0_1 t113 = gm_f_free_rkuh2rm((void *)NULL, cx, NULL, t112, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kpmakm2t9q3)));
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint64_t t114 = gm_l1;
                    gm_t9qkh0rsooc8d t115 = gm_l3;
                    gm_t617vr72bd7b3 t116;
                    GM_ST(uint8_t, (uint8_t *)&t116 + 9, 0);
                    GM_ST(uint16_t, (uint8_t *)&t116 + 10, 0);
                    GM_ST(uint32_t, (uint8_t *)&t116 + 12, 0);
                    GM_ST(uint64_t, ((uint8_t *)&t116 + 0), t114);
                    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t116 + 8), t115);
                    gm_tpekhfs32op0f t117;
                    GM_ST(uint8_t, (uint8_t *)&t117 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t117 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t117 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t117 + 24, 0);
                    GM_ST(uint64_t, (uint8_t *)&t117 + 32, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t117 + 0), ((uint8_t)7ULL));
                    GM_ST(gm_t617vr72bd7b3, ((uint8_t *)&t117 + 8), t116);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t3j6egd6cfnk5 *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t3j6egd6cfnk5 *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t3j6egd6cfnk5 *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_t3j6egd6cfnk5 *)gm_out))[0] = 1;
                    GM_ST(gm_tpekhfs32op0f, (uint8_t *)&(*(gm_t3j6egd6cfnk5 *)gm_out) + 8, t117);
                    goto gm_fdone;
                }
                gm_l12 = t108;
            }
            {
                void * t118 = gm_l0;
                void * t119 = (void *)(((uint8_t *)t118) + 0);
                gm_t2i0cb8fcu1gb t120;
                void * t121 = GM_LD(void *, ((uint8_t *)&gm_l12));
                GM_ST(void *, (uint8_t *)&t120 + 0, t121);
                uint64_t t122 = gm_l11;
                GM_ST(uint64_t, (uint8_t *)&t120 + 8, t122);
                uint64_t t123 = gm_l8;
                GM_ST(uint64_t, (uint8_t *)&t120 + 16, t123);
                GM_ST(uint32_t, (uint8_t *)&t120 + 24, ((uint32_t)0ULL));
                GM_ST(uint32_t, (uint8_t *)&t120 + 28, ((uint32_t)0ULL));
                gm_tftb1odc739t5 t124 = gm_f_push_20a7t0m(t119, &t120);
                if (((((uint8_t *)&t124))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t124) + 1, gm_srcq7ohh8h77bn7, 145, 13);
                gm_b0_1 t125 = GM_LD(gm_b0_1, (((uint8_t *)&t124) + 1));
            }
            {
                uint64_t t126 = gm_l9;
                uint64_t t127 = (uintptr_t)t126;
                void * t128 = gm_l0;
                if (t127 >= (uintptr_t)(8)) gm_trap_index(t127, 8, gm_srcq7ohh8h77bn7, 146, 13);
                void * t129 = gm_l0;
                uint64_t t130 = GM_LD(uintptr_t, ((((uint8_t *)t129) + 0) + 0) + 8);
                GM_ST(uint64_t, ((((uint8_t *)t128) + 56) + t127 * 8), t130);
            }
            goto ix72;
ie72:;
ix72:;
        }
        {
            uint64_t t131 = gm_l9;
            uint64_t t132 = (uintptr_t)t131;
            void * t133 = gm_l0;
            if (t132 >= (uintptr_t)(8)) gm_trap_index(t132, 8, gm_srcq7ohh8h77bn7, 148, 30);
            uint64_t t134 = GM_LD(uint64_t, ((((uint8_t *)t133) + 56) + t132 * 8));
            uint64_t r135; bool o136 = __builtin_sub_overflow(t134, ((uint64_t)1ULL), &r135);
            if (o136) gm_trap("integer overflow in `-`", gm_srcq7ohh8h77bn7, 148, 30);
            uint64_t t137 = r135;
            uint64_t t138 = (uintptr_t)t137;
            void * t139 = gm_l0;
            uint64_t t140 = GM_LD(uintptr_t, ((((uint8_t *)t139) + 0) + 0) + 8);
            if (t138 >= (uintptr_t)(t140)) gm_trap_index(t138, t140, gm_srcq7ohh8h77bn7, 148, 16);
            void * t141 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t139) + 0) + 0)) + t138 * 32);
            gm_l13 = t141;
        }
        {
            void * t142 = gm_l13;
            uint32_t t143 = GM_LD(uint32_t, (((uint8_t *)t142) + 24));
            gm_l14 = t143;
        }
        {
            void * t144 = gm_l13;
            void * t145 = gm_l13;
            uint32_t t146 = GM_LD(uint32_t, (((uint8_t *)t145) + 24));
            uint32_t r147; bool o148 = __builtin_add_overflow(t146, ((uint32_t)1ULL), &r147);
            if (o148) gm_trap("integer overflow in `+`", gm_srcq7ohh8h77bn7, 150, 9);
            uint32_t t149 = r147;
            GM_ST(uint32_t, (((uint8_t *)t144) + 24), t149);
        }
        {
            void * t151 = gm_l13;
            void * t152 = GM_LD(void *, (((uint8_t *)t151) + 0));
            if (!(GM_LD(void *, ((uint8_t *)&t152)) != 0)) goto ie150;
            void * t153 = GM_LD(void *, ((uint8_t *)&t152));
            gm_l15 = t153;
            {
                uint32_t t155 = gm_l14;
                uint64_t t156 = (uint64_t)t155;
                uint64_t t157 = (uintptr_t)t156;
                void * t158 = gm_l15;
                gm_tbls6nq5r2dk1 t159 = gm_l6;
                GM_ST(gm_tbls6nq5r2dk1, ((uint8_t *)t158 + t157 * 24), t159);
            }
            goto ix150;
ie150:;
ix150:;
        }
        {
            void * t160 = gm_l13;
            uint64_t t161 = GM_LD(uint64_t, (((uint8_t *)t160) + 8));
            uint32_t t162 = gm_l14;
            uint64_t t163 = (uint64_t)t162;
            uint64_t t164 = gm_l8;
            unsigned __int128 n165 = (unsigned __int128)t164;
            if (n165 >= 64) gm_trap_shift((uint64_t)n165, "u64", gm_srcq7ohh8h77bn7, 152, 27);
            uint64_t r166 = (uint64_t)((uint64_t)t163 << (int)n165);
            bool o167 = (uint64_t)(r166 >> (int)n165) != t163;
            if (o167) gm_trap("integer overflow in `<<`", gm_srcq7ohh8h77bn7, 152, 27);
            uint64_t t168 = r166;
            uint64_t r169; bool o170 = __builtin_add_overflow(t161, t168, &r169);
            if (o170) gm_trap("integer overflow in `+`", gm_srcq7ohh8h77bn7, 152, 16);
            uint64_t t171 = r169;
            memset(&(*(gm_t3j6egd6cfnk5 *)gm_out), 0, sizeof (*(gm_t3j6egd6cfnk5 *)gm_out));
            ((uint8_t *)&(*(gm_t3j6egd6cfnk5 *)gm_out))[0] = 0;
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t3j6egd6cfnk5 *)gm_out) + 8, t171);
            goto gm_fdone;
        }
        goto ix37;
ie37:;
ix37:;
    }
    {
        for (;;) {
            void * t173 = gm_l0;
            uint64_t t174 = GM_LD(uint64_t, (((uint8_t *)t173) + 40));
            void * t175 = gm_l0;
            uint64_t t176 = GM_LD(uintptr_t, ((((uint8_t *)t175) + 0) + 0) + 8);
            uint64_t t177 = (uint64_t)t176;
            uint64_t t178 = GM_LD(uint64_t, gm_ckasdtpni1c);
            uint64_t r179; bool o180 = __builtin_mul_overflow(t177, t178, &r179);
            if (o180) gm_trap("integer overflow in `*`", gm_srcq7ohh8h77bn7, 155, 21);
            uint64_t t181 = r179;
            uint64_t r182; bool o183 = __builtin_add_overflow(t174, t181, &r182);
            if (o183) gm_trap("integer overflow in `+`", gm_srcq7ohh8h77bn7, 155, 12);
            uint64_t t184 = r182;
            uint64_t t185 = gm_l2;
            uint64_t t186 = (t185 > ((uint64_t)1ULL) ? t185 : ((uint64_t)1ULL));
            if (t186 == 0) gm_trap("division by zero", gm_srcq7ohh8h77bn7, 155, 11);
            uint64_t t187 = (uint64_t)(t184 % t186);
            uint8_t t188 = (!(t187 == ((uint64_t)0ULL)));
            if (!t188) goto lb172;
            {
                {
                    void * t192 = gm_l0;
                    gm_tf5srh4mp4b3i t193 = gm_f_next_pages_p5tqfl1(t192, ((uint64_t)1ULL));
                    uint64_t t194;
                    if (((((uint8_t *)&t193))[8] != 0)) {
                        uint64_t t195 = GM_LD(uint64_t, (((uint8_t *)&t193) + 0));
                        t194 = t195;
                    } else {
                        {
                            gm_b16_8 t197 = gm_l5;
                            gm_b16_8 t198 = GM_LD(gm_b16_8, ((uint8_t *)&t197));
                            gm_b0_1 t199 = gm_f_free_rkuh2rm((void *)NULL, cx, NULL, t198, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k3ho3jie8mm)));
                            if (gm_unw.kind) {
                                goto gm_ll189;
                            }
                        }
                        gm_tpekhfs32op0f t200;
                        memset(&t200, 0, sizeof t200);
                        GM_ST(uint8_t, ((uint8_t *)&t200 + 0), ((uint8_t)6ULL));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t3j6egd6cfnk5 *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_t3j6egd6cfnk5 *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_t3j6egd6cfnk5 *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_t3j6egd6cfnk5 *)gm_out))[0] = 1;
                        GM_ST(gm_tpekhfs32op0f, (uint8_t *)&(*(gm_t3j6egd6cfnk5 *)gm_out) + 8, t200);
                        goto gm_fdone;
                    }
                }
            }
            {
                void * t201 = gm_l0;
                void * t202 = (void *)(((uint8_t *)t201) + 0);
                gm_t2i0cb8fcu1gb t203;
                GM_ST(void *, (uint8_t *)&t203 + 0, ((void *)0));
                GM_ST(uint64_t, (uint8_t *)&t203 + 8, ((uint64_t)0ULL));
                uint64_t t204 = GM_LD(uint64_t, gm_c2t30gdg5i0);
                GM_ST(uint64_t, (uint8_t *)&t203 + 16, t204);
                GM_ST(uint32_t, (uint8_t *)&t203 + 24, ((uint32_t)0ULL));
                GM_ST(uint32_t, (uint8_t *)&t203 + 28, ((uint32_t)0ULL));
                gm_tftb1odc739t5 t205 = gm_f_push_20a7t0m(t202, &t203);
                if (((((uint8_t *)&t205))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t205) + 1, gm_srcq7ohh8h77bn7, 160, 9);
                gm_b0_1 t206 = GM_LD(gm_b0_1, (((uint8_t *)&t205) + 1));
            }
lc172:;
        }
gm_ll189:;
        if (gm_unw.frame == &F.h && gm_unw.label == 1) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb172; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc172; }
        }
        goto gm_lad;
lb172:;
    }
    {
        uint64_t t207 = gm_l1;
        uint64_t t208 = GM_LD(uint64_t, gm_ckasdtpni1c);
        if (t208 == 0) gm_trap("division by zero", gm_srcq7ohh8h77bn7, 162, 10);
        uint64_t t209 = (uint64_t)(t207 / t208);
        uint64_t r210; bool o211 = __builtin_add_overflow(t209, ((uint64_t)1ULL), &r210);
        if (o211) gm_trap("integer overflow in `+`", gm_srcq7ohh8h77bn7, 162, 10);
        uint64_t t212 = r210;
        gm_l16 = t212;
    }
    {
        void * t213 = gm_l0;
        uint64_t t214 = gm_l16;
        gm_tf5srh4mp4b3i t215 = gm_f_next_pages_p5tqfl1(t213, t214);
        uint64_t t216;
        if (((((uint8_t *)&t215))[8] != 0)) {
            uint64_t t217 = GM_LD(uint64_t, (((uint8_t *)&t215) + 0));
            t216 = t217;
        } else {
            {
                gm_b16_8 t219 = gm_l5;
                gm_b16_8 t220 = GM_LD(gm_b16_8, ((uint8_t *)&t219));
                gm_b0_1 t221 = gm_f_free_rkuh2rm((void *)NULL, cx, NULL, t220, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_krsnqpdcjf4)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_tpekhfs32op0f t222;
            memset(&t222, 0, sizeof t222);
            GM_ST(uint8_t, ((uint8_t *)&t222 + 0), ((uint8_t)6ULL));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t3j6egd6cfnk5 *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t3j6egd6cfnk5 *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t3j6egd6cfnk5 *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_t3j6egd6cfnk5 *)gm_out))[0] = 1;
            GM_ST(gm_tpekhfs32op0f, (uint8_t *)&(*(gm_t3j6egd6cfnk5 *)gm_out) + 8, t222);
            goto gm_fdone;
        }
        gm_l17 = t216;
    }
    {
        gm_tq7s98ihonkju t223 = gm_f_new_skgqa77((void *)NULL, cx, NULL, &(*(gm_tbls6nq5r2dk1 *)((uint8_t *)&gm_l6)), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_keudd4gct2b)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t224;
        if (!((((uint8_t *)&t223))[0] != 0)) {
            void * t225 = GM_LD(void *, (((uint8_t *)&t223) + 8));
            t224 = t225;
        } else {
            {
                gm_b16_8 t227 = gm_l5;
                gm_b16_8 t228 = GM_LD(gm_b16_8, ((uint8_t *)&t227));
                gm_b0_1 t229 = gm_f_free_rkuh2rm((void *)NULL, cx, NULL, t228, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_km03kohh9a4)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            uint64_t t230 = gm_l1;
            gm_t9qkh0rsooc8d t231 = gm_l3;
            gm_t617vr72bd7b3 t232;
            GM_ST(uint8_t, (uint8_t *)&t232 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t232 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t232 + 12, 0);
            GM_ST(uint64_t, ((uint8_t *)&t232 + 0), t230);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t232 + 8), t231);
            gm_tpekhfs32op0f t233;
            GM_ST(uint8_t, (uint8_t *)&t233 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t233 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t233 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t233 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t233 + 32, 0);
            GM_ST(uint8_t, ((uint8_t *)&t233 + 0), ((uint8_t)7ULL));
            GM_ST(gm_t617vr72bd7b3, ((uint8_t *)&t233 + 8), t232);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t3j6egd6cfnk5 *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t3j6egd6cfnk5 *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t3j6egd6cfnk5 *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_t3j6egd6cfnk5 *)gm_out))[0] = 1;
            GM_ST(gm_tpekhfs32op0f, (uint8_t *)&(*(gm_t3j6egd6cfnk5 *)gm_out) + 8, t233);
            goto gm_fdone;
        }
        gm_l18 = t224;
    }
    {
        {
            gm_l20 = ((uint64_t)0ULL);
        }
        {
            uint64_t t235 = gm_l16;
            uint64_t t236 = gm_f_us_cuv01fp(t235);
            gm_l19 = t236;
        }
        {
            for (;;) {
                uint64_t t238 = gm_l20;
                uint64_t t239 = gm_l19;
                uint8_t t240 = (t238 < t239);
                if (!t240) goto lb237;
                {
                    uint64_t t243 = gm_l20;
                    {
                        void * t247 = gm_l0;
                        void * t248 = (void *)(((uint8_t *)t247) + 0);
                        gm_t2i0cb8fcu1gb t249;
                        void * t250 = gm_l18;
                        void * t251 = t250;
                        GM_ST(void *, (uint8_t *)&t249 + 0, t251);
                        uint64_t t252 = gm_l17;
                        GM_ST(uint64_t, (uint8_t *)&t249 + 8, t252);
                        uint64_t t253 = GM_LD(uint64_t, gm_c2t30gdg5i0);
                        GM_ST(uint64_t, (uint8_t *)&t249 + 16, t253);
                        GM_ST(uint32_t, (uint8_t *)&t249 + 24, ((uint32_t)1ULL));
                        GM_ST(uint32_t, (uint8_t *)&t249 + 28, ((uint32_t)0ULL));
                        gm_tftb1odc739t5 t254 = gm_f_push_20a7t0m(t248, &t249);
                        if (((((uint8_t *)&t254))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t254) + 1, gm_srcq7ohh8h77bn7, 171, 25);
                        gm_b0_1 t255 = GM_LD(gm_b0_1, (((uint8_t *)&t254) + 1));
                    }
                    goto me244;
mn245:;
                    gm_trap("no match arm matched the scrutinee", gm_srcq7ohh8h77bn7, 171, 5);
me244:;
                }
lc237:;
                {
                    uint64_t t257 = gm_l20;
                    uint64_t r258; bool o259 = __builtin_add_overflow(t257, ((uint64_t)1ULL), &r258);
                    uint64_t t260 = r258;
                    gm_l20 = t260;
                }
            }
lb237:;
        }
    }
    {
        uint64_t t261 = gm_l17;
        memset(&(*(gm_t3j6egd6cfnk5 *)gm_out), 0, sizeof (*(gm_t3j6egd6cfnk5 *)gm_out));
        ((uint8_t *)&(*(gm_t3j6egd6cfnk5 *)gm_out))[0] = 0;
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t3j6egd6cfnk5 *)gm_out) + 8, t261);
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
inline __attribute__((aligned(4))) void gm_f_describe_t62jhnt(void *gm_out, void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_describe_t62jhnt F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* addr */
    gm_ttf6i6v1da8cm gm_l2; /* at */
    GM_STACK_CHECK_SHALLOW(&F, 203u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        uint64_t t3 = gm_l1;
        gm_taordiq61jj46 t4;
        gm_f_locate_ciotffk((void *)&t4, t2, t3);
        gm_ttf6i6v1da8cm t5;
        if (((((uint8_t *)&t4))[16] != 0)) {
            gm_ttf6i6v1da8cm t6 = GM_LD(gm_ttf6i6v1da8cm, (((uint8_t *)&t4) + 0));
            t5 = t6;
        } else {
            gm_tqvpud5umqqp1 t7;
            memset(&t7, 0, sizeof t7);
            uint8_t t8 = 0;
            F.ret = t7;
            goto gm_fret;
        }
        gm_l2 = t5;
    }
    {
        gm_tilak255t5m34 t9;
        memset((uint8_t *)&t9 + 18, 0, 6);
        uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
        GM_ST(uint64_t, (uint8_t *)&t9 + 0, t10);
        void * t11 = GM_LD(void *, (((uint8_t *)&gm_l2) + 0));
        uint64_t t12 = GM_LD(uint64_t, (((uint8_t *)t11) + 8));
        GM_ST(uint64_t, (uint8_t *)&t9 + 8, t12);
        void * t13 = GM_LD(void *, (((uint8_t *)&gm_l2) + 0));
        gm_t9qkh0rsooc8d t14 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t13) + 19));
        GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t9 + 16, t14);
        void * t15 = GM_LD(void *, (((uint8_t *)&gm_l2) + 0));
        uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)t15) + 17));
        GM_ST(uint8_t, (uint8_t *)&t9 + 17, t16);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tqvpud5umqqp1 *)gm_out) + 25, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tqvpud5umqqp1 *)gm_out) + 26, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tqvpud5umqqp1 *)gm_out) + 28, 0);
        ((uint8_t *)&(*(gm_tqvpud5umqqp1 *)gm_out))[24] = 1;
        GM_ST(gm_tilak255t5m34, (uint8_t *)&(*(gm_tqvpud5umqqp1 *)gm_out) + 0, t9);
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_tqvpud5umqqp1 gm_f_describe_t62jhnt_c(void * a0, uint64_t a1) { gm_tqvpud5umqqp1 gm_r; gm_f_describe_t62jhnt(&gm_r, a0, a1); return gm_r; }
__attribute__((aligned(4))) gm_b0_1 gm_f_drop_oldest_o9uaetp(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_drop_oldest_o9uaetp F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* pi */
    gm_b16_8 gm_l2; /* rest */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* k */
    GM_STACK_CHECK(&F, 499u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)t2) + 160));
        uint64_t t4 = (uintptr_t)t3;
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uintptr_t, ((((uint8_t *)t5) + 120) + 0) + 8);
        if (t4 >= (uintptr_t)(t6)) gm_trap_index(t4, t6, gm_srcq7ohh8h77bn7, 287, 11);
        uint64_t t7 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t5) + 120) + 0)) + t4 * 8));
        gm_l1 = t7;
    }
    {
        void * t8 = gm_l0;
        void * t9 = gm_l0;
        uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)t9) + 160));
        uint64_t r11; bool o12 = __builtin_add_overflow(t10, ((uint64_t)1ULL), &r11);
        if (o12) gm_trap("integer overflow in `+`", gm_srcq7ohh8h77bn7, 288, 5);
        uint64_t t13 = r11;
        GM_ST(uint64_t, (((uint8_t *)t8) + 160), t13);
    }
    {
        void * t15 = gm_l0;
        uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)t15) + 160));
        uint8_t t17 = (t16 > ((uint64_t)1024ULL));
        if (!t17) goto ie14;
        void * t18 = gm_l0;
        uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)t18) + 160));
        uint64_t r20; bool o21 = __builtin_mul_overflow(t19, ((uint64_t)2ULL), &r20);
        if (o21) gm_trap("integer overflow in `*`", gm_srcq7ohh8h77bn7, 289, 34);
        uint64_t t22 = r20;
        void * t23 = gm_l0;
        uint64_t t24 = GM_LD(uintptr_t, ((((uint8_t *)t23) + 120) + 0) + 8);
        uint8_t t25 = (t22 > t24);
        if (!t25) goto ie14;
        {
            void * t27 = gm_l0;
            uint64_t t28 = GM_LD(uintptr_t, ((((uint8_t *)t27) + 120) + 0) + 8);
            void * t29 = ((uint8_t *)GM_LD(void *, ((((uint8_t *)t27) + 120) + 0)));
            void * t30 = gm_l0;
            uint64_t t31 = GM_LD(uint64_t, (((uint8_t *)t30) + 160));
            uint64_t t32 = (uintptr_t)t31;
            void * t33 = gm_l0;
            uint64_t t34 = GM_LD(uintptr_t, ((((uint8_t *)t33) + 120) + 0) + 8);
            uint64_t t35 = (uintptr_t)t34;
            if (t32 > t35 || t35 > (uintptr_t)t28) gm_trap_slice(t32, t35, t28, gm_srcq7ohh8h77bn7, 290, 17);
            gm_b16_8 t36;
            GM_ST(void *, (uint8_t *)&t36, (void *)(((uint8_t *)t29 + t32 * 8)));
            GM_ST(uintptr_t, (uint8_t *)&t36 + 8, (uintptr_t)(t35 - t32));
            gm_l2 = t36;
        }
        {
            {
                gm_l4 = ((uint64_t)0ULL);
            }
            {
                uint64_t t38 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                gm_l3 = t38;
            }
            {
                for (;;) {
                    uint64_t t40 = gm_l4;
                    uint64_t t41 = gm_l3;
                    uint8_t t42 = (t40 < t41);
                    if (!t42) goto lb39;
                    {
                        uint64_t t45 = gm_l4;
                        uint64_t t48 = GM_LD(uint64_t, ((uint8_t *)&t45));
                        gm_l5 = t48;
                        {
                            uint64_t t50 = gm_l5;
                            uint64_t t51 = (uintptr_t)t50;
                            void * t52 = gm_l0;
                            uint64_t t53 = GM_LD(uintptr_t, ((((uint8_t *)t52) + 120) + 0) + 8);
                            if (t51 >= (uintptr_t)(t53)) gm_trap_index(t51, t53, gm_srcq7ohh8h77bn7, 291, 32);
                            uint64_t t54 = gm_l5;
                            uint64_t t55 = (uintptr_t)t54;
                            uint64_t t56 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                            if (t55 >= (uintptr_t)(t56)) gm_trap_index(t55, t56, gm_srcq7ohh8h77bn7, 291, 53);
                            uint64_t t57 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t55 * 8));
                            GM_ST(uint64_t, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t52) + 120) + 0)) + t51 * 8), t57);
                        }
                        goto me46;
mn47:;
                        gm_trap("no match arm matched the scrutinee", gm_srcq7ohh8h77bn7, 291, 9);
me46:;
                    }
lc39:;
                    {
                        uint64_t t59 = gm_l4;
                        uint64_t r60; bool o61 = __builtin_add_overflow(t59, ((uint64_t)1ULL), &r60);
                        uint64_t t62 = r60;
                        gm_l4 = t62;
                    }
                }
lb39:;
            }
        }
        {
            void * t63 = gm_l0;
            void * t64 = (void *)(((uint8_t *)t63) + 120);
            uint64_t t65 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            gm_f_truncate_ucmmehv(t64, t65);
        }
        {
            void * t66 = gm_l0;
            GM_ST(uint64_t, (((uint8_t *)t66) + 160), ((uint64_t)0ULL));
        }
        goto ix14;
ie14:;
ix14:;
    }
    {
        void * t67 = gm_l0;
        uint8_t *t68 = (((uint8_t *)t67) + 168);
        void * t69 = gm_l0;
        uint64_t t70 = GM_LD(uint64_t, (((uint8_t *)t69) + 168));
        void * t71 = gm_l0;
        uint64_t t72 = gm_l1;
        uint64_t t73 = gm_f_release_page_66inn9t((void *)NULL, cx, NULL, t71, t72);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint64_t r74; bool o75 = __builtin_sub_overflow(t70, t73, &r74);
        if (o75) gm_trap("integer overflow in `-`", gm_srcq7ohh8h77bn7, 295, 5);
        uint64_t t76 = r74;
        GM_ST(uint64_t, t68, t76);
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
inline __attribute__((aligned(4))) void gm_f_fault_u31b92l(void *gm_out, void * a0, uint64_t a1, uint64_t a2, uint64_t a3, uint8_t a4) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fault_u31b92l F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* addr */
    uint64_t gm_l2; /* len */
    uint64_t gm_l3; /* align */
    uint8_t gm_l4; /* write */
    gm_ttf6i6v1da8cm gm_l5; /* at */
    GM_STACK_CHECK_SHALLOW(&F, 435u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        uint64_t t3 = gm_l1;
        uint64_t t4 = gm_l3;
        uint8_t t5 = gm_f_aligned_q6sor4h(t3, t4);
        uint8_t t6 = (uint8_t)!t5;
        if (!t6) goto ie2;
        {
            uint64_t t8 = gm_l1;
            uint64_t t9 = gm_l3;
            gm_t2lqu8jsj511d t10;
            GM_ST(uint64_t, ((uint8_t *)&t10 + 0), t8);
            GM_ST(uint64_t, ((uint8_t *)&t10 + 8), t9);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tpekhfs32op0f *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tpekhfs32op0f *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tpekhfs32op0f *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tpekhfs32op0f *)gm_out) + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tpekhfs32op0f *)gm_out) + 32, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tpekhfs32op0f *)gm_out) + 0, ((uint8_t)4ULL));
            GM_ST(gm_t2lqu8jsj511d, (uint8_t *)&(*(gm_tpekhfs32op0f *)gm_out) + 8, t10);
            goto gm_fdone;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint64_t t12 = gm_l1;
        uint8_t t13 = ((t12 == ((uint64_t)0ULL)));
        if (!t13) goto ie11;
        {
            memset(&(*(gm_tpekhfs32op0f *)gm_out), 0, sizeof (*(gm_tpekhfs32op0f *)gm_out));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tpekhfs32op0f *)gm_out) + 0, ((uint8_t)0ULL));
            goto gm_fdone;
        }
        goto ix11;
ie11:;
ix11:;
    }
    {
        void * t15 = gm_l0;
        uint64_t t16 = gm_l1;
        gm_taordiq61jj46 t17;
        gm_f_locate_ciotffk((void *)&t17, t15, t16);
        gm_ttf6i6v1da8cm t18;
        if (((((uint8_t *)&t17))[16] != 0)) {
            gm_ttf6i6v1da8cm t19 = GM_LD(gm_ttf6i6v1da8cm, (((uint8_t *)&t17) + 0));
            t18 = t19;
        } else {
            uint64_t t20 = gm_l1;
            uint64_t t21 = gm_l2;
            gm_t2lqu8jsj511d t22;
            GM_ST(uint64_t, ((uint8_t *)&t22 + 0), t20);
            GM_ST(uint64_t, ((uint8_t *)&t22 + 8), t21);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tpekhfs32op0f *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tpekhfs32op0f *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tpekhfs32op0f *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tpekhfs32op0f *)gm_out) + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tpekhfs32op0f *)gm_out) + 32, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tpekhfs32op0f *)gm_out) + 0, ((uint8_t)1ULL));
            GM_ST(gm_t2lqu8jsj511d, (uint8_t *)&(*(gm_tpekhfs32op0f *)gm_out) + 8, t22);
            goto gm_fdone;
        }
        gm_l5 = t18;
    }
    {
        void * t24 = GM_LD(void *, (((uint8_t *)&gm_l5) + 0));
        uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)t24) + 17));
        uint8_t t26 = (uint8_t)!t25;
        if (!t26) goto ie23;
        {
            uint64_t t28 = gm_l1;
            void * t29 = GM_LD(void *, (((uint8_t *)&gm_l5) + 0));
            gm_t9qkh0rsooc8d t30 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t29) + 19));
            gm_t617vr72bd7b3 t31;
            GM_ST(uint8_t, (uint8_t *)&t31 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 12, 0);
            GM_ST(uint64_t, ((uint8_t *)&t31 + 0), t28);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t31 + 8), t30);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tpekhfs32op0f *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tpekhfs32op0f *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tpekhfs32op0f *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tpekhfs32op0f *)gm_out) + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tpekhfs32op0f *)gm_out) + 32, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tpekhfs32op0f *)gm_out) + 0, ((uint8_t)2ULL));
            GM_ST(gm_t617vr72bd7b3, (uint8_t *)&(*(gm_tpekhfs32op0f *)gm_out) + 8, t31);
            goto gm_fdone;
        }
        goto ix23;
ie23:;
ix23:;
    }
    {
        uint64_t t33 = gm_l2;
        void * t34 = GM_LD(void *, (((uint8_t *)&gm_l5) + 0));
        uint64_t t35 = GM_LD(uint64_t, (((uint8_t *)t34) + 8));
        uint64_t t36 = gm_l1;
        uint64_t t37 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
        uint64_t r38; bool o39 = __builtin_sub_overflow(t36, t37, &r38);
        if (o39) gm_trap("integer overflow in `-`", gm_srcq7ohh8h77bn7, 224, 30);
        uint64_t t40 = r38;
        uint64_t r41; bool o42 = __builtin_sub_overflow(t35, t40, &r41);
        if (o42) gm_trap("integer overflow in `-`", gm_srcq7ohh8h77bn7, 224, 14);
        uint64_t t43 = r41;
        uint8_t t44 = (t33 > t43);
        if (!t44) goto ie32;
        {
            gm_t66j1im9ip128 t46;
            uint64_t t47 = gm_l1;
            GM_ST(uint64_t, (uint8_t *)&t46 + 0, t47);
            uint64_t t48 = gm_l2;
            GM_ST(uint64_t, (uint8_t *)&t46 + 8, t48);
            uint64_t t49 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
            GM_ST(uint64_t, (uint8_t *)&t46 + 16, t49);
            void * t50 = GM_LD(void *, (((uint8_t *)&gm_l5) + 0));
            uint64_t t51 = GM_LD(uint64_t, (((uint8_t *)t50) + 8));
            GM_ST(uint64_t, (uint8_t *)&t46 + 24, t51);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tpekhfs32op0f *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tpekhfs32op0f *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tpekhfs32op0f *)gm_out) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tpekhfs32op0f *)gm_out) + 0, ((uint8_t)3ULL));
            GM_ST(gm_t66j1im9ip128, (uint8_t *)&(*(gm_tpekhfs32op0f *)gm_out) + 8, t46);
            goto gm_fdone;
        }
        goto ix32;
ie32:;
ix32:;
    }
    {
        uint8_t t53 = gm_l4;
        if (!t53) goto ie52;
        void * t54 = GM_LD(void *, (((uint8_t *)&gm_l5) + 0));
        uint8_t t55 = GM_LD(uint8_t, (((uint8_t *)t54) + 18));
        uint8_t t56 = (uint8_t)!t55;
        if (!t56) goto ie52;
        {
            uint64_t t58 = gm_l1;
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tpekhfs32op0f *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tpekhfs32op0f *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tpekhfs32op0f *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tpekhfs32op0f *)gm_out) + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tpekhfs32op0f *)gm_out) + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tpekhfs32op0f *)gm_out) + 32, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tpekhfs32op0f *)gm_out) + 0, ((uint8_t)5ULL));
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tpekhfs32op0f *)gm_out) + 8, t58);
            goto gm_fdone;
        }
        goto ix52;
ie52:;
ix52:;
    }
    {
        uint64_t t59 = gm_l1;
        uint64_t t60 = gm_l2;
        gm_t2lqu8jsj511d t61;
        GM_ST(uint64_t, ((uint8_t *)&t61 + 0), t59);
        GM_ST(uint64_t, ((uint8_t *)&t61 + 8), t60);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tpekhfs32op0f *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tpekhfs32op0f *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tpekhfs32op0f *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tpekhfs32op0f *)gm_out) + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tpekhfs32op0f *)gm_out) + 32, 0);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tpekhfs32op0f *)gm_out) + 0, ((uint8_t)1ULL));
        GM_ST(gm_t2lqu8jsj511d, (uint8_t *)&(*(gm_tpekhfs32op0f *)gm_out) + 8, t61);
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_tpekhfs32op0f gm_f_fault_u31b92l_c(void * a0, uint64_t a1, uint64_t a2, uint64_t a3, uint8_t a4) { gm_tpekhfs32op0f gm_r; gm_f_fault_u31b92l(&gm_r, a0, a1, a2, a3, a4); return gm_r; }
inline __attribute__((aligned(4))) void gm_f_init_9fuf99v(void *gm_out, void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_init_9fuf99v F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* addr */
    gm_b16_8 gm_l2; /* data */
    void * gm_l3; /* p */
    GM_STACK_CHECK(&F, 249u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        {
            uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            uint8_t t5 = ((t4 == ((uint64_t)0ULL)));
            if (!t5) goto ie3;
            {
                gm_b0_1 t7;
                memset(&(*(gm_tgfdnufetdnfc *)gm_out), 0, sizeof (*(gm_tgfdnufetdnfc *)gm_out));
                ((uint8_t *)&(*(gm_tgfdnufetdnfc *)gm_out))[0] = 0;
                GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tgfdnufetdnfc *)gm_out) + 8, t7);
                goto gm_fdone;
            }
            goto ix3;
ie3:;
ix3:;
        }
        {
            void * t8 = gm_l0;
            uint64_t t9 = gm_l1;
            uint64_t t10 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            uint64_t t11 = (uint64_t)t10;
            void * t12 = gm_f_view_qalkbv1(t8, t9, t11, ((uint8_t)0));
            void * t13;
            if ((GM_LD(void *, ((uint8_t *)&t12)) != 0)) {
                void * t14 = GM_LD(void *, (((uint8_t *)&t12) + 0));
                t13 = t14;
            } else {
                void * t15 = gm_l0;
                uint64_t t16 = gm_l1;
                uint64_t t17 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                uint64_t t18 = (uint64_t)t17;
                gm_tpekhfs32op0f t19;
                gm_f_fault_u31b92l((void *)&t19, t15, t16, t18, ((uint64_t)1ULL), ((uint8_t)0));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tgfdnufetdnfc *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tgfdnufetdnfc *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tgfdnufetdnfc *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_tgfdnufetdnfc *)gm_out))[0] = 1;
                GM_ST(gm_tpekhfs32op0f, (uint8_t *)&(*(gm_tgfdnufetdnfc *)gm_out) + 8, t19);
                goto gm_fdone;
            }
            gm_l3 = t13;
        }
        {
            void * t20 = gm_l3;
            void * t21 = ((uint8_t *)t20);
            uint64_t t22 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t23 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            uint64_t t24 = (uintptr_t)t23;
            gm_b16_8 t25;
            GM_ST(void *, (uint8_t *)&t25, (void *)(((uint8_t *)t21 + t22 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t25 + 8, (uintptr_t)(t24 - t22));
            gm_b16_8 t26 = gm_l2;
            gm_f_copy_from_f7j6p05(t25, t26);
        }
    }
    gm_b0_1 t27;
    memset(&(*(gm_tgfdnufetdnfc *)gm_out), 0, sizeof (*(gm_tgfdnufetdnfc *)gm_out));
    ((uint8_t *)&(*(gm_tgfdnufetdnfc *)gm_out))[0] = 0;
    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tgfdnufetdnfc *)gm_out) + 8, t27);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_tgfdnufetdnfc gm_f_init_9fuf99v_c(void * a0, uint64_t a1, gm_b16_8 a2) { gm_tgfdnufetdnfc gm_r; gm_f_init_9fuf99v(&gm_r, a0, a1, a2); return gm_r; }
__attribute__((aligned(4))) uint8_t gm_f_kill_3kcavfd(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1) {
    struct gm_fr_kill_3kcavfd F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* base */
    gm_ttf6i6v1da8cm gm_l2; /* at */
    void * gm_l3; /* s */
    uint64_t gm_l4; /* pi */
    void * gm_l5; /* pg */
    uint32_t gm_l6; /* full */
    GM_STACK_CHECK(&F, 607u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        uint64_t t3 = gm_l1;
        gm_taordiq61jj46 t4;
        gm_f_locate_ciotffk((void *)&t4, t2, t3);
        gm_ttf6i6v1da8cm t5;
        if (((((uint8_t *)&t4))[16] != 0)) {
            gm_ttf6i6v1da8cm t6 = GM_LD(gm_ttf6i6v1da8cm, (((uint8_t *)&t4) + 0));
            t5 = t6;
        } else {
            uint8_t t7 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        gm_l2 = t5;
    }
    {
        void * t8 = GM_LD(void *, (((uint8_t *)&gm_l2) + 0));
        gm_l3 = t8;
    }
    {
        uint8_t t10 = 0;
        uint64_t t11 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
        uint64_t t12 = gm_l1;
        uint8_t t13 = (!(t11 == t12));
        if (!t13) {
            void * t14 = gm_l3;
            uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)t14) + 17));
            uint8_t t16 = (uint8_t)!t15;
            t10 = t16;
        } else t10 = 1;
        if (!t10) goto ie9;
        {
            uint8_t t18 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix9;
ie9:;
ix9:;
    }
    {
        void * t19 = gm_l3;
        GM_ST(uint8_t, (((uint8_t *)t19) + 17), ((uint8_t)0));
    }
    {
        void * t21 = gm_l3;
        uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)t21) + 8));
        uint8_t t23 = (t22 > ((uint64_t)0ULL));
        if (!t23) goto ie20;
        {
            void * t25 = gm_l3;
            void * t26 = GM_LD(void *, (((uint8_t *)t25) + 0));
            void * t27 = ((uint8_t *)t26);
            uint64_t t28 = (uintptr_t)((uint64_t)0ULL);
            void * t29 = gm_l3;
            uint64_t t30 = GM_LD(uint64_t, (((uint8_t *)t29) + 8));
            uint64_t t31 = gm_f_us_cuv01fp(t30);
            uint64_t t32 = (uintptr_t)t31;
            gm_b16_8 t33;
            GM_ST(void *, (uint8_t *)&t33, (void *)(((uint8_t *)t27 + t28 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t33 + 8, (uintptr_t)(t32 - t28));
            gm_b16_8 t34 = GM_LD(gm_b16_8, ((uint8_t *)&t33));
            gm_b0_1 t35 = gm_f_free_rkuh2rm((void *)NULL, cx, NULL, t34, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_ks0d3nb0eoq)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix20;
ie20:;
ix20:;
    }
    {
        uint64_t t36 = gm_l1;
        void * t37 = gm_l0;
        uint64_t t38 = GM_LD(uint64_t, (((uint8_t *)t37) + 40));
        uint64_t r39; bool o40 = __builtin_sub_overflow(t36, t38, &r39);
        if (o40) gm_trap("integer overflow in `-`", gm_srcq7ohh8h77bn7, 272, 15);
        uint64_t t41 = r39;
        uint64_t t42 = GM_LD(uint64_t, gm_c6p4chcutca);
        unsigned __int128 n43 = (unsigned __int128)t42;
        if (n43 >= 64) gm_trap_shift((uint64_t)n43, "u64", gm_srcq7ohh8h77bn7, 272, 14);
        uint64_t t44 = (uint64_t)(t41 >> (int)n43);
        uint64_t t45 = gm_f_us_cuv01fp(t44);
        gm_l4 = t45;
    }
    {
        uint64_t t46 = gm_l4;
        uint64_t t47 = (uintptr_t)t46;
        void * t48 = gm_l0;
        uint64_t t49 = GM_LD(uintptr_t, ((((uint8_t *)t48) + 0) + 0) + 8);
        if (t47 >= (uintptr_t)(t49)) gm_trap_index(t47, t49, gm_srcq7ohh8h77bn7, 273, 12);
        void * t50 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t48) + 0) + 0)) + t47 * 32);
        gm_l5 = t50;
    }
    {
        void * t51 = gm_l5;
        void * t52 = gm_l5;
        uint32_t t53 = GM_LD(uint32_t, (((uint8_t *)t52) + 28));
        uint32_t r54; bool o55 = __builtin_add_overflow(t53, ((uint32_t)1ULL), &r54);
        if (o55) gm_trap("integer overflow in `+`", gm_srcq7ohh8h77bn7, 274, 5);
        uint32_t t56 = r54;
        GM_ST(uint32_t, (((uint8_t *)t51) + 28), t56);
    }
    {
        uint32_t t57;
        void * t59 = gm_l5;
        uint64_t t60 = GM_LD(uint64_t, (((uint8_t *)t59) + 16));
        uint64_t t61 = GM_LD(uint64_t, gm_c2t30gdg5i0);
        uint8_t t62 = ((t60 == t61));
        if (!t62) goto ie58;
        uint32_t t64 = ((uint32_t)1ULL);
        t57 = t64;
        goto ix58;
ie58:;
        uint64_t t66 = GM_LD(uint64_t, gm_ckasdtpni1c);
        void * t67 = gm_l5;
        uint64_t t68 = GM_LD(uint64_t, (((uint8_t *)t67) + 16));
        unsigned __int128 n69 = (unsigned __int128)t68;
        if (n69 >= 64) gm_trap_shift((uint64_t)n69, "u64", gm_srcq7ohh8h77bn7, 275, 48);
        uint64_t t70 = (uint64_t)(t66 >> (int)n69);
        uint32_t t71 = (uint32_t)t70;
        uint32_t t72 = t71;
        t57 = t72;
ix58:;
        gm_l6 = t57;
    }
    {
        void * t74 = gm_l5;
        uint32_t t75 = GM_LD(uint32_t, (((uint8_t *)t74) + 28));
        uint32_t t76 = gm_l6;
        uint8_t t77 = ((t75 == t76));
        if (!t77) goto ie73;
        {
            void * t79 = gm_l0;
            void * t80 = (void *)(((uint8_t *)t79) + 120);
            uint64_t t81 = gm_l4;
            gm_tftb1odc739t5 t82 = gm_f_push_ucgmlo2(t80, t81);
            if (((((uint8_t *)&t82))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t82) + 1, gm_srcq7ohh8h77bn7, 278, 9);
            gm_b0_1 t83 = GM_LD(gm_b0_1, (((uint8_t *)&t82) + 1));
        }
        {
            void * t84 = gm_l0;
            void * t85 = gm_l0;
            uint64_t t86 = GM_LD(uint64_t, (((uint8_t *)t85) + 168));
            uint32_t t87 = gm_l6;
            uint64_t t88 = (uint64_t)t87;
            uint64_t r89; bool o90 = __builtin_add_overflow(t86, t88, &r89);
            if (o90) gm_trap("integer overflow in `+`", gm_srcq7ohh8h77bn7, 279, 9);
            uint64_t t91 = r89;
            GM_ST(uint64_t, (((uint8_t *)t84) + 168), t91);
        }
        {
            for (;;) {
                void * t93 = gm_l0;
                uint64_t t94 = GM_LD(uint64_t, (((uint8_t *)t93) + 168));
                uint64_t t95 = GM_LD(uint64_t, gm_c2ftoibr66a);
                uint8_t t96 = (t94 > t95);
                if (!t96) goto lb92;
                {
                    void * t99 = gm_l0;
                    gm_b0_1 t100 = gm_f_drop_oldest_o9uaetp((void *)NULL, cx, NULL, t99);
                    if (gm_unw.kind) {
                        goto gm_ll97;
                    }
                }
lc92:;
            }
gm_ll97:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb92; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc92; }
            }
            goto gm_lad;
lb92:;
        }
        goto ix73;
ie73:;
ix73:;
    }
    {
        uint8_t t101 = 0;
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
inline __attribute__((aligned(4))) void gm_f_locate_ciotffk(void *gm_out, void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_locate_ciotffk F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* addr */
    void * gm_l2; /* pg */
    void * gm_l3; /* ss */
    uint64_t gm_l4; /* i */
    void * gm_l5; /* s */
    uint64_t gm_l6; /* base */
    GM_STACK_CHECK_SHALLOW(&F, 439u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        uint64_t t3 = gm_l1;
        void * t4 = gm_f_page_of_artfs32(t2, t3);
        void * t5;
        if ((GM_LD(void *, ((uint8_t *)&t4)) != 0)) {
            void * t6 = GM_LD(void *, (((uint8_t *)&t4) + 0));
            t5 = t6;
        } else {
            gm_taordiq61jj46 t7;
            memset(&t7, 0, sizeof t7);
            uint8_t t8 = 0;
            F.ret = t7;
            goto gm_fret;
        }
        gm_l2 = t5;
    }
    {
        void * t9 = gm_l2;
        void * t10 = GM_LD(void *, (((uint8_t *)t9) + 0));
        void * t11;
        if ((GM_LD(void *, ((uint8_t *)&t10)) != 0)) {
            void * t12 = GM_LD(void *, (((uint8_t *)&t10) + 0));
            t11 = t12;
        } else {
            gm_taordiq61jj46 t13;
            memset(&t13, 0, sizeof t13);
            uint8_t t14 = 0;
            F.ret = t13;
            goto gm_fret;
        }
        gm_l3 = t11;
    }
    {
        uint64_t t15 = gm_l1;
        void * t16 = gm_l2;
        uint64_t t17 = GM_LD(uint64_t, (((uint8_t *)t16) + 8));
        uint64_t r18; bool o19 = __builtin_sub_overflow(t15, t17, &r18);
        if (o19) gm_trap("integer overflow in `-`", gm_srcq7ohh8h77bn7, 187, 11);
        uint64_t t20 = r18;
        void * t21 = gm_l2;
        uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)t21) + 16));
        unsigned __int128 n23 = (unsigned __int128)t22;
        if (n23 >= 64) gm_trap_shift((uint64_t)n23, "u64", gm_srcq7ohh8h77bn7, 187, 10);
        uint64_t t24 = (uint64_t)(t20 >> (int)n23);
        gm_l4 = t24;
    }
    {
        void * t25 = gm_l3;
        void * t26 = t25;
        uint64_t t27 = gm_l4;
        uint64_t t28 = gm_f_us_cuv01fp(t27);
        uint64_t t29 = (uintptr_t)t28;
        void * t30 = (void *)((uint8_t *)t26 + t29 * 24);
        gm_l5 = t30;
    }
    {
        void * t31 = gm_l2;
        uint64_t t32 = GM_LD(uint64_t, (((uint8_t *)t31) + 8));
        uint64_t t33 = gm_l4;
        void * t34 = gm_l2;
        uint64_t t35 = GM_LD(uint64_t, (((uint8_t *)t34) + 16));
        unsigned __int128 n36 = (unsigned __int128)t35;
        if (n36 >= 64) gm_trap_shift((uint64_t)n36, "u64", gm_srcq7ohh8h77bn7, 189, 24);
        uint64_t r37 = (uint64_t)((uint64_t)t33 << (int)n36);
        bool o38 = (uint64_t)(r37 >> (int)n36) != t33;
        if (o38) gm_trap("integer overflow in `<<`", gm_srcq7ohh8h77bn7, 189, 24);
        uint64_t t39 = r37;
        uint64_t r40; bool o41 = __builtin_add_overflow(t32, t39, &r40);
        if (o41) gm_trap("integer overflow in `+`", gm_srcq7ohh8h77bn7, 189, 13);
        uint64_t t42 = r40;
        gm_l6 = t42;
    }
    {
        uint8_t t44 = 0;
        void * t45 = gm_l5;
        uint8_t t46 = GM_LD(uint8_t, (((uint8_t *)t45) + 16));
        uint8_t t47 = (uint8_t)!t46;
        if (!t47) {
            uint64_t t48 = gm_l1;
            uint64_t t49 = gm_l6;
            uint64_t r50; bool o51 = __builtin_sub_overflow(t48, t49, &r50);
            if (o51) gm_trap("integer overflow in `-`", gm_srcq7ohh8h77bn7, 190, 22);
            uint64_t t52 = r50;
            void * t53 = gm_l5;
            uint64_t t54 = GM_LD(uint64_t, (((uint8_t *)t53) + 8));
            uint8_t t55 = (t52 > t54);
            t44 = t55;
        } else t44 = 1;
        if (!t44) goto ie43;
        {
            gm_taordiq61jj46 t57;
            memset(&t57, 0, sizeof t57);
            uint8_t t58 = 0;
            F.ret = t57;
            goto gm_fret;
        }
        goto ix43;
ie43:;
ix43:;
    }
    {
        void * t59 = gm_l5;
        uint64_t t60 = gm_l6;
        gm_ttf6i6v1da8cm t61;
        GM_ST(void *, ((uint8_t *)&t61 + 0), t59);
        GM_ST(uint64_t, ((uint8_t *)&t61 + 8), t60);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_taordiq61jj46 *)gm_out) + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_taordiq61jj46 *)gm_out) + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_taordiq61jj46 *)gm_out) + 20, 0);
        ((uint8_t *)&(*(gm_taordiq61jj46 *)gm_out))[16] = 1;
        GM_ST(gm_ttf6i6v1da8cm, (uint8_t *)&(*(gm_taordiq61jj46 *)gm_out) + 0, t61);
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_taordiq61jj46 gm_f_locate_ciotffk_c(void * a0, uint64_t a1) { gm_taordiq61jj46 gm_r; gm_f_locate_ciotffk(&gm_r, a0, a1); return gm_r; }
inline __attribute__((aligned(4))) gm_b0_1 gm_f_mem_deinit_0088958(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_mem_deinit_0088958 F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* <tmp> */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* pi */
    GM_STACK_CHECK(&F, 153u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        {
            gm_l2 = ((uint64_t)0ULL);
        }
        {
            void * t3 = gm_l0;
            uint64_t t4 = GM_LD(uintptr_t, ((((uint8_t *)t3) + 0) + 0) + 8);
            gm_l1 = t4;
        }
        {
            for (;;) {
                uint64_t t6 = gm_l2;
                uint64_t t7 = gm_l1;
                uint8_t t8 = (t6 < t7);
                if (!t8) goto lb5;
                {
                    uint64_t t11 = gm_l2;
                    uint64_t t14 = GM_LD(uint64_t, ((uint8_t *)&t11));
                    gm_l3 = t14;
                    {
                        {
                            void * t17 = gm_l0;
                            uint64_t t18 = gm_l3;
                            uint64_t t19 = gm_f_release_page_66inn9t((void *)NULL, cx, NULL, t17, t18);
                            if (gm_unw.kind) {
                                goto gm_ll9;
                            }
                        }
                    }
                    goto me12;
mn13:;
                    gm_trap("no match arm matched the scrutinee", gm_srcq7ohh8h77bn7, 327, 5);
me12:;
                }
lc5:;
                {
                    uint64_t t21 = gm_l2;
                    uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
                    uint64_t t24 = r22;
                    gm_l2 = t24;
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
        void * t25 = gm_l0;
        void * t26 = (void *)(((uint8_t *)t25) + 0);
        gm_f_deinit_u4v62un(t26);
    }
    {
        void * t27 = gm_l0;
        void * t28 = (void *)(((uint8_t *)t27) + 120);
        gm_f_deinit_d5p9g61(t28);
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
__attribute__((aligned(4))) gm_b16_8 gm_f_mem_message_e0go901(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_mem_message_e0go901 F;
    uint64_t gm_l1; /* addr */
    uint64_t gm_l2; /* len */
    uint64_t gm_l3; /* addr */
    gm_t9qkh0rsooc8d gm_l4; /* kind */
    gm_b16_8 gm_l5; /* what */
    gm_t66j1im9ip128 gm_l6; /* x */
    uint64_t gm_l7; /* addr */
    uint64_t gm_l8; /* align */
    uint64_t gm_l9; /* addr */
    uint64_t gm_l10; /* size */
    gm_t9qkh0rsooc8d gm_l11; /* kind */
    GM_STACK_CHECK(&F, 1040u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1;
    uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    switch (t12) { case ((uint8_t)0ULL): goto ma3; case ((uint8_t)1ULL): goto ma4; case ((uint8_t)2ULL): goto ma5; case ((uint8_t)3ULL): goto ma6; case ((uint8_t)4ULL): goto ma7; case ((uint8_t)5ULL): goto ma8; case ((uint8_t)6ULL): goto ma9; case ((uint8_t)7ULL): goto ma10; default: goto ma11; }
ma3:;
    uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t14 != ((uint8_t)0ULL)) goto mn13;
    gm_b16_8 t15;
    GM_ST(void *, (uint8_t *)&t15, (void *)(gm_suklu1i9clb5l));
    GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)(47));
    t1 = t15;
    goto me2;
mn13:;
ma4:;
    uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t17 != ((uint8_t)1ULL)) goto mn16;
    uint64_t t18 = GM_LD(uint64_t, ((((uint8_t *)a0) + 8) + 0));
    gm_l1 = t18;
    uint64_t t19 = GM_LD(uint64_t, ((((uint8_t *)a0) + 8) + 8));
    gm_l2 = t19;
    uint64_t t20 = gm_l2;
    uint64_t t21 = gm_l1;
    gm_t2lqu8jsj511d t22;
    GM_ST(uint64_t, ((uint8_t *)&t22 + 0), t20);
    GM_ST(uint64_t, ((uint8_t *)&t22 + 8), t21);
    gm_tabb710d9mq2e t23;
    gm_f_format_bovj9ap((void *)&t23, (void *)NULL, cx, NULL, t22);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (((((uint8_t *)&t23))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t23) + 8, gm_srcq7ohh8h77bn7, 78, 29);
    gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&t23) + 8));
    gm_b16_8 t25 = GM_LD(gm_b16_8, ((uint8_t *)&t24));
    t1 = t25;
    goto me2;
mn16:;
ma5:;
    uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t27 != ((uint8_t)2ULL)) goto mn26;
    uint64_t t28 = GM_LD(uint64_t, ((((uint8_t *)a0) + 8) + 0));
    gm_l3 = t28;
    gm_t9qkh0rsooc8d t29 = GM_LD(gm_t9qkh0rsooc8d, ((((uint8_t *)a0) + 8) + 8));
    gm_l4 = t29;
    {
        gm_b16_8 t31;
        uint8_t t37 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
        switch (t37) { case ((uint8_t)0ULL): goto ma33; case ((uint8_t)1ULL): goto ma33; case ((uint8_t)2ULL): goto ma34; case ((uint8_t)3ULL): goto ma35; case ((uint8_t)4ULL): goto ma35; case ((uint8_t)5ULL): goto ma35; default: goto ma35; }
ma33:;
        uint8_t t41 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
        if (t41 != ((uint8_t)0ULL)) goto pa40;
        goto po39;
pa40:;
        uint8_t t43 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
        if (t43 != ((uint8_t)1ULL)) goto pa42;
        goto po39;
pa42:;
        goto mn38;
po39:;
        gm_b16_8 t44;
        GM_ST(void *, (uint8_t *)&t44, (void *)(gm_skt7g4v99o4j0));
        GM_ST(uintptr_t, (uint8_t *)&t44 + 8, (uintptr_t)(41));
        t31 = t44;
        goto me32;
mn38:;
ma34:;
        uint8_t t46 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
        if (t46 != ((uint8_t)2ULL)) goto mn45;
        gm_b16_8 t47;
        GM_ST(void *, (uint8_t *)&t47, (void *)(gm_sagd3osht35og));
        GM_ST(uintptr_t, (uint8_t *)&t47 + 8, (uintptr_t)(12));
        t31 = t47;
        goto me32;
mn45:;
ma35:;
        gm_b16_8 t49;
        GM_ST(void *, (uint8_t *)&t49, (void *)(gm_sacs352aeakr8));
        GM_ST(uintptr_t, (uint8_t *)&t49 + 8, (uintptr_t)(17));
        t31 = t49;
        goto me32;
mn48:;
ma36:;
        gm_trap("no match arm matched the scrutinee", gm_srcq7ohh8h77bn7, 80, 17);
me32:;
        gm_l5 = t31;
    }
    gm_tf1bjg81u0glv t50;
    uint64_t t51 = gm_l3;
    GM_ST(uint64_t, (uint8_t *)&t50 + 0, t51);
    gm_b16_8 t52 = gm_l5;
    GM_ST(gm_b16_8, (uint8_t *)&t50 + 8, t52);
    gm_tabb710d9mq2e t53;
    gm_f_format_q07lmui((void *)&t53, (void *)NULL, cx, NULL, &t50);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (((((uint8_t *)&t53))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t53) + 8, gm_srcq7ohh8h77bn7, 85, 9);
    gm_b16_8 t54 = GM_LD(gm_b16_8, (((uint8_t *)&t53) + 8));
    gm_b16_8 t55 = t54;
    gm_b16_8 t56 = GM_LD(gm_b16_8, ((uint8_t *)&t55));
    t1 = t56;
    goto me2;
mn26:;
ma6:;
    uint8_t t58 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t58 != ((uint8_t)3ULL)) goto mn57;
    gm_t66j1im9ip128 t59 = GM_LD(gm_t66j1im9ip128, (((uint8_t *)a0) + 8));
    gm_l6 = t59;
    gm_t66j1im9ip128 t60;
    uint64_t t61 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
    GM_ST(uint64_t, (uint8_t *)&t60 + 0, t61);
    uint64_t t62 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 0));
    GM_ST(uint64_t, (uint8_t *)&t60 + 8, t62);
    uint64_t t63 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 16));
    GM_ST(uint64_t, (uint8_t *)&t60 + 16, t63);
    uint64_t t64 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 24));
    GM_ST(uint64_t, (uint8_t *)&t60 + 24, t64);
    gm_tabb710d9mq2e t65;
    gm_f_format_173gk69((void *)&t65, (void *)NULL, cx, NULL, &t60);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (((((uint8_t *)&t65))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t65) + 8, gm_srcq7ohh8h77bn7, 87, 24);
    gm_b16_8 t66 = GM_LD(gm_b16_8, (((uint8_t *)&t65) + 8));
    gm_b16_8 t67 = GM_LD(gm_b16_8, ((uint8_t *)&t66));
    t1 = t67;
    goto me2;
mn57:;
ma7:;
    uint8_t t69 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t69 != ((uint8_t)4ULL)) goto mn68;
    uint64_t t70 = GM_LD(uint64_t, ((((uint8_t *)a0) + 8) + 0));
    gm_l7 = t70;
    uint64_t t71 = GM_LD(uint64_t, ((((uint8_t *)a0) + 8) + 8));
    gm_l8 = t71;
    uint64_t t72 = gm_l7;
    uint64_t t73 = gm_l8;
    gm_t2lqu8jsj511d t74;
    GM_ST(uint64_t, ((uint8_t *)&t74 + 0), t72);
    GM_ST(uint64_t, ((uint8_t *)&t74 + 8), t73);
    gm_tabb710d9mq2e t75;
    gm_f_format_hvaqp8b((void *)&t75, (void *)NULL, cx, NULL, t74);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (((((uint8_t *)&t75))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t75) + 8, gm_srcq7ohh8h77bn7, 88, 33);
    gm_b16_8 t76 = GM_LD(gm_b16_8, (((uint8_t *)&t75) + 8));
    gm_b16_8 t77 = GM_LD(gm_b16_8, ((uint8_t *)&t76));
    t1 = t77;
    goto me2;
mn68:;
ma8:;
    uint8_t t79 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t79 != ((uint8_t)5ULL)) goto mn78;
    uint64_t t80 = GM_LD(uint64_t, (((uint8_t *)a0) + 8));
    gm_l9 = t80;
    uint64_t t81 = gm_l9;
    gm_t4ce0jd3tdro4 t82;
    GM_ST(uint64_t, ((uint8_t *)&t82 + 0), t81);
    gm_tabb710d9mq2e t83;
    gm_f_format_n0787fu((void *)&t83, (void *)NULL, cx, NULL, t82);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (((((uint8_t *)&t83))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t83) + 8, gm_srcq7ohh8h77bn7, 89, 24);
    gm_b16_8 t84 = GM_LD(gm_b16_8, (((uint8_t *)&t83) + 8));
    gm_b16_8 t85 = GM_LD(gm_b16_8, ((uint8_t *)&t84));
    t1 = t85;
    goto me2;
mn78:;
ma9:;
    uint8_t t87 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t87 != ((uint8_t)6ULL)) goto mn86;
    gm_b16_8 t88;
    GM_ST(void *, (uint8_t *)&t88, (void *)(gm_sdvkaj73n9goh));
    GM_ST(uintptr_t, (uint8_t *)&t88 + 8, (uintptr_t)(23));
    t1 = t88;
    goto me2;
mn86:;
ma10:;
    uint8_t t90 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t90 != ((uint8_t)7ULL)) goto mn89;
    uint64_t t91 = GM_LD(uint64_t, ((((uint8_t *)a0) + 8) + 0));
    gm_l10 = t91;
    gm_t9qkh0rsooc8d t92 = GM_LD(gm_t9qkh0rsooc8d, ((((uint8_t *)a0) + 8) + 8));
    gm_l11 = t92;
    gm_b16_8 t93;
    gm_t9qkh0rsooc8d t95 = gm_l11;
    gm_t9qkh0rsooc8d t96;
    GM_ST(uint8_t, ((uint8_t *)&t96 + 0), ((uint8_t)0ULL));
    gm_t9qkh0rsooc8d t97;
    GM_ST(uint8_t, ((uint8_t *)&t97 + 0), ((uint8_t)1ULL));
    gm_t0am46iohlpol t98;
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t98 + 0), t96);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t98 + 1), t97);
    uint8_t t99 = 0;
    if (gm_eqv4kh4qul78(((uint8_t *)&t98) + 0, ((uint8_t *)&t95))) t99 = 1;
    if (gm_eqv4kh4qul78(((uint8_t *)&t98) + 1, ((uint8_t *)&t95))) t99 = 1;
    uint8_t t100 = (uint8_t)(t99);
    if (!t100) goto ie94;
    uint64_t t102 = gm_l10;
    gm_t4ce0jd3tdro4 t103;
    GM_ST(uint64_t, ((uint8_t *)&t103 + 0), t102);
    gm_tabb710d9mq2e t104;
    gm_f_format_itetl3f((void *)&t104, (void *)NULL, cx, NULL, t103);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (((((uint8_t *)&t104))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t104) + 8, gm_srcq7ohh8h77bn7, 91, 77);
    gm_b16_8 t105 = GM_LD(gm_b16_8, (((uint8_t *)&t104) + 8));
    gm_b16_8 t106 = t105;
    gm_b16_8 t107 = GM_LD(gm_b16_8, ((uint8_t *)&t106));
    t93 = t107;
    goto ix94;
ie94:;
    uint64_t t109 = gm_l10;
    gm_t4ce0jd3tdro4 t110;
    GM_ST(uint64_t, ((uint8_t *)&t110 + 0), t109);
    gm_tabb710d9mq2e t111;
    gm_f_format_o05ssh3((void *)&t111, (void *)NULL, cx, NULL, t110);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (((((uint8_t *)&t111))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t111) + 8, gm_srcq7ohh8h77bn7, 91, 156);
    gm_b16_8 t112 = GM_LD(gm_b16_8, (((uint8_t *)&t111) + 8));
    gm_b16_8 t113 = t112;
    gm_b16_8 t114 = GM_LD(gm_b16_8, ((uint8_t *)&t113));
    t93 = t114;
ix94:;
    t1 = t93;
    goto me2;
mn89:;
ma11:;
    gm_trap("no match arm matched the scrutinee", gm_srcq7ohh8h77bn7, 76, 57);
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
inline __attribute__((aligned(4))) void gm_f_memory_dqom95e(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0, uint64_t a1) {
    struct gm_fr_memory_dqom95e F;
    uint64_t gm_l0; /* data_base */
    uint64_t gm_l1; /* ptr_bytes */
    uint64_t gm_l2; /* limit */
    uint64_t gm_l3; /* base */
    GM_STACK_CHECK(&F, 321u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t2;
        uint64_t t4 = gm_l1;
        uint8_t t5 = (t4 >= ((uint64_t)8ULL));
        if (!t5) goto ie3;
        uint64_t t7 = (uint64_t)~((uint64_t)0ULL);
        uint64_t t8 = t7;
        t2 = t8;
        goto ix3;
ie3:;
        uint64_t t10 = gm_l1;
        uint64_t r11; bool o12 = __builtin_mul_overflow(t10, ((uint64_t)8ULL), &r11);
        if (o12) gm_trap("integer overflow in `*`", gm_srcq7ohh8h77bn7, 111, 62);
        uint64_t t13 = r11;
        unsigned __int128 n14 = (unsigned __int128)t13;
        if (n14 >= 64) gm_trap_shift((uint64_t)n14, "u64", gm_srcq7ohh8h77bn7, 111, 51);
        uint64_t r15 = (uint64_t)((uint64_t)((uint64_t)1ULL) << (int)n14);
        bool o16 = (uint64_t)(r15 >> (int)n14) != ((uint64_t)1ULL);
        if (o16) gm_trap("integer overflow in `<<`", gm_srcq7ohh8h77bn7, 111, 51);
        uint64_t t17 = r15;
        uint64_t t18 = t17;
        t2 = t18;
ix3:;
        gm_l2 = t2;
    }
    {
        uint64_t t19 = gm_l0;
        uint64_t t20 = GM_LD(uint64_t, gm_ckasdtpni1c);
        uint64_t r21; bool o22 = __builtin_add_overflow(t19, t20, &r21);
        if (o22) gm_trap("integer overflow in `+`", gm_srcq7ohh8h77bn7, 112, 14);
        uint64_t t23 = r21;
        uint64_t r24; bool o25 = __builtin_sub_overflow(t23, ((uint64_t)1ULL), &r24);
        if (o25) gm_trap("integer overflow in `-`", gm_srcq7ohh8h77bn7, 112, 14);
        uint64_t t26 = r24;
        uint64_t t27 = GM_LD(uint64_t, gm_ckasdtpni1c);
        if (t27 == 0) gm_trap("division by zero", gm_srcq7ohh8h77bn7, 112, 13);
        uint64_t t28 = (uint64_t)(t26 / t27);
        uint64_t t29 = GM_LD(uint64_t, gm_ckasdtpni1c);
        uint64_t r30; bool o31 = __builtin_mul_overflow(t28, t29, &r30);
        if (o31) gm_trap("integer overflow in `*`", gm_srcq7ohh8h77bn7, 112, 13);
        uint64_t t32 = r30;
        gm_l3 = t32;
    }
    {
        gm_trqfvmjtt4sij t33;
        gm_f_list_bcj49nd((void *)&t33, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint64_t t34 = gm_l3;
        uint64_t t35 = gm_l2;
        gm_tl4e4vqe807ge t36;
        for (uint64_t i = 0; i < 8; i++) GM_ST(uint64_t, (uint8_t *)&t36 + i * 8, ((uint64_t)0ULL));
        gm_trqfvmjtt4sij t37;
        gm_f_list_5dhgg4i((void *)&t37, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_t4m55nu62fs91 *)gm_out) + 0, t33);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t4m55nu62fs91 *)gm_out) + 40, t34);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t4m55nu62fs91 *)gm_out) + 48, t35);
        GM_ST(gm_tl4e4vqe807ge, (uint8_t *)&(*(gm_t4m55nu62fs91 *)gm_out) + 56, t36);
        GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_t4m55nu62fs91 *)gm_out) + 120, t37);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t4m55nu62fs91 *)gm_out) + 160, ((uint64_t)0ULL));
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t4m55nu62fs91 *)gm_out) + 168, ((uint64_t)0ULL));
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
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_next_pages_p5tqfl1(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_next_pages_p5tqfl1 F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* n */
    uint64_t gm_l2; /* addr */
    GM_STACK_CHECK_SHALLOW(&F, 219u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)t2) + 40));
        void * t4 = gm_l0;
        uint64_t t5 = GM_LD(uintptr_t, ((((uint8_t *)t4) + 0) + 0) + 8);
        uint64_t t6 = (uint64_t)t5;
        uint64_t t7 = GM_LD(uint64_t, gm_ckasdtpni1c);
        uint64_t r8; bool o9 = __builtin_mul_overflow(t6, t7, &r8);
        if (o9) gm_trap("integer overflow in `*`", gm_srcq7ohh8h77bn7, 118, 22);
        uint64_t t10 = r8;
        uint64_t r11; bool o12 = __builtin_add_overflow(t3, t10, &r11);
        if (o12) gm_trap("integer overflow in `+`", gm_srcq7ohh8h77bn7, 118, 13);
        uint64_t t13 = r11;
        gm_l2 = t13;
    }
    {
        uint64_t t15 = gm_l1;
        void * t16 = gm_l0;
        uint64_t t17 = GM_LD(uint64_t, (((uint8_t *)t16) + 48));
        uint64_t t18 = gm_l2;
        uint64_t r19; bool o20 = __builtin_sub_overflow(t17, t18, &r19);
        if (o20) gm_trap("integer overflow in `-`", gm_srcq7ohh8h77bn7, 119, 13);
        uint64_t t21 = r19;
        uint64_t t22 = GM_LD(uint64_t, gm_ckasdtpni1c);
        if (t22 == 0) gm_trap("division by zero", gm_srcq7ohh8h77bn7, 119, 12);
        uint64_t t23 = (uint64_t)(t21 / t22);
        uint8_t t24 = (t15 > t23);
        if (!t24) goto ie14;
        {
            gm_tf5srh4mp4b3i t26;
            memset(&t26, 0, sizeof t26);
            uint8_t t27 = 0;
            F.ret = t26;
            goto gm_fret;
        }
        goto ix14;
ie14:;
ix14:;
    }
    {
        uint64_t t28 = gm_l2;
        gm_tf5srh4mp4b3i t29;
        GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
        GM_ST(uint64_t, ((uint8_t *)&t29), t28);
        ((uint8_t *)&t29)[8] = 1;
        uint8_t t30 = 0;
        F.ret = t29;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) void * gm_f_page_of_artfs32(void * a0, uint64_t a1);
extern __attribute__((aligned(4))) void gm_f_read_u6r4vns(void *gm_out, void * a0, uint64_t a1, uint64_t a2, uint64_t a3);
__attribute__((aligned(4))) gm_t934k1j7u70ct gm_f_read_u6r4vns_c(void * a0, uint64_t a1, uint64_t a2, uint64_t a3) { gm_t934k1j7u70ct gm_r; gm_f_read_u6r4vns(&gm_r, a0, a1, a2, a3); return gm_r; }
__attribute__((aligned(4))) uint64_t gm_f_release_page_66inn9t(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1) {
    struct gm_fr_release_page_66inn9t F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* pi */
    void * gm_l2; /* pg */
    void * gm_l3; /* ss */
    void * gm_l4; /* s */
    uint64_t gm_l5; /* k */
    uint64_t gm_l6; /* per */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* i */
    void * gm_l10; /* s */
    GM_STACK_CHECK(&F, 925u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t2 = gm_l1;
        uint64_t t3 = (uintptr_t)t2;
        void * t4 = gm_l0;
        uint64_t t5 = GM_LD(uintptr_t, ((((uint8_t *)t4) + 0) + 0) + 8);
        if (t3 >= (uintptr_t)(t5)) gm_trap_index(t3, t5, gm_srcq7ohh8h77bn7, 302, 12);
        void * t6 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t4) + 0) + 0)) + t3 * 32);
        gm_l2 = t6;
    }
    {
        void * t7 = gm_l2;
        void * t8 = GM_LD(void *, (((uint8_t *)t7) + 0));
        void * t9;
        if ((GM_LD(void *, ((uint8_t *)&t8)) != 0)) {
            void * t10 = GM_LD(void *, (((uint8_t *)&t8) + 0));
            t9 = t10;
        } else {
            uint8_t t11 = 0;
            F.ret = ((uint64_t)0ULL);
            goto gm_fret;
        }
        gm_l3 = t9;
    }
    {
        void * t13 = gm_l2;
        uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)t13) + 16));
        uint64_t t15 = GM_LD(uint64_t, gm_c2t30gdg5i0);
        uint8_t t16 = ((t14 == t15));
        if (!t16) goto ie12;
        {
            uint64_t t18 = (uintptr_t)((uint64_t)0ULL);
            void * t19 = gm_l3;
            void * t20 = (void *)((uint8_t *)t19 + t18 * 24);
            gm_l4 = t20;
        }
        {
            void * t22 = gm_l4;
            uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)t22) + 17));
            if (!t23) goto ie21;
            void * t24 = gm_l4;
            uint64_t t25 = GM_LD(uint64_t, (((uint8_t *)t24) + 8));
            uint8_t t26 = (t25 > ((uint64_t)0ULL));
            if (!t26) goto ie21;
            {
                void * t28 = gm_l4;
                void * t29 = GM_LD(void *, (((uint8_t *)t28) + 0));
                void * t30 = ((uint8_t *)t29);
                uint64_t t31 = (uintptr_t)((uint64_t)0ULL);
                void * t32 = gm_l4;
                uint64_t t33 = GM_LD(uint64_t, (((uint8_t *)t32) + 8));
                uint64_t t34 = gm_f_us_cuv01fp(t33);
                uint64_t t35 = (uintptr_t)t34;
                gm_b16_8 t36;
                GM_ST(void *, (uint8_t *)&t36, (void *)(((uint8_t *)t30 + t31 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t36 + 8, (uintptr_t)(t35 - t31));
                gm_b16_8 t37 = GM_LD(gm_b16_8, ((uint8_t *)&t36));
                gm_b0_1 t38 = gm_f_free_rkuh2rm((void *)NULL, cx, NULL, t37, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k0ddgrnig17)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix21;
ie21:;
ix21:;
        }
        {
            uint64_t t39 = gm_l1;
            gm_l5 = t39;
        }
        {
            for (;;) {
                uint64_t t41 = gm_l5;
                void * t42 = gm_l0;
                uint64_t t43 = GM_LD(uintptr_t, ((((uint8_t *)t42) + 0) + 0) + 8);
                uint8_t t44 = (t41 < t43);
                if (!t44) goto lb40;
                uint64_t t45 = gm_l5;
                uint64_t t46 = (uintptr_t)t45;
                void * t47 = gm_l0;
                uint64_t t48 = GM_LD(uintptr_t, ((((uint8_t *)t47) + 0) + 0) + 8);
                if (t46 >= (uintptr_t)(t48)) gm_trap_index(t46, t48, gm_srcq7ohh8h77bn7, 308, 41);
                uint64_t t49 = GM_LD(uint64_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t47) + 0) + 0)) + t46 * 32) + 16));
                uint64_t t50 = GM_LD(uint64_t, gm_c2t30gdg5i0);
                uint8_t t51 = ((t49 == t50));
                if (!t51) goto lb40;
                uint64_t t52 = gm_l5;
                uint64_t t53 = (uintptr_t)t52;
                void * t54 = gm_l0;
                uint64_t t55 = GM_LD(uintptr_t, ((((uint8_t *)t54) + 0) + 0) + 8);
                if (t53 >= (uintptr_t)(t55)) gm_trap_index(t53, t55, gm_srcq7ohh8h77bn7, 308, 77);
                uint64_t t56 = GM_LD(uint64_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t54) + 0) + 0)) + t53 * 32) + 8));
                void * t57 = gm_l2;
                uint64_t t58 = GM_LD(uint64_t, (((uint8_t *)t57) + 8));
                uint8_t t59 = ((t56 == t58));
                if (!t59) goto lb40;
                uint64_t t60 = gm_l5;
                uint64_t t61 = (uintptr_t)t60;
                void * t62 = gm_l0;
                uint64_t t63 = GM_LD(uintptr_t, ((((uint8_t *)t62) + 0) + 0) + 8);
                if (t61 >= (uintptr_t)(t63)) gm_trap_index(t61, t63, gm_srcq7ohh8h77bn7, 308, 114);
                void * t64 = GM_LD(void *, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t62) + 0) + 0)) + t61 * 32) + 0));
                uint8_t t65;
                if ((GM_LD(void *, ((uint8_t *)&t64)) != 0)) goto mn67;
                t65 = ((uint8_t)0);
                goto me66;
mn67:;
                t65 = ((uint8_t)1);
                goto me66;
mn68:;
                gm_trap("no match arm matched the scrutinee", gm_srcq7ohh8h77bn7, 308, 114);
me66:;
                if (!t65) goto lb40;
                {
                    uint64_t t71 = gm_l5;
                    uint64_t t72 = (uintptr_t)t71;
                    void * t73 = gm_l0;
                    uint64_t t74 = GM_LD(uintptr_t, ((((uint8_t *)t73) + 0) + 0) + 8);
                    if (t72 >= (uintptr_t)(t74)) gm_trap_index(t72, t74, gm_srcq7ohh8h77bn7, 309, 13);
                    GM_ST(void *, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t73) + 0) + 0)) + t72 * 32) + 0), ((void *)0));
                }
                {
                    uint64_t t75 = gm_l5;
                    uint64_t r76; bool o77 = __builtin_add_overflow(t75, ((uint64_t)1ULL), &r76);
                    if (o77) gm_trap("integer overflow in `+`", gm_srcq7ohh8h77bn7, 310, 13);
                    uint64_t t78 = r76;
                    gm_l5 = t78;
                }
lc40:;
            }
lb40:;
        }
        {
            void * t79 = gm_l4;
            gm_b0_1 t80 = gm_f_destroy_l0inh8t((void *)NULL, cx, NULL, t79, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kgubkej3sov)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            uint8_t t81 = 0;
            F.ret = ((uint64_t)1ULL);
            goto gm_fret;
        }
        goto ix12;
ie12:;
ix12:;
    }
    {
        uint64_t t82 = GM_LD(uint64_t, gm_ckasdtpni1c);
        void * t83 = gm_l2;
        uint64_t t84 = GM_LD(uint64_t, (((uint8_t *)t83) + 16));
        unsigned __int128 n85 = (unsigned __int128)t84;
        if (n85 >= 64) gm_trap_shift((uint64_t)n85, "u64", gm_srcq7ohh8h77bn7, 315, 15);
        uint64_t t86 = (uint64_t)(t82 >> (int)n85);
        uint64_t t87 = gm_f_us_cuv01fp(t86);
        gm_l6 = t87;
    }
    {
        {
            gm_l8 = ((uint64_t)0ULL);
        }
        {
            uint64_t t89 = gm_l6;
            gm_l7 = t89;
        }
        {
            for (;;) {
                uint64_t t91 = gm_l8;
                uint64_t t92 = gm_l7;
                uint8_t t93 = (t91 < t92);
                if (!t93) goto lb90;
                {
                    uint64_t t96 = gm_l8;
                    uint64_t t99 = GM_LD(uint64_t, ((uint8_t *)&t96));
                    gm_l9 = t99;
                    {
                        uint64_t t101 = gm_l9;
                        uint64_t t102 = (uintptr_t)t101;
                        void * t103 = gm_l3;
                        void * t104 = (void *)((uint8_t *)t103 + t102 * 24);
                        gm_l10 = t104;
                    }
                    {
                        void * t106 = gm_l10;
                        uint8_t t107 = GM_LD(uint8_t, (((uint8_t *)t106) + 17));
                        if (!t107) goto ie105;
                        void * t108 = gm_l10;
                        uint64_t t109 = GM_LD(uint64_t, (((uint8_t *)t108) + 8));
                        uint8_t t110 = (t109 > ((uint64_t)0ULL));
                        if (!t110) goto ie105;
                        {
                            void * t112 = gm_l10;
                            void * t113 = GM_LD(void *, (((uint8_t *)t112) + 0));
                            void * t114 = ((uint8_t *)t113);
                            uint64_t t115 = (uintptr_t)((uint64_t)0ULL);
                            void * t116 = gm_l10;
                            uint64_t t117 = GM_LD(uint64_t, (((uint8_t *)t116) + 8));
                            uint64_t t118 = gm_f_us_cuv01fp(t117);
                            uint64_t t119 = (uintptr_t)t118;
                            gm_b16_8 t120;
                            GM_ST(void *, (uint8_t *)&t120, (void *)(((uint8_t *)t114 + t115 * 1)));
                            GM_ST(uintptr_t, (uint8_t *)&t120 + 8, (uintptr_t)(t119 - t115));
                            gm_b16_8 t121 = GM_LD(gm_b16_8, ((uint8_t *)&t120));
                            gm_b0_1 t122 = gm_f_free_rkuh2rm((void *)NULL, cx, NULL, t121, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k2afmplo64l)));
                            if (gm_unw.kind) {
                                goto gm_ll94;
                            }
                        }
                        goto ix105;
ie105:;
ix105:;
                    }
                    goto me97;
mn98:;
                    gm_trap("no match arm matched the scrutinee", gm_srcq7ohh8h77bn7, 316, 5);
me97:;
                }
lc90:;
                {
                    uint64_t t124 = gm_l8;
                    uint64_t r125; bool o126 = __builtin_add_overflow(t124, ((uint64_t)1ULL), &r125);
                    uint64_t t127 = r125;
                    gm_l8 = t127;
                }
            }
gm_ll94:;
            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb90; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc90; }
            }
            goto gm_lad;
lb90:;
        }
    }
    {
        void * t128 = gm_l3;
        void * t129 = ((uint8_t *)t128);
        uint64_t t130 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t131 = gm_l6;
        uint64_t t132 = (uintptr_t)t131;
        gm_b16_8 t133;
        GM_ST(void *, (uint8_t *)&t133, (void *)(((uint8_t *)t129 + t130 * 24)));
        GM_ST(uintptr_t, (uint8_t *)&t133 + 8, (uintptr_t)(t132 - t130));
        gm_b16_8 t134 = GM_LD(gm_b16_8, ((uint8_t *)&t133));
        gm_b0_1 t135 = gm_f_free_oobi9fu((void *)NULL, cx, NULL, t134, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_ke9p7o91039)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t136 = gm_l2;
        GM_ST(void *, (((uint8_t *)t136) + 0), ((void *)0));
    }
    {
        uint64_t t137 = gm_l6;
        uint8_t t138 = 0;
        F.ret = t137;
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
extern __attribute__((aligned(4))) void * gm_f_view_qalkbv1(void * a0, uint64_t a1, uint64_t a2, uint8_t a3);
inline __attribute__((aligned(4))) void gm_f_write_5gub0ul(void *gm_out, void * a0, uint64_t a1, gm_b16_8 a2, uint64_t a3) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_5gub0ul F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* addr */
    gm_b16_8 gm_l2; /* data */
    uint64_t gm_l3; /* align */
    gm_b16_8 gm_l4; /* d */
    GM_STACK_CHECK(&F, 240u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        {
            void * t3 = gm_l0;
            uint64_t t4 = gm_l1;
            uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            uint64_t t6 = (uint64_t)t5;
            uint64_t t7 = gm_l3;
            gm_t934k1j7u70ct t8;
            gm_f_write_view_pebit6h((void *)&t8, t3, t4, t6, t7);
            if (((((uint8_t *)&t8))[0] != 0)) {
                gm_tgfdnufetdnfc t9;
                GM_ST(uint8_t, (uint8_t *)&t9 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t9 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t9 + 4, 0);
                ((uint8_t *)&t9)[0] = 1;
                GM_ST(gm_tpekhfs32op0f, ((uint8_t *)&t9 + 8), (*(gm_tpekhfs32op0f *)(((uint8_t *)&t8) + 8)));
                F.ret = t9;
                goto gm_fret;
            }
            gm_b16_8 t10 = GM_LD(gm_b16_8, (((uint8_t *)&t8) + 8));
            gm_l4 = t10;
        }
        {
            gm_b16_8 t11 = gm_l4;
            gm_b16_8 t12 = gm_l2;
            gm_f_copy_from_f7j6p05(t11, t12);
        }
    }
    gm_b0_1 t13;
    memset(&(*(gm_tgfdnufetdnfc *)gm_out), 0, sizeof (*(gm_tgfdnufetdnfc *)gm_out));
    ((uint8_t *)&(*(gm_tgfdnufetdnfc *)gm_out))[0] = 0;
    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tgfdnufetdnfc *)gm_out) + 8, t13);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_tgfdnufetdnfc gm_f_write_5gub0ul_c(void * a0, uint64_t a1, gm_b16_8 a2, uint64_t a3) { gm_tgfdnufetdnfc gm_r; gm_f_write_5gub0ul(&gm_r, a0, a1, a2, a3); return gm_r; }
extern __attribute__((aligned(4))) void gm_f_write_view_pebit6h(void *gm_out, void * a0, uint64_t a1, uint64_t a2, uint64_t a3);
__attribute__((aligned(4))) gm_t934k1j7u70ct gm_f_write_view_pebit6h_c(void * a0, uint64_t a1, uint64_t a2, uint64_t a3) { gm_t934k1j7u70ct gm_r; gm_f_write_view_pebit6h(&gm_r, a0, a1, a2, a3); return gm_r; }

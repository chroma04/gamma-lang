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
typedef struct gm_cs9ufftihtbpo2 { uint32_t f0; uint32_t f1; } gm_cs9ufftihtbpo2;
typedef struct gm_cseahvg8v7p0vs { uint32_t f0; uint32_t f1; uint32_t f2; } gm_cseahvg8v7p0vs;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { uint64_t a[3]; } gm_tc0nbpdhofi37;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; } gm_tgtub0v94d5u5;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { uint8_t f0; uint8_t p1[3]; union { int32_t v0; } f1; } gm_toe5fmr3vvf4d;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
_Static_assert(sizeof(gm_cs9ufftihtbpo2) == 8 && _Alignof(gm_cs9ufftihtbpo2) == 4 && offsetof(gm_cs9ufftihtbpo2, f0) == 0 && offsetof(gm_cs9ufftihtbpo2, f1) == 4, "layout of `LineCol`");
_Static_assert(sizeof(gm_cseahvg8v7p0vs) == 12 && _Alignof(gm_cseahvg8v7p0vs) == 4 && offsetof(gm_cseahvg8v7p0vs, f0) == 0 && offsetof(gm_cseahvg8v7p0vs, f1) == 4 && offsetof(gm_cseahvg8v7p0vs, f2) == 8, "layout of `Span`");
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t f2; uint8_t p3[6]; gm_b16_8 f3; void * f4; void * f5; gm_tl874t2atnlh3 f6; uint8_t pe[4]; } gm_t24301fas1b2a;
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t79rbdv4rafe5;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; void * f1; } gm_t8ghgl2jj0qha;
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tao63pk3r18v5) == 32 && _Alignof(gm_tao63pk3r18v5) == 8 && offsetof(gm_tao63pk3r18v5, f0) == 0 && offsetof(gm_tao63pk3r18v5, f1) == 8 && offsetof(gm_tao63pk3r18v5, f2) == 16 && offsetof(gm_tao63pk3r18v5, f3) == 24, "layout of `SliceExpr`");
_Static_assert(sizeof(gm_tc0nbpdhofi37) == 24 && _Alignof(gm_tc0nbpdhofi37) == 8 && offsetof(gm_tc0nbpdhofi37, a) == 0, "layout of `[3]usize`");
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { uint64_t f0; gm_b16_8 f1; } gm_tf1bjg81u0glv;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
_Static_assert(sizeof(gm_tgtub0v94d5u5) == 16 && _Alignof(gm_tgtub0v94d5u5) == 8 && offsetof(gm_tgtub0v94d5u5, f0) == 0 && offsetof(gm_tgtub0v94d5u5, f1) == 8, "layout of `(is_const: bool, inner: ^Expr)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_tibupt4dmcqai;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_toe5fmr3vvf4d v0; gm_t9qkh0rsooc8d v1; } f1; } gm_tj5jn79kq9pnm;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
typedef struct { void * f0; gm_b16_8 f1; uint8_t f2; uint8_t pe[7]; } gm_tlljg61a2ctgq;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t f1; uint8_t pe[3]; } gm_toceffb697p0u;
_Static_assert(sizeof(gm_toe5fmr3vvf4d) == 8 && _Alignof(gm_toe5fmr3vvf4d) == 4 && offsetof(gm_toe5fmr3vvf4d, f0) == 0 && offsetof(gm_toe5fmr3vvf4d, f1) == 4, "layout of `IoError`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint8_t pe[4]; } gm_tommf4j9upomc;
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; void * f2; uint32_t f3; uint8_t pe[4]; } gm_tpi91a971gh2v;
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
typedef struct { gm_b16_8 a[17]; } gm_tu0fv4fh3er3s;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; } gm_tud7oovfvnf43;
typedef struct { void * f0; uint32_t f1; gm_tl874t2atnlh3 f2; } gm_tupk62cr6lbdu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; gm_t9qkh0rsooc8d f3; uint8_t p4[7]; void * f4; gm_tl874t2atnlh3 f5; uint8_t pe[4]; } gm_tv2uf4leil5hq;
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
typedef struct { gm_t08olq97rscn3 f0; uint8_t f1; uint8_t pe[7]; } gm_t1s75i08qtptd;
_Static_assert(sizeof(gm_t24301fas1b2a) == 72 && _Alignof(gm_t24301fas1b2a) == 8 && offsetof(gm_t24301fas1b2a, f0) == 0 && offsetof(gm_t24301fas1b2a, f1) == 16 && offsetof(gm_t24301fas1b2a, f2) == 17 && offsetof(gm_t24301fas1b2a, f3) == 24 && offsetof(gm_t24301fas1b2a, f4) == 40 && offsetof(gm_t24301fas1b2a, f5) == 48 && offsetof(gm_t24301fas1b2a, f6) == 56, "layout of `FieldDecl`");
typedef struct { gm_trqfvmjtt4sij f0; uint64_t f1; uint64_t f2; uint32_t f3; uint8_t pe[4]; } gm_t2792tsg5eg72;
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `WasmReader`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_th2lqbo77pvsc f6; } gm_t4gut2c9fj1kt;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tnpifv20irnph v0; } f1; } gm_t5ghc3lnpmg2h;
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t79rbdv4rafe5) == 40 && _Alignof(gm_t79rbdv4rafe5) == 8 && offsetof(gm_t79rbdv4rafe5, f0) == 0 && offsetof(gm_t79rbdv4rafe5, f1) == 8 && offsetof(gm_t79rbdv4rafe5, f2) == 24, "layout of `Block`");
typedef struct { void * f0; gm_b16_8 f1; gm_th2lqbo77pvsc f2; gm_tl874t2atnlh3 f3; uint8_t pe[4]; } gm_t7bjbmukv26e7;
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { gm_t08olq97rscn3 f0; gm_t9qkh0rsooc8d f1; uint8_t p2[7]; gm_b16_8 f2; void * f3; gm_tl874t2atnlh3 f4; uint8_t pe[4]; } gm_tc0sbs1tp6d7f;
typedef struct { gm_tibupt4dmcqai f0; gm_tl874t2atnlh3 f1; uint8_t p2[4]; gm_t79rbdv4rafe5 f2; uint8_t f3; uint8_t pe[7]; } gm_tcfiotghd1s5k;
typedef struct { gm_t08olq97rscn3 f0; gm_th2lqbo77pvsc f1; } gm_tet9guhev5r4u;
_Static_assert(sizeof(gm_tf1bjg81u0glv) == 24 && _Alignof(gm_tf1bjg81u0glv) == 8 && offsetof(gm_tf1bjg81u0glv, f0) == 0 && offsetof(gm_tf1bjg81u0glv, f1) == 8, "layout of `(i: usize, x: string)`");
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `Toolchain`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tnpifv20irnph v0; gm_tud7oovfvnf43 v1; } f1; } gm_tflfe6l4hnrb7;
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tj5jn79kq9pnm v0; gm_b16_8 v1; } f1; } gm_tgvh15qshf643;
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_tibupt4dmcqai) == 24 && _Alignof(gm_tibupt4dmcqai) == 8 && offsetof(gm_tibupt4dmcqai, f0) == 0 && offsetof(gm_tibupt4dmcqai, f1) == 16, "layout of `StrLit`");
typedef struct { gm_trqfvmjtt4sij f0; } gm_tin97tt4ud0s1;
typedef struct { void * f0; gm_t08olq97rscn3 f1; } gm_tioie2ju3q4ul;
_Static_assert(sizeof(gm_tj5jn79kq9pnm) == 12 && _Alignof(gm_tj5jn79kq9pnm) == 4 && offsetof(gm_tj5jn79kq9pnm, f0) == 0 && offsetof(gm_tj5jn79kq9pnm, f1) == 4, "layout of `ReadError`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_b16_8 f2; gm_th2lqbo77pvsc f3; gm_b16_8 f4; gm_b16_8 f5; gm_b16_8 f6; gm_th2lqbo77pvsc f7; } gm_tk50ot32aqf1o;
typedef struct { gm_t08olq97rscn3 f0; gm_th2lqbo77pvsc f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tkacitepg5fk0;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t p2[7]; void * f2; gm_th2lqbo77pvsc f3; gm_tl874t2atnlh3 f4; uint8_t pe[4]; } gm_tlivgedjs5ps3;
_Static_assert(sizeof(gm_tlljg61a2ctgq) == 32 && _Alignof(gm_tlljg61a2ctgq) == 8 && offsetof(gm_tlljg61a2ctgq, f0) == 0 && offsetof(gm_tlljg61a2ctgq, f1) == 8 && offsetof(gm_tlljg61a2ctgq, f2) == 24, "layout of `(callee: ^Expr, args: []Arg, trailing_comma: bool)`");
typedef struct { gm_t08olq97rscn3 f0; void * f1; void * f2; gm_tl874t2atnlh3 f3; uint8_t pe[4]; } gm_tnljbma3s3h16;
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_toceffb697p0u) == 16 && _Alignof(gm_toceffb697p0u) == 4 && offsetof(gm_toceffb697p0u, f0) == 0 && offsetof(gm_toceffb697p0u, f1) == 12, "layout of `?Span`");
_Static_assert(sizeof(gm_tommf4j9upomc) == 24 && _Alignof(gm_tommf4j9upomc) == 8 && offsetof(gm_tommf4j9upomc, f0) == 0 && offsetof(gm_tommf4j9upomc, f1) == 16, "layout of `(string, u32)`");
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
_Static_assert(sizeof(gm_tpi91a971gh2v) == 48 && _Alignof(gm_tpi91a971gh2v) == 8 && offsetof(gm_tpi91a971gh2v, f0) == 0 && offsetof(gm_tpi91a971gh2v, f1) == 16 && offsetof(gm_tpi91a971gh2v, f2) == 32 && offsetof(gm_tpi91a971gh2v, f3) == 40, "layout of `Cx`");
typedef struct { gm_trqfvmjtt4sij f0; gm_trqfvmjtt4sij f1; gm_trqfvmjtt4sij f2; } gm_tr8dvlu9ik4gj;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; uint64_t f2; void * f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_trqfvmjtt4sij f6; gm_tc0nbpdhofi37 f7; gm_trqfvmjtt4sij f8; gm_toceffb697p0u f9; gm_trqfvmjtt4sij f10; uint8_t f11; uint8_t pe[7]; } gm_trd6bg8is1v4n;
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
typedef struct { gm_t08olq97rscn3 f0; void * f1; } gm_ts686ckv8ehm2;
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
_Static_assert(sizeof(gm_tu0fv4fh3er3s) == 272 && _Alignof(gm_tu0fv4fh3er3s) == 8 && offsetof(gm_tu0fv4fh3er3s, a) == 0, "layout of `[17]string`");
_Static_assert(sizeof(gm_tud7oovfvnf43) == 80 && _Alignof(gm_tud7oovfvnf43) == 8 && offsetof(gm_tud7oovfvnf43, f0) == 0 && offsetof(gm_tud7oovfvnf43, f1) == 16 && offsetof(gm_tud7oovfvnf43, f2) == 32 && offsetof(gm_tud7oovfvnf43, f3) == 48 && offsetof(gm_tud7oovfvnf43, f4) == 64, "layout of `(string, string, string, string, string)`");
_Static_assert(sizeof(gm_tupk62cr6lbdu) == 24 && _Alignof(gm_tupk62cr6lbdu) == 8 && offsetof(gm_tupk62cr6lbdu, f0) == 0 && offsetof(gm_tupk62cr6lbdu, f1) == 8 && offsetof(gm_tupk62cr6lbdu, f2) == 12, "layout of `(base: ^Expr, index: u32, index_span: Span)`");
_Static_assert(sizeof(gm_tv2uf4leil5hq) == 64 && _Alignof(gm_tv2uf4leil5hq) == 8 && offsetof(gm_tv2uf4leil5hq, f0) == 0 && offsetof(gm_tv2uf4leil5hq, f1) == 8 && offsetof(gm_tv2uf4leil5hq, f2) == 24 && offsetof(gm_tv2uf4leil5hq, f3) == 32 && offsetof(gm_tv2uf4leil5hq, f4) == 40 && offsetof(gm_tv2uf4leil5hq, f5) == 48, "layout of `Binding`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tnljbma3s3h16 v0; gm_t7bjbmukv26e7 v1; } f1; } gm_t03a3imodqi32;
_Static_assert(sizeof(gm_t1s75i08qtptd) == 40 && _Alignof(gm_t1s75i08qtptd) == 8 && offsetof(gm_t1s75i08qtptd, f0) == 0 && offsetof(gm_t1s75i08qtptd, f1) == 32, "layout of `?Ident`");
_Static_assert(sizeof(gm_t2792tsg5eg72) == 64 && _Alignof(gm_t2792tsg5eg72) == 8 && offsetof(gm_t2792tsg5eg72, f0) == 0 && offsetof(gm_t2792tsg5eg72, f1) == 40 && offsetof(gm_t2792tsg5eg72, f2) == 48 && offsetof(gm_t2792tsg5eg72, f3) == 56, "layout of `Diagnostics`");
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
_Static_assert(sizeof(gm_t5ghc3lnpmg2h) == 24 && _Alignof(gm_t5ghc3lnpmg2h) == 8 && offsetof(gm_t5ghc3lnpmg2h, f0) == 0 && offsetof(gm_t5ghc3lnpmg2h, f1) == 8, "layout of `Failure!()`");
typedef struct { gm_t08olq97rscn3 f0; gm_tlivgedjs5ps3 f1; } gm_t70h2c775g4n7;
_Static_assert(sizeof(gm_t7bjbmukv26e7) == 64 && _Alignof(gm_t7bjbmukv26e7) == 8 && offsetof(gm_t7bjbmukv26e7, f0) == 0 && offsetof(gm_t7bjbmukv26e7, f1) == 8 && offsetof(gm_t7bjbmukv26e7, f2) == 24 && offsetof(gm_t7bjbmukv26e7, f3) == 48, "layout of `WhenItems`");
typedef struct { gm_tin97tt4ud0s1 f0; gm_t2792tsg5eg72 f1; uint32_t f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_b16_8 f5; } gm_t9672f4jnou6n;
_Static_assert(sizeof(gm_tc0sbs1tp6d7f) == 80 && _Alignof(gm_tc0sbs1tp6d7f) == 8 && offsetof(gm_tc0sbs1tp6d7f, f0) == 0 && offsetof(gm_tc0sbs1tp6d7f, f1) == 32 && offsetof(gm_tc0sbs1tp6d7f, f2) == 40 && offsetof(gm_tc0sbs1tp6d7f, f3) == 56 && offsetof(gm_tc0sbs1tp6d7f, f4) == 64, "layout of `OpDecl`");
_Static_assert(sizeof(gm_tcfiotghd1s5k) == 88 && _Alignof(gm_tcfiotghd1s5k) == 8 && offsetof(gm_tcfiotghd1s5k, f0) == 0 && offsetof(gm_tcfiotghd1s5k, f1) == 24 && offsetof(gm_tcfiotghd1s5k, f2) == 40 && offsetof(gm_tcfiotghd1s5k, f3) == 80, "layout of `TestDecl`");
typedef struct { gm_t1s75i08qtptd f0; void * f1; } gm_tchr52g0ema53;
_Static_assert(sizeof(gm_tet9guhev5r4u) == 56 && _Alignof(gm_tet9guhev5r4u) == 8 && offsetof(gm_tet9guhev5r4u, f0) == 0 && offsetof(gm_tet9guhev5r4u, f1) == 32, "layout of `IntrinsicExpr`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t24301fas1b2a v0; gm_t7bjbmukv26e7 v1; } f1; } gm_tfc3oljlofib6;
_Static_assert(sizeof(gm_tflfe6l4hnrb7) == 88 && _Alignof(gm_tflfe6l4hnrb7) == 8 && offsetof(gm_tflfe6l4hnrb7, f0) == 0 && offsetof(gm_tflfe6l4hnrb7, f1) == 8, "layout of `Failure!Module`");
_Static_assert(sizeof(gm_tgvh15qshf643) == 24 && _Alignof(gm_tgvh15qshf643) == 8 && offsetof(gm_tgvh15qshf643, f0) == 0 && offsetof(gm_tgvh15qshf643, f1) == 8, "layout of `ReadError![]u8`");
_Static_assert(sizeof(gm_tin97tt4ud0s1) == 40 && _Alignof(gm_tin97tt4ud0s1) == 8 && offsetof(gm_tin97tt4ud0s1, f0) == 0, "layout of `SourceMap`");
typedef struct { void * f0; gm_t1s75i08qtptd f1; void * f2; } gm_tioi3jh9v22g5;
_Static_assert(sizeof(gm_tioie2ju3q4ul) == 40 && _Alignof(gm_tioie2ju3q4ul) == 8 && offsetof(gm_tioie2ju3q4ul, f0) == 0 && offsetof(gm_tioie2ju3q4ul, f1) == 8, "layout of `(base: ^Expr, name: Ident)`");
_Static_assert(sizeof(gm_tk50ot32aqf1o) == 136 && _Alignof(gm_tk50ot32aqf1o) == 8 && offsetof(gm_tk50ot32aqf1o, f0) == 0 && offsetof(gm_tk50ot32aqf1o, f1) == 8 && offsetof(gm_tk50ot32aqf1o, f2) == 24 && offsetof(gm_tk50ot32aqf1o, f3) == 40 && offsetof(gm_tk50ot32aqf1o, f4) == 64 && offsetof(gm_tk50ot32aqf1o, f5) == 80 && offsetof(gm_tk50ot32aqf1o, f6) == 96 && offsetof(gm_tk50ot32aqf1o, f7) == 112, "layout of `Decl`");
_Static_assert(sizeof(gm_tkacitepg5fk0) == 72 && _Alignof(gm_tkacitepg5fk0) == 8 && offsetof(gm_tkacitepg5fk0, f0) == 0 && offsetof(gm_tkacitepg5fk0, f1) == 32 && offsetof(gm_tkacitepg5fk0, f2) == 56, "layout of `Attribute`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tv2uf4leil5hq v0; gm_t7bjbmukv26e7 v1; void * v2; gm_tcfiotghd1s5k v3; void * v4; } f1; } gm_tkoope2qgegd9;
_Static_assert(sizeof(gm_tlivgedjs5ps3) == 72 && _Alignof(gm_tlivgedjs5ps3) == 8 && offsetof(gm_tlivgedjs5ps3, f0) == 0 && offsetof(gm_tlivgedjs5ps3, f1) == 16 && offsetof(gm_tlivgedjs5ps3, f2) == 24 && offsetof(gm_tlivgedjs5ps3, f3) == 32 && offsetof(gm_tlivgedjs5ps3, f4) == 56, "layout of `FnSig`");
_Static_assert(sizeof(gm_tnljbma3s3h16) == 64 && _Alignof(gm_tnljbma3s3h16) == 8 && offsetof(gm_tnljbma3s3h16, f0) == 0 && offsetof(gm_tnljbma3s3h16, f1) == 32 && offsetof(gm_tnljbma3s3h16, f2) == 40 && offsetof(gm_tnljbma3s3h16, f3) == 48, "layout of `Variant`");
_Static_assert(sizeof(gm_tr8dvlu9ik4gj) == 120 && _Alignof(gm_tr8dvlu9ik4gj) == 8 && offsetof(gm_tr8dvlu9ik4gj, f0) == 0 && offsetof(gm_tr8dvlu9ik4gj, f1) == 40 && offsetof(gm_tr8dvlu9ik4gj, f2) == 80, "layout of `Extras`");
_Static_assert(sizeof(gm_trd6bg8is1v4n) == 288 && _Alignof(gm_trd6bg8is1v4n) == 8 && offsetof(gm_trd6bg8is1v4n, f0) == 0 && offsetof(gm_trd6bg8is1v4n, f1) == 8 && offsetof(gm_trd6bg8is1v4n, f2) == 24 && offsetof(gm_trd6bg8is1v4n, f3) == 32 && offsetof(gm_trd6bg8is1v4n, f4) == 40 && offsetof(gm_trd6bg8is1v4n, f5) == 80 && offsetof(gm_trd6bg8is1v4n, f6) == 120 && offsetof(gm_trd6bg8is1v4n, f7) == 160 && offsetof(gm_trd6bg8is1v4n, f8) == 184 && offsetof(gm_trd6bg8is1v4n, f9) == 224 && offsetof(gm_trd6bg8is1v4n, f10) == 240 && offsetof(gm_trd6bg8is1v4n, f11) == 280, "layout of `Lexer`");
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
_Static_assert(sizeof(gm_ts686ckv8ehm2) == 40 && _Alignof(gm_ts686ckv8ehm2) == 8 && offsetof(gm_ts686ckv8ehm2, f0) == 0 && offsetof(gm_ts686ckv8ehm2, f1) == 32, "layout of `(name: Ident, ty: ^Expr)`");
_Static_assert(sizeof(gm_t03a3imodqi32) == 72 && _Alignof(gm_t03a3imodqi32) == 8 && offsetof(gm_t03a3imodqi32, f0) == 0 && offsetof(gm_t03a3imodqi32, f1) == 8, "layout of `UnionMember`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; void * v1; gm_tibupt4dmcqai v2; uint8_t v3; gm_t08olq97rscn3 v4; gm_t08olq97rscn3 v5; void * v6; gm_b16_8 v7; void * v8; gm_t8ghgl2jj0qha v9; gm_trl9d7m1k5lbd v10; gm_tioi3jh9v22g5 v11; void * v12; void * v13; gm_t3mf58gv9kf3n v14; void * v15; gm_tlljg61a2ctgq v16; gm_t3mf58gv9kf3n v17; gm_tao63pk3r18v5 v18; gm_tioie2ju3q4ul v19; gm_tupk62cr6lbdu v20; void * v21; gm_tgtub0v94d5u5 v22; gm_tgtub0v94d5u5 v23; gm_tgtub0v94d5u5 v24; gm_t3mf58gv9kf3n v25; gm_t3mf58gv9kf3n v26; void * v27; void * v28; void * v29; void * v30; void * v31; gm_tpe1hv3fekn05 v32; void * v33; void * v34; gm_tibupt4dmcqai v35; void * v36; void * v37; void * v38; void * v39; void * v40; gm_t79rbdv4rafe5 v41; void * v42; gm_tchr52g0ema53 v43; gm_t1s75i08qtptd v44; gm_t3mf58gv9kf3n v45; gm_tet9guhev5r4u v46; } f1; } gm_t2gtbh39ptekd;
_Static_assert(sizeof(gm_t70h2c775g4n7) == 104 && _Alignof(gm_t70h2c775g4n7) == 8 && offsetof(gm_t70h2c775g4n7, f0) == 0 && offsetof(gm_t70h2c775g4n7, f1) == 32, "layout of `(name: Ident, sig: FnSig)`");
_Static_assert(sizeof(gm_t9672f4jnou6n) == 208 && _Alignof(gm_t9672f4jnou6n) == 8 && offsetof(gm_t9672f4jnou6n, f0) == 0 && offsetof(gm_t9672f4jnou6n, f1) == 40 && offsetof(gm_t9672f4jnou6n, f2) == 104 && offsetof(gm_t9672f4jnou6n, f3) == 112 && offsetof(gm_t9672f4jnou6n, f4) == 152 && offsetof(gm_t9672f4jnou6n, f5) == 192, "layout of `Reader`");
_Static_assert(sizeof(gm_tchr52g0ema53) == 48 && _Alignof(gm_tchr52g0ema53) == 8 && offsetof(gm_tchr52g0ema53, f0) == 0 && offsetof(gm_tchr52g0ema53, f1) == 40, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_tfc3oljlofib6) == 80 && _Alignof(gm_tfc3oljlofib6) == 8 && offsetof(gm_tfc3oljlofib6, f0) == 0 && offsetof(gm_tfc3oljlofib6, f1) == 8, "layout of `StructMember`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t70h2c775g4n7 v0; gm_ts686ckv8ehm2 v1; } f1; } gm_tg69e9d4krfe7;
_Static_assert(sizeof(gm_tioi3jh9v22g5) == 56 && _Alignof(gm_tioi3jh9v22g5) == 8 && offsetof(gm_tioi3jh9v22g5, f0) == 0 && offsetof(gm_tioi3jh9v22g5, f1) == 8 && offsetof(gm_tioi3jh9v22g5, f2) == 48, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_tkoope2qgegd9) == 96 && _Alignof(gm_tkoope2qgegd9) == 8 && offsetof(gm_tkoope2qgegd9, f0) == 0 && offsetof(gm_tkoope2qgegd9, f1) == 8, "layout of `ItemKind`");
typedef struct { uint32_t f0; gm_tl874t2atnlh3 f1; gm_th2lqbo77pvsc f2; gm_b16_8 f3; gm_t9qkh0rsooc8d f4; uint8_t p5[7]; gm_tkoope2qgegd9 f5; } gm_tvpnktv5fo8nl;
_Static_assert(sizeof(gm_t2gtbh39ptekd) == 64 && _Alignof(gm_t2gtbh39ptekd) == 8 && offsetof(gm_t2gtbh39ptekd, f0) == 0 && offsetof(gm_t2gtbh39ptekd, f1) == 8, "layout of `ExprKind`");
typedef struct { gm_b16_8 f0; gm_tg69e9d4krfe7 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t7rh4gj19r55v;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_t2gtbh39ptekd f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tc3oht4ksth9d;
_Static_assert(sizeof(gm_tg69e9d4krfe7) == 112 && _Alignof(gm_tg69e9d4krfe7) == 8 && offsetof(gm_tg69e9d4krfe7, f0) == 0 && offsetof(gm_tg69e9d4krfe7, f1) == 8, "layout of `ForeignMemberKind`");
_Static_assert(sizeof(gm_tvpnktv5fo8nl) == 160 && _Alignof(gm_tvpnktv5fo8nl) == 8 && offsetof(gm_tvpnktv5fo8nl, f0) == 0 && offsetof(gm_tvpnktv5fo8nl, f1) == 4 && offsetof(gm_tvpnktv5fo8nl, f2) == 16 && offsetof(gm_tvpnktv5fo8nl, f3) == 40 && offsetof(gm_tvpnktv5fo8nl, f4) == 56 && offsetof(gm_tvpnktv5fo8nl, f5) == 64, "layout of `Item`");
typedef struct { gm_tc3oht4ksth9d f0; uint8_t f1; uint8_t pe[7]; } gm_t0mfir12leg2g;
_Static_assert(sizeof(gm_t7rh4gj19r55v) == 144 && _Alignof(gm_t7rh4gj19r55v) == 8 && offsetof(gm_t7rh4gj19r55v, f0) == 0 && offsetof(gm_t7rh4gj19r55v, f1) == 16 && offsetof(gm_t7rh4gj19r55v, f2) == 128, "layout of `ForeignMember`");
_Static_assert(sizeof(gm_tc3oht4ksth9d) == 88 && _Alignof(gm_tc3oht4ksth9d) == 8 && offsetof(gm_tc3oht4ksth9d, f0) == 0 && offsetof(gm_tc3oht4ksth9d, f1) == 8 && offsetof(gm_tc3oht4ksth9d, f2) == 72, "layout of `Expr`");
_Static_assert(sizeof(gm_t0mfir12leg2g) == 96 && _Alignof(gm_t0mfir12leg2g) == 8 && offsetof(gm_t0mfir12leg2g, f0) == 0 && offsetof(gm_t0mfir12leg2g, f1) == 88, "layout of `?Expr`");
struct gm_fr_base_name_6a6hn9r {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_binding_hi7f9ij {
    gm_frame h;
    gm_trqfvmjtt4sij l6; /* names */
    gm_trqfvmjtt4sij l12; /* members */
    gm_t79rbdv4rafe5 l17; /* blk */
    gm_trqfvmjtt4sij l22; /* body */
    gm_trqfvmjtt4sij l25; /* body */
    gm_trqfvmjtt4sij l27; /* body */
    gm_tc0sbs1tp6d7f l30; /* op */
    gm_trqfvmjtt4sij l32; /* body */
    gm_t7rh4gj19r55v l35; /* m */
    gm_t2gtbh39ptekd l38; /* k */
    gm_b0_1 ret;
};
struct gm_fr_braced_r53kp2g {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_collect_items_bck8of3 {
    gm_frame h;
    gm_tvpnktv5fo8nl l6; /* item */
    gm_t7bjbmukv26e7 l7; /* w */
    gm_tv2uf4leil5hq l14; /* b */
    gm_b0_1 ret;
};
struct gm_fr_describe_read_error_uikkr59 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_diagnostics_n3kkhf4 {
    gm_frame h;
    gm_t2792tsg5eg72 ret;
};
struct gm_fr_empty_slice_1th396a {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_8jt91p4 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ab79bmt_3 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_n7sb6c8_3 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_fields_30hs0i5 {
    gm_frame h;
    gm_tfc3oljlofib6 l7; /* m */
    gm_t24301fas1b2a l8; /* f */
    gm_trqfvmjtt4sij l9; /* names */
    gm_t7bjbmukv26e7 l13; /* w */
    gm_b0_1 ret;
};
struct gm_fr_file_o15fqku {
    gm_frame h;
    void * ret;
};
struct gm_fr_find_open_brace_eomst48 {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_handler_decl_bvaup21 {
    gm_frame h;
    gm_tc3oht4ksth9d l7; /* e */
    gm_b0_1 ret;
};
struct gm_fr_has_errors_if7ai0m {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_init_text_is_primitive_86n04kn {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_upper_apd69if {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_join_braced_ab65ll8 {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* out */
    gm_b16_8 ret;
};
struct gm_fr_join_e7fm1oo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_lex_dc8mbmf {
    gm_frame h;
    gm_trd6bg8is1v4n l3; /* lx */
    gm_tr8dvlu9ik4gj ret;
};
struct gm_fr_list_016fo30_3 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_792g2go_3 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_bvkp9ir {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_epvducg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_prbqo9j {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_load_module_qmml7un {
    gm_frame h;
    gm_t9672f4jnou6n l3; /* r */
    gm_tflfe6l4hnrb7 ret;
};
struct gm_fr_location_3jufeug {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_member_doc_9b4clb2 {
    gm_frame h;
    gm_trqfvmjtt4sij l7; /* lines */
    gm_b0_1 ret;
};
struct gm_fr_missing_6dl2vil {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* out */
    gm_tk50ot32aqf1o l4; /* d */
    gm_b16_8 ret;
};
struct gm_fr_module_doc_noqdur2 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* lines */
    gm_b16_8 ret;
};
struct gm_fr_module_of_a4sic60 {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* public */
    gm_tk50ot32aqf1o l6; /* d */
    gm_trqfvmjtt4sij l7; /* kept */
    gm_tk50ot32aqf1o l10; /* d */
    gm_tud7oovfvnf43 ret;
};
struct gm_fr_noun_pdqc588 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_read_file_dac5brv {
    gm_frame h;
    gm_tr8dvlu9ik4gj l4; /* lexed */
    gm_tpi91a971gh2v l8; /* cx */
    gm_t5ghc3lnpmg2h ret;
};
struct gm_fr_reader_rcu54pm {
    gm_frame h;
    gm_t9672f4jnou6n ret;
};
struct gm_fr_render_diagnostics_65a9o86 {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* out */
    gm_t4gut2c9fj1kt l5; /* d */
    gm_b16_8 ret;
};
struct gm_fr_report_eardf07 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_reverse_cgmjfos {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_section_9ecia9e {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_source_map_4v8dg4b {
    gm_frame h;
    gm_tin97tt4ud0s1 ret;
};
struct gm_fr_strip_attrs_g0rb77v {
    gm_frame h;
    void * ret;
};
struct gm_fr_strip_one_space_smv2rv8 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_text_epcdloe {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_trim_ebi5drb {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_valid_up_to_8vv3poh {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_variants_e6ibj0m {
    gm_frame h;
    gm_t03a3imodqi32 l6; /* m */
    gm_tnljbma3s3h16 l7; /* v */
    gm_t7bjbmukv26e7 l8; /* w */
    gm_b0_1 ret;
};
struct gm_fr_with_attrs_429rjgv {
    gm_frame h;
    gm_trqfvmjtt4sij l4; /* out */
    gm_tkacitepg5fk0 l8; /* a */
    gm_b16_8 ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eqb44cb3cf1n(const uint8_t *a, const uint8_t *b);
inline bool gm_eqjmccqdb90f(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) uint32_t gm_f_add_pva1tcq(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_base_name_6a6hn9r(void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_binding_hi7f9ij(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, const void * a3, void * a4);
__attribute__((aligned(4))) gm_b16_8 gm_f_braced_r53kp2g(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, uint32_t a2, gm_b16_8 a3, uint8_t a4);
__attribute__((aligned(4))) gm_b0_1 gm_f_collect_items_bck8of3(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, const void * a2, void * a3);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_describe_read_error_uikkr59(void *env, gm_node *cx, gm_node *inst, gm_tj5jn79kq9pnm a0);
inline __attribute__((aligned(4))) void gm_f_diagnostics_n3kkhf4(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_1th396a(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8jt91p4(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ab79bmt_3(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_n7sb6c8_3(void);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_fields_30hs0i5(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, void * a2, void * a3, void * a4);
inline __attribute__((aligned(4))) void * gm_f_file_o15fqku(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_nab29co(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint64_t gm_f_find_open_brace_eomst48(gm_b16_8 a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_format_04pispc(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_0gmpchc(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_4qd139f(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_akfobpm(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_buut5mk(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_cpfjqa5(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_eu309t0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_f6rro2t(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_g4c3lqa(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_njo4244(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_handler_decl_bvaup21(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, const void * a3, void * a4);
inline __attribute__((aligned(4))) uint8_t gm_f_has_errors_if7ai0m(void * a0);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_index_of_hdeg351(gm_b16_8 a0, uint8_t a1);
inline __attribute__((aligned(4))) uint8_t gm_f_init_text_is_primitive_86n04kn(void * a0, void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_upper_apd69if(gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_join_braced_ab65ll8(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, uint8_t a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_join_e7fm1oo(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_join_sfknljb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_lex_dc8mbmf(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint32_t a0, gm_b16_8 a1, void * a2);
__attribute__((aligned(4))) gm_cs9ufftihtbpo2 gm_f_line_col_9vn24mu(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_lines_of_t8v251a(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_list_016fo30_3(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_792g2go_3(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_bvkp9ir(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_prbqo9j(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_load_module_qmml7un(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_location_3jufeug(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_member_doc_9b4clb2(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2, uint32_t a3);
__attribute__((aligned(4))) gm_b16_8 gm_f_missing_6dl2vil(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_module_doc_noqdur2(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_module_of_a4sic60(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_new_lexer_dm12g12(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint32_t a0, gm_b16_8 a1, void * a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_noun_pdqc588(gm_t9qkh0rsooc8d a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_os_error_text_319eh6h(void *env, gm_node *cx, gm_node *inst, gm_toe5fmr3vvf4d a0);
__attribute__((aligned(4))) void gm_f_parse_file_70g7vok(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint32_t a0, void * a1, void * a2, void * a3);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_m55rf90_3(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_rda4o9n_3(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_t83rg2m(void * a0, uint8_t a1);
__attribute__((aligned(4))) void gm_f_read_all_r9rh9qd(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_read_file_dac5brv(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_reader_rcu54pm(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b16_8 gm_f_render_diagnostics_65a9o86(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_render_pi7hvmf(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t9qkh0rsooc8d a2, void * a3);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_reverse_cgmjfos(gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_reverse_range_kcrihc4(gm_b16_8 a0, uint64_t a1, uint64_t a2);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_rfind_ai5s2js(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_run_v26is74(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_section_9ecia9e(gm_t9qkh0rsooc8d a0);
__attribute__((aligned(4))) void gm_f_sort_s8jqb1h(void * a0);
inline __attribute__((aligned(4))) void gm_f_source_map_4v8dg4b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b16_8 gm_f_squash_2iiihih(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) uint8_t gm_f_starts_with_6vvt01e(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void * gm_f_strip_attrs_g0rb77v(void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_strip_one_space_smv2rv8(gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_strip_prefix_b4949s8(void *gm_out, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_strip_suffix_8155gbi(void *gm_out, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_text_epcdloe(void * a0, gm_cseahvg8v7p0vs a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_trim_ebi5drb(gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_trim_end_2059dot(gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_trim_start_lj2dhj3(gm_b16_8 a0);
__attribute__((aligned(4))) uint8_t gm_f_utf8_valid_a8oc1t8(gm_b16_8 a0);
__attribute__((aligned(4))) uint64_t gm_f_valid_up_to_8vv3poh(gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_variants_e6ibj0m(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, void * a2, void * a3);
__attribute__((aligned(4))) gm_b16_8 gm_f_with_attrs_429rjgv(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_7oqfkkd(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_8cmm737(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_aaqg4k2(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_cdouvhd(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_kcc65a2(void * a0, gm_tnpifv20irnph a1);
extern uint8_t gm_cdhuk7ib5ef[272] __attribute__((aligned(8)));
extern uint8_t gm_cejn1dj0054[12] __attribute__((aligned(4)));
extern uint8_t gm_cramav0v1j7[16] __attribute__((aligned(8)));
extern const uint8_t gm_s05fn63o3te82[13];
extern const uint8_t gm_s2737i1iq57nf[5];
extern const uint8_t gm_s2cd81u5542io[10];
extern const uint8_t gm_s3h586tvdle6o[31];
extern const uint8_t gm_s3p0rbs9hc3lg[2];
extern const uint8_t gm_s64skgbd4ii1k[10];
extern const uint8_t gm_s8bi9iapfhnht[11];
extern const uint8_t gm_s8dbdqj1sqibj[3];
extern const uint8_t gm_s8p1h96ji9kh0[6];
extern const uint8_t gm_sa05gpeedjved[8];
extern const uint8_t gm_sa0amdcmvej1l[14];
extern const uint8_t gm_sah2hl0iapccb[8];
extern const uint8_t gm_sbhui4f7fb5q5[9];
extern const uint8_t gm_sc49g68j2cekh[7];
extern const uint8_t gm_sg3kpa3h16no3[7];
extern const uint8_t gm_sg5gdnrith2i6[3];
extern const uint8_t gm_sh40na9qbga3l[2];
extern const uint8_t gm_shanrrdljtv7g[4];
extern const uint8_t gm_sic5vsema0r36[8];
extern const uint8_t gm_sidpdvffdra02[14];
extern const uint8_t gm_sj8ni1it038ec[18];
extern const uint8_t gm_sjig620oug367[9];
extern const uint8_t gm_sjss9mvc5fcod[9];
extern const uint8_t gm_sjtiiac2ha9i1[8];
extern const uint8_t gm_skr6p9lfc5ftp[11];
extern const uint8_t gm_sl1tolq1dm391[10];
extern const uint8_t gm_sm8obe455p2d9[3];
extern const uint8_t gm_so514oj5nbhcu[2];
extern const uint8_t gm_sopiqj93d6nu4[7];
extern const uint8_t gm_sp21r812mg46a[4];
extern const uint8_t gm_sq0dqiq83b924[9];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src3onm9035msn9[];
extern const char gm_src8pvk8vd1qnr1[];
extern const char gm_srciagdq75um1d5[];
extern const char gm_srcrgc9bkquf6nr[];
extern const char gm_srcv1nri2247d2r[];
extern const uint8_t gm_st3ja0nqi37bv[2];
extern const uint8_t gm_sv52j7o6hr9j8[10];
extern const uint8_t gm_svklio0qebpog[9];
extern const uint8_t gm_svqdrhms9i231[7];
extern const uint8_t gm_svt17hrer9pjd[7];
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    return n == 0 || memcmp(pa, pb, n * 1) == 0;
}
inline bool gm_eqb44cb3cf1n(const uint8_t *a, const uint8_t *b) {
    if (a[1] != b[1]) return false;
    return !a[1] || gm_eqjmccqdb90f(a, b);
}
inline bool gm_eqjmccqdb90f(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint8_t, a) == GM_LD(uint8_t, b);
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
inline __attribute__((aligned(4))) void gm_f_diagnostics_n3kkhf4(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_diagnostics_n3kkhf4 F;
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memset((uint8_t *)&(*(gm_t2792tsg5eg72 *)gm_out) + 60, 0, 4);
    gm_trqfvmjtt4sij t1;
    gm_f_list_prbqo9j((void *)&t1, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_t2792tsg5eg72 *)gm_out) + 0, t1);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_t2792tsg5eg72 *)gm_out) + 40, ((uint64_t)0ULL));
    GM_ST(uint64_t, (uint8_t *)&(*(gm_t2792tsg5eg72 *)gm_out) + 48, ((uint64_t)0ULL));
    GM_ST(uint32_t, (uint8_t *)&(*(gm_t2792tsg5eg72 *)gm_out) + 56, ((uint32_t)0ULL));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_1th396a(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_1th396a F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 184)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8jt91p4(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_8jt91p4 F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 80)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ab79bmt_3(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_ab79bmt_3 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_n7sb6c8_3(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_n7sb6c8_3 F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 136)));
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
inline __attribute__((aligned(4))) void * gm_f_file_o15fqku(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_file_o15fqku F;
    void * gm_l0; /* sm */
    uint32_t gm_l1; /* id */
    GM_STACK_CHECK_SHALLOW(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint32_t t1 = gm_l1;
    uint64_t t2 = (uint64_t)t1;
    uint64_t t3 = (uintptr_t)t2;
    void * t4 = gm_l0;
    uint64_t t5 = GM_LD(uintptr_t, ((((uint8_t *)t4) + 0) + 0) + 8);
    if (t3 >= (uintptr_t)(t5)) gm_trap_index(t3, t5, gm_srcv1nri2247d2r, 78, 75);
    void * t6 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t4) + 0) + 0)) + t3 * 80);
    F.ret = t6;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_has_errors_if7ai0m(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_has_errors_if7ai0m F;
    void * gm_l0; /* ds */
    GM_STACK_CHECK_SHALLOW(&F, 27u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    uint64_t t2 = GM_LD(uint64_t, (((uint8_t *)t1) + 40));
    uint8_t t3 = (t2 > ((uint64_t)0ULL));
    F.ret = t3;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_init_text_is_primitive_86n04kn(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_init_text_is_primitive_86n04kn F;
    void * gm_l0; /* cx */
    void * gm_l1; /* init */
    GM_STACK_CHECK_SHALLOW(&F, 624u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = gm_l1;
    gm_tl874t2atnlh3 t3 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t2) + 72));
    gm_b16_8 t4 = gm_f_text_epcdloe(t1, GM_CONV(gm_cseahvg8v7p0vs, t3));
    gm_tu0fv4fh3er3s t5 = GM_LD(gm_tu0fv4fh3er3s, gm_cdhuk7ib5ef);
    gm_tu0fv4fh3er3s t6 = t5;
    uint8_t t7 = 0;
    gm_b16_8 t8 = t4;
    for (uintptr_t i = 0; i < (uintptr_t)(17); i++) if (gm_eq0iqiubfm7e(((uint8_t *)&t6) + i * 16, ((uint8_t *)&t8))) { t7 = 1; break; }
    uint8_t t9 = (uint8_t)(t7);
    F.ret = t9;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_upper_apd69if(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_upper_apd69if F;
    gm_b16_8 gm_l0; /* name */
    uint8_t gm_l1; /* <tmp> */
    GM_STACK_CHECK_SHALLOW(&F, 52u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t1 = 0;
    uint64_t t2 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
    uint8_t t3 = (t2 > ((uint64_t)0ULL));
    if (t3) {
        {
            uint64_t t5 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            if (t5 >= (uintptr_t)(t6)) gm_trap_index(t5, t6, gm_srciagdq75um1d5, 385, 58);
            uint8_t t7 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t5 * 1));
            gm_l1 = t7;
        }
        uint8_t t8 = 0;
        uint8_t t9 = gm_l1;
        uint8_t t10 = (((uint8_t)65ULL) <= t9);
        if (t10) {
            uint8_t t11 = gm_l1;
            uint8_t t12 = (t11 <= ((uint8_t)90ULL));
            t8 = t12;
        } else t8 = 0;
        uint8_t t13 = t8;
        t1 = t13;
    } else t1 = 0;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_join_e7fm1oo(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_join_e7fm1oo F;
    gm_b16_8 gm_l0; /* parts */
    gm_b16_8 gm_l1; /* sep */
    GM_STACK_CHECK(&F, 192u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_b16_8 t1 = gm_l0;
    gm_b16_8 t2 = gm_l1;
    gm_tabb710d9mq2e t3;
    gm_f_join_sfknljb((void *)&t3, (void *)NULL, cx, NULL, t1, t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tabb710d9mq2e t4 = t3;
    if (((((uint8_t *)&t4))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t4) + 8, gm_src8pvk8vd1qnr1, 121, 73);
    gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&t4) + 8));
    gm_b16_8 t6 = t5;
    gm_b16_8 t7 = GM_LD(gm_b16_8, ((uint8_t *)&t6));
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
inline __attribute__((aligned(4))) void gm_f_lex_dc8mbmf(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint32_t a0, gm_b16_8 a1, void * a2) {
    struct gm_fr_lex_dc8mbmf F;
    uint32_t gm_l0; /* file */
    gm_b16_8 gm_l1; /* src */
    void * gm_l2; /* diags */
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        uint32_t t2 = gm_l0;
        gm_b16_8 t3 = gm_l1;
        void * t4 = gm_l2;
        gm_f_new_lexer_dm12g12((void *)&F.l3, (void *)NULL, cx, NULL, t2, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t5 = (void *)((uint8_t *)&F.l3);
        gm_f_run_v26is74((void *)&(*(gm_tr8dvlu9ik4gj *)gm_out), (void *)NULL, cx, NULL, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
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
inline __attribute__((aligned(4))) void gm_f_list_016fo30_3(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_016fo30_3 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_n7sb6c8_3();
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
inline __attribute__((aligned(4))) void gm_f_list_792g2go_3(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_792g2go_3 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_ab79bmt_3();
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
inline __attribute__((aligned(4))) void gm_f_list_bvkp9ir(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_bvkp9ir F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_8jt91p4();
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
inline __attribute__((aligned(4))) void gm_f_list_prbqo9j(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_prbqo9j F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_1th396a();
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
inline __attribute__((aligned(4))) void gm_f_reverse_cgmjfos(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_reverse_cgmjfos F;
    gm_b16_8 gm_l0; /* s */
    GM_STACK_CHECK_SHALLOW(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t2 = gm_l0;
        uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        gm_f_reverse_range_kcrihc4(t2, ((uint64_t)0ULL), t3);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_source_map_4v8dg4b(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_source_map_4v8dg4b F;
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_trqfvmjtt4sij t1;
    gm_f_list_bvkp9ir((void *)&t1, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_tin97tt4ud0s1 *)gm_out) + 0, t1);
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
inline __attribute__((aligned(4))) void * gm_f_strip_attrs_g0rb77v(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_strip_attrs_g0rb77v F;
    void * gm_l0; /* e */
    void * gm_l1; /* inner */
    GM_STACK_CHECK(&F, 81u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    void * t2;
    uint8_t t5 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    if (t5 != ((uint8_t)35ULL)) goto mn4;
    void * t6 = GM_LD(void *, (((((uint8_t *)t1) + 8) + 8) + 16));
    gm_l1 = t6;
    void * t7 = gm_l1;
    void * t8 = gm_f_strip_attrs_g0rb77v(t7);
    t2 = t8;
    goto me3;
mn4:;
    void * t10 = gm_l0;
    t2 = t10;
    goto me3;
mn9:;
    gm_trap("no match arm matched the scrutinee", gm_srciagdq75um1d5, 493, 61);
me3:;
    F.ret = t2;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_strip_one_space_smv2rv8(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_strip_one_space_smv2rv8 F;
    gm_b16_8 gm_l0; /* s */
    GM_STACK_CHECK_SHALLOW(&F, 176u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t1 = gm_l0;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s3p0rbs9hc3lg));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(1));
    gm_th2lqbo77pvsc t3;
    gm_f_strip_prefix_b4949s8((void *)&t3, t1, t2);
    gm_th2lqbo77pvsc t4 = t3;
    gm_b16_8 t5;
    if (((((uint8_t *)&t4))[16] != 0)) {
        gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)&t4) + 0));
        t5 = t6;
    } else {
        gm_b16_8 t7 = gm_l0;
        t5 = t7;
    }
    F.ret = t5;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_text_epcdloe(void * a0, gm_cseahvg8v7p0vs a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_text_epcdloe F;
    void * gm_l0; /* cx */
    gm_tl874t2atnlh3 gm_l1; /* s */
    GM_STACK_CHECK_SHALLOW(&F, 132u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_tl874t2atnlh3, a1);
    void * t1 = gm_l0;
    uint64_t t2 = GM_LD(uintptr_t, (((uint8_t *)t1) + 0) + 8);
    void * t3 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t1) + 0)));
    uint32_t t4 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 4));
    uint64_t t5 = (uint64_t)t4;
    uint64_t t6 = (uintptr_t)t5;
    uint32_t t7 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 8));
    uint64_t t8 = (uint64_t)t7;
    uint64_t t9 = (uintptr_t)t8;
    if (t6 > t9 || t9 > (uintptr_t)t2) gm_trap_slice(t6, t9, t2, gm_srciagdq75um1d5, 225, 56);
    gm_b16_8 t10;
    GM_ST(void *, (uint8_t *)&t10, (void *)(((uint8_t *)t3 + t6 * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t10 + 8, (uintptr_t)(t9 - t6));
    F.ret = t10;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_trim_ebi5drb(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_trim_ebi5drb F;
    gm_b16_8 gm_l0; /* s */
    GM_STACK_CHECK_SHALLOW(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t1 = gm_l0;
    gm_b16_8 t2 = gm_f_trim_start_lj2dhj3(t1);
    gm_b16_8 t3 = gm_f_trim_end_2059dot(t2);
    F.ret = t3;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_base_name_6a6hn9r(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_base_name_6a6hn9r F;
    void * gm_l0; /* cx */
    void * gm_l1; /* e */
    void * gm_l2; /* inner */
    void * gm_l3; /* inner */
    void * gm_l4; /* callee */
    GM_STACK_CHECK(&F, 264u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l1;
    gm_b16_8 t2;
    uint8_t t9 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    switch (t9) { case ((uint8_t)0ULL): goto ma7; case ((uint8_t)1ULL): goto ma7; case ((uint8_t)2ULL): goto ma7; case ((uint8_t)3ULL): goto ma7; case ((uint8_t)4ULL): goto ma7; case ((uint8_t)5ULL): goto ma7; case ((uint8_t)6ULL): goto ma7; case ((uint8_t)7ULL): goto ma7; case ((uint8_t)8ULL): goto ma7; case ((uint8_t)9ULL): goto ma5; case ((uint8_t)10ULL): goto ma7; case ((uint8_t)11ULL): goto ma7; case ((uint8_t)12ULL): goto ma7; case ((uint8_t)13ULL): goto ma7; case ((uint8_t)14ULL): goto ma7; case ((uint8_t)15ULL): goto ma7; case ((uint8_t)16ULL): goto ma7; case ((uint8_t)17ULL): goto ma7; case ((uint8_t)18ULL): goto ma7; case ((uint8_t)19ULL): goto ma6; case ((uint8_t)20ULL): goto ma7; case ((uint8_t)21ULL): goto ma7; case ((uint8_t)22ULL): goto ma7; case ((uint8_t)23ULL): goto ma7; case ((uint8_t)24ULL): goto ma7; case ((uint8_t)25ULL): goto ma4; case ((uint8_t)26ULL): goto ma7; case ((uint8_t)27ULL): goto ma7; case ((uint8_t)28ULL): goto ma7; case ((uint8_t)29ULL): goto ma7; case ((uint8_t)30ULL): goto ma7; case ((uint8_t)31ULL): goto ma7; case ((uint8_t)32ULL): goto ma7; case ((uint8_t)33ULL): goto ma7; case ((uint8_t)34ULL): goto ma7; case ((uint8_t)35ULL): goto ma7; case ((uint8_t)36ULL): goto ma7; case ((uint8_t)37ULL): goto ma7; case ((uint8_t)38ULL): goto ma7; case ((uint8_t)39ULL): goto ma7; case ((uint8_t)40ULL): goto ma7; case ((uint8_t)41ULL): goto ma7; case ((uint8_t)42ULL): goto ma7; case ((uint8_t)43ULL): goto ma7; case ((uint8_t)44ULL): goto ma7; case ((uint8_t)45ULL): goto ma7; case ((uint8_t)46ULL): goto ma7; case ((uint8_t)47ULL): goto ma7; case ((uint8_t)48ULL): goto ma7; case ((uint8_t)49ULL): goto ma7; case ((uint8_t)50ULL): goto ma7; default: goto ma7; }
ma4:;
    uint8_t t11 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    if (t11 != ((uint8_t)25ULL)) goto mn10;
    void * t12 = GM_LD(void *, (((((uint8_t *)t1) + 8) + 8) + 8));
    gm_l2 = t12;
    void * t13 = gm_l0;
    void * t14 = gm_l2;
    gm_b16_8 t15 = gm_f_base_name_6a6hn9r(t13, t14);
    t2 = t15;
    goto me3;
mn10:;
ma5:;
    uint8_t t17 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    if (t17 != ((uint8_t)9ULL)) goto mn16;
    void * t18 = GM_LD(void *, ((((uint8_t *)t1) + 8) + 8));
    gm_l3 = t18;
    void * t19 = gm_l0;
    void * t20 = gm_l3;
    gm_b16_8 t21 = gm_f_base_name_6a6hn9r(t19, t20);
    t2 = t21;
    goto me3;
mn16:;
ma6:;
    uint8_t t23 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    if (t23 != ((uint8_t)19ULL)) goto mn22;
    void * t24 = GM_LD(void *, (((((uint8_t *)t1) + 8) + 8) + 0));
    gm_l4 = t24;
    void * t25 = gm_l0;
    void * t26 = gm_l4;
    gm_b16_8 t27 = gm_f_base_name_6a6hn9r(t25, t26);
    t2 = t27;
    goto me3;
mn22:;
ma7:;
    void * t29 = gm_l0;
    void * t30 = gm_l1;
    gm_tl874t2atnlh3 t31 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t30) + 72));
    gm_b16_8 t32 = gm_f_text_epcdloe(t29, GM_CONV(gm_cseahvg8v7p0vs, t31));
    t2 = t32;
    goto me3;
mn28:;
ma8:;
    gm_trap("no match arm matched the scrutinee", gm_srciagdq75um1d5, 278, 65);
me3:;
    F.ret = t2;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b0_1 gm_f_binding_hi7f9ij(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, const void * a3, void * a4) {
    struct gm_fr_binding_hi7f9ij F;
    void * gm_l0; /* cx */
    void * gm_l1; /* item */
    void * gm_l2; /* b */
    gm_th2lqbo77pvsc gm_l3; /* cond */
    void * gm_l4; /* out */
    uint64_t gm_l5; /* name_lo */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    gm_t08olq97rscn3 gm_l9; /* n */
    gm_b16_8 gm_l10; /* name */
    void * gm_l11; /* init */
    gm_b16_8 gm_l13; /* label */
    gm_t9qkh0rsooc8d gm_l14; /* kind */
    gm_b16_8 gm_l15; /* sig */
    void * gm_l16; /* f */
    void * gm_l18; /* e */
    uint64_t gm_l19; /* end */
    gm_b16_8 gm_l20; /* t */
    void * gm_l21; /* s */
    uint8_t gm_l23; /* hidden */
    void * gm_l24; /* u */
    void * gm_l26; /* e */
    gm_b16_8 gm_l28; /* <tmp> */
    uint64_t gm_l29; /* <tmp> */
    void * gm_l31; /* f */
    gm_b16_8 gm_l33; /* <tmp> */
    uint64_t gm_l34; /* <tmp> */
    uint64_t gm_l36; /* open */
    gm_b16_8 gm_l37; /* head */
    uint8_t gm_l39; /* typed_value */
    void * gm_l40; /* t */
    gm_b16_8 gm_l41; /* full */
    void * gm_l42; /* t */
    GM_STACK_CHECK(&F, 3631u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l4 = a4;
    {
        uint64_t t2 = (uintptr_t)((uint64_t)0ULL);
        void * t3 = gm_l2;
        uint64_t t4 = GM_LD(uintptr_t, (((uint8_t *)t3) + 8) + 8);
        if (t2 >= (uintptr_t)(t4)) gm_trap_index(t2, t4, gm_srciagdq75um1d5, 286, 22);
        uint32_t t5 = GM_LD(uint32_t, ((((uint8_t *)GM_LD(void *, (((uint8_t *)t3) + 8)) + t2 * 32) + 16) + 4));
        uint64_t t6 = (uint64_t)t5;
        gm_l5 = t6;
    }
    {
        gm_f_list_epvducg((void *)&F.l6, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            void * t8 = gm_l2;
            gm_b16_8 t9 = GM_LD(gm_b16_8, (((uint8_t *)t8) + 8));
            gm_l7 = t9;
        }
        {
            gm_l8 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t11 = gm_l8;
                uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                uint8_t t13 = (t11 < t12);
                if (!t13) goto lb10;
                {
                    uint64_t t16 = gm_l8;
                    uint64_t t17 = (uintptr_t)t16;
                    uint64_t t18 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                    gm_t08olq97rscn3 t21 = GM_LD(gm_t08olq97rscn3, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t17 * 32));
                    gm_l9 = t21;
                    {
                        void * t23 = (void *)((uint8_t *)&F.l6);
                        void * t24 = gm_l0;
                        gm_tl874t2atnlh3 t25 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l9) + 16));
                        gm_b16_8 t26 = gm_f_text_epcdloe(t24, GM_CONV(gm_cseahvg8v7p0vs, t25));
                        gm_tftb1odc739t5 t27 = gm_f_push_3ef746b(t23, t26);
                        gm_tftb1odc739t5 t28 = t27;
                        if (((((uint8_t *)&t28))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t28) + 1, gm_srciagdq75um1d5, 288, 24);
                        gm_b0_1 t29 = GM_LD(gm_b0_1, (((uint8_t *)&t28) + 1));
                    }
                    goto me19;
mn20:;
                    gm_trap("no match arm matched the scrutinee", gm_srciagdq75um1d5, 288, 5);
me19:;
                }
lc10:;
                {
                    uint64_t t31 = gm_l8;
                    uint64_t r32; bool o33 = __builtin_add_overflow(t31, ((uint64_t)1ULL), &r32);
                    uint64_t t34 = r32;
                    gm_l8 = t34;
                }
            }
lb10:;
        }
    }
    {
        gm_b16_8 t35 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
        gm_b16_8 t36 = t35;
        gm_b16_8 t37 = GM_LD(gm_b16_8, ((uint8_t *)&t36));
        gm_b16_8 t38;
        GM_ST(void *, (uint8_t *)&t38, (void *)(gm_sg5gdnrith2i6));
        GM_ST(uintptr_t, (uint8_t *)&t38 + 8, (uintptr_t)(2));
        gm_b16_8 t39 = gm_f_join_e7fm1oo((void *)NULL, cx, NULL, t37, t38);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l10 = t39;
    }
    {
        void * t40 = gm_l2;
        void * t41 = GM_LD(void *, (((uint8_t *)t40) + 40));
        void * t42 = gm_f_strip_attrs_g0rb77v(t41);
        gm_l11 = t42;
    }
    {
        gm_f_list_792g2go_3((void *)&F.l12, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t43;
        GM_ST(void *, (uint8_t *)&t43, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t43 + 8, (uintptr_t)(0));
        gm_l13 = t43;
    }
    {
        gm_t9qkh0rsooc8d t44;
        GM_ST(uint8_t, ((uint8_t *)&t44 + 0), ((uint8_t)5ULL));
        gm_l14 = t44;
    }
    {
        gm_b16_8 t45;
        GM_ST(void *, (uint8_t *)&t45, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t45 + 8, (uintptr_t)(0));
        gm_l15 = t45;
    }
    {
        void * t46 = gm_l11;
        uint8_t t56 = GM_LD(uint8_t, ((((uint8_t *)t46) + 8) + 0));
        switch (t56) { case ((uint8_t)0ULL): goto ma54; case ((uint8_t)1ULL): goto ma54; case ((uint8_t)2ULL): goto ma54; case ((uint8_t)3ULL): goto ma54; case ((uint8_t)4ULL): goto ma54; case ((uint8_t)5ULL): goto ma54; case ((uint8_t)6ULL): goto ma54; case ((uint8_t)7ULL): goto ma54; case ((uint8_t)8ULL): goto ma54; case ((uint8_t)9ULL): goto ma54; case ((uint8_t)10ULL): goto ma54; case ((uint8_t)11ULL): goto ma54; case ((uint8_t)12ULL): goto ma54; case ((uint8_t)13ULL): goto ma54; case ((uint8_t)14ULL): goto ma54; case ((uint8_t)15ULL): goto ma54; case ((uint8_t)16ULL): goto ma54; case ((uint8_t)17ULL): goto ma54; case ((uint8_t)18ULL): goto ma54; case ((uint8_t)19ULL): goto ma54; case ((uint8_t)20ULL): goto ma54; case ((uint8_t)21ULL): goto ma54; case ((uint8_t)22ULL): goto ma54; case ((uint8_t)23ULL): goto ma54; case ((uint8_t)24ULL): goto ma54; case ((uint8_t)25ULL): goto ma54; case ((uint8_t)26ULL): goto ma54; case ((uint8_t)27ULL): goto ma54; case ((uint8_t)28ULL): goto ma54; case ((uint8_t)29ULL): goto ma54; case ((uint8_t)30ULL): goto ma54; case ((uint8_t)31ULL): goto ma54; case ((uint8_t)32ULL): goto ma49; case ((uint8_t)33ULL): goto ma50; case ((uint8_t)34ULL): goto ma51; case ((uint8_t)35ULL): goto ma54; case ((uint8_t)36ULL): goto ma48; case ((uint8_t)37ULL): goto ma54; case ((uint8_t)38ULL): goto ma53; case ((uint8_t)39ULL): goto ma52; case ((uint8_t)40ULL): goto ma54; case ((uint8_t)41ULL): goto ma54; case ((uint8_t)42ULL): goto ma54; case ((uint8_t)43ULL): goto ma54; case ((uint8_t)44ULL): goto ma54; case ((uint8_t)45ULL): goto ma54; case ((uint8_t)46ULL): goto ma54; case ((uint8_t)47ULL): goto ma54; case ((uint8_t)48ULL): goto ma54; case ((uint8_t)49ULL): goto ma54; case ((uint8_t)50ULL): goto ma54; default: goto ma54; }
ma48:;
        uint8_t t58 = GM_LD(uint8_t, ((((uint8_t *)t46) + 8) + 0));
        if (t58 != ((uint8_t)36ULL)) goto mn57;
        void * t59 = GM_LD(void *, ((((uint8_t *)t46) + 8) + 8));
        gm_l16 = t59;
        {
            void * t61 = gm_l16;
            uint32_t t62;
            uint8_t t65 = GM_LD(uint8_t, ((((uint8_t *)t61) + 88) + 0));
            if (t65 != ((uint8_t)0ULL)) goto mn64;
            gm_t79rbdv4rafe5 t66 = GM_LD(gm_t79rbdv4rafe5, ((((uint8_t *)t61) + 88) + 8));
            F.l17 = t66;
            uint32_t t67 = GM_LD(uint32_t, ((((uint8_t *)&F.l17) + 24) + 4));
            t62 = t67;
            goto me63;
mn64:;
            uint8_t t69 = GM_LD(uint8_t, ((((uint8_t *)t61) + 88) + 0));
            if (t69 != ((uint8_t)1ULL)) goto mn68;
            void * t70 = GM_LD(void *, ((((uint8_t *)t61) + 88) + 8));
            gm_l18 = t70;
            void * t71 = gm_l18;
            uint32_t t72 = GM_LD(uint32_t, ((((uint8_t *)t71) + 72) + 4));
            t62 = t72;
            goto me63;
mn68:;
            gm_trap("no match arm matched the scrutinee", gm_srciagdq75um1d5, 297, 26);
me63:;
            uint64_t t73 = (uint64_t)t62;
            gm_l19 = t73;
        }
        {
            void * t74 = gm_l0;
            uint64_t t75 = GM_LD(uintptr_t, (((uint8_t *)t74) + 0) + 8);
            void * t76 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t74) + 0)));
            uint64_t t77 = gm_l5;
            uint64_t t78 = (uintptr_t)t77;
            uint64_t t79 = gm_l19;
            uint64_t t80 = (uintptr_t)t79;
            if (t78 > t80 || t80 > (uintptr_t)t75) gm_trap_slice(t78, t80, t75, gm_srciagdq75um1d5, 301, 27);
            gm_b16_8 t81;
            GM_ST(void *, (uint8_t *)&t81, (void *)(((uint8_t *)t76 + t78 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t81 + 8, (uintptr_t)(t80 - t78));
            gm_b16_8 t82 = gm_f_trim_end_2059dot(t81);
            gm_l20 = t82;
        }
        {
            gm_b16_8 t83 = gm_l20;
            gm_b16_8 t84;
            GM_ST(void *, (uint8_t *)&t84, (void *)(gm_sm8obe455p2d9));
            GM_ST(uintptr_t, (uint8_t *)&t84 + 8, (uintptr_t)(2));
            gm_th2lqbo77pvsc t85;
            gm_f_strip_suffix_8155gbi((void *)&t85, t83, t84);
            gm_th2lqbo77pvsc t86 = t85;
            gm_b16_8 t87;
            if (((((uint8_t *)&t86))[16] != 0)) {
                gm_b16_8 t88 = GM_LD(gm_b16_8, (((uint8_t *)&t86) + 0));
                t87 = t88;
            } else {
                gm_b16_8 t89 = gm_l20;
                t87 = t89;
            }
            gm_b16_8 t90 = gm_f_trim_end_2059dot(t87);
            gm_l20 = t90;
        }
        {
            gm_t9qkh0rsooc8d t91;
            GM_ST(uint8_t, ((uint8_t *)&t91 + 0), ((uint8_t)7ULL));
            gm_l14 = t91;
        }
        {
            gm_b16_8 t92 = gm_l20;
            gm_l15 = t92;
        }
        goto me47;
mn57:;
ma49:;
        uint8_t t94 = GM_LD(uint8_t, ((((uint8_t *)t46) + 8) + 0));
        if (t94 != ((uint8_t)32ULL)) goto mn93;
        void * t95 = GM_LD(void *, ((((uint8_t *)t46) + 8) + 8));
        gm_l21 = t95;
        {
            gm_b16_8 t97;
            GM_ST(void *, (uint8_t *)&t97, (void *)(gm_sopiqj93d6nu4));
            GM_ST(uintptr_t, (uint8_t *)&t97 + 8, (uintptr_t)(6));
            gm_l13 = t97;
        }
        {
            gm_f_list_epvducg((void *)&F.l22, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_l23 = ((uint8_t)0);
        }
        {
            void * t98 = gm_l0;
            void * t99 = gm_l21;
            gm_b16_8 t100 = GM_LD(gm_b16_8, (((uint8_t *)t99) + 40));
            gm_b16_8 t101 = t100;
            gm_b16_8 t102 = GM_LD(gm_b16_8, ((uint8_t *)&t101));
            void * t103 = (void *)((uint8_t *)&F.l22);
            void * t104 = (void *)((uint8_t *)&F.l12);
            void * t105 = (void *)((uint8_t *)&gm_l23);
            gm_b0_1 t106 = gm_f_fields_30hs0i5((void *)NULL, cx, NULL, t98, t102, t103, t104, t105);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_t9qkh0rsooc8d t107;
            GM_ST(uint8_t, ((uint8_t *)&t107 + 0), ((uint8_t)0ULL));
            gm_l14 = t107;
        }
        {
            void * t108 = gm_l0;
            uint64_t t109 = gm_l5;
            void * t110 = gm_l21;
            uint32_t t111 = GM_LD(uint32_t, ((((uint8_t *)t110) + 56) + 4));
            gm_b16_8 t112 = GM_LD(gm_b16_8, (((uint8_t *)&F.l22) + 0));
            gm_b16_8 t113 = t112;
            gm_b16_8 t114 = GM_LD(gm_b16_8, ((uint8_t *)&t113));
            uint8_t t115 = gm_l23;
            gm_b16_8 t116 = gm_f_braced_r53kp2g((void *)NULL, cx, NULL, t108, t109, t111, t114, t115);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l15 = t116;
        }
        goto me47;
mn93:;
ma50:;
        uint8_t t118 = GM_LD(uint8_t, ((((uint8_t *)t46) + 8) + 0));
        if (t118 != ((uint8_t)33ULL)) goto mn117;
        void * t119 = GM_LD(void *, ((((uint8_t *)t46) + 8) + 8));
        gm_l24 = t119;
        {
            gm_b16_8 t121;
            GM_ST(void *, (uint8_t *)&t121, (void *)(gm_sbhui4f7fb5q5));
            GM_ST(uintptr_t, (uint8_t *)&t121 + 8, (uintptr_t)(8));
            gm_l13 = t121;
        }
        {
            gm_f_list_epvducg((void *)&F.l25, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t122 = gm_l0;
            void * t123 = gm_l24;
            gm_b16_8 t124 = GM_LD(gm_b16_8, (((uint8_t *)t123) + 48));
            gm_b16_8 t125 = t124;
            gm_b16_8 t126 = GM_LD(gm_b16_8, ((uint8_t *)&t125));
            void * t127 = (void *)((uint8_t *)&F.l25);
            void * t128 = (void *)((uint8_t *)&F.l12);
            gm_b0_1 t129 = gm_f_variants_e6ibj0m((void *)NULL, cx, NULL, t122, t126, t127, t128);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_t9qkh0rsooc8d t130;
            GM_ST(uint8_t, ((uint8_t *)&t130 + 0), ((uint8_t)1ULL));
            gm_l14 = t130;
        }
        {
            void * t131 = gm_l0;
            uint64_t t132 = gm_l5;
            void * t133 = gm_l24;
            uint32_t t134 = GM_LD(uint32_t, ((((uint8_t *)t133) + 64) + 4));
            gm_b16_8 t135 = GM_LD(gm_b16_8, (((uint8_t *)&F.l25) + 0));
            gm_b16_8 t136 = t135;
            gm_b16_8 t137 = GM_LD(gm_b16_8, ((uint8_t *)&t136));
            gm_b16_8 t138 = gm_f_braced_r53kp2g((void *)NULL, cx, NULL, t131, t132, t134, t137, ((uint8_t)0));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l15 = t138;
        }
        goto me47;
mn117:;
ma51:;
        uint8_t t140 = GM_LD(uint8_t, ((((uint8_t *)t46) + 8) + 0));
        if (t140 != ((uint8_t)34ULL)) goto mn139;
        void * t141 = GM_LD(void *, ((((uint8_t *)t46) + 8) + 8));
        gm_l26 = t141;
        {
            gm_b16_8 t143;
            GM_ST(void *, (uint8_t *)&t143, (void *)(gm_s8bi9iapfhnht));
            GM_ST(uintptr_t, (uint8_t *)&t143 + 8, (uintptr_t)(10));
            gm_l13 = t143;
        }
        {
            gm_f_list_epvducg((void *)&F.l27, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                void * t145 = gm_l26;
                gm_b16_8 t146 = GM_LD(gm_b16_8, (((uint8_t *)t145) + 24));
                gm_l28 = t146;
            }
            {
                gm_l29 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t148 = gm_l29;
                    uint64_t t149 = GM_LD(uintptr_t, ((uint8_t *)&gm_l28) + 8);
                    uint8_t t150 = (t148 < t149);
                    if (!t150) goto lb147;
                    {
                        uint64_t t153 = gm_l29;
                        uint64_t t154 = (uintptr_t)t153;
                        uint64_t t155 = GM_LD(uintptr_t, ((uint8_t *)&gm_l28) + 8);
                        gm_tc0sbs1tp6d7f t158 = GM_LD(gm_tc0sbs1tp6d7f, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l28)) + t154 * 80));
                        F.l30 = t158;
                        {
                            void * t160 = (void *)((uint8_t *)&F.l27);
                            void * t161 = gm_l0;
                            gm_tl874t2atnlh3 t162 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l30) + 64));
                            gm_b16_8 t163 = gm_f_text_epcdloe(t161, GM_CONV(gm_cseahvg8v7p0vs, t162));
                            gm_tftb1odc739t5 t164 = gm_f_push_3ef746b(t160, t163);
                            gm_tftb1odc739t5 t165 = t164;
                            if (((((uint8_t *)&t165))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t165) + 1, gm_srciagdq75um1d5, 325, 17);
                            gm_b0_1 t166 = GM_LD(gm_b0_1, (((uint8_t *)&t165) + 1));
                        }
                        {
                            void * t167 = gm_l0;
                            void * t168 = (void *)((uint8_t *)&F.l12);
                            void * t169 = gm_l0;
                            gm_tl874t2atnlh3 t170 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)&F.l30) + 0) + 16));
                            gm_b16_8 t171 = gm_f_text_epcdloe(t169, GM_CONV(gm_cseahvg8v7p0vs, t170));
                            uint32_t t172 = GM_LD(uint32_t, ((((uint8_t *)&F.l30) + 64) + 4));
                            gm_b0_1 t173 = gm_f_member_doc_9b4clb2((void *)NULL, cx, NULL, t167, t168, t171, t172);
                            if (gm_unw.kind) {
                                goto gm_ll151;
                            }
                        }
                        goto me156;
mn157:;
                        gm_trap("no match arm matched the scrutinee", gm_srciagdq75um1d5, 324, 13);
me156:;
                    }
lc147:;
                    {
                        uint64_t t175 = gm_l29;
                        uint64_t r176; bool o177 = __builtin_add_overflow(t175, ((uint64_t)1ULL), &r176);
                        uint64_t t178 = r176;
                        gm_l29 = t178;
                    }
                }
gm_ll151:;
                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb147; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc147; }
                }
                goto gm_lad;
lb147:;
            }
        }
        {
            gm_t9qkh0rsooc8d t179;
            GM_ST(uint8_t, ((uint8_t *)&t179 + 0), ((uint8_t)3ULL));
            gm_l14 = t179;
        }
        {
            void * t180 = gm_l0;
            uint64_t t181 = gm_l5;
            void * t182 = gm_l26;
            uint32_t t183 = GM_LD(uint32_t, ((((uint8_t *)t182) + 40) + 4));
            gm_b16_8 t184 = GM_LD(gm_b16_8, (((uint8_t *)&F.l27) + 0));
            gm_b16_8 t185 = t184;
            gm_b16_8 t186 = GM_LD(gm_b16_8, ((uint8_t *)&t185));
            gm_b16_8 t187 = gm_f_braced_r53kp2g((void *)NULL, cx, NULL, t180, t181, t183, t186, ((uint8_t)0));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l15 = t187;
        }
        goto me47;
mn139:;
ma52:;
        uint8_t t189 = GM_LD(uint8_t, ((((uint8_t *)t46) + 8) + 0));
        if (t189 != ((uint8_t)39ULL)) goto mn188;
        void * t190 = GM_LD(void *, ((((uint8_t *)t46) + 8) + 8));
        gm_l31 = t190;
        {
            gm_f_list_epvducg((void *)&F.l32, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                void * t193 = gm_l31;
                gm_b16_8 t194 = GM_LD(gm_b16_8, (((uint8_t *)t193) + 24));
                gm_l33 = t194;
            }
            {
                gm_l34 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t196 = gm_l34;
                    uint64_t t197 = GM_LD(uintptr_t, ((uint8_t *)&gm_l33) + 8);
                    uint8_t t198 = (t196 < t197);
                    if (!t198) goto lb195;
                    {
                        uint64_t t201 = gm_l34;
                        uint64_t t202 = (uintptr_t)t201;
                        uint64_t t203 = GM_LD(uintptr_t, ((uint8_t *)&gm_l33) + 8);
                        gm_t7rh4gj19r55v t206 = GM_LD(gm_t7rh4gj19r55v, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l33)) + t202 * 144));
                        F.l35 = t206;
                        {
                            void * t208 = (void *)((uint8_t *)&F.l32);
                            void * t209 = gm_l0;
                            gm_tl874t2atnlh3 t210 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l35) + 128));
                            gm_b16_8 t211 = gm_f_text_epcdloe(t209, GM_CONV(gm_cseahvg8v7p0vs, t210));
                            gm_tftb1odc739t5 t212 = gm_f_push_3ef746b(t208, t211);
                            gm_tftb1odc739t5 t213 = t212;
                            if (((((uint8_t *)&t213))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t213) + 1, gm_srciagdq75um1d5, 333, 34);
                            gm_b0_1 t214 = GM_LD(gm_b0_1, (((uint8_t *)&t213) + 1));
                        }
                        goto me204;
mn205:;
                        gm_trap("no match arm matched the scrutinee", gm_srciagdq75um1d5, 333, 13);
me204:;
                    }
lc195:;
                    {
                        uint64_t t216 = gm_l34;
                        uint64_t r217; bool o218 = __builtin_add_overflow(t216, ((uint64_t)1ULL), &r217);
                        uint64_t t219 = r217;
                        gm_l34 = t219;
                    }
                }
lb195:;
            }
        }
        {
            void * t220 = gm_l0;
            gm_b16_8 t221 = GM_LD(gm_b16_8, (((uint8_t *)t220) + 0));
            void * t222 = gm_l11;
            uint32_t t223 = GM_LD(uint32_t, ((((uint8_t *)t222) + 72) + 4));
            uint64_t t224 = (uint64_t)t223;
            uint64_t t225 = gm_f_find_open_brace_eomst48(t221, t224);
            gm_l36 = t225;
        }
        {
            void * t226 = gm_l0;
            uint64_t t227 = GM_LD(uintptr_t, (((uint8_t *)t226) + 0) + 8);
            void * t228 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t226) + 0)));
            uint64_t t229 = gm_l5;
            uint64_t t230 = (uintptr_t)t229;
            uint64_t t231 = gm_l36;
            uint64_t t232 = (uintptr_t)t231;
            if (t230 > t232 || t232 > (uintptr_t)t227) gm_trap_slice(t230, t232, t227, gm_srciagdq75um1d5, 335, 30);
            gm_b16_8 t233;
            GM_ST(void *, (uint8_t *)&t233, (void *)(((uint8_t *)t228 + t230 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t233 + 8, (uintptr_t)(t232 - t230));
            gm_b16_8 t234 = gm_f_trim_end_2059dot(t233);
            gm_l37 = t234;
        }
        {
            gm_t9qkh0rsooc8d t235;
            GM_ST(uint8_t, ((uint8_t *)&t235 + 0), ((uint8_t)8ULL));
            gm_l14 = t235;
        }
        {
            gm_b16_8 t236 = gm_l37;
            gm_b16_8 t237 = GM_LD(gm_b16_8, (((uint8_t *)&F.l32) + 0));
            gm_b16_8 t238 = t237;
            gm_b16_8 t239 = GM_LD(gm_b16_8, ((uint8_t *)&t238));
            gm_b16_8 t240 = gm_f_join_braced_ab65ll8((void *)NULL, cx, NULL, t236, t239, ((uint8_t)0));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l15 = t240;
        }
        goto me47;
mn188:;
ma53:;
        uint8_t t242 = GM_LD(uint8_t, ((((uint8_t *)t46) + 8) + 0));
        if (t242 != ((uint8_t)38ULL)) goto mn241;
        {
            gm_t9qkh0rsooc8d t244;
            GM_ST(uint8_t, ((uint8_t *)&t244 + 0), ((uint8_t)9ULL));
            gm_l14 = t244;
        }
        {
            void * t245 = gm_l0;
            uint64_t t246 = GM_LD(uintptr_t, (((uint8_t *)t245) + 0) + 8);
            void * t247 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t245) + 0)));
            uint64_t t248 = gm_l5;
            uint64_t t249 = (uintptr_t)t248;
            void * t250 = gm_l2;
            void * t251 = GM_LD(void *, (((uint8_t *)t250) + 40));
            uint32_t t252 = GM_LD(uint32_t, ((((uint8_t *)t251) + 72) + 8));
            uint64_t t253 = (uint64_t)t252;
            uint64_t t254 = (uintptr_t)t253;
            if (t249 > t254 || t254 > (uintptr_t)t246) gm_trap_slice(t249, t254, t246, gm_srciagdq75um1d5, 341, 19);
            gm_b16_8 t255;
            GM_ST(void *, (uint8_t *)&t255, (void *)(((uint8_t *)t247 + t249 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t255 + 8, (uintptr_t)(t254 - t249));
            gm_l15 = t255;
        }
        goto me47;
mn241:;
ma54:;
        gm_t2gtbh39ptekd t257 = GM_LD(gm_t2gtbh39ptekd, (((uint8_t *)t46) + 8));
        F.l38 = t257;
        {
            gm_l39 = ((uint8_t)0);
        }
        {
            void * t260 = gm_l2;
            void * t261 = GM_LD(void *, (((uint8_t *)t260) + 24));
            void * t262 = t261;
            if (!(GM_LD(void *, ((uint8_t *)&t262)) != 0)) goto ie259;
            void * t263 = GM_LD(void *, ((uint8_t *)&t262));
            gm_l40 = t263;
            {
                void * t265 = gm_l0;
                void * t266 = gm_l40;
                gm_tl874t2atnlh3 t267 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t266) + 72));
                gm_b16_8 t268 = gm_f_text_epcdloe(t265, GM_CONV(gm_cseahvg8v7p0vs, t267));
                gm_b16_8 t269 = t268;
                uint8_t t270 = (!((GM_LD(uintptr_t, ((uint8_t *)&t269) + 8) == 4) && memcmp(GM_LD(void *, ((uint8_t *)&t269)), gm_s2737i1iq57nf, 4) == 0));
                gm_l39 = t270;
            }
            goto ix259;
ie259:;
ix259:;
        }
        {
            void * t272 = gm_l2;
            uint8_t t273 = ((GM_LD(uint8_t, (((uint8_t *)t272) + 32) + 0) == ((uint8_t)1ULL)));
            if (!t273) goto ie271;
            {
                gm_t9qkh0rsooc8d t275;
                GM_ST(uint8_t, ((uint8_t *)&t275 + 0), ((uint8_t)6ULL));
                gm_l14 = t275;
            }
            goto ix271;
ie271:;
            uint8_t t277 = gm_l39;
            if (!t277) goto ie276;
            {
                gm_t9qkh0rsooc8d t279;
                GM_ST(uint8_t, ((uint8_t *)&t279 + 0), ((uint8_t)5ULL));
                gm_l14 = t279;
            }
            goto ix276;
ie276:;
            uint8_t t281;
            uint8_t t293 = GM_LD(uint8_t, (((uint8_t *)&F.l38) + 0));
            switch (t293) { case ((uint8_t)0ULL): goto ma291; case ((uint8_t)1ULL): goto ma291; case ((uint8_t)2ULL): goto ma291; case ((uint8_t)3ULL): goto ma291; case ((uint8_t)4ULL): goto ma291; case ((uint8_t)5ULL): goto ma291; case ((uint8_t)6ULL): goto ma291; case ((uint8_t)7ULL): goto ma291; case ((uint8_t)8ULL): goto ma291; case ((uint8_t)9ULL): goto ma291; case ((uint8_t)10ULL): goto ma291; case ((uint8_t)11ULL): goto ma291; case ((uint8_t)12ULL): goto ma291; case ((uint8_t)13ULL): goto ma291; case ((uint8_t)14ULL): goto ma291; case ((uint8_t)15ULL): goto ma291; case ((uint8_t)16ULL): goto ma291; case ((uint8_t)17ULL): goto ma291; case ((uint8_t)18ULL): goto ma291; case ((uint8_t)19ULL): goto ma291; case ((uint8_t)20ULL): goto ma291; case ((uint8_t)21ULL): goto ma291; case ((uint8_t)22ULL): goto ma291; case ((uint8_t)23ULL): goto ma291; case ((uint8_t)24ULL): goto ma283; case ((uint8_t)25ULL): goto ma284; case ((uint8_t)26ULL): goto ma285; case ((uint8_t)27ULL): goto ma286; case ((uint8_t)28ULL): goto ma287; case ((uint8_t)29ULL): goto ma288; case ((uint8_t)30ULL): goto ma289; case ((uint8_t)31ULL): goto ma290; case ((uint8_t)32ULL): goto ma291; case ((uint8_t)33ULL): goto ma291; case ((uint8_t)34ULL): goto ma291; case ((uint8_t)35ULL): goto ma291; case ((uint8_t)36ULL): goto ma291; case ((uint8_t)37ULL): goto ma291; case ((uint8_t)38ULL): goto ma291; case ((uint8_t)39ULL): goto ma291; case ((uint8_t)40ULL): goto ma291; case ((uint8_t)41ULL): goto ma291; case ((uint8_t)42ULL): goto ma291; case ((uint8_t)43ULL): goto ma291; case ((uint8_t)44ULL): goto ma291; case ((uint8_t)45ULL): goto ma291; case ((uint8_t)46ULL): goto ma291; case ((uint8_t)47ULL): goto ma291; case ((uint8_t)48ULL): goto ma291; case ((uint8_t)49ULL): goto ma291; case ((uint8_t)50ULL): goto ma291; default: goto ma291; }
ma283:;
            uint8_t t295 = GM_LD(uint8_t, (((uint8_t *)&F.l38) + 0));
            if (t295 != ((uint8_t)24ULL)) goto mn294;
            t281 = ((uint8_t)1);
            goto me282;
mn294:;
ma284:;
            uint8_t t297 = GM_LD(uint8_t, (((uint8_t *)&F.l38) + 0));
            if (t297 != ((uint8_t)25ULL)) goto mn296;
            t281 = ((uint8_t)1);
            goto me282;
mn296:;
ma285:;
            uint8_t t299 = GM_LD(uint8_t, (((uint8_t *)&F.l38) + 0));
            if (t299 != ((uint8_t)26ULL)) goto mn298;
            t281 = ((uint8_t)1);
            goto me282;
mn298:;
ma286:;
            uint8_t t301 = GM_LD(uint8_t, (((uint8_t *)&F.l38) + 0));
            if (t301 != ((uint8_t)27ULL)) goto mn300;
            t281 = ((uint8_t)1);
            goto me282;
mn300:;
ma287:;
            uint8_t t303 = GM_LD(uint8_t, (((uint8_t *)&F.l38) + 0));
            if (t303 != ((uint8_t)28ULL)) goto mn302;
            t281 = ((uint8_t)1);
            goto me282;
mn302:;
ma288:;
            uint8_t t305 = GM_LD(uint8_t, (((uint8_t *)&F.l38) + 0));
            if (t305 != ((uint8_t)29ULL)) goto mn304;
            t281 = ((uint8_t)1);
            goto me282;
mn304:;
ma289:;
            uint8_t t307 = GM_LD(uint8_t, (((uint8_t *)&F.l38) + 0));
            if (t307 != ((uint8_t)30ULL)) goto mn306;
            t281 = ((uint8_t)1);
            goto me282;
mn306:;
ma290:;
            uint8_t t309 = GM_LD(uint8_t, (((uint8_t *)&F.l38) + 0));
            if (t309 != ((uint8_t)31ULL)) goto mn308;
            t281 = ((uint8_t)1);
            goto me282;
mn308:;
ma291:;
            t281 = ((uint8_t)0);
            goto me282;
mn310:;
ma292:;
            gm_trap("no match arm matched the scrutinee", gm_srciagdq75um1d5, 350, 23);
me282:;
            if (!t281) goto ie280;
            {
                gm_t9qkh0rsooc8d t312;
                GM_ST(uint8_t, ((uint8_t *)&t312 + 0), ((uint8_t)2ULL));
                gm_l14 = t312;
            }
            goto ix280;
ie280:;
            uint8_t t314;
            uint8_t t321 = GM_LD(uint8_t, (((uint8_t *)&F.l38) + 0));
            switch (t321) { case ((uint8_t)0ULL): goto ma319; case ((uint8_t)1ULL): goto ma319; case ((uint8_t)2ULL): goto ma319; case ((uint8_t)3ULL): goto ma319; case ((uint8_t)4ULL): goto ma319; case ((uint8_t)5ULL): goto ma319; case ((uint8_t)6ULL): goto ma316; case ((uint8_t)7ULL): goto ma319; case ((uint8_t)8ULL): goto ma319; case ((uint8_t)9ULL): goto ma319; case ((uint8_t)10ULL): goto ma319; case ((uint8_t)11ULL): goto ma319; case ((uint8_t)12ULL): goto ma319; case ((uint8_t)13ULL): goto ma319; case ((uint8_t)14ULL): goto ma319; case ((uint8_t)15ULL): goto ma319; case ((uint8_t)16ULL): goto ma319; case ((uint8_t)17ULL): goto ma319; case ((uint8_t)18ULL): goto ma319; case ((uint8_t)19ULL): goto ma318; case ((uint8_t)20ULL): goto ma319; case ((uint8_t)21ULL): goto ma319; case ((uint8_t)22ULL): goto ma317; case ((uint8_t)23ULL): goto ma319; case ((uint8_t)24ULL): goto ma319; case ((uint8_t)25ULL): goto ma319; case ((uint8_t)26ULL): goto ma319; case ((uint8_t)27ULL): goto ma319; case ((uint8_t)28ULL): goto ma319; case ((uint8_t)29ULL): goto ma319; case ((uint8_t)30ULL): goto ma319; case ((uint8_t)31ULL): goto ma319; case ((uint8_t)32ULL): goto ma319; case ((uint8_t)33ULL): goto ma319; case ((uint8_t)34ULL): goto ma319; case ((uint8_t)35ULL): goto ma319; case ((uint8_t)36ULL): goto ma319; case ((uint8_t)37ULL): goto ma319; case ((uint8_t)38ULL): goto ma319; case ((uint8_t)39ULL): goto ma319; case ((uint8_t)40ULL): goto ma319; case ((uint8_t)41ULL): goto ma319; case ((uint8_t)42ULL): goto ma319; case ((uint8_t)43ULL): goto ma319; case ((uint8_t)44ULL): goto ma319; case ((uint8_t)45ULL): goto ma319; case ((uint8_t)46ULL): goto ma319; case ((uint8_t)47ULL): goto ma319; case ((uint8_t)48ULL): goto ma319; case ((uint8_t)49ULL): goto ma319; case ((uint8_t)50ULL): goto ma319; default: goto ma319; }
ma316:;
            uint8_t t323 = GM_LD(uint8_t, (((uint8_t *)&F.l38) + 0));
            if (t323 != ((uint8_t)6ULL)) goto mn322;
            t314 = ((uint8_t)1);
            goto me315;
mn322:;
ma317:;
            uint8_t t325 = GM_LD(uint8_t, (((uint8_t *)&F.l38) + 0));
            if (t325 != ((uint8_t)22ULL)) goto mn324;
            t314 = ((uint8_t)1);
            goto me315;
mn324:;
ma318:;
            uint8_t t327 = GM_LD(uint8_t, (((uint8_t *)&F.l38) + 0));
            if (t327 != ((uint8_t)19ULL)) goto mn326;
            t314 = ((uint8_t)1);
            goto me315;
mn326:;
ma319:;
            t314 = ((uint8_t)0);
            goto me315;
mn328:;
ma320:;
            gm_trap("no match arm matched the scrutinee", gm_srciagdq75um1d5, 352, 23);
me315:;
            if (!t314) goto ie313;
            void * t329 = gm_l2;
            void * t330 = GM_LD(void *, (((uint8_t *)t329) + 24));
            void * t331 = t330;
            uint8_t t332;
            if ((GM_LD(void *, ((uint8_t *)&t331)) != 0)) goto mn334;
            t332 = ((uint8_t)1);
            goto me333;
mn334:;
            t332 = ((uint8_t)0);
            goto me333;
mn335:;
            gm_trap("no match arm matched the scrutinee", gm_srciagdq75um1d5, 352, 65);
me333:;
            if (!t332) goto ie313;
            uint8_t t336 = 0;
            gm_b16_8 t337 = gm_l10;
            uint8_t t338 = gm_f_is_upper_apd69if(t337);
            if (!t338) {
                void * t339 = gm_l0;
                void * t340 = gm_l11;
                uint8_t t341 = gm_f_init_text_is_primitive_86n04kn(t339, t340);
                t336 = t341;
            } else t336 = 1;
            if (!t336) goto ie313;
            {
                gm_t9qkh0rsooc8d t343;
                GM_ST(uint8_t, ((uint8_t *)&t343 + 0), ((uint8_t)2ULL));
                gm_l14 = t343;
            }
            goto ix313;
ie313:;
            {
                gm_t9qkh0rsooc8d t345;
                GM_ST(uint8_t, ((uint8_t *)&t345 + 0), ((uint8_t)5ULL));
                gm_l14 = t345;
            }
ix313:;
ix280:;
ix276:;
ix271:;
        }
        {
            void * t346 = gm_l0;
            uint64_t t347 = GM_LD(uintptr_t, (((uint8_t *)t346) + 0) + 8);
            void * t348 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t346) + 0)));
            uint64_t t349 = gm_l5;
            uint64_t t350 = (uintptr_t)t349;
            void * t351 = gm_l2;
            void * t352 = GM_LD(void *, (((uint8_t *)t351) + 40));
            uint32_t t353 = GM_LD(uint32_t, ((((uint8_t *)t352) + 72) + 8));
            uint64_t t354 = (uint64_t)t353;
            uint64_t t355 = (uintptr_t)t354;
            if (t350 > t355 || t355 > (uintptr_t)t347) gm_trap_slice(t350, t355, t347, gm_srciagdq75um1d5, 357, 21);
            gm_b16_8 t356;
            GM_ST(void *, (uint8_t *)&t356, (void *)(((uint8_t *)t348 + t350 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t356 + 8, (uintptr_t)(t355 - t350));
            gm_l41 = t356;
        }
        {
            uint8_t t358 = 0;
            gm_b16_8 t359 = gm_l41;
            gm_tf5srh4mp4b3i t360 = gm_f_index_of_hdeg351(t359, ((uint8_t)10ULL));
            gm_tf5srh4mp4b3i t361 = t360;
            uint8_t t362;
            if (((((uint8_t *)&t361))[8] != 0)) goto mn364;
            t362 = ((uint8_t)0);
            goto me363;
mn364:;
            t362 = ((uint8_t)1);
            goto me363;
mn365:;
            gm_trap("no match arm matched the scrutinee", gm_srciagdq75um1d5, 358, 16);
me363:;
            if (!t362) {
                uint64_t t366 = GM_LD(uintptr_t, ((uint8_t *)&gm_l41) + 8);
                uint8_t t367 = (t366 > ((uint64_t)100ULL));
                t358 = t367;
            } else t358 = 1;
            if (!t358) goto ie357;
            {
                uint8_t t370 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l14) + 0) == ((uint8_t)2ULL)));
                if (!t370) goto ie369;
                {
                    gm_b16_8 t372 = gm_l10;
                    gm_tnpifv20irnph t373;
                    GM_ST(gm_b16_8, ((uint8_t *)&t373 + 0), t372);
                    gm_tabb710d9mq2e t374;
                    gm_f_format_g4c3lqa((void *)&t374, (void *)NULL, cx, NULL, t373);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tabb710d9mq2e t375 = t374;
                    if (((((uint8_t *)&t375))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t375) + 8, gm_srciagdq75um1d5, 361, 27);
                    gm_b16_8 t376 = GM_LD(gm_b16_8, (((uint8_t *)&t375) + 8));
                    gm_b16_8 t377 = t376;
                    gm_b16_8 t378 = GM_LD(gm_b16_8, ((uint8_t *)&t377));
                    gm_l15 = t378;
                }
                goto ix369;
ie369:;
                void * t380 = gm_l2;
                void * t381 = GM_LD(void *, (((uint8_t *)t380) + 24));
                void * t382 = t381;
                if (!(GM_LD(void *, ((uint8_t *)&t382)) != 0)) goto ie379;
                void * t383 = GM_LD(void *, ((uint8_t *)&t382));
                gm_l42 = t383;
                {
                    gm_t6j6smj7qoctu t385;
                    gm_b16_8 t386 = gm_l10;
                    GM_ST(gm_b16_8, (uint8_t *)&t385 + 0, t386);
                    void * t387 = gm_l0;
                    void * t388 = gm_l42;
                    gm_tl874t2atnlh3 t389 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t388) + 72));
                    gm_b16_8 t390 = gm_f_text_epcdloe(t387, GM_CONV(gm_cseahvg8v7p0vs, t389));
                    GM_ST(gm_b16_8, (uint8_t *)&t385 + 16, t390);
                    gm_tabb710d9mq2e t391;
                    gm_f_format_f6rro2t((void *)&t391, (void *)NULL, cx, NULL, &t385);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tabb710d9mq2e t392 = t391;
                    if (((((uint8_t *)&t392))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t392) + 8, gm_srciagdq75um1d5, 363, 27);
                    gm_b16_8 t393 = GM_LD(gm_b16_8, (((uint8_t *)&t392) + 8));
                    gm_b16_8 t394 = t393;
                    gm_b16_8 t395 = GM_LD(gm_b16_8, ((uint8_t *)&t394));
                    gm_l15 = t395;
                }
                goto ix379;
ie379:;
                {
                    gm_b16_8 t397 = gm_l10;
                    gm_tnpifv20irnph t398;
                    GM_ST(gm_b16_8, ((uint8_t *)&t398 + 0), t397);
                    gm_tabb710d9mq2e t399;
                    gm_f_format_g4c3lqa((void *)&t399, (void *)NULL, cx, NULL, t398);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tabb710d9mq2e t400 = t399;
                    if (((((uint8_t *)&t400))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t400) + 8, gm_srciagdq75um1d5, 365, 27);
                    gm_b16_8 t401 = GM_LD(gm_b16_8, (((uint8_t *)&t400) + 8));
                    gm_b16_8 t402 = t401;
                    gm_b16_8 t403 = GM_LD(gm_b16_8, ((uint8_t *)&t402));
                    gm_l15 = t403;
                }
ix379:;
ix369:;
            }
            goto ix357;
ie357:;
            {
                gm_b16_8 t405 = gm_l41;
                gm_l15 = t405;
            }
ix357:;
        }
        goto me47;
mn256:;
ma55:;
        gm_trap("no match arm matched the scrutinee", gm_srciagdq75um1d5, 295, 5);
me47:;
    }
    {
        void * t406 = gm_l4;
        gm_tk50ot32aqf1o t407;
        memset((uint8_t *)&t407 + 1, 0, 7);
        gm_t9qkh0rsooc8d t408 = gm_l14;
        GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t407 + 0, t408);
        gm_b16_8 t409 = gm_l10;
        GM_ST(gm_b16_8, (uint8_t *)&t407 + 8, t409);
        void * t410 = gm_l0;
        void * t411 = gm_l1;
        gm_b16_8 t412 = gm_l15;
        gm_b16_8 t413 = gm_f_with_attrs_429rjgv((void *)NULL, cx, NULL, t410, t411, &(*(gm_th2lqbo77pvsc *)((uint8_t *)a3)), t412);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t407 + 24, t413);
        void * t414 = gm_l1;
        memcpy((uint8_t *)&t407 + 40, (((uint8_t *)t414) + 16), 24);
        gm_b16_8 t415 = gm_l13;
        GM_ST(gm_b16_8, (uint8_t *)&t407 + 64, t415);
        gm_b16_8 t416 = GM_LD(gm_b16_8, (((uint8_t *)&F.l12) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t407 + 80, t416);
        void * t417 = gm_l0;
        void * t418 = gm_l1;
        uint32_t t419 = GM_LD(uint32_t, ((((uint8_t *)t418) + 4) + 4));
        gm_b16_8 t420 = gm_f_location_3jufeug((void *)NULL, cx, NULL, t417, t419);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t407 + 96, t420);
        gm_th2lqbo77pvsc t421;
        memset(&t421, 0, sizeof t421);
        GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&t407 + 112, t421);
        gm_tftb1odc739t5 t422 = gm_f_push_rda4o9n_3(t406, &t407);
        gm_tftb1odc739t5 t423 = t422;
        if (((((uint8_t *)&t423))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t423) + 1, gm_srciagdq75um1d5, 372, 5);
        gm_b0_1 t424 = GM_LD(gm_b0_1, (((uint8_t *)&t423) + 1));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_braced_r53kp2g(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, uint32_t a2, gm_b16_8 a3, uint8_t a4) {
    struct gm_fr_braced_r53kp2g F;
    void * gm_l0; /* cx */
    uint64_t gm_l1; /* name_lo */
    uint32_t gm_l2; /* lit_lo */
    gm_b16_8 gm_l3; /* body */
    uint8_t gm_l4; /* hidden */
    uint64_t gm_l5; /* open */
    gm_b16_8 gm_l6; /* head */
    GM_STACK_CHECK(&F, 275u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        void * t2 = gm_l0;
        gm_b16_8 t3 = GM_LD(gm_b16_8, (((uint8_t *)t2) + 0));
        uint32_t t4 = gm_l2;
        uint64_t t5 = (uint64_t)t4;
        uint64_t t6 = gm_f_find_open_brace_eomst48(t3, t5);
        gm_l5 = t6;
    }
    {
        void * t7 = gm_l0;
        uint64_t t8 = GM_LD(uintptr_t, (((uint8_t *)t7) + 0) + 8);
        void * t9 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t7) + 0)));
        uint64_t t10 = gm_l1;
        uint64_t t11 = (uintptr_t)t10;
        uint64_t t12 = gm_l5;
        uint64_t t13 = (uintptr_t)t12;
        if (t11 > t13 || t13 > (uintptr_t)t8) gm_trap_slice(t11, t13, t8, gm_srciagdq75um1d5, 393, 22);
        gm_b16_8 t14;
        GM_ST(void *, (uint8_t *)&t14, (void *)(((uint8_t *)t9 + t11 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(t13 - t11));
        gm_b16_8 t15 = gm_f_trim_end_2059dot(t14);
        gm_l6 = t15;
    }
    {
        gm_b16_8 t16 = gm_l6;
        gm_b16_8 t17 = gm_l3;
        uint8_t t18 = gm_l4;
        gm_b16_8 t19 = gm_f_join_braced_ab65ll8((void *)NULL, cx, NULL, t16, t17, t18);
        if (gm_unw.kind) {
            goto gm_lad;
        }
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
__attribute__((aligned(4))) gm_b0_1 gm_f_collect_items_bck8of3(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, const void * a2, void * a3) {
    struct gm_fr_collect_items_bck8of3 F;
    void * gm_l0; /* cx */
    gm_b16_8 gm_l1; /* items */
    gm_th2lqbo77pvsc gm_l2; /* cond */
    void * gm_l3; /* out */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_b16_8 gm_l8; /* c0 */
    gm_b16_8 gm_l9; /* outer */
    gm_b16_8 gm_l10; /* c */
    gm_b16_8 gm_l11; /* e */
    gm_b16_8 gm_l12; /* outer */
    gm_b16_8 gm_l13; /* c1 */
    void * gm_l15; /* h */
    GM_STACK_CHECK(&F, 1466u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    memcpy(&gm_l2, a2, sizeof gm_l2);
    gm_l3 = a3;
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
                    gm_tvpnktv5fo8nl t15 = GM_LD(gm_tvpnktv5fo8nl, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t11 * 160));
                    F.l6 = t15;
                    {
                        uint8_t t23 = GM_LD(uint8_t, ((((uint8_t *)&F.l6) + 64) + 0));
                        switch (t23) { case ((uint8_t)0ULL): goto ma19; case ((uint8_t)1ULL): goto ma18; case ((uint8_t)2ULL): goto ma20; case ((uint8_t)3ULL): goto ma21; case ((uint8_t)4ULL): goto ma21; default: goto ma21; }
ma18:;
                        uint8_t t25 = GM_LD(uint8_t, ((((uint8_t *)&F.l6) + 64) + 0));
                        if (t25 != ((uint8_t)1ULL)) goto mn24;
                        gm_t7bjbmukv26e7 t26 = GM_LD(gm_t7bjbmukv26e7, ((((uint8_t *)&F.l6) + 64) + 8));
                        F.l7 = t26;
                        {
                            void * t28 = gm_l0;
                            void * t29 = GM_LD(void *, (((uint8_t *)&F.l7) + 0));
                            gm_tl874t2atnlh3 t30 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t29) + 72));
                            gm_b16_8 t31 = gm_f_text_epcdloe(t28, GM_CONV(gm_cseahvg8v7p0vs, t30));
                            gm_b16_8 t32 = gm_f_squash_2iiihih((void *)NULL, cx, NULL, t31);
                            if (gm_unw.kind) {
                                goto gm_ll8;
                            }
                            gm_tnpifv20irnph t33;
                            GM_ST(gm_b16_8, ((uint8_t *)&t33 + 0), t32);
                            gm_tabb710d9mq2e t34;
                            gm_f_format_eu309t0((void *)&t34, (void *)NULL, cx, NULL, t33);
                            if (gm_unw.kind) {
                                goto gm_ll8;
                            }
                            gm_tabb710d9mq2e t35 = t34;
                            if (((((uint8_t *)&t35))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t35) + 8, gm_srciagdq75um1d5, 236, 23);
                            gm_b16_8 t36 = GM_LD(gm_b16_8, (((uint8_t *)&t35) + 8));
                            gm_l8 = t36;
                        }
                        {
                            gm_b16_8 t37;
                            if (!((((uint8_t *)&gm_l2))[16] != 0)) goto ie38;
                            gm_b16_8 t39 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l2));
                            gm_l9 = t39;
                            gm_t6j6smj7qoctu t41;
                            gm_b16_8 t42 = gm_l9;
                            GM_ST(gm_b16_8, (uint8_t *)&t41 + 0, t42);
                            gm_b16_8 t43 = gm_l8;
                            GM_ST(gm_b16_8, (uint8_t *)&t41 + 16, t43);
                            gm_tabb710d9mq2e t44;
                            gm_f_format_cpfjqa5((void *)&t44, (void *)NULL, cx, NULL, &t41);
                            if (gm_unw.kind) {
                                goto gm_ll8;
                            }
                            gm_tabb710d9mq2e t45 = t44;
                            if (((((uint8_t *)&t45))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t45) + 8, gm_srciagdq75um1d5, 237, 48);
                            gm_b16_8 t46 = GM_LD(gm_b16_8, (((uint8_t *)&t45) + 8));
                            gm_b16_8 t47 = t46;
                            gm_b16_8 t48 = t47;
                            gm_b16_8 t49 = GM_LD(gm_b16_8, ((uint8_t *)&t48));
                            t37 = t49;
                            goto ix38;
ie38:;
                            gm_b16_8 t51 = gm_l8;
                            gm_b16_8 t52 = t51;
                            gm_b16_8 t53 = t52;
                            gm_b16_8 t54 = GM_LD(gm_b16_8, ((uint8_t *)&t53));
                            t37 = t54;
ix38:;
                            gm_l10 = t37;
                        }
                        {
                            void * t55 = gm_l0;
                            gm_b16_8 t56 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 8));
                            gm_b16_8 t57 = t56;
                            gm_b16_8 t58 = GM_LD(gm_b16_8, ((uint8_t *)&t57));
                            gm_b16_8 t59 = gm_l10;
                            gm_th2lqbo77pvsc t60;
                            GM_ST(uint8_t, (uint8_t *)&t60 + 17, 0);
                            GM_ST(uint16_t, (uint8_t *)&t60 + 18, 0);
                            GM_ST(uint32_t, (uint8_t *)&t60 + 20, 0);
                            GM_ST(gm_b16_8, ((uint8_t *)&t60), t59);
                            ((uint8_t *)&t60)[16] = 1;
                            void * t61 = gm_l3;
                            gm_b0_1 t62 = gm_f_collect_items_bck8of3((void *)NULL, cx, NULL, t55, t58, &t60, t61);
                            if (gm_unw.kind) {
                                goto gm_ll8;
                            }
                        }
                        {
                            if (!(((((uint8_t *)&F.l7) + 24))[16] != 0)) goto ie63;
                            gm_b16_8 t64 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 24));
                            gm_l11 = t64;
                            {
                                gm_b16_8 t66;
                                if (!((((uint8_t *)&gm_l2))[16] != 0)) goto ie67;
                                gm_b16_8 t68 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l2));
                                gm_l12 = t68;
                                gm_b16_8 t70 = gm_l12;
                                gm_tnpifv20irnph t71;
                                GM_ST(gm_b16_8, ((uint8_t *)&t71 + 0), t70);
                                gm_tabb710d9mq2e t72;
                                gm_f_format_0gmpchc((void *)&t72, (void *)NULL, cx, NULL, t71);
                                if (gm_unw.kind) {
                                    goto gm_ll8;
                                }
                                gm_tabb710d9mq2e t73 = t72;
                                if (((((uint8_t *)&t73))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t73) + 8, gm_srciagdq75um1d5, 240, 53);
                                gm_b16_8 t74 = GM_LD(gm_b16_8, (((uint8_t *)&t73) + 8));
                                gm_b16_8 t75 = t74;
                                gm_b16_8 t76 = t75;
                                gm_b16_8 t77 = GM_LD(gm_b16_8, ((uint8_t *)&t76));
                                t66 = t77;
                                goto ix67;
ie67:;
                                gm_b16_8 t79;
                                GM_ST(void *, (uint8_t *)&t79, (void *)(gm_s2cd81u5542io));
                                GM_ST(uintptr_t, (uint8_t *)&t79 + 8, (uintptr_t)(9));
                                gm_b16_8 t80 = t79;
                                t66 = t80;
ix67:;
                                gm_l13 = t66;
                            }
                            {
                                void * t81 = gm_l0;
                                gm_b16_8 t82 = gm_l11;
                                gm_b16_8 t83 = t82;
                                gm_b16_8 t84 = GM_LD(gm_b16_8, ((uint8_t *)&t83));
                                gm_b16_8 t85 = gm_l13;
                                gm_th2lqbo77pvsc t86;
                                GM_ST(uint8_t, (uint8_t *)&t86 + 17, 0);
                                GM_ST(uint16_t, (uint8_t *)&t86 + 18, 0);
                                GM_ST(uint32_t, (uint8_t *)&t86 + 20, 0);
                                GM_ST(gm_b16_8, ((uint8_t *)&t86), t85);
                                ((uint8_t *)&t86)[16] = 1;
                                void * t87 = gm_l3;
                                gm_b0_1 t88 = gm_f_collect_items_bck8of3((void *)NULL, cx, NULL, t81, t84, &t86, t87);
                                if (gm_unw.kind) {
                                    goto gm_ll8;
                                }
                            }
                            goto ix63;
ie63:;
ix63:;
                        }
                        goto me17;
mn24:;
ma19:;
                        uint8_t t90 = GM_LD(uint8_t, ((((uint8_t *)&F.l6) + 64) + 0));
                        if (t90 != ((uint8_t)0ULL)) goto mn89;
                        gm_tv2uf4leil5hq t91 = GM_LD(gm_tv2uf4leil5hq, ((((uint8_t *)&F.l6) + 64) + 8));
                        F.l14 = t91;
                        {
                            uint8_t t94 = ((GM_LD(uint8_t, (((uint8_t *)&F.l6) + 56) + 0) == ((uint8_t)2ULL)));
                            if (!t94) goto ie93;
                            {
                                void * t96 = gm_l0;
                                void * t97 = (void *)((uint8_t *)&F.l6);
                                void * t98 = (void *)((uint8_t *)&F.l14);
                                void * t99 = gm_l3;
                                gm_b0_1 t100 = gm_f_binding_hi7f9ij((void *)NULL, cx, NULL, t96, t97, t98, &(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l2)), t99);
                                if (gm_unw.kind) {
                                    goto gm_ll8;
                                }
                            }
                            goto ix93;
ie93:;
ix93:;
                        }
                        goto me17;
mn89:;
ma20:;
                        uint8_t t102 = GM_LD(uint8_t, ((((uint8_t *)&F.l6) + 64) + 0));
                        if (t102 != ((uint8_t)2ULL)) goto mn101;
                        void * t103 = GM_LD(void *, ((((uint8_t *)&F.l6) + 64) + 8));
                        gm_l15 = t103;
                        void * t104 = gm_l0;
                        void * t105 = (void *)((uint8_t *)&F.l6);
                        void * t106 = gm_l15;
                        void * t107 = gm_l3;
                        gm_b0_1 t108 = gm_f_handler_decl_bvaup21((void *)NULL, cx, NULL, t104, t105, t106, &(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l2)), t107);
                        if (gm_unw.kind) {
                            goto gm_ll8;
                        }
                        goto me17;
mn101:;
ma21:;
                        goto me17;
mn109:;
ma22:;
                        gm_trap("no match arm matched the scrutinee", gm_srciagdq75um1d5, 234, 9);
me17:;
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_srciagdq75um1d5, 233, 5);
me13:;
                }
lc4:;
                {
                    uint64_t t112 = gm_l5;
                    uint64_t r113; bool o114 = __builtin_add_overflow(t112, ((uint64_t)1ULL), &r113);
                    uint64_t t115 = r113;
                    gm_l5 = t115;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_fields_30hs0i5(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, void * a2, void * a3, void * a4) {
    struct gm_fr_fields_30hs0i5 F;
    void * gm_l0; /* cx */
    gm_b16_8 gm_l1; /* ms */
    void * gm_l2; /* body */
    void * gm_l3; /* docs */
    void * gm_l4; /* hidden */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    gm_t08olq97rscn3 gm_l12; /* n */
    gm_b16_8 gm_l14; /* e */
    GM_STACK_CHECK(&F, 921u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        {
            gm_b16_8 t3 = gm_l1;
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
                    gm_tfc3oljlofib6 t15 = GM_LD(gm_tfc3oljlofib6, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t11 * 80));
                    F.l7 = t15;
                    {
                        uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&F.l7) + 0));
                        if (t19 != ((uint8_t)0ULL)) goto mn18;
                        gm_t24301fas1b2a t20 = GM_LD(gm_t24301fas1b2a, (((uint8_t *)&F.l7) + 8));
                        F.l8 = t20;
                        {
                            uint8_t t23 = ((GM_LD(uint8_t, (((uint8_t *)&F.l8) + 16) + 0) == ((uint8_t)2ULL)));
                            if (!t23) goto ie22;
                            {
                                void * t25 = gm_l2;
                                void * t26 = gm_l0;
                                gm_tl874t2atnlh3 t27 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l8) + 56));
                                gm_b16_8 t28 = gm_f_text_epcdloe(t26, GM_CONV(gm_cseahvg8v7p0vs, t27));
                                gm_tftb1odc739t5 t29 = gm_f_push_3ef746b(t25, t28);
                                gm_tftb1odc739t5 t30 = t29;
                                if (((((uint8_t *)&t30))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t30) + 1, gm_srciagdq75um1d5, 402, 21);
                                gm_b0_1 t31 = GM_LD(gm_b0_1, (((uint8_t *)&t30) + 1));
                            }
                            {
                                gm_f_list_epvducg((void *)&F.l9, (void *)NULL, cx, NULL);
                                if (gm_unw.kind) {
                                    goto gm_ll8;
                                }
                            }
                            {
                                {
                                    gm_b16_8 t33 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 24));
                                    gm_l10 = t33;
                                }
                                {
                                    gm_l11 = ((uint64_t)0ULL);
                                }
                                {
                                    for (;;) {
                                        uint64_t t35 = gm_l11;
                                        uint64_t t36 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                                        uint8_t t37 = (t35 < t36);
                                        if (!t37) goto lb34;
                                        {
                                            uint64_t t40 = gm_l11;
                                            uint64_t t41 = (uintptr_t)t40;
                                            uint64_t t42 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                                            gm_t08olq97rscn3 t45 = GM_LD(gm_t08olq97rscn3, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l10)) + t41 * 32));
                                            gm_l12 = t45;
                                            {
                                                void * t47 = (void *)((uint8_t *)&F.l9);
                                                void * t48 = gm_l0;
                                                gm_tl874t2atnlh3 t49 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l12) + 16));
                                                gm_b16_8 t50 = gm_f_text_epcdloe(t48, GM_CONV(gm_cseahvg8v7p0vs, t49));
                                                gm_tftb1odc739t5 t51 = gm_f_push_3ef746b(t47, t50);
                                                gm_tftb1odc739t5 t52 = t51;
                                                if (((((uint8_t *)&t52))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t52) + 1, gm_srciagdq75um1d5, 404, 40);
                                                gm_b0_1 t53 = GM_LD(gm_b0_1, (((uint8_t *)&t52) + 1));
                                            }
                                            goto me43;
mn44:;
                                            gm_trap("no match arm matched the scrutinee", gm_srciagdq75um1d5, 404, 21);
me43:;
                                        }
lc34:;
                                        {
                                            uint64_t t55 = gm_l11;
                                            uint64_t r56; bool o57 = __builtin_add_overflow(t55, ((uint64_t)1ULL), &r56);
                                            uint64_t t58 = r56;
                                            gm_l11 = t58;
                                        }
                                    }
lb34:;
                                }
                            }
                            {
                                void * t59 = gm_l0;
                                void * t60 = gm_l3;
                                gm_b16_8 t61 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 0));
                                gm_b16_8 t62 = t61;
                                gm_b16_8 t63 = GM_LD(gm_b16_8, ((uint8_t *)&t62));
                                gm_b16_8 t64;
                                GM_ST(void *, (uint8_t *)&t64, (void *)(gm_sg5gdnrith2i6));
                                GM_ST(uintptr_t, (uint8_t *)&t64 + 8, (uintptr_t)(2));
                                gm_b16_8 t65 = gm_f_join_e7fm1oo((void *)NULL, cx, NULL, t63, t64);
                                if (gm_unw.kind) {
                                    goto gm_ll8;
                                }
                                uint32_t t66 = GM_LD(uint32_t, ((((uint8_t *)&F.l8) + 56) + 4));
                                gm_b0_1 t67 = gm_f_member_doc_9b4clb2((void *)NULL, cx, NULL, t59, t60, t65, t66);
                                if (gm_unw.kind) {
                                    goto gm_ll8;
                                }
                            }
                            goto ix22;
ie22:;
                            {
                                void * t69 = gm_l4;
                                GM_ST(uint8_t, ((uint8_t *)t69), ((uint8_t)1));
                            }
ix22:;
                        }
                        goto me17;
mn18:;
                        uint8_t t71 = GM_LD(uint8_t, (((uint8_t *)&F.l7) + 0));
                        if (t71 != ((uint8_t)1ULL)) goto mn70;
                        gm_t7bjbmukv26e7 t72 = GM_LD(gm_t7bjbmukv26e7, (((uint8_t *)&F.l7) + 8));
                        F.l13 = t72;
                        {
                            void * t74 = gm_l0;
                            gm_b16_8 t75 = GM_LD(gm_b16_8, (((uint8_t *)&F.l13) + 8));
                            gm_b16_8 t76 = t75;
                            gm_b16_8 t77 = GM_LD(gm_b16_8, ((uint8_t *)&t76));
                            void * t78 = gm_l2;
                            void * t79 = gm_l3;
                            void * t80 = gm_l4;
                            gm_b0_1 t81 = gm_f_fields_30hs0i5((void *)NULL, cx, NULL, t74, t77, t78, t79, t80);
                            if (gm_unw.kind) {
                                goto gm_ll8;
                            }
                        }
                        {
                            if (!(((((uint8_t *)&F.l13) + 24))[16] != 0)) goto ie82;
                            gm_b16_8 t83 = GM_LD(gm_b16_8, (((uint8_t *)&F.l13) + 24));
                            gm_l14 = t83;
                            {
                                void * t85 = gm_l0;
                                gm_b16_8 t86 = gm_l14;
                                gm_b16_8 t87 = t86;
                                gm_b16_8 t88 = GM_LD(gm_b16_8, ((uint8_t *)&t87));
                                void * t89 = gm_l2;
                                void * t90 = gm_l3;
                                void * t91 = gm_l4;
                                gm_b0_1 t92 = gm_f_fields_30hs0i5((void *)NULL, cx, NULL, t85, t88, t89, t90, t91);
                                if (gm_unw.kind) {
                                    goto gm_ll8;
                                }
                            }
                            goto ix82;
ie82:;
ix82:;
                        }
                        goto me17;
mn70:;
                        gm_trap("no match arm matched the scrutinee", gm_srciagdq75um1d5, 399, 9);
me17:;
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_srciagdq75um1d5, 398, 5);
me13:;
                }
lc4:;
                {
                    uint64_t t94 = gm_l6;
                    uint64_t r95; bool o96 = __builtin_add_overflow(t94, ((uint64_t)1ULL), &r95);
                    uint64_t t97 = r95;
                    gm_l6 = t97;
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
__attribute__((aligned(4))) uint64_t gm_f_find_open_brace_eomst48(gm_b16_8 a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_find_open_brace_eomst48 F;
    gm_b16_8 gm_l0; /* src */
    uint64_t gm_l1; /* from */
    uint64_t gm_l2; /* i */
    int32_t gm_l3; /* depth */
    GM_STACK_CHECK_SHALLOW(&F, 427u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t2 = gm_l1;
        gm_l2 = t2;
    }
    {
        gm_l3 = ((int32_t)0LL);
    }
    {
        for (;;) {
            uint64_t t4 = gm_l2;
            uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            uint8_t t6 = (t4 < t5);
            if (!t6) goto lb3;
            {
                uint64_t t9 = gm_l2;
                uint64_t t10 = (uintptr_t)t9;
                uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                if (t10 >= (uintptr_t)(t11)) gm_trap_index(t10, t11, gm_srciagdq75um1d5, 504, 15);
                uint8_t t12 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t10 * 1));
                uint8_t t13 = t12;
                uint8_t t18 = GM_LD(uint8_t, ((uint8_t *)&t13));
                if (!(t18 == ((uint8_t)40ULL))) goto pa17;
                goto po16;
pa17:;
                uint8_t t20 = GM_LD(uint8_t, ((uint8_t *)&t13));
                if (!(t20 == ((uint8_t)91ULL))) goto pa19;
                goto po16;
pa19:;
                goto mn15;
po16:;
                {
                    int32_t t22 = gm_l3;
                    int32_t r23; bool o24 = __builtin_add_overflow(t22, ((int32_t)1LL), &r23);
                    if (o24) gm_trap("integer overflow in `+`", gm_srciagdq75um1d5, 505, 27);
                    int32_t t25 = r23;
                    gm_l3 = t25;
                }
                goto me14;
mn15:;
                uint8_t t29 = GM_LD(uint8_t, ((uint8_t *)&t13));
                if (!(t29 == ((uint8_t)41ULL))) goto pa28;
                goto po27;
pa28:;
                uint8_t t31 = GM_LD(uint8_t, ((uint8_t *)&t13));
                if (!(t31 == ((uint8_t)93ULL))) goto pa30;
                goto po27;
pa30:;
                goto mn26;
po27:;
                {
                    int32_t t33 = gm_l3;
                    int32_t r34; bool o35 = __builtin_sub_overflow(t33, ((int32_t)1LL), &r34);
                    if (o35) gm_trap("integer overflow in `-`", gm_srciagdq75um1d5, 506, 27);
                    int32_t t36 = r34;
                    gm_l3 = t36;
                }
                goto me14;
mn26:;
                uint8_t t38 = GM_LD(uint8_t, ((uint8_t *)&t13));
                if (!(t38 == ((uint8_t)123ULL))) goto mn37;
                int32_t t39 = gm_l3;
                uint8_t t40 = (t39 <= ((int32_t)0LL));
                if (!t40) goto mn37;
                uint64_t t41 = gm_l2;
                uint8_t t42 = 0;
                F.ret = t41;
                goto gm_fret;
                goto me14;
mn37:;
                uint8_t t44 = GM_LD(uint8_t, ((uint8_t *)&t13));
                if (!(t44 == ((uint8_t)34ULL))) goto mn43;
                {
                    uint64_t t46 = gm_l2;
                    uint64_t r47; bool o48 = __builtin_add_overflow(t46, ((uint64_t)1ULL), &r47);
                    if (o48) gm_trap("integer overflow in `+`", gm_srciagdq75um1d5, 509, 17);
                    uint64_t t49 = r47;
                    gm_l2 = t49;
                }
                {
                    for (;;) {
                        uint64_t t51 = gm_l2;
                        uint64_t t52 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                        uint8_t t53 = (t51 < t52);
                        if (!t53) goto lb50;
                        uint64_t t54 = gm_l2;
                        uint64_t t55 = (uintptr_t)t54;
                        uint64_t t56 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                        if (t55 >= (uintptr_t)(t56)) gm_trap_index(t55, t56, gm_srciagdq75um1d5, 510, 39);
                        uint8_t t57 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t55 * 1));
                        uint8_t t58 = (!(t57 == ((uint8_t)34ULL)));
                        if (!t58) goto lb50;
                        {
                            uint64_t t62 = gm_l2;
                            uint64_t t63 = (uintptr_t)t62;
                            uint64_t t64 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                            if (t63 >= (uintptr_t)(t64)) gm_trap_index(t63, t64, gm_srciagdq75um1d5, 511, 24);
                            uint8_t t65 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t63 * 1));
                            uint8_t t66 = ((t65 == ((uint8_t)92ULL)));
                            if (!t66) goto ie61;
                            {
                                uint64_t t68 = gm_l2;
                                uint64_t r69; bool o70 = __builtin_add_overflow(t68, ((uint64_t)1ULL), &r69);
                                if (o70) gm_trap("integer overflow in `+`", gm_srciagdq75um1d5, 511, 41);
                                uint64_t t71 = r69;
                                gm_l2 = t71;
                            }
                            goto ix61;
ie61:;
ix61:;
                        }
                        {
                            uint64_t t72 = gm_l2;
                            uint64_t r73; bool o74 = __builtin_add_overflow(t72, ((uint64_t)1ULL), &r73);
                            if (o74) gm_trap("integer overflow in `+`", gm_srciagdq75um1d5, 512, 21);
                            uint64_t t75 = r73;
                            gm_l2 = t75;
                        }
lc50:;
                    }
lb50:;
                }
                goto me14;
mn43:;
                uint8_t t77 = GM_LD(uint8_t, ((uint8_t *)&t13));
                if (!(t77 == ((uint8_t)47ULL))) goto mn76;
                gm_b16_8 t78 = gm_l0;
                gm_b16_8 t79 = t78;
                void * t80 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t79)));
                uint64_t t81 = (uintptr_t)GM_LD(uintptr_t, ((uint8_t *)&t79) + 8);
                uint64_t t82 = gm_l2;
                uint64_t r83; bool o84 = __builtin_add_overflow(t82, ((uint64_t)1ULL), &r83);
                if (o84) gm_trap("integer overflow in `+`", gm_srciagdq75um1d5, 515, 28);
                uint64_t t85 = r83;
                uint64_t t86 = (uintptr_t)t85;
                gm_tqs5p4n2tq19v t87;
                if (t86 < t81) {
                uint8_t t88 = GM_LD(uint8_t, ((uint8_t *)t80 + t86 * 1));
                gm_tqs5p4n2tq19v t89;
                GM_ST(uint8_t, ((uint8_t *)&t89), t88);
                ((uint8_t *)&t89)[1] = 1;
                    t87 = t89;
                } else {
                gm_tqs5p4n2tq19v t90;
                memset(&t90, 0, sizeof t90);
                    t87 = t90;
                }
                gm_tqs5p4n2tq19v t91;
                GM_ST(uint8_t, ((uint8_t *)&t91), ((uint8_t)47ULL));
                ((uint8_t *)&t91)[1] = 1;
                gm_tqs5p4n2tq19v t92 = t87;
                gm_tqs5p4n2tq19v t93 = t91;
                uint8_t t94 = (gm_eqb44cb3cf1n(((uint8_t *)&t92), ((uint8_t *)&t93)));
                if (!t94) goto mn76;
                {
                    for (;;) {
                        uint64_t t97 = gm_l2;
                        uint64_t t98 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                        uint8_t t99 = (t97 < t98);
                        if (!t99) goto lb96;
                        uint64_t t100 = gm_l2;
                        uint64_t t101 = (uintptr_t)t100;
                        uint64_t t102 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                        if (t101 >= (uintptr_t)(t102)) gm_trap_index(t101, t102, gm_srciagdq75um1d5, 516, 39);
                        uint8_t t103 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t101 * 1));
                        uint8_t t104 = (!(t103 == ((uint8_t)10ULL)));
                        if (!t104) goto lb96;
                        {
                            uint64_t t107 = gm_l2;
                            uint64_t r108; bool o109 = __builtin_add_overflow(t107, ((uint64_t)1ULL), &r108);
                            if (o109) gm_trap("integer overflow in `+`", gm_srciagdq75um1d5, 516, 60);
                            uint64_t t110 = r108;
                            gm_l2 = t110;
                        }
lc96:;
                    }
lb96:;
                }
                goto me14;
mn76:;
                goto me14;
mn111:;
                gm_trap("no match arm matched the scrutinee", gm_srciagdq75um1d5, 504, 9);
me14:;
            }
            {
                uint64_t t113 = gm_l2;
                uint64_t r114; bool o115 = __builtin_add_overflow(t113, ((uint64_t)1ULL), &r114);
                if (o115) gm_trap("integer overflow in `+`", gm_srciagdq75um1d5, 520, 9);
                uint64_t t116 = r114;
                gm_l2 = t116;
            }
lc3:;
        }
lb3:;
    }
    {
        uint64_t t117 = gm_l1;
        uint8_t t118 = 0;
        F.ret = t117;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b0_1 gm_f_handler_decl_bvaup21(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, const void * a3, void * a4) {
    struct gm_fr_handler_decl_bvaup21 F;
    void * gm_l0; /* cx */
    void * gm_l1; /* item */
    void * gm_l2; /* h */
    gm_th2lqbo77pvsc gm_l3; /* cond */
    void * gm_l4; /* out */
    uint32_t gm_l5; /* from */
    gm_b16_8 gm_l6; /* effs */
    uint64_t gm_l8; /* open */
    uint64_t gm_l9; /* lo */
    gm_b16_8 gm_l10; /* head */
    gm_b16_8 gm_l11; /* name */
    gm_b16_8 gm_l12; /* sig */
    gm_th2lqbo77pvsc gm_l13; /* target */
    GM_STACK_CHECK(&F, 1738u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l4 = a4;
    {
        void * t2 = gm_l2;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 32));
        uint32_t t4 = GM_LD(uint32_t, ((((uint8_t *)t3) + 72) + 8));
        gm_l5 = t4;
    }
    {
        void * t6 = gm_l2;
        if (!(((((uint8_t *)t6) + 40))[16] != 0)) goto ie5;
        gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)t6) + 40));
        gm_l6 = t7;
        {
            gm_b16_8 t10 = gm_l6;
            gm_b16_8 t11 = t10;
            void * t12 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t11)));
            uint64_t t13 = (uintptr_t)GM_LD(uintptr_t, ((uint8_t *)&t11) + 8);
            uint64_t t14 = (t13 - 1);
            gm_t0mfir12leg2g t15;
            if (t14 < t13) {
            gm_tc3oht4ksth9d t16 = GM_LD(gm_tc3oht4ksth9d, ((uint8_t *)t12 + t14 * 88));
            gm_t0mfir12leg2g t17;
            GM_ST(uint8_t, (uint8_t *)&t17 + 89, 0);
            GM_ST(uint16_t, (uint8_t *)&t17 + 90, 0);
            GM_ST(uint32_t, (uint8_t *)&t17 + 92, 0);
            GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t17), t16);
            ((uint8_t *)&t17)[88] = 1;
                t15 = t17;
            } else {
            gm_t0mfir12leg2g t18;
            memset(&t18, 0, sizeof t18);
                t15 = t18;
            }
            gm_t0mfir12leg2g t19 = t15;
            if (!((((uint8_t *)&t19))[88] != 0)) goto ie9;
            gm_tc3oht4ksth9d t20 = GM_LD(gm_tc3oht4ksth9d, ((uint8_t *)&t19));
            F.l7 = t20;
            {
                uint32_t t22 = GM_LD(uint32_t, ((((uint8_t *)&F.l7) + 72) + 8));
                gm_l5 = t22;
            }
            goto ix9;
ie9:;
ix9:;
        }
        goto ix5;
ie5:;
ix5:;
    }
    {
        uint32_t t23 = gm_l5;
        uint64_t t24 = (uint64_t)t23;
        gm_l8 = t24;
    }
    {
        uint64_t t25 = gm_l8;
        void * t26 = gm_l0;
        uint64_t t27 = GM_LD(uintptr_t, (((uint8_t *)t26) + 0) + 8);
        void * t28 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t26) + 0)));
        uint64_t t29 = gm_l8;
        uint64_t t30 = (uintptr_t)t29;
        if (t30 > (uintptr_t)t27 || (uintptr_t)t27 > (uintptr_t)t27) gm_trap_slice(t30, (uintptr_t)t27, t27, gm_srciagdq75um1d5, 259, 22);
        gm_b16_8 t31;
        GM_ST(void *, (uint8_t *)&t31, (void *)(((uint8_t *)t28 + t30 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t31 + 8, (uintptr_t)((uintptr_t)t27 - t30));
        gm_b16_8 t32;
        GM_ST(void *, (uint8_t *)&t32, (void *)(gm_sh40na9qbga3l));
        GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(1));
        gm_tf5srh4mp4b3i t33 = gm_f_find_nab29co(t31, t32);
        gm_tf5srh4mp4b3i t34 = t33;
        uint64_t t35;
        if (((((uint8_t *)&t34))[8] != 0)) {
            uint64_t t36 = GM_LD(uint64_t, (((uint8_t *)&t34) + 0));
            t35 = t36;
        } else {
            t35 = ((uint64_t)0ULL);
        }
        uint64_t r37; bool o38 = __builtin_add_overflow(t25, t35, &r37);
        if (o38) gm_trap("integer overflow in `+`", gm_srciagdq75um1d5, 259, 5);
        uint64_t t39 = r37;
        gm_l8 = t39;
    }
    {
        void * t40 = gm_l2;
        uint32_t t41 = GM_LD(uint32_t, ((((uint8_t *)t40) + 80) + 4));
        uint64_t t42 = (uint64_t)t41;
        gm_l9 = t42;
    }
    {
        void * t43 = gm_l0;
        uint64_t t44 = GM_LD(uintptr_t, (((uint8_t *)t43) + 0) + 8);
        void * t45 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t43) + 0)));
        uint64_t t46 = gm_l9;
        uint64_t t47 = (uintptr_t)t46;
        uint64_t t48 = gm_l8;
        uint64_t t49 = (uintptr_t)t48;
        if (t47 > t49 || t49 > (uintptr_t)t44) gm_trap_slice(t47, t49, t44, gm_srciagdq75um1d5, 261, 22);
        gm_b16_8 t50;
        GM_ST(void *, (uint8_t *)&t50, (void *)(((uint8_t *)t45 + t47 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t50 + 8, (uintptr_t)(t49 - t47));
        gm_b16_8 t51 = gm_f_trim_end_2059dot(t50);
        gm_l10 = t51;
    }
    {
        void * t52 = gm_l0;
        uint64_t t53 = GM_LD(uintptr_t, (((uint8_t *)t52) + 0) + 8);
        void * t54 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t52) + 0)));
        uint64_t t55 = gm_l9;
        uint64_t t56 = (uintptr_t)t55;
        void * t57 = gm_l2;
        void * t58 = GM_LD(void *, (((uint8_t *)t57) + 32));
        uint32_t t59 = GM_LD(uint32_t, ((((uint8_t *)t58) + 72) + 8));
        uint64_t t60 = (uint64_t)t59;
        uint64_t t61 = (uintptr_t)t60;
        if (t56 > t61 || t61 > (uintptr_t)t53) gm_trap_slice(t56, t61, t53, gm_srciagdq75um1d5, 262, 20);
        gm_b16_8 t62;
        GM_ST(void *, (uint8_t *)&t62, (void *)(((uint8_t *)t54 + t56 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t62 + 8, (uintptr_t)(t61 - t56));
        gm_b16_8 t63 = gm_f_squash_2iiihih((void *)NULL, cx, NULL, t62);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l11 = t63;
    }
    {
        void * t64 = gm_l0;
        void * t65 = gm_l1;
        gm_b16_8 t66 = gm_l10;
        gm_b16_8 t67 = gm_f_with_attrs_429rjgv((void *)NULL, cx, NULL, t64, t65, &(*(gm_th2lqbo77pvsc *)((uint8_t *)a3)), t66);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l12 = t67;
    }
    {
        gm_th2lqbo77pvsc t68;
        void * t70 = gm_l1;
        uint8_t t71 = ((GM_LD(uint8_t, (((uint8_t *)t70) + 56) + 0) == ((uint8_t)2ULL)));
        if (!t71) goto ie69;
        gm_th2lqbo77pvsc t73;
        memset(&t73, 0, sizeof t73);
        gm_th2lqbo77pvsc t74 = t73;
        t68 = t74;
        goto ix69;
ie69:;
        void * t76 = gm_l0;
        void * t77 = gm_l2;
        void * t78 = GM_LD(void *, (((uint8_t *)t77) + 32));
        gm_b16_8 t79 = gm_f_base_name_6a6hn9r(t76, t78);
        gm_b16_8 t80 = t79;
        gm_th2lqbo77pvsc t81;
        GM_ST(uint8_t, (uint8_t *)&t81 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t81 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t81 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t81), t80);
        ((uint8_t *)&t81)[16] = 1;
        t68 = t81;
ix69:;
        gm_l13 = t68;
    }
    {
        void * t82 = gm_l4;
        gm_tk50ot32aqf1o t83;
        memset((uint8_t *)&t83 + 1, 0, 7);
        gm_t9qkh0rsooc8d t84;
        GM_ST(uint8_t, ((uint8_t *)&t84 + 0), ((uint8_t)4ULL));
        GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t83 + 0, t84);
        gm_b16_8 t85 = gm_l11;
        GM_ST(gm_b16_8, (uint8_t *)&t83 + 8, t85);
        gm_b16_8 t86 = gm_l12;
        GM_ST(gm_b16_8, (uint8_t *)&t83 + 24, t86);
        void * t87 = gm_l1;
        memcpy((uint8_t *)&t83 + 40, (((uint8_t *)t87) + 16), 24);
        gm_b16_8 t88;
        GM_ST(void *, (uint8_t *)&t88, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t88 + 8, (uintptr_t)(0));
        GM_ST(gm_b16_8, (uint8_t *)&t83 + 64, t88);
        gm_trqfvmjtt4sij t89;
        gm_f_list_792g2go_3((void *)&t89, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t90 = t89;
        gm_b16_8 t91 = GM_LD(gm_b16_8, (((uint8_t *)&t90) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t83 + 80, t91);
        void * t92 = gm_l0;
        void * t93 = gm_l1;
        uint32_t t94 = GM_LD(uint32_t, ((((uint8_t *)t93) + 4) + 4));
        gm_b16_8 t95 = gm_f_location_3jufeug((void *)NULL, cx, NULL, t92, t94);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t83 + 96, t95);
        memcpy((uint8_t *)&t83 + 112, ((uint8_t *)&gm_l13), 24);
        gm_tftb1odc739t5 t96 = gm_f_push_rda4o9n_3(t82, &t83);
        gm_tftb1odc739t5 t97 = t96;
        if (((((uint8_t *)&t97))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t97) + 1, gm_srciagdq75um1d5, 265, 5);
        gm_b0_1 t98 = GM_LD(gm_b0_1, (((uint8_t *)&t97) + 1));
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
extern __attribute__((aligned(4))) uint8_t gm_f_init_text_is_primitive_86n04kn(void * a0, void * a1);
extern __attribute__((aligned(4))) uint8_t gm_f_is_upper_apd69if(gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_join_braced_ab65ll8(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, uint8_t a2) {
    struct gm_fr_join_braced_ab65ll8 F;
    gm_b16_8 gm_l0; /* head */
    gm_b16_8 gm_l1; /* body */
    uint8_t gm_l2; /* hidden */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_b16_8 gm_l6; /* b */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* i */
    gm_b16_8 gm_l10; /* line */
    gm_b16_8 gm_l11; /* l */
    GM_STACK_CHECK(&F, 947u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        gm_f_list_uvfu1k1((void *)&F.l3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t4 = ((t3 == ((uint64_t)0ULL)));
        if (!t4) goto ie2;
        uint8_t t5 = gm_l2;
        uint8_t t6 = (uint8_t)!t5;
        if (!t6) goto ie2;
        {
            void * t8 = (void *)((uint8_t *)&F.l3);
            gm_b16_8 t9 = gm_l0;
            gm_tnpifv20irnph t10;
            GM_ST(gm_b16_8, ((uint8_t *)&t10 + 0), t9);
            gm_tftb1odc739t5 t11 = gm_f_write_8cmm737(t8, t10);
            gm_tftb1odc739t5 t12 = t11;
            if (((((uint8_t *)&t12))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t12) + 1, gm_srciagdq75um1d5, 473, 9);
            gm_b0_1 t13 = GM_LD(gm_b0_1, (((uint8_t *)&t12) + 1));
        }
        {
            gm_b16_8 t14 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_b16_8 t15 = t14;
            gm_b16_8 t16 = GM_LD(gm_b16_8, ((uint8_t *)&t15));
            uint8_t t17 = 0;
            F.ret = t16;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t18 = (void *)((uint8_t *)&F.l3);
        gm_b16_8 t19 = gm_l0;
        gm_tnpifv20irnph t20;
        GM_ST(gm_b16_8, ((uint8_t *)&t20 + 0), t19);
        gm_tftb1odc739t5 t21 = gm_f_write_7oqfkkd(t18, t20);
        gm_tftb1odc739t5 t22 = t21;
        if (((((uint8_t *)&t22))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t22) + 1, gm_srciagdq75um1d5, 476, 5);
        gm_b0_1 t23 = GM_LD(gm_b0_1, (((uint8_t *)&t22) + 1));
    }
    {
        {
            gm_b16_8 t25 = gm_l1;
            gm_l4 = t25;
        }
        {
            gm_l5 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t27 = gm_l5;
                uint64_t t28 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                uint8_t t29 = (t27 < t28);
                if (!t29) goto lb26;
                {
                    uint64_t t32 = gm_l5;
                    uint64_t t33 = (uintptr_t)t32;
                    uint64_t t34 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    gm_b16_8 t37 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t33 * 16));
                    gm_l6 = t37;
                    {
                        {
                            gm_b16_8 t40 = gm_l6;
                            gm_b16_8 t41 = gm_f_lines_of_t8v251a((void *)NULL, cx, NULL, t40);
                            if (gm_unw.kind) {
                                goto gm_ll30;
                            }
                            gm_b16_8 t42 = t41;
                            gm_b16_8 t43 = GM_LD(gm_b16_8, ((uint8_t *)&t42));
                            gm_l7 = t43;
                        }
                        {
                            gm_l8 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t45 = gm_l8;
                                uint64_t t46 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                                uint8_t t47 = (t45 < t46);
                                if (!t47) goto lb44;
                                {
                                    gm_tf1bjg81u0glv t50;
                                    uint64_t t51 = gm_l8;
                                    GM_ST(uint64_t, (uint8_t *)&t50 + 0, t51);
                                    uint64_t t52 = gm_l8;
                                    uint64_t t53 = (uintptr_t)t52;
                                    uint64_t t54 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                                    gm_b16_8 t55 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t53 * 16));
                                    GM_ST(gm_b16_8, (uint8_t *)&t50 + 8, t55);
                                    gm_tf1bjg81u0glv t56 = t50;
                                    uint64_t t59 = GM_LD(uint64_t, (((uint8_t *)&t56) + 0));
                                    gm_l9 = t59;
                                    gm_b16_8 t60 = GM_LD(gm_b16_8, (((uint8_t *)&t56) + 8));
                                    gm_l10 = t60;
                                    {
                                        gm_b16_8 t62;
                                        uint64_t t64 = gm_l9;
                                        uint8_t t65 = ((t64 == ((uint64_t)0ULL)));
                                        if (!t65) goto ie63;
                                        gm_b16_8 t67 = gm_l10;
                                        gm_b16_8 t68 = gm_f_trim_start_lj2dhj3(t67);
                                        gm_b16_8 t69 = t68;
                                        t62 = t69;
                                        goto ix63;
ie63:;
                                        gm_b16_8 t71 = gm_l10;
                                        gm_b16_8 t72 = t71;
                                        t62 = t72;
ix63:;
                                        gm_l11 = t62;
                                    }
                                    {
                                        gm_b16_8 t74 = gm_l11;
                                        gm_b16_8 t75 = gm_f_trim_ebi5drb(t74);
                                        gm_b16_8 t76 = t75;
                                        uint64_t t77 = GM_LD(uintptr_t, ((uint8_t *)&t76) + 8);
                                        uint8_t t78 = ((t77 == ((uint64_t)0ULL)));
                                        if (!t78) goto ie73;
                                        {
                                            void * t80 = (void *)((uint8_t *)&F.l3);
                                            gm_tftb1odc739t5 t81 = gm_f_push_t83rg2m(t80, ((uint8_t)10ULL));
                                            gm_tftb1odc739t5 t82 = t81;
                                            if (((((uint8_t *)&t82))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t82) + 1, gm_srciagdq75um1d5, 482, 17);
                                            gm_b0_1 t83 = GM_LD(gm_b0_1, (((uint8_t *)&t82) + 1));
                                        }
                                        goto ix73;
ie73:;
                                        {
                                            void * t85 = (void *)((uint8_t *)&F.l3);
                                            gm_b16_8 t86 = gm_l11;
                                            gm_b16_8 t87 = gm_f_trim_end_2059dot(t86);
                                            gm_tnpifv20irnph t88;
                                            GM_ST(gm_b16_8, ((uint8_t *)&t88 + 0), t87);
                                            gm_tftb1odc739t5 t89 = gm_f_write_aaqg4k2(t85, t88);
                                            gm_tftb1odc739t5 t90 = t89;
                                            if (((((uint8_t *)&t90))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t90) + 1, gm_srciagdq75um1d5, 484, 17);
                                            gm_b0_1 t91 = GM_LD(gm_b0_1, (((uint8_t *)&t90) + 1));
                                        }
ix73:;
                                    }
                                    goto me57;
mn58:;
                                    gm_trap("no match arm matched the scrutinee", gm_srciagdq75um1d5, 478, 9);
me57:;
                                }
lc44:;
                                {
                                    uint64_t t93 = gm_l8;
                                    uint64_t r94; bool o95 = __builtin_add_overflow(t93, ((uint64_t)1ULL), &r94);
                                    uint64_t t96 = r94;
                                    gm_l8 = t96;
                                }
                            }
lb44:;
                        }
                    }
                    goto me35;
mn36:;
                    gm_trap("no match arm matched the scrutinee", gm_srciagdq75um1d5, 477, 5);
me35:;
                }
lc26:;
                {
                    uint64_t t98 = gm_l5;
                    uint64_t r99; bool o100 = __builtin_add_overflow(t98, ((uint64_t)1ULL), &r99);
                    uint64_t t101 = r99;
                    gm_l5 = t101;
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
        uint8_t t103 = gm_l2;
        if (!t103) goto ie102;
        {
            void * t105 = (void *)((uint8_t *)&F.l3);
            gm_b16_8 t106;
            GM_ST(void *, (uint8_t *)&t106, (void *)(gm_s3h586tvdle6o));
            GM_ST(uintptr_t, (uint8_t *)&t106 + 8, (uintptr_t)(30));
            gm_tftb1odc739t5 t107 = gm_f_extend_0cs85gs(t105, t106);
            gm_tftb1odc739t5 t108 = t107;
            if (((((uint8_t *)&t108))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t108) + 1, gm_srciagdq75um1d5, 488, 17);
            gm_b0_1 t109 = GM_LD(gm_b0_1, (((uint8_t *)&t108) + 1));
        }
        goto ix102;
ie102:;
ix102:;
    }
    {
        void * t110 = (void *)((uint8_t *)&F.l3);
        gm_tftb1odc739t5 t111 = gm_f_push_t83rg2m(t110, ((uint8_t)125ULL));
        gm_tftb1odc739t5 t112 = t111;
        if (((((uint8_t *)&t112))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t112) + 1, gm_srciagdq75um1d5, 489, 5);
        gm_b0_1 t113 = GM_LD(gm_b0_1, (((uint8_t *)&t112) + 1));
    }
    {
        gm_b16_8 t114 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
        gm_b16_8 t115 = t114;
        gm_b16_8 t116 = GM_LD(gm_b16_8, ((uint8_t *)&t115));
        uint8_t t117 = 0;
        F.ret = t116;
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
inline __attribute__((aligned(4))) void gm_f_load_module_qmml7un(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, gm_b16_8 a2) {
    struct gm_fr_load_module_qmml7un F;
    gm_b16_8 gm_l0; /* files */
    gm_b16_8 gm_l1; /* name */
    gm_b16_8 gm_l2; /* path */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_b16_8 gm_l6; /* f */
    gm_tj5jn79kq9pnm gm_l7; /* e */
    gm_b16_8 gm_l8; /* src */
    GM_STACK_CHECK(&F, 1251u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        gm_f_reader_rcu54pm((void *)&F.l3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t3 = gm_l0;
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
                    gm_b16_8 t15 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t11 * 16));
                    gm_l6 = t15;
                    {
                        gm_b16_8 t17 = gm_l6;
                        gm_tgvh15qshf643 t18;
                        gm_f_read_all_r9rh9qd((void *)&t18, (void *)NULL, cx, NULL, t17);
                        if (gm_unw.kind) {
                            goto gm_ll8;
                        }
                        gm_tgvh15qshf643 t19 = t18;
                        gm_b16_8 t20;
                        if (!((((uint8_t *)&t19))[0] != 0)) {
                            gm_b16_8 t21 = GM_LD(gm_b16_8, (((uint8_t *)&t19) + 8));
                            t20 = t21;
                        } else {
                            gm_tj5jn79kq9pnm t22 = GM_LD(gm_tj5jn79kq9pnm, (((uint8_t *)&t19) + 8));
                            gm_l7 = t22;
                            {
                                void * t24 = (void *)(((uint8_t *)&F.l3) + 40);
                                gm_tl874t2atnlh3 t25 = GM_LD(gm_tl874t2atnlh3, gm_cejn1dj0054);
                                gm_t6j6smj7qoctu t26;
                                gm_b16_8 t27 = gm_l6;
                                GM_ST(gm_b16_8, (uint8_t *)&t26 + 0, t27);
                                gm_tj5jn79kq9pnm t28 = gm_l7;
                                gm_b16_8 t29 = gm_f_describe_read_error_uikkr59((void *)NULL, cx, NULL, t28);
                                if (gm_unw.kind) {
                                    goto gm_ll8;
                                }
                                GM_ST(gm_b16_8, (uint8_t *)&t26 + 16, t29);
                                gm_tabb710d9mq2e t30;
                                gm_f_format_04pispc((void *)&t30, (void *)NULL, cx, NULL, &t26);
                                if (gm_unw.kind) {
                                    goto gm_ll8;
                                }
                                gm_tabb710d9mq2e t31 = t30;
                                if (((((uint8_t *)&t31))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t31) + 8, gm_srciagdq75um1d5, 126, 42);
                                gm_b16_8 t32 = GM_LD(gm_b16_8, (((uint8_t *)&t31) + 8));
                                gm_b16_8 t33 = t32;
                                gm_b16_8 t34 = GM_LD(gm_b16_8, ((uint8_t *)&t33));
                                gm_b0_1 t35 = gm_f_report_eardf07((void *)NULL, cx, NULL, t24, t25, t34);
                                if (gm_unw.kind) {
                                    goto gm_ll8;
                                }
                            }
                            void * t36 = (void *)(((uint8_t *)&F.l3) + 0);
                            void * t37 = (void *)(((uint8_t *)&F.l3) + 40);
                            gm_b16_8 t38 = gm_f_render_diagnostics_65a9o86((void *)NULL, cx, NULL, t36, t37);
                            if (gm_unw.kind) {
                                goto gm_ll8;
                            }
                            gm_tnpifv20irnph t39;
                            GM_ST(gm_b16_8, ((uint8_t *)&t39 + 0), t38);
                            gm_tflfe6l4hnrb7 t40;
                            memset(&t40, 0, sizeof t40);
                            ((uint8_t *)&t40)[0] = 1;
                            GM_ST(gm_tnpifv20irnph, ((uint8_t *)&t40 + 8), t39);
                            gm_tflfe6l4hnrb7 t41 = t40;
                            uint8_t t42 = ((((uint8_t *)&t41))[0] != 0);
                            F.ret = t40;
                            goto gm_fret;
                        }
                        gm_l8 = t20;
                    }
                    {
                        void * t43 = (void *)((uint8_t *)&F.l3);
                        gm_b16_8 t44 = gm_l6;
                        gm_b16_8 t45 = gm_l8;
                        gm_b16_8 t46 = t45;
                        gm_b16_8 t47 = GM_LD(gm_b16_8, ((uint8_t *)&t46));
                        gm_t5ghc3lnpmg2h t48;
                        gm_f_read_file_dac5brv((void *)&t48, (void *)NULL, cx, NULL, t43, t44, t47);
                        if (gm_unw.kind) {
                            goto gm_ll8;
                        }
                        gm_t5ghc3lnpmg2h t49 = t48;
                        if (((((uint8_t *)&t49))[0] != 0)) {
                            gm_tnpifv20irnph t50 = GM_LD(gm_tnpifv20irnph, (((uint8_t *)&t49) + 8));
                            gm_tflfe6l4hnrb7 t51;
                            memset(&t51, 0, sizeof t51);
                            ((uint8_t *)&t51)[0] = 1;
                            GM_ST(gm_tnpifv20irnph, ((uint8_t *)&t51 + 8), t50);
                            F.ret = t51;
                            goto gm_fret;
                        }
                        gm_b0_1 t52 = GM_LD(gm_b0_1, (((uint8_t *)&t49) + 8));
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_srciagdq75um1d5, 124, 5);
me13:;
                }
lc4:;
                {
                    uint64_t t54 = gm_l5;
                    uint64_t r55; bool o56 = __builtin_add_overflow(t54, ((uint64_t)1ULL), &r55);
                    uint64_t t57 = r55;
                    gm_l5 = t57;
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
        void * t58 = (void *)((uint8_t *)&F.l3);
        gm_b16_8 t59 = gm_l1;
        gm_b16_8 t60 = gm_l2;
        gm_tud7oovfvnf43 t61;
        gm_f_module_of_a4sic60((void *)&t61, (void *)NULL, cx, NULL, t58, t59, t60);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tflfe6l4hnrb7 t62;
        GM_ST(uint8_t, (uint8_t *)&t62 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t62 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t62 + 4, 0);
        ((uint8_t *)&t62)[0] = 0;
        GM_ST(gm_tud7oovfvnf43, ((uint8_t *)&t62 + 8), t61);
        gm_tflfe6l4hnrb7 t63 = t62;
        uint8_t t64 = ((((uint8_t *)&t63))[0] != 0);
        F.ret = t62;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_location_3jufeug(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_location_3jufeug F;
    void * gm_l0; /* cx */
    uint32_t gm_l1; /* lo */
    gm_tcc1rq4n1avb2 gm_l2; /* lc */
    GM_STACK_CHECK(&F, 249u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 32));
        void * t4 = gm_l0;
        uint32_t t5 = GM_LD(uint32_t, (((uint8_t *)t4) + 40));
        void * t6 = gm_f_file_o15fqku(t3, t5);
        uint32_t t7 = gm_l1;
        gm_tcc1rq4n1avb2 t8 = GM_CONV(gm_tcc1rq4n1avb2, gm_f_line_col_9vn24mu(t6, t7));
        gm_l2 = t8;
    }
    {
        gm_tommf4j9upomc t9;
        memset((uint8_t *)&t9 + 20, 0, 4);
        void * t10 = gm_l0;
        gm_b16_8 t11 = GM_LD(gm_b16_8, (((uint8_t *)t10) + 16));
        GM_ST(gm_b16_8, (uint8_t *)&t9 + 0, t11);
        uint32_t t12 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 0));
        GM_ST(uint32_t, (uint8_t *)&t9 + 16, t12);
        gm_tabb710d9mq2e t13;
        gm_f_format_akfobpm((void *)&t13, (void *)NULL, cx, NULL, &t9);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t14 = t13;
        if (((((uint8_t *)&t14))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t14) + 8, gm_srciagdq75um1d5, 229, 12);
        gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&t14) + 8));
        gm_b16_8 t16 = t15;
        gm_b16_8 t17 = GM_LD(gm_b16_8, ((uint8_t *)&t16));
        uint8_t t18 = 0;
        F.ret = t17;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_member_doc_9b4clb2(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2, uint32_t a3) {
    struct gm_fr_member_doc_9b4clb2 F;
    void * gm_l0; /* cx */
    void * gm_l1; /* docs */
    gm_b16_8 gm_l2; /* name */
    uint32_t gm_l3; /* lo */
    uint64_t gm_l4; /* lo_ */
    uint64_t gm_l5; /* line_start */
    uint64_t gm_l6; /* i */
    uint64_t gm_l8; /* end */
    uint64_t gm_l9; /* start */
    uint64_t gm_l10; /* i */
    gm_b16_8 gm_l11; /* line */
    gm_b16_8 gm_l12; /* rest */
    GM_STACK_CHECK(&F, 989u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        uint32_t t2 = gm_l3;
        uint64_t t3 = (uint64_t)t2;
        gm_l4 = t3;
    }
    {
        gm_l5 = ((uint64_t)0ULL);
    }
    {
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uintptr_t, (((uint8_t *)t5) + 0) + 8);
        void * t7 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t5) + 0)));
        uint64_t t8 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t9 = gm_l4;
        uint64_t t10 = (uintptr_t)t9;
        if (t8 > t10 || t10 > (uintptr_t)t6) gm_trap_slice(t8, t10, t6, gm_srciagdq75um1d5, 437, 23);
        gm_b16_8 t11;
        GM_ST(void *, (uint8_t *)&t11, (void *)(((uint8_t *)t7 + t8 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(t10 - t8));
        gm_b16_8 t12;
        GM_ST(void *, (uint8_t *)&t12, (void *)(gm_so514oj5nbhcu));
        GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)(1));
        gm_tf5srh4mp4b3i t13 = gm_f_rfind_ai5s2js(t11, t12);
        gm_tf5srh4mp4b3i t14 = t13;
        if (!((((uint8_t *)&t14))[8] != 0)) goto ie4;
        uint64_t t15 = GM_LD(uint64_t, ((uint8_t *)&t14));
        gm_l6 = t15;
        {
            uint64_t t17 = gm_l6;
            uint64_t r18; bool o19 = __builtin_add_overflow(t17, ((uint64_t)1ULL), &r18);
            if (o19) gm_trap("integer overflow in `+`", gm_srciagdq75um1d5, 437, 60);
            uint64_t t20 = r18;
            gm_l5 = t20;
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        void * t22 = gm_l0;
        uint64_t t23 = GM_LD(uintptr_t, (((uint8_t *)t22) + 0) + 8);
        void * t24 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t22) + 0)));
        uint64_t t25 = gm_l5;
        uint64_t t26 = (uintptr_t)t25;
        uint64_t t27 = gm_l4;
        uint64_t t28 = (uintptr_t)t27;
        if (t26 > t28 || t28 > (uintptr_t)t23) gm_trap_slice(t26, t28, t23, gm_srciagdq75um1d5, 438, 13);
        gm_b16_8 t29;
        GM_ST(void *, (uint8_t *)&t29, (void *)(((uint8_t *)t24 + t26 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t29 + 8, (uintptr_t)(t28 - t26));
        gm_b16_8 t30 = gm_f_trim_ebi5drb(t29);
        gm_b16_8 t31 = t30;
        uint64_t t32 = GM_LD(uintptr_t, ((uint8_t *)&t31) + 8);
        uint8_t t33 = (t32 > ((uint64_t)0ULL));
        if (!t33) goto ie21;
        {
            gm_b0_1 t35;
            uint8_t t36 = 0;
            F.ret = t35;
            goto gm_fret;
        }
        goto ix21;
ie21:;
ix21:;
    }
    {
        gm_f_list_epvducg((void *)&F.l7, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint64_t t37 = gm_l5;
        gm_l8 = t37;
    }
    {
        for (;;) {
            uint64_t t39 = gm_l8;
            uint8_t t40 = (t39 > ((uint64_t)0ULL));
            if (!t40) goto lb38;
            {
                gm_l9 = ((uint64_t)0ULL);
            }
            {
                void * t44 = gm_l0;
                uint64_t t45 = GM_LD(uintptr_t, (((uint8_t *)t44) + 0) + 8);
                void * t46 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t44) + 0)));
                uint64_t t47 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t48 = gm_l8;
                uint64_t r49; bool o50 = __builtin_sub_overflow(t48, ((uint64_t)1ULL), &r49);
                if (o50) gm_trap("integer overflow in `-`", gm_srciagdq75um1d5, 445, 37);
                uint64_t t51 = r49;
                uint64_t t52 = (uintptr_t)t51;
                if (t47 > t52 || t52 > (uintptr_t)t45) gm_trap_slice(t47, t52, t45, gm_srciagdq75um1d5, 445, 27);
                gm_b16_8 t53;
                GM_ST(void *, (uint8_t *)&t53, (void *)(((uint8_t *)t46 + t47 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t53 + 8, (uintptr_t)(t52 - t47));
                gm_b16_8 t54;
                GM_ST(void *, (uint8_t *)&t54, (void *)(gm_so514oj5nbhcu));
                GM_ST(uintptr_t, (uint8_t *)&t54 + 8, (uintptr_t)(1));
                gm_tf5srh4mp4b3i t55 = gm_f_rfind_ai5s2js(t53, t54);
                gm_tf5srh4mp4b3i t56 = t55;
                if (!((((uint8_t *)&t56))[8] != 0)) goto ie43;
                uint64_t t57 = GM_LD(uint64_t, ((uint8_t *)&t56));
                gm_l10 = t57;
                {
                    uint64_t t59 = gm_l10;
                    uint64_t r60; bool o61 = __builtin_add_overflow(t59, ((uint64_t)1ULL), &r60);
                    if (o61) gm_trap("integer overflow in `+`", gm_srciagdq75um1d5, 445, 63);
                    uint64_t t62 = r60;
                    gm_l9 = t62;
                }
                goto ix43;
ie43:;
ix43:;
            }
            {
                void * t63 = gm_l0;
                uint64_t t64 = GM_LD(uintptr_t, (((uint8_t *)t63) + 0) + 8);
                void * t65 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t63) + 0)));
                uint64_t t66 = gm_l9;
                uint64_t t67 = (uintptr_t)t66;
                uint64_t t68 = gm_l8;
                uint64_t t69 = (uintptr_t)t68;
                if (t67 > t69 || t69 > (uintptr_t)t64) gm_trap_slice(t67, t69, t64, gm_srciagdq75um1d5, 446, 22);
                gm_b16_8 t70;
                GM_ST(void *, (uint8_t *)&t70, (void *)(((uint8_t *)t65 + t67 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t70 + 8, (uintptr_t)(t69 - t67));
                gm_b16_8 t71 = gm_f_trim_ebi5drb(t70);
                gm_l11 = t71;
            }
            {
                gm_b16_8 t72 = gm_l11;
                gm_b16_8 t73;
                GM_ST(void *, (uint8_t *)&t73, (void *)(gm_sp21r812mg46a));
                GM_ST(uintptr_t, (uint8_t *)&t73 + 8, (uintptr_t)(3));
                gm_th2lqbo77pvsc t74;
                gm_f_strip_prefix_b4949s8((void *)&t74, t72, t73);
                gm_th2lqbo77pvsc t75 = t74;
                gm_b16_8 t76;
                if (((((uint8_t *)&t75))[16] != 0)) {
                    gm_b16_8 t77 = GM_LD(gm_b16_8, (((uint8_t *)&t75) + 0));
                    t76 = t77;
                } else {
                    goto lb38;
                }
                gm_l12 = t76;
            }
            {
                gm_b16_8 t79 = gm_l12;
                gm_b16_8 t80;
                GM_ST(void *, (uint8_t *)&t80, (void *)(gm_st3ja0nqi37bv));
                GM_ST(uintptr_t, (uint8_t *)&t80 + 8, (uintptr_t)(1));
                uint8_t t81 = gm_f_starts_with_6vvt01e(t79, t80);
                if (!t81) goto ie78;
                {
                    goto lb38;
                }
                goto ix78;
ie78:;
ix78:;
            }
            {
                void * t83 = (void *)((uint8_t *)&F.l7);
                gm_b16_8 t84 = gm_l12;
                gm_b16_8 t85 = gm_f_strip_one_space_smv2rv8(t84);
                gm_tftb1odc739t5 t86 = gm_f_push_3ef746b(t83, t85);
                gm_tftb1odc739t5 t87 = t86;
                if (((((uint8_t *)&t87))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t87) + 1, gm_srciagdq75um1d5, 449, 9);
                gm_b0_1 t88 = GM_LD(gm_b0_1, (((uint8_t *)&t87) + 1));
            }
            {
                uint64_t t89 = gm_l9;
                gm_l8 = t89;
            }
lc38:;
        }
lb38:;
    }
    {
        uint64_t t91 = GM_LD(uintptr_t, (((uint8_t *)&F.l7) + 0) + 8);
        uint8_t t92 = (t91 > ((uint64_t)0ULL));
        if (!t92) goto ie90;
        {
            gm_b16_8 t94 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 0));
            gm_f_reverse_cgmjfos(t94);
        }
        {
            void * t95 = gm_l1;
            gm_t6j6smj7qoctu t96;
            gm_b16_8 t97 = gm_l2;
            GM_ST(gm_b16_8, (uint8_t *)&t96 + 0, t97);
            gm_b16_8 t98 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 0));
            gm_b16_8 t99 = t98;
            gm_b16_8 t100 = GM_LD(gm_b16_8, ((uint8_t *)&t99));
            gm_b16_8 t101;
            GM_ST(void *, (uint8_t *)&t101, (void *)(gm_so514oj5nbhcu));
            GM_ST(uintptr_t, (uint8_t *)&t101 + 8, (uintptr_t)(1));
            gm_b16_8 t102 = gm_f_join_e7fm1oo((void *)NULL, cx, NULL, t100, t101);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t96 + 16, t102);
            gm_tftb1odc739t5 t103 = gm_f_push_m55rf90_3(t95, &t96);
            gm_tftb1odc739t5 t104 = t103;
            if (((((uint8_t *)&t104))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t104) + 1, gm_srciagdq75um1d5, 454, 9);
            gm_b0_1 t105 = GM_LD(gm_b0_1, (((uint8_t *)&t104) + 1));
        }
        goto ix90;
ie90:;
ix90:;
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
__attribute__((aligned(4))) gm_b16_8 gm_f_missing_6dl2vil(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_missing_6dl2vil F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 758u);
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
        void * t3 = gm_l0;
        gm_b16_8 t4 = GM_LD(gm_b16_8, (((uint8_t *)t3) + 32));
        gm_b16_8 t5 = gm_f_trim_ebi5drb(t4);
        gm_b16_8 t6 = t5;
        uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&t6) + 8);
        uint8_t t8 = ((t7 == ((uint64_t)0ULL)));
        if (!t8) goto ie2;
        {
            void * t10 = (void *)((uint8_t *)&F.l1);
            gm_t6j6smj7qoctu t11;
            void * t12 = gm_l0;
            gm_b16_8 t13 = GM_LD(gm_b16_8, (((uint8_t *)t12) + 64));
            GM_ST(gm_b16_8, (uint8_t *)&t11 + 0, t13);
            void * t14 = gm_l0;
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)t14) + 0));
            GM_ST(gm_b16_8, (uint8_t *)&t11 + 16, t15);
            gm_tabb710d9mq2e t16;
            gm_f_format_4qd139f((void *)&t16, (void *)NULL, cx, NULL, &t11);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t17 = t16;
            if (((((uint8_t *)&t17))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t17) + 8, gm_srciagdq75um1d5, 90, 18);
            gm_b16_8 t18 = GM_LD(gm_b16_8, (((uint8_t *)&t17) + 8));
            gm_b16_8 t19 = t18;
            gm_b16_8 t20 = GM_LD(gm_b16_8, ((uint8_t *)&t19));
            gm_tftb1odc739t5 t21 = gm_f_push_3ef746b(t10, t20);
            gm_tftb1odc739t5 t22 = t21;
            if (((((uint8_t *)&t22))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t22) + 1, gm_srciagdq75um1d5, 90, 9);
            gm_b0_1 t23 = GM_LD(gm_b0_1, (((uint8_t *)&t22) + 1));
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        {
            void * t25 = gm_l0;
            gm_b16_8 t26 = GM_LD(gm_b16_8, (((uint8_t *)t25) + 48));
            gm_l2 = t26;
        }
        {
            gm_l3 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t28 = gm_l3;
                uint64_t t29 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                uint8_t t30 = (t28 < t29);
                if (!t30) goto lb27;
                {
                    uint64_t t33 = gm_l3;
                    uint64_t t34 = (uintptr_t)t33;
                    uint64_t t35 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    gm_tk50ot32aqf1o t38 = GM_LD(gm_tk50ot32aqf1o, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t34 * 136));
                    F.l4 = t38;
                    {
                        uint8_t t41;
                        if ((((((uint8_t *)&F.l4) + 40))[16] != 0)) goto mn43;
                        t41 = ((uint8_t)1);
                        goto me42;
mn43:;
                        t41 = ((uint8_t)0);
                        goto me42;
mn44:;
                        gm_trap("no match arm matched the scrutinee", gm_srciagdq75um1d5, 93, 12);
me42:;
                        if (!t41) goto ie40;
                        uint8_t t45;
                        if ((((((uint8_t *)&F.l4) + 112))[16] != 0)) goto mn47;
                        t45 = ((uint8_t)1);
                        goto me46;
mn47:;
                        t45 = ((uint8_t)0);
                        goto me46;
mn48:;
                        gm_trap("no match arm matched the scrutinee", gm_srciagdq75um1d5, 93, 30);
me46:;
                        if (!t45) goto ie40;
                        {
                            void * t50 = (void *)((uint8_t *)&F.l1);
                            gm_tfeab279c16rr t51;
                            gm_b16_8 t52 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 96));
                            GM_ST(gm_b16_8, (uint8_t *)&t51 + 0, t52);
                            gm_t9qkh0rsooc8d t53 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&F.l4) + 0));
                            gm_b16_8 t54 = gm_f_noun_pdqc588(t53);
                            GM_ST(gm_b16_8, (uint8_t *)&t51 + 16, t54);
                            gm_b16_8 t55 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 8));
                            GM_ST(gm_b16_8, (uint8_t *)&t51 + 32, t55);
                            gm_tabb710d9mq2e t56;
                            gm_f_format_njo4244((void *)&t56, (void *)NULL, cx, NULL, &t51);
                            if (gm_unw.kind) {
                                goto gm_ll31;
                            }
                            gm_tabb710d9mq2e t57 = t56;
                            if (((((uint8_t *)&t57))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t57) + 8, gm_srciagdq75um1d5, 94, 22);
                            gm_b16_8 t58 = GM_LD(gm_b16_8, (((uint8_t *)&t57) + 8));
                            gm_b16_8 t59 = t58;
                            gm_b16_8 t60 = GM_LD(gm_b16_8, ((uint8_t *)&t59));
                            gm_tftb1odc739t5 t61 = gm_f_push_3ef746b(t50, t60);
                            gm_tftb1odc739t5 t62 = t61;
                            if (((((uint8_t *)&t62))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t62) + 1, gm_srciagdq75um1d5, 94, 13);
                            gm_b0_1 t63 = GM_LD(gm_b0_1, (((uint8_t *)&t62) + 1));
                        }
                        goto ix40;
ie40:;
ix40:;
                    }
                    goto me36;
mn37:;
                    gm_trap("no match arm matched the scrutinee", gm_srciagdq75um1d5, 92, 5);
me36:;
                }
lc27:;
                {
                    uint64_t t65 = gm_l3;
                    uint64_t r66; bool o67 = __builtin_add_overflow(t65, ((uint64_t)1ULL), &r66);
                    uint64_t t68 = r66;
                    gm_l3 = t68;
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
        gm_b16_8 t69 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
        uint8_t t70 = 0;
        F.ret = t69;
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
__attribute__((aligned(4))) gm_b16_8 gm_f_module_doc_noqdur2(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_module_doc_noqdur2 F;
    gm_b16_8 gm_l0; /* head */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_b16_8 gm_l4; /* l */
    gm_b16_8 gm_l5; /* rest */
    gm_b16_8 gm_l6; /* all */
    uint64_t gm_l7; /* start */
    uint64_t gm_l8; /* end */
    GM_STACK_CHECK(&F, 692u);
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
            gm_b16_8 t4 = gm_f_lines_of_t8v251a((void *)NULL, cx, NULL, t3);
            if (gm_unw.kind) {
                goto gm_lad;
            }
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
                    gm_b16_8 t16 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t12 * 16));
                    gm_l4 = t16;
                    {
                        gm_b16_8 t18 = gm_l4;
                        gm_b16_8 t19 = gm_f_trim_start_lj2dhj3(t18);
                        gm_b16_8 t20;
                        GM_ST(void *, (uint8_t *)&t20, (void *)(gm_shanrrdljtv7g));
                        GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(3));
                        gm_th2lqbo77pvsc t21;
                        gm_f_strip_prefix_b4949s8((void *)&t21, t19, t20);
                        gm_th2lqbo77pvsc t22 = t21;
                        gm_b16_8 t23;
                        if (((((uint8_t *)&t22))[16] != 0)) {
                            gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&t22) + 0));
                            t23 = t24;
                        } else {
                            goto lc5;
                        }
                        gm_l5 = t23;
                    }
                    {
                        void * t25 = (void *)((uint8_t *)&F.l1);
                        gm_b16_8 t26 = gm_l5;
                        gm_b16_8 t27 = gm_f_strip_one_space_smv2rv8(t26);
                        gm_tftb1odc739t5 t28 = gm_f_push_3ef746b(t25, t27);
                        gm_tftb1odc739t5 t29 = t28;
                        if (((((uint8_t *)&t29))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t29) + 1, gm_srciagdq75um1d5, 201, 9);
                        gm_b0_1 t30 = GM_LD(gm_b0_1, (((uint8_t *)&t29) + 1));
                    }
                    goto me14;
mn15:;
                    gm_trap("no match arm matched the scrutinee", gm_srciagdq75um1d5, 199, 5);
me14:;
                }
lc5:;
                {
                    uint64_t t32 = gm_l3;
                    uint64_t r33; bool o34 = __builtin_add_overflow(t32, ((uint64_t)1ULL), &r33);
                    uint64_t t35 = r33;
                    gm_l3 = t35;
                }
            }
lb5:;
        }
    }
    {
        gm_b16_8 t36 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
        gm_b16_8 t37 = t36;
        gm_b16_8 t38 = GM_LD(gm_b16_8, ((uint8_t *)&t37));
        gm_b16_8 t39;
        GM_ST(void *, (uint8_t *)&t39, (void *)(gm_so514oj5nbhcu));
        GM_ST(uintptr_t, (uint8_t *)&t39 + 8, (uintptr_t)(1));
        gm_b16_8 t40 = gm_f_join_e7fm1oo((void *)NULL, cx, NULL, t38, t39);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l6 = t40;
    }
    {
        gm_l7 = ((uint64_t)0ULL);
    }
    {
        uint64_t t41 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
        gm_l8 = t41;
    }
    {
        for (;;) {
            uint64_t t43 = gm_l7;
            uint64_t t44 = gm_l8;
            uint8_t t45 = (t43 < t44);
            if (!t45) goto lb42;
            uint64_t t46 = gm_l7;
            uint64_t t47 = (uintptr_t)t46;
            uint64_t t48 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
            if (t47 >= (uintptr_t)(t48)) gm_trap_index(t47, t48, gm_srciagdq75um1d5, 206, 27);
            uint8_t t49 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t47 * 1));
            uint8_t t50 = ((t49 == ((uint8_t)10ULL)));
            if (!t50) goto lb42;
            {
                uint64_t t53 = gm_l7;
                uint64_t r54; bool o55 = __builtin_add_overflow(t53, ((uint64_t)1ULL), &r54);
                if (o55) gm_trap("integer overflow in `+`", gm_srciagdq75um1d5, 206, 48);
                uint64_t t56 = r54;
                gm_l7 = t56;
            }
lc42:;
        }
lb42:;
    }
    {
        for (;;) {
            uint64_t t58 = gm_l8;
            uint64_t t59 = gm_l7;
            uint8_t t60 = (t58 > t59);
            if (!t60) goto lb57;
            uint64_t t61 = gm_l8;
            uint64_t r62; bool o63 = __builtin_sub_overflow(t61, ((uint64_t)1ULL), &r62);
            if (o63) gm_trap("integer overflow in `-`", gm_srciagdq75um1d5, 207, 31);
            uint64_t t64 = r62;
            uint64_t t65 = (uintptr_t)t64;
            uint64_t t66 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
            if (t65 >= (uintptr_t)(t66)) gm_trap_index(t65, t66, gm_srciagdq75um1d5, 207, 27);
            uint8_t t67 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t65 * 1));
            uint8_t t68 = ((t67 == ((uint8_t)10ULL)));
            if (!t68) goto lb57;
            {
                uint64_t t71 = gm_l8;
                uint64_t r72; bool o73 = __builtin_sub_overflow(t71, ((uint64_t)1ULL), &r72);
                if (o73) gm_trap("integer overflow in `-`", gm_srciagdq75um1d5, 207, 50);
                uint64_t t74 = r72;
                gm_l8 = t74;
            }
lc57:;
        }
lb57:;
    }
    {
        uint64_t t75 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
        void * t76 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)));
        uint64_t t77 = gm_l7;
        uint64_t t78 = (uintptr_t)t77;
        uint64_t t79 = gm_l8;
        uint64_t t80 = (uintptr_t)t79;
        if (t78 > t80 || t80 > (uintptr_t)t75) gm_trap_slice(t78, t80, t75, gm_srciagdq75um1d5, 208, 12);
        gm_b16_8 t81;
        GM_ST(void *, (uint8_t *)&t81, (void *)(((uint8_t *)t76 + t78 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t81 + 8, (uintptr_t)(t80 - t78));
        uint8_t t82 = 0;
        F.ret = t81;
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
__attribute__((aligned(4))) void gm_f_module_of_a4sic60(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2) {
    struct gm_fr_module_of_a4sic60 F;
    void * gm_l0; /* r */
    gm_b16_8 gm_l1; /* name */
    gm_b16_8 gm_l2; /* path */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    gm_b16_8 gm_l11; /* t */
    GM_STACK_CHECK(&F, 821u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        gm_f_list_epvducg((void *)&F.l3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            void * t3 = gm_l0;
            gm_b16_8 t4 = GM_LD(gm_b16_8, ((((uint8_t *)t3) + 112) + 0));
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
                    gm_tk50ot32aqf1o t16 = GM_LD(gm_tk50ot32aqf1o, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t12 * 136));
                    F.l6 = t16;
                    {
                        uint8_t t19 = (!(GM_LD(uint8_t, (((uint8_t *)&F.l6) + 0) + 0) == ((uint8_t)4ULL)));
                        if (!t19) goto ie18;
                        {
                            void * t21 = (void *)((uint8_t *)&F.l3);
                            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 8));
                            gm_tftb1odc739t5 t23 = gm_f_push_3ef746b(t21, t22);
                            gm_tftb1odc739t5 t24 = t23;
                            if (((((uint8_t *)&t24))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t24) + 1, gm_srciagdq75um1d5, 158, 37);
                            gm_b0_1 t25 = GM_LD(gm_b0_1, (((uint8_t *)&t24) + 1));
                        }
                        goto ix18;
ie18:;
ix18:;
                    }
                    goto me14;
mn15:;
                    gm_trap("no match arm matched the scrutinee", gm_srciagdq75um1d5, 157, 5);
me14:;
                }
lc5:;
                {
                    uint64_t t27 = gm_l5;
                    uint64_t r28; bool o29 = __builtin_add_overflow(t27, ((uint64_t)1ULL), &r28);
                    uint64_t t30 = r28;
                    gm_l5 = t30;
                }
            }
lb5:;
        }
    }
    {
        gm_f_list_016fo30_3((void *)&F.l7, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            void * t32 = gm_l0;
            gm_b16_8 t33 = GM_LD(gm_b16_8, ((((uint8_t *)t32) + 112) + 0));
            gm_l8 = t33;
        }
        {
            gm_l9 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t35 = gm_l9;
                uint64_t t36 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                uint8_t t37 = (t35 < t36);
                if (!t37) goto lb34;
                {
                    uint64_t t40 = gm_l9;
                    uint64_t t41 = (uintptr_t)t40;
                    uint64_t t42 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                    gm_tk50ot32aqf1o t45 = GM_LD(gm_tk50ot32aqf1o, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l8)) + t41 * 136));
                    F.l10 = t45;
                    {
                        if (!(((((uint8_t *)&F.l10) + 112))[16] != 0)) goto ie47;
                        gm_b16_8 t48 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 112));
                        gm_l11 = t48;
                        {
                            gm_b16_8 t51 = gm_l11;
                            gm_b16_8 t52 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
                            gm_b16_8 t53 = t52;
                            uint8_t t54 = 0;
                            gm_b16_8 t55 = t51;
                            for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t53) + 8)); i++) if (gm_eq0iqiubfm7e(((uint8_t *)GM_LD(void *, ((uint8_t *)&t53))) + i * 16, ((uint8_t *)&t55))) { t54 = 1; break; }
                            uint8_t t56 = (uint8_t)(!t54);
                            if (!t56) goto ie50;
                            {
                                goto lc34;
                            }
                            goto ix50;
ie50:;
ix50:;
                        }
                        goto ix47;
ie47:;
ix47:;
                    }
                    {
                        void * t58 = (void *)((uint8_t *)&F.l7);
                        gm_tftb1odc739t5 t59 = gm_f_push_rda4o9n_3(t58, &(*(gm_tk50ot32aqf1o *)((uint8_t *)&F.l10)));
                        gm_tftb1odc739t5 t60 = t59;
                        if (((((uint8_t *)&t60))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t60) + 1, gm_srciagdq75um1d5, 165, 9);
                        gm_b0_1 t61 = GM_LD(gm_b0_1, (((uint8_t *)&t60) + 1));
                    }
                    goto me43;
mn44:;
                    gm_trap("no match arm matched the scrutinee", gm_srciagdq75um1d5, 161, 5);
me43:;
                }
lc34:;
                {
                    uint64_t t63 = gm_l9;
                    uint64_t r64; bool o65 = __builtin_add_overflow(t63, ((uint64_t)1ULL), &r64);
                    uint64_t t66 = r64;
                    gm_l9 = t66;
                }
            }
lb34:;
        }
    }
    {
        gm_b16_8 t67 = gm_l1;
        gm_b16_8 t68 = gm_l2;
        void * t69 = gm_l0;
        gm_b16_8 t70 = GM_LD(gm_b16_8, ((((uint8_t *)t69) + 152) + 0));
        gm_b16_8 t71 = t70;
        gm_b16_8 t72 = GM_LD(gm_b16_8, ((uint8_t *)&t71));
        gm_b16_8 t73;
        GM_ST(void *, (uint8_t *)&t73, (void *)(gm_s8dbdqj1sqibj));
        GM_ST(uintptr_t, (uint8_t *)&t73 + 8, (uintptr_t)(2));
        gm_b16_8 t74 = gm_f_join_e7fm1oo((void *)NULL, cx, NULL, t72, t73);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t75 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 0));
        void * t76 = gm_l0;
        gm_b16_8 t77 = GM_LD(gm_b16_8, (((uint8_t *)t76) + 192));
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tud7oovfvnf43 *)gm_out) + 0, t67);
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tud7oovfvnf43 *)gm_out) + 16, t68);
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tud7oovfvnf43 *)gm_out) + 32, t74);
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tud7oovfvnf43 *)gm_out) + 48, t75);
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tud7oovfvnf43 *)gm_out) + 64, t77);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_noun_pdqc588(gm_t9qkh0rsooc8d a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_noun_pdqc588 F;
    gm_t9qkh0rsooc8d gm_l0; /* k */
    GM_STACK_CHECK_SHALLOW(&F, 220u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t1;
    uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    switch (t14) { case ((uint8_t)0ULL): goto ma3; case ((uint8_t)1ULL): goto ma4; case ((uint8_t)2ULL): goto ma5; case ((uint8_t)3ULL): goto ma6; case ((uint8_t)4ULL): goto ma7; case ((uint8_t)5ULL): goto ma8; case ((uint8_t)6ULL): goto ma9; case ((uint8_t)7ULL): goto ma10; case ((uint8_t)8ULL): goto ma11; case ((uint8_t)9ULL): goto ma12; default: goto ma13; }
ma3:;
    uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t16 != ((uint8_t)0ULL)) goto mn15;
    gm_b16_8 t17;
    GM_ST(void *, (uint8_t *)&t17, (void *)(gm_sc49g68j2cekh));
    GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)(6));
    t1 = t17;
    goto me2;
mn15:;
ma4:;
    uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t19 != ((uint8_t)1ULL)) goto mn18;
    gm_b16_8 t20;
    GM_ST(void *, (uint8_t *)&t20, (void *)(gm_s8p1h96ji9kh0));
    GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(5));
    t1 = t20;
    goto me2;
mn18:;
ma5:;
    uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t22 != ((uint8_t)2ULL)) goto mn21;
    gm_b16_8 t23;
    GM_ST(void *, (uint8_t *)&t23, (void *)(gm_skr6p9lfc5ftp));
    GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(10));
    t1 = t23;
    goto me2;
mn21:;
ma6:;
    uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t25 != ((uint8_t)3ULL)) goto mn24;
    gm_b16_8 t26;
    GM_ST(void *, (uint8_t *)&t26, (void *)(gm_svqdrhms9i231));
    GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)(6));
    t1 = t26;
    goto me2;
mn24:;
ma7:;
    uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t28 != ((uint8_t)4ULL)) goto mn27;
    gm_b16_8 t29;
    GM_ST(void *, (uint8_t *)&t29, (void *)(gm_sic5vsema0r36));
    GM_ST(uintptr_t, (uint8_t *)&t29 + 8, (uintptr_t)(7));
    t1 = t29;
    goto me2;
mn27:;
ma8:;
    uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t31 != ((uint8_t)5ULL)) goto mn30;
    gm_b16_8 t32;
    GM_ST(void *, (uint8_t *)&t32, (void *)(gm_svklio0qebpog));
    GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(8));
    t1 = t32;
    goto me2;
mn30:;
ma9:;
    uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t34 != ((uint8_t)6ULL)) goto mn33;
    gm_b16_8 t35;
    GM_ST(void *, (uint8_t *)&t35, (void *)(gm_sjss9mvc5fcod));
    GM_ST(uintptr_t, (uint8_t *)&t35 + 8, (uintptr_t)(8));
    t1 = t35;
    goto me2;
mn33:;
ma10:;
    uint8_t t37 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t37 != ((uint8_t)7ULL)) goto mn36;
    gm_b16_8 t38;
    GM_ST(void *, (uint8_t *)&t38, (void *)(gm_sq0dqiq83b924));
    GM_ST(uintptr_t, (uint8_t *)&t38 + 8, (uintptr_t)(8));
    t1 = t38;
    goto me2;
mn36:;
ma11:;
    uint8_t t40 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t40 != ((uint8_t)8ULL)) goto mn39;
    gm_b16_8 t41;
    GM_ST(void *, (uint8_t *)&t41, (void *)(gm_sa0amdcmvej1l));
    GM_ST(uintptr_t, (uint8_t *)&t41 + 8, (uintptr_t)(13));
    t1 = t41;
    goto me2;
mn39:;
ma12:;
    uint8_t t43 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t43 != ((uint8_t)9ULL)) goto mn42;
    gm_b16_8 t44;
    GM_ST(void *, (uint8_t *)&t44, (void *)(gm_sg3kpa3h16no3));
    GM_ST(uintptr_t, (uint8_t *)&t44 + 8, (uintptr_t)(6));
    t1 = t44;
    goto me2;
mn42:;
ma13:;
    gm_trap("no match arm matched the scrutinee", gm_srciagdq75um1d5, 33, 34);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_read_file_dac5brv(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2) {
    struct gm_fr_read_file_dac5brv F;
    void * gm_l0; /* r */
    gm_b16_8 gm_l1; /* path */
    gm_b16_8 gm_l2; /* src */
    uint32_t gm_l3; /* id */
    gm_t08olq97rscn3 gm_l5; /* tree */
    uint64_t gm_l6; /* first */
    gm_b16_8 gm_l7; /* doc */
    GM_STACK_CHECK(&F, 1105u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t3 = gm_l0;
        uint64_t t4 = GM_LD(uintptr_t, (((uint8_t *)t3) + 192) + 8);
        uint8_t t5 = ((t4 == ((uint64_t)0ULL)));
        if (!t5) goto ie2;
        {
            void * t7 = gm_l0;
            gm_b16_8 t8 = gm_l1;
            GM_ST(gm_b16_8, (((uint8_t *)t7) + 192), t8);
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_b16_8 t10 = gm_l2;
        uint8_t t11 = gm_f_utf8_valid_a8oc1t8(t10);
        uint8_t t12 = (uint8_t)!t11;
        if (!t12) goto ie9;
        {
            void * t14 = gm_l0;
            void * t15 = (void *)(((uint8_t *)t14) + 40);
            gm_tl874t2atnlh3 t16 = GM_LD(gm_tl874t2atnlh3, gm_cejn1dj0054);
            gm_t3877pk7lv748 t17;
            gm_b16_8 t18 = gm_l1;
            GM_ST(gm_b16_8, (uint8_t *)&t17 + 0, t18);
            gm_b16_8 t19 = gm_l2;
            uint64_t t20 = gm_f_valid_up_to_8vv3poh(t19);
            GM_ST(uint64_t, (uint8_t *)&t17 + 16, t20);
            gm_tabb710d9mq2e t21;
            gm_f_format_buut5mk((void *)&t21, (void *)NULL, cx, NULL, &t17);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t22 = t21;
            if (((((uint8_t *)&t22))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t22) + 8, gm_srciagdq75um1d5, 138, 38);
            gm_b16_8 t23 = GM_LD(gm_b16_8, (((uint8_t *)&t22) + 8));
            gm_b16_8 t24 = t23;
            gm_b16_8 t25 = GM_LD(gm_b16_8, ((uint8_t *)&t24));
            gm_b0_1 t26 = gm_f_report_eardf07((void *)NULL, cx, NULL, t15, t16, t25);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t27 = gm_l0;
            void * t28 = (void *)(((uint8_t *)t27) + 0);
            void * t29 = gm_l0;
            void * t30 = (void *)(((uint8_t *)t29) + 40);
            gm_b16_8 t31 = gm_f_render_diagnostics_65a9o86((void *)NULL, cx, NULL, t28, t30);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tnpifv20irnph t32;
            GM_ST(gm_b16_8, ((uint8_t *)&t32 + 0), t31);
            gm_t5ghc3lnpmg2h t33;
            GM_ST(uint8_t, (uint8_t *)&t33 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t33 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t33 + 4, 0);
            ((uint8_t *)&t33)[0] = 1;
            GM_ST(gm_tnpifv20irnph, ((uint8_t *)&t33 + 8), t32);
            gm_t5ghc3lnpmg2h t34 = t33;
            uint8_t t35 = ((((uint8_t *)&t34))[0] != 0);
            F.ret = t33;
            goto gm_fret;
        }
        goto ix9;
ie9:;
ix9:;
    }
    {
        void * t36 = gm_l0;
        void * t37 = (void *)(((uint8_t *)t36) + 0);
        gm_b16_8 t38 = gm_l1;
        gm_b16_8 t39 = gm_l2;
        uint32_t t40 = gm_f_add_pva1tcq((void *)NULL, cx, NULL, t37, t38, t39);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l3 = t40;
    }
    {
        uint32_t t41 = gm_l3;
        gm_b16_8 t42 = gm_l2;
        void * t43 = gm_l0;
        void * t44 = (void *)(((uint8_t *)t43) + 40);
        gm_f_lex_dc8mbmf((void *)&F.l4, (void *)NULL, cx, NULL, t41, t42, t44);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint32_t t45 = gm_l3;
        void * t46 = (void *)((uint8_t *)&F.l4);
        void * t47 = gm_l0;
        void * t48 = (void *)(((uint8_t *)t47) + 104);
        void * t49 = gm_l0;
        void * t50 = (void *)(((uint8_t *)t49) + 40);
        gm_f_parse_file_70g7vok((void *)&gm_l5, (void *)NULL, cx, NULL, t45, t46, t48, t50);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t52 = gm_l0;
        void * t53 = (void *)(((uint8_t *)t52) + 40);
        uint8_t t54 = gm_f_has_errors_if7ai0m(t53);
        if (!t54) goto ie51;
        {
            void * t56 = gm_l0;
            void * t57 = (void *)(((uint8_t *)t56) + 0);
            void * t58 = gm_l0;
            void * t59 = (void *)(((uint8_t *)t58) + 40);
            gm_b16_8 t60 = gm_f_render_diagnostics_65a9o86((void *)NULL, cx, NULL, t57, t59);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tnpifv20irnph t61;
            GM_ST(gm_b16_8, ((uint8_t *)&t61 + 0), t60);
            gm_t5ghc3lnpmg2h t62;
            GM_ST(uint8_t, (uint8_t *)&t62 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t62 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t62 + 4, 0);
            ((uint8_t *)&t62)[0] = 1;
            GM_ST(gm_tnpifv20irnph, ((uint8_t *)&t62 + 8), t61);
            gm_t5ghc3lnpmg2h t63 = t62;
            uint8_t t64 = ((((uint8_t *)&t63))[0] != 0);
            F.ret = t62;
            goto gm_fret;
        }
        goto ix51;
ie51:;
ix51:;
    }
    {
        uint64_t t65;
        uint64_t t67 = GM_LD(uintptr_t, (((uint8_t *)&gm_l5) + 0) + 8);
        uint8_t t68 = (t67 > ((uint64_t)0ULL));
        if (!t68) goto ie66;
        uint64_t t70 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t71 = GM_LD(uintptr_t, (((uint8_t *)&gm_l5) + 0) + 8);
        if (t70 >= (uintptr_t)(t71)) gm_trap_index(t70, t71, gm_srciagdq75um1d5, 145, 44);
        uint32_t t72 = GM_LD(uint32_t, ((((uint8_t *)GM_LD(void *, (((uint8_t *)&gm_l5) + 0)) + t70 * 160) + 4) + 4));
        uint64_t t73 = (uint64_t)t72;
        uint64_t t74 = t73;
        t65 = t74;
        goto ix66;
ie66:;
        uint64_t t76 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        uint64_t t77 = t76;
        t65 = t77;
ix66:;
        gm_l6 = t65;
    }
    {
        uint64_t t78 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        void * t79 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)));
        uint64_t t80 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t81 = gm_l6;
        uint64_t t82 = (uintptr_t)t81;
        if (t80 > t82 || t82 > (uintptr_t)t78) gm_trap_slice(t80, t82, t78, gm_srciagdq75um1d5, 146, 23);
        gm_b16_8 t83;
        GM_ST(void *, (uint8_t *)&t83, (void *)(((uint8_t *)t79 + t80 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t83 + 8, (uintptr_t)(t82 - t80));
        gm_b16_8 t84 = gm_f_module_doc_noqdur2((void *)NULL, cx, NULL, t83);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l7 = t84;
    }
    {
        uint64_t t86 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
        uint8_t t87 = (t86 > ((uint64_t)0ULL));
        if (!t87) goto ie85;
        {
            void * t89 = gm_l0;
            void * t90 = (void *)(((uint8_t *)t89) + 152);
            gm_b16_8 t91 = gm_l7;
            gm_tftb1odc739t5 t92 = gm_f_push_3ef746b(t90, t91);
            gm_tftb1odc739t5 t93 = t92;
            if (((((uint8_t *)&t93))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t93) + 1, gm_srciagdq75um1d5, 147, 22);
            gm_b0_1 t94 = GM_LD(gm_b0_1, (((uint8_t *)&t93) + 1));
        }
        goto ix85;
ie85:;
ix85:;
    }
    {
        memset((uint8_t *)&F.l8 + 44, 0, 4);
        gm_b16_8 t95 = gm_l2;
        GM_ST(gm_b16_8, (uint8_t *)&F.l8 + 0, t95);
        gm_b16_8 t96 = gm_l1;
        GM_ST(gm_b16_8, (uint8_t *)&F.l8 + 16, t96);
        void * t97 = gm_l0;
        void * t98 = (void *)(((uint8_t *)t97) + 0);
        GM_ST(void *, (uint8_t *)&F.l8 + 32, t98);
        uint32_t t99 = gm_l3;
        GM_ST(uint32_t, (uint8_t *)&F.l8 + 40, t99);
    }
    {
        void * t100 = (void *)((uint8_t *)&F.l8);
        gm_b16_8 t101 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 0));
        gm_b16_8 t102 = t101;
        gm_b16_8 t103 = GM_LD(gm_b16_8, ((uint8_t *)&t102));
        gm_th2lqbo77pvsc t104;
        memset(&t104, 0, sizeof t104);
        void * t105 = gm_l0;
        void * t106 = (void *)(((uint8_t *)t105) + 112);
        gm_b0_1 t107 = gm_f_collect_items_bck8of3((void *)NULL, cx, NULL, t100, t103, &t104, t106);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b0_1 t108;
        gm_t5ghc3lnpmg2h t109;
        GM_ST(uint8_t, (uint8_t *)&t109 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t109 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t109 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t109 + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&t109 + 16, 0);
        ((uint8_t *)&t109)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t109 + 8), t108);
        gm_t5ghc3lnpmg2h t110 = t109;
        uint8_t t111 = ((((uint8_t *)&t110))[0] != 0);
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
inline __attribute__((aligned(4))) void gm_f_reader_rcu54pm(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_reader_rcu54pm F;
    GM_STACK_CHECK(&F, 200u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memset((uint8_t *)&(*(gm_t9672f4jnou6n *)gm_out) + 108, 0, 4);
    gm_tin97tt4ud0s1 t1;
    gm_f_source_map_4v8dg4b((void *)&t1, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t2792tsg5eg72 t2;
    gm_f_diagnostics_n3kkhf4((void *)&t2, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trqfvmjtt4sij t3;
    gm_f_list_016fo30_3((void *)&t3, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trqfvmjtt4sij t4;
    gm_f_list_epvducg((void *)&t4, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t5 = GM_LD(gm_b16_8, gm_cramav0v1j7);
    GM_ST(gm_tin97tt4ud0s1, (uint8_t *)&(*(gm_t9672f4jnou6n *)gm_out) + 0, t1);
    GM_ST(gm_t2792tsg5eg72, (uint8_t *)&(*(gm_t9672f4jnou6n *)gm_out) + 40, t2);
    GM_ST(uint32_t, (uint8_t *)&(*(gm_t9672f4jnou6n *)gm_out) + 104, ((uint32_t)0ULL));
    GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_t9672f4jnou6n *)gm_out) + 112, t3);
    GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_t9672f4jnou6n *)gm_out) + 152, t4);
    GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t9672f4jnou6n *)gm_out) + 192, t5);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_render_diagnostics_65a9o86(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_render_diagnostics_65a9o86 F;
    void * gm_l0; /* sm */
    void * gm_l1; /* diags */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    GM_STACK_CHECK(&F, 419u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l1;
        gm_f_sort_s8jqb1h(t2);
    }
    {
        gm_f_list_uvfu1k1((void *)&F.l2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            void * t4 = gm_l1;
            gm_b16_8 t5 = GM_LD(gm_b16_8, ((((uint8_t *)t4) + 0) + 0));
            gm_l3 = t5;
        }
        {
            gm_l4 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t7 = gm_l4;
                uint64_t t8 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                uint8_t t9 = (t7 < t8);
                if (!t9) goto lb6;
                {
                    uint64_t t12 = gm_l4;
                    uint64_t t13 = (uintptr_t)t12;
                    uint64_t t14 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    gm_t4gut2c9fj1kt t17 = GM_LD(gm_t4gut2c9fj1kt, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t13 * 184));
                    F.l5 = t17;
                    {
                        void * t19 = gm_l0;
                        void * t20 = (void *)((uint8_t *)&F.l5);
                        gm_t9qkh0rsooc8d t21;
                        GM_ST(uint8_t, ((uint8_t *)&t21 + 0), ((uint8_t)0ULL));
                        void * t22 = (void *)((uint8_t *)&F.l2);
                        gm_b0_1 t23 = gm_f_render_pi7hvmf((void *)NULL, cx, NULL, t19, t20, t21, t22);
                        if (gm_unw.kind) {
                            goto gm_ll10;
                        }
                    }
                    goto me15;
mn16:;
                    gm_trap("no match arm matched the scrutinee", gm_srciagdq75um1d5, 174, 5);
me15:;
                }
lc6:;
                {
                    uint64_t t25 = gm_l4;
                    uint64_t r26; bool o27 = __builtin_add_overflow(t25, ((uint64_t)1ULL), &r26);
                    uint64_t t28 = r26;
                    gm_l4 = t28;
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
        gm_b16_8 t29 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
        gm_b16_8 t30 = t29;
        gm_b16_8 t31 = GM_LD(gm_b16_8, ((uint8_t *)&t30));
        uint8_t t32 = 0;
        F.ret = t31;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_section_9ecia9e(gm_t9qkh0rsooc8d a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_section_9ecia9e F;
    gm_t9qkh0rsooc8d gm_l0; /* k */
    GM_STACK_CHECK_SHALLOW(&F, 220u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t1;
    uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    switch (t14) { case ((uint8_t)0ULL): goto ma3; case ((uint8_t)1ULL): goto ma4; case ((uint8_t)2ULL): goto ma5; case ((uint8_t)3ULL): goto ma6; case ((uint8_t)4ULL): goto ma7; case ((uint8_t)5ULL): goto ma8; case ((uint8_t)6ULL): goto ma9; case ((uint8_t)7ULL): goto ma10; case ((uint8_t)8ULL): goto ma11; case ((uint8_t)9ULL): goto ma12; default: goto ma13; }
ma3:;
    uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t16 != ((uint8_t)0ULL)) goto mn15;
    gm_b16_8 t17;
    GM_ST(void *, (uint8_t *)&t17, (void *)(gm_sjtiiac2ha9i1));
    GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)(7));
    t1 = t17;
    goto me2;
mn15:;
ma4:;
    uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t19 != ((uint8_t)1ULL)) goto mn18;
    gm_b16_8 t20;
    GM_ST(void *, (uint8_t *)&t20, (void *)(gm_svt17hrer9pjd));
    GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(6));
    t1 = t20;
    goto me2;
mn18:;
ma5:;
    uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t22 != ((uint8_t)2ULL)) goto mn21;
    gm_b16_8 t23;
    GM_ST(void *, (uint8_t *)&t23, (void *)(gm_s05fn63o3te82));
    GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(12));
    t1 = t23;
    goto me2;
mn21:;
ma6:;
    uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t25 != ((uint8_t)3ULL)) goto mn24;
    gm_b16_8 t26;
    GM_ST(void *, (uint8_t *)&t26, (void *)(gm_sah2hl0iapccb));
    GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)(7));
    t1 = t26;
    goto me2;
mn24:;
ma7:;
    uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t28 != ((uint8_t)4ULL)) goto mn27;
    gm_b16_8 t29;
    GM_ST(void *, (uint8_t *)&t29, (void *)(gm_sjig620oug367));
    GM_ST(uintptr_t, (uint8_t *)&t29 + 8, (uintptr_t)(8));
    t1 = t29;
    goto me2;
mn27:;
ma8:;
    uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t31 != ((uint8_t)5ULL)) goto mn30;
    gm_b16_8 t32;
    GM_ST(void *, (uint8_t *)&t32, (void *)(gm_sv52j7o6hr9j8));
    GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(9));
    t1 = t32;
    goto me2;
mn30:;
ma9:;
    uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t34 != ((uint8_t)6ULL)) goto mn33;
    gm_b16_8 t35;
    GM_ST(void *, (uint8_t *)&t35, (void *)(gm_s64skgbd4ii1k));
    GM_ST(uintptr_t, (uint8_t *)&t35 + 8, (uintptr_t)(9));
    t1 = t35;
    goto me2;
mn33:;
ma10:;
    uint8_t t37 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t37 != ((uint8_t)7ULL)) goto mn36;
    gm_b16_8 t38;
    GM_ST(void *, (uint8_t *)&t38, (void *)(gm_sl1tolq1dm391));
    GM_ST(uintptr_t, (uint8_t *)&t38 + 8, (uintptr_t)(9));
    t1 = t38;
    goto me2;
mn36:;
ma11:;
    uint8_t t40 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t40 != ((uint8_t)8ULL)) goto mn39;
    gm_b16_8 t41;
    GM_ST(void *, (uint8_t *)&t41, (void *)(gm_sj8ni1it038ec));
    GM_ST(uintptr_t, (uint8_t *)&t41 + 8, (uintptr_t)(17));
    t1 = t41;
    goto me2;
mn39:;
ma12:;
    uint8_t t43 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t43 != ((uint8_t)9ULL)) goto mn42;
    gm_b16_8 t44;
    GM_ST(void *, (uint8_t *)&t44, (void *)(gm_sa05gpeedjved));
    GM_ST(uintptr_t, (uint8_t *)&t44 + 8, (uintptr_t)(7));
    t1 = t44;
    goto me2;
mn42:;
ma13:;
    gm_trap("no match arm matched the scrutinee", gm_srciagdq75um1d5, 19, 37);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) void * gm_f_strip_attrs_g0rb77v(void * a0);
extern __attribute__((aligned(4))) gm_b16_8 gm_f_text_epcdloe(void * a0, gm_cseahvg8v7p0vs a1);
inline __attribute__((aligned(4))) uint64_t gm_f_valid_up_to_8vv3poh(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_valid_up_to_8vv3poh F;
    gm_b16_8 gm_l0; /* text */
    uint64_t gm_l1; /* i */
    uint8_t gm_l2; /* c */
    uint64_t gm_l3; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 273u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_l1 = ((uint64_t)0ULL);
    }
    {
        for (;;) {
            uint64_t t3 = gm_l1;
            uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            uint8_t t5 = (t3 < t4);
            if (!t5) goto lb2;
            {
                uint64_t t8 = gm_l1;
                uint64_t t9 = (uintptr_t)t8;
                uint64_t t10 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                if (t9 >= (uintptr_t)(t10)) gm_trap_index(t9, t10, gm_srciagdq75um1d5, 182, 14);
                uint8_t t11 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t9 * 1));
                gm_l2 = t11;
            }
            {
                uint8_t t12 = gm_l2;
                uint8_t t13 = t12;
                uint64_t t14;
                uint8_t t17 = GM_LD(uint8_t, ((uint8_t *)&t13));
                if ((t17 < ((uint8_t)0ULL)) || (t17 > ((uint8_t)127ULL))) goto mn16;
                t14 = ((uint64_t)1ULL);
                goto me15;
mn16:;
                uint8_t t19 = GM_LD(uint8_t, ((uint8_t *)&t13));
                if ((t19 < ((uint8_t)194ULL)) || (t19 > ((uint8_t)223ULL))) goto mn18;
                t14 = ((uint64_t)2ULL);
                goto me15;
mn18:;
                uint8_t t21 = GM_LD(uint8_t, ((uint8_t *)&t13));
                if ((t21 < ((uint8_t)224ULL)) || (t21 > ((uint8_t)239ULL))) goto mn20;
                t14 = ((uint64_t)3ULL);
                goto me15;
mn20:;
                uint8_t t23 = GM_LD(uint8_t, ((uint8_t *)&t13));
                if ((t23 < ((uint8_t)240ULL)) || (t23 > ((uint8_t)244ULL))) goto mn22;
                t14 = ((uint64_t)4ULL);
                goto me15;
mn22:;
                t14 = ((uint64_t)0ULL);
                goto me15;
mn24:;
                gm_trap("no match arm matched the scrutinee", gm_srciagdq75um1d5, 183, 20);
me15:;
                gm_l3 = t14;
            }
            {
                uint8_t t26 = 0;
                uint8_t t27 = 0;
                uint64_t t28 = gm_l3;
                uint8_t t29 = ((t28 == ((uint64_t)0ULL)));
                if (!t29) {
                    uint64_t t30 = gm_l1;
                    uint64_t t31 = gm_l3;
                    uint64_t r32; bool o33 = __builtin_add_overflow(t30, t31, &r32);
                    if (o33) gm_trap("integer overflow in `+`", gm_srciagdq75um1d5, 190, 22);
                    uint64_t t34 = r32;
                    uint64_t t35 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    uint8_t t36 = (t34 > t35);
                    t27 = t36;
                } else t27 = 1;
                if (!t27) {
                    uint64_t t37 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                    void * t38 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
                    uint64_t t39 = gm_l1;
                    uint64_t t40 = (uintptr_t)t39;
                    uint64_t t41 = gm_l1;
                    uint64_t t42 = gm_l3;
                    uint64_t r43; bool o44 = __builtin_add_overflow(t41, t42, &r43);
                    if (o44) gm_trap("integer overflow in `+`", gm_srciagdq75um1d5, 190, 54);
                    uint64_t t45 = r43;
                    uint64_t t46 = (uintptr_t)t45;
                    if (t40 > t46 || t46 > (uintptr_t)t37) gm_trap_slice(t40, t46, t37, gm_srciagdq75um1d5, 190, 46);
                    gm_b16_8 t47;
                    GM_ST(void *, (uint8_t *)&t47, (void *)(((uint8_t *)t38 + t40 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t47 + 8, (uintptr_t)(t46 - t40));
                    uint8_t t48 = gm_f_utf8_valid_a8oc1t8(t47);
                    uint8_t t49 = (uint8_t)!t48;
                    t26 = t49;
                } else t26 = 1;
                if (!t26) goto ie25;
                {
                    uint64_t t51 = gm_l1;
                    uint8_t t52 = 0;
                    F.ret = t51;
                    goto gm_fret;
                }
                goto ix25;
ie25:;
ix25:;
            }
            {
                uint64_t t53 = gm_l1;
                uint64_t t54 = gm_l3;
                uint64_t r55; bool o56 = __builtin_add_overflow(t53, t54, &r55);
                if (o56) gm_trap("integer overflow in `+`", gm_srciagdq75um1d5, 191, 9);
                uint64_t t57 = r55;
                gm_l1 = t57;
            }
lc2:;
        }
lb2:;
    }
    {
        uint64_t t58 = gm_l1;
        uint8_t t59 = 0;
        F.ret = t58;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_variants_e6ibj0m(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, void * a2, void * a3) {
    struct gm_fr_variants_e6ibj0m F;
    void * gm_l0; /* cx */
    gm_b16_8 gm_l1; /* ms */
    void * gm_l2; /* body */
    void * gm_l3; /* docs */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_b16_8 gm_l9; /* e */
    GM_STACK_CHECK(&F, 619u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
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
                    gm_t03a3imodqi32 t15 = GM_LD(gm_t03a3imodqi32, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t11 * 72));
                    F.l6 = t15;
                    {
                        uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&F.l6) + 0));
                        if (t19 != ((uint8_t)0ULL)) goto mn18;
                        gm_tnljbma3s3h16 t20 = GM_LD(gm_tnljbma3s3h16, (((uint8_t *)&F.l6) + 8));
                        F.l7 = t20;
                        {
                            void * t22 = gm_l2;
                            void * t23 = gm_l0;
                            gm_tl874t2atnlh3 t24 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l7) + 48));
                            gm_b16_8 t25 = gm_f_text_epcdloe(t23, GM_CONV(gm_cseahvg8v7p0vs, t24));
                            gm_tftb1odc739t5 t26 = gm_f_push_3ef746b(t22, t25);
                            gm_tftb1odc739t5 t27 = t26;
                            if (((((uint8_t *)&t27))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t27) + 1, gm_srciagdq75um1d5, 422, 17);
                            gm_b0_1 t28 = GM_LD(gm_b0_1, (((uint8_t *)&t27) + 1));
                        }
                        {
                            void * t29 = gm_l0;
                            void * t30 = gm_l3;
                            void * t31 = gm_l0;
                            gm_tl874t2atnlh3 t32 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)&F.l7) + 0) + 16));
                            gm_b16_8 t33 = gm_f_text_epcdloe(t31, GM_CONV(gm_cseahvg8v7p0vs, t32));
                            uint32_t t34 = GM_LD(uint32_t, ((((uint8_t *)&F.l7) + 48) + 4));
                            gm_b0_1 t35 = gm_f_member_doc_9b4clb2((void *)NULL, cx, NULL, t29, t30, t33, t34);
                            if (gm_unw.kind) {
                                goto gm_ll8;
                            }
                        }
                        goto me17;
mn18:;
                        uint8_t t37 = GM_LD(uint8_t, (((uint8_t *)&F.l6) + 0));
                        if (t37 != ((uint8_t)1ULL)) goto mn36;
                        gm_t7bjbmukv26e7 t38 = GM_LD(gm_t7bjbmukv26e7, (((uint8_t *)&F.l6) + 8));
                        F.l8 = t38;
                        {
                            void * t40 = gm_l0;
                            gm_b16_8 t41 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 8));
                            gm_b16_8 t42 = t41;
                            gm_b16_8 t43 = GM_LD(gm_b16_8, ((uint8_t *)&t42));
                            void * t44 = gm_l2;
                            void * t45 = gm_l3;
                            gm_b0_1 t46 = gm_f_variants_e6ibj0m((void *)NULL, cx, NULL, t40, t43, t44, t45);
                            if (gm_unw.kind) {
                                goto gm_ll8;
                            }
                        }
                        {
                            if (!(((((uint8_t *)&F.l8) + 24))[16] != 0)) goto ie47;
                            gm_b16_8 t48 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 24));
                            gm_l9 = t48;
                            {
                                void * t50 = gm_l0;
                                gm_b16_8 t51 = gm_l9;
                                gm_b16_8 t52 = t51;
                                gm_b16_8 t53 = GM_LD(gm_b16_8, ((uint8_t *)&t52));
                                void * t54 = gm_l2;
                                void * t55 = gm_l3;
                                gm_b0_1 t56 = gm_f_variants_e6ibj0m((void *)NULL, cx, NULL, t50, t53, t54, t55);
                                if (gm_unw.kind) {
                                    goto gm_ll8;
                                }
                            }
                            goto ix47;
ie47:;
ix47:;
                        }
                        goto me17;
mn36:;
                        gm_trap("no match arm matched the scrutinee", gm_srciagdq75um1d5, 420, 9);
me17:;
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_srciagdq75um1d5, 419, 5);
me13:;
                }
lc4:;
                {
                    uint64_t t58 = gm_l5;
                    uint64_t r59; bool o60 = __builtin_add_overflow(t58, ((uint64_t)1ULL), &r59);
                    uint64_t t61 = r59;
                    gm_l5 = t61;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_with_attrs_429rjgv(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, gm_b16_8 a3) {
    struct gm_fr_with_attrs_429rjgv F;
    void * gm_l0; /* cx */
    void * gm_l1; /* item */
    gm_th2lqbo77pvsc gm_l2; /* cond */
    gm_b16_8 gm_l3; /* sig */
    gm_b16_8 gm_l5; /* c */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    GM_STACK_CHECK(&F, 482u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l3 = a3;
    {
        gm_f_list_uvfu1k1((void *)&F.l4, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        if (!((((uint8_t *)a2))[16] != 0)) goto ie2;
        gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)a2));
        gm_l5 = t3;
        {
            void * t5 = (void *)((uint8_t *)&F.l4);
            gm_b16_8 t6 = gm_l5;
            gm_tnpifv20irnph t7;
            GM_ST(gm_b16_8, ((uint8_t *)&t7 + 0), t6);
            gm_tftb1odc739t5 t8 = gm_f_write_kcc65a2(t5, t7);
            gm_tftb1odc739t5 t9 = t8;
            if (((((uint8_t *)&t9))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t9) + 1, gm_srciagdq75um1d5, 462, 20);
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        {
            void * t12 = gm_l1;
            gm_b16_8 t13 = GM_LD(gm_b16_8, (((uint8_t *)t12) + 40));
            gm_l6 = t13;
        }
        {
            gm_l7 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t15 = gm_l7;
                uint64_t t16 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                uint8_t t17 = (t15 < t16);
                if (!t17) goto lb14;
                {
                    uint64_t t20 = gm_l7;
                    uint64_t t21 = (uintptr_t)t20;
                    uint64_t t22 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    gm_tkacitepg5fk0 t25 = GM_LD(gm_tkacitepg5fk0, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t21 * 72));
                    F.l8 = t25;
                    {
                        void * t27 = (void *)((uint8_t *)&F.l4);
                        void * t28 = gm_l0;
                        gm_tl874t2atnlh3 t29 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l8) + 56));
                        gm_b16_8 t30 = gm_f_text_epcdloe(t28, GM_CONV(gm_cseahvg8v7p0vs, t29));
                        gm_tnpifv20irnph t31;
                        GM_ST(gm_b16_8, ((uint8_t *)&t31 + 0), t30);
                        gm_tftb1odc739t5 t32 = gm_f_write_cdouvhd(t27, t31);
                        gm_tftb1odc739t5 t33 = t32;
                        if (((((uint8_t *)&t33))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t33) + 1, gm_srciagdq75um1d5, 463, 27);
                        gm_b0_1 t34 = GM_LD(gm_b0_1, (((uint8_t *)&t33) + 1));
                    }
                    goto me23;
mn24:;
                    gm_trap("no match arm matched the scrutinee", gm_srciagdq75um1d5, 463, 5);
me23:;
                }
lc14:;
                {
                    uint64_t t36 = gm_l7;
                    uint64_t r37; bool o38 = __builtin_add_overflow(t36, ((uint64_t)1ULL), &r37);
                    uint64_t t39 = r37;
                    gm_l7 = t39;
                }
            }
lb14:;
        }
    }
    {
        void * t40 = (void *)((uint8_t *)&F.l4);
        gm_b16_8 t41 = gm_l3;
        gm_tftb1odc739t5 t42 = gm_f_extend_0cs85gs(t40, t41);
        gm_tftb1odc739t5 t43 = t42;
        if (((((uint8_t *)&t43))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t43) + 1, gm_srciagdq75um1d5, 464, 5);
        gm_b0_1 t44 = GM_LD(gm_b0_1, (((uint8_t *)&t43) + 1));
    }
    {
        gm_b16_8 t45 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 0));
        gm_b16_8 t46 = t45;
        gm_b16_8 t47 = GM_LD(gm_b16_8, ((uint8_t *)&t46));
        uint8_t t48 = 0;
        F.ret = t47;
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

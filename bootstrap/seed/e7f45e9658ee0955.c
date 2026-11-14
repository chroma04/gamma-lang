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
typedef struct { uint8_t a[8]; } gm_t2doi72kmmlil;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint8_t a[32]; } gm_t7mlrlibdq1ja;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint32_t a[8]; } gm_tg8e8rhc3qio9;
typedef struct { uint8_t f0; uint8_t p1[3]; union { int32_t v0; } f1; } gm_toe5fmr3vvf4d;
typedef struct { uint8_t a[64]; } gm_tsn02dhhitgs4;
typedef struct { uint8_t f1; } gm_ttsm01t0d8srg;
_Static_assert(sizeof(gm_t2doi72kmmlil) == 8 && _Alignof(gm_t2doi72kmmlil) == 1 && offsetof(gm_t2doi72kmmlil, a) == 0, "layout of `[8]u8`");
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_b16_8 a[6]; } gm_t4d1ka1c3gdt5;
typedef struct { uint64_t f0; gm_t9qkh0rsooc8d f1; uint8_t p2[7]; int64_t f2; uint32_t f3; uint8_t pe[4]; } gm_t5j14m8ru132p;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
_Static_assert(sizeof(gm_t7mlrlibdq1ja) == 32 && _Alignof(gm_t7mlrlibdq1ja) == 1 && offsetof(gm_t7mlrlibdq1ja, a) == 0, "layout of `[32]u8`");
typedef struct { gm_b16_8 a[3]; } gm_t9bmh80edou9c;
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { gm_b16_8 a[4]; } gm_tdmi4fk9nhqa7;
typedef struct { uint64_t f0; gm_b16_8 f1; } gm_tf1bjg81u0glv;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
_Static_assert(sizeof(gm_tg8e8rhc3qio9) == 32 && _Alignof(gm_tg8e8rhc3qio9) == 4 && offsetof(gm_tg8e8rhc3qio9, a) == 0, "layout of `[8]u32`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { int64_t f0; gm_b16_8 f1; } gm_thgmovcqlsivv;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_tibupt4dmcqai;
typedef struct { uint8_t f0; uint8_t p1[3]; uint32_t f1; gm_b16_8 f2; gm_b16_8 f3; } gm_tipro5npl6rd8;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; } f1; } gm_tl572d7579hns;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
_Static_assert(sizeof(gm_toe5fmr3vvf4d) == 8 && _Alignof(gm_toe5fmr3vvf4d) == 4 && offsetof(gm_toe5fmr3vvf4d, f0) == 0 && offsetof(gm_toe5fmr3vvf4d, f1) == 4, "layout of `IoError`");
typedef struct { gm_tg8e8rhc3qio9 f0; gm_tsn02dhhitgs4 f1; uint64_t f2; uint64_t f3; } gm_tq14qvifgdvpm;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
_Static_assert(sizeof(gm_tsn02dhhitgs4) == 64 && _Alignof(gm_tsn02dhhitgs4) == 1 && offsetof(gm_tsn02dhhitgs4, a) == 0, "layout of `[64]u8`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_toe5fmr3vvf4d v0; } f1; } gm_ttln0d0hd70en;
_Static_assert(sizeof(gm_ttsm01t0d8srg) == 1 && _Alignof(gm_ttsm01t0d8srg) == 1 && offsetof(gm_ttsm01t0d8srg, f1) == 0, "layout of `?()`");
_Static_assert(sizeof(gm_t4d1ka1c3gdt5) == 96 && _Alignof(gm_t4d1ka1c3gdt5) == 8 && offsetof(gm_t4d1ka1c3gdt5, a) == 0, "layout of `[6]string`");
_Static_assert(sizeof(gm_t5j14m8ru132p) == 32 && _Alignof(gm_t5j14m8ru132p) == 8 && offsetof(gm_t5j14m8ru132p, f0) == 0 && offsetof(gm_t5j14m8ru132p, f1) == 8 && offsetof(gm_t5j14m8ru132p, f2) == 16 && offsetof(gm_t5j14m8ru132p, f3) == 24, "layout of `FileInfo`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t9bmh80edou9c) == 48 && _Alignof(gm_t9bmh80edou9c) == 8 && offsetof(gm_t9bmh80edou9c, a) == 0, "layout of `[3]string`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tdmi4fk9nhqa7) == 64 && _Alignof(gm_tdmi4fk9nhqa7) == 8 && offsetof(gm_tdmi4fk9nhqa7, a) == 0, "layout of `[4]string`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; gm_tibupt4dmcqai v1; } f1; } gm_te2n4j2vtm53d;
_Static_assert(sizeof(gm_tf1bjg81u0glv) == 24 && _Alignof(gm_tf1bjg81u0glv) == 8 && offsetof(gm_tf1bjg81u0glv, f0) == 0 && offsetof(gm_tf1bjg81u0glv, f1) == 8, "layout of `(i: usize, x: string)`");
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `Toolchain`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_thgmovcqlsivv) == 24 && _Alignof(gm_thgmovcqlsivv) == 8 && offsetof(gm_thgmovcqlsivv, f0) == 0 && offsetof(gm_thgmovcqlsivv, f1) == 8, "layout of `CachedProgram`");
_Static_assert(sizeof(gm_tibupt4dmcqai) == 24 && _Alignof(gm_tibupt4dmcqai) == 8 && offsetof(gm_tibupt4dmcqai, f0) == 0 && offsetof(gm_tibupt4dmcqai, f1) == 16, "layout of `StrLit`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; gm_t5j14m8ru132p v1; } f1; } gm_tih6h4avv5nrb;
_Static_assert(sizeof(gm_tipro5npl6rd8) == 40 && _Alignof(gm_tipro5npl6rd8) == 8 && offsetof(gm_tipro5npl6rd8, f0) == 0 && offsetof(gm_tipro5npl6rd8, f1) == 4 && offsetof(gm_tipro5npl6rd8, f2) == 8 && offsetof(gm_tipro5npl6rd8, f3) == 24, "layout of `Ran`");
_Static_assert(sizeof(gm_tl572d7579hns) == 24 && _Alignof(gm_tl572d7579hns) == 8 && offsetof(gm_tl572d7579hns, f0) == 0 && offsetof(gm_tl572d7579hns, f1) == 8, "layout of `string!()`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tq14qvifgdvpm) == 112 && _Alignof(gm_tq14qvifgdvpm) == 8 && offsetof(gm_tq14qvifgdvpm, f0) == 0 && offsetof(gm_tq14qvifgdvpm, f1) == 32 && offsetof(gm_tq14qvifgdvpm, f2) == 96 && offsetof(gm_tq14qvifgdvpm, f3) == 104, "layout of `Sha256`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ttln0d0hd70en) == 12 && _Alignof(gm_ttln0d0hd70en) == 4 && offsetof(gm_ttln0d0hd70en, f0) == 0 && offsetof(gm_ttln0d0hd70en, f1) == 4, "layout of `IoError!()`");
_Static_assert(sizeof(gm_te2n4j2vtm53d) == 32 && _Alignof(gm_te2n4j2vtm53d) == 8 && offsetof(gm_te2n4j2vtm53d, f0) == 0 && offsetof(gm_te2n4j2vtm53d, f1) == 8, "layout of `IoError!Entry`");
_Static_assert(sizeof(gm_tih6h4avv5nrb) == 40 && _Alignof(gm_tih6h4avv5nrb) == 8 && offsetof(gm_tih6h4avv5nrb, f0) == 0 && offsetof(gm_tih6h4avv5nrb, f1) == 8, "layout of `IoError!FileInfo`");
struct gm_fr_adopt_object_psc9tpi {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_cache_key_tf6ebrd {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* buf */
    gm_b16_8 ret;
};
struct gm_fr_compiler_version_hj63jgf {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* argv */
    gm_tipro5npl6rd8 l2; /* r */
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_contains_gkki4kq {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_empty_slice_4pgooq2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_fn_3n129lo {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_for_body_1b63vnj {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_for_body_6ellthc {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_init_s32ien0 {
    gm_frame h;
    gm_tq14qvifgdvpm ret;
};
struct gm_fr_is_file_tcuv9rv {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_gcc_epb17a6 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_list_epvducg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_vuor028 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_private_name_u7i0rip {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_prune_eu93ghe {
    gm_frame h;
    gm_b16_8 l0; /* dir */
    int64_t l2; /* stale */
    gm_trqfvmjtt4sij l3; /* progs */
    gm_b0_1 ret;
    gm_b0_1 w0;
};
struct gm_fr_prune_units_iqs7tqd {
    gm_frame h;
    gm_b16_8 l0; /* dir */
    int64_t l2; /* stale */
    gm_trqfvmjtt4sij l3; /* objs */
    gm_b0_1 ret;
    gm_b0_1 w0;
};
struct gm_fr_rename_or_remove_l80dbfs {
    gm_frame h;
    gm_ttsm01t0d8srg ret;
};
struct gm_fr_runtime_object_oihv3ss {
    gm_frame h;
    gm_t4d1ka1c3gdt5 l7; /* <temporary array> */
    gm_tdmi4fk9nhqa7 l14; /* tail */
    gm_trqfvmjtt4sij l15; /* argv */
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_sha256_hex_4vhcm7o {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_sort_by_ci5a9mg {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_sum_ugu720i {
    gm_frame h;
    gm_tq14qvifgdvpm l1; /* s */
    gm_t7mlrlibdq1ja ret;
};
struct gm_fr_touch_frggs6u {
    gm_frame h;
    gm_t9bmh80edou9c l1; /* argv */
    gm_b0_1 ret;
};
struct gm_fr_unit_objects_3f31tcd {
    gm_frame h;
    gm_trqfvmjtt4sij l11; /* objects */
    gm_trqfvmjtt4sij l12; /* todo_c */
    gm_trqfvmjtt4sij l13; /* todo_tmp */
    gm_trqfvmjtt4sij l14; /* todo_obj */
    gm_t4d1ka1c3gdt5 l19; /* <temporary array> */
    gm_tl572d7579hns ret;
};
struct gm_fr_units_to_prune_qp6vjmt {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* out */
    gm_b16_8 ret;
};
__attribute__((aligned(4))) uint8_t gm_f_adopt_object_psc9tpi(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_cache_dir_81h90qu(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b16_8 gm_f_cache_key_tf6ebrd(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_command_b2u33fl(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) uint8_t gm_f_compile_objects_mu5secm(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2, gm_b16_8 a3, uint64_t a4);
__attribute__((aligned(4))) void gm_f_compiler_version_hj63jgf(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_contains_gkki4kq(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_copy_fg1ge35(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_deinit_r70pji5(void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_4pgooq2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
__attribute__((aligned(4))) uint8_t gm_f_ends_with_biriu5l(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_entries_44o5n9f_sh4kvg0(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_entries_44o5n9f_sre2b72(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_vogsd72(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_final_rg5ko3p(void *gm_out, void * a0);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_nab29co(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint8_t gm_f_fn_3n129lo(void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_1b63vnj(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_6ellthc(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_3pfhto5(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_c31q7f8(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_cncadt9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_n1il1vf(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_nk7pf4i(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_pf7dqps(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_soc2hp6(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_hex_euih242(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_init_s32ien0(void *gm_out);
__attribute__((aligned(4))) void gm_f_introsort_scs0qol(gm_b16_8 a0, gm_closure a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_file_tcuv9rv(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_gcc_epb17a6(const void * a0);
__attribute__((aligned(4))) uint8_t gm_f_is_gcc_epb17a6_c(gm_th2lqbo77pvsc a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_join_qveh5b2(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_join_sfknljb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_vuor028(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_make_dirs_gbp8nfr(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_private_name_u7i0rip(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_prune_eu93ghe(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_prune_units_iqs7tqd(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_4u3hstp(void * a0, const void * a1);
__attribute__((aligned(4))) gm_ttsm01t0d8srg gm_f_rename_or_remove_l80dbfs(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_resolve_lines_h4bf30f(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_run_mj5dobu(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_runtime_object_oihv3ss(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2, gm_b16_8 a3, const void * a4);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_sha256_hex_4vhcm7o(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_sort_by_ci5a9mg(gm_b16_8 a0, gm_closure a1);
__attribute__((aligned(4))) uint8_t gm_f_starts_with_6vvt01e(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_sum_ugu720i(void *gm_out, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_touch_frggs6u(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_unit_objects_3f31tcd(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2, const void * a3, uint64_t a4, uint8_t a5);
__attribute__((aligned(4))) gm_b16_8 gm_f_units_to_prune_qp6vjmt(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, int64_t a1);
__attribute__((aligned(4))) void gm_f_update_pn3fq6a(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_write_all_o8ucaji(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_jltv6p4(void * a0, const void * a1);
inline bool gm_lt0iqiubfm7e(const uint8_t *a, const uint8_t *b);
GM_NOINLINE bool gm_pf454qfrpt6r(gm_node *cx, const void *d, uintptr_t n, const char *file, uint32_t line, uint32_t col);
GM_NOINLINE bool gm_pfu8dq5bikbh(gm_node *cx, const gm_type *d, const void *v, const char *file, uint32_t line, uint32_t col);
extern void *const gm_hl8alodt65ii[];
extern void *const gm_hlgmpdevos39[];
extern const uint8_t gm_s0pi0emdqd3vb[2];
extern const uint8_t gm_s2cgbvfh1b7rp[6];
extern const uint8_t gm_s2gnpj7k4l22n[2];
extern const uint8_t gm_s4vd6e3mkiiso[3];
extern const uint8_t gm_s4vnlpe2ovpj8[6];
extern const uint8_t gm_s65qt8qo64cje[3];
extern const uint8_t gm_s7278rr8ss873[19];
extern const uint8_t gm_s8p032eorpf80[3];
extern const uint8_t gm_s9ab1ohnfcjkc[6];
extern const uint8_t gm_sa23fj423hlk4[6];
extern const uint8_t gm_sbkvlhvo2a3ic[7];
extern const uint8_t gm_se0f2jka437t4[2];
extern const uint8_t gm_sfbtgpom3l6k2[5];
extern const uint8_t gm_shl253arneuel[19];
extern const uint8_t gm_skou19srual26[10];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src3d0v42kavhv7[];
extern const char gm_src4rbevsr9gjng[];
extern const char gm_srcc2qutubarto1[];
extern const char gm_srcfjrpo4i6b0at[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_srd2ukct5o1qh[6];
extern const uint8_t gm_ssmr7jgj9e42t[25];
extern const uint8_t gm_suvsl6ie8luas[15];
extern const gm_type gm_ty0iqiubfm7e;
extern const gm_type gm_tyo86ks9qkvt;
inline __attribute__((aligned(4))) uint8_t gm_f_contains_gkki4kq(gm_b16_8 a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_contains_gkki4kq F;
    gm_b16_8 gm_l0; /* s */
    gm_b16_8 gm_l1; /* needle */
    GM_STACK_CHECK_SHALLOW(&F, 99u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_b16_8 t1 = gm_l0;
    gm_b16_8 t2 = gm_l1;
    gm_tf5srh4mp4b3i t3 = gm_f_find_nab29co(t1, t2);
    gm_tf5srh4mp4b3i t4 = t3;
    uint8_t t5;
    if (((((uint8_t *)&t4))[8] != 0)) goto mn7;
    t5 = ((uint8_t)0);
    goto me6;
mn7:;
    t5 = ((uint8_t)1);
    goto me6;
mn8:;
    gm_trap("no match arm matched the scrutinee", gm_src3d0v42kavhv7, 82, 50);
me6:;
    F.ret = t5;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_4pgooq2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_4pgooq2 F;
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
inline __attribute__((aligned(4))) uint8_t gm_f_fn_3n129lo(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fn_3n129lo F;
    void * gm_l0; /* a */
    void * gm_l1; /* b */
    GM_STACK_CHECK_SHALLOW(&F, 167u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t t1 = 0;
    void * t2 = gm_l0;
    int64_t t3 = GM_LD(int64_t, (((uint8_t *)t2) + 0));
    void * t4 = gm_l1;
    int64_t t5 = GM_LD(int64_t, (((uint8_t *)t4) + 0));
    uint8_t t6 = (t3 < t5);
    if (!t6) {
        uint8_t t7 = 0;
        void * t8 = gm_l0;
        int64_t t9 = GM_LD(int64_t, (((uint8_t *)t8) + 0));
        void * t10 = gm_l1;
        int64_t t11 = GM_LD(int64_t, (((uint8_t *)t10) + 0));
        uint8_t t12 = ((t9 == t11));
        if (t12) {
            void * t13 = gm_l0;
            gm_b16_8 t14 = GM_LD(gm_b16_8, (((uint8_t *)t13) + 8));
            void * t15 = gm_l1;
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)t15) + 8));
            gm_b16_8 t17 = t14;
            gm_b16_8 t18 = t16;
            uint8_t t19 = (uint8_t)gm_lt0iqiubfm7e(((uint8_t *)&t17), ((uint8_t *)&t18));
            t7 = t19;
        } else t7 = 0;
        t1 = t7;
    } else t1 = 1;
    F.ret = t1;
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_file_tcuv9rv(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_is_file_tcuv9rv F;
    gm_b16_8 gm_l0; /* p */
    gm_t5j14m8ru132p gm_l1; /* info */
    GM_STACK_CHECK(&F, 213u);
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
        gm_tih6h4avv5nrb t5 = t4;
        gm_t5j14m8ru132p t6;
        if (!((((uint8_t *)&t5))[0] != 0)) {
            gm_t5j14m8ru132p t7 = GM_LD(gm_t5j14m8ru132p, (((uint8_t *)&t5) + 8));
            t6 = t7;
        } else {
            uint8_t t8 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        gm_l1 = t6;
    }
    {
        uint8_t t9 = ((GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 8) + 0) == ((uint8_t)0ULL)));
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_gcc_epb17a6(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_gcc_epb17a6 F;
    gm_th2lqbo77pvsc gm_l0; /* version */
    gm_b16_8 gm_l1; /* v */
    GM_STACK_CHECK_SHALLOW(&F, 192u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_th2lqbo77pvsc t2 = GM_LD(gm_th2lqbo77pvsc, ((uint8_t *)a0));
        gm_th2lqbo77pvsc t3 = t2;
        gm_b16_8 t4;
        if (((((uint8_t *)&t3))[16] != 0)) {
            gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&t3) + 0));
            t4 = t5;
        } else {
            uint8_t t6 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        gm_l1 = t4;
    }
    {
        uint8_t t7 = 0;
        gm_b16_8 t8 = gm_l1;
        gm_b16_8 t9;
        GM_ST(void *, (uint8_t *)&t9, (void *)(gm_ssmr7jgj9e42t));
        GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(24));
        uint8_t t10 = gm_f_contains_gkki4kq(t8, t9);
        if (t10) {
            gm_b16_8 t11 = gm_l1;
            gm_b16_8 t12;
            GM_ST(void *, (uint8_t *)&t12, (void *)(gm_s2cgbvfh1b7rp));
            GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)(5));
            uint8_t t13 = gm_f_contains_gkki4kq(t11, t12);
            uint8_t t14 = (uint8_t)!t13;
            t7 = t14;
        } else t7 = 0;
        uint8_t t15 = 0;
        F.ret = t7;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
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
inline __attribute__((aligned(4))) void gm_f_list_vuor028(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_vuor028 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_4pgooq2();
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_sha256_hex_4vhcm7o(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_sha256_hex_4vhcm7o F;
    gm_b16_8 gm_l0; /* data */
    gm_t7mlrlibdq1ja gm_l1; /* d */
    GM_STACK_CHECK(&F, 169u);
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
        gm_b16_8 t5 = t4;
        gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)&t5));
        gm_b16_8 t7 = gm_f_hex_euih242((void *)NULL, cx, NULL, t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
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
inline __attribute__((aligned(4))) void gm_f_sort_by_ci5a9mg(gm_b16_8 a0, gm_closure a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sort_by_ci5a9mg F;
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
        gm_f_introsort_scs0qol(t2, t3);
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
inline bool gm_lt0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t na = GM_LD(uintptr_t, a + 8), nb = GM_LD(uintptr_t, b + 8);
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    uintptr_t m = na < nb ? na : nb;
    int c = m == 0 ? 0 : memcmp(pa, pb, m);
    return c != 0 ? c < 0 : na < nb;
}
__attribute__((aligned(4))) uint8_t gm_f_adopt_object_psc9tpi(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, gm_b16_8 a2) {
    struct gm_fr_adopt_object_psc9tpi F;
    gm_b16_8 gm_l0; /* source */
    gm_b16_8 gm_l1; /* dir */
    gm_b16_8 gm_l2; /* stem */
    gm_b16_8 gm_l3; /* ahead */
    gm_b16_8 gm_l4; /* from */
    gm_b16_8 gm_l5; /* tmp */
    GM_STACK_CHECK(&F, 857u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        gm_b16_8 t2;
        GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s7278rr8ss873));
        GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(18));
        gm_node *pn3 = gm_find(cx, 621690230);
        if (!pn3) gm_trap("no handler installed for effect `Env`", gm_src4rbevsr9gjng, 111, 14);
        gm_th2lqbo77pvsc t4;
        if (pn3->kind == GM_NATIVE) {
            const uint8_t *ev; size_t evn;
            bool evok = gm_env_var(GM_LD(void *, &t2), GM_LD(uintptr_t, (uint8_t *)&t2 + 8), &ev, &evn);
            gm_b16_8 t5;
            GM_ST(void *, (uint8_t *)&t5, (void *)(ev));
            GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(evn));
            gm_th2lqbo77pvsc t6;
            GM_ST(uint8_t, (uint8_t *)&t6 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t6 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t6 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t6), t5);
            ((uint8_t *)&t6)[16] = 1;
            gm_th2lqbo77pvsc t7;
            memset(&t7, 0, sizeof t7);
            t4 = evok ? t6 : t7;
        } else {
            ((void (*)(void *, void *, gm_node *, gm_node *, gm_b16_8))pn3->clauses[1])((void *)&t4, pn3->env, pn3->hctx, pn3, t2);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_th2lqbo77pvsc t8 = t4;
        gm_b16_8 t9;
        if (((((uint8_t *)&t8))[16] != 0)) {
            gm_b16_8 t10 = GM_LD(gm_b16_8, (((uint8_t *)&t8) + 0));
            t9 = t10;
        } else {
            uint8_t t11 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        gm_l3 = t9;
    }
    {
        uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        uint8_t t14 = ((t13 == ((uint64_t)0ULL)));
        if (!t14) goto ie12;
        {
            uint8_t t16 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix12;
ie12:;
ix12:;
    }
    {
        gm_b16_8 t17 = gm_l3;
        gm_b16_8 t18 = gm_l0;
        gm_b16_8 t19 = gm_f_sha256_hex_4vhcm7o((void *)NULL, cx, NULL, t18);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t20 = t19;
        uint64_t t21 = GM_LD(uintptr_t, ((uint8_t *)&t20) + 8);
        void * t22 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t20)));
        uint64_t t23 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t24 = (uintptr_t)((uint64_t)16ULL);
        if (t23 > t24 || t24 > (uintptr_t)t21) gm_trap_slice(t23, t24, t21, gm_src4rbevsr9gjng, 113, 49);
        gm_b16_8 t25;
        GM_ST(void *, (uint8_t *)&t25, (void *)(((uint8_t *)t22 + t23 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t25 + 8, (uintptr_t)(t24 - t23));
        gm_tnpifv20irnph t26;
        GM_ST(gm_b16_8, ((uint8_t *)&t26 + 0), t25);
        gm_tabb710d9mq2e t27;
        gm_f_format_pf7dqps((void *)&t27, (void *)NULL, cx, NULL, t26);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t28 = t27;
        if (((((uint8_t *)&t28))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t28) + 8, gm_src4rbevsr9gjng, 113, 30);
        gm_b16_8 t29 = GM_LD(gm_b16_8, (((uint8_t *)&t28) + 8));
        gm_b16_8 t30 = t29;
        gm_b16_8 t31 = GM_LD(gm_b16_8, ((uint8_t *)&t30));
        gm_b16_8 t32 = gm_f_join_qveh5b2((void *)NULL, cx, NULL, t17, t31);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l4 = t32;
    }
    {
        gm_b16_8 t34 = gm_l4;
        uint8_t t35 = gm_f_is_file_tcuv9rv((void *)NULL, cx, NULL, t34);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t36 = (uint8_t)!t35;
        if (!t36) goto ie33;
        {
            uint8_t t38 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix33;
ie33:;
ix33:;
    }
    {
        gm_b16_8 t39 = gm_l1;
        gm_b16_8 t40 = gm_l2;
        gm_b16_8 t41;
        GM_ST(void *, (uint8_t *)&t41, (void *)(gm_se0f2jka437t4));
        GM_ST(uintptr_t, (uint8_t *)&t41 + 8, (uintptr_t)(1));
        gm_b16_8 t42 = gm_f_private_name_u7i0rip((void *)NULL, cx, NULL, t39, t40, t41);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l5 = t42;
    }
    {
        gm_b16_8 t43 = gm_l4;
        gm_b16_8 t44 = gm_l5;
        gm_ttln0d0hd70en t45 = gm_f_copy_fg1ge35((void *)NULL, cx, NULL, t43, t44);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_ttln0d0hd70en t46 = t45;
        gm_b0_1 t47;
        if (!((((uint8_t *)&t46))[0] != 0)) {
            gm_b0_1 t48 = GM_LD(gm_b0_1, (((uint8_t *)&t46) + 4));
            t47 = t48;
        } else {
            {
                {
                    gm_b16_8 t51 = gm_l5;
                    gm_node *pn52 = gm_find(cx, 2126215853);
                    if (!pn52) gm_trap("no handler installed for effect `Fs`", gm_src4rbevsr9gjng, 117, 13);
                    gm_ttln0d0hd70en t53;
                    t53 = ((gm_ttln0d0hd70en (*)(void *, gm_node *, gm_node *, gm_b16_8))pn52->clauses[6])(pn52->env, pn52->hctx, pn52, t51);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
            }
            uint8_t t54 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
    }
    {
        gm_b16_8 t55 = gm_l5;
        gm_b16_8 t56 = gm_l1;
        gm_b16_8 t57 = gm_l2;
        gm_tnpifv20irnph t58;
        GM_ST(gm_b16_8, ((uint8_t *)&t58 + 0), t57);
        gm_tabb710d9mq2e t59;
        gm_f_format_pf7dqps((void *)&t59, (void *)NULL, cx, NULL, t58);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t60 = t59;
        if (((((uint8_t *)&t60))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t60) + 8, gm_src4rbevsr9gjng, 120, 49);
        gm_b16_8 t61 = GM_LD(gm_b16_8, (((uint8_t *)&t60) + 8));
        gm_b16_8 t62 = t61;
        gm_b16_8 t63 = GM_LD(gm_b16_8, ((uint8_t *)&t62));
        gm_b16_8 t64 = gm_f_join_qveh5b2((void *)NULL, cx, NULL, t56, t63);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_ttsm01t0d8srg t65 = gm_f_rename_or_remove_l80dbfs((void *)NULL, cx, NULL, t55, t64);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_ttsm01t0d8srg t66 = t65;
        uint8_t t67;
        if (((((uint8_t *)&t66))[0] != 0)) goto mn69;
        t67 = ((uint8_t)0);
        goto me68;
mn69:;
        t67 = ((uint8_t)1);
        goto me68;
mn70:;
        gm_trap("no match arm matched the scrutinee", gm_src4rbevsr9gjng, 120, 12);
me68:;
        uint8_t t71 = 0;
        F.ret = t67;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_cache_key_tf6ebrd(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_cache_key_tf6ebrd F;
    gm_b16_8 gm_l0; /* parts */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_b16_8 gm_l4; /* p */
    GM_STACK_CHECK(&F, 390u);
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
    }
    {
        {
            gm_b16_8 t4 = gm_l0;
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
                        void * t18 = (void *)((uint8_t *)&F.l1);
                        gm_tf1bjg81u0glv t19;
                        uint64_t t20 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                        GM_ST(uint64_t, (uint8_t *)&t19 + 0, t20);
                        gm_b16_8 t21 = gm_l4;
                        GM_ST(gm_b16_8, (uint8_t *)&t19 + 8, t21);
                        gm_tftb1odc739t5 t22 = gm_f_write_jltv6p4(t18, &t19);
                        gm_tftb1odc739t5 t23 = t22;
                        if (((((uint8_t *)&t23))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t23) + 1, gm_src4rbevsr9gjng, 32, 22);
                        gm_b0_1 t24 = GM_LD(gm_b0_1, (((uint8_t *)&t23) + 1));
                    }
                    goto me14;
mn15:;
                    gm_trap("no match arm matched the scrutinee", gm_src4rbevsr9gjng, 32, 5);
me14:;
                }
lc5:;
                {
                    uint64_t t26 = gm_l3;
                    uint64_t r27; bool o28 = __builtin_add_overflow(t26, ((uint64_t)1ULL), &r27);
                    uint64_t t29 = r27;
                    gm_l3 = t29;
                }
            }
lb5:;
        }
    }
    {
        gm_b16_8 t30 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
        gm_b16_8 t31 = t30;
        gm_b16_8 t32 = GM_LD(gm_b16_8, ((uint8_t *)&t31));
        gm_b16_8 t33 = gm_f_sha256_hex_4vhcm7o((void *)NULL, cx, NULL, t32);
        if (gm_unw.kind) {
            goto gm_ld1_1;
        }
        gm_b16_8 t34 = t33;
        uint64_t t35 = GM_LD(uintptr_t, ((uint8_t *)&t34) + 8);
        void * t36 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t34)));
        uint64_t t37 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t38 = (uintptr_t)((uint64_t)16ULL);
        if (t37 > t38 || t38 > (uintptr_t)t35) gm_trap_slice(t37, t38, t35, gm_src4rbevsr9gjng, 33, 12);
        gm_b16_8 t39;
        GM_ST(void *, (uint8_t *)&t39, (void *)(((uint8_t *)t36 + t37 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t39 + 8, (uintptr_t)(t38 - t37));
        uint8_t t40 = 0;
        F.ret = t39;
        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = t40;
        goto gm_ld1_1;
    }
    {
        void * t41 = (void *)((uint8_t *)&F.l1);
        gm_f_deinit_r70pji5(t41);
    }
    goto gm_ls42;
gm_ld1_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t43 = (void *)((uint8_t *)&F.l1);
        gm_f_deinit_r70pji5(t43);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls42:;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_compiler_version_hj63jgf(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_compiler_version_hj63jgf F;
    void * gm_l0; /* tc */
    GM_STACK_CHECK(&F, 188u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        void * t3 = gm_l0;
        gm_b16_8 t4 = GM_LD(gm_b16_8, (((uint8_t *)t3) + 0));
        gm_tftb1odc739t5 t5 = gm_f_extend_vogsd72(t2, t4);
        gm_tftb1odc739t5 t6 = t5;
        if (((((uint8_t *)&t6))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t6) + 1, gm_src4rbevsr9gjng, 40, 5);
        gm_b0_1 t7 = GM_LD(gm_b0_1, (((uint8_t *)&t6) + 1));
    }
    {
        void * t8 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t9;
        GM_ST(void *, (uint8_t *)&t9, (void *)(gm_skou19srual26));
        GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(9));
        gm_tftb1odc739t5 t10 = gm_f_push_3ef746b(t8, t9);
        gm_tftb1odc739t5 t11 = t10;
        if (((((uint8_t *)&t11))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t11) + 1, gm_src4rbevsr9gjng, 41, 5);
        gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t11) + 1));
    }
    {
        gm_b16_8 t13 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
        gm_b16_8 t14 = t13;
        gm_b16_8 t15 = GM_LD(gm_b16_8, ((uint8_t *)&t14));
        gm_f_run_mj5dobu((void *)&F.l2, (void *)NULL, cx, NULL, t15);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
        uint8_t t18 = (uint8_t)!t17;
        if (!t18) goto ie16;
        {
            gm_th2lqbo77pvsc t20;
            memset(&t20, 0, sizeof t20);
            uint8_t t21 = 0;
            F.ret = t20;
            goto gm_fret;
        }
        goto ix16;
ie16:;
ix16:;
    }
    {
        gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 8));
        gm_th2lqbo77pvsc t23;
        GM_ST(uint8_t, (uint8_t *)&t23 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t23 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t23 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t23), t22);
        ((uint8_t *)&t23)[16] = 1;
        uint8_t t24 = 0;
        F.ret = t23;
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
extern __attribute__((aligned(4))) uint8_t gm_f_fn_3n129lo(void * a0, void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_1b63vnj(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_for_body_1b63vnj F;
    gm_te2n4j2vtm53d gm_l0; /* <item> */
    gm_te2n4j2vtm53d gm_l1; /* r */
    gm_tibupt4dmcqai gm_l2; /* e */
    gm_b16_8 gm_l3; /* p */
    gm_t5j14m8ru132p gm_l4; /* info */
    GM_STACK_CHECK(&F, 696u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    memcpy(&gm_l0, a0, sizeof gm_l0);
    gm_te2n4j2vtm53d t3 = GM_LD(gm_te2n4j2vtm53d, ((uint8_t *)&gm_l0));
    gm_l1 = t3;
    {
        gm_te2n4j2vtm53d t5 = gm_l1;
        gm_te2n4j2vtm53d t6 = t5;
        gm_tibupt4dmcqai t7;
        if (!((((uint8_t *)&t6))[0] != 0)) {
            gm_tibupt4dmcqai t8 = GM_LD(gm_tibupt4dmcqai, (((uint8_t *)&t6) + 8));
            t7 = t8;
        } else {
            gm_unw.kind = GM_BREAK; gm_unw.frame = inst->with_frame; gm_unw.label = inst->with_label; gm_unw.is_err = 0;
            goto gm_lad;
        }
        gm_l2 = t7;
    }
    {
        gm_b16_8 t10 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l2) + 0));
        gm_b16_8 t11;
        GM_ST(void *, (uint8_t *)&t11, (void *)(gm_s4vnlpe2ovpj8));
        GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(5));
        uint8_t t12 = gm_f_starts_with_6vvt01e(t10, t11);
        uint8_t t13 = (uint8_t)!t12;
        if (!t13) goto ie9;
        {
            gm_b0_1 t15;
            uint8_t t16 = 0;
            F.ret = t15;
            goto gm_fret;
        }
        goto ix9;
ie9:;
ix9:;
    }
    {
        gm_b16_8 t17 = GM_LD(gm_b16_8, ((uint8_t *)&((struct gm_fr_prune_units_iqs7tqd *)((&F.h)->up))->l0));
        gm_b16_8 t18 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l2) + 0));
        gm_b16_8 t19 = gm_f_join_qveh5b2((void *)NULL, cx, NULL, t17, t18);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l3 = t19;
    }
    {
        gm_b16_8 t20 = gm_l3;
        gm_node *pn21 = gm_find(cx, 2126215853);
        if (!pn21) gm_trap("no handler installed for effect `Fs`", gm_src4rbevsr9gjng, 250, 17);
        gm_tih6h4avv5nrb t22;
        ((void (*)(void *, void *, gm_node *, gm_node *, gm_b16_8))pn21->clauses[13])((void *)&t22, pn21->env, pn21->hctx, pn21, t20);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tih6h4avv5nrb t23 = t22;
        gm_t5j14m8ru132p t24;
        if (!((((uint8_t *)&t23))[0] != 0)) {
            gm_t5j14m8ru132p t25 = GM_LD(gm_t5j14m8ru132p, (((uint8_t *)&t23) + 8));
            t24 = t25;
        } else {
            gm_b0_1 t26;
            uint8_t t27 = 0;
            F.ret = t26;
            goto gm_fret;
        }
        gm_l4 = t24;
    }
    {
        gm_b16_8 t29 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l2) + 0));
        gm_b16_8 t30;
        GM_ST(void *, (uint8_t *)&t30, (void *)(gm_srd2ukct5o1qh));
        GM_ST(uintptr_t, (uint8_t *)&t30 + 8, (uintptr_t)(5));
        uint8_t t31 = gm_f_contains_gkki4kq(t29, t30);
        if (!t31) goto ie28;
        {
            int64_t t34 = GM_LD(int64_t, (((uint8_t *)&gm_l4) + 16));
            int64_t t35 = GM_LD(int64_t, ((uint8_t *)&((struct gm_fr_prune_units_iqs7tqd *)((&F.h)->up))->l2));
            uint8_t t36 = (t34 < t35);
            if (!t36) goto ie33;
            {
                {
                    gm_b16_8 t39 = gm_l3;
                    gm_node *pn40 = gm_find(cx, 2126215853);
                    if (!pn40) gm_trap("no handler installed for effect `Fs`", gm_src4rbevsr9gjng, 252, 44);
                    gm_ttln0d0hd70en t41;
                    t41 = ((gm_ttln0d0hd70en (*)(void *, gm_node *, gm_node *, gm_b16_8))pn40->clauses[6])(pn40->env, pn40->hctx, pn40, t39);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
            }
            goto ix33;
ie33:;
ix33:;
        }
        {
            gm_b0_1 t42;
            uint8_t t43 = 0;
            F.ret = t42;
            goto gm_fret;
        }
        goto ix28;
ie28:;
ix28:;
    }
    {
        gm_b16_8 t45 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l2) + 0));
        gm_b16_8 t46;
        GM_ST(void *, (uint8_t *)&t46, (void *)(gm_s8p032eorpf80));
        GM_ST(uintptr_t, (uint8_t *)&t46 + 8, (uintptr_t)(2));
        uint8_t t47 = gm_f_ends_with_biriu5l(t45, t46);
        if (!t47) goto ie44;
        {
            void * t49 = (void *)((uint8_t *)&((struct gm_fr_prune_units_iqs7tqd *)((&F.h)->up))->l3);
            gm_thgmovcqlsivv t50;
            int64_t t51 = GM_LD(int64_t, (((uint8_t *)&gm_l4) + 16));
            GM_ST(int64_t, (uint8_t *)&t50 + 0, t51);
            gm_b16_8 t52 = gm_l3;
            GM_ST(gm_b16_8, (uint8_t *)&t50 + 8, t52);
            gm_tftb1odc739t5 t53 = gm_f_push_4u3hstp(t49, &t50);
            gm_tftb1odc739t5 t54 = t53;
            if (((((uint8_t *)&t54))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t54) + 1, gm_src4rbevsr9gjng, 255, 37);
            gm_b0_1 t55 = GM_LD(gm_b0_1, (((uint8_t *)&t54) + 1));
        }
        goto ix44;
ie44:;
ix44:;
    }
    goto me1;
mn2:;
    gm_trap("no match arm matched the scrutinee", gm_src4rbevsr9gjng, 246, 5);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_6ellthc(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_for_body_6ellthc F;
    gm_te2n4j2vtm53d gm_l0; /* <item> */
    gm_te2n4j2vtm53d gm_l1; /* r */
    gm_tibupt4dmcqai gm_l2; /* e */
    gm_b16_8 gm_l3; /* p */
    gm_t5j14m8ru132p gm_l4; /* info */
    GM_STACK_CHECK(&F, 663u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    memcpy(&gm_l0, a0, sizeof gm_l0);
    gm_te2n4j2vtm53d t3 = GM_LD(gm_te2n4j2vtm53d, ((uint8_t *)&gm_l0));
    gm_l1 = t3;
    {
        gm_te2n4j2vtm53d t5 = gm_l1;
        gm_te2n4j2vtm53d t6 = t5;
        gm_tibupt4dmcqai t7;
        if (!((((uint8_t *)&t6))[0] != 0)) {
            gm_tibupt4dmcqai t8 = GM_LD(gm_tibupt4dmcqai, (((uint8_t *)&t6) + 8));
            t7 = t8;
        } else {
            gm_unw.kind = GM_BREAK; gm_unw.frame = inst->with_frame; gm_unw.label = inst->with_label; gm_unw.is_err = 0;
            goto gm_lad;
        }
        gm_l2 = t7;
    }
    {
        gm_b16_8 t10 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l2) + 0));
        gm_b16_8 t11;
        GM_ST(void *, (uint8_t *)&t11, (void *)(gm_s9ab1ohnfcjkc));
        GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(5));
        uint8_t t12 = gm_f_starts_with_6vvt01e(t10, t11);
        uint8_t t13 = (uint8_t)!t12;
        if (!t13) goto ie9;
        {
            gm_b0_1 t15;
            uint8_t t16 = 0;
            F.ret = t15;
            goto gm_fret;
        }
        goto ix9;
ie9:;
ix9:;
    }
    {
        gm_b16_8 t17 = GM_LD(gm_b16_8, ((uint8_t *)&((struct gm_fr_prune_eu93ghe *)((&F.h)->up))->l0));
        gm_b16_8 t18 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l2) + 0));
        gm_b16_8 t19 = gm_f_join_qveh5b2((void *)NULL, cx, NULL, t17, t18);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l3 = t19;
    }
    {
        gm_b16_8 t20 = gm_l3;
        gm_node *pn21 = gm_find(cx, 2126215853);
        if (!pn21) gm_trap("no handler installed for effect `Fs`", gm_src4rbevsr9gjng, 147, 17);
        gm_tih6h4avv5nrb t22;
        ((void (*)(void *, void *, gm_node *, gm_node *, gm_b16_8))pn21->clauses[13])((void *)&t22, pn21->env, pn21->hctx, pn21, t20);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tih6h4avv5nrb t23 = t22;
        gm_t5j14m8ru132p t24;
        if (!((((uint8_t *)&t23))[0] != 0)) {
            gm_t5j14m8ru132p t25 = GM_LD(gm_t5j14m8ru132p, (((uint8_t *)&t23) + 8));
            t24 = t25;
        } else {
            gm_b0_1 t26;
            uint8_t t27 = 0;
            F.ret = t26;
            goto gm_fret;
        }
        gm_l4 = t24;
    }
    {
        gm_b16_8 t29 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l2) + 0));
        gm_b16_8 t30;
        GM_ST(void *, (uint8_t *)&t30, (void *)(gm_srd2ukct5o1qh));
        GM_ST(uintptr_t, (uint8_t *)&t30 + 8, (uintptr_t)(5));
        uint8_t t31 = gm_f_contains_gkki4kq(t29, t30);
        if (!t31) goto ie28;
        {
            int64_t t34 = GM_LD(int64_t, (((uint8_t *)&gm_l4) + 16));
            int64_t t35 = GM_LD(int64_t, ((uint8_t *)&((struct gm_fr_prune_eu93ghe *)((&F.h)->up))->l2));
            uint8_t t36 = (t34 < t35);
            if (!t36) goto ie33;
            {
                {
                    gm_b16_8 t39 = gm_l3;
                    gm_node *pn40 = gm_find(cx, 2126215853);
                    if (!pn40) gm_trap("no handler installed for effect `Fs`", gm_src4rbevsr9gjng, 149, 44);
                    gm_ttln0d0hd70en t41;
                    t41 = ((gm_ttln0d0hd70en (*)(void *, gm_node *, gm_node *, gm_b16_8))pn40->clauses[6])(pn40->env, pn40->hctx, pn40, t39);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
            }
            goto ix33;
ie33:;
ix33:;
        }
        {
            gm_b0_1 t42;
            uint8_t t43 = 0;
            F.ret = t42;
            goto gm_fret;
        }
        goto ix28;
ie28:;
ix28:;
    }
    {
        void * t44 = (void *)((uint8_t *)&((struct gm_fr_prune_eu93ghe *)((&F.h)->up))->l3);
        gm_thgmovcqlsivv t45;
        int64_t t46 = GM_LD(int64_t, (((uint8_t *)&gm_l4) + 16));
        GM_ST(int64_t, (uint8_t *)&t45 + 0, t46);
        gm_b16_8 t47 = gm_l3;
        GM_ST(gm_b16_8, (uint8_t *)&t45 + 8, t47);
        gm_tftb1odc739t5 t48 = gm_f_push_4u3hstp(t44, &t45);
        gm_tftb1odc739t5 t49 = t48;
        if (((((uint8_t *)&t49))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t49) + 1, gm_src4rbevsr9gjng, 152, 9);
        gm_b0_1 t50 = GM_LD(gm_b0_1, (((uint8_t *)&t49) + 1));
    }
    goto me1;
mn2:;
    gm_trap("no match arm matched the scrutinee", gm_src4rbevsr9gjng, 143, 5);
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
extern __attribute__((aligned(4))) uint8_t gm_f_is_gcc_epb17a6(const void * a0);
__attribute__((aligned(4))) uint8_t gm_f_is_gcc_epb17a6_c(gm_th2lqbo77pvsc a0) { return gm_f_is_gcc_epb17a6(&a0); }
inline __attribute__((aligned(4))) gm_b16_8 gm_f_private_name_u7i0rip(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, gm_b16_8 a2) {
    struct gm_fr_private_name_u7i0rip F;
    gm_b16_8 gm_l0; /* dir */
    gm_b16_8 gm_l1; /* stem */
    gm_b16_8 gm_l2; /* ext */
    gm_t2doi72kmmlil gm_l3; /* r */
    GM_STACK_CHECK(&F, 393u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
    }
    {
        void * t2 = ((uint8_t *)&gm_l3);
        if ((uintptr_t)0 > (uintptr_t)8 || (uintptr_t)8 > (uintptr_t)8) gm_trap_slice((uintptr_t)0, (uintptr_t)8, 8, gm_src4rbevsr9gjng, 86, 15);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(((uint8_t *)t2 + (uintptr_t)0 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)((uintptr_t)8 - (uintptr_t)0));
        gm_node *pn4 = gm_find(cx, 1087599154);
        if (!pn4) gm_trap("no handler installed for effect `Rand`", gm_src4rbevsr9gjng, 86, 5);
        gm_b0_1 t5;
        t5 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_b16_8))pn4->clauses[0])(pn4->env, pn4->hctx, pn4, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t6 = gm_l0;
        gm_tfeab279c16rr t7;
        gm_b16_8 t8 = gm_l1;
        GM_ST(gm_b16_8, (uint8_t *)&t7 + 0, t8);
        void * t9 = ((uint8_t *)&gm_l3);
        if ((uintptr_t)0 > (uintptr_t)8 || (uintptr_t)8 > (uintptr_t)8) gm_trap_slice((uintptr_t)0, (uintptr_t)8, 8, gm_src4rbevsr9gjng, 87, 68);
        gm_b16_8 t10;
        GM_ST(void *, (uint8_t *)&t10, (void *)(((uint8_t *)t9 + (uintptr_t)0 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t10 + 8, (uintptr_t)((uintptr_t)8 - (uintptr_t)0));
        gm_b16_8 t11 = t10;
        gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
        gm_b16_8 t13 = gm_f_hex_euih242((void *)NULL, cx, NULL, t12);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t7 + 16, t13);
        gm_b16_8 t14 = gm_l2;
        GM_ST(gm_b16_8, (uint8_t *)&t7 + 32, t14);
        gm_tabb710d9mq2e t15;
        gm_f_format_3pfhto5((void *)&t15, (void *)NULL, cx, NULL, &t7);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t16 = t15;
        if (((((uint8_t *)&t16))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t16) + 8, gm_src4rbevsr9gjng, 87, 27);
        gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)&t16) + 8));
        gm_b16_8 t18 = t17;
        gm_b16_8 t19 = GM_LD(gm_b16_8, ((uint8_t *)&t18));
        gm_b16_8 t20 = gm_f_join_qveh5b2((void *)NULL, cx, NULL, t6, t19);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t21 = 0;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_prune_eu93ghe(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_prune_eu93ghe F;
    int64_t gm_l1; /* now */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* n */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* i */
    uint64_t gm_l9; /* j */
    gm_thgmovcqlsivv gm_l10; /* tmp */
    int64_t gm_l11; /* recent */
    uint64_t gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    uint64_t gm_l14; /* i */
    GM_STACK_CHECK(&F, 959u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    {
        gm_node *pn2 = gm_find(cx, 1579066750);
        if (!pn2) gm_trap("no handler installed for effect `Clock`", gm_src4rbevsr9gjng, 140, 12);
        int64_t t3;
        t3 = ((int64_t (*)(void *, gm_node *, gm_node *))pn2->clauses[1])(pn2->env, pn2->hctx, pn2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l1 = t3;
    }
    {
        int64_t t4 = gm_l1;
        int64_t r5; bool o6 = __builtin_mul_overflow(((int64_t)3600LL), ((int64_t)1000000000LL), &r5);
        if (o6) gm_trap("integer overflow in `*`", gm_src4rbevsr9gjng, 141, 20);
        int64_t t7 = r5;
        int64_t r8; bool o9 = __builtin_sub_overflow(t4, t7, &r8);
        if (o9) gm_trap("integer overflow in `-`", gm_src4rbevsr9gjng, 141, 14);
        int64_t t10 = r8;
        F.l2 = t10;
    }
    {
        gm_f_list_vuor028((void *)&F.l3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t12 = F.l0;
            gm_l4 = t12;
        }
        gm_hval t13 = ((gm_hval){(void *)&F.h, cx});
        gm_node wn14 = {.eff = 2076700068, .kind = GM_LIT, .clauses = gm_hlgmpdevos39, .env = t13.env, .hctx = t13.cx, .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = cx};
        gm_b16_8 t17 = gm_l4;
        gm_b0_1 t18 = gm_f_entries_44o5n9f_sre2b72((void *)NULL, (&wn14), NULL, t17);
        if (gm_unw.kind) {
            goto gm_ll16;
        }
        F.w0 = t18;
        goto gm_ls19;
gm_ll16:;
        if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 0) { gm_unw.kind = GM_NONE; goto we15; }
        goto gm_lad;
gm_ls19:;
we15:;
        gm_b0_1 t20 = F.w0;
        gm_b0_1 t21 = t20;
    }
    {
        uint64_t t23 = GM_LD(uintptr_t, (((uint8_t *)&F.l3) + 0) + 8);
        uint8_t t24 = (t23 <= ((uint64_t)32ULL));
        if (!t24) goto ie22;
        {
            gm_b0_1 t26;
            uint8_t t27 = 0;
            F.ret = t26;
            goto gm_fret;
        }
        goto ix22;
ie22:;
ix22:;
    }
    {
        uint64_t t28 = GM_LD(uintptr_t, (((uint8_t *)&F.l3) + 0) + 8);
        gm_l5 = t28;
    }
    {
        {
            gm_l7 = ((uint64_t)1ULL);
        }
        {
            uint64_t t30 = gm_l5;
            gm_l6 = t30;
        }
        {
            for (;;) {
                uint64_t t32 = gm_l7;
                uint64_t t33 = gm_l6;
                uint8_t t34 = (t32 < t33);
                if (!t34) goto lb31;
                {
                    uint64_t t37 = gm_l7;
                    uint64_t t38 = t37;
                    uint64_t t41 = GM_LD(uint64_t, ((uint8_t *)&t38));
                    gm_l8 = t41;
                    {
                        uint64_t t43 = gm_l8;
                        gm_l9 = t43;
                    }
                    {
                        for (;;) {
                            uint64_t t45 = gm_l9;
                            uint8_t t46 = (t45 > ((uint64_t)0ULL));
                            if (!t46) goto lb44;
                            uint8_t t47 = 0;
                            uint64_t t48 = gm_l9;
                            uint64_t r49; bool o50 = __builtin_sub_overflow(t48, ((uint64_t)1ULL), &r49);
                            if (o50) gm_trap("integer overflow in `-`", gm_src4rbevsr9gjng, 159, 38);
                            uint64_t t51 = r49;
                            uint64_t t52 = (uintptr_t)t51;
                            uint64_t t53 = GM_LD(uintptr_t, (((uint8_t *)&F.l3) + 0) + 8);
                            if (t52 >= (uintptr_t)(t53)) gm_trap_index(t52, t53, gm_src4rbevsr9gjng, 159, 26);
                            int64_t t54 = GM_LD(int64_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l3) + 0)) + t52 * 24) + 0));
                            uint64_t t55 = gm_l9;
                            uint64_t t56 = (uintptr_t)t55;
                            uint64_t t57 = GM_LD(uintptr_t, (((uint8_t *)&F.l3) + 0) + 8);
                            if (t56 >= (uintptr_t)(t57)) gm_trap_index(t56, t57, gm_src4rbevsr9gjng, 159, 56);
                            int64_t t58 = GM_LD(int64_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l3) + 0)) + t56 * 24) + 0));
                            uint8_t t59 = (t54 > t58);
                            if (!t59) {
                                uint8_t t60 = 0;
                                uint64_t t61 = gm_l9;
                                uint64_t r62; bool o63 = __builtin_sub_overflow(t61, ((uint64_t)1ULL), &r62);
                                if (o63) gm_trap("integer overflow in `-`", gm_src4rbevsr9gjng, 159, 96);
                                uint64_t t64 = r62;
                                uint64_t t65 = (uintptr_t)t64;
                                uint64_t t66 = GM_LD(uintptr_t, (((uint8_t *)&F.l3) + 0) + 8);
                                if (t65 >= (uintptr_t)(t66)) gm_trap_index(t65, t66, gm_src4rbevsr9gjng, 159, 84);
                                int64_t t67 = GM_LD(int64_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l3) + 0)) + t65 * 24) + 0));
                                uint64_t t68 = gm_l9;
                                uint64_t t69 = (uintptr_t)t68;
                                uint64_t t70 = GM_LD(uintptr_t, (((uint8_t *)&F.l3) + 0) + 8);
                                if (t69 >= (uintptr_t)(t70)) gm_trap_index(t69, t70, gm_src4rbevsr9gjng, 159, 115);
                                int64_t t71 = GM_LD(int64_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l3) + 0)) + t69 * 24) + 0));
                                uint8_t t72 = ((t67 == t71));
                                if (t72) {
                                    uint64_t t73 = gm_l9;
                                    uint64_t r74; bool o75 = __builtin_sub_overflow(t73, ((uint64_t)1ULL), &r74);
                                    if (o75) gm_trap("integer overflow in `-`", gm_src4rbevsr9gjng, 159, 155);
                                    uint64_t t76 = r74;
                                    uint64_t t77 = (uintptr_t)t76;
                                    uint64_t t78 = GM_LD(uintptr_t, (((uint8_t *)&F.l3) + 0) + 8);
                                    if (t77 >= (uintptr_t)(t78)) gm_trap_index(t77, t78, gm_src4rbevsr9gjng, 159, 143);
                                    gm_b16_8 t79 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l3) + 0)) + t77 * 24) + 8));
                                    uint64_t t80 = gm_l9;
                                    uint64_t t81 = (uintptr_t)t80;
                                    uint64_t t82 = GM_LD(uintptr_t, (((uint8_t *)&F.l3) + 0) + 8);
                                    if (t81 >= (uintptr_t)(t82)) gm_trap_index(t81, t82, gm_src4rbevsr9gjng, 159, 169);
                                    gm_b16_8 t83 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l3) + 0)) + t81 * 24) + 8));
                                    gm_b16_8 t84 = t79;
                                    gm_b16_8 t85 = t83;
                                    uint8_t t86 = (uint8_t)gm_lt0iqiubfm7e(((uint8_t *)&t85), ((uint8_t *)&t84));
                                    t60 = t86;
                                } else t60 = 0;
                                t47 = t60;
                            } else t47 = 1;
                            if (!t47) goto lb44;
                            {
                                uint64_t t89 = gm_l9;
                                uint64_t r90; bool o91 = __builtin_sub_overflow(t89, ((uint64_t)1ULL), &r90);
                                if (o91) gm_trap("integer overflow in `-`", gm_src4rbevsr9gjng, 160, 32);
                                uint64_t t92 = r90;
                                uint64_t t93 = (uintptr_t)t92;
                                uint64_t t94 = GM_LD(uintptr_t, (((uint8_t *)&F.l3) + 0) + 8);
                                if (t93 >= (uintptr_t)(t94)) gm_trap_index(t93, t94, gm_src4rbevsr9gjng, 160, 20);
                                gm_thgmovcqlsivv t95 = GM_LD(gm_thgmovcqlsivv, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l3) + 0)) + t93 * 24));
                                gm_l10 = t95;
                            }
                            {
                                uint64_t t96 = gm_l9;
                                uint64_t r97; bool o98 = __builtin_sub_overflow(t96, ((uint64_t)1ULL), &r97);
                                if (o98) gm_trap("integer overflow in `-`", gm_src4rbevsr9gjng, 161, 25);
                                uint64_t t99 = r97;
                                uint64_t t100 = (uintptr_t)t99;
                                uint64_t t101 = GM_LD(uintptr_t, (((uint8_t *)&F.l3) + 0) + 8);
                                if (t100 >= (uintptr_t)(t101)) gm_trap_index(t100, t101, gm_src4rbevsr9gjng, 161, 13);
                                uint64_t t102 = gm_l9;
                                uint64_t t103 = (uintptr_t)t102;
                                uint64_t t104 = GM_LD(uintptr_t, (((uint8_t *)&F.l3) + 0) + 8);
                                if (t103 >= (uintptr_t)(t104)) gm_trap_index(t103, t104, gm_src4rbevsr9gjng, 161, 34);
                                gm_thgmovcqlsivv t105 = GM_LD(gm_thgmovcqlsivv, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l3) + 0)) + t103 * 24));
                                GM_ST(gm_thgmovcqlsivv, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l3) + 0)) + t100 * 24), t105);
                            }
                            {
                                uint64_t t106 = gm_l9;
                                uint64_t t107 = (uintptr_t)t106;
                                uint64_t t108 = GM_LD(uintptr_t, (((uint8_t *)&F.l3) + 0) + 8);
                                if (t107 >= (uintptr_t)(t108)) gm_trap_index(t107, t108, gm_src4rbevsr9gjng, 162, 13);
                                gm_thgmovcqlsivv t109 = gm_l10;
                                GM_ST(gm_thgmovcqlsivv, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l3) + 0)) + t107 * 24), t109);
                            }
                            {
                                uint64_t t110 = gm_l9;
                                uint64_t r111; bool o112 = __builtin_sub_overflow(t110, ((uint64_t)1ULL), &r111);
                                if (o112) gm_trap("integer overflow in `-`", gm_src4rbevsr9gjng, 163, 13);
                                uint64_t t113 = r111;
                                gm_l9 = t113;
                            }
lc44:;
                        }
lb44:;
                    }
                    goto me39;
mn40:;
                    gm_trap("no match arm matched the scrutinee", gm_src4rbevsr9gjng, 157, 5);
me39:;
                }
lc31:;
                {
                    uint64_t t115 = gm_l7;
                    uint64_t r116; bool o117 = __builtin_add_overflow(t115, ((uint64_t)1ULL), &r116);
                    uint64_t t118 = r116;
                    gm_l7 = t118;
                }
            }
lb31:;
        }
    }
    {
        int64_t t119 = gm_l1;
        int64_t r120; bool o121 = __builtin_mul_overflow(((int64_t)600LL), ((int64_t)1000000000LL), &r120);
        if (o121) gm_trap("integer overflow in `*`", gm_src4rbevsr9gjng, 166, 21);
        int64_t t122 = r120;
        int64_t r123; bool o124 = __builtin_sub_overflow(t119, t122, &r123);
        if (o124) gm_trap("integer overflow in `-`", gm_src4rbevsr9gjng, 166, 15);
        int64_t t125 = r123;
        gm_l11 = t125;
    }
    {
        {
            gm_l13 = ((uint64_t)0ULL);
        }
        {
            uint64_t t127 = gm_l5;
            uint64_t r128; bool o129 = __builtin_sub_overflow(t127, ((uint64_t)32ULL), &r128);
            if (o129) gm_trap("integer overflow in `-`", gm_src4rbevsr9gjng, 167, 18);
            uint64_t t130 = r128;
            gm_l12 = t130;
        }
        {
            for (;;) {
                uint64_t t132 = gm_l13;
                uint64_t t133 = gm_l12;
                uint8_t t134 = (t132 < t133);
                if (!t134) goto lb131;
                {
                    uint64_t t137 = gm_l13;
                    uint64_t t138 = t137;
                    uint64_t t141 = GM_LD(uint64_t, ((uint8_t *)&t138));
                    gm_l14 = t141;
                    {
                        uint64_t t144 = gm_l14;
                        uint64_t t145 = (uintptr_t)t144;
                        uint64_t t146 = GM_LD(uintptr_t, (((uint8_t *)&F.l3) + 0) + 8);
                        if (t145 >= (uintptr_t)(t146)) gm_trap_index(t145, t146, gm_src4rbevsr9gjng, 168, 12);
                        int64_t t147 = GM_LD(int64_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l3) + 0)) + t145 * 24) + 0));
                        int64_t t148 = gm_l11;
                        uint8_t t149 = (t147 < t148);
                        if (!t149) goto ie143;
                        {
                            {
                                uint64_t t152 = gm_l14;
                                uint64_t t153 = (uintptr_t)t152;
                                uint64_t t154 = GM_LD(uintptr_t, (((uint8_t *)&F.l3) + 0) + 8);
                                if (t153 >= (uintptr_t)(t154)) gm_trap_index(t153, t154, gm_src4rbevsr9gjng, 168, 61);
                                gm_b16_8 t155 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l3) + 0)) + t153 * 24) + 8));
                                gm_node *pn156 = gm_find(cx, 2126215853);
                                if (!pn156) gm_trap("no handler installed for effect `Fs`", gm_src4rbevsr9gjng, 168, 51);
                                gm_ttln0d0hd70en t157;
                                t157 = ((gm_ttln0d0hd70en (*)(void *, gm_node *, gm_node *, gm_b16_8))pn156->clauses[6])(pn156->env, pn156->hctx, pn156, t155);
                                if (gm_unw.kind) {
                                    goto gm_ll135;
                                }
                            }
                        }
                        goto ix143;
ie143:;
ix143:;
                    }
                    goto me139;
mn140:;
                    gm_trap("no match arm matched the scrutinee", gm_src4rbevsr9gjng, 167, 5);
me139:;
                }
lc131:;
                {
                    uint64_t t159 = gm_l13;
                    uint64_t r160; bool o161 = __builtin_add_overflow(t159, ((uint64_t)1ULL), &r160);
                    uint64_t t162 = r160;
                    gm_l13 = t162;
                }
            }
gm_ll135:;
            if (gm_unw.frame == &F.h && gm_unw.label == 3) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb131; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc131; }
            }
            goto gm_lad;
lb131:;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_prune_units_iqs7tqd(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_prune_units_iqs7tqd F;
    int64_t gm_l1; /* now */
    gm_b16_8 gm_l4; /* <tmp> */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    gm_b16_8 gm_l7; /* o */
    GM_STACK_CHECK(&F, 473u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    {
        gm_node *pn2 = gm_find(cx, 1579066750);
        if (!pn2) gm_trap("no handler installed for effect `Clock`", gm_src4rbevsr9gjng, 243, 12);
        int64_t t3;
        t3 = ((int64_t (*)(void *, gm_node *, gm_node *))pn2->clauses[1])(pn2->env, pn2->hctx, pn2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l1 = t3;
    }
    {
        int64_t t4 = gm_l1;
        int64_t r5; bool o6 = __builtin_mul_overflow(((int64_t)3600LL), ((int64_t)1000000000LL), &r5);
        if (o6) gm_trap("integer overflow in `*`", gm_src4rbevsr9gjng, 244, 20);
        int64_t t7 = r5;
        int64_t r8; bool o9 = __builtin_sub_overflow(t4, t7, &r8);
        if (o9) gm_trap("integer overflow in `-`", gm_src4rbevsr9gjng, 244, 14);
        int64_t t10 = r8;
        F.l2 = t10;
    }
    {
        gm_f_list_vuor028((void *)&F.l3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t12 = F.l0;
            gm_l4 = t12;
        }
        gm_hval t13 = ((gm_hval){(void *)&F.h, cx});
        gm_node wn14 = {.eff = 2076700068, .kind = GM_LIT, .clauses = gm_hl8alodt65ii, .env = t13.env, .hctx = t13.cx, .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = cx};
        gm_b16_8 t17 = gm_l4;
        gm_b0_1 t18 = gm_f_entries_44o5n9f_sh4kvg0((void *)NULL, (&wn14), NULL, t17);
        if (gm_unw.kind) {
            goto gm_ll16;
        }
        F.w0 = t18;
        goto gm_ls19;
gm_ll16:;
        if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 0) { gm_unw.kind = GM_NONE; goto we15; }
        goto gm_lad;
gm_ls19:;
we15:;
        gm_b0_1 t20 = F.w0;
        gm_b0_1 t21 = t20;
    }
    {
        {
            gm_b16_8 t23 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            int64_t t24 = gm_l1;
            gm_b16_8 t25 = gm_f_units_to_prune_qp6vjmt((void *)NULL, cx, NULL, t23, t24);
            if (gm_unw.kind) {
                goto gm_lad;
            }
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
                    gm_b16_8 t37 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t33 * 16));
                    gm_l7 = t37;
                    {
                        {
                            gm_b16_8 t40 = gm_l7;
                            gm_node *pn41 = gm_find(cx, 2126215853);
                            if (!pn41) gm_trap("no handler installed for effect `Fs`", gm_src4rbevsr9gjng, 258, 13);
                            gm_ttln0d0hd70en t42;
                            t42 = ((gm_ttln0d0hd70en (*)(void *, gm_node *, gm_node *, gm_b16_8))pn41->clauses[6])(pn41->env, pn41->hctx, pn41, t40);
                            if (gm_unw.kind) {
                                goto gm_ll30;
                            }
                        }
                    }
                    {
                        {
                            uint64_t t44 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                            void * t45 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)));
                            uint64_t t46 = (uintptr_t)((uint64_t)0ULL);
                            uint64_t t47 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                            uint64_t r48; bool o49 = __builtin_sub_overflow(t47, ((uint64_t)2ULL), &r48);
                            if (o49) gm_trap("integer overflow in `-`", gm_src4rbevsr9gjng, 259, 47);
                            uint64_t t50 = r48;
                            uint64_t t51 = (uintptr_t)t50;
                            if (t46 > t51 || t51 > (uintptr_t)t44) gm_trap_slice(t46, t51, t44, gm_src4rbevsr9gjng, 259, 42);
                            gm_b16_8 t52;
                            GM_ST(void *, (uint8_t *)&t52, (void *)(((uint8_t *)t45 + t46 * 1)));
                            GM_ST(uintptr_t, (uint8_t *)&t52 + 8, (uintptr_t)(t51 - t46));
                            gm_tnpifv20irnph t53;
                            GM_ST(gm_b16_8, ((uint8_t *)&t53 + 0), t52);
                            gm_tabb710d9mq2e t54;
                            gm_f_format_cncadt9((void *)&t54, (void *)NULL, cx, NULL, t53);
                            if (gm_unw.kind) {
                                goto gm_ll30;
                            }
                            gm_tabb710d9mq2e t55 = t54;
                            if (((((uint8_t *)&t55))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t55) + 8, gm_src4rbevsr9gjng, 259, 23);
                            gm_b16_8 t56 = GM_LD(gm_b16_8, (((uint8_t *)&t55) + 8));
                            gm_b16_8 t57 = t56;
                            gm_b16_8 t58 = GM_LD(gm_b16_8, ((uint8_t *)&t57));
                            gm_node *pn59 = gm_find(cx, 2126215853);
                            if (!pn59) gm_trap("no handler installed for effect `Fs`", gm_src4rbevsr9gjng, 259, 13);
                            gm_ttln0d0hd70en t60;
                            t60 = ((gm_ttln0d0hd70en (*)(void *, gm_node *, gm_node *, gm_b16_8))pn59->clauses[6])(pn59->env, pn59->hctx, pn59, t58);
                            if (gm_unw.kind) {
                                goto gm_ll30;
                            }
                        }
                    }
                    goto me35;
mn36:;
                    gm_trap("no match arm matched the scrutinee", gm_src4rbevsr9gjng, 257, 5);
me35:;
                }
lc26:;
                {
                    uint64_t t62 = gm_l6;
                    uint64_t r63; bool o64 = __builtin_add_overflow(t62, ((uint64_t)1ULL), &r63);
                    uint64_t t65 = r63;
                    gm_l6 = t65;
                }
            }
gm_ll30:;
            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb26; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc26; }
            }
            goto gm_lad;
lb26:;
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
inline __attribute__((aligned(4))) gm_ttsm01t0d8srg gm_f_rename_or_remove_l80dbfs(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_rename_or_remove_l80dbfs F;
    gm_b16_8 gm_l0; /* from */
    gm_b16_8 gm_l1; /* to */
    GM_STACK_CHECK(&F, 141u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_b16_8 t2 = gm_l0;
        gm_b16_8 t3 = gm_l1;
        gm_node *pn4 = gm_find(cx, 2126215853);
        if (!pn4) gm_trap("no handler installed for effect `Fs`", gm_src4rbevsr9gjng, 96, 5);
        gm_ttln0d0hd70en t5;
        t5 = ((gm_ttln0d0hd70en (*)(void *, gm_node *, gm_node *, gm_b16_8, gm_b16_8))pn4->clauses[7])(pn4->env, pn4->hctx, pn4, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_ttln0d0hd70en t6 = t5;
        gm_b0_1 t7;
        if (!((((uint8_t *)&t6))[0] != 0)) {
            gm_b0_1 t8 = GM_LD(gm_b0_1, (((uint8_t *)&t6) + 4));
            t7 = t8;
        } else {
            {
                {
                    gm_b16_8 t11 = gm_l0;
                    gm_node *pn12 = gm_find(cx, 2126215853);
                    if (!pn12) gm_trap("no handler installed for effect `Fs`", gm_src4rbevsr9gjng, 97, 13);
                    gm_ttln0d0hd70en t13;
                    t13 = ((gm_ttln0d0hd70en (*)(void *, gm_node *, gm_node *, gm_b16_8))pn12->clauses[6])(pn12->env, pn12->hctx, pn12, t11);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
            }
            {
                gm_b16_8 t15 = gm_l1;
                uint8_t t16 = gm_f_is_file_tcuv9rv((void *)NULL, cx, NULL, t15);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (!t16) goto ie14;
                {
                    gm_b0_1 t18;
                    gm_ttsm01t0d8srg t19;
                    GM_ST(gm_b0_1, ((uint8_t *)&t19), t18);
                    ((uint8_t *)&t19)[0] = 1;
                    uint8_t t20 = 0;
                    F.ret = t19;
                    goto gm_fret;
                }
                goto ix14;
ie14:;
ix14:;
            }
            gm_ttsm01t0d8srg t21;
            memset(&t21, 0, sizeof t21);
            uint8_t t22 = 0;
            F.ret = t21;
            goto gm_fret;
        }
    }
    {
        gm_b0_1 t23;
        gm_ttsm01t0d8srg t24;
        GM_ST(gm_b0_1, ((uint8_t *)&t24), t23);
        ((uint8_t *)&t24)[0] = 1;
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
__attribute__((aligned(4))) void gm_f_runtime_object_oihv3ss(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2, gm_b16_8 a3, const void * a4) {
    struct gm_fr_runtime_object_oihv3ss F;
    void * gm_l0; /* tc */
    gm_b16_8 gm_l1; /* flags */
    gm_b16_8 gm_l2; /* source */
    gm_b16_8 gm_l3; /* prefix */
    gm_th2lqbo77pvsc gm_l4; /* compiler */
    gm_b16_8 gm_l5; /* dir */
    gm_b16_8 gm_l6; /* version */
    gm_b16_8 gm_l8; /* key */
    gm_b16_8 gm_l9; /* stem */
    gm_b16_8 gm_l10; /* obj */
    gm_b16_8 gm_l11; /* c_file */
    gm_b16_8 gm_l12; /* tmp */
    gm_b16_8 gm_l13; /* tmp */
    GM_STACK_CHECK(&F, 2432u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        gm_th2lqbo77pvsc t2;
        gm_f_cache_dir_81h90qu((void *)&t2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_th2lqbo77pvsc t3 = t2;
        gm_b16_8 t4;
        if (((((uint8_t *)&t3))[16] != 0)) {
            gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&t3) + 0));
            t4 = t5;
        } else {
            gm_th2lqbo77pvsc t6;
            memset(&t6, 0, sizeof t6);
            uint8_t t7 = 0;
            F.ret = t6;
            goto gm_fret;
        }
        gm_l5 = t4;
    }
    {
        gm_th2lqbo77pvsc t8 = GM_LD(gm_th2lqbo77pvsc, ((uint8_t *)a4));
        gm_th2lqbo77pvsc t9 = t8;
        gm_b16_8 t10;
        if (((((uint8_t *)&t9))[16] != 0)) {
            gm_b16_8 t11 = GM_LD(gm_b16_8, (((uint8_t *)&t9) + 0));
            t10 = t11;
        } else {
            gm_th2lqbo77pvsc t12;
            memset(&t12, 0, sizeof t12);
            uint8_t t13 = 0;
            F.ret = t12;
            goto gm_fret;
        }
        gm_l6 = t10;
    }
    {
    }
    {
        {
            gm_b16_8 t15;
            GM_ST(void *, (uint8_t *)&t15, (void *)(gm_sbkvlhvo2a3ic));
            GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)(6));
            gm_b16_8 t16 = gm_l2;
            void * t17 = gm_l0;
            gm_b16_8 t18 = GM_LD(gm_b16_8, (((uint8_t *)t17) + 0));
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_s0pi0emdqd3vb));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(1));
            gm_tabb710d9mq2e t20;
            gm_f_join_sfknljb((void *)&t20, (void *)NULL, cx, NULL, t18, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t21 = t20;
            if (((((uint8_t *)&t21))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t21) + 8, gm_src4rbevsr9gjng, 54, 50);
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&t21) + 8));
            gm_b16_8 t23 = t22;
            gm_b16_8 t24 = GM_LD(gm_b16_8, ((uint8_t *)&t23));
            void * t25 = gm_l0;
            gm_b16_8 t26 = GM_LD(gm_b16_8, (((uint8_t *)t25) + 16));
            gm_b16_8 t27;
            GM_ST(void *, (uint8_t *)&t27, (void *)(gm_s0pi0emdqd3vb));
            GM_ST(uintptr_t, (uint8_t *)&t27 + 8, (uintptr_t)(1));
            gm_tabb710d9mq2e t28;
            gm_f_join_sfknljb((void *)&t28, (void *)NULL, cx, NULL, t26, t27);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t29 = t28;
            if (((((uint8_t *)&t29))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t29) + 8, gm_src4rbevsr9gjng, 54, 74);
            gm_b16_8 t30 = GM_LD(gm_b16_8, (((uint8_t *)&t29) + 8));
            gm_b16_8 t31 = t30;
            gm_b16_8 t32 = GM_LD(gm_b16_8, ((uint8_t *)&t31));
            gm_b16_8 t33 = gm_l1;
            gm_b16_8 t34;
            GM_ST(void *, (uint8_t *)&t34, (void *)(gm_s0pi0emdqd3vb));
            GM_ST(uintptr_t, (uint8_t *)&t34 + 8, (uintptr_t)(1));
            gm_tabb710d9mq2e t35;
            gm_f_join_sfknljb((void *)&t35, (void *)NULL, cx, NULL, t33, t34);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t36 = t35;
            if (((((uint8_t *)&t36))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t36) + 8, gm_src4rbevsr9gjng, 54, 102);
            gm_b16_8 t37 = GM_LD(gm_b16_8, (((uint8_t *)&t36) + 8));
            gm_b16_8 t38 = t37;
            gm_b16_8 t39 = GM_LD(gm_b16_8, ((uint8_t *)&t38));
            gm_b16_8 t40 = gm_l6;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t4d1ka1c3gdt5 *)((uint8_t *)&F.l7)) + 0, t15);
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t4d1ka1c3gdt5 *)((uint8_t *)&F.l7)) + 16, t16);
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t4d1ka1c3gdt5 *)((uint8_t *)&F.l7)) + 32, t24);
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t4d1ka1c3gdt5 *)((uint8_t *)&F.l7)) + 48, t32);
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t4d1ka1c3gdt5 *)((uint8_t *)&F.l7)) + 64, t39);
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t4d1ka1c3gdt5 *)((uint8_t *)&F.l7)) + 80, t40);
        }
        void * t41 = ((uint8_t *)&F.l7);
        if ((uintptr_t)0 > (uintptr_t)6 || (uintptr_t)6 > (uintptr_t)6) gm_trap_slice((uintptr_t)0, (uintptr_t)6, 6, gm_src4rbevsr9gjng, 54, 22);
        gm_b16_8 t42;
        GM_ST(void *, (uint8_t *)&t42, (void *)(((uint8_t *)t41 + (uintptr_t)0 * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t42 + 8, (uintptr_t)((uintptr_t)6 - (uintptr_t)0));
        gm_b16_8 t43 = t42;
        gm_b16_8 t44 = gm_f_cache_key_tf6ebrd((void *)NULL, cx, NULL, t43);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l8 = t44;
    }
    {
        gm_t6j6smj7qoctu t45;
        gm_b16_8 t46 = gm_l3;
        GM_ST(gm_b16_8, (uint8_t *)&t45 + 0, t46);
        gm_b16_8 t47 = gm_l8;
        GM_ST(gm_b16_8, (uint8_t *)&t45 + 16, t47);
        gm_tabb710d9mq2e t48;
        gm_f_format_c31q7f8((void *)&t48, (void *)NULL, cx, NULL, &t45);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t49 = t48;
        if (((((uint8_t *)&t49))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t49) + 8, gm_src4rbevsr9gjng, 55, 13);
        gm_b16_8 t50 = GM_LD(gm_b16_8, (((uint8_t *)&t49) + 8));
        gm_l9 = t50;
    }
    {
        gm_b16_8 t51 = gm_l5;
        gm_b16_8 t52 = gm_l9;
        gm_tnpifv20irnph t53;
        GM_ST(gm_b16_8, ((uint8_t *)&t53 + 0), t52);
        gm_tabb710d9mq2e t54;
        gm_f_format_soc2hp6((void *)&t54, (void *)NULL, cx, NULL, t53);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t55 = t54;
        if (((((uint8_t *)&t55))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t55) + 8, gm_src4rbevsr9gjng, 56, 27);
        gm_b16_8 t56 = GM_LD(gm_b16_8, (((uint8_t *)&t55) + 8));
        gm_b16_8 t57 = t56;
        gm_b16_8 t58 = GM_LD(gm_b16_8, ((uint8_t *)&t57));
        gm_b16_8 t59 = gm_f_join_qveh5b2((void *)NULL, cx, NULL, t51, t58);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l10 = t59;
    }
    {
        gm_b16_8 t61 = gm_l10;
        uint8_t t62 = gm_f_is_file_tcuv9rv((void *)NULL, cx, NULL, t61);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t62) goto ie60;
        {
            gm_b16_8 t64 = gm_l10;
            gm_b0_1 t65 = gm_f_touch_frggs6u((void *)NULL, cx, NULL, t64);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_b16_8 t66 = gm_l10;
            gm_th2lqbo77pvsc t67;
            GM_ST(uint8_t, (uint8_t *)&t67 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t67 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t67 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t67), t66);
            ((uint8_t *)&t67)[16] = 1;
            uint8_t t68 = 0;
            F.ret = t67;
            goto gm_fret;
        }
        goto ix60;
ie60:;
ix60:;
    }
    {
        gm_b16_8 t69 = gm_l5;
        gm_tl572d7579hns t70;
        gm_f_make_dirs_gbp8nfr((void *)&t70, (void *)NULL, cx, NULL, t69);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tl572d7579hns t71 = t70;
        gm_b0_1 t72;
        if (!((((uint8_t *)&t71))[0] != 0)) {
            gm_b0_1 t73 = GM_LD(gm_b0_1, (((uint8_t *)&t71) + 8));
            t72 = t73;
        } else {
            gm_th2lqbo77pvsc t74;
            memset(&t74, 0, sizeof t74);
            uint8_t t75 = 0;
            F.ret = t74;
            goto gm_fret;
        }
    }
    {
        gm_b16_8 t77 = gm_l2;
        gm_b16_8 t78 = gm_l5;
        gm_b16_8 t79 = gm_l9;
        gm_b16_8 t80 = t79;
        gm_b16_8 t81 = GM_LD(gm_b16_8, ((uint8_t *)&t80));
        uint8_t t82 = gm_f_adopt_object_psc9tpi((void *)NULL, cx, NULL, t77, t78, t81);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t82) goto ie76;
        {
            gm_b16_8 t84 = gm_l10;
            gm_th2lqbo77pvsc t85;
            GM_ST(uint8_t, (uint8_t *)&t85 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t85 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t85 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t85), t84);
            ((uint8_t *)&t85)[16] = 1;
            uint8_t t86 = 0;
            F.ret = t85;
            goto gm_fret;
        }
        goto ix76;
ie76:;
ix76:;
    }
    {
        gm_b16_8 t87 = gm_l5;
        gm_b16_8 t88 = gm_l9;
        gm_tnpifv20irnph t89;
        GM_ST(gm_b16_8, ((uint8_t *)&t89 + 0), t88);
        gm_tabb710d9mq2e t90;
        gm_f_format_nk7pf4i((void *)&t90, (void *)NULL, cx, NULL, t89);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t91 = t90;
        if (((((uint8_t *)&t91))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t91) + 8, gm_src4rbevsr9gjng, 66, 30);
        gm_b16_8 t92 = GM_LD(gm_b16_8, (((uint8_t *)&t91) + 8));
        gm_b16_8 t93 = t92;
        gm_b16_8 t94 = GM_LD(gm_b16_8, ((uint8_t *)&t93));
        gm_b16_8 t95 = gm_f_join_qveh5b2((void *)NULL, cx, NULL, t87, t94);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l11 = t95;
    }
    {
        gm_b16_8 t97 = gm_l11;
        uint8_t t98 = gm_f_is_file_tcuv9rv((void *)NULL, cx, NULL, t97);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t99 = (uint8_t)!t98;
        if (!t99) goto ie96;
        {
            gm_b16_8 t101 = gm_l5;
            gm_b16_8 t102 = gm_l9;
            gm_b16_8 t103 = t102;
            gm_b16_8 t104 = GM_LD(gm_b16_8, ((uint8_t *)&t103));
            gm_b16_8 t105;
            GM_ST(void *, (uint8_t *)&t105, (void *)(gm_s2gnpj7k4l22n));
            GM_ST(uintptr_t, (uint8_t *)&t105 + 8, (uintptr_t)(1));
            gm_b16_8 t106 = gm_f_private_name_u7i0rip((void *)NULL, cx, NULL, t101, t104, t105);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l12 = t106;
        }
        {
            gm_b16_8 t107 = gm_l12;
            gm_b16_8 t108 = gm_l2;
            gm_ttln0d0hd70en t109 = gm_f_write_all_o8ucaji((void *)NULL, cx, NULL, t107, t108);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_ttln0d0hd70en t110 = t109;
            gm_b0_1 t111;
            if (!((((uint8_t *)&t110))[0] != 0)) {
                gm_b0_1 t112 = GM_LD(gm_b0_1, (((uint8_t *)&t110) + 4));
                t111 = t112;
            } else {
                gm_th2lqbo77pvsc t113;
                memset(&t113, 0, sizeof t113);
                uint8_t t114 = 0;
                F.ret = t113;
                goto gm_fret;
            }
        }
        {
            gm_b16_8 t115 = gm_l12;
            gm_b16_8 t116 = gm_l11;
            gm_ttsm01t0d8srg t117 = gm_f_rename_or_remove_l80dbfs((void *)NULL, cx, NULL, t115, t116);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_ttsm01t0d8srg t118 = t117;
            gm_b0_1 t119;
            if (((((uint8_t *)&t118))[0] != 0)) {
                gm_b0_1 t120 = GM_LD(gm_b0_1, (((uint8_t *)&t118) + 0));
                t119 = t120;
            } else {
                gm_th2lqbo77pvsc t121;
                memset(&t121, 0, sizeof t121);
                uint8_t t122 = 0;
                F.ret = t121;
                goto gm_fret;
            }
        }
        goto ix96;
ie96:;
ix96:;
    }
    {
        gm_b16_8 t123 = gm_l5;
        gm_b16_8 t124 = gm_l9;
        gm_b16_8 t125 = t124;
        gm_b16_8 t126 = GM_LD(gm_b16_8, ((uint8_t *)&t125));
        gm_b16_8 t127;
        GM_ST(void *, (uint8_t *)&t127, (void *)(gm_se0f2jka437t4));
        GM_ST(uintptr_t, (uint8_t *)&t127 + 8, (uintptr_t)(1));
        gm_b16_8 t128 = gm_f_private_name_u7i0rip((void *)NULL, cx, NULL, t123, t126, t127);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l13 = t128;
    }
    {
        gm_b16_8 t129;
        GM_ST(void *, (uint8_t *)&t129, (void *)(gm_s4vd6e3mkiiso));
        GM_ST(uintptr_t, (uint8_t *)&t129 + 8, (uintptr_t)(2));
        GM_ST(gm_b16_8, (uint8_t *)&F.l14 + 0, t129);
        gm_b16_8 t130;
        GM_ST(void *, (uint8_t *)&t130, (void *)(gm_s65qt8qo64cje));
        GM_ST(uintptr_t, (uint8_t *)&t130 + 8, (uintptr_t)(2));
        GM_ST(gm_b16_8, (uint8_t *)&F.l14 + 16, t130);
        gm_b16_8 t131 = gm_l13;
        GM_ST(gm_b16_8, (uint8_t *)&F.l14 + 32, t131);
        gm_b16_8 t132 = gm_l11;
        GM_ST(gm_b16_8, (uint8_t *)&F.l14 + 48, t132);
    }
    {
        void * t133 = gm_l0;
        gm_b16_8 t134 = gm_l1;
        void * t135 = ((uint8_t *)&F.l14);
        if ((uintptr_t)0 > (uintptr_t)4 || (uintptr_t)4 > (uintptr_t)4) gm_trap_slice((uintptr_t)0, (uintptr_t)4, 4, gm_src4rbevsr9gjng, 74, 36);
        gm_b16_8 t136;
        GM_ST(void *, (uint8_t *)&t136, (void *)(((uint8_t *)t135 + (uintptr_t)0 * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t136 + 8, (uintptr_t)((uintptr_t)4 - (uintptr_t)0));
        gm_b16_8 t137 = t136;
        gm_b16_8 t138 = GM_LD(gm_b16_8, ((uint8_t *)&t137));
        gm_f_command_b2u33fl((void *)&F.l15, (void *)NULL, cx, NULL, t133, t134, t138);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t140 = GM_LD(gm_b16_8, (((uint8_t *)&F.l15) + 0));
        gm_b16_8 t141 = t140;
        gm_b16_8 t142 = GM_LD(gm_b16_8, ((uint8_t *)&t141));
        gm_tipro5npl6rd8 t143;
        gm_f_run_mj5dobu((void *)&t143, (void *)NULL, cx, NULL, t142);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tipro5npl6rd8 t144 = t143;
        uint8_t t145 = GM_LD(uint8_t, (((uint8_t *)&t144) + 0));
        uint8_t t146 = (uint8_t)!t145;
        if (!t146) goto ie139;
        {
            {
                gm_b16_8 t149 = gm_l13;
                gm_node *pn150 = gm_find(cx, 2126215853);
                if (!pn150) gm_trap("no handler installed for effect `Fs`", gm_src4rbevsr9gjng, 76, 13);
                gm_ttln0d0hd70en t151;
                t151 = ((gm_ttln0d0hd70en (*)(void *, gm_node *, gm_node *, gm_b16_8))pn150->clauses[6])(pn150->env, pn150->hctx, pn150, t149);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
        }
        {
            gm_th2lqbo77pvsc t152;
            memset(&t152, 0, sizeof t152);
            uint8_t t153 = 0;
            F.ret = t152;
            goto gm_fret;
        }
        goto ix139;
ie139:;
ix139:;
    }
    {
        gm_b16_8 t154 = gm_l13;
        gm_b16_8 t155 = gm_l10;
        gm_ttsm01t0d8srg t156 = gm_f_rename_or_remove_l80dbfs((void *)NULL, cx, NULL, t154, t155);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_ttsm01t0d8srg t157 = t156;
        gm_b0_1 t158;
        if (((((uint8_t *)&t157))[0] != 0)) {
            gm_b0_1 t159 = GM_LD(gm_b0_1, (((uint8_t *)&t157) + 0));
            t158 = t159;
        } else {
            gm_th2lqbo77pvsc t160;
            memset(&t160, 0, sizeof t160);
            uint8_t t161 = 0;
            F.ret = t160;
            goto gm_fret;
        }
    }
    {
        gm_b16_8 t162 = gm_l10;
        gm_th2lqbo77pvsc t163;
        GM_ST(uint8_t, (uint8_t *)&t163 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t163 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t163 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t163), t162);
        ((uint8_t *)&t163)[16] = 1;
        uint8_t t164 = 0;
        F.ret = t163;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_touch_frggs6u(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_touch_frggs6u F;
    gm_b16_8 gm_l0; /* p */
    GM_STACK_CHECK(&F, 160u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t2;
        GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sa23fj423hlk4));
        GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(5));
        GM_ST(gm_b16_8, (uint8_t *)&F.l1 + 0, t2);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s4vd6e3mkiiso));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(2));
        GM_ST(gm_b16_8, (uint8_t *)&F.l1 + 16, t3);
        gm_b16_8 t4 = gm_l0;
        GM_ST(gm_b16_8, (uint8_t *)&F.l1 + 32, t4);
    }
    {
        {
            void * t6 = ((uint8_t *)&F.l1);
            if ((uintptr_t)0 > (uintptr_t)3 || (uintptr_t)3 > (uintptr_t)3) gm_trap_slice((uintptr_t)0, (uintptr_t)3, 3, gm_src4rbevsr9gjng, 127, 17);
            gm_b16_8 t7;
            GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t6 + (uintptr_t)0 * 16)));
            GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)((uintptr_t)3 - (uintptr_t)0));
            gm_b16_8 t8 = t7;
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
__attribute__((aligned(4))) void gm_f_unit_objects_3f31tcd(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2, const void * a3, uint64_t a4, uint8_t a5) {
    struct gm_fr_unit_objects_3f31tcd F;
    void * gm_l0; /* tc */
    gm_b16_8 gm_l1; /* flags */
    gm_b16_8 gm_l2; /* sources */
    gm_th2lqbo77pvsc gm_l3; /* compiler */
    uint64_t gm_l4; /* jobs */
    uint8_t gm_l5; /* line_paths */
    gm_b16_8 gm_l6; /* dir */
    gm_b16_8 gm_l7; /* version */
    gm_b16_8 gm_l8; /* cc */
    gm_b16_8 gm_l9; /* cflags */
    gm_b16_8 gm_l10; /* fl */
    int64_t gm_l15; /* day */
    gm_b16_8 gm_l16; /* <tmp> */
    uint64_t gm_l17; /* <tmp> */
    gm_b16_8 gm_l18; /* src */
    gm_b16_8 gm_l20; /* key */
    gm_b16_8 gm_l21; /* stem */
    gm_b16_8 gm_l22; /* obj */
    gm_t5j14m8ru132p gm_l23; /* info */
    gm_b16_8 gm_l24; /* c_file */
    gm_b16_8 gm_l25; /* text */
    gm_b16_8 gm_l26; /* tmp_c */
    gm_b16_8 gm_l27; /* <tmp> */
    uint64_t gm_l28; /* <tmp> */
    gm_b16_8 gm_l29; /* o */
    gm_b16_8 gm_l30; /* <tmp> */
    uint64_t gm_l31; /* <tmp> */
    uint64_t gm_l32; /* i */
    gm_b16_8 gm_l33; /* tmp */
    GM_STACK_CHECK(&F, 3692u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l4 = a4;
    gm_l5 = a5;
    {
        gm_th2lqbo77pvsc t2;
        gm_f_cache_dir_81h90qu((void *)&t2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_th2lqbo77pvsc t3 = t2;
        gm_b16_8 t4;
        if (((((uint8_t *)&t3))[16] != 0)) {
            gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&t3) + 0));
            t4 = t5;
        } else {
            gm_tl572d7579hns t6;
            GM_ST(uint8_t, (uint8_t *)&t6 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t6 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t6 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t6 + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&t6 + 16, 0);
            GM_ST(uint8_t, ((uint8_t *)&t6 + 0), ((uint8_t)1ULL));
            uint8_t t7 = 0;
            F.ret = t6;
            goto gm_fret;
        }
        gm_l6 = t4;
    }
    {
        gm_th2lqbo77pvsc t8 = GM_LD(gm_th2lqbo77pvsc, ((uint8_t *)a3));
        gm_th2lqbo77pvsc t9 = t8;
        gm_b16_8 t10;
        if (((((uint8_t *)&t9))[16] != 0)) {
            gm_b16_8 t11 = GM_LD(gm_b16_8, (((uint8_t *)&t9) + 0));
            t10 = t11;
        } else {
            gm_tl572d7579hns t12;
            GM_ST(uint8_t, (uint8_t *)&t12 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t12 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t12 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t12 + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&t12 + 16, 0);
            GM_ST(uint8_t, ((uint8_t *)&t12 + 0), ((uint8_t)1ULL));
            uint8_t t13 = 0;
            F.ret = t12;
            goto gm_fret;
        }
        gm_l7 = t10;
    }
    {
        gm_b16_8 t14 = gm_l6;
        gm_tl572d7579hns t15;
        gm_f_make_dirs_gbp8nfr((void *)&t15, (void *)NULL, cx, NULL, t14);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tl572d7579hns t16 = t15;
        gm_b0_1 t17;
        if (!((((uint8_t *)&t16))[0] != 0)) {
            gm_b0_1 t18 = GM_LD(gm_b0_1, (((uint8_t *)&t16) + 8));
            t17 = t18;
        } else {
            gm_tl572d7579hns t19;
            GM_ST(uint8_t, (uint8_t *)&t19 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t19 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t19 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t19 + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&t19 + 16, 0);
            GM_ST(uint8_t, ((uint8_t *)&t19 + 0), ((uint8_t)1ULL));
            uint8_t t20 = 0;
            F.ret = t19;
            goto gm_fret;
        }
    }
    {
        void * t21 = gm_l0;
        gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)t21) + 0));
        gm_b16_8 t23;
        GM_ST(void *, (uint8_t *)&t23, (void *)(gm_s0pi0emdqd3vb));
        GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(1));
        gm_tabb710d9mq2e t24;
        gm_f_join_sfknljb((void *)&t24, (void *)NULL, cx, NULL, t22, t23);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t25 = t24;
        if (((((uint8_t *)&t25))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t25) + 8, gm_src4rbevsr9gjng, 188, 11);
        gm_b16_8 t26 = GM_LD(gm_b16_8, (((uint8_t *)&t25) + 8));
        gm_l8 = t26;
    }
    {
        void * t27 = gm_l0;
        gm_b16_8 t28 = GM_LD(gm_b16_8, (((uint8_t *)t27) + 16));
        gm_b16_8 t29;
        GM_ST(void *, (uint8_t *)&t29, (void *)(gm_s0pi0emdqd3vb));
        GM_ST(uintptr_t, (uint8_t *)&t29 + 8, (uintptr_t)(1));
        gm_tabb710d9mq2e t30;
        gm_f_join_sfknljb((void *)&t30, (void *)NULL, cx, NULL, t28, t29);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t31 = t30;
        if (((((uint8_t *)&t31))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t31) + 8, gm_src4rbevsr9gjng, 189, 15);
        gm_b16_8 t32 = GM_LD(gm_b16_8, (((uint8_t *)&t31) + 8));
        gm_l9 = t32;
    }
    {
        gm_b16_8 t33 = gm_l1;
        gm_b16_8 t34;
        GM_ST(void *, (uint8_t *)&t34, (void *)(gm_s0pi0emdqd3vb));
        GM_ST(uintptr_t, (uint8_t *)&t34 + 8, (uintptr_t)(1));
        gm_tabb710d9mq2e t35;
        gm_f_join_sfknljb((void *)&t35, (void *)NULL, cx, NULL, t33, t34);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t36 = t35;
        if (((((uint8_t *)&t36))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t36) + 8, gm_src4rbevsr9gjng, 190, 11);
        gm_b16_8 t37 = GM_LD(gm_b16_8, (((uint8_t *)&t36) + 8));
        gm_l10 = t37;
    }
    {
        gm_f_list_epvducg((void *)&F.l11, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_list_epvducg((void *)&F.l12, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_list_epvducg((void *)&F.l13, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_list_epvducg((void *)&F.l14, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_node *pn38 = gm_find(cx, 1579066750);
        if (!pn38) gm_trap("no handler installed for effect `Clock`", gm_src4rbevsr9gjng, 195, 12);
        int64_t t39;
        t39 = ((int64_t (*)(void *, gm_node *, gm_node *))pn38->clauses[1])(pn38->env, pn38->hctx, pn38);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        int64_t r40; bool o41 = __builtin_mul_overflow(((int64_t)86400LL), ((int64_t)1000000000LL), &r40);
        if (o41) gm_trap("integer overflow in `*`", gm_src4rbevsr9gjng, 195, 27);
        int64_t t42 = r40;
        int64_t r43; bool o44 = __builtin_sub_overflow(t39, t42, &r43);
        if (o44) gm_trap("integer overflow in `-`", gm_src4rbevsr9gjng, 195, 12);
        int64_t t45 = r43;
        gm_l15 = t45;
    }
    {
        {
            gm_b16_8 t47 = gm_l2;
            gm_l16 = t47;
        }
        {
            gm_l17 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t49 = gm_l17;
                uint64_t t50 = GM_LD(uintptr_t, ((uint8_t *)&gm_l16) + 8);
                uint8_t t51 = (t49 < t50);
                if (!t51) goto lb48;
                {
                    uint64_t t54 = gm_l17;
                    uint64_t t55 = (uintptr_t)t54;
                    uint64_t t56 = GM_LD(uintptr_t, ((uint8_t *)&gm_l16) + 8);
                    gm_b16_8 t59 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l16)) + t55 * 16));
                    gm_l18 = t59;
                    {
                    }
                    {
                        {
                            gm_b16_8 t62;
                            GM_ST(void *, (uint8_t *)&t62, (void *)(gm_sfbtgpom3l6k2));
                            GM_ST(uintptr_t, (uint8_t *)&t62 + 8, (uintptr_t)(4));
                            gm_b16_8 t63 = gm_l18;
                            gm_b16_8 t64 = gm_l8;
                            gm_b16_8 t65 = t64;
                            gm_b16_8 t66 = GM_LD(gm_b16_8, ((uint8_t *)&t65));
                            gm_b16_8 t67 = gm_l9;
                            gm_b16_8 t68 = t67;
                            gm_b16_8 t69 = GM_LD(gm_b16_8, ((uint8_t *)&t68));
                            gm_b16_8 t70 = gm_l10;
                            gm_b16_8 t71 = t70;
                            gm_b16_8 t72 = GM_LD(gm_b16_8, ((uint8_t *)&t71));
                            gm_b16_8 t73 = gm_l7;
                            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t4d1ka1c3gdt5 *)((uint8_t *)&F.l19)) + 0, t62);
                            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t4d1ka1c3gdt5 *)((uint8_t *)&F.l19)) + 16, t63);
                            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t4d1ka1c3gdt5 *)((uint8_t *)&F.l19)) + 32, t66);
                            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t4d1ka1c3gdt5 *)((uint8_t *)&F.l19)) + 48, t69);
                            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t4d1ka1c3gdt5 *)((uint8_t *)&F.l19)) + 64, t72);
                            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t4d1ka1c3gdt5 *)((uint8_t *)&F.l19)) + 80, t73);
                        }
                        void * t74 = ((uint8_t *)&F.l19);
                        if ((uintptr_t)0 > (uintptr_t)6 || (uintptr_t)6 > (uintptr_t)6) gm_trap_slice((uintptr_t)0, (uintptr_t)6, 6, gm_src4rbevsr9gjng, 197, 26);
                        gm_b16_8 t75;
                        GM_ST(void *, (uint8_t *)&t75, (void *)(((uint8_t *)t74 + (uintptr_t)0 * 16)));
                        GM_ST(uintptr_t, (uint8_t *)&t75 + 8, (uintptr_t)((uintptr_t)6 - (uintptr_t)0));
                        gm_b16_8 t76 = t75;
                        gm_b16_8 t77 = gm_f_cache_key_tf6ebrd((void *)NULL, cx, NULL, t76);
                        if (gm_unw.kind) {
                            goto gm_ll52;
                        }
                        gm_l20 = t77;
                    }
                    {
                        gm_b16_8 t78 = gm_l6;
                        gm_b16_8 t79 = gm_l20;
                        gm_tnpifv20irnph t80;
                        GM_ST(gm_b16_8, ((uint8_t *)&t80 + 0), t79);
                        gm_tabb710d9mq2e t81;
                        gm_f_format_n1il1vf((void *)&t81, (void *)NULL, cx, NULL, t80);
                        if (gm_unw.kind) {
                            goto gm_ll52;
                        }
                        gm_tabb710d9mq2e t82 = t81;
                        if (((((uint8_t *)&t82))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t82) + 8, gm_src4rbevsr9gjng, 198, 32);
                        gm_b16_8 t83 = GM_LD(gm_b16_8, (((uint8_t *)&t82) + 8));
                        gm_b16_8 t84 = t83;
                        gm_b16_8 t85 = GM_LD(gm_b16_8, ((uint8_t *)&t84));
                        gm_b16_8 t86 = gm_f_join_qveh5b2((void *)NULL, cx, NULL, t78, t85);
                        if (gm_unw.kind) {
                            goto gm_ll52;
                        }
                        gm_l21 = t86;
                    }
                    {
                        gm_b16_8 t87 = gm_l21;
                        gm_tnpifv20irnph t88;
                        GM_ST(gm_b16_8, ((uint8_t *)&t88 + 0), t87);
                        gm_tabb710d9mq2e t89;
                        gm_f_format_pf7dqps((void *)&t89, (void *)NULL, cx, NULL, t88);
                        if (gm_unw.kind) {
                            goto gm_ll52;
                        }
                        gm_tabb710d9mq2e t90 = t89;
                        if (((((uint8_t *)&t90))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t90) + 8, gm_src4rbevsr9gjng, 199, 16);
                        gm_b16_8 t91 = GM_LD(gm_b16_8, (((uint8_t *)&t90) + 8));
                        gm_l22 = t91;
                    }
                    {
                        void * t92 = (void *)((uint8_t *)&F.l11);
                        gm_b16_8 t93 = gm_l22;
                        gm_b16_8 t94 = t93;
                        gm_b16_8 t95 = GM_LD(gm_b16_8, ((uint8_t *)&t94));
                        gm_tftb1odc739t5 t96 = gm_f_push_3ef746b(t92, t95);
                        gm_tftb1odc739t5 t97 = t96;
                        if (((((uint8_t *)&t97))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t97) + 1, gm_src4rbevsr9gjng, 200, 9);
                        gm_b0_1 t98 = GM_LD(gm_b0_1, (((uint8_t *)&t97) + 1));
                    }
                    {
                        gm_b16_8 t100 = gm_l22;
                        gm_b16_8 t101 = t100;
                        gm_b16_8 t102 = GM_LD(gm_b16_8, ((uint8_t *)&t101));
                        gm_node *pn103 = gm_find(cx, 2126215853);
                        if (!pn103) gm_trap("no handler installed for effect `Fs`", gm_src4rbevsr9gjng, 201, 20);
                        gm_tih6h4avv5nrb t104;
                        ((void (*)(void *, void *, gm_node *, gm_node *, gm_b16_8))pn103->clauses[5])((void *)&t104, pn103->env, pn103->hctx, pn103, t102);
                        if (gm_unw.kind) {
                            goto gm_ll52;
                        }
                        gm_tih6h4avv5nrb t105 = t104;
                        if (((((uint8_t *)&t105))[0] != 0)) goto ie99;
                        gm_t5j14m8ru132p t106 = GM_LD(gm_t5j14m8ru132p, (((uint8_t *)&t105) + 8));
                        gm_l23 = t106;
                        {
                            int64_t t109 = GM_LD(int64_t, (((uint8_t *)&gm_l23) + 16));
                            int64_t t110 = gm_l15;
                            uint8_t t111 = (t109 < t110);
                            if (!t111) goto ie108;
                            {
                                gm_b16_8 t113 = gm_l22;
                                gm_b16_8 t114 = t113;
                                gm_b16_8 t115 = GM_LD(gm_b16_8, ((uint8_t *)&t114));
                                gm_b0_1 t116 = gm_f_touch_frggs6u((void *)NULL, cx, NULL, t115);
                                if (gm_unw.kind) {
                                    goto gm_ll52;
                                }
                            }
                            goto ix108;
ie108:;
ix108:;
                        }
                        {
                            goto lc48;
                        }
                        goto ix99;
ie99:;
ix99:;
                    }
                    {
                        uint8_t t118 = gm_l5;
                        uint8_t t119 = (uint8_t)!t118;
                        if (!t119) goto ie117;
                        gm_b16_8 t120 = gm_l18;
                        gm_b16_8 t121 = gm_l6;
                        gm_b16_8 t122 = gm_l20;
                        gm_tnpifv20irnph t123;
                        GM_ST(gm_b16_8, ((uint8_t *)&t123 + 0), t122);
                        gm_tabb710d9mq2e t124;
                        gm_f_format_n1il1vf((void *)&t124, (void *)NULL, cx, NULL, t123);
                        if (gm_unw.kind) {
                            goto gm_ll52;
                        }
                        gm_tabb710d9mq2e t125 = t124;
                        if (((((uint8_t *)&t125))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t125) + 8, gm_src4rbevsr9gjng, 207, 54);
                        gm_b16_8 t126 = GM_LD(gm_b16_8, (((uint8_t *)&t125) + 8));
                        gm_b16_8 t127 = t126;
                        gm_b16_8 t128 = GM_LD(gm_b16_8, ((uint8_t *)&t127));
                        uint8_t t129 = gm_f_adopt_object_psc9tpi((void *)NULL, cx, NULL, t120, t121, t128);
                        if (gm_unw.kind) {
                            goto gm_ll52;
                        }
                        if (!t129) goto ie117;
                        {
                            goto lc48;
                        }
                        goto ix117;
ie117:;
ix117:;
                    }
                    {
                        gm_b16_8 t131 = gm_l21;
                        gm_tnpifv20irnph t132;
                        GM_ST(gm_b16_8, ((uint8_t *)&t132 + 0), t131);
                        gm_tabb710d9mq2e t133;
                        gm_f_format_cncadt9((void *)&t133, (void *)NULL, cx, NULL, t132);
                        if (gm_unw.kind) {
                            goto gm_ll52;
                        }
                        gm_tabb710d9mq2e t134 = t133;
                        if (((((uint8_t *)&t134))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t134) + 8, gm_src4rbevsr9gjng, 208, 19);
                        gm_b16_8 t135 = GM_LD(gm_b16_8, (((uint8_t *)&t134) + 8));
                        gm_l24 = t135;
                    }
                    {
                        gm_b16_8 t136;
                        uint8_t t138 = gm_l5;
                        if (!t138) goto ie137;
                        gm_b16_8 t140 = gm_l18;
                        gm_b16_8 t141 = gm_l24;
                        gm_b16_8 t142 = t141;
                        gm_b16_8 t143 = GM_LD(gm_b16_8, ((uint8_t *)&t142));
                        gm_b16_8 t144 = gm_f_resolve_lines_h4bf30f((void *)NULL, cx, NULL, t140, t143);
                        if (gm_unw.kind) {
                            goto gm_ll52;
                        }
                        gm_b16_8 t145 = t144;
                        t136 = t145;
                        goto ix137;
ie137:;
                        gm_b16_8 t147 = gm_l18;
                        gm_b16_8 t148 = t147;
                        t136 = t148;
ix137:;
                        gm_l25 = t136;
                    }
                    {
                        gm_b16_8 t149 = gm_l6;
                        gm_b16_8 t150 = gm_l20;
                        gm_tnpifv20irnph t151;
                        GM_ST(gm_b16_8, ((uint8_t *)&t151 + 0), t150);
                        gm_tabb710d9mq2e t152;
                        gm_f_format_n1il1vf((void *)&t152, (void *)NULL, cx, NULL, t151);
                        if (gm_unw.kind) {
                            goto gm_ll52;
                        }
                        gm_tabb710d9mq2e t153 = t152;
                        if (((((uint8_t *)&t153))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t153) + 8, gm_src4rbevsr9gjng, 210, 36);
                        gm_b16_8 t154 = GM_LD(gm_b16_8, (((uint8_t *)&t153) + 8));
                        gm_b16_8 t155 = t154;
                        gm_b16_8 t156 = GM_LD(gm_b16_8, ((uint8_t *)&t155));
                        gm_b16_8 t157;
                        GM_ST(void *, (uint8_t *)&t157, (void *)(gm_s2gnpj7k4l22n));
                        GM_ST(uintptr_t, (uint8_t *)&t157 + 8, (uintptr_t)(1));
                        gm_b16_8 t158 = gm_f_private_name_u7i0rip((void *)NULL, cx, NULL, t149, t156, t157);
                        if (gm_unw.kind) {
                            goto gm_ll52;
                        }
                        gm_l26 = t158;
                    }
                    {
                        gm_b16_8 t159 = gm_l26;
                        gm_b16_8 t160 = gm_l25;
                        gm_ttln0d0hd70en t161 = gm_f_write_all_o8ucaji((void *)NULL, cx, NULL, t159, t160);
                        if (gm_unw.kind) {
                            goto gm_ll52;
                        }
                        gm_ttln0d0hd70en t162 = t161;
                        gm_b0_1 t163;
                        if (!((((uint8_t *)&t162))[0] != 0)) {
                            gm_b0_1 t164 = GM_LD(gm_b0_1, (((uint8_t *)&t162) + 4));
                            t163 = t164;
                        } else {
                            gm_tl572d7579hns t165;
                            GM_ST(uint8_t, (uint8_t *)&t165 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t165 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t165 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&t165 + 8, 0);
                            GM_ST(uint64_t, (uint8_t *)&t165 + 16, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t165 + 0), ((uint8_t)1ULL));
                            uint8_t t166 = 0;
                            F.ret = t165;
                            goto gm_fret;
                        }
                    }
                    {
                        gm_b16_8 t167 = gm_l26;
                        gm_b16_8 t168 = gm_l24;
                        gm_b16_8 t169 = t168;
                        gm_b16_8 t170 = GM_LD(gm_b16_8, ((uint8_t *)&t169));
                        gm_ttsm01t0d8srg t171 = gm_f_rename_or_remove_l80dbfs((void *)NULL, cx, NULL, t167, t170);
                        if (gm_unw.kind) {
                            goto gm_ll52;
                        }
                        gm_ttsm01t0d8srg t172 = t171;
                        gm_b0_1 t173;
                        if (((((uint8_t *)&t172))[0] != 0)) {
                            gm_b0_1 t174 = GM_LD(gm_b0_1, (((uint8_t *)&t172) + 0));
                            t173 = t174;
                        } else {
                            gm_tl572d7579hns t175;
                            GM_ST(uint8_t, (uint8_t *)&t175 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t175 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t175 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&t175 + 8, 0);
                            GM_ST(uint64_t, (uint8_t *)&t175 + 16, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t175 + 0), ((uint8_t)1ULL));
                            uint8_t t176 = 0;
                            F.ret = t175;
                            goto gm_fret;
                        }
                    }
                    {
                        void * t177 = (void *)((uint8_t *)&F.l12);
                        gm_b16_8 t178 = gm_l24;
                        gm_b16_8 t179 = t178;
                        gm_b16_8 t180 = GM_LD(gm_b16_8, ((uint8_t *)&t179));
                        gm_tftb1odc739t5 t181 = gm_f_push_3ef746b(t177, t180);
                        gm_tftb1odc739t5 t182 = t181;
                        if (((((uint8_t *)&t182))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t182) + 1, gm_src4rbevsr9gjng, 213, 9);
                        gm_b0_1 t183 = GM_LD(gm_b0_1, (((uint8_t *)&t182) + 1));
                    }
                    {
                        void * t184 = (void *)((uint8_t *)&F.l13);
                        gm_b16_8 t185 = gm_l6;
                        gm_b16_8 t186 = gm_l20;
                        gm_tnpifv20irnph t187;
                        GM_ST(gm_b16_8, ((uint8_t *)&t187 + 0), t186);
                        gm_tabb710d9mq2e t188;
                        gm_f_format_n1il1vf((void *)&t188, (void *)NULL, cx, NULL, t187);
                        if (gm_unw.kind) {
                            goto gm_ll52;
                        }
                        gm_tabb710d9mq2e t189 = t188;
                        if (((((uint8_t *)&t189))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t189) + 8, gm_src4rbevsr9gjng, 214, 41);
                        gm_b16_8 t190 = GM_LD(gm_b16_8, (((uint8_t *)&t189) + 8));
                        gm_b16_8 t191 = t190;
                        gm_b16_8 t192 = GM_LD(gm_b16_8, ((uint8_t *)&t191));
                        gm_b16_8 t193;
                        GM_ST(void *, (uint8_t *)&t193, (void *)(gm_se0f2jka437t4));
                        GM_ST(uintptr_t, (uint8_t *)&t193 + 8, (uintptr_t)(1));
                        gm_b16_8 t194 = gm_f_private_name_u7i0rip((void *)NULL, cx, NULL, t185, t192, t193);
                        if (gm_unw.kind) {
                            goto gm_ll52;
                        }
                        gm_tftb1odc739t5 t195 = gm_f_push_3ef746b(t184, t194);
                        gm_tftb1odc739t5 t196 = t195;
                        if (((((uint8_t *)&t196))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t196) + 1, gm_src4rbevsr9gjng, 214, 9);
                        gm_b0_1 t197 = GM_LD(gm_b0_1, (((uint8_t *)&t196) + 1));
                    }
                    {
                        void * t198 = (void *)((uint8_t *)&F.l14);
                        gm_b16_8 t199 = gm_l22;
                        gm_b16_8 t200 = t199;
                        gm_b16_8 t201 = GM_LD(gm_b16_8, ((uint8_t *)&t200));
                        gm_tftb1odc739t5 t202 = gm_f_push_3ef746b(t198, t201);
                        gm_tftb1odc739t5 t203 = t202;
                        if (((((uint8_t *)&t203))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t203) + 1, gm_src4rbevsr9gjng, 215, 9);
                        gm_b0_1 t204 = GM_LD(gm_b0_1, (((uint8_t *)&t203) + 1));
                    }
                    goto me57;
mn58:;
                    gm_trap("no match arm matched the scrutinee", gm_src4rbevsr9gjng, 196, 5);
me57:;
                }
lc48:;
                {
                    uint64_t t206 = gm_l17;
                    uint64_t r207; bool o208 = __builtin_add_overflow(t206, ((uint64_t)1ULL), &r207);
                    uint64_t t209 = r207;
                    gm_l17 = t209;
                }
            }
gm_ll52:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb48; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc48; }
            }
            goto gm_lad;
lb48:;
        }
    }
    {
        uint64_t t211 = GM_LD(uintptr_t, (((uint8_t *)&F.l12) + 0) + 8);
        uint8_t t212 = (t211 > ((uint64_t)0ULL));
        if (!t212) goto ie210;
        {
            void * t215 = gm_l0;
            gm_b16_8 t216 = gm_l1;
            gm_b16_8 t217 = GM_LD(gm_b16_8, (((uint8_t *)&F.l12) + 0));
            gm_b16_8 t218 = t217;
            gm_b16_8 t219 = GM_LD(gm_b16_8, ((uint8_t *)&t218));
            gm_b16_8 t220 = GM_LD(gm_b16_8, (((uint8_t *)&F.l13) + 0));
            gm_b16_8 t221 = t220;
            gm_b16_8 t222 = GM_LD(gm_b16_8, ((uint8_t *)&t221));
            uint64_t t223 = gm_l4;
            uint8_t t224 = gm_f_compile_objects_mu5secm((void *)NULL, cx, NULL, t215, t216, t219, t222, t223);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t225 = (uint8_t)!t224;
            if (!t225) goto ie214;
            {
                {
                    gm_b16_8 t228 = GM_LD(gm_b16_8, (((uint8_t *)&F.l13) + 0));
                    gm_l27 = t228;
                }
                {
                    gm_l28 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t230 = gm_l28;
                        uint64_t t231 = GM_LD(uintptr_t, ((uint8_t *)&gm_l27) + 8);
                        uint8_t t232 = (t230 < t231);
                        if (!t232) goto lb229;
                        {
                            uint64_t t235 = gm_l28;
                            uint64_t t236 = (uintptr_t)t235;
                            uint64_t t237 = GM_LD(uintptr_t, ((uint8_t *)&gm_l27) + 8);
                            gm_b16_8 t240 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l27)) + t236 * 16));
                            gm_l29 = t240;
                            {
                                {
                                    gm_b16_8 t243 = gm_l29;
                                    gm_node *pn244 = gm_find(cx, 2126215853);
                                    if (!pn244) gm_trap("no handler installed for effect `Fs`", gm_src4rbevsr9gjng, 219, 43);
                                    gm_ttln0d0hd70en t245;
                                    t245 = ((gm_ttln0d0hd70en (*)(void *, gm_node *, gm_node *, gm_b16_8))pn244->clauses[6])(pn244->env, pn244->hctx, pn244, t243);
                                    if (gm_unw.kind) {
                                        goto gm_ll233;
                                    }
                                }
                            }
                            goto me238;
mn239:;
                            gm_trap("no match arm matched the scrutinee", gm_src4rbevsr9gjng, 219, 13);
me238:;
                        }
lc229:;
                        {
                            uint64_t t247 = gm_l28;
                            uint64_t r248; bool o249 = __builtin_add_overflow(t247, ((uint64_t)1ULL), &r248);
                            uint64_t t250 = r248;
                            gm_l28 = t250;
                        }
                    }
gm_ll233:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb229; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc229; }
                    }
                    goto gm_lad;
lb229:;
                }
            }
            {
                gm_tl572d7579hns t251;
                GM_ST(uint8_t, (uint8_t *)&t251 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t251 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t251 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t251 + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&t251 + 16, 0);
                GM_ST(uint8_t, ((uint8_t *)&t251 + 0), ((uint8_t)2ULL));
                uint8_t t252 = 0;
                F.ret = t251;
                goto gm_fret;
            }
            goto ix214;
ie214:;
ix214:;
        }
        {
            {
                gm_b16_8 t254 = GM_LD(gm_b16_8, (((uint8_t *)&F.l13) + 0));
                gm_b16_8 t255 = t254;
                gm_b16_8 t256 = GM_LD(gm_b16_8, ((uint8_t *)&t255));
                gm_l30 = t256;
            }
            {
                gm_l31 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t258 = gm_l31;
                    uint64_t t259 = GM_LD(uintptr_t, ((uint8_t *)&gm_l30) + 8);
                    uint8_t t260 = (t258 < t259);
                    if (!t260) goto lb257;
                    {
                        gm_tf1bjg81u0glv t263;
                        uint64_t t264 = gm_l31;
                        GM_ST(uint64_t, (uint8_t *)&t263 + 0, t264);
                        uint64_t t265 = gm_l31;
                        uint64_t t266 = (uintptr_t)t265;
                        uint64_t t267 = GM_LD(uintptr_t, ((uint8_t *)&gm_l30) + 8);
                        gm_b16_8 t268 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l30)) + t266 * 16));
                        GM_ST(gm_b16_8, (uint8_t *)&t263 + 8, t268);
                        gm_tf1bjg81u0glv t269 = t263;
                        uint64_t t272 = GM_LD(uint64_t, (((uint8_t *)&t269) + 0));
                        gm_l32 = t272;
                        gm_b16_8 t273 = GM_LD(gm_b16_8, (((uint8_t *)&t269) + 8));
                        gm_l33 = t273;
                        {
                            gm_b16_8 t275 = gm_l33;
                            uint64_t t276 = gm_l32;
                            uint64_t t277 = (uintptr_t)t276;
                            uint64_t t278 = GM_LD(uintptr_t, (((uint8_t *)&F.l14) + 0) + 8);
                            if (t277 >= (uintptr_t)(t278)) gm_trap_index(t277, t278, gm_src4rbevsr9gjng, 223, 35);
                            gm_b16_8 t279 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l14) + 0)) + t277 * 16));
                            gm_ttsm01t0d8srg t280 = gm_f_rename_or_remove_l80dbfs((void *)NULL, cx, NULL, t275, t279);
                            if (gm_unw.kind) {
                                goto gm_ll261;
                            }
                            gm_ttsm01t0d8srg t281 = t280;
                            gm_b0_1 t282;
                            if (((((uint8_t *)&t281))[0] != 0)) {
                                gm_b0_1 t283 = GM_LD(gm_b0_1, (((uint8_t *)&t281) + 0));
                                t282 = t283;
                            } else {
                                {
                                    uint64_t t285 = gm_l32;
                                    uint64_t t286 = (uintptr_t)t285;
                                    uint64_t t287 = GM_LD(uintptr_t, (((uint8_t *)&F.l14) + 0) + 8);
                                    if (t286 >= (uintptr_t)(t287)) gm_trap_index(t286, t287, gm_src4rbevsr9gjng, 224, 63);
                                    gm_b16_8 t288 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l14) + 0)) + t286 * 16));
                                    gm_b16_8 t289 = t288;
                                    if (gm_pf454qfrpt6r(cx, gm_shl253arneuel, 18, gm_src4rbevsr9gjng, 224, 17)) {
                                        goto gm_ll261;
                                    }
                                    if (gm_pfu8dq5bikbh(cx, &gm_ty0iqiubfm7e, ((uint8_t *)&t289), gm_src4rbevsr9gjng, 224, 17)) {
                                        goto gm_ll261;
                                    }
                                    if (gm_pf454qfrpt6r(cx, gm_suvsl6ie8luas, 14, gm_src4rbevsr9gjng, 224, 17)) {
                                        goto gm_ll261;
                                    }
                                }
                                gm_tl572d7579hns t290;
                                GM_ST(uint8_t, (uint8_t *)&t290 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t290 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t290 + 4, 0);
                                GM_ST(uint64_t, (uint8_t *)&t290 + 8, 0);
                                GM_ST(uint64_t, (uint8_t *)&t290 + 16, 0);
                                GM_ST(uint8_t, ((uint8_t *)&t290 + 0), ((uint8_t)2ULL));
                                uint8_t t291 = 0;
                                F.ret = t290;
                                goto gm_fret;
                            }
                        }
                        goto me270;
mn271:;
                        gm_trap("no match arm matched the scrutinee", gm_src4rbevsr9gjng, 222, 9);
me270:;
                    }
lc257:;
                    {
                        uint64_t t293 = gm_l31;
                        uint64_t r294; bool o295 = __builtin_add_overflow(t293, ((uint64_t)1ULL), &r294);
                        uint64_t t296 = r294;
                        gm_l31 = t296;
                    }
                }
gm_ll261:;
                if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb257; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc257; }
                }
                goto gm_lad;
lb257:;
            }
        }
        {
            gm_b16_8 t297 = gm_l6;
            gm_b0_1 t298 = gm_f_prune_units_iqs7tqd((void *)NULL, cx, NULL, t297);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix210;
ie210:;
ix210:;
    }
    {
        gm_b16_8 t299 = GM_LD(gm_b16_8, (((uint8_t *)&F.l11) + 0));
        gm_b16_8 t300 = t299;
        gm_b16_8 t301 = GM_LD(gm_b16_8, ((uint8_t *)&t300));
        gm_tl572d7579hns t302;
        GM_ST(uint8_t, (uint8_t *)&t302 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t302 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t302 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t302 + 0), ((uint8_t)0ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t302 + 8), t301);
        uint8_t t303 = 0;
        F.ret = t302;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_units_to_prune_qp6vjmt(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, int64_t a1) {
    struct gm_fr_units_to_prune_qp6vjmt F;
    gm_b16_8 gm_l0; /* objs */
    int64_t gm_l1; /* now */
    int64_t gm_l3; /* recent */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* i */
    GM_STACK_CHECK(&F, 425u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_f_list_epvducg((void *)&F.l2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint8_t t4 = (t3 <= ((uint64_t)4000ULL));
        if (!t4) goto ie2;
        {
            gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
            gm_b16_8 t7 = t6;
            gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&t7));
            uint8_t t9 = 0;
            F.ret = t8;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_b16_8 t10 = gm_l0;
        gm_closure t11 = ((gm_closure){GM_PTR_TAG(gm_f_fn_3n129lo), NULL, cx});
        gm_f_sort_by_ci5a9mg(t10, t11);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        int64_t t12 = gm_l1;
        int64_t r13; bool o14 = __builtin_mul_overflow(((int64_t)86400LL), ((int64_t)1000000000LL), &r13);
        if (o14) gm_trap("integer overflow in `*`", gm_src4rbevsr9gjng, 272, 21);
        int64_t t15 = r13;
        int64_t r16; bool o17 = __builtin_sub_overflow(t12, t15, &r16);
        if (o17) gm_trap("integer overflow in `-`", gm_src4rbevsr9gjng, 272, 15);
        int64_t t18 = r16;
        gm_l3 = t18;
    }
    {
        {
            gm_l5 = ((uint64_t)0ULL);
        }
        {
            uint64_t t20 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            uint64_t r21; bool o22 = __builtin_sub_overflow(t20, ((uint64_t)4000ULL), &r21);
            if (o22) gm_trap("integer overflow in `-`", gm_src4rbevsr9gjng, 273, 18);
            uint64_t t23 = r21;
            gm_l4 = t23;
        }
        {
            for (;;) {
                uint64_t t25 = gm_l5;
                uint64_t t26 = gm_l4;
                uint8_t t27 = (t25 < t26);
                if (!t27) goto lb24;
                {
                    uint64_t t30 = gm_l5;
                    uint64_t t31 = t30;
                    uint64_t t34 = GM_LD(uint64_t, ((uint8_t *)&t31));
                    gm_l6 = t34;
                    {
                        uint64_t t37 = gm_l6;
                        uint64_t t38 = (uintptr_t)t37;
                        uint64_t t39 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                        if (t38 >= (uintptr_t)(t39)) gm_trap_index(t38, t39, gm_src4rbevsr9gjng, 274, 12);
                        int64_t t40 = GM_LD(int64_t, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t38 * 24) + 0));
                        int64_t t41 = gm_l3;
                        uint8_t t42 = (t40 >= t41);
                        if (!t42) goto ie36;
                        {
                            goto lb24;
                        }
                        goto ix36;
ie36:;
ix36:;
                    }
                    {
                        void * t44 = (void *)((uint8_t *)&F.l2);
                        uint64_t t45 = gm_l6;
                        uint64_t t46 = (uintptr_t)t45;
                        uint64_t t47 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
                        if (t46 >= (uintptr_t)(t47)) gm_trap_index(t46, t47, gm_src4rbevsr9gjng, 275, 18);
                        gm_b16_8 t48 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t46 * 24) + 8));
                        gm_tftb1odc739t5 t49 = gm_f_push_3ef746b(t44, t48);
                        gm_tftb1odc739t5 t50 = t49;
                        if (((((uint8_t *)&t50))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t50) + 1, gm_src4rbevsr9gjng, 275, 9);
                        gm_b0_1 t51 = GM_LD(gm_b0_1, (((uint8_t *)&t50) + 1));
                    }
                    goto me32;
mn33:;
                    gm_trap("no match arm matched the scrutinee", gm_src4rbevsr9gjng, 273, 5);
me32:;
                }
lc24:;
                {
                    uint64_t t53 = gm_l5;
                    uint64_t r54; bool o55 = __builtin_add_overflow(t53, ((uint64_t)1ULL), &r54);
                    uint64_t t56 = r54;
                    gm_l5 = t56;
                }
            }
lb24:;
        }
    }
    {
        gm_b16_8 t57 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
        gm_b16_8 t58 = t57;
        gm_b16_8 t59 = GM_LD(gm_b16_8, ((uint8_t *)&t58));
        uint8_t t60 = 0;
        F.ret = t59;
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

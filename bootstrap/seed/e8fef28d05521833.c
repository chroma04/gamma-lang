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
typedef struct gm_csguankko4lmhd { uint32_t f0; } gm_csguankko4lmhd;
typedef struct gm_csn5gfg3c8ooaj { _Bool f0; void * f1; } gm_csn5gfg3c8ooaj;
typedef struct { int32_t f0; } gm_t19enj90vg8bd;
typedef struct { uint64_t f0; uint64_t f1; } gm_t2lqu8jsj511d;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint64_t f0; } gm_t4ce0jd3tdro4;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint32_t f0; uint8_t p1[4]; uint64_t f1; } gm_tb31k5l794t7p;
typedef struct { void * f0; uint32_t f1; uint8_t pe[4]; } gm_tbl8il92l97ut;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; } gm_tgtub0v94d5u5;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; } f1; } gm_tmn7n0n9raou5;
typedef struct { uint64_t f0; uint32_t f1; uint8_t pe[4]; } gm_tn68el8d14vku;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
typedef struct { uint8_t f0; uint8_t p1[3]; uint32_t f1; uint64_t f2; } gm_tuglhjjmj2g58;
typedef struct gm_cs7e9ppsd384bj { uint32_t f0; gm_csguankko4lmhd f1; } gm_cs7e9ppsd384bj;
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
_Static_assert(sizeof(gm_csn5gfg3c8ooaj) == 16 && _Alignof(gm_csn5gfg3c8ooaj) == 8 && offsetof(gm_csn5gfg3c8ooaj, f0) == 0 && offsetof(gm_csn5gfg3c8ooaj, f1) == 8, "layout of `Defer`");
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_t9qkh0rsooc8d f0; gm_t9qkh0rsooc8d f1; } gm_t0am46iohlpol;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; uint64_t f4; } gm_t0j6181meiu4s;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; } gm_t0uuhaljsf410;
_Static_assert(sizeof(gm_t19enj90vg8bd) == 4 && _Alignof(gm_t19enj90vg8bd) == 4 && offsetof(gm_t19enj90vg8bd, f0) == 0, "layout of `(i32,)`");
typedef struct { uint8_t f0; gm_tqs5p4n2tq19v f1; uint8_t f2; } gm_t1h8kp9hsu3ua;
_Static_assert(sizeof(gm_t2lqu8jsj511d) == 16 && _Alignof(gm_t2lqu8jsj511d) == 8 && offsetof(gm_t2lqu8jsj511d, f0) == 0 && offsetof(gm_t2lqu8jsj511d, f1) == 8, "layout of `(u64, u64)`");
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_b16_8 f0; uint64_t f1; uint64_t f2; uint64_t f3; gm_t3mf58gv9kf3n f4; } gm_t4c7ipjj9s3fp;
_Static_assert(sizeof(gm_t4ce0jd3tdro4) == 8 && _Alignof(gm_t4ce0jd3tdro4) == 8 && offsetof(gm_t4ce0jd3tdro4, f0) == 0, "layout of `Instant`");
typedef struct { gm_tbl8il92l97ut f0; uint8_t f1; uint8_t pe[7]; } gm_t5lf9i3n1r3co;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tuglhjjmj2g58 v0; uint16_t v1; } f1; } gm_t5v8n6qrn4rh4;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; void * f3; } gm_t710lsrq1m8gk;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tb31k5l794t7p) == 16 && _Alignof(gm_tb31k5l794t7p) == 8 && offsetof(gm_tb31k5l794t7p, f0) == 0 && offsetof(gm_tb31k5l794t7p, f1) == 8, "layout of `Char`");
typedef struct { uint64_t f0; uint64_t f1; gm_b16_8 f2; } gm_tbccvikg6b96e;
_Static_assert(sizeof(gm_tbl8il92l97ut) == 16 && _Alignof(gm_tbl8il92l97ut) == 8 && offsetof(gm_tbl8il92l97ut, f0) == 0 && offsetof(gm_tbl8il92l97ut, f1) == 8, "layout of `SpecCall`");
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; } gm_tcrdb1inj22os;
typedef struct { gm_t2lqu8jsj511d a[2]; } gm_te3hi449i91a8;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint64_t f2; } gm_tftksr4op114e;
typedef struct { gm_b16_8 f0; uint64_t f1; uint64_t f2; } gm_tgbgti82mf879;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
_Static_assert(sizeof(gm_tgtub0v94d5u5) == 16 && _Alignof(gm_tgtub0v94d5u5) == 8 && offsetof(gm_tgtub0v94d5u5, f0) == 0 && offsetof(gm_tgtub0v94d5u5, f1) == 8, "layout of `(is_const: bool, inner: ^Expr)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_b16_8 f2; uint64_t f3; } gm_thddbqb6snabm;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_b16_8 f2; } gm_thognv9odov03;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
typedef struct { gm_b16_8 f0; gm_tf5srh4mp4b3i f1; } gm_tj64ui6kruni6;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
typedef struct { gm_b16_8 f0; uint64_t f1; int32_t f2; uint8_t pe[4]; } gm_tlk7da9ssbu7q;
typedef struct { void * f0; gm_b16_8 f1; uint8_t f2; uint8_t pe[7]; } gm_tlljg61a2ctgq;
_Static_assert(sizeof(gm_tmn7n0n9raou5) == 8 && _Alignof(gm_tmn7n0n9raou5) == 4 && offsetof(gm_tmn7n0n9raou5, f0) == 0 && offsetof(gm_tmn7n0n9raou5, f1) == 4, "layout of `Coercion`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint64_t f2; gm_b16_8 f3; } gm_tmr5oep1578tb;
_Static_assert(sizeof(gm_tn68el8d14vku) == 16 && _Alignof(gm_tn68el8d14vku) == 8 && offsetof(gm_tn68el8d14vku, f0) == 0 && offsetof(gm_tn68el8d14vku, f1) == 8, "layout of `(i: usize, x: u32)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t9805qtonbsag v0; } f1; } gm_tnrltbc61i6v1;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t f1; uint8_t pe[3]; } gm_toceffb697p0u;
typedef struct { gm_t2lqu8jsj511d a[1]; } gm_tomkd2bemeags;
typedef struct { gm_b16_8 f0; uint32_t f1; uint8_t pe[4]; } gm_tommf4j9upomc;
typedef struct { uint64_t f0; gm_t9805qtonbsag f1; } gm_tp0pp2cmr6tvd;
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
typedef struct { uint32_t f0; gm_t88rcucg1b7uk f1; } gm_tplpbqf3duvn3;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_b16_8 f2; } gm_tq3m5qgvkpje8;
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t f2; uint8_t p3[3]; gm_tl874t2atnlh3 f3; uint8_t pe[4]; } gm_tsoie1ppgdvcg;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; gm_b16_8 f5; uint32_t f6; uint8_t p7[4]; gm_b16_8 f7; gm_b16_8 f8; uint32_t f9; uint8_t pe[4]; } gm_tt4j8kuecfafr;
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; uint64_t f3; } gm_tttl9nsdo1iu6;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; } gm_tud7oovfvnf43;
_Static_assert(sizeof(gm_tuglhjjmj2g58) == 16 && _Alignof(gm_tuglhjjmj2g58) == 8 && offsetof(gm_tuglhjjmj2g58, f0) == 0 && offsetof(gm_tuglhjjmj2g58, f1) == 4 && offsetof(gm_tuglhjjmj2g58, f2) == 8, "layout of `(signed: bool, bits: u32, storage: u64)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_tcc1rq4n1avb2 v3; uint32_t v4; uint32_t v5; } f1; } gm_tvdg94bk7aa22;
_Static_assert(sizeof(gm_cs7e9ppsd384bj) == 8 && _Alignof(gm_cs7e9ppsd384bj) == 4 && offsetof(gm_cs7e9ppsd384bj, f0) == 0 && offsetof(gm_cs7e9ppsd384bj, f1) == 4, "layout of `WithSlot`");
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t0am46iohlpol) == 2 && _Alignof(gm_t0am46iohlpol) == 1 && offsetof(gm_t0am46iohlpol, f0) == 0 && offsetof(gm_t0am46iohlpol, f1) == 1, "layout of `(Command, Command)`");
_Static_assert(sizeof(gm_t0j6181meiu4s) == 72 && _Alignof(gm_t0j6181meiu4s) == 8 && offsetof(gm_t0j6181meiu4s, f0) == 0 && offsetof(gm_t0j6181meiu4s, f1) == 16 && offsetof(gm_t0j6181meiu4s, f2) == 32 && offsetof(gm_t0j6181meiu4s, f3) == 48 && offsetof(gm_t0j6181meiu4s, f4) == 64, "layout of `(string, string, string, string, u64)`");
_Static_assert(sizeof(gm_t0uuhaljsf410) == 64 && _Alignof(gm_t0uuhaljsf410) == 8 && offsetof(gm_t0uuhaljsf410, f0) == 0 && offsetof(gm_t0uuhaljsf410, f1) == 16 && offsetof(gm_t0uuhaljsf410, f2) == 32 && offsetof(gm_t0uuhaljsf410, f3) == 48, "layout of `(string, string, string, string)`");
_Static_assert(sizeof(gm_t1h8kp9hsu3ua) == 4 && _Alignof(gm_t1h8kp9hsu3ua) == 1 && offsetof(gm_t1h8kp9hsu3ua, f0) == 0 && offsetof(gm_t1h8kp9hsu3ua, f1) == 1 && offsetof(gm_t1h8kp9hsu3ua, f2) == 3, "layout of `Hints`");
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `WasmReader`");
typedef struct { gm_tiilmfd16v3fc f0; uint32_t f1; uint8_t pe[4]; } gm_t419mo45g7j0a;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
_Static_assert(sizeof(gm_t4c7ipjj9s3fp) == 56 && _Alignof(gm_t4c7ipjj9s3fp) == 8 && offsetof(gm_t4c7ipjj9s3fp, f0) == 0 && offsetof(gm_t4c7ipjj9s3fp, f1) == 16 && offsetof(gm_t4c7ipjj9s3fp, f2) == 24 && offsetof(gm_t4c7ipjj9s3fp, f3) == 32 && offsetof(gm_t4c7ipjj9s3fp, f4) == 40, "layout of `HashMap(TyKind, u32, null, null)`");
typedef struct { gm_tcmkompbl09il f0; uint8_t f1; uint8_t pe[7]; } gm_t57euh612ligr;
typedef struct { uint64_t f0; gm_tsoie1ppgdvcg f1; } gm_t5im2v50m3hk6;
_Static_assert(sizeof(gm_t5lf9i3n1r3co) == 24 && _Alignof(gm_t5lf9i3n1r3co) == 8 && offsetof(gm_t5lf9i3n1r3co, f0) == 0 && offsetof(gm_t5lf9i3n1r3co, f1) == 16, "layout of `?SpecCall`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t08olq97rscn3 v0; gm_b16_8 v1; } f1; } gm_t5o0tpv5jka2l;
_Static_assert(sizeof(gm_t5v8n6qrn4rh4) == 24 && _Alignof(gm_t5v8n6qrn4rh4) == 8 && offsetof(gm_t5v8n6qrn4rh4, f0) == 0 && offsetof(gm_t5v8n6qrn4rh4, f1) == 8, "layout of `Repr`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t710lsrq1m8gk) == 40 && _Alignof(gm_t710lsrq1m8gk) == 8 && offsetof(gm_t710lsrq1m8gk, f0) == 0 && offsetof(gm_t710lsrq1m8gk, f1) == 8 && offsetof(gm_t710lsrq1m8gk, f2) == 24 && offsetof(gm_t710lsrq1m8gk, f3) == 32, "layout of `WithExpr`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tbccvikg6b96e) == 32 && _Alignof(gm_tbccvikg6b96e) == 8 && offsetof(gm_tbccvikg6b96e, f0) == 0 && offsetof(gm_tbccvikg6b96e, f1) == 8 && offsetof(gm_tbccvikg6b96e, f2) == 16, "layout of `Layout`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_thognv9odov03 v0; gm_tiilmfd16v3fc v1; } f1; } gm_tbf8u1b86cgmi;
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
_Static_assert(sizeof(gm_tcrdb1inj22os) == 72 && _Alignof(gm_tcrdb1inj22os) == 8 && offsetof(gm_tcrdb1inj22os, f0) == 0 && offsetof(gm_tcrdb1inj22os, f1) == 16 && offsetof(gm_tcrdb1inj22os, f2) == 24 && offsetof(gm_tcrdb1inj22os, f3) == 40 && offsetof(gm_tcrdb1inj22os, f4) == 56, "layout of `(string, usize, string, string, string)`");
typedef struct { gm_trqfvmjtt4sij f0; gm_trqfvmjtt4sij f1; } gm_td7pkamr64l0c;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
typedef struct { gm_tlljg61a2ctgq f0; uint8_t f1; uint8_t pe[7]; } gm_tdh3gh5n00hec;
_Static_assert(sizeof(gm_te3hi449i91a8) == 32 && _Alignof(gm_te3hi449i91a8) == 8 && offsetof(gm_te3hi449i91a8, a) == 0, "layout of `[2]BlobPart`");
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `Toolchain`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tftksr4op114e) == 40 && _Alignof(gm_tftksr4op114e) == 8 && offsetof(gm_tftksr4op114e, f0) == 0 && offsetof(gm_tftksr4op114e, f1) == 16 && offsetof(gm_tftksr4op114e, f2) == 32, "layout of `(string, string, usize)`");
_Static_assert(sizeof(gm_tgbgti82mf879) == 32 && _Alignof(gm_tgbgti82mf879) == 8 && offsetof(gm_tgbgti82mf879, f0) == 0 && offsetof(gm_tgbgti82mf879, f1) == 16 && offsetof(gm_tgbgti82mf879, f2) == 24, "layout of `PayloadUnion`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_thddbqb6snabm) == 48 && _Alignof(gm_thddbqb6snabm) == 8 && offsetof(gm_thddbqb6snabm, f0) == 0 && offsetof(gm_thddbqb6snabm, f1) == 16 && offsetof(gm_thddbqb6snabm, f2) == 24 && offsetof(gm_thddbqb6snabm, f3) == 40, "layout of `(string, u64, string, u64)`");
_Static_assert(sizeof(gm_thognv9odov03) == 40 && _Alignof(gm_thognv9odov03) == 8 && offsetof(gm_thognv9odov03, f0) == 0 && offsetof(gm_thognv9odov03, f1) == 8 && offsetof(gm_thognv9odov03, f2) == 24, "layout of `ForeignFn`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tj64ui6kruni6) == 32 && _Alignof(gm_tj64ui6kruni6) == 8 && offsetof(gm_tj64ui6kruni6, f0) == 0 && offsetof(gm_tj64ui6kruni6, f1) == 16, "layout of `(string, ?usize)`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tlk7da9ssbu7q) == 32 && _Alignof(gm_tlk7da9ssbu7q) == 8 && offsetof(gm_tlk7da9ssbu7q, f0) == 0 && offsetof(gm_tlk7da9ssbu7q, f1) == 16 && offsetof(gm_tlk7da9ssbu7q, f2) == 24, "layout of `(string, u64, i32)`");
_Static_assert(sizeof(gm_tlljg61a2ctgq) == 32 && _Alignof(gm_tlljg61a2ctgq) == 8 && offsetof(gm_tlljg61a2ctgq, f0) == 0 && offsetof(gm_tlljg61a2ctgq, f1) == 8 && offsetof(gm_tlljg61a2ctgq, f2) == 24, "layout of `(callee: ^Expr, args: []Arg, trailing_comma: bool)`");
_Static_assert(sizeof(gm_tmr5oep1578tb) == 56 && _Alignof(gm_tmr5oep1578tb) == 8 && offsetof(gm_tmr5oep1578tb, f0) == 0 && offsetof(gm_tmr5oep1578tb, f1) == 16 && offsetof(gm_tmr5oep1578tb, f2) == 32 && offsetof(gm_tmr5oep1578tb, f3) == 40, "layout of `(string, []u8, usize, []u8)`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tnrltbc61i6v1) == 12 && _Alignof(gm_tnrltbc61i6v1) == 4 && offsetof(gm_tnrltbc61i6v1, f0) == 0 && offsetof(gm_tnrltbc61i6v1, f1) == 4, "layout of `Observed`");
_Static_assert(sizeof(gm_toceffb697p0u) == 16 && _Alignof(gm_toceffb697p0u) == 4 && offsetof(gm_toceffb697p0u, f0) == 0 && offsetof(gm_toceffb697p0u, f1) == 12, "layout of `?Span`");
_Static_assert(sizeof(gm_tomkd2bemeags) == 16 && _Alignof(gm_tomkd2bemeags) == 8 && offsetof(gm_tomkd2bemeags, a) == 0, "layout of `[1]BlobPart`");
_Static_assert(sizeof(gm_tommf4j9upomc) == 24 && _Alignof(gm_tommf4j9upomc) == 8 && offsetof(gm_tommf4j9upomc, f0) == 0 && offsetof(gm_tommf4j9upomc, f1) == 16, "layout of `(string, u32)`");
_Static_assert(sizeof(gm_tp0pp2cmr6tvd) == 16 && _Alignof(gm_tp0pp2cmr6tvd) == 8 && offsetof(gm_tp0pp2cmr6tvd, f0) == 0 && offsetof(gm_tp0pp2cmr6tvd, f1) == 8, "layout of `(i: usize, x: ?u32)`");
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
_Static_assert(sizeof(gm_tplpbqf3duvn3) == 8 && _Alignof(gm_tplpbqf3duvn3) == 4 && offsetof(gm_tplpbqf3duvn3, f0) == 0 && offsetof(gm_tplpbqf3duvn3, f1) == 4, "layout of `WithSlot`");
_Static_assert(sizeof(gm_tq3m5qgvkpje8) == 40 && _Alignof(gm_tq3m5qgvkpje8) == 8 && offsetof(gm_tq3m5qgvkpje8, f0) == 0 && offsetof(gm_tq3m5qgvkpje8, f1) == 16 && offsetof(gm_tq3m5qgvkpje8, f2) == 24, "layout of `(string, usize, string)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t08olq97rscn3 v0; } f1; } gm_tqgtd5gtgj65s;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_ttggq7ng4kp3p v0; gm_tpe1hv3fekn05 v1; gm_t3mf58gv9kf3n v2; gm_tpe1hv3fekn05 v3; void * v4; gm_tgtub0v94d5u5 v5; } f1; } gm_trcmmd12cefb1;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
typedef struct { void * f0; gm_b16_8 f1; gm_tnrltbc61i6v1 f2; uint8_t pe[4]; } gm_ts0eitcfeteg8;
_Static_assert(sizeof(gm_tsoie1ppgdvcg) == 40 && _Alignof(gm_tsoie1ppgdvcg) == 8 && offsetof(gm_tsoie1ppgdvcg, f0) == 0 && offsetof(gm_tsoie1ppgdvcg, f1) == 16 && offsetof(gm_tsoie1ppgdvcg, f2) == 20 && offsetof(gm_tsoie1ppgdvcg, f3) == 24, "layout of `LocalDecl`");
_Static_assert(sizeof(gm_tt4j8kuecfafr) == 144 && _Alignof(gm_tt4j8kuecfafr) == 8 && offsetof(gm_tt4j8kuecfafr, f0) == 0 && offsetof(gm_tt4j8kuecfafr, f1) == 16 && offsetof(gm_tt4j8kuecfafr, f2) == 32 && offsetof(gm_tt4j8kuecfafr, f3) == 48 && offsetof(gm_tt4j8kuecfafr, f4) == 64 && offsetof(gm_tt4j8kuecfafr, f5) == 80 && offsetof(gm_tt4j8kuecfafr, f6) == 96 && offsetof(gm_tt4j8kuecfafr, f7) == 104 && offsetof(gm_tt4j8kuecfafr, f8) == 120 && offsetof(gm_tt4j8kuecfafr, f9) == 136, "layout of `(string, string, string, string, string, string, u32, string, string, u32)`");
_Static_assert(sizeof(gm_tttl9nsdo1iu6) == 56 && _Alignof(gm_tttl9nsdo1iu6) == 8 && offsetof(gm_tttl9nsdo1iu6, f0) == 0 && offsetof(gm_tttl9nsdo1iu6, f1) == 16 && offsetof(gm_tttl9nsdo1iu6, f2) == 32 && offsetof(gm_tttl9nsdo1iu6, f3) == 48, "layout of `(string, string, string, u64)`");
typedef struct { gm_t6j6smj7qoctu f0; uint8_t f1; uint8_t pe[7]; } gm_ttu09lij36f8c;
_Static_assert(sizeof(gm_tud7oovfvnf43) == 80 && _Alignof(gm_tud7oovfvnf43) == 8 && offsetof(gm_tud7oovfvnf43, f0) == 0 && offsetof(gm_tud7oovfvnf43, f1) == 16 && offsetof(gm_tud7oovfvnf43, f2) == 32 && offsetof(gm_tud7oovfvnf43, f3) == 48 && offsetof(gm_tud7oovfvnf43, f4) == 64, "layout of `(string, string, string, string, string)`");
typedef struct { gm_t4c7ipjj9s3fp f0; uint8_t f1; uint8_t pe[7]; } gm_tuutj662vpgtg;
_Static_assert(sizeof(gm_tvdg94bk7aa22) == 12 && _Alignof(gm_tvdg94bk7aa22) == 4 && offsetof(gm_tvdg94bk7aa22, f0) == 0 && offsetof(gm_tvdg94bk7aa22, f1) == 4, "layout of `Builtin`");
_Static_assert(sizeof(gm_t419mo45g7j0a) == 32 && _Alignof(gm_t419mo45g7j0a) == 8 && offsetof(gm_t419mo45g7j0a, f0) == 0 && offsetof(gm_t419mo45g7j0a, f1) == 24, "layout of `(effect_: EffectRef, op: u32)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t57euh612ligr) == 32 && _Alignof(gm_t57euh612ligr) == 8 && offsetof(gm_t57euh612ligr, f0) == 0 && offsetof(gm_t57euh612ligr, f1) == 24, "layout of `?BitsLayout`");
_Static_assert(sizeof(gm_t5im2v50m3hk6) == 48 && _Alignof(gm_t5im2v50m3hk6) == 8 && offsetof(gm_t5im2v50m3hk6, f0) == 0 && offsetof(gm_t5im2v50m3hk6, f1) == 8, "layout of `(i: usize, x: LocalDecl)`");
_Static_assert(sizeof(gm_t5o0tpv5jka2l) == 40 && _Alignof(gm_t5o0tpv5jka2l) == 8 && offsetof(gm_t5o0tpv5jka2l, f0) == 0 && offsetof(gm_t5o0tpv5jka2l, f1) == 8, "layout of `Unsup!string`");
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
_Static_assert(sizeof(gm_tbf8u1b86cgmi) == 48 && _Alignof(gm_tbf8u1b86cgmi) == 8 && offsetof(gm_tbf8u1b86cgmi, f0) == 0 && offsetof(gm_tbf8u1b86cgmi, f1) == 8, "layout of `ScopeKind`");
typedef struct { gm_b16_8 f0; gm_tkn42u7na3t81 f1; gm_tbci5ospk5nnn f2; } gm_tckdobunaulee;
_Static_assert(sizeof(gm_td7pkamr64l0c) == 80 && _Alignof(gm_td7pkamr64l0c) == 8 && offsetof(gm_td7pkamr64l0c, f0) == 0 && offsetof(gm_td7pkamr64l0c, f1) == 40, "layout of `(q: List(u32), r: List(u32))`");
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_tdh3gh5n00hec) == 40 && _Alignof(gm_tdh3gh5n00hec) == 8 && offsetof(gm_tdh3gh5n00hec, f0) == 0 && offsetof(gm_tdh3gh5n00hec, f1) == 32, "layout of `?Spine`");
typedef struct { gm_tbf8u1b86cgmi f0; uint32_t f1; uint8_t p2[4]; gm_trqfvmjtt4sij f2; uint64_t f3; } gm_tehgl6lkqgvt0;
typedef struct { gm_ts0eitcfeteg8 f0; uint8_t f1; uint8_t pe[7]; } gm_tjefu848ssvm9;
_Static_assert(sizeof(gm_tqgtd5gtgj65s) == 40 && _Alignof(gm_tqgtd5gtgj65s) == 8 && offsetof(gm_tqgtd5gtgj65s, f0) == 0 && offsetof(gm_tqgtd5gtgj65s, f1) == 8, "layout of `Unsup!()`");
_Static_assert(sizeof(gm_trcmmd12cefb1) == 32 && _Alignof(gm_trcmmd12cefb1) == 8 && offsetof(gm_trcmmd12cefb1, f0) == 0 && offsetof(gm_trcmmd12cefb1, f1) == 8, "layout of `Stmt`");
_Static_assert(sizeof(gm_ts0eitcfeteg8) == 40 && _Alignof(gm_ts0eitcfeteg8) == 8 && offsetof(gm_ts0eitcfeteg8, f0) == 0 && offsetof(gm_ts0eitcfeteg8, f1) == 8 && offsetof(gm_ts0eitcfeteg8, f2) == 24, "layout of `DestCall`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t419mo45g7j0a v1; void * v2; } f1; } gm_tsth9qu1dv0g3;
_Static_assert(sizeof(gm_ttu09lij36f8c) == 40 && _Alignof(gm_ttu09lij36f8c) == 8 && offsetof(gm_ttu09lij36f8c, f0) == 0 && offsetof(gm_ttu09lij36f8c, f1) == 32, "layout of `?Split`");
_Static_assert(sizeof(gm_tuutj662vpgtg) == 64 && _Alignof(gm_tuutj662vpgtg) == 8 && offsetof(gm_tuutj662vpgtg, f0) == 0 && offsetof(gm_tuutj662vpgtg, f1) == 56, "layout of `?HashMap((u32, u32), bool, null, null)`");
typedef struct { gm_tehgl6lkqgvt0 f0; uint8_t f1; uint8_t pe[7]; } gm_t1g28mkc78m0c;
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_tckdobunaulee) == 72 && _Alignof(gm_tckdobunaulee) == 8 && offsetof(gm_tckdobunaulee, f0) == 0 && offsetof(gm_tckdobunaulee, f1) == 16 && offsetof(gm_tckdobunaulee, f2) == 24, "layout of `VariantDef`");
typedef struct { void * f0; uint32_t f1; uint8_t p2[4]; gm_b16_8 f2; gm_t9805qtonbsag f3; gm_t5lf9i3n1r3co f4; gm_tdh3gh5n00hec f5; gm_tjefu848ssvm9 f6; gm_th2lqbo77pvsc f7; void * f8; gm_trqfvmjtt4sij f9; uint64_t f10; uint32_t f11; uint8_t p12[4]; gm_trqfvmjtt4sij f12; gm_b16_8 f13; uint8_t f14; uint8_t f15; uint8_t f16; uint8_t p17[5]; uint64_t f17; gm_tf5srh4mp4b3i f18; gm_b16_8 f19; gm_b16_8 f20; uint32_t f21; uint8_t f22; uint8_t p23[3]; gm_trqfvmjtt4sij f23; } gm_tctk9teldifb1;
_Static_assert(sizeof(gm_tehgl6lkqgvt0) == 104 && _Alignof(gm_tehgl6lkqgvt0) == 8 && offsetof(gm_tehgl6lkqgvt0, f0) == 0 && offsetof(gm_tehgl6lkqgvt0, f1) == 48 && offsetof(gm_tehgl6lkqgvt0, f2) == 56 && offsetof(gm_tehgl6lkqgvt0, f3) == 96, "layout of `Scope`");
_Static_assert(sizeof(gm_tjefu848ssvm9) == 48 && _Alignof(gm_tjefu848ssvm9) == 8 && offsetof(gm_tjefu848ssvm9, f0) == 0 && offsetof(gm_tjefu848ssvm9, f1) == 40, "layout of `?DestCall`");
_Static_assert(sizeof(gm_tsth9qu1dv0g3) == 40 && _Alignof(gm_tsth9qu1dv0g3) == 8 && offsetof(gm_tsth9qu1dv0g3, f0) == 0 && offsetof(gm_tsth9qu1dv0g3, f1) == 8, "layout of `Callee`");
_Static_assert(sizeof(gm_t1g28mkc78m0c) == 112 && _Alignof(gm_t1g28mkc78m0c) == 8 && offsetof(gm_t1g28mkc78m0c, f0) == 0 && offsetof(gm_t1g28mkc78m0c, f1) == 104, "layout of `?Scope`");
_Static_assert(sizeof(gm_tctk9teldifb1) == 408 && _Alignof(gm_tctk9teldifb1) == 8 && offsetof(gm_tctk9teldifb1, f0) == 0 && offsetof(gm_tctk9teldifb1, f1) == 8 && offsetof(gm_tctk9teldifb1, f2) == 16 && offsetof(gm_tctk9teldifb1, f3) == 32 && offsetof(gm_tctk9teldifb1, f4) == 40 && offsetof(gm_tctk9teldifb1, f5) == 64 && offsetof(gm_tctk9teldifb1, f6) == 104 && offsetof(gm_tctk9teldifb1, f7) == 152 && offsetof(gm_tctk9teldifb1, f8) == 176 && offsetof(gm_tctk9teldifb1, f9) == 184 && offsetof(gm_tctk9teldifb1, f10) == 224 && offsetof(gm_tctk9teldifb1, f11) == 232 && offsetof(gm_tctk9teldifb1, f12) == 240 && offsetof(gm_tctk9teldifb1, f13) == 280 && offsetof(gm_tctk9teldifb1, f14) == 296 && offsetof(gm_tctk9teldifb1, f15) == 297 && offsetof(gm_tctk9teldifb1, f16) == 298 && offsetof(gm_tctk9teldifb1, f17) == 304 && offsetof(gm_tctk9teldifb1, f18) == 312 && offsetof(gm_tctk9teldifb1, f19) == 328 && offsetof(gm_tctk9teldifb1, f20) == 344 && offsetof(gm_tctk9teldifb1, f21) == 360 && offsetof(gm_tctk9teldifb1, f22) == 364 && offsetof(gm_tctk9teldifb1, f23) == 368, "layout of `FnGen`");
struct gm_fr_add_member_name_j3i0eje {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_adt_def_ni54he1 {
    gm_frame h;
    void * ret;
};
struct gm_fr_any_child_ffc8h1k {
    gm_frame h;
    gm_closure l1; /* pred */
    uint8_t l2; /* found */
    uint8_t ret;
};
struct gm_fr_assign_addr_rlofoa5 {
    gm_frame h;
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_big_place_raoupin {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_bits_layout_23rect9 {
    gm_frame h;
    gm_t57euh612ligr ret;
};
struct gm_fr_block_31qi85g {
    gm_frame h;
    gm_tehgl6lkqgvt0 l2; /* scope */
    gm_tehgl6lkqgvt0 l4; /* popped */
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_block_inner_83vev92 {
    gm_frame h;
    gm_tqgtd5gtgj65s ret;
};
struct gm_fr_block_scope_e3cgusu {
    gm_frame h;
    gm_tehgl6lkqgvt0 ret;
};
struct gm_fr_builds_in_place_ae1b90u {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_by_ref_fi0o860 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_c_abi_wrapper_33vbmt4 {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* params */
    gm_trqfvmjtt4sij l4; /* args */
    gm_trqfvmjtt4sij l14; /* with_out */
    gm_b0_1 ret;
};
struct gm_fr_call_depth_4m6ap7n {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_calls_probe_2ia3rhf {
    gm_frame h;
    void * l0; /* g */
    gm_tsth9qu1dv0g3 l2; /* callee */
    uint8_t l5; /* ok */
    uint8_t ret;
};
struct gm_fr_captured_locals_glsvloj {
    gm_frame h;
    gm_t4c7ipjj9s3fp l1; /* set */
    gm_t4c7ipjj9s3fp l2; /* set */
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_check_unwind_2hji4bl {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_collect_upvars_q0085da {
    gm_frame h;
    void * l0; /* prog */
    uint32_t l1; /* f */
    void * l3; /* out */
    gm_b0_1 ret;
};
struct gm_fr_collect_withs_fa29mk1 {
    gm_frame h;
    void * l1; /* out */
    gm_t710lsrq1m8gk l2; /* w */
    gm_b0_1 ret;
};
struct gm_fr_contains_gkki4kq {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_conv_attr_m0b89pc {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_defer_body_avm7tau {
    gm_frame h;
    gm_td7pkamr64l0c l4; /* saved */
    gm_b0_1 ret;
};
struct gm_fr_defer_body_inner_v8ddaht {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_empty_slice_4q8glfd {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_84b0b46 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_93nt26c {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_gm6v230 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_k21v01c {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_lv6i4jh {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_meisvg6 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ppl3r45 {
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
struct gm_fr_end_lines_99v0bhd {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_end_scope_45ar5qv {
    gm_frame h;
    gm_tehgl6lkqgvt0 l1; /* s */
    gm_thognv9odov03 l7; /* l */
    gm_b0_1 ret;
};
struct gm_fr_exit_c_name_i72r98r {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_expr_call_depth_dliqc7u {
    gm_frame h;
    void * l0; /* g */
    uint32_t l2; /* depth */
    gm_tsth9qu1dv0g3 l3; /* callee */
    uint32_t ret;
};
struct gm_fr_expr_into_dn9kavo {
    gm_frame h;
    gm_t5o0tpv5jka2l l5; /* r */
    gm_trqfvmjtt4sij l16; /* l */
    gm_trqfvmjtt4sij l38; /* vals */
    gm_tehgl6lkqgvt0 l52; /* scope */
    gm_tehgl6lkqgvt0 l54; /* popped */
    gm_tqgtd5gtgj65s ret;
};
struct gm_fr_fixed_address_5usq50j {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_fn_480otki {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_fn_5evrfa7 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_7u87bne {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_count_g9jh9qr {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_fn_i2el8vi {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_fn_jjarlg6 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_kssb0q2 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_v0t9sik {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_frame_field_nj8m22n {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_frame_locals_mt4nb6o {
    gm_frame h;
    gm_trqfvmjtt4sij l5; /* out */
    gm_t4c7ipjj9s3fp l10; /* set */
    gm_tsoie1ppgdvcg l15; /* l */
    gm_b16_8 ret;
};
struct gm_fr_frame_name_stddmgg {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_frame_pure_cbdra2q {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_frame_up_g8486eu {
    gm_frame h;
    gm_tommf4j9upomc ret;
};
struct gm_fr_fresh_5kokjmg {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_fresh_tmp_6tadl69 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_gen_body_j6ku8qk {
    gm_frame h;
    gm_trqfvmjtt4sij l10; /* params */
    gm_tctk9teldifb1 l26; /* fg */
    gm_tsoie1ppgdvcg l30; /* l */
    gm_tqgtd5gtgj65s ret;
};
struct gm_fr_gen_fn_2fqvb90 {
    gm_frame h;
    gm_trqfvmjtt4sij l6; /* frame */
    gm_trqfvmjtt4sij l8; /* local_names */
    gm_tsoie1ppgdvcg l11; /* l */
    gm_t4c7ipjj9s3fp l12; /* taken */
    gm_tsoie1ppgdvcg l16; /* l */
    gm_trqfvmjtt4sij l22; /* withs */
    gm_tqgtd5gtgj65s ret;
};
struct gm_fr_get_ag7vhtq {
    gm_frame h;
    gm_tqs5p4n2tq19v ret;
};
struct gm_fr_has_fiber_with_53c3jik {
    gm_frame h;
    gm_t710lsrq1m8gk l1; /* w */
    uint8_t ret;
};
struct gm_fr_hash_map_cuh4fs9 {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_t9h7o8p {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hint_attrs_mktdhi1 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* s */
    gm_b16_8 ret;
};
struct gm_fr_indent_line_b3mckqa {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_is_borrowed_qi4apdt {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_never_in5ftma {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_unit_17uennd {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_unit_n83ttnb {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_kind_ghvav22 {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_label_710oev4 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_ladder_36amdbn {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_ladder_target_84n3aep {
    gm_frame h;
    gm_tj64ui6kruni6 ret;
};
struct gm_fr_layout_of_e6hfj1r {
    gm_frame h;
    gm_tbccvikg6b96e ret;
};
struct gm_fr_line_l6u87p9 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_list_0c2v515 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_9ljroit {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_epvducg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_kl0t9m0 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_ns53od0 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_rl2scuf {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_s83jn2q {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_ubba23m {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_local_addr_g9lkr6l {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_local_exit_4aogt00 {
    gm_frame h;
    gm_tehgl6lkqgvt0 l7; /* s */
    gm_thognv9odov03 l9; /* l */
    gm_tehgl6lkqgvt0 l17; /* s */
    gm_tehgl6lkqgvt0 l23; /* s */
    gm_thognv9odov03 l24; /* l */
    uint8_t ret;
};
struct gm_fr_local_lv_m2jtf0n {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_loop_scope_ee94ihm {
    gm_frame h;
    gm_tehgl6lkqgvt0 ret;
};
struct gm_fr_mask_epjl3to {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_may_write_702phjq {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_nests_in_place_jc41ehn {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_no_defers_hci755r {
    gm_frame h;
    gm_tehgl6lkqgvt0 l3; /* s */
    uint8_t ret;
};
struct gm_fr_offsets_eh6vfle {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_param_cty_e8tvhrm {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_param_index_rp1nn6k {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_plain_cty_q9lhlci {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_pop_g2usrvd {
    gm_frame h;
    gm_tehgl6lkqgvt0 l2; /* x */
    gm_t1g28mkc78m0c ret;
};
struct gm_fr_probe_free_4hilimu {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_reads_place_nji55rm {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_replacen1_7ksfmmn {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_restore_scopes_f62992p {
    gm_frame h;
    gm_td7pkamr64l0c l1; /* saved */
    gm_b0_1 ret;
};
struct gm_fr_set_aside_scopes_ho12if8 {
    gm_frame h;
    gm_td7pkamr64l0c l1; /* saved */
    gm_td7pkamr64l0c ret;
};
struct gm_fr_size_attrs_abk1l3m {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_size_dmhtkib {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_size_of_k0fluj2 {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_src_line_d24sajl {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_start_exit_m57dnn5 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_starts_with_6vvt01e {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_stmt_1t29bfr {
    gm_frame h;
    gm_trqfvmjtt4sij l53; /* addrs */
    gm_trqfvmjtt4sij l71; /* saved_out */
    gm_td7pkamr64l0c l73; /* saved */
    gm_t5o0tpv5jka2l l74; /* r */
    gm_tqgtd5gtgj65s ret;
};
struct gm_fr_stmt_block_5vkbn0n {
    gm_frame h;
    gm_tqgtd5gtgj65s l3; /* r */
    gm_tqgtd5gtgj65s ret;
};
struct gm_fr_stmt_span_d0pel0p {
    gm_frame h;
    gm_toceffb697p0u ret;
};
struct gm_fr_stmts_1199hll {
    gm_frame h;
    gm_tqgtd5gtgj65s ret;
};
struct gm_fr_tagged_into_ncahfuo {
    gm_frame h;
    gm_tqgtd5gtgj65s ret;
};
struct gm_fr_tmp_uninit_jr39s5u {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_tmp_uohsptq {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_unique_members_mrojmjf {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_unit_2age34u {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_unit_8ekedsl {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_unit_ty_vgceilu {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_unsupported_531q51r {
    gm_frame h;
    gm_t08olq97rscn3 ret;
};
struct gm_fr_unwind_call_4k8ulav {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_unwind_ladder_a9ij1gj {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_variant_into_2smodtg {
    gm_frame h;
    gm_tckdobunaulee l8; /* variant */
    gm_tqgtd5gtgj65s ret;
};
struct gm_fr_with_scope_o670qbc {
    gm_frame h;
    gm_tehgl6lkqgvt0 ret;
};
struct gm_fr_wrap_into_lm24d33 {
    gm_frame h;
    gm_tqgtd5gtgj65s ret;
};
struct gm_fr_zero_tlk13aa {
    gm_frame h;
    gm_b16_8 ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b);
inline bool gm_eqgnrg8h3j6k(const uint8_t *a, const uint8_t *b);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_add_member_name_j3i0eje(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void * gm_f_adt_def_ni54he1(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void * gm_f_adt_of_gej99vn(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_any_child_ffc8h1k(void * a0, gm_closure a1);
__attribute__((aligned(4))) void gm_f_assign_addr_rlofoa5(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) void gm_f_backtrace_name_mars429(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
inline __attribute__((aligned(4))) uint8_t gm_f_big_place_raoupin(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) void gm_f_bits_layout_23rect9(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_bits_layout_u6kkccp(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_bits_raw_e0qg9mb(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_block_31qi85g(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_block_inner_83vev92(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
inline __attribute__((aligned(4))) void gm_f_block_scope_e3cgusu(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_borrowed_params_b39va3r(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) uint8_t gm_f_builds_in_place_ae1b90u(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_by_ref_fi0o860(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_c_abi_wrapper_33vbmt4(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, void * a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_c_string_n7h27b3(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) uint32_t gm_f_call_depth_4m6ap7n(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) uint8_t gm_f_calls_probe_2ia3rhf(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_captured_locals_glsvloj(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_check_unwind_2hji4bl(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_clear_outside_q01mr8i(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_collect_upvars_q0085da(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, void * a2, void * a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_collect_withs_fa29mk1(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_contains_gkki4kq(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_conv_attr_m0b89pc(uint8_t a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_cty_66ee0kf(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_debug_view_gp38euq(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_defer_body_avm7tau(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, uint8_t a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_defer_body_inner_v8ddaht(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, uint8_t a3);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_4q8glfd(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_84b0b46(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_93nt26c(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_gm6v230(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_k21v01c(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lv6i4jh(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_meisvg6(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ppl3r45(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rcd2jln(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_s1lbe5s(void);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_end_lines_99v0bhd(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_end_scope_45ar5qv(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, uint8_t a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_exit_c_name_i72r98r(gm_t9qkh0rsooc8d a0);
__attribute__((aligned(4))) uint32_t gm_f_expr_call_depth_dliqc7u(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_expr_into_dn9kavo(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2, gm_tnrltbc61i6v1 a3);
__attribute__((aligned(4))) void gm_f_expr_porbc9t(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_ll5819n(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_vogsd72(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_efqiqfc(void * a0, void * a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_nab29co(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_fixed_address_5usq50j(void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_fn_480otki(void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_5evrfa7(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_7u87bne(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) uint64_t gm_f_fn_count_g9jh9qr(void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_fn_i2el8vi(void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_jjarlg6(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_kssb0q2(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_fn_name_6n34573(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_fn_tag_okd90ep(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_t9805qtonbsag a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_v0t9sik(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_for_each_child_fm49s73(void * a0, gm_closure a1);
__attribute__((aligned(4))) void gm_f_foreign_decl_rld7m78(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_format_07f8le4(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_32n178h(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_43nlbju(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_4hn970h(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_4un19t8(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_5bcbbmm(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tb31k5l794t7p a0);
__attribute__((aligned(4))) void gm_f_format_5erjuiq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_822rutp(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_8lfevbr(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_99gc605(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_9c0uitb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_9pumrco(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_cp0hre9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_d6fj9tl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_dd6d4bt(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_ddrb17v(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_dsta2ql(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_e1d93qv(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_eq87866(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_fsfutor(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_ghqt9ar(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_gt1sds7(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_hi89jlk(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_hs8tdgq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_kadoe0b(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_ko1kpil(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_l05am3t(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_lb0inss(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_loh175m(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_lsmcccn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_lufljqc(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_mhned9o(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_pu04c4a(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_rgk51pp(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_rmu2e6q(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_rtv6jsp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_u9etquj(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) uint64_t gm_f_frame_cost_oei64tv(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, void * a2);
__attribute__((aligned(4))) uint8_t gm_f_frame_done_014nc4i(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_frame_field_nj8m22n(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_frame_locals_mt4nb6o(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_frame_name_stddmgg(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) uint8_t gm_f_frame_pure_cbdra2q(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_frame_up_g8486eu(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
inline __attribute__((aligned(4))) uint32_t gm_f_fresh_5kokjmg(void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_fresh_tmp_6tadl69(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void * gm_f_func_in17r5n(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_gamma_value_rgbfhcc(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_gen_body_j6ku8qk(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, void * a2, void * a3, gm_t9805qtonbsag a4);
__attribute__((aligned(4))) void gm_f_gen_fn_2fqvb90(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_t9805qtonbsag a2);
inline __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_get_ag7vhtq(void * a0, gm_tcc1rq4n1avb2 a1);
__attribute__((aligned(4))) uint8_t gm_f_has_fiber_with_53c3jik(void * a0);
inline __attribute__((aligned(4))) void gm_f_hash_map_cuh4fs9(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_t9h7o8p(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b16_8 gm_f_hint_attrs_mktdhi1(void *env, gm_node *cx, gm_node *inst, gm_t1h8kp9hsu3ua a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_indent_line_b3mckqa(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_int_lit_ai37456(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, const void * a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_intern_ia19mhl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_borrowed_qi4apdt(void * a0, uint32_t a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_never_in5ftma(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_is_plain_qr08i40(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_unit_17uennd(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_unit_n83ttnb(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_join_sfknljb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_jump_pvnkvme(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tf5srh4mp4b3i a2);
inline __attribute__((aligned(4))) void gm_f_kind_ghvav22(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_label_710oev4(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_ladder_36amdbn(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_ladder_target_84n3aep(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_layout_a5ovnrm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_layout_of_e6hfj1r(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_line_directive_fjg94gc(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_line_l6u87p9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_1p9d72n(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_21h063v(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_2n883h6(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_4plvds6(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_5n29svs(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_5n8up5i(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_7p5m9ll(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_848m8fq(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_8i4929c(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_aka1a8l(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_bi02v43(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_caq6p77(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_cs9kbvs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t19enj90vg8bd a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_cuactnf(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_eils77b(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_f1jrb6n(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_g6msml8(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_h4kb1fp(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_heeeedc(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_iin7hfk(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_kta085d(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_ligf694(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_md5jtn8(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_mkfjg2u(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_njoe3l6(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_nvqsj6t(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_pduqjh2(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_qqk2cu4(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_rgkpmgb(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_thnd8mb(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_uu8i6ue(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_list_0c2v515(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_9ljroit(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_kl0t9m0(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_ns53od0(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_rl2scuf(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_s83jn2q(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_ubba23m(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b16_8 gm_f_local_addr_g9lkr6l(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) uint8_t gm_f_local_exit_4aogt00(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, gm_b16_8 a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b16_8 gm_f_local_lv_m2jtf0n(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) uint8_t gm_f_long_body_qgvuu1c(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_loop_scope_ee94ihm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_mask_epjl3to(void *env, gm_node *cx, gm_node *inst, uint32_t a0);
__attribute__((aligned(4))) uint8_t gm_f_may_write_702phjq(void * a0);
__attribute__((aligned(4))) void gm_f_mirror_lcjegsf(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) uint8_t gm_f_needs_wrapper_ktlbb7i(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) uint8_t gm_f_nests_in_place_jc41ehn(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tnrltbc61i6v1 a2);
__attribute__((aligned(4))) uint8_t gm_f_no_defers_hci755r(void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_offsets_eh6vfle(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) uint8_t gm_f_outlines_2kvbvit(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_param_cty_e8tvhrm(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_param_index_rp1nn6k(void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_place_addr_7ji15ot(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_plain_cty_q9lhlci(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_plain_value_e380k1u(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_g2usrvd(void *gm_out, void * a0);
__attribute__((aligned(4))) uint8_t gm_f_probe_free_4hilimu(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_2lk6rs2(void * a0, gm_csn5gfg3c8ooaj a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_7dfimu1(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_7o6oubd(void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_d0271rf(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_givp2as(void * a0, gm_cs7e9ppsd384bj a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_pcqb56b(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_t83rg2m(void * a0, uint8_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_vd1290p(void * a0, uint8_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_3g1tdpr(void * a0, gm_tcc1rq4n1avb2 a1, uint8_t a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_5c6qgui(void * a0, gm_b16_8 a1, uint8_t a2);
__attribute__((aligned(4))) uint8_t gm_f_reads_place_nji55rm(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_replacen1_7ksfmmn(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_repr_53quqki(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_restore_scopes_f62992p(void * a0, const void * a1);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_root_local_40frg7f(void * a0, void * a1);
inline __attribute__((aligned(4))) void gm_f_set_aside_scopes_ho12if8(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_size_attrs_abk1l3m(uint32_t a0);
inline __attribute__((aligned(4))) uint64_t gm_f_size_dmhtkib(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) uint64_t gm_f_size_of_k0fluj2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_spec_name_jptsbbf(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint32_t a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_src_line_d24sajl(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_start_exit_m57dnn5(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, gm_b16_8 a2, gm_b16_8 a3, gm_b16_8 a4);
inline __attribute__((aligned(4))) uint8_t gm_f_starts_with_6vvt01e(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_stmt_1t29bfr(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_stmt_block_5vkbn0n(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_stmt_list_d9ovq7d(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_toceffb697p0u gm_f_stmt_span_d0pel0p(void * a0);
inline __attribute__((aligned(4))) void gm_f_stmts_1199hll(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_tagged_into_ncahfuo(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, uint64_t a3, uint64_t a4, void * a5, uint64_t a6, gm_b16_8 a7, gm_tnrltbc61i6v1 a8);
__attribute__((aligned(4))) gm_b16_8 gm_f_tmp_uninit_jr39s5u(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_tmp_uohsptq(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_unique_members_mrojmjf(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_unique_with_mffff7e(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1, uint8_t a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unit_2age34u(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_unit_8ekedsl(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unit_ty_vgceilu(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void gm_f_unsupported_531q51r(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_unwind_call_4k8ulav(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_unwind_ladder_a9ij1gj(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_variant_into_2smodtg(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, void * a2, gm_b16_8 a3, gm_tnrltbc61i6v1 a4);
__attribute__((aligned(4))) void gm_f_with_scope_o670qbc(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_wrap_into_lm24d33(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, uint8_t a2, uint8_t a3, void * a4, gm_b16_8 a5, gm_tnrltbc61i6v1 a6);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_3c9chj4(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_ajj3a37(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_cdouvhd(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_f1cbljc(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_i2vvij8(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_ih4qokb(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_ihqa4kk(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_ildv1bj(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_j67vjku(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_l0vqbl8(void * a0, gm_t4ce0jd3tdro4 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_m7rqe8h(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_n0vcum5(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_s0f8vaq(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_s858s21(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_shh24f1(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_va1cfg2(void * a0, const void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_zero_tlk13aa(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
extern uint8_t gm_c1id0n0l1pf[8] __attribute__((aligned(8)));
extern uint8_t gm_c71idar1l1k[1] __attribute__((aligned(1)));
extern uint8_t gm_cbemal9c6bf[8] __attribute__((aligned(8)));
extern uint8_t gm_cc9imq1spis[16] __attribute__((aligned(8)));
extern uint8_t gm_ccmgjp6th6r[16] __attribute__((aligned(8)));
extern uint8_t gm_cdmki9acqi9[4] __attribute__((aligned(4)));
extern uint8_t gm_cgsmfcq937e_4[16] __attribute__((aligned(8)));
extern uint8_t gm_csshki16t34[4] __attribute__((aligned(4)));
extern uint8_t gm_ctq6t8frtor[4] __attribute__((aligned(4)));
extern uint8_t gm_cvpsnk4jni3[4] __attribute__((aligned(4)));
extern const uint8_t gm_s04qus0luoga5[5];
extern const uint8_t gm_s0m07bui94rjq[8];
extern const uint8_t gm_s0r850bpi2rnm[14];
extern const uint8_t gm_s2254b9hkee3b[6];
extern const uint8_t gm_s2cuflhqkkuaq[2];
extern const uint8_t gm_s2gtin895e08a[18];
extern const uint8_t gm_s2hbi2g88pale[19];
extern const uint8_t gm_s2hi6cc9n75l5[7];
extern const uint8_t gm_s2kvk8bnjk52t[12];
extern const uint8_t gm_s2micd2hub1nf[26];
extern const uint8_t gm_s2ogeh8fp9da3[12];
extern const uint8_t gm_s39jha51ulivf[27];
extern const uint8_t gm_s3cu59itn73k7[23];
extern const uint8_t gm_s4ql65vkv7h9p[13];
extern const uint8_t gm_s65rai75evpi7[14];
extern const uint8_t gm_s8hh4pp2rpnlj[43];
extern const uint8_t gm_s9nvmaqikg2bc[8];
extern const uint8_t gm_sapbk38ibps8q[28];
extern const uint8_t gm_sb8kl54a499bf[7];
extern const uint8_t gm_sbrtnmpf78kqq[10];
extern const uint8_t gm_sdi44r3kug6n6[14];
extern const uint8_t gm_sdikuksp636di[12];
extern const uint8_t gm_sg398rgmck67b[4];
extern const uint8_t gm_sg5gdnrith2i6[3];
extern const uint8_t gm_sgdqi8a9n3f12[15];
extern const uint8_t gm_sh40na9qbga3l[2];
extern const uint8_t gm_sicgmpca63jln[2];
extern const uint8_t gm_siji3oo7ud53t[10];
extern const uint8_t gm_sjibpf3glbc54[9];
extern const uint8_t gm_sjs17a3d2ohel[35];
extern const uint8_t gm_skdpq4t6o94o2[25];
extern const uint8_t gm_skdv3lmatcupg[21];
extern const uint8_t gm_slnuijmj5p1vk[19];
extern const uint8_t gm_sm7odqe0d6lgu[23];
extern const uint8_t gm_smap40unho18s[19];
extern const uint8_t gm_smr5ddngcjuao[19];
extern const uint8_t gm_sn2gg3b98t635[21];
extern const uint8_t gm_so0h0lkl6bp4l[39];
extern const uint8_t gm_so514oj5nbhcu[2];
extern const uint8_t gm_soa9mnl4ahjud[8];
extern const uint8_t gm_soah6d9unb7uo[32];
extern const uint8_t gm_sovhd8e8hvflr[52];
extern const uint8_t gm_sp1g56u3ibrtp[23];
extern const uint8_t gm_spdft68ac8308[95];
extern const uint8_t gm_spdntfv7vkd2t[17];
extern const uint8_t gm_spdpobhso0dbc[10];
extern const uint8_t gm_sqtc9fffut4s2[13];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src0gtuh94b1fce[];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src3d0v42kavhv7[];
extern const char gm_srcbsc23is03ttm[];
extern const char gm_srcc24pnihhgkug[];
extern const char gm_srcd5arorrf8jkg[];
extern const char gm_srcht195tfrfuqo[];
extern const char gm_srckfibkpt681aa[];
extern const char gm_srcmok80ho8hh01[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_ss03naco6s5l7[5];
extern const uint8_t gm_sts5j6f5pkov0[5];
extern const uint8_t gm_sug1rl2kq22u3[9];
extern const uint8_t gm_sugv53k5dm8d7[24];
extern const uint8_t gm_suuphrufuvh4m[12];
extern const uint8_t gm_sv394jlms97cd[14];
extern const uint8_t gm_svdapq3j66qso[60];
extern const uint8_t gm_svg1jvtfj7fgp[6];
extern const uint8_t gm_svj967tkdfgka[15];
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
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq467d5cm1d5(a + 0, b + 0)) return false;
    return true;
}
inline bool gm_eqgnrg8h3j6k(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    return true;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_add_member_name_j3i0eje(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_add_member_name_j3i0eje F;
    void * gm_l0; /* ty */
    gm_b16_8 gm_l1; /* name */
    GM_STACK_CHECK(&F, 58u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 816);
        gm_b16_8 t4 = gm_l1;
        gm_tftb1odc739t5 t5 = gm_f_put_5c6qgui(t3, t4, ((uint8_t)1));
        if (((((uint8_t *)&t5))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t5) + 1, gm_srckfibkpt681aa, 450, 5);
        gm_b0_1 t6 = GM_LD(gm_b0_1, (((uint8_t *)&t5) + 1));
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void * gm_f_adt_def_ni54he1(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_adt_def_ni54he1 F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    void * t3 = (void *)(((uint8_t *)t2) + 8);
    gm_t88rcucg1b7uk t4 = gm_l1;
    void * t5 = gm_f_adt_of_gej99vn(t3, GM_CONV(gm_csguankko4lmhd, t4));
    F.ret = t5;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_any_child_ffc8h1k(void * a0, gm_closure a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_any_child_ffc8h1k F;
    void * gm_l0; /* e */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    F.l1 = a1;
    {
        F.l2 = ((uint8_t)0);
    }
    {
        void * t2 = gm_l0;
        gm_closure t3 = ((gm_closure){(void *)gm_f_fn_7u87bne, (void *)&F.h, cx});
        gm_f_for_each_child_fm49s73(t2, t3);
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
inline __attribute__((aligned(4))) uint8_t gm_f_big_place_raoupin(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_big_place_raoupin F;
    void * gm_l0; /* fg */
    void * gm_l1; /* e */
    GM_STACK_CHECK(&F, 82u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint8_t t2 = 0;
        void * t3 = gm_l0;
        void * t4 = gm_l1;
        uint8_t t5 = gm_f_reads_place_nji55rm((void *)NULL, cx, NULL, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (t5) {
            void * t6 = gm_l0;
            void * t7 = GM_LD(void *, (((uint8_t *)t6) + 0));
            void * t8 = (void *)(((uint8_t *)t7) + 8);
            void * t9 = gm_l1;
            gm_t88rcucg1b7uk t10 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t9) + 0));
            uint64_t t11 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t8, t10);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t12 = (t11 > ((uint64_t)256ULL));
            t2 = t12;
        } else t2 = 0;
        uint8_t t13 = 0;
        F.ret = t2;
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
inline __attribute__((aligned(4))) void gm_f_bits_layout_23rect9(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_bits_layout_23rect9 F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_f_bits_layout_u6kkccp((void *)&(*(gm_t57euh612ligr *)gm_out), (void *)NULL, cx, NULL, t2, t3);
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
inline __attribute__((aligned(4))) void gm_f_block_scope_e3cgusu(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_block_scope_e3cgusu F;
    void * gm_l0; /* fg */
    GM_STACK_CHECK(&F, 108u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memset((uint8_t *)&(*(gm_tehgl6lkqgvt0 *)gm_out) + 52, 0, 4);
    gm_tbf8u1b86cgmi t1;
    memset(&t1, 0, sizeof t1);
    GM_ST(uint8_t, ((uint8_t *)&t1 + 0), ((uint8_t)0ULL));
    void * t2 = gm_l0;
    uint32_t t3 = gm_f_fresh_5kokjmg(t2);
    gm_trqfvmjtt4sij t4;
    gm_f_list_rl2scuf((void *)&t4, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_tbf8u1b86cgmi, (uint8_t *)&(*(gm_tehgl6lkqgvt0 *)gm_out) + 0, t1);
    GM_ST(uint32_t, (uint8_t *)&(*(gm_tehgl6lkqgvt0 *)gm_out) + 48, t3);
    GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_tehgl6lkqgvt0 *)gm_out) + 56, t4);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tehgl6lkqgvt0 *)gm_out) + 96, ((uint64_t)0ULL));
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
inline __attribute__((aligned(4))) uint8_t gm_f_by_ref_fi0o860(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_by_ref_fi0o860 F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 73u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t t1 = 0;
    void * t2 = gm_l0;
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_t5v8n6qrn4rh4 t4;
    gm_f_repr_53quqki((void *)&t4, (void *)NULL, cx, NULL, t2, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t5 = ((GM_LD(uint8_t, ((uint8_t *)&t4) + 0) == ((uint8_t)6ULL)));
    if (t5) {
        void * t6 = gm_l0;
        gm_t88rcucg1b7uk t7 = gm_l1;
        uint64_t t8 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t6, t7);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t9 = (t8 > ((uint64_t)16ULL));
        t1 = t9;
    } else t1 = 0;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_check_unwind_2hji4bl(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_check_unwind_2hji4bl F;
    void * gm_l0; /* fg */
    GM_STACK_CHECK(&F, 50u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t3 = gm_l0;
        uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)t3) + 296));
        if (!t4) goto ie2;
        {
            gm_b0_1 t6;
            uint8_t t7 = 0;
            F.ret = t6;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t8 = gm_l0;
        gm_b16_8 t9;
        GM_ST(void *, (uint8_t *)&t9, (void *)(gm_smr5ddngcjuao));
        GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(18));
        gm_b0_1 t10 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t8, t9);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t11 = gm_l0;
        gm_b0_1 t12 = gm_f_unwind_ladder_a9ij1gj((void *)NULL, cx, NULL, t11);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_conv_attr_m0b89pc(uint8_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_conv_attr_m0b89pc F;
    uint8_t gm_l0; /* win64 */
    GM_STACK_CHECK_SHALLOW(&F, 114u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t1;
    uint8_t t3 = gm_l0;
    if (!t3) goto ie2;
    gm_b16_8 t5;
    GM_ST(void *, (uint8_t *)&t5, (void *)(gm_skdpq4t6o94o2));
    GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(24));
    gm_b16_8 t6 = t5;
    t1 = t6;
    goto ix2;
ie2:;
    gm_b16_8 t8;
    GM_ST(void *, (uint8_t *)&t8, (void *)(gm_sr4qbrk6tlgaa));
    GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(0));
    gm_b16_8 t9 = t8;
    t1 = t9;
ix2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_defer_body_avm7tau(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, uint8_t a3) {
    struct gm_fr_defer_body_avm7tau F;
    void * gm_l0; /* fg */
    gm_th2lqbo77pvsc gm_l1; /* guard */
    void * gm_l2; /* body */
    uint8_t gm_l3; /* unwinding */
    GM_STACK_CHECK(&F, 74u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&gm_l1, a1, sizeof gm_l1);
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        gm_f_set_aside_scopes_ho12if8((void *)&F.l4, (void *)NULL, cx, NULL, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t3 = gm_l0;
        void * t4 = gm_l2;
        uint8_t t5 = gm_l3;
        gm_b0_1 t6 = gm_f_defer_body_inner_v8ddaht((void *)NULL, cx, NULL, t3, &(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l1)), t4, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t7 = gm_l0;
        gm_f_restore_scopes_f62992p(t7, &(*(gm_td7pkamr64l0c *)((uint8_t *)&F.l4)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_4q8glfd(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_4q8glfd F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_84b0b46(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_84b0b46 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_93nt26c(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_93nt26c F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 48)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_gm6v230(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_gm6v230 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_k21v01c(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_k21v01c F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lv6i4jh(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_lv6i4jh F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 104)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_meisvg6(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_meisvg6 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ppl3r45(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_ppl3r45 F;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_end_lines_99v0bhd(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_end_lines_99v0bhd F;
    void * gm_l0; /* fg */
    GM_STACK_CHECK(&F, 75u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t3 = gm_l0;
        void * t4 = GM_LD(void *, (((uint8_t *)t3) + 0));
        uint8_t t5 = GM_LD(uint8_t, ((((uint8_t *)t4) + 1136) + 2));
        if (!t5) goto ie2;
        {
            void * t7 = gm_l0;
            void * t8 = (void *)(((uint8_t *)t7) + 184);
            gm_b16_8 t9 = GM_LD(gm_b16_8, gm_ccmgjp6th6r);
            gm_tnpifv20irnph t10;
            GM_ST(gm_b16_8, ((uint8_t *)&t10 + 0), t9);
            gm_tftb1odc739t5 t11 = gm_f_write_cdouvhd(t8, t10);
            if (((((uint8_t *)&t11))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t11) + 1, gm_srcmok80ho8hh01, 445, 31);
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t11) + 1));
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
inline __attribute__((aligned(4))) uint8_t gm_f_fn_480otki(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fn_480otki F;
    void * gm_l0; /* c */
    GM_STACK_CHECK(&F, 19u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    uint8_t t2 = gm_f_may_write_702phjq(t1);
    F.ret = t2;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_5evrfa7(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_5evrfa7 F;
    void * gm_l0; /* x */
    GM_STACK_CHECK(&F, 26u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t3 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_calls_probe_2ia3rhf *)((&F.h)->up))->l0));
        void * t4 = gm_l0;
        uint8_t t5 = gm_f_calls_probe_2ia3rhf((void *)NULL, cx, NULL, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t6 = (uint8_t)!t5;
        if (!t6) goto ie2;
        {
            GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_calls_probe_2ia3rhf *)((&F.h)->up))->l5), ((uint8_t)0));
        }
        goto ix2;
ie2:;
ix2:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_7u87bne(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_7u87bne F;
    void * gm_l0; /* c */
    GM_STACK_CHECK(&F, 75u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_any_child_ffc8h1k *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            gm_closure t6 = GM_LD(gm_closure, ((uint8_t *)&((struct gm_fr_any_child_ffc8h1k *)((&F.h)->up))->l1));
            void * t7 = gm_l0;
            gm_closure t8 = t6;
            gm_node *ccx9 = t8.cx;
            void * t10 = t8.code;
            uint8_t t11;
            if (GM_PTR_TAGGED(t10)) {
                t11 = ((uint8_t (*)(void *))GM_PTR_UNTAG(t10))(t7);
            } else {
                t11 = ((uint8_t (*)(void *, gm_node *, gm_node *, void *))t10)(t8.env, ccx9, NULL, t7);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_any_child_ffc8h1k *)((&F.h)->up))->l2), t11);
        }
        goto ix2;
ie2:;
ix2:;
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
inline __attribute__((aligned(4))) uint64_t gm_f_fn_count_g9jh9qr(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fn_count_g9jh9qr F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    uint64_t t3 = GM_LD(uintptr_t, ((((uint8_t *)t2) + 344) + 0) + 8);
    F.ret = t3;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_fn_i2el8vi(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fn_i2el8vi F;
    void * gm_l0; /* c */
    GM_STACK_CHECK(&F, 19u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    uint8_t t2 = gm_f_has_fiber_with_53c3jik(t1);
    F.ret = t2;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_jjarlg6(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_jjarlg6 F;
    void * gm_l0; /* c */
    GM_STACK_CHECK(&F, 36u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_collect_upvars_q0085da *)((&F.h)->up))->l0));
        uint32_t t3 = GM_LD(uint32_t, ((uint8_t *)&((struct gm_fr_collect_upvars_q0085da *)((&F.h)->up))->l1));
        void * t4 = gm_l0;
        void * t5 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_collect_upvars_q0085da *)((&F.h)->up))->l3));
        gm_b0_1 t6 = gm_f_collect_upvars_q0085da((void *)NULL, cx, NULL, t2, t3, t4, t5);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_kssb0q2(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_kssb0q2 F;
    void * gm_l0; /* c */
    GM_STACK_CHECK(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_collect_withs_fa29mk1 *)((&F.h)->up))->l1));
        gm_b0_1 t4 = gm_f_collect_withs_fa29mk1((void *)NULL, cx, NULL, t2, t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_v0t9sik(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_v0t9sik F;
    void * gm_l0; /* x */
    GM_STACK_CHECK(&F, 45u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint32_t t3 = GM_LD(uint32_t, ((uint8_t *)&((struct gm_fr_expr_call_depth_dliqc7u *)((&F.h)->up))->l2));
        uint32_t t4 = GM_LD(uint32_t, gm_cdmki9acqi9);
        uint8_t t5 = (t3 <= t4);
        if (!t5) goto ie2;
        {
            uint32_t t7 = GM_LD(uint32_t, ((uint8_t *)&((struct gm_fr_expr_call_depth_dliqc7u *)((&F.h)->up))->l2));
            void * t8 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_expr_call_depth_dliqc7u *)((&F.h)->up))->l0));
            void * t9 = gm_l0;
            uint32_t t10 = gm_f_expr_call_depth_dliqc7u((void *)NULL, cx, NULL, t8, t9);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint32_t t11 = (t7 > t10 ? t7 : t10);
            GM_ST(uint32_t, ((uint8_t *)&((struct gm_fr_expr_call_depth_dliqc7u *)((&F.h)->up))->l2), t11);
        }
        goto ix2;
ie2:;
ix2:;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_frame_name_stddmgg(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_frame_name_stddmgg F;
    void * gm_l0; /* g */
    uint32_t gm_l1; /* f */
    GM_STACK_CHECK(&F, 152u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    uint32_t t2 = gm_l1;
    gm_t9805qtonbsag t3;
    memset(&t3, 0, sizeof t3);
    gm_b16_8 t4 = gm_f_fn_tag_okd90ep((void *)NULL, cx, NULL, t1, t2, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tnpifv20irnph t5;
    GM_ST(gm_b16_8, ((uint8_t *)&t5 + 0), t4);
    gm_tabb710d9mq2e t6;
    gm_f_format_5erjuiq((void *)&t6, (void *)NULL, cx, NULL, t5);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (((((uint8_t *)&t6))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t6) + 8, gm_srcht195tfrfuqo, 167, 60);
    gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&t6) + 8));
    gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&t7));
    F.ret = t8;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint32_t gm_f_fresh_5kokjmg(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fresh_5kokjmg F;
    void * gm_l0; /* fg */
    GM_STACK_CHECK_SHALLOW(&F, 77u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        void * t3 = gm_l0;
        uint32_t t4 = GM_LD(uint32_t, (((uint8_t *)t3) + 232));
        uint32_t r5; bool o6 = __builtin_add_overflow(t4, ((uint32_t)1ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_srcmok80ho8hh01, 480, 5);
        uint32_t t7 = r5;
        GM_ST(uint32_t, (((uint8_t *)t2) + 232), t7);
    }
    {
        void * t8 = gm_l0;
        void * t9 = gm_l0;
        uint32_t t10 = GM_LD(uint32_t, (((uint8_t *)t9) + 360));
        uint32_t r11; bool o12 = __builtin_add_overflow(t10, ((uint32_t)1ULL), &r11);
        if (o12) gm_trap("integer overflow in `+`", gm_srcmok80ho8hh01, 481, 5);
        uint32_t t13 = r11;
        GM_ST(uint32_t, (((uint8_t *)t8) + 360), t13);
    }
    {
        void * t14 = gm_l0;
        uint32_t t15 = GM_LD(uint32_t, (((uint8_t *)t14) + 232));
        uint8_t t16 = 0;
        F.ret = t15;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_get_ag7vhtq(void * a0, gm_tcc1rq4n1avb2 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_get_ag7vhtq F;
    void * gm_l0; /* m */
    gm_tcc1rq4n1avb2 gm_l1; /* k */
    uint64_t gm_l2; /* i */
    GM_STACK_CHECK(&F, 115u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)((uint8_t *)&gm_l1);
        gm_tf5srh4mp4b3i t4 = gm_f_find_efqiqfc(t2, t3);
        uint64_t t5;
        if (((((uint8_t *)&t4))[8] != 0)) {
            uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)&t4) + 0));
            t5 = t6;
        } else {
            gm_tqs5p4n2tq19v t7;
            memset(&t7, 0, sizeof t7);
            uint8_t t8 = 0;
            F.ret = t7;
            goto gm_fret;
        }
        gm_l2 = t5;
    }
    {
        uint64_t t9 = gm_l2;
        uint64_t t10 = (uintptr_t)t9;
        void * t11 = gm_l0;
        uint64_t t12 = GM_LD(uintptr_t, (((uint8_t *)t11) + 0) + 8);
        if (t10 >= (uintptr_t)(t12)) gm_trap_index(t10, t12, gm_srcbsc23is03ttm, 119, 12);
        uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)t11) + 0)) + t10 * 16) + 12));
        gm_tqs5p4n2tq19v t14;
        GM_ST(uint8_t, ((uint8_t *)&t14), t13);
        ((uint8_t *)&t14)[1] = 1;
        uint8_t t15 = 0;
        F.ret = t14;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_hash_map_cuh4fs9(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_cuh4fs9 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_b16_8 t2 = gm_f_empty_slice_k21v01c();
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
inline __attribute__((aligned(4))) void gm_f_hash_map_t9h7o8p(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_t9h7o8p F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_b16_8 t2 = gm_f_empty_slice_gm6v230();
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_borrowed_qi4apdt(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_borrowed_qi4apdt F;
    void * gm_l0; /* fg */
    uint32_t gm_l1; /* l */
    uint64_t gm_l2; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 93u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        uint32_t t4 = gm_l1;
        gm_tf5srh4mp4b3i t5 = gm_f_param_index_rp1nn6k(t3, t4);
        if (!((((uint8_t *)&t5))[8] != 0)) goto ie2;
        uint64_t t6 = GM_LD(uint64_t, ((uint8_t *)&t5));
        gm_l2 = t6;
        {
            uint64_t t8 = gm_l2;
            uint64_t t9 = (uintptr_t)t8;
            void * t10 = gm_l0;
            uint64_t t11 = GM_LD(uintptr_t, (((uint8_t *)t10) + 328) + 8);
            if (t9 >= (uintptr_t)(t11)) gm_trap_index(t9, t11, gm_srcmok80ho8hh01, 559, 40);
            uint8_t t12 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t10) + 328)) + t9 * 1));
            uint8_t t13 = 0;
            F.ret = t12;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint8_t t14 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_never_in5ftma(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_never_in5ftma F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 99u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    void * t3 = (void *)(((uint8_t *)t2) + 8);
    gm_t88rcucg1b7uk t4 = gm_l1;
    gm_t53at80pfdus3 t5;
    gm_f_kind_jcnt4hm((void *)&t5, t3, GM_CONV(gm_csguankko4lmhd, t4));
    uint8_t t6 = ((GM_LD(uint8_t, ((uint8_t *)&t5) + 0) == ((uint8_t)5ULL)));
    F.ret = t6;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_unit_17uennd(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_unit_17uennd F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 43u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    void * t3 = (void *)(((uint8_t *)t2) + 8);
    gm_t88rcucg1b7uk t4 = gm_l1;
    uint8_t t5 = gm_f_is_unit_n83ttnb(t3, GM_CONV(gm_csguankko4lmhd, t4));
    F.ret = t5;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_unit_n83ttnb(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_unit_n83ttnb F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    gm_b16_8 gm_l2; /* elems */
    GM_STACK_CHECK_SHALLOW(&F, 126u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    {
        void * t3 = gm_l0;
        gm_t88rcucg1b7uk t4 = gm_l1;
        gm_t53at80pfdus3 t5;
        gm_f_kind_jcnt4hm((void *)&t5, t3, GM_CONV(gm_csguankko4lmhd, t4));
        uint8_t t6 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t6 != ((uint8_t)12ULL)) goto ie2;
        gm_b16_8 t7 = GM_LD(gm_b16_8, ((((uint8_t *)&t5) + 8) + 0));
        gm_l2 = t7;
        {
            uint64_t t9 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            uint8_t t10 = ((t9 == ((uint64_t)0ULL)));
            uint8_t t11 = 0;
            F.ret = t10;
            goto gm_fret;
        }
        goto ix2;
ie2:;
        {
            uint8_t t13 = 0;
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
inline __attribute__((aligned(4))) void gm_f_kind_ghvav22(void *gm_out, void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_kind_ghvav22 F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    void * t3 = (void *)(((uint8_t *)t2) + 8);
    gm_t88rcucg1b7uk t4 = gm_l1;
    gm_f_kind_jcnt4hm((void *)&(*(gm_t53at80pfdus3 *)gm_out), t3, GM_CONV(gm_csguankko4lmhd, t4));
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_label_710oev4(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_label_710oev4 F;
    void * gm_l0; /* fg */
    gm_b16_8 gm_l1; /* name */
    GM_STACK_CHECK(&F, 74u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 184);
        gm_b16_8 t4 = gm_l1;
        gm_tnpifv20irnph t5;
        GM_ST(gm_b16_8, ((uint8_t *)&t5 + 0), t4);
        gm_tftb1odc739t5 t6 = gm_f_write_f1cbljc(t3, t5);
        if (((((uint8_t *)&t6))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t6) + 1, gm_srcmok80ho8hh01, 535, 5);
        gm_b0_1 t7 = GM_LD(gm_b0_1, (((uint8_t *)&t6) + 1));
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_ladder_36amdbn(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_ladder_36amdbn F;
    void * gm_l0; /* fg */
    gm_b16_8 gm_l1; /* target */
    gm_tf5srh4mp4b3i gm_l2; /* scope */
    GM_STACK_CHECK(&F, 152u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        gm_tj64ui6kruni6 t3;
        gm_f_ladder_target_84n3aep((void *)&t3, (void *)NULL, cx, NULL, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tj64ui6kruni6 t4 = t3;
        gm_l1 = GM_LD(gm_b16_8, (uint8_t *)&t4 + 0);
        gm_l2 = GM_LD(gm_tf5srh4mp4b3i, (uint8_t *)&t4 + 16);
    }
    {
        void * t5 = gm_l0;
        gm_b16_8 t6 = gm_l1;
        gm_tf5srh4mp4b3i t7 = gm_l2;
        gm_b0_1 t8 = gm_f_jump_pvnkvme((void *)NULL, cx, NULL, t5, t6, t7);
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
inline __attribute__((aligned(4))) void gm_f_layout_of_e6hfj1r(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_layout_of_e6hfj1r F;
    void * gm_l0; /* lay */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 64));
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_f_layout_a5ovnrm((void *)&(*(gm_tbccvikg6b96e *)gm_out), (void *)NULL, cx, NULL, t2, t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_line_l6u87p9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_line_l6u87p9 F;
    void * gm_l0; /* fg */
    gm_b16_8 gm_l1; /* s */
    GM_STACK_CHECK(&F, 84u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        gm_b0_1 t3 = gm_f_indent_line_b3mckqa((void *)NULL, cx, NULL, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t4 = gm_l0;
        void * t5 = (void *)(((uint8_t *)t4) + 184);
        gm_b16_8 t6 = gm_l1;
        gm_tftb1odc739t5 t7 = gm_f_extend_0cs85gs(t5, t6);
        if (((((uint8_t *)&t7))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t7) + 1, gm_srcmok80ho8hh01, 464, 5);
        gm_b0_1 t8 = GM_LD(gm_b0_1, (((uint8_t *)&t7) + 1));
    }
    {
        void * t9 = gm_l0;
        void * t10 = (void *)(((uint8_t *)t9) + 184);
        gm_tftb1odc739t5 t11 = gm_f_push_t83rg2m(t10, ((uint8_t)10ULL));
        if (((((uint8_t *)&t11))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t11) + 1, gm_srcmok80ho8hh01, 465, 5);
        gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t11) + 1));
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
inline __attribute__((aligned(4))) void gm_f_list_0c2v515(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_0c2v515 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_ppl3r45();
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
inline __attribute__((aligned(4))) void gm_f_list_9ljroit(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_9ljroit F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_meisvg6();
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
inline __attribute__((aligned(4))) void gm_f_list_kl0t9m0(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_kl0t9m0 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_84b0b46();
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
inline __attribute__((aligned(4))) void gm_f_list_ns53od0(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_ns53od0 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_93nt26c();
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
inline __attribute__((aligned(4))) void gm_f_list_rl2scuf(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_rl2scuf F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_4q8glfd();
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
inline __attribute__((aligned(4))) void gm_f_list_ubba23m(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_ubba23m F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_lv6i4jh();
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_mask_epjl3to(void *env, gm_node *cx, gm_node *inst, uint32_t a0) {
    struct gm_fr_mask_epjl3to F;
    uint32_t gm_l0; /* w */
    GM_STACK_CHECK(&F, 123u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint32_t t3 = gm_l0;
        uint8_t t4 = (t3 >= ((uint32_t)128ULL));
        if (!t4) goto ie2;
        {
            gm_b16_8 t6;
            GM_ST(void *, (uint8_t *)&t6, (void *)(gm_sugv53k5dm8d7));
            GM_ST(uintptr_t, (uint8_t *)&t6 + 8, (uintptr_t)(23));
            uint8_t t7 = 0;
            F.ret = t6;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint32_t t8 = gm_l0;
        gm_t88rcucg1b7uk t9;
        GM_ST(uint32_t, ((uint8_t *)&t9 + 0), t8);
        gm_tabb710d9mq2e t10;
        gm_f_format_mhned9o((void *)&t10, (void *)NULL, cx, NULL, t9);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t10))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t10) + 8, gm_src0gtuh94b1fce, 1250, 12);
        gm_b16_8 t11 = GM_LD(gm_b16_8, (((uint8_t *)&t10) + 8));
        gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_offsets_eh6vfle(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_offsets_eh6vfle F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 112u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = (void *)(((uint8_t *)t1) + 8);
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_tbccvikg6b96e t4;
    gm_f_layout_of_e6hfj1r((void *)&t4, (void *)NULL, cx, NULL, t2, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&t4) + 16));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_param_cty_e8tvhrm(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_param_cty_e8tvhrm F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 149u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_b16_8 t1;
    void * t3 = gm_l0;
    gm_t88rcucg1b7uk t4 = gm_l1;
    uint8_t t5 = gm_f_by_ref_fi0o860((void *)NULL, cx, NULL, t3, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (!t5) goto ie2;
    gm_b16_8 t7;
    GM_ST(void *, (uint8_t *)&t7, (void *)(gm_sqtc9fffut4s2));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(12));
    gm_b16_8 t8 = t7;
    t1 = t8;
    goto ix2;
ie2:;
    void * t10 = gm_l0;
    gm_t88rcucg1b7uk t11 = gm_l1;
    gm_b16_8 t12 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t10, t11);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t13 = t12;
    t1 = t13;
ix2:;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_plain_cty_q9lhlci(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_plain_cty_q9lhlci F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* m */
    GM_STACK_CHECK(&F, 158u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        gm_t88rcucg1b7uk t4 = gm_l1;
        gm_th2lqbo77pvsc t5;
        gm_f_mirror_lcjegsf((void *)&t5, (void *)NULL, cx, NULL, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!((((uint8_t *)&t5))[16] != 0)) goto ie2;
        gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)&t5));
        gm_l2 = t6;
        {
            gm_b16_8 t8 = gm_l2;
            uint8_t t9 = 0;
            F.ret = t8;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t10 = gm_l0;
        gm_t88rcucg1b7uk t11 = gm_l1;
        gm_b16_8 t12 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t10, t11);
        if (gm_unw.kind) {
            goto gm_lad;
        }
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_g2usrvd(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_g2usrvd F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
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
            gm_t1g28mkc78m0c t8;
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
        gm_tehgl6lkqgvt0 t17 = GM_LD(gm_tehgl6lkqgvt0, ((uint8_t *)GM_LD(void *, (((uint8_t *)t15) + 0)) + t14 * 104));
        F.l2 = t17;
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
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 104)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        GM_ST(gm_b16_8, (((uint8_t *)t18) + 0), t28);
    }
    {
        const uint8_t *t29 = ((uint8_t *)&F.l2);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t1g28mkc78m0c *)gm_out) + 105, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t1g28mkc78m0c *)gm_out) + 106, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t1g28mkc78m0c *)gm_out) + 108, 0);
        ((uint8_t *)&(*(gm_t1g28mkc78m0c *)gm_out))[104] = 1;
        memmove((uint8_t *)&(*(gm_t1g28mkc78m0c *)gm_out) + 0, t29, 104);
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_restore_scopes_f62992p(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_restore_scopes_f62992p F;
    void * gm_l0; /* fg */
    GM_STACK_CHECK_SHALLOW(&F, 104u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        gm_trqfvmjtt4sij t3 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)a1) + 0));
        GM_ST(gm_trqfvmjtt4sij, (((uint8_t *)t2) + 240), t3);
    }
    {
        void * t4 = gm_l0;
        gm_trqfvmjtt4sij t5 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)a1) + 40));
        GM_ST(gm_trqfvmjtt4sij, (((uint8_t *)t4) + 368), t5);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_set_aside_scopes_ho12if8(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_set_aside_scopes_ho12if8 F;
    void * gm_l0; /* fg */
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        memcpy((uint8_t *)&F.l1 + 0, (((uint8_t *)t2) + 240), 40);
        void * t3 = gm_l0;
        memcpy((uint8_t *)&F.l1 + 40, (((uint8_t *)t3) + 368), 40);
    }
    {
        void * t4 = gm_l0;
        uint8_t *t5 = (((uint8_t *)t4) + 240);
        gm_f_list_ubba23m((void *)&(*(gm_trqfvmjtt4sij *)t5), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t6 = gm_l0;
        uint8_t *t7 = (((uint8_t *)t6) + 368);
        gm_f_list_ns53od0((void *)&(*(gm_trqfvmjtt4sij *)t7), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        memmove(gm_out, ((uint8_t *)&F.l1), 80);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_size_attrs_abk1l3m(uint32_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_size_attrs_abk1l3m F;
    uint32_t gm_l0; /* names */
    GM_STACK_CHECK_SHALLOW(&F, 105u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint32_t t3 = gm_l0;
        uint32_t t4 = GM_LD(uint32_t, gm_cvpsnk4jni3);
        uint8_t t5 = (t3 > t4);
        if (!t5) goto ie2;
        {
            gm_b16_8 t7;
            GM_ST(void *, (uint8_t *)&t7, (void *)(gm_sp1g56u3ibrtp));
            GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(22));
            uint8_t t8 = 0;
            F.ret = t7;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint32_t t10 = gm_l0;
        uint32_t t11 = GM_LD(uint32_t, gm_ctq6t8frtor);
        uint8_t t12 = (t10 > t11);
        if (!t12) goto ie9;
        {
            gm_b16_8 t14;
            GM_ST(void *, (uint8_t *)&t14, (void *)(gm_suuphrufuvh4m));
            GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(11));
            uint8_t t15 = 0;
            F.ret = t14;
            goto gm_fret;
        }
        goto ix9;
ie9:;
ix9:;
    }
    {
        gm_b16_8 t16;
        GM_ST(void *, (uint8_t *)&t16, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t16 + 8, (uintptr_t)(0));
        uint8_t t17 = 0;
        F.ret = t16;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint64_t gm_f_size_dmhtkib(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_size_dmhtkib F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = (void *)(((uint8_t *)t1) + 8);
    gm_t88rcucg1b7uk t3 = gm_l1;
    uint64_t t4 = gm_f_size_of_k0fluj2((void *)NULL, cx, NULL, t2, t3);
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
inline __attribute__((aligned(4))) uint64_t gm_f_size_of_k0fluj2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_size_of_k0fluj2 F;
    void * gm_l0; /* lay */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 64));
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_tbccvikg6b96e t4;
    gm_f_layout_a5ovnrm((void *)&t4, (void *)NULL, cx, NULL, t2, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)&t4) + 0));
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
inline __attribute__((aligned(4))) void gm_f_stmts_1199hll(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_stmts_1199hll F;
    void * gm_l0; /* fg */
    void * gm_l1; /* b */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = gm_l1;
    gm_b16_8 t3 = GM_LD(gm_b16_8, (((uint8_t *)t2) + 0));
    gm_f_stmt_list_d9ovq7d((void *)&(*(gm_tqgtd5gtgj65s *)gm_out), (void *)NULL, cx, NULL, t1, t3);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_unique_members_mrojmjf(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1) {
    struct gm_fr_unique_members_mrojmjf F;
    gm_b16_8 gm_l0; /* names */
    void * gm_l1; /* taken */
    GM_STACK_CHECK(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_b16_8 t1 = gm_l0;
    void * t2 = gm_l1;
    gm_b16_8 t3 = gm_f_unique_with_mffff7e((void *)NULL, cx, NULL, t1, t2, ((uint8_t)0));
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unit_2age34u(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_unit_2age34u F;
    void * gm_l0; /* t */
    GM_STACK_CHECK(&F, 180u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_t6j6smj7qoctu t2;
    gm_b16_8 t3 = gm_f_empty_slice_rcd2jln();
    gm_b16_8 t4 = GM_LD(gm_b16_8, ((uint8_t *)&t3));
    GM_ST(gm_b16_8, (uint8_t *)&t2 + 0, t4);
    gm_b16_8 t5 = gm_f_empty_slice_5nn7ph0();
    gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)&t5));
    GM_ST(gm_b16_8, (uint8_t *)&t2 + 16, t6);
    gm_t53at80pfdus3 t7;
    GM_ST(uint8_t, (uint8_t *)&t7 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t7 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t7 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t7 + 40, 0);
    GM_ST(uint64_t, (uint8_t *)&t7 + 48, 0);
    GM_ST(uint8_t, ((uint8_t *)&t7 + 0), ((uint8_t)12ULL));
    GM_ST(gm_t6j6smj7qoctu, ((uint8_t *)&t7 + 8), t2);
    gm_t88rcucg1b7uk t8 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t1, &t7);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t8;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_unit_8ekedsl(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_unit_8ekedsl F;
    void * gm_l0; /* fg */
    GM_STACK_CHECK(&F, 92u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
    void * t4 = (void *)(((uint8_t *)t3) + 8);
    gm_t88rcucg1b7uk t5 = gm_f_unit_ty_vgceilu((void *)NULL, cx, NULL, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t6 = gm_f_zero_tlk13aa((void *)NULL, cx, NULL, t1, t5);
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unit_ty_vgceilu(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_unit_ty_vgceilu F;
    void * gm_l0; /* ty */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    void * t3 = (void *)(((uint8_t *)t2) + 8);
    gm_t88rcucg1b7uk t4 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t3);
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
inline __attribute__((aligned(4))) void gm_f_unsupported_531q51r(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1) {
    struct gm_fr_unsupported_531q51r F;
    gm_tl874t2atnlh3 gm_l0; /* span */
    gm_b16_8 gm_l1; /* what */
    GM_STACK_CHECK(&F, 128u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        memset((uint8_t *)&(*(gm_t08olq97rscn3 *)gm_out) + 28, 0, 4);
        gm_b16_8 t2 = gm_l1;
        gm_tnpifv20irnph t3;
        GM_ST(gm_b16_8, ((uint8_t *)&t3 + 0), t2);
        gm_tabb710d9mq2e t4;
        gm_f_format_fsfutor((void *)&t4, (void *)NULL, cx, NULL, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t4))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t4) + 8, gm_srcd5arorrf8jkg, 35, 27);
        gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&t4) + 8));
        gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)&t5));
        gm_tl874t2atnlh3 t7 = gm_l0;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t08olq97rscn3 *)gm_out) + 0, t6);
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&(*(gm_t08olq97rscn3 *)gm_out) + 16, t7);
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
inline __attribute__((aligned(4))) void gm_f_assign_addr_rlofoa5(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2) {
    struct gm_fr_assign_addr_rlofoa5 F;
    void * gm_l0; /* fg */
    void * gm_l1; /* place */
    void * gm_l2; /* value */
    gm_b16_8 gm_l3; /* p */
    gm_b16_8 gm_l4; /* n */
    GM_STACK_CHECK(&F, 348u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = gm_l1;
        gm_t5o0tpv5jka2l t4;
        gm_f_place_addr_7ji15ot((void *)&t4, (void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t4))[0] != 0)) {
            gm_t5o0tpv5jka2l t5;
            GM_ST(uint8_t, (uint8_t *)&t5 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t5 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t5 + 4, 0);
            ((uint8_t *)&t5)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t5 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t4) + 8)));
            F.ret = t5;
            goto gm_fret;
        }
        gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)&t4) + 8));
        gm_l3 = t6;
    }
    {
        uint8_t t8 = 0;
        void * t9 = gm_l1;
        uint8_t t10 = gm_f_fixed_address_5usq50j(t9);
        if (!t10) {
            void * t11 = gm_l2;
            uint8_t t12 = gm_f_may_write_702phjq(t11);
            uint8_t t13 = (uint8_t)!t12;
            t8 = t13;
        } else t8 = 1;
        if (!t8) goto ie7;
        {
            gm_b16_8 t15 = gm_l3;
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
            ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t15);
            goto gm_fdone;
        }
        goto ix7;
ie7:;
ix7:;
    }
    {
        void * t16 = gm_l0;
        void * t17 = gm_l0;
        uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)t17) + 304));
        uint64_t r19; bool o20 = __builtin_add_overflow(t18, ((uint64_t)8ULL), &r19);
        if (o20) gm_trap("integer overflow in `+`", gm_srcmok80ho8hh01, 518, 5);
        uint64_t t21 = r19;
        GM_ST(uint64_t, (((uint8_t *)t16) + 304), t21);
    }
    {
        void * t22 = gm_l0;
        gm_b16_8 t23 = gm_f_fresh_tmp_6tadl69((void *)NULL, cx, NULL, t22);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l4 = t23;
    }
    {
        void * t24 = gm_l0;
        gm_t6j6smj7qoctu t25;
        gm_b16_8 t26 = gm_l4;
        GM_ST(gm_b16_8, (uint8_t *)&t25 + 0, t26);
        gm_b16_8 t27 = gm_l3;
        GM_ST(gm_b16_8, (uint8_t *)&t25 + 16, t27);
        gm_b0_1 t28 = gm_f_linef_5n8up5i((void *)NULL, cx, NULL, t24, &t25);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t29 = gm_l4;
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t29);
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
extern __attribute__((aligned(4))) uint8_t gm_f_big_place_raoupin(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) void gm_f_block_31qi85g(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_block_31qi85g F;
    void * gm_l0; /* fg */
    void * gm_l1; /* b */
    gm_th2lqbo77pvsc gm_l3; /* result */
    uint64_t gm_l5; /* k */
    gm_tgtub0v94d5u5 gm_l6; /* d */
    gm_b16_8 gm_l7; /* r */
    GM_STACK_CHECK(&F, 541u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        gm_f_block_scope_e3cgusu((void *)&F.l2, (void *)NULL, cx, NULL, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t3 = gm_l0;
        void * t4 = (void *)(((uint8_t *)t3) + 240);
        gm_tftb1odc739t5 t5 = gm_f_push_pcqb56b(t4, &(*(gm_tehgl6lkqgvt0 *)((uint8_t *)&F.l2)));
        if (((((uint8_t *)&t5))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t5) + 1, gm_srcmok80ho8hh01, 1068, 5);
        gm_b0_1 t6 = GM_LD(gm_b0_1, (((uint8_t *)&t5) + 1));
    }
    {
        gm_th2lqbo77pvsc t7;
        memset(&t7, 0, sizeof t7);
        gm_l3 = t7;
    }
    {
        void * t8 = gm_l0;
        void * t9 = gm_l1;
        void * t10 = (void *)((uint8_t *)&gm_l3);
        gm_tqgtd5gtgj65s t11;
        gm_f_block_inner_83vev92((void *)&t11, (void *)NULL, cx, NULL, t8, t9, t10);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t11))[0] != 0)) {
            gm_t5o0tpv5jka2l t12;
            GM_ST(uint8_t, (uint8_t *)&t12 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t12 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t12 + 4, 0);
            ((uint8_t *)&t12)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t12 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t11) + 8)));
            F.ret = t12;
            goto gm_fret;
        }
        gm_b0_1 t13 = GM_LD(gm_b0_1, (((uint8_t *)&t11) + 8));
    }
    {
        void * t14 = gm_l0;
        void * t15 = (void *)(((uint8_t *)t14) + 240);
        gm_t1g28mkc78m0c t16;
        gm_f_pop_g2usrvd((void *)&t16, t15);
        if (!((((uint8_t *)&t16))[104] != 0)) gm_trap("unwrap of null", gm_srcmok80ho8hh01, 1071, 15);
        F.l4 = (*(gm_tehgl6lkqgvt0 *)(((uint8_t *)&t16) + 0));
    }
    {
        uint64_t t17 = GM_LD(uintptr_t, ((((uint8_t *)&F.l4) + 56) + 0) + 8);
        gm_l5 = t17;
    }
    {
        for (;;) {
            uint64_t t19 = gm_l5;
            uint8_t t20 = (t19 > ((uint64_t)0ULL));
            if (!t20) goto lb18;
            {
                uint64_t t23 = gm_l5;
                uint64_t r24; bool o25 = __builtin_sub_overflow(t23, ((uint64_t)1ULL), &r24);
                if (o25) gm_trap("integer overflow in `-`", gm_srcmok80ho8hh01, 1075, 9);
                uint64_t t26 = r24;
                gm_l5 = t26;
            }
            {
                uint64_t t27 = gm_l5;
                uint64_t t28 = (uintptr_t)t27;
                uint64_t t29 = GM_LD(uintptr_t, ((((uint8_t *)&F.l4) + 56) + 0) + 8);
                if (t28 >= (uintptr_t)(t29)) gm_trap_index(t28, t29, gm_srcmok80ho8hh01, 1076, 14);
                gm_tgtub0v94d5u5 t30 = GM_LD(gm_tgtub0v94d5u5, ((uint8_t *)GM_LD(void *, ((((uint8_t *)&F.l4) + 56) + 0)) + t28 * 16));
                gm_l6 = t30;
            }
            {
                uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)&gm_l6) + 0));
                uint8_t t33 = (uint8_t)!t32;
                if (!t33) goto ie31;
                {
                    void * t35 = gm_l0;
                    gm_th2lqbo77pvsc t36;
                    memset(&t36, 0, sizeof t36);
                    void * t37 = GM_LD(void *, (((uint8_t *)&gm_l6) + 8));
                    gm_b0_1 t38 = gm_f_defer_body_avm7tau((void *)NULL, cx, NULL, t35, &t36, t37, ((uint8_t)0));
                    if (gm_unw.kind) {
                        goto gm_ll21;
                    }
                }
                goto ix31;
ie31:;
ix31:;
            }
lc18:;
        }
gm_ll21:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb18; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc18; }
        }
        goto gm_lad;
lb18:;
    }
    {
        void * t39 = gm_l0;
        gm_b0_1 t40 = gm_f_end_scope_45ar5qv((void *)NULL, cx, NULL, t39, &(*(gm_tehgl6lkqgvt0 *)((uint8_t *)&F.l4)), ((uint8_t)1));
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        if (!((((uint8_t *)&gm_l3))[16] != 0)) goto ie41;
        gm_b16_8 t42 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l3));
        gm_l7 = t42;
        {
            gm_b16_8 t44 = gm_l7;
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
            ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t44);
            goto gm_fdone;
        }
        goto ix41;
ie41:;
ix41:;
    }
    {
        void * t45 = gm_l0;
        gm_b16_8 t46 = gm_f_unit_8ekedsl((void *)NULL, cx, NULL, t45);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t46);
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
inline __attribute__((aligned(4))) void gm_f_block_inner_83vev92(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2) {
    struct gm_fr_block_inner_83vev92 F;
    void * gm_l0; /* fg */
    void * gm_l1; /* b */
    void * gm_l2; /* result */
    void * gm_l3; /* t */
    gm_b16_8 gm_l4; /* v */
    GM_STACK_CHECK(&F, 465u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        {
            void * t3 = gm_l0;
            void * t4 = gm_l1;
            gm_tqgtd5gtgj65s t5;
            gm_f_stmts_1199hll((void *)&t5, (void *)NULL, cx, NULL, t3, t4);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t5))[0] != 0)) {
                gm_tqgtd5gtgj65s t6;
                GM_ST(uint8_t, (uint8_t *)&t6 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t6 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t6 + 4, 0);
                ((uint8_t *)&t6)[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t6 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t5) + 8)));
                F.ret = t6;
                goto gm_fret;
            }
            gm_b0_1 t7 = GM_LD(gm_b0_1, (((uint8_t *)&t5) + 8));
        }
        {
            void * t9 = gm_l1;
            void * t10 = GM_LD(void *, (((uint8_t *)t9) + 16));
            if (!(GM_LD(void *, ((uint8_t *)&t10)) != 0)) goto ie8;
            void * t11 = GM_LD(void *, ((uint8_t *)&t10));
            gm_l3 = t11;
            {
                void * t13 = gm_l0;
                void * t14 = gm_l3;
                gm_tl874t2atnlh3 t15 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t14) + 72));
                gm_b0_1 t16 = gm_f_src_line_d24sajl((void *)NULL, cx, NULL, t13, t15);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t17 = gm_l0;
                void * t18 = gm_l3;
                gm_t5o0tpv5jka2l t19;
                gm_f_expr_porbc9t((void *)&t19, (void *)NULL, cx, NULL, t17, t18);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t19))[0] != 0)) {
                    gm_tqgtd5gtgj65s t20;
                    GM_ST(uint8_t, (uint8_t *)&t20 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t20 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t20 + 4, 0);
                    ((uint8_t *)&t20)[0] = 1;
                    GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t20 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t19) + 8)));
                    F.ret = t20;
                    goto gm_fret;
                }
                gm_b16_8 t21 = GM_LD(gm_b16_8, (((uint8_t *)&t19) + 8));
                gm_l4 = t21;
            }
            {
                void * t23 = gm_l0;
                void * t24 = GM_LD(void *, (((uint8_t *)t23) + 0));
                void * t25 = (void *)(((uint8_t *)t24) + 8);
                void * t26 = gm_l3;
                gm_t88rcucg1b7uk t27 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t26) + 0));
                uint8_t t28 = gm_f_is_never_in5ftma(t25, GM_CONV(gm_csguankko4lmhd, t27));
                if (!t28) goto ie22;
                {
                    void * t30 = gm_l2;
                    uint8_t *t31 = ((uint8_t *)t30);
                    void * t32 = gm_l0;
                    void * t33 = gm_l3;
                    gm_t88rcucg1b7uk t34 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t33) + 0));
                    gm_b16_8 t35 = gm_f_zero_tlk13aa((void *)NULL, cx, NULL, t32, t34);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)t31) + 17, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)t31) + 18, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)t31) + 20, 0);
                    ((uint8_t *)&(*(gm_th2lqbo77pvsc *)t31))[16] = 1;
                    GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)t31) + 0, t35);
                }
                goto ix22;
ie22:;
                {
                    void * t37 = gm_l2;
                    uint8_t *t38 = ((uint8_t *)t37);
                    void * t39 = gm_l0;
                    void * t40 = gm_l3;
                    gm_t88rcucg1b7uk t41 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t40) + 0));
                    gm_b16_8 t42 = gm_l4;
                    gm_b16_8 t43 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t39, t41, t42);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)t38) + 17, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)t38) + 18, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)t38) + 20, 0);
                    ((uint8_t *)&(*(gm_th2lqbo77pvsc *)t38))[16] = 1;
                    GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)t38) + 0, t43);
                }
ix22:;
            }
            goto ix8;
ie8:;
ix8:;
        }
    }
    gm_b0_1 t44;
    memset(&(*(gm_tqgtd5gtgj65s *)gm_out), 0, sizeof (*(gm_tqgtd5gtgj65s *)gm_out));
    ((uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out))[0] = 0;
    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 8, t44);
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
extern __attribute__((aligned(4))) void gm_f_block_scope_e3cgusu(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) uint8_t gm_f_builds_in_place_ae1b90u(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_builds_in_place_ae1b90u F;
    void * gm_l0; /* fg */
    void * gm_l1; /* e */
    gm_tmn7n0n9raou5 gm_l2; /* kind */
    void * gm_l3; /* b */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_trcmmd12cefb1 gm_l6; /* s */
    void * gm_l7; /* t */
    GM_STACK_CHECK(&F, 451u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint8_t t3 = 0;
        void * t4 = gm_l0;
        void * t5 = GM_LD(void *, (((uint8_t *)t4) + 0));
        void * t6 = (void *)(((uint8_t *)t5) + 8);
        void * t7 = gm_l1;
        gm_t88rcucg1b7uk t8 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t7) + 0));
        uint8_t t9 = gm_f_by_ref_fi0o860((void *)NULL, cx, NULL, t6, t8);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t10 = (uint8_t)!t9;
        if (!t10) {
            void * t11 = gm_l0;
            void * t12 = GM_LD(void *, (((uint8_t *)t11) + 0));
            void * t13 = (void *)(((uint8_t *)t12) + 8);
            void * t14 = gm_l1;
            gm_t88rcucg1b7uk t15 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t14) + 0));
            uint8_t t16 = gm_f_is_never_in5ftma(t13, GM_CONV(gm_csguankko4lmhd, t15));
            t3 = t16;
        } else t3 = 1;
        if (!t3) goto ie2;
        {
            uint8_t t18 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t19 = gm_l1;
        uint8_t t28 = GM_LD(uint8_t, ((((uint8_t *)t19) + 8) + 0));
        switch (t28) { case ((uint8_t)0ULL): goto ma26; case ((uint8_t)1ULL): goto ma26; case ((uint8_t)2ULL): goto ma26; case ((uint8_t)3ULL): goto ma26; case ((uint8_t)4ULL): goto ma26; case ((uint8_t)5ULL): goto ma26; case ((uint8_t)6ULL): goto ma26; case ((uint8_t)7ULL): goto ma26; case ((uint8_t)8ULL): goto ma26; case ((uint8_t)9ULL): goto ma26; case ((uint8_t)10ULL): goto ma26; case ((uint8_t)11ULL): goto ma26; case ((uint8_t)12ULL): goto ma26; case ((uint8_t)13ULL): goto ma26; case ((uint8_t)14ULL): goto ma26; case ((uint8_t)15ULL): goto ma26; case ((uint8_t)16ULL): goto ma26; case ((uint8_t)17ULL): goto ma26; case ((uint8_t)18ULL): goto ma26; case ((uint8_t)19ULL): goto ma26; case ((uint8_t)20ULL): goto ma26; case ((uint8_t)21ULL): goto ma26; case ((uint8_t)22ULL): goto ma26; case ((uint8_t)23ULL): goto ma26; case ((uint8_t)24ULL): goto ma26; case ((uint8_t)25ULL): goto ma26; case ((uint8_t)26ULL): goto ma26; case ((uint8_t)27ULL): goto ma26; case ((uint8_t)28ULL): goto ma26; case ((uint8_t)29ULL): goto ma26; case ((uint8_t)30ULL): goto ma26; case ((uint8_t)31ULL): goto ma26; case ((uint8_t)32ULL): goto ma23; case ((uint8_t)33ULL): goto ma26; case ((uint8_t)34ULL): goto ma21; case ((uint8_t)35ULL): goto ma22; case ((uint8_t)36ULL): goto ma21; case ((uint8_t)37ULL): goto ma26; case ((uint8_t)38ULL): goto ma24; case ((uint8_t)39ULL): goto ma24; case ((uint8_t)40ULL): goto ma26; case ((uint8_t)41ULL): goto ma26; case ((uint8_t)42ULL): goto ma26; case ((uint8_t)43ULL): goto ma26; case ((uint8_t)44ULL): goto ma26; case ((uint8_t)45ULL): goto ma26; case ((uint8_t)46ULL): goto ma25; case ((uint8_t)47ULL): goto ma26; case ((uint8_t)48ULL): goto ma26; case ((uint8_t)49ULL): goto ma26; case ((uint8_t)50ULL): goto ma26; case ((uint8_t)51ULL): goto ma26; case ((uint8_t)52ULL): goto ma26; case ((uint8_t)53ULL): goto ma26; case ((uint8_t)54ULL): goto ma26; case ((uint8_t)55ULL): goto ma26; case ((uint8_t)56ULL): goto ma26; case ((uint8_t)57ULL): goto ma26; case ((uint8_t)58ULL): goto ma26; default: goto ma26; }
ma21:;
        uint8_t t32 = GM_LD(uint8_t, ((((uint8_t *)t19) + 8) + 0));
        if (t32 != ((uint8_t)34ULL)) goto pa31;
        goto po30;
pa31:;
        uint8_t t34 = GM_LD(uint8_t, ((((uint8_t *)t19) + 8) + 0));
        if (t34 != ((uint8_t)36ULL)) goto pa33;
        goto po30;
pa33:;
        goto mn29;
po30:;
        uint8_t t35 = 0;
        F.ret = ((uint8_t)1);
        goto gm_fret;
        goto me20;
mn29:;
ma22:;
        uint8_t t37 = GM_LD(uint8_t, ((((uint8_t *)t19) + 8) + 0));
        if (t37 != ((uint8_t)35ULL)) goto mn36;
        void * t38 = gm_l0;
        void * t39 = GM_LD(void *, (((uint8_t *)t38) + 0));
        void * t40 = (void *)(((uint8_t *)t39) + 8);
        void * t41 = gm_l1;
        gm_t88rcucg1b7uk t42 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t41) + 0));
        gm_t57euh612ligr t43;
        gm_f_bits_layout_23rect9((void *)&t43, (void *)NULL, cx, NULL, t40, t42);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t44;
        if (((((uint8_t *)&t43))[24] != 0)) goto mn46;
        t44 = ((uint8_t)1);
        goto me45;
mn46:;
        t44 = ((uint8_t)0);
        goto me45;
mn47:;
        gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 825, 33);
me45:;
        uint8_t t48 = 0;
        F.ret = t44;
        goto gm_fret;
        goto me20;
mn36:;
ma23:;
        uint8_t t50 = GM_LD(uint8_t, ((((uint8_t *)t19) + 8) + 0));
        if (t50 != ((uint8_t)32ULL)) goto mn49;
        gm_tmn7n0n9raou5 t51 = GM_LD(gm_tmn7n0n9raou5, (((((uint8_t *)t19) + 8) + 8) + 0));
        gm_l2 = t51;
        uint8_t t52 = 0;
        uint8_t t53 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)2ULL)));
        if (!t53) {
            uint8_t t54 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)3ULL)));
            t52 = t54;
        } else t52 = 1;
        uint8_t t55 = 0;
        F.ret = t52;
        goto gm_fret;
        goto me20;
mn49:;
ma24:;
        uint8_t t59 = GM_LD(uint8_t, ((((uint8_t *)t19) + 8) + 0));
        if (t59 != ((uint8_t)39ULL)) goto pa58;
        goto po57;
pa58:;
        uint8_t t61 = GM_LD(uint8_t, ((((uint8_t *)t19) + 8) + 0));
        if (t61 != ((uint8_t)38ULL)) goto pa60;
        goto po57;
pa60:;
        goto mn56;
po57:;
        uint8_t t62 = 0;
        F.ret = ((uint8_t)1);
        goto gm_fret;
        goto me20;
mn56:;
ma25:;
        uint8_t t64 = GM_LD(uint8_t, ((((uint8_t *)t19) + 8) + 0));
        if (t64 != ((uint8_t)46ULL)) goto mn63;
        void * t65 = GM_LD(void *, ((((uint8_t *)t19) + 8) + 8));
        gm_l3 = t65;
        {
            {
                void * t68 = gm_l3;
                gm_b16_8 t69 = GM_LD(gm_b16_8, (((uint8_t *)t68) + 0));
                gm_l4 = t69;
            }
            {
                gm_l5 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t71 = gm_l5;
                    uint64_t t72 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    uint8_t t73 = (t71 < t72);
                    if (!t73) goto lb70;
                    {
                        uint64_t t76 = gm_l5;
                        uint64_t t77 = (uintptr_t)t76;
                        uint64_t t78 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                        gm_trcmmd12cefb1 t81 = GM_LD(gm_trcmmd12cefb1, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t77 * 32));
                        gm_l6 = t81;
                        {
                            uint8_t t84;
                            uint8_t t87 = GM_LD(uint8_t, (((uint8_t *)&gm_l6) + 0));
                            if (t87 != ((uint8_t)5ULL)) goto mn86;
                            t84 = ((uint8_t)1);
                            goto me85;
mn86:;
                            t84 = ((uint8_t)0);
                            goto me85;
mn88:;
                            gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 830, 20);
me85:;
                            if (!t84) goto ie83;
                            {
                                uint8_t t90 = 0;
                                F.ret = ((uint8_t)0);
                                goto gm_fret;
                            }
                            goto ix83;
ie83:;
ix83:;
                        }
                        goto me79;
mn80:;
                        gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 829, 13);
me79:;
                    }
lc70:;
                    {
                        uint64_t t92 = gm_l5;
                        uint64_t r93; bool o94 = __builtin_add_overflow(t92, ((uint64_t)1ULL), &r93);
                        uint64_t t95 = r93;
                        gm_l5 = t95;
                    }
                }
lb70:;
            }
        }
        {
            void * t96 = gm_l3;
            void * t97 = GM_LD(void *, (((uint8_t *)t96) + 16));
            void * t98;
            if ((GM_LD(void *, ((uint8_t *)&t97)) != 0)) {
                void * t99 = GM_LD(void *, (((uint8_t *)&t97) + 0));
                t98 = t99;
            } else {
                uint8_t t100 = 0;
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            gm_l7 = t98;
        }
        {
            void * t101 = gm_l0;
            void * t102 = gm_l7;
            uint8_t t103 = gm_f_builds_in_place_ae1b90u((void *)NULL, cx, NULL, t101, t102);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t104 = 0;
            F.ret = t103;
            goto gm_fret;
        }
        goto me20;
mn63:;
ma26:;
        uint8_t t106 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
        goto me20;
mn105:;
ma27:;
        gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 823, 5);
me20:;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_c_abi_wrapper_33vbmt4(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, void * a2) {
    struct gm_fr_c_abi_wrapper_33vbmt4 F;
    void * gm_l0; /* g */
    uint32_t gm_l1; /* f */
    void * gm_l2; /* def */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* i */
    uint32_t gm_l8; /* p */
    gm_t88rcucg1b7uk gm_l9; /* t */
    gm_b16_8 gm_l10; /* call */
    gm_b16_8 gm_l11; /* ret */
    gm_b16_8 gm_l12; /* body */
    gm_b16_8 gm_l13; /* c */
    gm_b16_8 gm_l15; /* out_call */
    gm_b16_8 gm_l16; /* v */
    gm_b16_8 gm_l17; /* m */
    gm_b16_8 gm_l18; /* v */
    gm_b16_8 gm_l19; /* m */
    gm_b16_8 gm_l20; /* sig */
    GM_STACK_CHECK(&F, 2527u);
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
        gm_f_list_epvducg((void *)&F.l4, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            void * t3 = gm_l2;
            gm_b16_8 t4 = GM_LD(gm_b16_8, (((uint8_t *)t3) + 32));
            gm_l5 = t4;
        }
        {
            gm_l6 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t6 = gm_l6;
                uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                uint8_t t8 = (t6 < t7);
                if (!t8) goto lb5;
                {
                    uint64_t t11 = gm_l6;
                    uint64_t t12 = gm_l6;
                    uint64_t t13 = (uintptr_t)t12;
                    uint64_t t14 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                    uint32_t t15 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t13 * 4));
                    gm_tn68el8d14vku t16;
                    GM_ST(uint32_t, (uint8_t *)&t16 + 12, 0);
                    GM_ST(uint64_t, ((uint8_t *)&t16 + 0), t11);
                    GM_ST(uint32_t, ((uint8_t *)&t16 + 8), t15);
                    uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)&t16) + 0));
                    gm_l7 = t19;
                    uint32_t t20 = GM_LD(uint32_t, (((uint8_t *)&t16) + 8));
                    gm_l8 = t20;
                    {
                        uint32_t t22 = gm_l8;
                        uint64_t t23 = (uint64_t)t22;
                        uint64_t t24 = (uintptr_t)t23;
                        void * t25 = gm_l2;
                        uint64_t t26 = GM_LD(uintptr_t, (((uint8_t *)t25) + 48) + 8);
                        if (t24 >= (uintptr_t)(t26)) gm_trap_index(t24, t26, gm_srcmok80ho8hh01, 369, 14);
                        gm_t88rcucg1b7uk t27 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, (((uint8_t *)t25) + 48)) + t24 * 40) + 16));
                        gm_l9 = t27;
                    }
                    {
                        void * t28 = (void *)((uint8_t *)&F.l3);
                        gm_t3877pk7lv748 t29;
                        void * t30 = gm_l0;
                        void * t31 = (void *)(((uint8_t *)t30) + 8);
                        gm_t88rcucg1b7uk t32 = gm_l9;
                        gm_b16_8 t33 = gm_f_plain_cty_q9lhlci((void *)NULL, cx, NULL, t31, t32);
                        if (gm_unw.kind) {
                            goto gm_ll9;
                        }
                        GM_ST(gm_b16_8, (uint8_t *)&t29 + 0, t33);
                        uint64_t t34 = gm_l7;
                        GM_ST(uint64_t, (uint8_t *)&t29 + 16, t34);
                        gm_tabb710d9mq2e t35;
                        gm_f_format_eq87866((void *)&t35, (void *)NULL, cx, NULL, &t29);
                        if (gm_unw.kind) {
                            goto gm_ll9;
                        }
                        if (((((uint8_t *)&t35))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t35) + 8, gm_srcmok80ho8hh01, 370, 21);
                        gm_b16_8 t36 = GM_LD(gm_b16_8, (((uint8_t *)&t35) + 8));
                        gm_b16_8 t37 = GM_LD(gm_b16_8, ((uint8_t *)&t36));
                        gm_tftb1odc739t5 t38 = gm_f_push_3ef746b(t28, t37);
                        if (((((uint8_t *)&t38))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t38) + 1, gm_srcmok80ho8hh01, 370, 9);
                        gm_b0_1 t39 = GM_LD(gm_b0_1, (((uint8_t *)&t38) + 1));
                    }
                    {
                        void * t41 = gm_l0;
                        void * t42 = (void *)(((uint8_t *)t41) + 8);
                        gm_t88rcucg1b7uk t43 = gm_l9;
                        uint8_t t44 = gm_f_by_ref_fi0o860((void *)NULL, cx, NULL, t42, t43);
                        if (gm_unw.kind) {
                            goto gm_ll9;
                        }
                        if (!t44) goto ie40;
                        {
                            void * t46 = (void *)((uint8_t *)&F.l4);
                            uint64_t t47 = gm_l7;
                            gm_t4ce0jd3tdro4 t48;
                            GM_ST(uint64_t, ((uint8_t *)&t48 + 0), t47);
                            gm_tabb710d9mq2e t49;
                            gm_f_format_ddrb17v((void *)&t49, (void *)NULL, cx, NULL, t48);
                            if (gm_unw.kind) {
                                goto gm_ll9;
                            }
                            if (((((uint8_t *)&t49))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t49) + 8, gm_srcmok80ho8hh01, 371, 39);
                            gm_b16_8 t50 = GM_LD(gm_b16_8, (((uint8_t *)&t49) + 8));
                            gm_b16_8 t51 = GM_LD(gm_b16_8, ((uint8_t *)&t50));
                            gm_tftb1odc739t5 t52 = gm_f_push_3ef746b(t46, t51);
                            if (((((uint8_t *)&t52))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t52) + 1, gm_srcmok80ho8hh01, 371, 29);
                            gm_b0_1 t53 = GM_LD(gm_b0_1, (((uint8_t *)&t52) + 1));
                        }
                        goto ix40;
ie40:;
                        {
                            void * t55 = (void *)((uint8_t *)&F.l4);
                            uint64_t t56 = gm_l7;
                            gm_t4ce0jd3tdro4 t57;
                            GM_ST(uint64_t, ((uint8_t *)&t57 + 0), t56);
                            gm_tabb710d9mq2e t58;
                            gm_f_format_l05am3t((void *)&t58, (void *)NULL, cx, NULL, t57);
                            if (gm_unw.kind) {
                                goto gm_ll9;
                            }
                            if (((((uint8_t *)&t58))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t58) + 8, gm_srcmok80ho8hh01, 371, 83);
                            gm_b16_8 t59 = GM_LD(gm_b16_8, (((uint8_t *)&t58) + 8));
                            gm_b16_8 t60 = GM_LD(gm_b16_8, ((uint8_t *)&t59));
                            gm_tftb1odc739t5 t61 = gm_f_push_3ef746b(t55, t60);
                            if (((((uint8_t *)&t61))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t61) + 1, gm_srcmok80ho8hh01, 371, 73);
                            gm_b0_1 t62 = GM_LD(gm_b0_1, (((uint8_t *)&t61) + 1));
                        }
ix40:;
                    }
                    goto me17;
mn18:;
                    gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 368, 5);
me17:;
                }
lc5:;
                {
                    uint64_t t64 = gm_l6;
                    uint64_t r65; bool o66 = __builtin_add_overflow(t64, ((uint64_t)1ULL), &r65);
                    uint64_t t67 = r65;
                    gm_l6 = t67;
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
        uint64_t t69 = GM_LD(uintptr_t, (((uint8_t *)&F.l3) + 0) + 8);
        uint8_t t70 = ((t69 == ((uint64_t)0ULL)));
        if (!t70) goto ie68;
        {
            void * t72 = (void *)((uint8_t *)&F.l3);
            gm_b16_8 t73;
            GM_ST(void *, (uint8_t *)&t73, (void *)(gm_sts5j6f5pkov0));
            GM_ST(uintptr_t, (uint8_t *)&t73 + 8, (uintptr_t)(4));
            gm_tftb1odc739t5 t74 = gm_f_push_3ef746b(t72, t73);
            if (((((uint8_t *)&t74))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t74) + 1, gm_srcmok80ho8hh01, 373, 32);
            gm_b0_1 t75 = GM_LD(gm_b0_1, (((uint8_t *)&t74) + 1));
        }
        goto ix68;
ie68:;
ix68:;
    }
    {
        gm_t6j6smj7qoctu t76;
        void * t77 = gm_l0;
        uint32_t t78 = gm_l1;
        gm_b16_8 t79 = gm_f_fn_name_6n34573((void *)NULL, cx, NULL, t77, t78);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t76 + 0, t79);
        gm_b16_8 t80 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 0));
        gm_b16_8 t81 = GM_LD(gm_b16_8, ((uint8_t *)&t80));
        gm_b16_8 t82;
        GM_ST(void *, (uint8_t *)&t82, (void *)(gm_sg5gdnrith2i6));
        GM_ST(uintptr_t, (uint8_t *)&t82 + 8, (uintptr_t)(2));
        gm_tabb710d9mq2e t83;
        gm_f_join_sfknljb((void *)&t83, (void *)NULL, cx, NULL, t81, t82);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t83))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t83) + 8, gm_srcmok80ho8hh01, 374, 48);
        gm_b16_8 t84 = GM_LD(gm_b16_8, (((uint8_t *)&t83) + 8));
        GM_ST(gm_b16_8, (uint8_t *)&t76 + 16, t84);
        gm_tabb710d9mq2e t85;
        gm_f_format_99gc605((void *)&t85, (void *)NULL, cx, NULL, &t76);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t85))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t85) + 8, gm_srcmok80ho8hh01, 374, 13);
        gm_b16_8 t86 = GM_LD(gm_b16_8, (((uint8_t *)&t85) + 8));
        gm_l10 = t86;
    }
    {
        gm_b16_8 t87;
        GM_ST(void *, (uint8_t *)&t87, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t87 + 8, (uintptr_t)(0));
        gm_l11 = t87;
    }
    {
        gm_b16_8 t88;
        GM_ST(void *, (uint8_t *)&t88, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t88 + 8, (uintptr_t)(0));
        gm_l12 = t88;
    }
    {
        void * t90 = gm_l0;
        void * t91 = (void *)(((uint8_t *)t90) + 8);
        void * t92 = gm_l2;
        gm_t88rcucg1b7uk t93 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t92) + 64));
        uint8_t t94 = gm_f_is_unit_17uennd(t91, GM_CONV(gm_csguankko4lmhd, t93));
        if (!t94) goto ie89;
        {
            gm_b16_8 t96;
            GM_ST(void *, (uint8_t *)&t96, (void *)(gm_sts5j6f5pkov0));
            GM_ST(uintptr_t, (uint8_t *)&t96 + 8, (uintptr_t)(4));
            gm_l11 = t96;
        }
        {
            gm_b16_8 t97 = gm_l10;
            gm_tnpifv20irnph t98;
            GM_ST(gm_b16_8, ((uint8_t *)&t98 + 0), t97);
            gm_tabb710d9mq2e t99;
            gm_f_format_lsmcccn((void *)&t99, (void *)NULL, cx, NULL, t98);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t99))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t99) + 8, gm_srcmok80ho8hh01, 379, 16);
            gm_b16_8 t100 = GM_LD(gm_b16_8, (((uint8_t *)&t99) + 8));
            gm_b16_8 t101 = GM_LD(gm_b16_8, ((uint8_t *)&t100));
            gm_l12 = t101;
        }
        goto ix89;
ie89:;
        void * t103 = gm_l0;
        void * t104 = (void *)(((uint8_t *)t103) + 8);
        void * t105 = gm_l2;
        gm_t88rcucg1b7uk t106 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t105) + 64));
        uint8_t t107 = gm_f_by_ref_fi0o860((void *)NULL, cx, NULL, t104, t106);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t107) goto ie102;
        {
            void * t109 = gm_l0;
            void * t110 = (void *)(((uint8_t *)t109) + 8);
            void * t111 = gm_l2;
            gm_t88rcucg1b7uk t112 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t111) + 64));
            gm_b16_8 t113 = gm_f_plain_cty_q9lhlci((void *)NULL, cx, NULL, t110, t112);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l11 = t113;
        }
        {
            void * t114 = gm_l0;
            void * t115 = (void *)(((uint8_t *)t114) + 8);
            void * t116 = gm_l2;
            gm_t88rcucg1b7uk t117 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t116) + 64));
            gm_b16_8 t118 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t115, t117);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l13 = t118;
        }
        {
            gm_f_list_epvducg((void *)&F.l14, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t119 = (void *)((uint8_t *)&F.l14);
            gm_b16_8 t120;
            GM_ST(void *, (uint8_t *)&t120, (void *)(gm_s2254b9hkee3b));
            GM_ST(uintptr_t, (uint8_t *)&t120 + 8, (uintptr_t)(5));
            gm_tftb1odc739t5 t121 = gm_f_push_3ef746b(t119, t120);
            if (((((uint8_t *)&t121))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t121) + 1, gm_srcmok80ho8hh01, 384, 9);
            gm_b0_1 t122 = GM_LD(gm_b0_1, (((uint8_t *)&t121) + 1));
        }
        {
            void * t123 = (void *)((uint8_t *)&F.l14);
            gm_b16_8 t124 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 0));
            gm_b16_8 t125 = GM_LD(gm_b16_8, ((uint8_t *)&t124));
            gm_tftb1odc739t5 t126 = gm_f_extend_vogsd72(t123, t125);
            if (((((uint8_t *)&t126))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t126) + 1, gm_srcmok80ho8hh01, 385, 9);
            gm_b0_1 t127 = GM_LD(gm_b0_1, (((uint8_t *)&t126) + 1));
        }
        {
            gm_t6j6smj7qoctu t128;
            void * t129 = gm_l0;
            uint32_t t130 = gm_l1;
            gm_b16_8 t131 = gm_f_fn_name_6n34573((void *)NULL, cx, NULL, t129, t130);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t128 + 0, t131);
            gm_b16_8 t132 = GM_LD(gm_b16_8, (((uint8_t *)&F.l14) + 0));
            gm_b16_8 t133 = GM_LD(gm_b16_8, ((uint8_t *)&t132));
            gm_b16_8 t134;
            GM_ST(void *, (uint8_t *)&t134, (void *)(gm_sg5gdnrith2i6));
            GM_ST(uintptr_t, (uint8_t *)&t134 + 8, (uintptr_t)(2));
            gm_tabb710d9mq2e t135;
            gm_f_join_sfknljb((void *)&t135, (void *)NULL, cx, NULL, t133, t134);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t135))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t135) + 8, gm_srcmok80ho8hh01, 386, 56);
            gm_b16_8 t136 = GM_LD(gm_b16_8, (((uint8_t *)&t135) + 8));
            GM_ST(gm_b16_8, (uint8_t *)&t128 + 16, t136);
            gm_tabb710d9mq2e t137;
            gm_f_format_99gc605((void *)&t137, (void *)NULL, cx, NULL, &t128);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t137))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t137) + 8, gm_srcmok80ho8hh01, 386, 21);
            gm_b16_8 t138 = GM_LD(gm_b16_8, (((uint8_t *)&t137) + 8));
            gm_l15 = t138;
        }
        {
            gm_b16_8 t139;
            GM_ST(void *, (uint8_t *)&t139, (void *)(gm_ss03naco6s5l7));
            GM_ST(uintptr_t, (uint8_t *)&t139 + 8, (uintptr_t)(4));
            gm_l16 = t139;
        }
        {
            void * t141 = gm_l0;
            void * t142 = (void *)(((uint8_t *)t141) + 8);
            void * t143 = gm_l2;
            gm_t88rcucg1b7uk t144 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t143) + 64));
            gm_th2lqbo77pvsc t145;
            gm_f_mirror_lcjegsf((void *)&t145, (void *)NULL, cx, NULL, t142, t144);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!((((uint8_t *)&t145))[16] != 0)) goto ie140;
            gm_b16_8 t146 = GM_LD(gm_b16_8, ((uint8_t *)&t145));
            gm_l17 = t146;
            {
                gm_b16_8 t148 = gm_l17;
                gm_tnpifv20irnph t149;
                GM_ST(gm_b16_8, ((uint8_t *)&t149 + 0), t148);
                gm_tabb710d9mq2e t150;
                gm_f_format_cp0hre9((void *)&t150, (void *)NULL, cx, NULL, t149);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t150))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t150) + 8, gm_srcmok80ho8hh01, 388, 44);
                gm_b16_8 t151 = GM_LD(gm_b16_8, (((uint8_t *)&t150) + 8));
                gm_b16_8 t152 = GM_LD(gm_b16_8, ((uint8_t *)&t151));
                gm_l16 = t152;
            }
            goto ix140;
ie140:;
ix140:;
        }
        {
            gm_tfeab279c16rr t153;
            gm_b16_8 t154 = gm_l13;
            GM_ST(gm_b16_8, (uint8_t *)&t153 + 0, t154);
            gm_b16_8 t155 = gm_l15;
            GM_ST(gm_b16_8, (uint8_t *)&t153 + 16, t155);
            gm_b16_8 t156 = gm_l16;
            GM_ST(gm_b16_8, (uint8_t *)&t153 + 32, t156);
            gm_tabb710d9mq2e t157;
            gm_f_format_u9etquj((void *)&t157, (void *)NULL, cx, NULL, &t153);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t157))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t157) + 8, gm_srcmok80ho8hh01, 389, 16);
            gm_b16_8 t158 = GM_LD(gm_b16_8, (((uint8_t *)&t157) + 8));
            gm_b16_8 t159 = GM_LD(gm_b16_8, ((uint8_t *)&t158));
            gm_l12 = t159;
        }
        goto ix102;
ie102:;
        {
            void * t161 = gm_l0;
            void * t162 = (void *)(((uint8_t *)t161) + 8);
            void * t163 = gm_l2;
            gm_t88rcucg1b7uk t164 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t163) + 64));
            gm_b16_8 t165 = gm_f_plain_cty_q9lhlci((void *)NULL, cx, NULL, t162, t164);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l11 = t165;
        }
        {
            gm_b16_8 t166 = gm_l10;
            gm_l18 = t166;
        }
        {
            void * t168 = gm_l0;
            void * t169 = (void *)(((uint8_t *)t168) + 8);
            void * t170 = gm_l2;
            gm_t88rcucg1b7uk t171 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t170) + 64));
            gm_th2lqbo77pvsc t172;
            gm_f_mirror_lcjegsf((void *)&t172, (void *)NULL, cx, NULL, t169, t171);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!((((uint8_t *)&t172))[16] != 0)) goto ie167;
            gm_b16_8 t173 = GM_LD(gm_b16_8, ((uint8_t *)&t172));
            gm_l19 = t173;
            {
                void * t176 = gm_l0;
                void * t177 = (void *)(((uint8_t *)t176) + 8);
                void * t178 = gm_l2;
                gm_t88rcucg1b7uk t179 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t178) + 64));
                uint8_t t180 = gm_f_by_ref_fi0o860((void *)NULL, cx, NULL, t177, t179);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (!t180) goto ie175;
                {
                    gm_t6j6smj7qoctu t182;
                    gm_b16_8 t183 = gm_l19;
                    GM_ST(gm_b16_8, (uint8_t *)&t182 + 0, t183);
                    gm_b16_8 t184 = gm_l10;
                    GM_ST(gm_b16_8, (uint8_t *)&t182 + 16, t184);
                    gm_tabb710d9mq2e t185;
                    gm_f_format_4hn970h((void *)&t185, (void *)NULL, cx, NULL, &t182);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t185))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t185) + 8, gm_srcmok80ho8hh01, 394, 43);
                    gm_b16_8 t186 = GM_LD(gm_b16_8, (((uint8_t *)&t185) + 8));
                    gm_l18 = t186;
                }
                goto ix175;
ie175:;
ix175:;
            }
            goto ix167;
ie167:;
ix167:;
        }
        {
            gm_b16_8 t187 = gm_l18;
            gm_tnpifv20irnph t188;
            GM_ST(gm_b16_8, ((uint8_t *)&t188 + 0), t187);
            gm_tabb710d9mq2e t189;
            gm_f_format_ko1kpil((void *)&t189, (void *)NULL, cx, NULL, t188);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t189))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t189) + 8, gm_srcmok80ho8hh01, 396, 16);
            gm_b16_8 t190 = GM_LD(gm_b16_8, (((uint8_t *)&t189) + 8));
            gm_b16_8 t191 = GM_LD(gm_b16_8, ((uint8_t *)&t190));
            gm_l12 = t191;
        }
ix102:;
ix89:;
    }
    {
        gm_t0uuhaljsf410 t192;
        void * t193 = gm_l2;
        uint8_t t194 = GM_LD(uint8_t, (((uint8_t *)t193) + 144));
        gm_b16_8 t195 = gm_f_conv_attr_m0b89pc(t194);
        GM_ST(gm_b16_8, (uint8_t *)&t192 + 0, t195);
        gm_b16_8 t196 = gm_l11;
        GM_ST(gm_b16_8, (uint8_t *)&t192 + 16, t196);
        void * t197 = gm_l0;
        uint32_t t198 = gm_l1;
        gm_b16_8 t199 = gm_f_fn_name_6n34573((void *)NULL, cx, NULL, t197, t198);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t192 + 32, t199);
        gm_b16_8 t200 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
        gm_b16_8 t201 = GM_LD(gm_b16_8, ((uint8_t *)&t200));
        gm_b16_8 t202;
        GM_ST(void *, (uint8_t *)&t202, (void *)(gm_sg5gdnrith2i6));
        GM_ST(uintptr_t, (uint8_t *)&t202 + 8, (uintptr_t)(2));
        gm_tabb710d9mq2e t203;
        gm_f_join_sfknljb((void *)&t203, (void *)NULL, cx, NULL, t201, t202);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t203))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t203) + 8, gm_srcmok80ho8hh01, 398, 116);
        gm_b16_8 t204 = GM_LD(gm_b16_8, (((uint8_t *)&t203) + 8));
        GM_ST(gm_b16_8, (uint8_t *)&t192 + 48, t204);
        gm_tabb710d9mq2e t205;
        gm_f_format_kadoe0b((void *)&t205, (void *)NULL, cx, NULL, &t192);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t205))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t205) + 8, gm_srcmok80ho8hh01, 398, 12);
        gm_b16_8 t206 = GM_LD(gm_b16_8, (((uint8_t *)&t205) + 8));
        gm_l20 = t206;
    }
    {
        void * t207 = gm_l0;
        void * t208 = (void *)(((uint8_t *)t207) + 1888);
        gm_b16_8 t209 = gm_l20;
        gm_tnpifv20irnph t210;
        GM_ST(gm_b16_8, ((uint8_t *)&t210 + 0), t209);
        gm_tftb1odc739t5 t211 = gm_f_write_i2vvij8(t208, t210);
        if (((((uint8_t *)&t211))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t211) + 1, gm_srcmok80ho8hh01, 399, 5);
        gm_b0_1 t212 = GM_LD(gm_b0_1, (((uint8_t *)&t211) + 1));
    }
    {
        void * t213 = gm_l0;
        void * t214 = (void *)(((uint8_t *)t213) + 1928);
        gm_t6j6smj7qoctu t215;
        gm_b16_8 t216 = gm_l20;
        GM_ST(gm_b16_8, (uint8_t *)&t215 + 0, t216);
        gm_b16_8 t217 = gm_l12;
        GM_ST(gm_b16_8, (uint8_t *)&t215 + 16, t217);
        gm_tftb1odc739t5 t218 = gm_f_write_ildv1bj(t214, &t215);
        if (((((uint8_t *)&t218))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t218) + 1, gm_srcmok80ho8hh01, 400, 5);
        gm_b0_1 t219 = GM_LD(gm_b0_1, (((uint8_t *)&t218) + 1));
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
__attribute__((aligned(4))) uint32_t gm_f_call_depth_4m6ap7n(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_call_depth_4m6ap7n F;
    void * gm_l0; /* g */
    uint32_t gm_l1; /* f */
    uint64_t gm_l2; /* i */
    uint8_t gm_l3; /* known */
    uint32_t gm_l4; /* depth */
    void * gm_l5; /* def */
    void * gm_l6; /* b */
    GM_STACK_CHECK(&F, 340u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint32_t t2 = gm_l1;
        uint64_t t3 = (uint64_t)t2;
        gm_l2 = t3;
    }
    {
        for (;;) {
            void * t5 = gm_l0;
            uint64_t t6 = GM_LD(uintptr_t, ((((uint8_t *)t5) + 3040) + 0) + 8);
            uint64_t t7 = gm_l2;
            uint8_t t8 = (t6 <= t7);
            if (!t8) goto lb4;
            {
                void * t11 = gm_l0;
                void * t12 = (void *)(((uint8_t *)t11) + 3040);
                gm_tftb1odc739t5 t13 = gm_f_push_t83rg2m(t12, ((uint8_t)0ULL));
                if (((((uint8_t *)&t13))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t13) + 1, gm_srcmok80ho8hh01, 305, 41);
                gm_b0_1 t14 = GM_LD(gm_b0_1, (((uint8_t *)&t13) + 1));
            }
lc4:;
        }
lb4:;
    }
    {
        uint64_t t15 = gm_l2;
        uint64_t t16 = (uintptr_t)t15;
        void * t17 = gm_l0;
        uint64_t t18 = GM_LD(uintptr_t, ((((uint8_t *)t17) + 3040) + 0) + 8);
        if (t16 >= (uintptr_t)(t18)) gm_trap_index(t16, t18, gm_srcmok80ho8hh01, 306, 14);
        uint8_t t19 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t17) + 3040) + 0)) + t16 * 1));
        gm_l3 = t19;
    }
    {
        uint8_t t21 = gm_l3;
        uint8_t t22 = ((t21 == ((uint8_t)1ULL)));
        if (!t22) goto ie20;
        {
            uint8_t t24 = GM_LD(uint8_t, gm_c71idar1l1k);
            uint32_t t25 = (uint32_t)t24;
            uint8_t t26 = 0;
            F.ret = t25;
            goto gm_fret;
        }
        goto ix20;
ie20:;
ix20:;
    }
    {
        uint8_t t28 = gm_l3;
        uint8_t t29 = (t28 > ((uint8_t)1ULL));
        if (!t29) goto ie27;
        {
            uint8_t t31 = gm_l3;
            uint32_t t32 = (uint32_t)t31;
            uint32_t r33; bool o34 = __builtin_sub_overflow(t32, ((uint32_t)2ULL), &r33);
            if (o34) gm_trap("integer overflow in `-`", gm_srcmok80ho8hh01, 309, 27);
            uint32_t t35 = r33;
            uint8_t t36 = 0;
            F.ret = t35;
            goto gm_fret;
        }
        goto ix27;
ie27:;
ix27:;
    }
    {
        uint64_t t37 = gm_l2;
        uint64_t t38 = (uintptr_t)t37;
        void * t39 = gm_l0;
        uint64_t t40 = GM_LD(uintptr_t, ((((uint8_t *)t39) + 3040) + 0) + 8);
        if (t38 >= (uintptr_t)(t40)) gm_trap_index(t38, t40, gm_srcmok80ho8hh01, 310, 5);
        GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t39) + 3040) + 0)) + t38 * 1), ((uint8_t)1ULL));
    }
    {
        uint8_t t41 = GM_LD(uint8_t, gm_c71idar1l1k);
        uint32_t t42 = (uint32_t)t41;
        gm_l4 = t42;
    }
    {
        void * t43 = gm_l0;
        void * t44 = GM_LD(void *, (((uint8_t *)t43) + 0));
        uint32_t t45 = gm_l1;
        void * t46 = gm_f_func_in17r5n((void *)NULL, cx, NULL, t44, t45);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l5 = t46;
    }
    {
        void * t48 = gm_l5;
        void * t49 = GM_LD(void *, (((uint8_t *)t48) + 152));
        if (!(GM_LD(void *, ((uint8_t *)&t49)) != 0)) goto ie47;
        void * t50 = GM_LD(void *, ((uint8_t *)&t49));
        gm_l6 = t50;
        void * t51 = gm_l5;
        uint8_t t52;
        uint8_t t55 = GM_LD(uint8_t, ((((uint8_t *)t51) + 96) + 0));
        if (t55 != ((uint8_t)3ULL)) goto mn54;
        t52 = ((uint8_t)0);
        goto me53;
mn54:;
        t52 = ((uint8_t)1);
        goto me53;
mn56:;
        gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 313, 26);
me53:;
        if (!t52) goto ie47;
        {
            void * t58 = gm_l0;
            void * t59 = gm_l6;
            uint32_t t60 = gm_f_expr_call_depth_dliqc7u((void *)NULL, cx, NULL, t58, t59);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l4 = t60;
        }
        goto ix47;
ie47:;
ix47:;
    }
    {
        uint64_t t61 = gm_l2;
        uint64_t t62 = (uintptr_t)t61;
        void * t63 = gm_l0;
        uint64_t t64 = GM_LD(uintptr_t, ((((uint8_t *)t63) + 3040) + 0) + 8);
        if (t62 >= (uintptr_t)(t64)) gm_trap_index(t62, t64, gm_srcmok80ho8hh01, 314, 5);
        uint8_t *t65 = ((uint8_t *)GM_LD(void *, ((((uint8_t *)t63) + 3040) + 0)) + t62 * 1);
        uint8_t t66;
        uint32_t t68 = gm_l4;
        uint32_t t69 = GM_LD(uint32_t, gm_cdmki9acqi9);
        uint8_t t70 = (t68 > t69);
        if (!t70) goto ie67;
        uint8_t t72 = GM_LD(uint8_t, gm_c71idar1l1k);
        uint8_t t73 = t72;
        t66 = t73;
        goto ix67;
ie67:;
        uint32_t t75 = gm_l4;
        uint8_t t76 = (uint8_t)t75;
        uint8_t r77; bool o78 = __builtin_add_overflow(t76, ((uint8_t)2ULL), &r77);
        if (o78) gm_trap("integer overflow in `+`", gm_srcmok80ho8hh01, 314, 79);
        uint8_t t79 = r77;
        uint8_t t80 = t79;
        t66 = t80;
ix67:;
        GM_ST(uint8_t, t65, t66);
    }
    {
        uint32_t t81 = gm_l4;
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
inline __attribute__((aligned(4))) uint8_t gm_f_calls_probe_2ia3rhf(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_calls_probe_2ia3rhf F;
    void * gm_l1; /* e */
    uint32_t gm_l3; /* f */
    uint8_t gm_l4; /* x */
    GM_STACK_CHECK(&F, 140u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l1;
        uint8_t t8 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        switch (t8) { case ((uint8_t)0ULL): goto ma6; case ((uint8_t)1ULL): goto ma6; case ((uint8_t)2ULL): goto ma6; case ((uint8_t)3ULL): goto ma6; case ((uint8_t)4ULL): goto ma6; case ((uint8_t)5ULL): goto ma6; case ((uint8_t)6ULL): goto ma6; case ((uint8_t)7ULL): goto ma6; case ((uint8_t)8ULL): goto ma6; case ((uint8_t)9ULL): goto ma6; case ((uint8_t)10ULL): goto ma6; case ((uint8_t)11ULL): goto ma6; case ((uint8_t)12ULL): goto ma6; case ((uint8_t)13ULL): goto ma6; case ((uint8_t)14ULL): goto ma6; case ((uint8_t)15ULL): goto ma6; case ((uint8_t)16ULL): goto ma6; case ((uint8_t)17ULL): goto ma6; case ((uint8_t)18ULL): goto ma6; case ((uint8_t)19ULL): goto ma6; case ((uint8_t)20ULL): goto ma6; case ((uint8_t)21ULL): goto ma6; case ((uint8_t)22ULL): goto ma6; case ((uint8_t)23ULL): goto ma6; case ((uint8_t)24ULL): goto ma6; case ((uint8_t)25ULL): goto ma6; case ((uint8_t)26ULL): goto ma6; case ((uint8_t)27ULL): goto ma6; case ((uint8_t)28ULL): goto ma6; case ((uint8_t)29ULL): goto ma6; case ((uint8_t)30ULL): goto ma6; case ((uint8_t)31ULL): goto ma6; case ((uint8_t)32ULL): goto ma6; case ((uint8_t)33ULL): goto ma6; case ((uint8_t)34ULL): goto ma4; case ((uint8_t)35ULL): goto ma6; case ((uint8_t)36ULL): goto ma6; case ((uint8_t)37ULL): goto ma6; case ((uint8_t)38ULL): goto ma6; case ((uint8_t)39ULL): goto ma6; case ((uint8_t)40ULL): goto ma6; case ((uint8_t)41ULL): goto ma6; case ((uint8_t)42ULL): goto ma6; case ((uint8_t)43ULL): goto ma6; case ((uint8_t)44ULL): goto ma6; case ((uint8_t)45ULL): goto ma6; case ((uint8_t)46ULL): goto ma6; case ((uint8_t)47ULL): goto ma6; case ((uint8_t)48ULL): goto ma6; case ((uint8_t)49ULL): goto ma6; case ((uint8_t)50ULL): goto ma6; case ((uint8_t)51ULL): goto ma6; case ((uint8_t)52ULL): goto ma6; case ((uint8_t)53ULL): goto ma5; case ((uint8_t)54ULL): goto ma6; case ((uint8_t)55ULL): goto ma5; case ((uint8_t)56ULL): goto ma6; case ((uint8_t)57ULL): goto ma6; case ((uint8_t)58ULL): goto ma5; default: goto ma6; }
ma4:;
        uint8_t t10 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t10 != ((uint8_t)34ULL)) goto mn9;
        gm_tsth9qu1dv0g3 t11 = GM_LD(gm_tsth9qu1dv0g3, (((((uint8_t *)t2) + 8) + 8) + 0));
        F.l2 = t11;
        uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
        if (t14 != ((uint8_t)0ULL)) goto mn13;
        uint32_t t15 = GM_LD(uint32_t, (((uint8_t *)&F.l2) + 8));
        gm_l3 = t15;
        {
            void * t18 = F.l0;
            void * t19 = GM_LD(void *, (((uint8_t *)t18) + 0));
            uint32_t t20 = gm_l3;
            void * t21 = gm_f_func_in17r5n((void *)NULL, cx, NULL, t19, t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!((((((uint8_t *)t21) + 145) + 1))[1] != 0)) goto ie17;
            uint8_t t22 = GM_LD(uint8_t, ((((uint8_t *)t21) + 145) + 1));
            gm_l4 = t22;
            uint8_t t23 = gm_l4;
            if (!t23) goto ie17;
            {
                uint8_t t25 = 0;
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            goto ix17;
ie17:;
ix17:;
        }
        goto me12;
mn13:;
        uint8_t t27 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
        goto me12;
mn26:;
        gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 339, 29);
me12:;
        goto me3;
mn9:;
ma5:;
        uint8_t t31 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t31 != ((uint8_t)55ULL)) goto pa30;
        goto po29;
pa30:;
        uint8_t t33 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t33 != ((uint8_t)53ULL)) goto pa32;
        goto po29;
pa32:;
        uint8_t t35 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t35 != ((uint8_t)58ULL)) goto pa34;
        goto po29;
pa34:;
        goto mn28;
po29:;
        uint8_t t36 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
        goto me3;
mn28:;
ma6:;
        goto me3;
mn37:;
ma7:;
        gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 338, 5);
me3:;
    }
    {
        F.l5 = ((uint8_t)1);
    }
    {
        void * t39 = gm_l1;
        gm_closure t40 = ((gm_closure){(void *)gm_f_fn_5evrfa7, (void *)&F.h, cx});
        gm_f_for_each_child_fm49s73(t39, t40);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint8_t t41 = F.l5;
        uint8_t t42 = 0;
        F.ret = t41;
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
inline __attribute__((aligned(4))) void gm_f_captured_locals_glsvloj(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_captured_locals_glsvloj F;
    void * gm_l0; /* g */
    uint64_t gm_l3; /* n */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* i */
    void * gm_l7; /* d */
    void * gm_l8; /* b */
    GM_STACK_CHECK(&F, 321u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t3 = gm_l0;
        if (!(((((uint8_t *)t3) + 2896))[56] != 0)) goto ie2;
        gm_t4c7ipjj9s3fp t4 = GM_LD(gm_t4c7ipjj9s3fp, (((uint8_t *)t3) + 2896));
        F.l1 = t4;
        {
            memmove(gm_out, ((uint8_t *)&F.l1), 56);
            goto gm_fdone;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_f_hash_map_t9h7o8p((void *)&F.l2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t6 = gm_l0;
        void * t7 = GM_LD(void *, (((uint8_t *)t6) + 0));
        uint64_t t8 = gm_f_fn_count_g9jh9qr(t7);
        gm_l3 = t8;
    }
    {
        {
            gm_l5 = ((uint64_t)0ULL);
        }
        {
            uint64_t t10 = gm_l3;
            gm_l4 = t10;
        }
        {
            for (;;) {
                uint64_t t12 = gm_l5;
                uint64_t t13 = gm_l4;
                uint8_t t14 = (t12 < t13);
                if (!t14) goto lb11;
                {
                    uint64_t t17 = gm_l5;
                    uint64_t t20 = GM_LD(uint64_t, ((uint8_t *)&t17));
                    gm_l6 = t20;
                    {
                        void * t22 = gm_l0;
                        void * t23 = GM_LD(void *, (((uint8_t *)t22) + 0));
                        uint64_t t24 = gm_l6;
                        uint32_t t25 = (uint32_t)t24;
                        void * t26 = gm_f_func_in17r5n((void *)NULL, cx, NULL, t23, t25);
                        if (gm_unw.kind) {
                            goto gm_ll15;
                        }
                        gm_l7 = t26;
                    }
                    {
                        void * t28 = gm_l7;
                        void * t29 = GM_LD(void *, (((uint8_t *)t28) + 152));
                        if (!(GM_LD(void *, ((uint8_t *)&t29)) != 0)) goto ie27;
                        void * t30 = GM_LD(void *, ((uint8_t *)&t29));
                        gm_l8 = t30;
                        {
                            void * t32 = gm_l0;
                            void * t33 = GM_LD(void *, (((uint8_t *)t32) + 0));
                            uint64_t t34 = gm_l6;
                            uint32_t t35 = (uint32_t)t34;
                            void * t36 = gm_l8;
                            void * t37 = (void *)((uint8_t *)&F.l2);
                            gm_b0_1 t38 = gm_f_collect_upvars_q0085da((void *)NULL, cx, NULL, t33, t35, t36, t37);
                            if (gm_unw.kind) {
                                goto gm_ll15;
                            }
                        }
                        goto ix27;
ie27:;
ix27:;
                    }
                    goto me18;
mn19:;
                    gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 1281, 5);
me18:;
                }
lc11:;
                {
                    uint64_t t40 = gm_l5;
                    uint64_t r41; bool o42 = __builtin_add_overflow(t40, ((uint64_t)1ULL), &r41);
                    uint64_t t43 = r41;
                    gm_l5 = t43;
                }
            }
gm_ll15:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb11; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc11; }
            }
            goto gm_lad;
lb11:;
        }
    }
    {
        void * t44 = gm_l0;
        const uint8_t *t45 = ((uint8_t *)&F.l2);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tuutj662vpgtg *)(((uint8_t *)t44) + 2896)) + 57, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tuutj662vpgtg *)(((uint8_t *)t44) + 2896)) + 58, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tuutj662vpgtg *)(((uint8_t *)t44) + 2896)) + 60, 0);
        ((uint8_t *)&(*(gm_tuutj662vpgtg *)(((uint8_t *)t44) + 2896)))[56] = 1;
        memmove((uint8_t *)&(*(gm_tuutj662vpgtg *)(((uint8_t *)t44) + 2896)) + 0, t45, 56);
    }
    {
        memmove(gm_out, ((uint8_t *)&F.l2), 56);
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
extern __attribute__((aligned(4))) gm_b0_1 gm_f_check_unwind_2hji4bl(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_collect_upvars_q0085da(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, void * a2, void * a3) {
    struct gm_fr_collect_upvars_q0085da F;
    void * gm_l2; /* e */
    uint32_t gm_l4; /* levels */
    uint32_t gm_l5; /* local */
    uint32_t gm_l6; /* owner */
    uint32_t gm_l7; /* <tmp> */
    uint32_t gm_l8; /* <tmp> */
    uint32_t gm_l9; /* p */
    GM_STACK_CHECK(&F, 164u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    F.l1 = a1;
    gm_l2 = a2;
    F.l3 = a3;
    {
        void * t3 = gm_l2;
        uint8_t t4 = GM_LD(uint8_t, ((((uint8_t *)t3) + 8) + 0));
        if (t4 != ((uint8_t)16ULL)) goto ie2;
        uint32_t t5 = GM_LD(uint32_t, (((((uint8_t *)t3) + 8) + 8) + 0));
        gm_l4 = t5;
        uint32_t t6 = GM_LD(uint32_t, (((((uint8_t *)t3) + 8) + 8) + 4));
        gm_l5 = t6;
        {
            uint32_t t8 = F.l1;
            gm_l6 = t8;
        }
        {
            {
                gm_l8 = ((uint32_t)0ULL);
            }
            {
                uint32_t t10 = gm_l4;
                gm_l7 = t10;
            }
            {
                for (;;) {
                    uint32_t t12 = gm_l8;
                    uint32_t t13 = gm_l7;
                    uint8_t t14 = (t12 < t13);
                    if (!t14) goto lb11;
                    {
                        uint32_t t17 = gm_l8;
                        {
                            void * t22 = F.l0;
                            uint32_t t23 = gm_l6;
                            void * t24 = gm_f_func_in17r5n((void *)NULL, cx, NULL, t22, t23);
                            if (gm_unw.kind) {
                                goto gm_ll15;
                            }
                            if (!(((((uint8_t *)t24) + 88))[4] != 0)) goto ie21;
                            uint32_t t25 = GM_LD(uint32_t, (((uint8_t *)t24) + 88));
                            gm_l9 = t25;
                            {
                                uint32_t t27 = gm_l9;
                                gm_l6 = t27;
                            }
                            goto ix21;
ie21:;
ix21:;
                        }
                        goto me18;
mn19:;
                        gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 1301, 9);
me18:;
                    }
lc11:;
                    {
                        uint32_t t29 = gm_l8;
                        uint32_t r30; bool o31 = __builtin_add_overflow(t29, ((uint32_t)1ULL), &r30);
                        uint32_t t32 = r30;
                        gm_l8 = t32;
                    }
                }
gm_ll15:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb11; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc11; }
                }
                goto gm_lad;
lb11:;
            }
        }
        {
            void * t33 = F.l3;
            uint32_t t34 = gm_l6;
            uint32_t t35 = gm_l5;
            gm_tcc1rq4n1avb2 t36;
            GM_ST(uint32_t, ((uint8_t *)&t36 + 0), t34);
            GM_ST(uint32_t, ((uint8_t *)&t36 + 4), t35);
            gm_tftb1odc739t5 t37 = gm_f_put_3g1tdpr(t33, t36, ((uint8_t)1));
            if (((((uint8_t *)&t37))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t37) + 1, gm_srcmok80ho8hh01, 1304, 9);
            gm_b0_1 t38 = GM_LD(gm_b0_1, (((uint8_t *)&t37) + 1));
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t39 = gm_l2;
        gm_closure t40 = ((gm_closure){(void *)gm_f_fn_jjarlg6, (void *)&F.h, cx});
        gm_f_for_each_child_fm49s73(t39, t40);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_collect_withs_fa29mk1(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_collect_withs_fa29mk1 F;
    void * gm_l0; /* e */
    GM_STACK_CHECK(&F, 123u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    F.l1 = a1;
    {
        void * t3 = gm_l0;
        uint8_t t4 = GM_LD(uint8_t, ((((uint8_t *)t3) + 8) + 0));
        if (t4 != ((uint8_t)58ULL)) goto ie2;
        gm_t710lsrq1m8gk t5 = GM_LD(gm_t710lsrq1m8gk, ((((uint8_t *)t3) + 8) + 8));
        F.l2 = t5;
        {
            void * t7 = F.l1;
            uint32_t t8 = GM_LD(uint32_t, (((uint8_t *)&F.l2) + 0));
            void * t9 = gm_l0;
            gm_t88rcucg1b7uk t10 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t9) + 0));
            gm_tplpbqf3duvn3 t11;
            GM_ST(uint32_t, ((uint8_t *)&t11 + 0), t8);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t11 + 4), t10);
            gm_tftb1odc739t5 t12 = gm_f_push_givp2as(t7, GM_CONV(gm_cs7e9ppsd384bj, t11));
            if (((((uint8_t *)&t12))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t12) + 1, gm_srcmok80ho8hh01, 432, 29);
            gm_b0_1 t13 = GM_LD(gm_b0_1, (((uint8_t *)&t12) + 1));
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t14 = gm_l0;
        gm_closure t15 = ((gm_closure){(void *)gm_f_fn_kssb0q2, (void *)&F.h, cx});
        gm_f_for_each_child_fm49s73(t14, t15);
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
extern __attribute__((aligned(4))) gm_b0_1 gm_f_defer_body_avm7tau(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, uint8_t a3);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_defer_body_inner_v8ddaht(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, uint8_t a3) {
    struct gm_fr_defer_body_inner_v8ddaht F;
    void * gm_l0; /* fg */
    gm_th2lqbo77pvsc gm_l1; /* guard */
    void * gm_l2; /* body */
    uint8_t gm_l3; /* unwinding */
    uint8_t gm_l4; /* saved_check */
    gm_b16_8 gm_l5; /* g */
    GM_STACK_CHECK(&F, 358u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        uint8_t t3 = GM_LD(uint8_t, (((uint8_t *)t2) + 296));
        gm_l4 = t3;
    }
    {
        void * t4 = gm_l0;
        GM_ST(uint8_t, (((uint8_t *)t4) + 296), ((uint8_t)1));
    }
    {
        if (!((((uint8_t *)a1))[16] != 0)) goto ie5;
        gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)a1));
        gm_l5 = t6;
        {
            void * t8 = gm_l0;
            gm_b16_8 t9 = gm_l5;
            gm_tnpifv20irnph t10;
            GM_ST(gm_b16_8, ((uint8_t *)&t10 + 0), t9);
            gm_b0_1 t11 = gm_f_linef_g6msml8((void *)NULL, cx, NULL, t8, t10);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix5;
ie5:;
        {
            void * t13 = gm_l0;
            gm_b16_8 t14;
            GM_ST(void *, (uint8_t *)&t14, (void *)(gm_sh40na9qbga3l));
            GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(1));
            gm_b0_1 t15 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t13, t14);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
ix5:;
    }
    {
        void * t16 = gm_l0;
        void * t17 = gm_l0;
        uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)t17) + 224));
        uint64_t r19; bool o20 = __builtin_add_overflow(t18, ((uint64_t)1ULL), &r19);
        if (o20) gm_trap("integer overflow in `+`", gm_srcmok80ho8hh01, 726, 5);
        uint64_t t21 = r19;
        GM_ST(uint64_t, (((uint8_t *)t16) + 224), t21);
    }
    {
        uint8_t t23 = gm_l3;
        if (!t23) goto ie22;
        {
            void * t25 = gm_l0;
            gm_b16_8 t26;
            GM_ST(void *, (uint8_t *)&t26, (void *)(gm_sovhd8e8hvflr));
            GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)(51));
            gm_b0_1 t27 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t25, t26);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix22;
ie22:;
ix22:;
    }
    {
        {
            void * t29 = gm_l0;
            void * t30 = gm_l2;
            gm_t5o0tpv5jka2l t31;
            gm_f_expr_porbc9t((void *)&t31, (void *)NULL, cx, NULL, t29, t30);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t33 = gm_l3;
        if (!t33) goto ie32;
        {
            void * t35 = gm_l0;
            gm_b16_8 t36;
            GM_ST(void *, (uint8_t *)&t36, (void *)(gm_s2hbi2g88pale));
            GM_ST(uintptr_t, (uint8_t *)&t36 + 8, (uintptr_t)(18));
            gm_b0_1 t37 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t35, t36);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix32;
ie32:;
ix32:;
    }
    {
        void * t38 = gm_l0;
        void * t39 = gm_l0;
        uint64_t t40 = GM_LD(uint64_t, (((uint8_t *)t39) + 224));
        uint64_t r41; bool o42 = __builtin_sub_overflow(t40, ((uint64_t)1ULL), &r41);
        if (o42) gm_trap("integer overflow in `-`", gm_srcmok80ho8hh01, 732, 5);
        uint64_t t43 = r41;
        GM_ST(uint64_t, (((uint8_t *)t38) + 224), t43);
    }
    {
        void * t44 = gm_l0;
        gm_b16_8 t45;
        GM_ST(void *, (uint8_t *)&t45, (void *)(gm_s2cuflhqkkuaq));
        GM_ST(uintptr_t, (uint8_t *)&t45 + 8, (uintptr_t)(1));
        gm_b0_1 t46 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t44, t45);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t47 = gm_l0;
        uint8_t t48 = gm_l4;
        GM_ST(uint8_t, (((uint8_t *)t47) + 296), t48);
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
extern __attribute__((aligned(4))) gm_b0_1 gm_f_end_lines_99v0bhd(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_end_scope_45ar5qv(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, uint8_t a2) {
    struct gm_fr_end_scope_45ar5qv F;
    void * gm_l0; /* fg */
    uint8_t gm_l2; /* fallthrough */
    gm_b16_8 gm_l3; /* skip */
    uint64_t gm_l4; /* k */
    gm_tgtub0v94d5u5 gm_l5; /* d */
    gm_th2lqbo77pvsc gm_l6; /* guard */
    gm_tiilmfd16v3fc gm_l8; /* w */
    GM_STACK_CHECK(&F, 883u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l2 = a2;
    {
        uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)a1) + 96));
        uint8_t t4 = ((t3 == ((uint64_t)0ULL)));
        if (!t4) goto ie2;
        {
            gm_b0_1 t6;
            uint8_t t7 = 0;
            F.ret = t6;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_b16_8 t8;
        GM_ST(void *, (uint8_t *)&t8, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(0));
        gm_l3 = t8;
    }
    {
        uint8_t t10 = gm_l2;
        if (!t10) goto ie9;
        {
            void * t12 = gm_l0;
            uint32_t t13 = gm_f_fresh_5kokjmg(t12);
            gm_t88rcucg1b7uk t14;
            GM_ST(uint32_t, ((uint8_t *)&t14 + 0), t13);
            gm_tabb710d9mq2e t15;
            gm_f_format_hs8tdgq((void *)&t15, (void *)NULL, cx, NULL, t14);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t15))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t15) + 8, gm_srcmok80ho8hh01, 677, 16);
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&t15) + 8));
            gm_b16_8 t17 = GM_LD(gm_b16_8, ((uint8_t *)&t16));
            gm_l3 = t17;
        }
        {
            void * t18 = gm_l0;
            gm_b16_8 t19 = gm_l3;
            gm_tnpifv20irnph t20;
            GM_ST(gm_b16_8, ((uint8_t *)&t20 + 0), t19);
            gm_b0_1 t21 = gm_f_linef_njoe3l6((void *)NULL, cx, NULL, t18, t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix9;
ie9:;
ix9:;
    }
    {
        uint8_t t27 = GM_LD(uint8_t, ((((uint8_t *)a1) + 0) + 0));
        switch (t27) { case ((uint8_t)0ULL): goto ma23; case ((uint8_t)1ULL): goto ma24; case ((uint8_t)2ULL): goto ma25; default: goto ma26; }
ma23:;
        uint8_t t29 = GM_LD(uint8_t, ((((uint8_t *)a1) + 0) + 0));
        if (t29 != ((uint8_t)0ULL)) goto mn28;
        {
            uint64_t t31 = GM_LD(uint64_t, (((uint8_t *)a1) + 96));
            gm_l4 = t31;
        }
        {
            for (;;) {
                uint64_t t33 = gm_l4;
                uint8_t t34 = (t33 >= ((uint64_t)1ULL));
                if (!t34) goto lb32;
                {
                    void * t37 = gm_l0;
                    uint32_t t38 = GM_LD(uint32_t, (((uint8_t *)a1) + 48));
                    uint64_t t39 = gm_l4;
                    gm_tb31k5l794t7p t40;
                    GM_ST(uint32_t, (uint8_t *)&t40 + 4, 0);
                    GM_ST(uint32_t, ((uint8_t *)&t40 + 0), t38);
                    GM_ST(uint64_t, ((uint8_t *)&t40 + 8), t39);
                    gm_tabb710d9mq2e t41;
                    gm_f_format_5bcbbmm((void *)&t41, (void *)NULL, cx, NULL, t40);
                    if (gm_unw.kind) {
                        goto gm_ll35;
                    }
                    if (((((uint8_t *)&t41))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t41) + 8, gm_srcmok80ho8hh01, 684, 26);
                    gm_b16_8 t42 = GM_LD(gm_b16_8, (((uint8_t *)&t41) + 8));
                    gm_b16_8 t43 = GM_LD(gm_b16_8, ((uint8_t *)&t42));
                    gm_b0_1 t44 = gm_f_label_710oev4((void *)NULL, cx, NULL, t37, t43);
                    if (gm_unw.kind) {
                        goto gm_ll35;
                    }
                }
                {
                    uint64_t t45 = gm_l4;
                    uint64_t r46; bool o47 = __builtin_sub_overflow(t45, ((uint64_t)1ULL), &r46);
                    if (o47) gm_trap("integer overflow in `-`", gm_srcmok80ho8hh01, 685, 37);
                    uint64_t t48 = r46;
                    uint64_t t49 = (uintptr_t)t48;
                    uint64_t t50 = GM_LD(uintptr_t, ((((uint8_t *)a1) + 56) + 0) + 8);
                    if (t49 >= (uintptr_t)(t50)) gm_trap_index(t49, t50, gm_srcmok80ho8hh01, 685, 22);
                    gm_tgtub0v94d5u5 t51 = GM_LD(gm_tgtub0v94d5u5, ((uint8_t *)GM_LD(void *, ((((uint8_t *)a1) + 56) + 0)) + t49 * 16));
                    gm_l5 = t51;
                }
                {
                    gm_th2lqbo77pvsc t52;
                    memset(&t52, 0, sizeof t52);
                    gm_l6 = t52;
                }
                {
                    uint8_t t54 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                    if (!t54) goto ie53;
                    {
                        gm_b16_8 t56;
                        GM_ST(void *, (uint8_t *)&t56, (void *)(gm_sm7odqe0d6lgu));
                        GM_ST(uintptr_t, (uint8_t *)&t56 + 8, (uintptr_t)(22));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l6)) + 17, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l6)) + 18, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l6)) + 20, 0);
                        ((uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l6)))[16] = 1;
                        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l6)) + 0, t56);
                    }
                    goto ix53;
ie53:;
ix53:;
                }
                {
                    void * t57 = gm_l0;
                    void * t58 = GM_LD(void *, (((uint8_t *)&gm_l5) + 8));
                    gm_b0_1 t59 = gm_f_defer_body_avm7tau((void *)NULL, cx, NULL, t57, &(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l6)), t58, ((uint8_t)1));
                    if (gm_unw.kind) {
                        goto gm_ll35;
                    }
                }
                {
                    uint64_t t60 = gm_l4;
                    uint64_t r61; bool o62 = __builtin_sub_overflow(t60, ((uint64_t)1ULL), &r61);
                    if (o62) gm_trap("integer overflow in `-`", gm_srcmok80ho8hh01, 689, 17);
                    uint64_t t63 = r61;
                    gm_l4 = t63;
                }
lc32:;
            }
gm_ll35:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb32; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc32; }
            }
            goto gm_lad;
lb32:;
        }
        goto me22;
mn28:;
ma24:;
        uint8_t t65 = GM_LD(uint8_t, ((((uint8_t *)a1) + 0) + 0));
        if (t65 != ((uint8_t)1ULL)) goto mn64;
        gm_thognv9odov03 t66 = GM_LD(gm_thognv9odov03, ((((uint8_t *)a1) + 0) + 8));
        F.l7 = t66;
        {
            void * t68 = gm_l0;
            uint32_t t69 = GM_LD(uint32_t, (((uint8_t *)a1) + 48));
            gm_t88rcucg1b7uk t70;
            GM_ST(uint32_t, ((uint8_t *)&t70 + 0), t69);
            gm_tabb710d9mq2e t71;
            gm_f_format_dd6d4bt((void *)&t71, (void *)NULL, cx, NULL, t70);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t71))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t71) + 8, gm_srcmok80ho8hh01, 693, 22);
            gm_b16_8 t72 = GM_LD(gm_b16_8, (((uint8_t *)&t71) + 8));
            gm_b16_8 t73 = GM_LD(gm_b16_8, ((uint8_t *)&t72));
            gm_b0_1 t74 = gm_f_label_710oev4((void *)NULL, cx, NULL, t68, t73);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t75 = gm_l0;
            uint32_t t76 = GM_LD(uint32_t, (((uint8_t *)&F.l7) + 0));
            gm_t88rcucg1b7uk t77;
            GM_ST(uint32_t, ((uint8_t *)&t77 + 0), t76);
            gm_b0_1 t78 = gm_f_linef_aka1a8l((void *)NULL, cx, NULL, t75, t77);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t79 = gm_l0;
            gm_b16_8 t80 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 8));
            gm_tnpifv20irnph t81;
            GM_ST(gm_b16_8, ((uint8_t *)&t81 + 0), t80);
            gm_b0_1 t82 = gm_f_linef_2n883h6((void *)NULL, cx, NULL, t79, t81);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t83 = gm_l0;
            gm_b16_8 t84 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 24));
            gm_tnpifv20irnph t85;
            GM_ST(gm_b16_8, ((uint8_t *)&t85 + 0), t84);
            gm_b0_1 t86 = gm_f_linef_mkfjg2u((void *)NULL, cx, NULL, t83, t85);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t87 = gm_l0;
            gm_b16_8 t88;
            GM_ST(void *, (uint8_t *)&t88, (void *)(gm_s2cuflhqkkuaq));
            GM_ST(uintptr_t, (uint8_t *)&t88 + 8, (uintptr_t)(1));
            gm_b0_1 t89 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t87, t88);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto me22;
mn64:;
ma25:;
        uint8_t t91 = GM_LD(uint8_t, ((((uint8_t *)a1) + 0) + 0));
        if (t91 != ((uint8_t)2ULL)) goto mn90;
        gm_tiilmfd16v3fc t92 = GM_LD(gm_tiilmfd16v3fc, ((((uint8_t *)a1) + 0) + 8));
        gm_l8 = t92;
        {
            void * t94 = gm_l0;
            uint32_t t95 = GM_LD(uint32_t, (((uint8_t *)a1) + 48));
            gm_t88rcucg1b7uk t96;
            GM_ST(uint32_t, ((uint8_t *)&t96 + 0), t95);
            gm_tabb710d9mq2e t97;
            gm_f_format_dd6d4bt((void *)&t97, (void *)NULL, cx, NULL, t96);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t97))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t97) + 8, gm_srcmok80ho8hh01, 700, 22);
            gm_b16_8 t98 = GM_LD(gm_b16_8, (((uint8_t *)&t97) + 8));
            gm_b16_8 t99 = GM_LD(gm_b16_8, ((uint8_t *)&t98));
            gm_b0_1 t100 = gm_f_label_710oev4((void *)NULL, cx, NULL, t94, t99);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t101 = gm_l0;
            gm_tiilmfd16v3fc t102;
            memset((uint8_t *)&t102 + 4, 0, 4);
            uint32_t t103 = GM_LD(uint32_t, (((uint8_t *)&gm_l8) + 0));
            GM_ST(uint32_t, (uint8_t *)&t102 + 0, t103);
            gm_b16_8 t104 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l8) + 8));
            GM_ST(gm_b16_8, (uint8_t *)&t102 + 8, t104);
            gm_b0_1 t105 = gm_f_linef_1p9d72n((void *)NULL, cx, NULL, t101, &t102);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto me22;
mn90:;
ma26:;
        gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 680, 5);
me22:;
    }
    {
        void * t106 = gm_l0;
        gm_b0_1 t107 = gm_f_ladder_36amdbn((void *)NULL, cx, NULL, t106);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint8_t t109 = gm_l2;
        if (!t109) goto ie108;
        {
            void * t111 = gm_l0;
            gm_b16_8 t112 = gm_l3;
            gm_b0_1 t113 = gm_f_label_710oev4((void *)NULL, cx, NULL, t111, t112);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix108;
ie108:;
ix108:;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_exit_c_name_i72r98r(gm_t9qkh0rsooc8d a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_exit_c_name_i72r98r F;
    gm_t9qkh0rsooc8d gm_l0; /* k */
    GM_STACK_CHECK_SHALLOW(&F, 135u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t1;
    uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    switch (t9) { case ((uint8_t)0ULL): goto ma3; case ((uint8_t)1ULL): goto ma4; case ((uint8_t)2ULL): goto ma5; case ((uint8_t)3ULL): goto ma6; case ((uint8_t)4ULL): goto ma7; default: goto ma8; }
ma3:;
    uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t11 != ((uint8_t)0ULL)) goto mn10;
    gm_b16_8 t12;
    GM_ST(void *, (uint8_t *)&t12, (void *)(gm_sbrtnmpf78kqq));
    GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)(9));
    t1 = t12;
    goto me2;
mn10:;
ma4:;
    uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t14 != ((uint8_t)1ULL)) goto mn13;
    gm_b16_8 t15;
    GM_ST(void *, (uint8_t *)&t15, (void *)(gm_sug1rl2kq22u3));
    GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)(8));
    t1 = t15;
    goto me2;
mn13:;
ma5:;
    uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t17 != ((uint8_t)2ULL)) goto mn16;
    gm_b16_8 t18;
    GM_ST(void *, (uint8_t *)&t18, (void *)(gm_s2ogeh8fp9da3));
    GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(11));
    t1 = t18;
    goto me2;
mn16:;
ma6:;
    uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t20 != ((uint8_t)3ULL)) goto mn19;
    gm_b16_8 t21;
    GM_ST(void *, (uint8_t *)&t21, (void *)(gm_s9nvmaqikg2bc));
    GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(7));
    t1 = t21;
    goto me2;
mn19:;
ma7:;
    uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t23 != ((uint8_t)4ULL)) goto mn22;
    gm_b16_8 t24;
    GM_ST(void *, (uint8_t *)&t24, (void *)(gm_spdpobhso0dbc));
    GM_ST(uintptr_t, (uint8_t *)&t24 + 8, (uintptr_t)(9));
    t1 = t24;
    goto me2;
mn22:;
ma8:;
    gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 742, 45);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint32_t gm_f_expr_call_depth_dliqc7u(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_expr_call_depth_dliqc7u F;
    void * gm_l1; /* e */
    uint32_t gm_l4; /* c */
    gm_tvdg94bk7aa22 gm_l5; /* op */
    GM_STACK_CHECK(&F, 186u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = a1;
    {
        F.l2 = ((uint32_t)0ULL);
    }
    {
        void * t2 = gm_l1;
        uint8_t t9 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        switch (t9) { case ((uint8_t)0ULL): goto ma7; case ((uint8_t)1ULL): goto ma7; case ((uint8_t)2ULL): goto ma7; case ((uint8_t)3ULL): goto ma7; case ((uint8_t)4ULL): goto ma7; case ((uint8_t)5ULL): goto ma7; case ((uint8_t)6ULL): goto ma7; case ((uint8_t)7ULL): goto ma7; case ((uint8_t)8ULL): goto ma7; case ((uint8_t)9ULL): goto ma7; case ((uint8_t)10ULL): goto ma7; case ((uint8_t)11ULL): goto ma7; case ((uint8_t)12ULL): goto ma7; case ((uint8_t)13ULL): goto ma7; case ((uint8_t)14ULL): goto ma7; case ((uint8_t)15ULL): goto ma7; case ((uint8_t)16ULL): goto ma7; case ((uint8_t)17ULL): goto ma7; case ((uint8_t)18ULL): goto ma7; case ((uint8_t)19ULL): goto ma7; case ((uint8_t)20ULL): goto ma7; case ((uint8_t)21ULL): goto ma7; case ((uint8_t)22ULL): goto ma7; case ((uint8_t)23ULL): goto ma7; case ((uint8_t)24ULL): goto ma7; case ((uint8_t)25ULL): goto ma7; case ((uint8_t)26ULL): goto ma7; case ((uint8_t)27ULL): goto ma7; case ((uint8_t)28ULL): goto ma7; case ((uint8_t)29ULL): goto ma7; case ((uint8_t)30ULL): goto ma7; case ((uint8_t)31ULL): goto ma7; case ((uint8_t)32ULL): goto ma7; case ((uint8_t)33ULL): goto ma6; case ((uint8_t)34ULL): goto ma4; case ((uint8_t)35ULL): goto ma7; case ((uint8_t)36ULL): goto ma7; case ((uint8_t)37ULL): goto ma7; case ((uint8_t)38ULL): goto ma7; case ((uint8_t)39ULL): goto ma7; case ((uint8_t)40ULL): goto ma7; case ((uint8_t)41ULL): goto ma7; case ((uint8_t)42ULL): goto ma7; case ((uint8_t)43ULL): goto ma7; case ((uint8_t)44ULL): goto ma7; case ((uint8_t)45ULL): goto ma7; case ((uint8_t)46ULL): goto ma7; case ((uint8_t)47ULL): goto ma7; case ((uint8_t)48ULL): goto ma7; case ((uint8_t)49ULL): goto ma7; case ((uint8_t)50ULL): goto ma7; case ((uint8_t)51ULL): goto ma7; case ((uint8_t)52ULL): goto ma7; case ((uint8_t)53ULL): goto ma5; case ((uint8_t)54ULL): goto ma5; case ((uint8_t)55ULL): goto ma5; case ((uint8_t)56ULL): goto ma5; case ((uint8_t)57ULL): goto ma5; case ((uint8_t)58ULL): goto ma5; default: goto ma7; }
ma4:;
        uint8_t t11 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t11 != ((uint8_t)34ULL)) goto mn10;
        gm_tsth9qu1dv0g3 t12 = GM_LD(gm_tsth9qu1dv0g3, (((((uint8_t *)t2) + 8) + 8) + 0));
        F.l3 = t12;
        uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
        if (t15 != ((uint8_t)0ULL)) goto mn14;
        uint32_t t16 = GM_LD(uint32_t, (((uint8_t *)&F.l3) + 8));
        gm_l4 = t16;
        {
            void * t18 = F.l0;
            uint32_t t19 = gm_l4;
            uint32_t t20 = gm_f_call_depth_4m6ap7n((void *)NULL, cx, NULL, t18, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint32_t r21; bool o22 = __builtin_add_overflow(t20, ((uint32_t)1ULL), &r21);
            uint32_t t23 = o22 ? (1 ? ((uint32_t)4294967295ULL) : ((uint32_t)0ULL)) : r21;
            F.l2 = t23;
        }
        goto me13;
mn14:;
        uint8_t t25 = GM_LD(uint8_t, gm_c71idar1l1k);
        uint32_t t26 = (uint32_t)t25;
        uint8_t t27 = 0;
        F.ret = t26;
        goto gm_fret;
        goto me13;
mn24:;
        gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 322, 29);
me13:;
        goto me3;
mn10:;
ma5:;
        uint8_t t31 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t31 != ((uint8_t)55ULL)) goto pa30;
        goto po29;
pa30:;
        uint8_t t33 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t33 != ((uint8_t)56ULL)) goto pa32;
        goto po29;
pa32:;
        uint8_t t35 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t35 != ((uint8_t)57ULL)) goto pa34;
        goto po29;
pa34:;
        uint8_t t37 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t37 != ((uint8_t)53ULL)) goto pa36;
        goto po29;
pa36:;
        uint8_t t39 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t39 != ((uint8_t)54ULL)) goto pa38;
        goto po29;
pa38:;
        uint8_t t41 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t41 != ((uint8_t)58ULL)) goto pa40;
        goto po29;
pa40:;
        goto mn28;
po29:;
        uint8_t t42 = GM_LD(uint8_t, gm_c71idar1l1k);
        uint32_t t43 = (uint32_t)t42;
        uint8_t t44 = 0;
        F.ret = t43;
        goto gm_fret;
        goto me3;
mn28:;
ma6:;
        uint8_t t46 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t46 != ((uint8_t)33ULL)) goto mn45;
        gm_tvdg94bk7aa22 t47 = GM_LD(gm_tvdg94bk7aa22, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l5 = t47;
        uint8_t t49;
        uint8_t t56 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
        switch (t56) { case ((uint8_t)0ULL): goto ma54; case ((uint8_t)1ULL): goto ma54; case ((uint8_t)2ULL): goto ma54; case ((uint8_t)3ULL): goto ma54; case ((uint8_t)4ULL): goto ma54; case ((uint8_t)5ULL): goto ma54; case ((uint8_t)6ULL): goto ma54; case ((uint8_t)7ULL): goto ma54; case ((uint8_t)8ULL): goto ma54; case ((uint8_t)9ULL): goto ma54; case ((uint8_t)10ULL): goto ma54; case ((uint8_t)11ULL): goto ma54; case ((uint8_t)12ULL): goto ma54; case ((uint8_t)13ULL): goto ma54; case ((uint8_t)14ULL): goto ma54; case ((uint8_t)15ULL): goto ma54; case ((uint8_t)16ULL): goto ma54; case ((uint8_t)17ULL): goto ma54; case ((uint8_t)18ULL): goto ma54; case ((uint8_t)19ULL): goto ma54; case ((uint8_t)20ULL): goto ma54; case ((uint8_t)21ULL): goto ma54; case ((uint8_t)22ULL): goto ma54; case ((uint8_t)23ULL): goto ma54; case ((uint8_t)24ULL): goto ma54; case ((uint8_t)25ULL): goto ma54; case ((uint8_t)26ULL): goto ma54; case ((uint8_t)27ULL): goto ma54; case ((uint8_t)28ULL): goto ma54; case ((uint8_t)29ULL): goto ma54; case ((uint8_t)30ULL): goto ma54; case ((uint8_t)31ULL): goto ma54; case ((uint8_t)32ULL): goto ma54; case ((uint8_t)33ULL): goto ma54; case ((uint8_t)34ULL): goto ma54; case ((uint8_t)35ULL): goto ma54; case ((uint8_t)36ULL): goto ma54; case ((uint8_t)37ULL): goto ma54; case ((uint8_t)38ULL): goto ma54; case ((uint8_t)39ULL): goto ma54; case ((uint8_t)40ULL): goto ma54; case ((uint8_t)41ULL): goto ma54; case ((uint8_t)42ULL): goto ma54; case ((uint8_t)43ULL): goto ma54; case ((uint8_t)44ULL): goto ma54; case ((uint8_t)45ULL): goto ma54; case ((uint8_t)46ULL): goto ma54; case ((uint8_t)47ULL): goto ma54; case ((uint8_t)48ULL): goto ma54; case ((uint8_t)49ULL): goto ma54; case ((uint8_t)50ULL): goto ma54; case ((uint8_t)51ULL): goto ma54; case ((uint8_t)52ULL): goto ma54; case ((uint8_t)53ULL): goto ma54; case ((uint8_t)54ULL): goto ma54; case ((uint8_t)55ULL): goto ma54; case ((uint8_t)56ULL): goto ma54; case ((uint8_t)57ULL): goto ma54; case ((uint8_t)58ULL): goto ma54; case ((uint8_t)59ULL): goto ma54; case ((uint8_t)60ULL): goto ma54; case ((uint8_t)61ULL): goto ma54; case ((uint8_t)62ULL): goto ma54; case ((uint8_t)63ULL): goto ma54; case ((uint8_t)64ULL): goto ma54; case ((uint8_t)65ULL): goto ma54; case ((uint8_t)66ULL): goto ma54; case ((uint8_t)67ULL): goto ma54; case ((uint8_t)68ULL): goto ma54; case ((uint8_t)69ULL): goto ma54; case ((uint8_t)70ULL): goto ma54; case ((uint8_t)71ULL): goto ma54; case ((uint8_t)72ULL): goto ma53; case ((uint8_t)73ULL): goto ma51; case ((uint8_t)74ULL): goto ma54; case ((uint8_t)75ULL): goto ma54; case ((uint8_t)76ULL): goto ma54; case ((uint8_t)77ULL): goto ma54; case ((uint8_t)78ULL): goto ma54; case ((uint8_t)79ULL): goto ma52; case ((uint8_t)80ULL): goto ma54; case ((uint8_t)81ULL): goto ma54; default: goto ma54; }
ma51:;
        uint8_t t58 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
        if (t58 != ((uint8_t)73ULL)) goto mn57;
        t49 = ((uint8_t)1);
        goto me50;
mn57:;
ma52:;
        uint8_t t60 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
        if (t60 != ((uint8_t)79ULL)) goto mn59;
        t49 = ((uint8_t)1);
        goto me50;
mn59:;
ma53:;
        uint8_t t62 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
        if (t62 != ((uint8_t)72ULL)) goto mn61;
        t49 = ((uint8_t)1);
        goto me50;
mn61:;
ma54:;
        t49 = ((uint8_t)0);
        goto me50;
mn63:;
ma55:;
        gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 327, 31);
me50:;
        if (!t49) goto ie48;
        {
            uint8_t t65 = GM_LD(uint8_t, gm_c71idar1l1k);
            uint32_t t66 = (uint32_t)t65;
            uint8_t t67 = 0;
            F.ret = t66;
            goto gm_fret;
        }
        goto ix48;
ie48:;
ix48:;
        goto me3;
mn45:;
ma7:;
        goto me3;
mn68:;
ma8:;
        gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 321, 5);
me3:;
    }
    {
        void * t70 = gm_l1;
        gm_closure t71 = ((gm_closure){(void *)gm_f_fn_v0t9sik, (void *)&F.h, cx});
        gm_f_for_each_child_fm49s73(t70, t71);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint32_t t72 = F.l2;
        uint8_t t73 = 0;
        F.ret = t72;
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
__attribute__((aligned(4))) void gm_f_expr_into_dn9kavo(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2, gm_tnrltbc61i6v1 a3) {
    struct gm_fr_expr_into_dn9kavo F;
    void * gm_l0; /* fg */
    void * gm_l1; /* e */
    gm_b16_8 gm_l2; /* dest */
    gm_tnrltbc61i6v1 gm_l3; /* obs */
    gm_b16_8 gm_l4; /* v */
    gm_b16_8 gm_l6; /* v */
    gm_b16_8 gm_l7; /* elems */
    gm_b16_8 gm_l8; /* elems */
    uint8_t gm_l9; /* nested */
    uint64_t gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* i */
    gm_b16_8 gm_l13; /* t */
    gm_b16_8 gm_l14; /* c */
    uint8_t gm_l15; /* eager */
    uint64_t gm_l17; /* <tmp> */
    uint64_t gm_l18; /* <tmp> */
    uint64_t gm_l19; /* i */
    gm_b16_8 gm_l20; /* offs */
    uint64_t gm_l21; /* cursor */
    uint64_t gm_l22; /* <tmp> */
    uint64_t gm_l23; /* <tmp> */
    uint64_t gm_l24; /* i */
    uint64_t gm_l25; /* off */
    uint64_t gm_l26; /* sz */
    uint64_t gm_l27; /* total */
    uint64_t gm_l28; /* <tmp> */
    uint64_t gm_l29; /* <tmp> */
    uint64_t gm_l30; /* i */
    void * gm_l31; /* x */
    uint64_t gm_l32; /* off */
    gm_b16_8 gm_l33; /* c */
    gm_b16_8 gm_l34; /* sub */
    gm_b16_8 gm_l35; /* a */
    uint64_t gm_l36; /* sz */
    gm_b16_8 gm_l37; /* v */
    uint64_t gm_l39; /* <tmp> */
    uint64_t gm_l40; /* <tmp> */
    uint64_t gm_l41; /* i */
    gm_b16_8 gm_l42; /* v */
    uint64_t gm_l43; /* <tmp> */
    uint64_t gm_l44; /* <tmp> */
    uint64_t gm_l45; /* i */
    gm_b16_8 gm_l46; /* c */
    gm_tmn7n0n9raou5 gm_l47; /* kind */
    void * gm_l48; /* x */
    void * gm_l49; /* x */
    gm_ttggq7ng4kp3p gm_l50; /* vx */
    void * gm_l51; /* b */
    void * gm_l53; /* t */
    GM_STACK_CHECK(&F, 4693u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        {
            void * t4 = gm_l0;
            void * t5 = gm_l1;
            uint8_t t6 = gm_f_builds_in_place_ae1b90u((void *)NULL, cx, NULL, t4, t5);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t7 = (uint8_t)!t6;
            if (!t7) goto ie3;
            {
                void * t9 = gm_l0;
                void * t10 = gm_l1;
                gm_t5o0tpv5jka2l t11;
                gm_f_expr_porbc9t((void *)&t11, (void *)NULL, cx, NULL, t9, t10);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t11))[0] != 0)) {
                    gm_tqgtd5gtgj65s t12;
                    GM_ST(uint8_t, (uint8_t *)&t12 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t12 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t12 + 4, 0);
                    ((uint8_t *)&t12)[0] = 1;
                    GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t12 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t11) + 8)));
                    F.ret = t12;
                    goto gm_fret;
                }
                gm_b16_8 t13 = GM_LD(gm_b16_8, (((uint8_t *)&t11) + 8));
                gm_l4 = t13;
            }
            {
                void * t15 = gm_l0;
                void * t16 = GM_LD(void *, (((uint8_t *)t15) + 0));
                void * t17 = (void *)(((uint8_t *)t16) + 8);
                void * t18 = gm_l1;
                gm_t88rcucg1b7uk t19 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t18) + 0));
                uint8_t t20 = gm_f_is_never_in5ftma(t17, GM_CONV(gm_csguankko4lmhd, t19));
                uint8_t t21 = (uint8_t)!t20;
                if (!t21) goto ie14;
                {
                    void * t23 = gm_l0;
                    gm_t6j6smj7qoctu t24;
                    gm_b16_8 t25 = gm_l2;
                    GM_ST(gm_b16_8, (uint8_t *)&t24 + 0, t25);
                    gm_b16_8 t26 = gm_l4;
                    GM_ST(gm_b16_8, (uint8_t *)&t24 + 16, t26);
                    gm_b0_1 t27 = gm_f_linef_uu8i6ue((void *)NULL, cx, NULL, t23, &t24);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                goto ix14;
ie14:;
ix14:;
            }
            {
                gm_b0_1 t28;
                memset(&(*(gm_tqgtd5gtgj65s *)gm_out), 0, sizeof (*(gm_tqgtd5gtgj65s *)gm_out));
                ((uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out))[0] = 0;
                GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 8, t28);
                goto gm_fdone;
            }
            goto ix3;
ie3:;
ix3:;
        }
        {
            void * t29 = gm_l1;
            uint8_t t39 = GM_LD(uint8_t, ((((uint8_t *)t29) + 8) + 0));
            switch (t39) { case ((uint8_t)0ULL): goto ma37; case ((uint8_t)1ULL): goto ma37; case ((uint8_t)2ULL): goto ma37; case ((uint8_t)3ULL): goto ma37; case ((uint8_t)4ULL): goto ma37; case ((uint8_t)5ULL): goto ma37; case ((uint8_t)6ULL): goto ma37; case ((uint8_t)7ULL): goto ma37; case ((uint8_t)8ULL): goto ma37; case ((uint8_t)9ULL): goto ma37; case ((uint8_t)10ULL): goto ma37; case ((uint8_t)11ULL): goto ma37; case ((uint8_t)12ULL): goto ma37; case ((uint8_t)13ULL): goto ma37; case ((uint8_t)14ULL): goto ma37; case ((uint8_t)15ULL): goto ma37; case ((uint8_t)16ULL): goto ma37; case ((uint8_t)17ULL): goto ma37; case ((uint8_t)18ULL): goto ma37; case ((uint8_t)19ULL): goto ma37; case ((uint8_t)20ULL): goto ma37; case ((uint8_t)21ULL): goto ma37; case ((uint8_t)22ULL): goto ma37; case ((uint8_t)23ULL): goto ma37; case ((uint8_t)24ULL): goto ma37; case ((uint8_t)25ULL): goto ma37; case ((uint8_t)26ULL): goto ma37; case ((uint8_t)27ULL): goto ma37; case ((uint8_t)28ULL): goto ma37; case ((uint8_t)29ULL): goto ma37; case ((uint8_t)30ULL): goto ma37; case ((uint8_t)31ULL): goto ma37; case ((uint8_t)32ULL): goto ma33; case ((uint8_t)33ULL): goto ma37; case ((uint8_t)34ULL): goto ma31; case ((uint8_t)35ULL): goto ma32; case ((uint8_t)36ULL): goto ma32; case ((uint8_t)37ULL): goto ma37; case ((uint8_t)38ULL): goto ma35; case ((uint8_t)39ULL): goto ma34; case ((uint8_t)40ULL): goto ma37; case ((uint8_t)41ULL): goto ma37; case ((uint8_t)42ULL): goto ma37; case ((uint8_t)43ULL): goto ma37; case ((uint8_t)44ULL): goto ma37; case ((uint8_t)45ULL): goto ma37; case ((uint8_t)46ULL): goto ma36; case ((uint8_t)47ULL): goto ma37; case ((uint8_t)48ULL): goto ma37; case ((uint8_t)49ULL): goto ma37; case ((uint8_t)50ULL): goto ma37; case ((uint8_t)51ULL): goto ma37; case ((uint8_t)52ULL): goto ma37; case ((uint8_t)53ULL): goto ma37; case ((uint8_t)54ULL): goto ma37; case ((uint8_t)55ULL): goto ma37; case ((uint8_t)56ULL): goto ma37; case ((uint8_t)57ULL): goto ma37; case ((uint8_t)58ULL): goto ma37; default: goto ma37; }
ma31:;
            uint8_t t41 = GM_LD(uint8_t, ((((uint8_t *)t29) + 8) + 0));
            if (t41 != ((uint8_t)34ULL)) goto mn40;
            {
                void * t43 = gm_l0;
                gm_ts0eitcfeteg8 t44;
                memset((uint8_t *)&t44 + 36, 0, 4);
                void * t45 = gm_l1;
                GM_ST(void *, (uint8_t *)&t44 + 0, t45);
                gm_b16_8 t46 = gm_l2;
                GM_ST(gm_b16_8, (uint8_t *)&t44 + 8, t46);
                gm_tnrltbc61i6v1 t47 = gm_l3;
                GM_ST(gm_tnrltbc61i6v1, (uint8_t *)&t44 + 24, t47);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tjefu848ssvm9 *)(((uint8_t *)t43) + 104)) + 41, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tjefu848ssvm9 *)(((uint8_t *)t43) + 104)) + 42, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tjefu848ssvm9 *)(((uint8_t *)t43) + 104)) + 44, 0);
                ((uint8_t *)&(*(gm_tjefu848ssvm9 *)(((uint8_t *)t43) + 104)))[40] = 1;
                GM_ST(gm_ts0eitcfeteg8, (uint8_t *)&(*(gm_tjefu848ssvm9 *)(((uint8_t *)t43) + 104)) + 0, t44);
            }
            {
                void * t48 = gm_l0;
                void * t49 = gm_l1;
                gm_f_expr_porbc9t((void *)&F.l5, (void *)NULL, cx, NULL, t48, t49);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t50 = gm_l0;
                gm_tjefu848ssvm9 t51;
                memset(&t51, 0, sizeof t51);
                GM_ST(gm_tjefu848ssvm9, (((uint8_t *)t50) + 104), t51);
            }
            {
                gm_t5o0tpv5jka2l t52 = F.l5;
                if (((((uint8_t *)&t52))[0] != 0)) {
                    gm_tqgtd5gtgj65s t53;
                    GM_ST(uint8_t, (uint8_t *)&t53 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t53 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t53 + 4, 0);
                    ((uint8_t *)&t53)[0] = 1;
                    GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t53 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t52) + 8)));
                    F.ret = t53;
                    goto gm_fret;
                }
                gm_b16_8 t54 = GM_LD(gm_b16_8, (((uint8_t *)&t52) + 8));
                gm_l6 = t54;
            }
            {
                gm_b16_8 t56 = gm_l6;
                gm_b16_8 t57 = gm_l2;
                uint8_t t58 = (gm_eq0iqiubfm7e(((uint8_t *)&t56), ((uint8_t *)&t57)));
                uint8_t t59 = (uint8_t)!t58;
                if (!t59) goto ie55;
                {
                    void * t61 = gm_l0;
                    gm_t6j6smj7qoctu t62;
                    gm_b16_8 t63 = gm_l2;
                    GM_ST(gm_b16_8, (uint8_t *)&t62 + 0, t63);
                    gm_b16_8 t64 = gm_l6;
                    GM_ST(gm_b16_8, (uint8_t *)&t62 + 16, t64);
                    gm_b0_1 t65 = gm_f_linef_uu8i6ue((void *)NULL, cx, NULL, t61, &t62);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                goto ix55;
ie55:;
ix55:;
            }
            goto me30;
mn40:;
ma32:;
            uint8_t t69 = GM_LD(uint8_t, ((((uint8_t *)t29) + 8) + 0));
            if (t69 != ((uint8_t)35ULL)) goto pa68;
            gm_b16_8 t70 = GM_LD(gm_b16_8, ((((uint8_t *)t29) + 8) + 8));
            gm_l7 = t70;
            goto po67;
pa68:;
            uint8_t t72 = GM_LD(uint8_t, ((((uint8_t *)t29) + 8) + 0));
            if (t72 != ((uint8_t)36ULL)) goto pa71;
            gm_b16_8 t73 = GM_LD(gm_b16_8, ((((uint8_t *)t29) + 8) + 8));
            gm_l7 = t73;
            goto po67;
pa71:;
            goto mn66;
po67:;
            {
                gm_l9 = ((uint8_t)0);
            }
            {
                {
                    gm_l11 = ((uint64_t)0ULL);
                }
                {
                    uint64_t t76 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                    gm_l10 = t76;
                }
                {
                    for (;;) {
                        uint64_t t78 = gm_l11;
                        uint64_t t79 = gm_l10;
                        uint8_t t80 = (t78 < t79);
                        if (!t80) goto lb77;
                        {
                            uint64_t t83 = gm_l11;
                            uint64_t t86 = GM_LD(uint64_t, ((uint8_t *)&t83));
                            gm_l12 = t86;
                            {
                                void * t89 = gm_l0;
                                uint64_t t90 = gm_l12;
                                uint64_t t91 = (uintptr_t)t90;
                                uint64_t t92 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                                if (t91 >= (uintptr_t)(t92)) gm_trap_index(t91, t92, gm_srcmok80ho8hh01, 874, 39);
                                void * t93 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t91 * 88);
                                gm_tnrltbc61i6v1 t94 = gm_l3;
                                uint8_t t95 = gm_f_nests_in_place_jc41ehn((void *)NULL, cx, NULL, t89, t93, t94);
                                if (gm_unw.kind) {
                                    goto gm_ll81;
                                }
                                if (!t95) goto ie88;
                                {
                                    gm_l9 = ((uint8_t)1);
                                }
                                goto ix88;
ie88:;
ix88:;
                            }
                            goto me84;
mn85:;
                            gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 873, 13);
me84:;
                        }
lc77:;
                        {
                            uint64_t t98 = gm_l11;
                            uint64_t r99; bool o100 = __builtin_add_overflow(t98, ((uint64_t)1ULL), &r99);
                            uint64_t t101 = r99;
                            gm_l11 = t101;
                        }
                    }
gm_ll81:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb77; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc77; }
                    }
                    goto gm_lad;
lb77:;
                }
            }
            {
                uint8_t t103 = gm_l9;
                if (!t103) goto ie102;
                uint8_t t104;
                uint8_t t107 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                if (t107 != ((uint8_t)2ULL)) goto mn106;
                t104 = ((uint8_t)1);
                goto me105;
mn106:;
                t104 = ((uint8_t)0);
                goto me105;
mn108:;
                gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 876, 27);
me105:;
                if (!t104) goto ie102;
                {
                    void * t110 = gm_l0;
                    void * t111 = gm_l1;
                    gm_t88rcucg1b7uk t112 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t111) + 0));
                    gm_b16_8 t113 = gm_f_tmp_uninit_jr39s5u((void *)NULL, cx, NULL, t110, t112);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l13 = t113;
                }
                {
                    void * t114 = gm_l0;
                    void * t115 = gm_l1;
                    gm_b16_8 t116 = gm_l13;
                    gm_tnrltbc61i6v1 t117;
                    GM_ST(uint8_t, (uint8_t *)&t117 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t117 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t117 + 4, 0);
                    GM_ST(uint32_t, (uint8_t *)&t117 + 8, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t117 + 0), ((uint8_t)0ULL));
                    gm_tqgtd5gtgj65s t118;
                    gm_f_expr_into_dn9kavo((void *)&t118, (void *)NULL, cx, NULL, t114, t115, t116, t117);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t118))[0] != 0)) {
                        gm_tqgtd5gtgj65s t119;
                        GM_ST(uint8_t, (uint8_t *)&t119 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t119 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t119 + 4, 0);
                        ((uint8_t *)&t119)[0] = 1;
                        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t119 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t118) + 8)));
                        F.ret = t119;
                        goto gm_fret;
                    }
                    gm_b0_1 t120 = GM_LD(gm_b0_1, (((uint8_t *)&t118) + 8));
                }
                {
                    void * t121 = gm_l0;
                    void * t122 = GM_LD(void *, (((uint8_t *)t121) + 0));
                    void * t123 = (void *)(((uint8_t *)t122) + 8);
                    void * t124 = gm_l1;
                    gm_t88rcucg1b7uk t125 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t124) + 0));
                    gm_b16_8 t126 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t123, t125);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l14 = t126;
                }
                {
                    void * t127 = gm_l0;
                    gm_tfeab279c16rr t128;
                    gm_b16_8 t129 = gm_l14;
                    GM_ST(gm_b16_8, (uint8_t *)&t128 + 0, t129);
                    gm_b16_8 t130 = gm_l2;
                    GM_ST(gm_b16_8, (uint8_t *)&t128 + 16, t130);
                    gm_b16_8 t131 = gm_l13;
                    GM_ST(gm_b16_8, (uint8_t *)&t128 + 32, t131);
                    gm_b0_1 t132 = gm_f_linef_21h063v((void *)NULL, cx, NULL, t127, &t128);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    gm_b0_1 t133;
                    memset(&(*(gm_tqgtd5gtgj65s *)gm_out), 0, sizeof (*(gm_tqgtd5gtgj65s *)gm_out));
                    ((uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out))[0] = 0;
                    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 8, t133);
                    goto gm_fdone;
                }
                goto ix102;
ie102:;
ix102:;
            }
            {
                gm_l15 = ((uint8_t)0);
            }
            {
                uint8_t t139 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                switch (t139) { case ((uint8_t)0ULL): goto ma135; case ((uint8_t)1ULL): goto ma136; case ((uint8_t)2ULL): goto ma137; default: goto ma138; }
ma135:;
                uint8_t t141 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                if (t141 != ((uint8_t)0ULL)) goto mn140;
                {
                    gm_l15 = ((uint8_t)1);
                }
                goto me134;
mn140:;
ma136:;
                uint8_t t144 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                if (t144 != ((uint8_t)1ULL)) goto mn143;
                {
                    void * t146 = gm_l0;
                    void * t147 = gm_l1;
                    uint8_t t148 = gm_f_frame_pure_cbdra2q((void *)NULL, cx, NULL, t146, t147);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l15 = t148;
                }
                goto me134;
mn143:;
ma137:;
                uint8_t t150 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                if (t150 != ((uint8_t)2ULL)) goto mn149;
                {
                    gm_l15 = ((uint8_t)0);
                }
                goto me134;
mn149:;
ma138:;
                gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 885, 13);
me134:;
            }
            {
                gm_f_list_kl0t9m0((void *)&F.l16, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t153 = gm_l1;
                uint8_t t154;
                uint8_t t157 = GM_LD(uint8_t, ((((uint8_t *)t153) + 8) + 0));
                if (t157 != ((uint8_t)35ULL)) goto mn156;
                t154 = ((uint8_t)1);
                goto me155;
mn156:;
                t154 = ((uint8_t)0);
                goto me155;
mn158:;
                gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 891, 16);
me155:;
                if (!t154) goto ie152;
                {
                    void * t160 = (void *)((uint8_t *)&F.l16);
                    void * t161 = gm_l0;
                    void * t162 = GM_LD(void *, (((uint8_t *)t161) + 0));
                    void * t163 = (void *)(((uint8_t *)t162) + 8);
                    void * t164 = gm_l1;
                    gm_t88rcucg1b7uk t165 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t164) + 0));
                    gm_b16_8 t166 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t163, t165);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tftb1odc739t5 t167 = gm_f_extend_ll5819n(t160, t166);
                    if (((((uint8_t *)&t167))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t167) + 1, gm_srcmok80ho8hh01, 892, 17);
                    gm_b0_1 t168 = GM_LD(gm_b0_1, (((uint8_t *)&t167) + 1));
                }
                goto ix152;
ie152:;
                {
                    {
                        gm_l18 = ((uint64_t)0ULL);
                    }
                    {
                        uint64_t t171 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                        gm_l17 = t171;
                    }
                    {
                        for (;;) {
                            uint64_t t173 = gm_l18;
                            uint64_t t174 = gm_l17;
                            uint8_t t175 = (t173 < t174);
                            if (!t175) goto lb172;
                            {
                                uint64_t t178 = gm_l18;
                                uint64_t t181 = GM_LD(uint64_t, ((uint8_t *)&t178));
                                gm_l19 = t181;
                                {
                                    void * t183 = (void *)((uint8_t *)&F.l16);
                                    uint64_t t184 = gm_l19;
                                    uint64_t t185 = (uint64_t)t184;
                                    void * t186 = gm_l0;
                                    void * t187 = GM_LD(void *, (((uint8_t *)t186) + 0));
                                    void * t188 = (void *)(((uint8_t *)t187) + 8);
                                    uint64_t t189 = gm_l19;
                                    uint64_t t190 = (uintptr_t)t189;
                                    uint64_t t191 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                                    if (t190 >= (uintptr_t)(t191)) gm_trap_index(t190, t191, gm_srcmok80ho8hh01, 894, 70);
                                    gm_t88rcucg1b7uk t192 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t190 * 88) + 0));
                                    uint64_t t193 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t188, t192);
                                    if (gm_unw.kind) {
                                        goto gm_ll176;
                                    }
                                    uint64_t r194; bool o195 = __builtin_mul_overflow(t185, t193, &r194);
                                    if (o195) gm_trap("integer overflow in `*`", gm_srcmok80ho8hh01, 894, 48);
                                    uint64_t t196 = r194;
                                    gm_tftb1odc739t5 t197 = gm_f_push_7o6oubd(t183, t196);
                                    if (((((uint8_t *)&t197))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t197) + 1, gm_srcmok80ho8hh01, 894, 41);
                                    gm_b0_1 t198 = GM_LD(gm_b0_1, (((uint8_t *)&t197) + 1));
                                }
                                goto me179;
mn180:;
                                gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 894, 17);
me179:;
                            }
lc172:;
                            {
                                uint64_t t200 = gm_l18;
                                uint64_t r201; bool o202 = __builtin_add_overflow(t200, ((uint64_t)1ULL), &r201);
                                uint64_t t203 = r201;
                                gm_l18 = t203;
                            }
                        }
gm_ll176:;
                        if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb172; }
                            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc172; }
                        }
                        goto gm_lad;
lb172:;
                    }
                }
ix152:;
            }
            {
                gm_b16_8 t204 = GM_LD(gm_b16_8, (((uint8_t *)&F.l16) + 0));
                gm_l20 = t204;
            }
            {
                gm_l21 = ((uint64_t)0ULL);
            }
            {
                {
                    gm_l23 = ((uint64_t)0ULL);
                }
                {
                    uint64_t t206 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                    gm_l22 = t206;
                }
                {
                    for (;;) {
                        uint64_t t208 = gm_l23;
                        uint64_t t209 = gm_l22;
                        uint8_t t210 = (t208 < t209);
                        if (!t210) goto lb207;
                        {
                            uint64_t t213 = gm_l23;
                            uint64_t t216 = GM_LD(uint64_t, ((uint8_t *)&t213));
                            gm_l24 = t216;
                            {
                                uint64_t t218 = gm_l24;
                                uint64_t t219 = (uintptr_t)t218;
                                uint64_t t220 = GM_LD(uintptr_t, ((uint8_t *)&gm_l20) + 8);
                                if (t219 >= (uintptr_t)(t220)) gm_trap_index(t219, t220, gm_srcmok80ho8hh01, 900, 24);
                                uint64_t t221 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l20)) + t219 * 8));
                                gm_l25 = t221;
                            }
                            {
                                void * t222 = gm_l0;
                                void * t223 = GM_LD(void *, (((uint8_t *)t222) + 0));
                                void * t224 = (void *)(((uint8_t *)t223) + 8);
                                uint64_t t225 = gm_l24;
                                uint64_t t226 = (uintptr_t)t225;
                                uint64_t t227 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                                if (t226 >= (uintptr_t)(t227)) gm_trap_index(t226, t227, gm_srcmok80ho8hh01, 901, 36);
                                gm_t88rcucg1b7uk t228 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t226 * 88) + 0));
                                uint64_t t229 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t224, t228);
                                if (gm_unw.kind) {
                                    goto gm_ll211;
                                }
                                gm_l26 = t229;
                            }
                            {
                                uint64_t t231 = gm_l26;
                                uint8_t t232 = (t231 > ((uint64_t)0ULL));
                                if (!t232) goto ie230;
                                {
                                    uint64_t t235 = gm_l25;
                                    uint64_t t236 = gm_l21;
                                    uint8_t t237 = (t235 > t236);
                                    if (!t237) goto ie234;
                                    {
                                        void * t239 = gm_l0;
                                        gm_tgbgti82mf879 t240;
                                        gm_b16_8 t241 = gm_l2;
                                        GM_ST(gm_b16_8, (uint8_t *)&t240 + 0, t241);
                                        uint64_t t242 = gm_l21;
                                        GM_ST(uint64_t, (uint8_t *)&t240 + 16, t242);
                                        uint64_t t243 = gm_l25;
                                        uint64_t t244 = gm_l21;
                                        uint64_t r245; bool o246 = __builtin_sub_overflow(t243, t244, &r245);
                                        if (o246) gm_trap("integer overflow in `-`", gm_srcmok80ho8hh01, 903, 101);
                                        uint64_t t247 = r245;
                                        GM_ST(uint64_t, (uint8_t *)&t240 + 24, t247);
                                        gm_b0_1 t248 = gm_f_linef_pduqjh2((void *)NULL, cx, NULL, t239, &t240);
                                        if (gm_unw.kind) {
                                            goto gm_ll211;
                                        }
                                    }
                                    goto ix234;
ie234:;
ix234:;
                                }
                                {
                                    uint64_t t249 = gm_l25;
                                    uint64_t t250 = gm_l26;
                                    uint64_t r251; bool o252 = __builtin_add_overflow(t249, t250, &r251);
                                    if (o252) gm_trap("integer overflow in `+`", gm_srcmok80ho8hh01, 904, 30);
                                    uint64_t t253 = r251;
                                    gm_l21 = t253;
                                }
                                goto ix230;
ie230:;
ix230:;
                            }
                            goto me214;
mn215:;
                            gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 899, 13);
me214:;
                        }
lc207:;
                        {
                            uint64_t t255 = gm_l23;
                            uint64_t r256; bool o257 = __builtin_add_overflow(t255, ((uint64_t)1ULL), &r256);
                            uint64_t t258 = r256;
                            gm_l23 = t258;
                        }
                    }
gm_ll211:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb207; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc207; }
                    }
                    goto gm_lad;
lb207:;
                }
            }
            {
                void * t259 = gm_l0;
                void * t260 = GM_LD(void *, (((uint8_t *)t259) + 0));
                void * t261 = (void *)(((uint8_t *)t260) + 8);
                void * t262 = gm_l1;
                gm_t88rcucg1b7uk t263 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t262) + 0));
                uint64_t t264 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t261, t263);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l27 = t264;
            }
            {
                uint64_t t266 = gm_l27;
                uint64_t t267 = gm_l21;
                uint8_t t268 = (t266 > t267);
                if (!t268) goto ie265;
                {
                    void * t270 = gm_l0;
                    gm_tgbgti82mf879 t271;
                    gm_b16_8 t272 = gm_l2;
                    GM_ST(gm_b16_8, (uint8_t *)&t271 + 0, t272);
                    uint64_t t273 = gm_l21;
                    GM_ST(uint64_t, (uint8_t *)&t271 + 16, t273);
                    uint64_t t274 = gm_l27;
                    uint64_t t275 = gm_l21;
                    uint64_t r276; bool o277 = __builtin_sub_overflow(t274, t275, &r276);
                    if (o277) gm_trap("integer overflow in `-`", gm_srcmok80ho8hh01, 908, 95);
                    uint64_t t278 = r276;
                    GM_ST(uint64_t, (uint8_t *)&t271 + 24, t278);
                    gm_b0_1 t279 = gm_f_linef_pduqjh2((void *)NULL, cx, NULL, t270, &t271);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                goto ix265;
ie265:;
ix265:;
            }
            {
                uint8_t t281 = gm_l15;
                if (!t281) goto ie280;
                {
                    {
                        gm_l29 = ((uint64_t)0ULL);
                    }
                    {
                        uint64_t t284 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                        gm_l28 = t284;
                    }
                    {
                        for (;;) {
                            uint64_t t286 = gm_l29;
                            uint64_t t287 = gm_l28;
                            uint8_t t288 = (t286 < t287);
                            if (!t288) goto lb285;
                            {
                                uint64_t t291 = gm_l29;
                                uint64_t t294 = GM_LD(uint64_t, ((uint8_t *)&t291));
                                gm_l30 = t294;
                                {
                                    uint64_t t296 = gm_l30;
                                    uint64_t t297 = (uintptr_t)t296;
                                    uint64_t t298 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                                    if (t297 >= (uintptr_t)(t298)) gm_trap_index(t297, t298, gm_srcmok80ho8hh01, 911, 27);
                                    void * t299 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t297 * 88);
                                    gm_l31 = t299;
                                }
                                {
                                    uint64_t t300 = gm_l30;
                                    uint64_t t301 = (uintptr_t)t300;
                                    uint64_t t302 = GM_LD(uintptr_t, ((uint8_t *)&gm_l20) + 8);
                                    if (t301 >= (uintptr_t)(t302)) gm_trap_index(t301, t302, gm_srcmok80ho8hh01, 912, 28);
                                    uint64_t t303 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l20)) + t301 * 8));
                                    gm_l32 = t303;
                                }
                                {
                                    void * t304 = gm_l0;
                                    void * t305 = GM_LD(void *, (((uint8_t *)t304) + 0));
                                    void * t306 = (void *)(((uint8_t *)t305) + 8);
                                    void * t307 = gm_l31;
                                    gm_t88rcucg1b7uk t308 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t307) + 0));
                                    gm_b16_8 t309 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t306, t308);
                                    if (gm_unw.kind) {
                                        goto gm_ll289;
                                    }
                                    gm_l33 = t309;
                                }
                                {
                                    void * t311 = gm_l0;
                                    void * t312 = gm_l31;
                                    gm_tnrltbc61i6v1 t313 = gm_l3;
                                    uint8_t t314 = gm_f_nests_in_place_jc41ehn((void *)NULL, cx, NULL, t311, t312, t313);
                                    if (gm_unw.kind) {
                                        goto gm_ll289;
                                    }
                                    if (!t314) goto ie310;
                                    {
                                        gm_tftksr4op114e t316;
                                        gm_b16_8 t317 = gm_l33;
                                        GM_ST(gm_b16_8, (uint8_t *)&t316 + 0, t317);
                                        gm_b16_8 t318 = gm_l2;
                                        GM_ST(gm_b16_8, (uint8_t *)&t316 + 16, t318);
                                        uint64_t t319 = gm_l32;
                                        GM_ST(uint64_t, (uint8_t *)&t316 + 32, t319);
                                        gm_tabb710d9mq2e t320;
                                        gm_f_format_e1d93qv((void *)&t320, (void *)NULL, cx, NULL, &t316);
                                        if (gm_unw.kind) {
                                            goto gm_ll289;
                                        }
                                        if (((((uint8_t *)&t320))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t320) + 8, gm_srcmok80ho8hh01, 915, 32);
                                        gm_b16_8 t321 = GM_LD(gm_b16_8, (((uint8_t *)&t320) + 8));
                                        gm_l34 = t321;
                                    }
                                    {
                                        void * t322 = gm_l0;
                                        void * t323 = gm_l31;
                                        gm_b16_8 t324 = gm_l34;
                                        gm_b16_8 t325 = GM_LD(gm_b16_8, ((uint8_t *)&t324));
                                        gm_tnrltbc61i6v1 t326 = gm_l3;
                                        gm_tqgtd5gtgj65s t327;
                                        gm_f_expr_into_dn9kavo((void *)&t327, (void *)NULL, cx, NULL, t322, t323, t325, t326);
                                        if (gm_unw.kind) {
                                            goto gm_ll289;
                                        }
                                        if (((((uint8_t *)&t327))[0] != 0)) {
                                            gm_tqgtd5gtgj65s t328;
                                            GM_ST(uint8_t, (uint8_t *)&t328 + 1, 0);
                                            GM_ST(uint16_t, (uint8_t *)&t328 + 2, 0);
                                            GM_ST(uint32_t, (uint8_t *)&t328 + 4, 0);
                                            ((uint8_t *)&t328)[0] = 1;
                                            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t328 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t327) + 8)));
                                            F.ret = t328;
                                            goto gm_fret;
                                        }
                                        gm_b0_1 t329 = GM_LD(gm_b0_1, (((uint8_t *)&t327) + 8));
                                    }
                                    goto ix310;
ie310:;
                                    void * t331 = gm_l0;
                                    void * t332 = GM_LD(void *, (((uint8_t *)t331) + 0));
                                    void * t333 = (void *)(((uint8_t *)t332) + 8);
                                    void * t334 = gm_l31;
                                    gm_t88rcucg1b7uk t335 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t334) + 0));
                                    uint8_t t336 = gm_f_by_ref_fi0o860((void *)NULL, cx, NULL, t333, t335);
                                    if (gm_unw.kind) {
                                        goto gm_ll289;
                                    }
                                    if (!t336) goto ie330;
                                    void * t337 = gm_l0;
                                    void * t338 = gm_l31;
                                    uint8_t t339 = gm_f_reads_place_nji55rm((void *)NULL, cx, NULL, t337, t338);
                                    if (gm_unw.kind) {
                                        goto gm_ll289;
                                    }
                                    if (!t339) goto ie330;
                                    {
                                        void * t341 = gm_l0;
                                        void * t342 = gm_l31;
                                        gm_t5o0tpv5jka2l t343;
                                        gm_f_place_addr_7ji15ot((void *)&t343, (void *)NULL, cx, NULL, t341, t342);
                                        if (gm_unw.kind) {
                                            goto gm_ll289;
                                        }
                                        if (((((uint8_t *)&t343))[0] != 0)) {
                                            gm_tqgtd5gtgj65s t344;
                                            GM_ST(uint8_t, (uint8_t *)&t344 + 1, 0);
                                            GM_ST(uint16_t, (uint8_t *)&t344 + 2, 0);
                                            GM_ST(uint32_t, (uint8_t *)&t344 + 4, 0);
                                            ((uint8_t *)&t344)[0] = 1;
                                            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t344 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t343) + 8)));
                                            F.ret = t344;
                                            goto gm_fret;
                                        }
                                        gm_b16_8 t345 = GM_LD(gm_b16_8, (((uint8_t *)&t343) + 8));
                                        gm_l35 = t345;
                                    }
                                    {
                                        void * t346 = gm_l0;
                                        void * t347 = GM_LD(void *, (((uint8_t *)t346) + 0));
                                        void * t348 = (void *)(((uint8_t *)t347) + 8);
                                        void * t349 = gm_l31;
                                        gm_t88rcucg1b7uk t350 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t349) + 0));
                                        uint64_t t351 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t348, t350);
                                        if (gm_unw.kind) {
                                            goto gm_ll289;
                                        }
                                        gm_l36 = t351;
                                    }
                                    {
                                        void * t352 = gm_l0;
                                        gm_thddbqb6snabm t353;
                                        gm_b16_8 t354 = gm_l2;
                                        GM_ST(gm_b16_8, (uint8_t *)&t353 + 0, t354);
                                        uint64_t t355 = gm_l32;
                                        GM_ST(uint64_t, (uint8_t *)&t353 + 16, t355);
                                        gm_b16_8 t356 = gm_l35;
                                        GM_ST(gm_b16_8, (uint8_t *)&t353 + 24, t356);
                                        uint64_t t357 = gm_l36;
                                        GM_ST(uint64_t, (uint8_t *)&t353 + 40, t357);
                                        gm_b0_1 t358 = gm_f_linef_thnd8mb((void *)NULL, cx, NULL, t352, &t353);
                                        if (gm_unw.kind) {
                                            goto gm_ll289;
                                        }
                                    }
                                    goto ix330;
ie330:;
                                    {
                                        void * t360 = gm_l0;
                                        void * t361 = gm_l31;
                                        gm_t5o0tpv5jka2l t362;
                                        gm_f_expr_porbc9t((void *)&t362, (void *)NULL, cx, NULL, t360, t361);
                                        if (gm_unw.kind) {
                                            goto gm_ll289;
                                        }
                                        if (((((uint8_t *)&t362))[0] != 0)) {
                                            gm_tqgtd5gtgj65s t363;
                                            GM_ST(uint8_t, (uint8_t *)&t363 + 1, 0);
                                            GM_ST(uint16_t, (uint8_t *)&t363 + 2, 0);
                                            GM_ST(uint32_t, (uint8_t *)&t363 + 4, 0);
                                            ((uint8_t *)&t363)[0] = 1;
                                            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t363 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t362) + 8)));
                                            F.ret = t363;
                                            goto gm_fret;
                                        }
                                        gm_b16_8 t364 = GM_LD(gm_b16_8, (((uint8_t *)&t362) + 8));
                                        gm_l37 = t364;
                                    }
                                    {
                                        void * t365 = gm_l0;
                                        gm_tmr5oep1578tb t366;
                                        gm_b16_8 t367 = gm_l33;
                                        GM_ST(gm_b16_8, (uint8_t *)&t366 + 0, t367);
                                        gm_b16_8 t368 = gm_l2;
                                        GM_ST(gm_b16_8, (uint8_t *)&t366 + 16, t368);
                                        uint64_t t369 = gm_l32;
                                        GM_ST(uint64_t, (uint8_t *)&t366 + 32, t369);
                                        gm_b16_8 t370 = gm_l37;
                                        GM_ST(gm_b16_8, (uint8_t *)&t366 + 40, t370);
                                        gm_b0_1 t371 = gm_f_linef_8i4929c((void *)NULL, cx, NULL, t365, &t366);
                                        if (gm_unw.kind) {
                                            goto gm_ll289;
                                        }
                                    }
ix330:;
ix310:;
                                }
                                goto me292;
mn293:;
                                gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 910, 17);
me292:;
                            }
lc285:;
                            {
                                uint64_t t373 = gm_l29;
                                uint64_t r374; bool o375 = __builtin_add_overflow(t373, ((uint64_t)1ULL), &r374);
                                uint64_t t376 = r374;
                                gm_l29 = t376;
                            }
                        }
gm_ll289:;
                        if (gm_unw.frame == &F.h && gm_unw.label == 3) {
                            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb285; }
                            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc285; }
                        }
                        goto gm_lad;
lb285:;
                    }
                }
                goto ix280;
ie280:;
                {
                    gm_f_list_epvducg((void *)&F.l38, (void *)NULL, cx, NULL);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    {
                        gm_l40 = ((uint64_t)0ULL);
                    }
                    {
                        uint64_t t379 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                        gm_l39 = t379;
                    }
                    {
                        for (;;) {
                            uint64_t t381 = gm_l40;
                            uint64_t t382 = gm_l39;
                            uint8_t t383 = (t381 < t382);
                            if (!t383) goto lb380;
                            {
                                uint64_t t386 = gm_l40;
                                uint64_t t389 = GM_LD(uint64_t, ((uint8_t *)&t386));
                                gm_l41 = t389;
                                {
                                    void * t391 = gm_l0;
                                    uint64_t t392 = gm_l41;
                                    uint64_t t393 = (uintptr_t)t392;
                                    uint64_t t394 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                                    if (t393 >= (uintptr_t)(t394)) gm_trap_index(t393, t394, gm_srcmok80ho8hh01, 930, 39);
                                    void * t395 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t393 * 88);
                                    gm_t5o0tpv5jka2l t396;
                                    gm_f_expr_porbc9t((void *)&t396, (void *)NULL, cx, NULL, t391, t395);
                                    if (gm_unw.kind) {
                                        goto gm_ll384;
                                    }
                                    if (((((uint8_t *)&t396))[0] != 0)) {
                                        gm_tqgtd5gtgj65s t397;
                                        GM_ST(uint8_t, (uint8_t *)&t397 + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t397 + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t397 + 4, 0);
                                        ((uint8_t *)&t397)[0] = 1;
                                        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t397 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t396) + 8)));
                                        F.ret = t397;
                                        goto gm_fret;
                                    }
                                    gm_b16_8 t398 = GM_LD(gm_b16_8, (((uint8_t *)&t396) + 8));
                                    gm_l42 = t398;
                                }
                                {
                                    void * t399 = (void *)((uint8_t *)&F.l38);
                                    gm_b16_8 t400 = gm_l42;
                                    gm_tftb1odc739t5 t401 = gm_f_push_3ef746b(t399, t400);
                                    if (((((uint8_t *)&t401))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t401) + 1, gm_srcmok80ho8hh01, 931, 21);
                                    gm_b0_1 t402 = GM_LD(gm_b0_1, (((uint8_t *)&t401) + 1));
                                }
                                goto me387;
mn388:;
                                gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 929, 17);
me387:;
                            }
lc380:;
                            {
                                uint64_t t404 = gm_l40;
                                uint64_t r405; bool o406 = __builtin_add_overflow(t404, ((uint64_t)1ULL), &r405);
                                uint64_t t407 = r405;
                                gm_l40 = t407;
                            }
                        }
gm_ll384:;
                        if (gm_unw.frame == &F.h && gm_unw.label == 4) {
                            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb380; }
                            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc380; }
                        }
                        goto gm_lad;
lb380:;
                    }
                }
                {
                    {
                        gm_l44 = ((uint64_t)0ULL);
                    }
                    {
                        uint64_t t409 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                        gm_l43 = t409;
                    }
                    {
                        for (;;) {
                            uint64_t t411 = gm_l44;
                            uint64_t t412 = gm_l43;
                            uint8_t t413 = (t411 < t412);
                            if (!t413) goto lb410;
                            {
                                uint64_t t416 = gm_l44;
                                uint64_t t419 = GM_LD(uint64_t, ((uint8_t *)&t416));
                                gm_l45 = t419;
                                {
                                    void * t421 = gm_l0;
                                    void * t422 = GM_LD(void *, (((uint8_t *)t421) + 0));
                                    void * t423 = (void *)(((uint8_t *)t422) + 8);
                                    uint64_t t424 = gm_l45;
                                    uint64_t t425 = (uintptr_t)t424;
                                    uint64_t t426 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                                    if (t425 >= (uintptr_t)(t426)) gm_trap_index(t425, t426, gm_srcmok80ho8hh01, 934, 38);
                                    gm_t88rcucg1b7uk t427 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t425 * 88) + 0));
                                    gm_b16_8 t428 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t423, t427);
                                    if (gm_unw.kind) {
                                        goto gm_ll414;
                                    }
                                    gm_l46 = t428;
                                }
                                {
                                    void * t429 = gm_l0;
                                    gm_tmr5oep1578tb t430;
                                    gm_b16_8 t431 = gm_l46;
                                    GM_ST(gm_b16_8, (uint8_t *)&t430 + 0, t431);
                                    gm_b16_8 t432 = gm_l2;
                                    GM_ST(gm_b16_8, (uint8_t *)&t430 + 16, t432);
                                    uint64_t t433 = gm_l45;
                                    uint64_t t434 = (uintptr_t)t433;
                                    uint64_t t435 = GM_LD(uintptr_t, ((uint8_t *)&gm_l20) + 8);
                                    if (t434 >= (uintptr_t)(t435)) gm_trap_index(t434, t435, gm_srcmok80ho8hh01, 935, 78);
                                    uint64_t t436 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l20)) + t434 * 8));
                                    GM_ST(uint64_t, (uint8_t *)&t430 + 32, t436);
                                    uint64_t t437 = gm_l45;
                                    uint64_t t438 = (uintptr_t)t437;
                                    uint64_t t439 = GM_LD(uintptr_t, (((uint8_t *)&F.l38) + 0) + 8);
                                    if (t438 >= (uintptr_t)(t439)) gm_trap_index(t438, t439, gm_srcmok80ho8hh01, 935, 87);
                                    gm_b16_8 t440 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l38) + 0)) + t438 * 16));
                                    GM_ST(gm_b16_8, (uint8_t *)&t430 + 40, t440);
                                    gm_b0_1 t441 = gm_f_linef_8i4929c((void *)NULL, cx, NULL, t429, &t430);
                                    if (gm_unw.kind) {
                                        goto gm_ll414;
                                    }
                                }
                                goto me417;
mn418:;
                                gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 933, 17);
me417:;
                            }
lc410:;
                            {
                                uint64_t t443 = gm_l44;
                                uint64_t r444; bool o445 = __builtin_add_overflow(t443, ((uint64_t)1ULL), &r444);
                                uint64_t t446 = r444;
                                gm_l44 = t446;
                            }
                        }
gm_ll414:;
                        if (gm_unw.frame == &F.h && gm_unw.label == 5) {
                            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb410; }
                            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc410; }
                        }
                        goto gm_lad;
lb410:;
                    }
                }
ix280:;
            }
            goto me30;
mn66:;
ma33:;
            uint8_t t448 = GM_LD(uint8_t, ((((uint8_t *)t29) + 8) + 0));
            if (t448 != ((uint8_t)32ULL)) goto mn447;
            gm_tmn7n0n9raou5 t449 = GM_LD(gm_tmn7n0n9raou5, (((((uint8_t *)t29) + 8) + 8) + 0));
            gm_l47 = t449;
            void * t450 = GM_LD(void *, (((((uint8_t *)t29) + 8) + 8) + 8));
            gm_l48 = t450;
            void * t451 = gm_l0;
            void * t452 = gm_l1;
            gm_t88rcucg1b7uk t453 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t452) + 0));
            uint8_t t454 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l47) + 0) == ((uint8_t)2ULL)));
            void * t455 = gm_l48;
            gm_b16_8 t456 = gm_l2;
            gm_tnrltbc61i6v1 t457 = gm_l3;
            gm_tqgtd5gtgj65s t458;
            gm_f_wrap_into_lm24d33((void *)&t458, (void *)NULL, cx, NULL, t451, t453, t454, ((uint8_t)0), t455, t456, t457);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t458))[0] != 0)) {
                gm_tqgtd5gtgj65s t459;
                GM_ST(uint8_t, (uint8_t *)&t459 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t459 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t459 + 4, 0);
                ((uint8_t *)&t459)[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t459 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t458) + 8)));
                F.ret = t459;
                goto gm_fret;
            }
            gm_b0_1 t460 = GM_LD(gm_b0_1, (((uint8_t *)&t458) + 8));
            goto me30;
mn447:;
ma34:;
            uint8_t t462 = GM_LD(uint8_t, ((((uint8_t *)t29) + 8) + 0));
            if (t462 != ((uint8_t)39ULL)) goto mn461;
            void * t463 = GM_LD(void *, ((((uint8_t *)t29) + 8) + 8));
            gm_l49 = t463;
            void * t464 = gm_l0;
            void * t465 = gm_l1;
            gm_t88rcucg1b7uk t466 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t465) + 0));
            void * t467 = gm_l49;
            gm_b16_8 t468 = gm_l2;
            gm_tnrltbc61i6v1 t469 = gm_l3;
            gm_tqgtd5gtgj65s t470;
            gm_f_wrap_into_lm24d33((void *)&t470, (void *)NULL, cx, NULL, t464, t466, ((uint8_t)0), ((uint8_t)1), t467, t468, t469);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t470))[0] != 0)) {
                gm_tqgtd5gtgj65s t471;
                GM_ST(uint8_t, (uint8_t *)&t471 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t471 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t471 + 4, 0);
                ((uint8_t *)&t471)[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t471 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t470) + 8)));
                F.ret = t471;
                goto gm_fret;
            }
            gm_b0_1 t472 = GM_LD(gm_b0_1, (((uint8_t *)&t470) + 8));
            goto me30;
mn461:;
ma35:;
            uint8_t t474 = GM_LD(uint8_t, ((((uint8_t *)t29) + 8) + 0));
            if (t474 != ((uint8_t)38ULL)) goto mn473;
            gm_ttggq7ng4kp3p t475 = GM_LD(gm_ttggq7ng4kp3p, ((((uint8_t *)t29) + 8) + 8));
            gm_l50 = t475;
            void * t476 = gm_l0;
            void * t477 = gm_l1;
            gm_t88rcucg1b7uk t478 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t477) + 0));
            void * t479 = (void *)((uint8_t *)&gm_l50);
            gm_b16_8 t480 = gm_l2;
            gm_tnrltbc61i6v1 t481 = gm_l3;
            gm_tqgtd5gtgj65s t482;
            gm_f_variant_into_2smodtg((void *)&t482, (void *)NULL, cx, NULL, t476, t478, t479, t480, t481);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t482))[0] != 0)) {
                gm_tqgtd5gtgj65s t483;
                GM_ST(uint8_t, (uint8_t *)&t483 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t483 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t483 + 4, 0);
                ((uint8_t *)&t483)[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t483 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t482) + 8)));
                F.ret = t483;
                goto gm_fret;
            }
            gm_b0_1 t484 = GM_LD(gm_b0_1, (((uint8_t *)&t482) + 8));
            goto me30;
mn473:;
ma36:;
            uint8_t t486 = GM_LD(uint8_t, ((((uint8_t *)t29) + 8) + 0));
            if (t486 != ((uint8_t)46ULL)) goto mn485;
            void * t487 = GM_LD(void *, ((((uint8_t *)t29) + 8) + 8));
            gm_l51 = t487;
            {
                void * t489 = gm_l0;
                gm_f_block_scope_e3cgusu((void *)&F.l52, (void *)NULL, cx, NULL, t489);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t490 = gm_l0;
                void * t491 = (void *)(((uint8_t *)t490) + 240);
                gm_tftb1odc739t5 t492 = gm_f_push_pcqb56b(t491, &(*(gm_tehgl6lkqgvt0 *)((uint8_t *)&F.l52)));
                if (((((uint8_t *)&t492))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t492) + 1, gm_srcmok80ho8hh01, 944, 13);
                gm_b0_1 t493 = GM_LD(gm_b0_1, (((uint8_t *)&t492) + 1));
            }
            {
                void * t494 = gm_l0;
                void * t495 = gm_l51;
                gm_tqgtd5gtgj65s t496;
                gm_f_stmts_1199hll((void *)&t496, (void *)NULL, cx, NULL, t494, t495);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t496))[0] != 0)) {
                    gm_tqgtd5gtgj65s t497;
                    GM_ST(uint8_t, (uint8_t *)&t497 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t497 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t497 + 4, 0);
                    ((uint8_t *)&t497)[0] = 1;
                    GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t497 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t496) + 8)));
                    F.ret = t497;
                    goto gm_fret;
                }
                gm_b0_1 t498 = GM_LD(gm_b0_1, (((uint8_t *)&t496) + 8));
            }
            {
                void * t499 = gm_l51;
                void * t500 = GM_LD(void *, (((uint8_t *)t499) + 16));
                if (!(GM_LD(void *, ((uint8_t *)&t500)) != 0)) gm_trap("unwrap of null", gm_srcmok80ho8hh01, 946, 18);
                void * t501 = GM_LD(void *, ((uint8_t *)&t500));
                gm_l53 = t501;
            }
            {
                void * t502 = gm_l0;
                void * t503 = gm_l53;
                gm_tl874t2atnlh3 t504 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t503) + 72));
                gm_b0_1 t505 = gm_f_src_line_d24sajl((void *)NULL, cx, NULL, t502, t504);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t506 = gm_l0;
                void * t507 = gm_l53;
                gm_b16_8 t508 = gm_l2;
                gm_tnrltbc61i6v1 t509 = gm_l3;
                gm_tqgtd5gtgj65s t510;
                gm_f_expr_into_dn9kavo((void *)&t510, (void *)NULL, cx, NULL, t506, t507, t508, t509);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t510))[0] != 0)) {
                    gm_tqgtd5gtgj65s t511;
                    GM_ST(uint8_t, (uint8_t *)&t511 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t511 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t511 + 4, 0);
                    ((uint8_t *)&t511)[0] = 1;
                    GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t511 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t510) + 8)));
                    F.ret = t511;
                    goto gm_fret;
                }
                gm_b0_1 t512 = GM_LD(gm_b0_1, (((uint8_t *)&t510) + 8));
            }
            {
                void * t513 = gm_l0;
                void * t514 = (void *)(((uint8_t *)t513) + 240);
                gm_t1g28mkc78m0c t515;
                gm_f_pop_g2usrvd((void *)&t515, t514);
                if (!((((uint8_t *)&t515))[104] != 0)) gm_trap("unwrap of null", gm_srcmok80ho8hh01, 949, 23);
                F.l54 = (*(gm_tehgl6lkqgvt0 *)(((uint8_t *)&t515) + 0));
            }
            {
                void * t516 = gm_l0;
                gm_b0_1 t517 = gm_f_end_scope_45ar5qv((void *)NULL, cx, NULL, t516, &(*(gm_tehgl6lkqgvt0 *)((uint8_t *)&F.l54)), ((uint8_t)1));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto me30;
mn485:;
ma37:;
            gm_trap("reached `@unreachable()`", gm_srcmok80ho8hh01, 952, 14);
            goto me30;
mn518:;
ma38:;
            gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 863, 5);
me30:;
        }
    }
    gm_b0_1 t519;
    memset(&(*(gm_tqgtd5gtgj65s *)gm_out), 0, sizeof (*(gm_tqgtd5gtgj65s *)gm_out));
    ((uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out))[0] = 0;
    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 8, t519);
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
inline __attribute__((aligned(4))) uint8_t gm_f_fixed_address_5usq50j(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fixed_address_5usq50j F;
    void * gm_l0; /* place */
    void * gm_l1; /* base */
    GM_STACK_CHECK(&F, 52u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        uint8_t t8 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        switch (t8) { case ((uint8_t)0ULL): goto ma6; case ((uint8_t)1ULL): goto ma6; case ((uint8_t)2ULL): goto ma6; case ((uint8_t)3ULL): goto ma6; case ((uint8_t)4ULL): goto ma6; case ((uint8_t)5ULL): goto ma6; case ((uint8_t)6ULL): goto ma6; case ((uint8_t)7ULL): goto ma6; case ((uint8_t)8ULL): goto ma6; case ((uint8_t)9ULL): goto ma6; case ((uint8_t)10ULL): goto ma6; case ((uint8_t)11ULL): goto ma6; case ((uint8_t)12ULL): goto ma6; case ((uint8_t)13ULL): goto ma6; case ((uint8_t)14ULL): goto ma4; case ((uint8_t)15ULL): goto ma4; case ((uint8_t)16ULL): goto ma4; case ((uint8_t)17ULL): goto ma4; case ((uint8_t)18ULL): goto ma6; case ((uint8_t)19ULL): goto ma5; case ((uint8_t)20ULL): goto ma6; case ((uint8_t)21ULL): goto ma6; case ((uint8_t)22ULL): goto ma6; case ((uint8_t)23ULL): goto ma6; case ((uint8_t)24ULL): goto ma6; case ((uint8_t)25ULL): goto ma6; case ((uint8_t)26ULL): goto ma6; case ((uint8_t)27ULL): goto ma6; case ((uint8_t)28ULL): goto ma6; case ((uint8_t)29ULL): goto ma6; case ((uint8_t)30ULL): goto ma6; case ((uint8_t)31ULL): goto ma6; case ((uint8_t)32ULL): goto ma6; case ((uint8_t)33ULL): goto ma6; case ((uint8_t)34ULL): goto ma6; case ((uint8_t)35ULL): goto ma6; case ((uint8_t)36ULL): goto ma6; case ((uint8_t)37ULL): goto ma6; case ((uint8_t)38ULL): goto ma6; case ((uint8_t)39ULL): goto ma6; case ((uint8_t)40ULL): goto ma6; case ((uint8_t)41ULL): goto ma6; case ((uint8_t)42ULL): goto ma6; case ((uint8_t)43ULL): goto ma6; case ((uint8_t)44ULL): goto ma6; case ((uint8_t)45ULL): goto ma6; case ((uint8_t)46ULL): goto ma6; case ((uint8_t)47ULL): goto ma6; case ((uint8_t)48ULL): goto ma6; case ((uint8_t)49ULL): goto ma6; case ((uint8_t)50ULL): goto ma6; case ((uint8_t)51ULL): goto ma6; case ((uint8_t)52ULL): goto ma6; case ((uint8_t)53ULL): goto ma6; case ((uint8_t)54ULL): goto ma6; case ((uint8_t)55ULL): goto ma6; case ((uint8_t)56ULL): goto ma6; case ((uint8_t)57ULL): goto ma6; case ((uint8_t)58ULL): goto ma6; default: goto ma6; }
ma4:;
        uint8_t t12 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t12 != ((uint8_t)15ULL)) goto pa11;
        goto po10;
pa11:;
        uint8_t t14 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t14 != ((uint8_t)17ULL)) goto pa13;
        goto po10;
pa13:;
        uint8_t t16 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t16 != ((uint8_t)14ULL)) goto pa15;
        goto po10;
pa15:;
        uint8_t t18 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t18 != ((uint8_t)16ULL)) goto pa17;
        goto po10;
pa17:;
        goto mn9;
po10:;
        uint8_t t19 = 0;
        F.ret = ((uint8_t)1);
        goto gm_fret;
        goto me3;
mn9:;
ma5:;
        uint8_t t21 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t21 != ((uint8_t)19ULL)) goto mn20;
        void * t22 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l1 = t22;
        void * t23 = gm_l1;
        uint8_t t24 = gm_f_fixed_address_5usq50j(t23);
        uint8_t t25 = 0;
        F.ret = t24;
        goto gm_fret;
        goto me3;
mn20:;
ma6:;
        uint8_t t27 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
        goto me3;
mn26:;
ma7:;
        gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 1312, 5);
me3:;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) uint8_t gm_f_fn_480otki(void * a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_5evrfa7(void *env, gm_node *cx, gm_node *inst, void * a0);
extern __attribute__((aligned(4))) uint8_t gm_f_fn_i2el8vi(void * a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_jjarlg6(void *env, gm_node *cx, gm_node *inst, void * a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_kssb0q2(void *env, gm_node *cx, gm_node *inst, void * a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_v0t9sik(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_frame_field_nj8m22n(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2) {
    struct gm_fr_frame_field_nj8m22n F;
    void * gm_l0; /* fg */
    uint32_t gm_l1; /* levels */
    gm_b16_8 gm_l2; /* field */
    gm_b16_8 gm_l3; /* p */
    uint32_t gm_l4; /* f */
    GM_STACK_CHECK(&F, 407u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        uint32_t t3 = gm_l1;
        uint8_t t4 = ((t3 == ((uint32_t)0ULL)));
        if (!t4) goto ie2;
        {
            gm_b16_8 t6 = gm_l2;
            gm_tnpifv20irnph t7;
            GM_ST(gm_b16_8, ((uint8_t *)&t7 + 0), t6);
            gm_tabb710d9mq2e t8;
            gm_f_format_rmu2e6q((void *)&t8, (void *)NULL, cx, NULL, t7);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t8))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t8) + 8, gm_srcmok80ho8hh01, 588, 29);
            gm_b16_8 t9 = GM_LD(gm_b16_8, (((uint8_t *)&t8) + 8));
            gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
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
        uint32_t t13 = gm_l1;
        gm_tommf4j9upomc t14;
        gm_f_frame_up_g8486eu((void *)&t14, (void *)NULL, cx, NULL, t12, t13);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tommf4j9upomc t15 = t14;
        gm_l3 = GM_LD(gm_b16_8, (uint8_t *)&t15 + 0);
        gm_l4 = GM_LD(uint32_t, (uint8_t *)&t15 + 16);
    }
    {
        gm_tfeab279c16rr t16;
        void * t17 = gm_l0;
        void * t18 = GM_LD(void *, (((uint8_t *)t17) + 0));
        uint32_t t19 = gm_l4;
        gm_b16_8 t20 = gm_f_frame_name_stddmgg((void *)NULL, cx, NULL, t18, t19);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t16 + 0, t20);
        gm_b16_8 t21 = gm_l3;
        GM_ST(gm_b16_8, (uint8_t *)&t16 + 16, t21);
        gm_b16_8 t22 = gm_l2;
        GM_ST(gm_b16_8, (uint8_t *)&t16 + 32, t22);
        gm_tabb710d9mq2e t23;
        gm_f_format_lufljqc((void *)&t23, (void *)NULL, cx, NULL, &t16);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t23))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t23) + 8, gm_srcmok80ho8hh01, 590, 12);
        gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&t23) + 8));
        gm_b16_8 t25 = GM_LD(gm_b16_8, ((uint8_t *)&t24));
        uint8_t t26 = 0;
        F.ret = t25;
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
__attribute__((aligned(4))) gm_b16_8 gm_f_frame_locals_mt4nb6o(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_frame_locals_mt4nb6o F;
    void * gm_l0; /* g */
    uint32_t gm_l1; /* f */
    uint64_t gm_l2; /* at */
    gm_b16_8 gm_l3; /* known */
    void * gm_l4; /* def */
    uint8_t gm_l6; /* all */
    void * gm_l7; /* b */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    uint8_t gm_l11; /* filc */
    gm_b16_8 gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    uint64_t gm_l14; /* i */
    GM_STACK_CHECK(&F, 787u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint32_t t2 = gm_l1;
        uint64_t t3 = (uint64_t)t2;
        gm_l2 = t3;
    }
    {
        for (;;) {
            void * t5 = gm_l0;
            uint64_t t6 = GM_LD(uintptr_t, ((((uint8_t *)t5) + 2960) + 0) + 8);
            uint64_t t7 = gm_l2;
            uint8_t t8 = (t6 <= t7);
            if (!t8) goto lb4;
            {
                void * t11 = gm_l0;
                void * t12 = (void *)(((uint8_t *)t11) + 2960);
                gm_th2lqbo77pvsc t13;
                memset(&t13, 0, sizeof t13);
                gm_tftb1odc739t5 t14 = gm_f_push_7dfimu1(t12, &t13);
                if (((((uint8_t *)&t14))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t14) + 1, gm_srcmok80ho8hh01, 1257, 42);
                gm_b0_1 t15 = GM_LD(gm_b0_1, (((uint8_t *)&t14) + 1));
            }
lc4:;
        }
lb4:;
    }
    {
        uint64_t t17 = gm_l2;
        uint64_t t18 = (uintptr_t)t17;
        void * t19 = gm_l0;
        uint64_t t20 = GM_LD(uintptr_t, ((((uint8_t *)t19) + 2960) + 0) + 8);
        if (t18 >= (uintptr_t)(t20)) gm_trap_index(t18, t20, gm_srcmok80ho8hh01, 1258, 17);
        if (!((((uint8_t *)GM_LD(void *, ((((uint8_t *)t19) + 2960) + 0)) + t18 * 24))[16] != 0)) goto ie16;
        gm_b16_8 t21 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t19) + 2960) + 0)) + t18 * 24));
        gm_l3 = t21;
        {
            gm_b16_8 t23 = gm_l3;
            uint8_t t24 = 0;
            F.ret = t23;
            goto gm_fret;
        }
        goto ix16;
ie16:;
ix16:;
    }
    {
        void * t25 = gm_l0;
        void * t26 = GM_LD(void *, (((uint8_t *)t25) + 0));
        uint32_t t27 = gm_l1;
        void * t28 = gm_f_func_in17r5n((void *)NULL, cx, NULL, t26, t27);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l4 = t28;
    }
    {
        gm_f_list_s83jn2q((void *)&F.l5, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint8_t t29 = 0;
        uint8_t t30 = 0;
        void * t31 = gm_l0;
        uint8_t t32 = GM_LD(uint8_t, ((((uint8_t *)t31) + 1136) + 1));
        if (!t32) {
            void * t33 = gm_l0;
            uint8_t t34 = GM_LD(uint8_t, ((((uint8_t *)t33) + 1136) + 2));
            t30 = t34;
        } else t30 = 1;
        if (!t30) {
            void * t35 = gm_l0;
            uint32_t t36 = gm_l1;
            uint8_t t37 = gm_f_outlines_2kvbvit((void *)NULL, cx, NULL, t35, t36);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            t29 = t37;
        } else t29 = 1;
        gm_l6 = t29;
    }
    {
        void * t39 = gm_l4;
        void * t40 = GM_LD(void *, (((uint8_t *)t39) + 152));
        if (!(GM_LD(void *, ((uint8_t *)&t40)) != 0)) goto ie38;
        void * t41 = GM_LD(void *, ((uint8_t *)&t40));
        gm_l7 = t41;
        {
            uint8_t t43 = 0;
            uint8_t t44 = gm_l6;
            if (!t44) {
                void * t45 = gm_l7;
                uint8_t t46 = gm_f_has_fiber_with_53c3jik(t45);
                t43 = t46;
            } else t43 = 1;
            gm_l6 = t43;
        }
        goto ix38;
ie38:;
ix38:;
    }
    {
        uint8_t t48 = gm_l6;
        if (!t48) goto ie47;
        {
            {
                void * t51 = gm_l4;
                gm_b16_8 t52 = GM_LD(gm_b16_8, (((uint8_t *)t51) + 48));
                gm_l8 = t52;
            }
            {
                gm_l9 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t54 = gm_l9;
                    uint64_t t55 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                    uint8_t t56 = (t54 < t55);
                    if (!t56) goto lb53;
                    {
                        uint64_t t59 = gm_l9;
                        uint64_t t60 = (uintptr_t)t59;
                        uint64_t t61 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                        {
                            void * t65 = (void *)((uint8_t *)&F.l5);
                            gm_tftb1odc739t5 t66 = gm_f_push_vd1290p(t65, ((uint8_t)1));
                            if (((((uint8_t *)&t66))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t66) + 1, gm_srcmok80ho8hh01, 1264, 31);
                            gm_b0_1 t67 = GM_LD(gm_b0_1, (((uint8_t *)&t66) + 1));
                        }
                        goto me62;
mn63:;
                        gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 1264, 9);
me62:;
                    }
lc53:;
                    {
                        uint64_t t69 = gm_l9;
                        uint64_t r70; bool o71 = __builtin_add_overflow(t69, ((uint64_t)1ULL), &r70);
                        uint64_t t72 = r70;
                        gm_l9 = t72;
                    }
                }
lb53:;
            }
        }
        goto ix47;
ie47:;
        {
            void * t74 = gm_l0;
            gm_f_captured_locals_glsvloj((void *)&F.l10, (void *)NULL, cx, NULL, t74);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t75 = gm_l0;
            uint8_t t76 = GM_LD(uint8_t, ((((((uint8_t *)t75) + 8) + 8) + 0) + 56));
            gm_l11 = t76;
        }
        {
            {
                void * t78 = gm_l4;
                gm_b16_8 t79 = GM_LD(gm_b16_8, (((uint8_t *)t78) + 48));
                gm_l12 = t79;
            }
            {
                gm_l13 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t81 = gm_l13;
                    uint64_t t82 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                    uint8_t t83 = (t81 < t82);
                    if (!t83) goto lb80;
                    {
                        gm_t5im2v50m3hk6 t86;
                        uint64_t t87 = gm_l13;
                        GM_ST(uint64_t, (uint8_t *)&t86 + 0, t87);
                        uint64_t t88 = gm_l13;
                        uint64_t t89 = (uintptr_t)t88;
                        uint64_t t90 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                        memcpy((uint8_t *)&t86 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l12)) + t89 * 40), 40);
                        uint64_t t93 = GM_LD(uint64_t, (((uint8_t *)&t86) + 0));
                        gm_l14 = t93;
                        F.l15 = (*(gm_tsoie1ppgdvcg *)(((uint8_t *)&t86) + 8));
                        {
                            void * t95 = (void *)((uint8_t *)&F.l5);
                            uint8_t t96 = 0;
                            uint8_t t97 = 0;
                            uint8_t t98 = gm_l11;
                            uint8_t t99 = (uint8_t)!t98;
                            if (t99) {
                                void * t100 = gm_l0;
                                void * t101 = (void *)(((uint8_t *)t100) + 8);
                                gm_t88rcucg1b7uk t102 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l15) + 16));
                                uint64_t t103 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t101, t102);
                                if (gm_unw.kind) {
                                    goto gm_ll84;
                                }
                                uint64_t t104 = GM_LD(uint64_t, gm_cbemal9c6bf);
                                uint8_t t105 = (t103 > t104);
                                t97 = t105;
                            } else t97 = 0;
                            if (!t97) {
                                void * t106 = (void *)((uint8_t *)&F.l10);
                                uint32_t t107 = gm_l1;
                                uint64_t t108 = gm_l14;
                                uint32_t t109 = (uint32_t)t108;
                                gm_tcc1rq4n1avb2 t110;
                                GM_ST(uint32_t, ((uint8_t *)&t110 + 0), t107);
                                GM_ST(uint32_t, ((uint8_t *)&t110 + 4), t109);
                                gm_tqs5p4n2tq19v t111 = gm_f_get_ag7vhtq(t106, t110);
                                uint8_t t112;
                                if (((((uint8_t *)&t111))[1] != 0)) goto mn114;
                                t112 = ((uint8_t)0);
                                goto me113;
mn114:;
                                t112 = ((uint8_t)1);
                                goto me113;
mn115:;
                                gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 1269, 70);
me113:;
                                t96 = t112;
                            } else t96 = 1;
                            gm_tftb1odc739t5 t116 = gm_f_push_vd1290p(t95, t96);
                            if (((((uint8_t *)&t116))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t116) + 1, gm_srcmok80ho8hh01, 1269, 13);
                            gm_b0_1 t117 = GM_LD(gm_b0_1, (((uint8_t *)&t116) + 1));
                        }
                        goto me91;
mn92:;
                        gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 1268, 9);
me91:;
                    }
lc80:;
                    {
                        uint64_t t119 = gm_l13;
                        uint64_t r120; bool o121 = __builtin_add_overflow(t119, ((uint64_t)1ULL), &r120);
                        uint64_t t122 = r120;
                        gm_l13 = t122;
                    }
                }
gm_ll84:;
                if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb80; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc80; }
                }
                goto gm_lad;
lb80:;
            }
        }
ix47:;
    }
    {
        uint64_t t123 = gm_l2;
        uint64_t t124 = (uintptr_t)t123;
        void * t125 = gm_l0;
        uint64_t t126 = GM_LD(uintptr_t, ((((uint8_t *)t125) + 2960) + 0) + 8);
        if (t124 >= (uintptr_t)(t126)) gm_trap_index(t124, t126, gm_srcmok80ho8hh01, 1272, 5);
        gm_b16_8 t127 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 0));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t125) + 2960) + 0)) + t124 * 24)) + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t125) + 2960) + 0)) + t124 * 24)) + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t125) + 2960) + 0)) + t124 * 24)) + 20, 0);
        ((uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t125) + 2960) + 0)) + t124 * 24)))[16] = 1;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t125) + 2960) + 0)) + t124 * 24)) + 0, t127);
    }
    {
        gm_b16_8 t128 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 0));
        uint8_t t129 = 0;
        F.ret = t128;
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
__attribute__((aligned(4))) uint8_t gm_f_frame_pure_cbdra2q(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_frame_pure_cbdra2q F;
    void * gm_l0; /* fg */
    void * gm_l1; /* e */
    void * gm_l2; /* base */
    void * gm_l3; /* base */
    void * gm_l4; /* index */
    void * gm_l5; /* operand */
    void * gm_l6; /* lhs */
    void * gm_l7; /* rhs */
    gm_b16_8 gm_l8; /* elems */
    gm_b16_8 gm_l9; /* elems */
    uint64_t gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* i */
    GM_STACK_CHECK(&F, 722u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l1;
        uint8_t t13 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        switch (t13) { case ((uint8_t)0ULL): goto ma4; case ((uint8_t)1ULL): goto ma4; case ((uint8_t)2ULL): goto ma11; case ((uint8_t)3ULL): goto ma11; case ((uint8_t)4ULL): goto ma4; case ((uint8_t)5ULL): goto ma4; case ((uint8_t)6ULL): goto ma4; case ((uint8_t)7ULL): goto ma4; case ((uint8_t)8ULL): goto ma4; case ((uint8_t)9ULL): goto ma11; case ((uint8_t)10ULL): goto ma11; case ((uint8_t)11ULL): goto ma11; case ((uint8_t)12ULL): goto ma11; case ((uint8_t)13ULL): goto ma11; case ((uint8_t)14ULL): goto ma4; case ((uint8_t)15ULL): goto ma5; case ((uint8_t)16ULL): goto ma11; case ((uint8_t)17ULL): goto ma11; case ((uint8_t)18ULL): goto ma11; case ((uint8_t)19ULL): goto ma6; case ((uint8_t)20ULL): goto ma7; case ((uint8_t)21ULL): goto ma11; case ((uint8_t)22ULL): goto ma11; case ((uint8_t)23ULL): goto ma11; case ((uint8_t)24ULL): goto ma11; case ((uint8_t)25ULL): goto ma11; case ((uint8_t)26ULL): goto ma11; case ((uint8_t)27ULL): goto ma11; case ((uint8_t)28ULL): goto ma11; case ((uint8_t)29ULL): goto ma8; case ((uint8_t)30ULL): goto ma9; case ((uint8_t)31ULL): goto ma11; case ((uint8_t)32ULL): goto ma11; case ((uint8_t)33ULL): goto ma11; case ((uint8_t)34ULL): goto ma11; case ((uint8_t)35ULL): goto ma10; case ((uint8_t)36ULL): goto ma10; case ((uint8_t)37ULL): goto ma11; case ((uint8_t)38ULL): goto ma11; case ((uint8_t)39ULL): goto ma11; case ((uint8_t)40ULL): goto ma11; case ((uint8_t)41ULL): goto ma11; case ((uint8_t)42ULL): goto ma11; case ((uint8_t)43ULL): goto ma11; case ((uint8_t)44ULL): goto ma11; case ((uint8_t)45ULL): goto ma11; case ((uint8_t)46ULL): goto ma11; case ((uint8_t)47ULL): goto ma11; case ((uint8_t)48ULL): goto ma11; case ((uint8_t)49ULL): goto ma11; case ((uint8_t)50ULL): goto ma11; case ((uint8_t)51ULL): goto ma11; case ((uint8_t)52ULL): goto ma11; case ((uint8_t)53ULL): goto ma11; case ((uint8_t)54ULL): goto ma11; case ((uint8_t)55ULL): goto ma11; case ((uint8_t)56ULL): goto ma11; case ((uint8_t)57ULL): goto ma11; case ((uint8_t)58ULL): goto ma11; default: goto ma11; }
ma4:;
        uint8_t t17 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t17 != ((uint8_t)0ULL)) goto pa16;
        goto po15;
pa16:;
        uint8_t t19 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t19 != ((uint8_t)1ULL)) goto pa18;
        goto po15;
pa18:;
        uint8_t t21 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t21 != ((uint8_t)4ULL)) goto pa20;
        goto po15;
pa20:;
        uint8_t t23 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t23 != ((uint8_t)7ULL)) goto pa22;
        goto po15;
pa22:;
        uint8_t t25 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t25 != ((uint8_t)5ULL)) goto pa24;
        goto po15;
pa24:;
        uint8_t t27 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t27 != ((uint8_t)6ULL)) goto pa26;
        goto po15;
pa26:;
        uint8_t t29 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t29 != ((uint8_t)8ULL)) goto pa28;
        goto po15;
pa28:;
        uint8_t t31 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t31 != ((uint8_t)14ULL)) goto pa30;
        goto po15;
pa30:;
        goto mn14;
po15:;
        uint8_t t32 = 0;
        F.ret = ((uint8_t)1);
        goto gm_fret;
        goto me3;
mn14:;
ma5:;
        uint8_t t34 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t34 != ((uint8_t)15ULL)) goto mn33;
        uint8_t t35 = 0;
        F.ret = ((uint8_t)1);
        goto gm_fret;
        goto me3;
mn33:;
ma6:;
        uint8_t t37 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t37 != ((uint8_t)19ULL)) goto mn36;
        void * t38 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l2 = t38;
        void * t39 = gm_l0;
        void * t40 = gm_l2;
        uint8_t t41 = gm_f_frame_pure_cbdra2q((void *)NULL, cx, NULL, t39, t40);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t42 = 0;
        F.ret = t41;
        goto gm_fret;
        goto me3;
mn36:;
ma7:;
        uint8_t t44 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t44 != ((uint8_t)20ULL)) goto mn43;
        void * t45 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l3 = t45;
        void * t46 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l4 = t46;
        uint8_t t47 = 0;
        uint8_t t48 = 0;
        void * t49 = gm_l0;
        void * t50 = GM_LD(void *, (((uint8_t *)t49) + 0));
        void * t51 = (void *)(((uint8_t *)t50) + 8);
        void * t52 = gm_l3;
        gm_t88rcucg1b7uk t53 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t52) + 0));
        gm_t53at80pfdus3 t54;
        gm_f_kind_ghvav22((void *)&t54, t51, GM_CONV(gm_csguankko4lmhd, t53));
        uint8_t t55;
        uint8_t t58 = GM_LD(uint8_t, (((uint8_t *)&t54) + 0));
        if (t58 != ((uint8_t)11ULL)) goto mn57;
        t55 = ((uint8_t)1);
        goto me56;
mn57:;
        t55 = ((uint8_t)0);
        goto me56;
mn59:;
        gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 1049, 42);
me56:;
        if (t55) {
            void * t60 = gm_l0;
            void * t61 = gm_l3;
            uint8_t t62 = gm_f_frame_pure_cbdra2q((void *)NULL, cx, NULL, t60, t61);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            t48 = t62;
        } else t48 = 0;
        if (t48) {
            void * t63 = gm_l0;
            void * t64 = gm_l4;
            uint8_t t65 = gm_f_frame_pure_cbdra2q((void *)NULL, cx, NULL, t63, t64);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            t47 = t65;
        } else t47 = 0;
        uint8_t t66 = 0;
        F.ret = t47;
        goto gm_fret;
        goto me3;
mn43:;
ma8:;
        uint8_t t68 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t68 != ((uint8_t)29ULL)) goto mn67;
        void * t69 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l5 = t69;
        uint8_t t70 = 0;
        void * t71 = gm_l0;
        void * t72 = GM_LD(void *, (((uint8_t *)t71) + 0));
        void * t73 = (void *)(((uint8_t *)t72) + 8);
        void * t74 = gm_l1;
        gm_t88rcucg1b7uk t75 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t74) + 0));
        gm_t5v8n6qrn4rh4 t76;
        gm_f_repr_53quqki((void *)&t76, (void *)NULL, cx, NULL, t73, t75);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t77 = ((GM_LD(uint8_t, ((uint8_t *)&t76) + 0) == ((uint8_t)6ULL)));
        uint8_t t78 = (uint8_t)!t77;
        if (t78) {
            void * t79 = gm_l0;
            void * t80 = gm_l5;
            uint8_t t81 = gm_f_frame_pure_cbdra2q((void *)NULL, cx, NULL, t79, t80);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            t70 = t81;
        } else t70 = 0;
        uint8_t t82 = 0;
        F.ret = t70;
        goto gm_fret;
        goto me3;
mn67:;
ma9:;
        uint8_t t84 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t84 != ((uint8_t)30ULL)) goto mn83;
        void * t85 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l6 = t85;
        void * t86 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 16));
        gm_l7 = t86;
        {
            uint8_t t88 = 0;
            uint8_t t89 = 0;
            uint8_t t90 = 0;
            void * t91 = gm_l0;
            void * t92 = GM_LD(void *, (((uint8_t *)t91) + 0));
            void * t93 = (void *)(((uint8_t *)t92) + 8);
            void * t94 = gm_l1;
            gm_t88rcucg1b7uk t95 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t94) + 0));
            gm_t5v8n6qrn4rh4 t96;
            gm_f_repr_53quqki((void *)&t96, (void *)NULL, cx, NULL, t93, t95);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t97 = ((GM_LD(uint8_t, ((uint8_t *)&t96) + 0) == ((uint8_t)6ULL)));
            uint8_t t98 = (uint8_t)!t97;
            if (t98) {
                void * t99 = gm_l0;
                void * t100 = GM_LD(void *, (((uint8_t *)t99) + 0));
                void * t101 = (void *)(((uint8_t *)t100) + 8);
                void * t102 = gm_l6;
                gm_t88rcucg1b7uk t103 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t102) + 0));
                gm_t5v8n6qrn4rh4 t104;
                gm_f_repr_53quqki((void *)&t104, (void *)NULL, cx, NULL, t101, t103);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t105 = ((GM_LD(uint8_t, ((uint8_t *)&t104) + 0) == ((uint8_t)6ULL)));
                uint8_t t106 = (uint8_t)!t105;
                t90 = t106;
            } else t90 = 0;
            if (t90) {
                void * t107 = gm_l0;
                void * t108 = gm_l6;
                uint8_t t109 = gm_f_frame_pure_cbdra2q((void *)NULL, cx, NULL, t107, t108);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                t89 = t109;
            } else t89 = 0;
            if (t89) {
                void * t110 = gm_l0;
                void * t111 = gm_l7;
                uint8_t t112 = gm_f_frame_pure_cbdra2q((void *)NULL, cx, NULL, t110, t111);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                t88 = t112;
            } else t88 = 0;
            uint8_t t113 = 0;
            F.ret = t88;
            goto gm_fret;
        }
        goto me3;
mn83:;
ma10:;
        uint8_t t117 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t117 != ((uint8_t)35ULL)) goto pa116;
        gm_b16_8 t118 = GM_LD(gm_b16_8, ((((uint8_t *)t2) + 8) + 8));
        gm_l8 = t118;
        goto po115;
pa116:;
        uint8_t t120 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t120 != ((uint8_t)36ULL)) goto pa119;
        gm_b16_8 t121 = GM_LD(gm_b16_8, ((((uint8_t *)t2) + 8) + 8));
        gm_l8 = t121;
        goto po115;
pa119:;
        goto mn114;
po115:;
        {
            {
                gm_l11 = ((uint64_t)0ULL);
            }
            {
                uint64_t t124 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                gm_l10 = t124;
            }
            {
                for (;;) {
                    uint64_t t126 = gm_l11;
                    uint64_t t127 = gm_l10;
                    uint8_t t128 = (t126 < t127);
                    if (!t128) goto lb125;
                    {
                        uint64_t t131 = gm_l11;
                        uint64_t t134 = GM_LD(uint64_t, ((uint8_t *)&t131));
                        gm_l12 = t134;
                        {
                            void * t137 = gm_l0;
                            uint64_t t138 = gm_l12;
                            uint64_t t139 = (uintptr_t)t138;
                            uint64_t t140 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                            if (t139 >= (uintptr_t)(t140)) gm_trap_index(t139, t140, gm_srcmok80ho8hh01, 1056, 39);
                            void * t141 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l8)) + t139 * 88);
                            uint8_t t142 = gm_f_frame_pure_cbdra2q((void *)NULL, cx, NULL, t137, t141);
                            if (gm_unw.kind) {
                                goto gm_ll129;
                            }
                            uint8_t t143 = (uint8_t)!t142;
                            if (!t143) goto ie136;
                            {
                                uint8_t t145 = 0;
                                F.ret = ((uint8_t)0);
                                goto gm_fret;
                            }
                            goto ix136;
ie136:;
ix136:;
                        }
                        goto me132;
mn133:;
                        gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 1055, 13);
me132:;
                    }
lc125:;
                    {
                        uint64_t t147 = gm_l11;
                        uint64_t r148; bool o149 = __builtin_add_overflow(t147, ((uint64_t)1ULL), &r148);
                        uint64_t t150 = r148;
                        gm_l11 = t150;
                    }
                }
gm_ll129:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb125; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc125; }
                }
                goto gm_lad;
lb125:;
            }
        }
        {
            uint8_t t151 = 0;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
        goto me3;
mn114:;
ma11:;
        uint8_t t153 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
        goto me3;
mn152:;
ma12:;
        gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 1045, 5);
me3:;
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
inline __attribute__((aligned(4))) void gm_f_frame_up_g8486eu(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_frame_up_g8486eu F;
    void * gm_l0; /* fg */
    uint32_t gm_l1; /* levels */
    gm_b16_8 gm_l2; /* expr */
    uint32_t gm_l3; /* f */
    uint32_t gm_l4; /* <tmp> */
    uint32_t gm_l5; /* <tmp> */
    uint32_t gm_l6; /* p */
    GM_STACK_CHECK(&F, 249u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_b16_8 t2;
        GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sb8kl54a499bf));
        GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(6));
        gm_l2 = t2;
    }
    {
        void * t3 = gm_l0;
        uint32_t t4 = GM_LD(uint32_t, (((uint8_t *)t3) + 8));
        gm_l3 = t4;
    }
    {
        {
            gm_l5 = ((uint32_t)0ULL);
        }
        {
            uint32_t t6 = gm_l1;
            gm_l4 = t6;
        }
        {
            for (;;) {
                uint32_t t8 = gm_l5;
                uint32_t t9 = gm_l4;
                uint8_t t10 = (t8 < t9);
                if (!t10) goto lb7;
                {
                    uint32_t t13 = gm_l5;
                    {
                        gm_b16_8 t17 = gm_l2;
                        gm_tnpifv20irnph t18;
                        GM_ST(gm_b16_8, ((uint8_t *)&t18 + 0), t17);
                        gm_tabb710d9mq2e t19;
                        gm_f_format_lb0inss((void *)&t19, (void *)NULL, cx, NULL, t18);
                        if (gm_unw.kind) {
                            goto gm_ll11;
                        }
                        if (((((uint8_t *)&t19))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t19) + 8, gm_srcmok80ho8hh01, 580, 16);
                        gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&t19) + 8));
                        gm_b16_8 t21 = GM_LD(gm_b16_8, ((uint8_t *)&t20));
                        gm_l2 = t21;
                    }
                    {
                        void * t23 = gm_l0;
                        void * t24 = GM_LD(void *, (((uint8_t *)t23) + 0));
                        void * t25 = GM_LD(void *, (((uint8_t *)t24) + 0));
                        uint32_t t26 = gm_l3;
                        void * t27 = gm_f_func_in17r5n((void *)NULL, cx, NULL, t25, t26);
                        if (gm_unw.kind) {
                            goto gm_ll11;
                        }
                        if (!(((((uint8_t *)t27) + 88))[4] != 0)) goto ie22;
                        uint32_t t28 = GM_LD(uint32_t, (((uint8_t *)t27) + 88));
                        gm_l6 = t28;
                        {
                            uint32_t t30 = gm_l6;
                            gm_l3 = t30;
                        }
                        goto ix22;
ie22:;
ix22:;
                    }
                    goto me14;
mn15:;
                    gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 579, 5);
me14:;
                }
lc7:;
                {
                    uint32_t t32 = gm_l5;
                    uint32_t r33; bool o34 = __builtin_add_overflow(t32, ((uint32_t)1ULL), &r33);
                    uint32_t t35 = r33;
                    gm_l5 = t35;
                }
            }
gm_ll11:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb7; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc7; }
            }
            goto gm_lad;
lb7:;
        }
    }
    {
        memset((uint8_t *)&(*(gm_tommf4j9upomc *)gm_out) + 20, 0, 4);
        gm_b16_8 t36 = gm_l2;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tommf4j9upomc *)gm_out) + 0, t36);
        uint32_t t37 = gm_l3;
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tommf4j9upomc *)gm_out) + 16, t37);
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
extern __attribute__((aligned(4))) uint32_t gm_f_fresh_5kokjmg(void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_fresh_tmp_6tadl69(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fresh_tmp_6tadl69 F;
    void * gm_l0; /* fg */
    uint64_t gm_l1; /* n */
    void * gm_l2; /* names */
    GM_STACK_CHECK(&F, 280u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        uint32_t t3 = gm_f_fresh_5kokjmg(t2);
        uint64_t t4 = (uint64_t)t3;
        gm_l1 = t4;
    }
    {
        void * t5 = gm_l0;
        void * t6 = GM_LD(void *, (((uint8_t *)t5) + 0));
        void * t7 = (void *)(((uint8_t *)t6) + 3120);
        gm_l2 = t7;
    }
    {
        for (;;) {
            void * t9 = gm_l2;
            uint64_t t10 = GM_LD(uintptr_t, (((uint8_t *)t9) + 0) + 8);
            uint64_t t11 = gm_l1;
            uint8_t t12 = (t10 < t11);
            if (!t12) goto lb8;
            {
                void * t15 = gm_l2;
                void * t16 = gm_l2;
                uint64_t t17 = GM_LD(uintptr_t, (((uint8_t *)t16) + 0) + 8);
                uint64_t r18; bool o19 = __builtin_add_overflow(t17, ((uint64_t)1ULL), &r18);
                if (o19) gm_trap("integer overflow in `+`", gm_srcmok80ho8hh01, 489, 62);
                uint64_t t20 = r18;
                gm_t4ce0jd3tdro4 t21;
                GM_ST(uint64_t, ((uint8_t *)&t21 + 0), t20);
                gm_tabb710d9mq2e t22;
                gm_f_format_rtv6jsp((void *)&t22, (void *)NULL, cx, NULL, t21);
                if (gm_unw.kind) {
                    goto gm_ll13;
                }
                if (((((uint8_t *)&t22))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t22) + 8, gm_srcmok80ho8hh01, 489, 44);
                gm_b16_8 t23 = GM_LD(gm_b16_8, (((uint8_t *)&t22) + 8));
                gm_b16_8 t24 = GM_LD(gm_b16_8, ((uint8_t *)&t23));
                gm_tftb1odc739t5 t25 = gm_f_push_3ef746b(t15, t24);
                if (((((uint8_t *)&t25))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t25) + 1, gm_srcmok80ho8hh01, 489, 33);
                gm_b0_1 t26 = GM_LD(gm_b0_1, (((uint8_t *)&t25) + 1));
            }
lc8:;
        }
gm_ll13:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb8; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc8; }
        }
        goto gm_lad;
lb8:;
    }
    {
        uint64_t t27 = gm_l1;
        uint64_t r28; bool o29 = __builtin_sub_overflow(t27, ((uint64_t)1ULL), &r28);
        if (o29) gm_trap("integer overflow in `-`", gm_srcmok80ho8hh01, 490, 24);
        uint64_t t30 = r28;
        uint64_t t31 = (uintptr_t)t30;
        void * t32 = gm_l2;
        uint64_t t33 = GM_LD(uintptr_t, (((uint8_t *)t32) + 0) + 8);
        if (t31 >= (uintptr_t)(t33)) gm_trap_index(t31, t33, gm_srcmok80ho8hh01, 490, 12);
        gm_b16_8 t34 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)t32) + 0)) + t31 * 16));
        uint8_t t35 = 0;
        F.ret = t34;
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
__attribute__((aligned(4))) void gm_f_gen_body_j6ku8qk(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, void * a2, void * a3, gm_t9805qtonbsag a4) {
    struct gm_fr_gen_body_j6ku8qk F;
    void * gm_l0; /* g */
    uint32_t gm_l1; /* f */
    void * gm_l2; /* def */
    void * gm_l3; /* body */
    gm_t9805qtonbsag gm_l4; /* spec */
    uint8_t gm_l5; /* plain */
    uint8_t gm_l6; /* out_ret */
    uint8_t gm_l7; /* void_ret */
    uint8_t gm_l8; /* blob_ret */
    gm_b16_8 gm_l9; /* ret */
    gm_b16_8 gm_l11; /* <tmp> */
    uint64_t gm_l12; /* <tmp> */
    uint64_t gm_l13; /* i */
    uint32_t gm_l14; /* p */
    gm_t88rcucg1b7uk gm_l15; /* t */
    gm_b16_8 gm_l16; /* c */
    gm_b16_8 gm_l17; /* borrowed */
    gm_b16_8 gm_l18; /* in_frame */
    uint8_t gm_l19; /* split_runs */
    gm_b16_8 gm_l20; /* cname */
    uint32_t gm_l21; /* sp */
    gm_b16_8 gm_l22; /* sig */
    uint64_t gm_l23; /* cost */
    gm_b16_8 gm_l24; /* name */
    gm_b16_8 gm_l25; /* tag */
    gm_b16_8 gm_l27; /* <tmp> */
    uint64_t gm_l28; /* <tmp> */
    uint64_t gm_l29; /* i */
    gm_b16_8 gm_l31; /* c */
    gm_b16_8 gm_l32; /* pop */
    gm_b16_8 gm_l33; /* name */
    gm_b16_8 gm_l34; /* n */
    int32_t gm_l35; /* clause_lit */
    gm_b16_8 gm_l36; /* <tmp> */
    uint64_t gm_l37; /* <tmp> */
    uint64_t gm_l38; /* i */
    uint32_t gm_l39; /* p */
    gm_t88rcucg1b7uk gm_l40; /* t */
    gm_b16_8 gm_l41; /* lv */
    gm_b16_8 gm_l42; /* an */
    gm_b16_8 gm_l43; /* a */
    gm_b16_8 gm_l44; /* lv */
    gm_b16_8 gm_l45; /* ct */
    gm_b16_8 gm_l46; /* a */
    gm_b16_8 gm_l47; /* kill */
    gm_b16_8 gm_l48; /* r */
    gm_b16_8 gm_l49; /* r */
    gm_b16_8 gm_l50; /* ct */
    gm_b16_8 gm_l51; /* tail */
    gm_b16_8 gm_l52; /* text */
    uint64_t gm_l53; /* nl */
    gm_b16_8 gm_l54; /* header */
    uint64_t gm_l55; /* at */
    GM_STACK_CHECK(&F, 6443u);
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
            void * t3 = gm_l0;
            uint32_t t4 = gm_l1;
            uint8_t t5 = gm_f_is_plain_qr08i40((void *)NULL, cx, NULL, t3, t4);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l5 = t5;
        }
        {
            void * t6 = gm_l0;
            void * t7 = (void *)(((uint8_t *)t6) + 8);
            void * t8 = gm_l2;
            gm_t88rcucg1b7uk t9 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t8) + 64));
            uint8_t t10 = gm_f_by_ref_fi0o860((void *)NULL, cx, NULL, t7, t9);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l6 = t10;
        }
        {
            uint8_t t11 = 0;
            uint8_t t12 = 0;
            uint8_t t13 = gm_l5;
            if (t13) {
                void * t14 = gm_l0;
                void * t15 = (void *)(((uint8_t *)t14) + 8);
                void * t16 = gm_l2;
                gm_t88rcucg1b7uk t17 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t16) + 64));
                uint8_t t18 = gm_f_is_unit_17uennd(t15, GM_CONV(gm_csguankko4lmhd, t17));
                t12 = t18;
            } else t12 = 0;
            if (!t12) {
                uint8_t t19 = gm_l6;
                t11 = t19;
            } else t11 = 1;
            gm_l7 = t11;
        }
        {
            uint8_t t20 = gm_l5;
            uint8_t t21 = (uint8_t)!t20;
            gm_l8 = t21;
        }
        {
            gm_b16_8 t22;
            GM_ST(void *, (uint8_t *)&t22, (void *)(gm_sr4qbrk6tlgaa));
            GM_ST(uintptr_t, (uint8_t *)&t22 + 8, (uintptr_t)(0));
            gm_l9 = t22;
        }
        {
            uint8_t t24 = gm_l7;
            if (!t24) goto ie23;
            {
                gm_b16_8 t26;
                GM_ST(void *, (uint8_t *)&t26, (void *)(gm_sts5j6f5pkov0));
                GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)(4));
                gm_l9 = t26;
            }
            goto ix23;
ie23:;
            uint8_t t28 = gm_l8;
            if (!t28) goto ie27;
            {
                void * t30 = gm_l0;
                void * t31 = (void *)(((uint8_t *)t30) + 8);
                void * t32 = gm_l2;
                gm_t88rcucg1b7uk t33 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t32) + 64));
                gm_b16_8 t34 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t31, t33);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l9 = t34;
            }
            goto ix27;
ie27:;
            {
                void * t36 = gm_l0;
                void * t37 = (void *)(((uint8_t *)t36) + 8);
                void * t38 = gm_l2;
                gm_t88rcucg1b7uk t39 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t38) + 64));
                gm_b16_8 t40 = gm_f_plain_cty_q9lhlci((void *)NULL, cx, NULL, t37, t39);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l9 = t40;
            }
ix27:;
ix23:;
        }
        {
            gm_f_list_epvducg((void *)&F.l10, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            uint8_t t42 = gm_l6;
            if (!t42) goto ie41;
            {
                void * t44 = (void *)((uint8_t *)&F.l10);
                gm_b16_8 t45;
                GM_ST(void *, (uint8_t *)&t45, (void *)(gm_s4ql65vkv7h9p));
                GM_ST(uintptr_t, (uint8_t *)&t45 + 8, (uintptr_t)(12));
                gm_tftb1odc739t5 t46 = gm_f_push_3ef746b(t44, t45);
                if (((((uint8_t *)&t46))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t46) + 1, gm_srcmok80ho8hh01, 115, 18);
                gm_b0_1 t47 = GM_LD(gm_b0_1, (((uint8_t *)&t46) + 1));
            }
            goto ix41;
ie41:;
ix41:;
        }
        {
            uint8_t t49 = gm_l5;
            uint8_t t50 = (uint8_t)!t49;
            if (!t50) goto ie48;
            {
                void * t52 = (void *)((uint8_t *)&F.l10);
                gm_b16_8 t53;
                GM_ST(void *, (uint8_t *)&t53, (void *)(gm_siji3oo7ud53t));
                GM_ST(uintptr_t, (uint8_t *)&t53 + 8, (uintptr_t)(9));
                gm_tftb1odc739t5 t54 = gm_f_push_3ef746b(t52, t53);
                if (((((uint8_t *)&t54))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t54) + 1, gm_srcmok80ho8hh01, 117, 9);
                gm_b0_1 t55 = GM_LD(gm_b0_1, (((uint8_t *)&t54) + 1));
            }
            {
                void * t56 = (void *)((uint8_t *)&F.l10);
                gm_b16_8 t57;
                GM_ST(void *, (uint8_t *)&t57, (void *)(gm_s2kvk8bnjk52t));
                GM_ST(uintptr_t, (uint8_t *)&t57 + 8, (uintptr_t)(11));
                gm_tftb1odc739t5 t58 = gm_f_push_3ef746b(t56, t57);
                if (((((uint8_t *)&t58))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t58) + 1, gm_srcmok80ho8hh01, 118, 9);
                gm_b0_1 t59 = GM_LD(gm_b0_1, (((uint8_t *)&t58) + 1));
            }
            {
                void * t60 = (void *)((uint8_t *)&F.l10);
                gm_b16_8 t61;
                GM_ST(void *, (uint8_t *)&t61, (void *)(gm_sdi44r3kug6n6));
                GM_ST(uintptr_t, (uint8_t *)&t61 + 8, (uintptr_t)(13));
                gm_tftb1odc739t5 t62 = gm_f_push_3ef746b(t60, t61);
                if (((((uint8_t *)&t62))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t62) + 1, gm_srcmok80ho8hh01, 119, 9);
                gm_b0_1 t63 = GM_LD(gm_b0_1, (((uint8_t *)&t62) + 1));
            }
            goto ix48;
ie48:;
ix48:;
        }
        {
            {
                void * t65 = gm_l2;
                gm_b16_8 t66 = GM_LD(gm_b16_8, (((uint8_t *)t65) + 32));
                gm_l11 = t66;
            }
            {
                gm_l12 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t68 = gm_l12;
                    uint64_t t69 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                    uint8_t t70 = (t68 < t69);
                    if (!t70) goto lb67;
                    {
                        uint64_t t73 = gm_l12;
                        uint64_t t74 = gm_l12;
                        uint64_t t75 = (uintptr_t)t74;
                        uint64_t t76 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                        uint32_t t77 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l11)) + t75 * 4));
                        gm_tn68el8d14vku t78;
                        GM_ST(uint32_t, (uint8_t *)&t78 + 12, 0);
                        GM_ST(uint64_t, ((uint8_t *)&t78 + 0), t73);
                        GM_ST(uint32_t, ((uint8_t *)&t78 + 8), t77);
                        uint64_t t81 = GM_LD(uint64_t, (((uint8_t *)&t78) + 0));
                        gm_l13 = t81;
                        uint32_t t82 = GM_LD(uint32_t, (((uint8_t *)&t78) + 8));
                        gm_l14 = t82;
                        {
                            uint32_t t84 = gm_l14;
                            uint64_t t85 = (uint64_t)t84;
                            uint64_t t86 = (uintptr_t)t85;
                            void * t87 = gm_l2;
                            uint64_t t88 = GM_LD(uintptr_t, (((uint8_t *)t87) + 48) + 8);
                            if (t86 >= (uintptr_t)(t88)) gm_trap_index(t86, t88, gm_srcmok80ho8hh01, 122, 14);
                            gm_t88rcucg1b7uk t89 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, (((uint8_t *)t87) + 48)) + t86 * 40) + 16));
                            gm_l15 = t89;
                        }
                        {
                            gm_b16_8 t90;
                            GM_ST(void *, (uint8_t *)&t90, (void *)(gm_sr4qbrk6tlgaa));
                            GM_ST(uintptr_t, (uint8_t *)&t90 + 8, (uintptr_t)(0));
                            gm_l16 = t90;
                        }
                        {
                            uint8_t t92 = gm_l5;
                            if (!t92) goto ie91;
                            void * t93 = gm_l0;
                            void * t94 = (void *)(((uint8_t *)t93) + 8);
                            gm_t88rcucg1b7uk t95 = gm_l15;
                            uint8_t t96 = gm_f_by_ref_fi0o860((void *)NULL, cx, NULL, t94, t95);
                            if (gm_unw.kind) {
                                goto gm_ll71;
                            }
                            uint8_t t97 = (uint8_t)!t96;
                            if (!t97) goto ie91;
                            {
                                void * t99 = gm_l0;
                                void * t100 = (void *)(((uint8_t *)t99) + 8);
                                gm_t88rcucg1b7uk t101 = gm_l15;
                                gm_b16_8 t102 = gm_f_plain_cty_q9lhlci((void *)NULL, cx, NULL, t100, t101);
                                if (gm_unw.kind) {
                                    goto gm_ll71;
                                }
                                gm_l16 = t102;
                            }
                            goto ix91;
ie91:;
                            {
                                void * t104 = gm_l0;
                                void * t105 = (void *)(((uint8_t *)t104) + 8);
                                gm_t88rcucg1b7uk t106 = gm_l15;
                                gm_b16_8 t107 = gm_f_param_cty_e8tvhrm((void *)NULL, cx, NULL, t105, t106);
                                if (gm_unw.kind) {
                                    goto gm_ll71;
                                }
                                gm_l16 = t107;
                            }
ix91:;
                        }
                        {
                            void * t108 = (void *)((uint8_t *)&F.l10);
                            gm_t3877pk7lv748 t109;
                            gm_b16_8 t110 = gm_l16;
                            GM_ST(gm_b16_8, (uint8_t *)&t109 + 0, t110);
                            uint64_t t111 = gm_l13;
                            GM_ST(uint64_t, (uint8_t *)&t109 + 16, t111);
                            gm_tabb710d9mq2e t112;
                            gm_f_format_eq87866((void *)&t112, (void *)NULL, cx, NULL, &t109);
                            if (gm_unw.kind) {
                                goto gm_ll71;
                            }
                            if (((((uint8_t *)&t112))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t112) + 8, gm_srcmok80ho8hh01, 125, 21);
                            gm_b16_8 t113 = GM_LD(gm_b16_8, (((uint8_t *)&t112) + 8));
                            gm_b16_8 t114 = GM_LD(gm_b16_8, ((uint8_t *)&t113));
                            gm_tftb1odc739t5 t115 = gm_f_push_3ef746b(t108, t114);
                            if (((((uint8_t *)&t115))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t115) + 1, gm_srcmok80ho8hh01, 125, 9);
                            gm_b0_1 t116 = GM_LD(gm_b0_1, (((uint8_t *)&t115) + 1));
                        }
                        goto me79;
mn80:;
                        gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 121, 5);
me79:;
                    }
lc67:;
                    {
                        uint64_t t118 = gm_l12;
                        uint64_t r119; bool o120 = __builtin_add_overflow(t118, ((uint64_t)1ULL), &r119);
                        uint64_t t121 = r119;
                        gm_l12 = t121;
                    }
                }
gm_ll71:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb67; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc67; }
                }
                goto gm_lad;
lb67:;
            }
        }
        {
            uint64_t t123 = GM_LD(uintptr_t, (((uint8_t *)&F.l10) + 0) + 8);
            uint8_t t124 = ((t123 == ((uint64_t)0ULL)));
            if (!t124) goto ie122;
            {
                void * t126 = (void *)((uint8_t *)&F.l10);
                gm_b16_8 t127;
                GM_ST(void *, (uint8_t *)&t127, (void *)(gm_sts5j6f5pkov0));
                GM_ST(uintptr_t, (uint8_t *)&t127 + 8, (uintptr_t)(4));
                gm_tftb1odc739t5 t128 = gm_f_push_3ef746b(t126, t127);
                if (((((uint8_t *)&t128))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t128) + 1, gm_srcmok80ho8hh01, 127, 32);
                gm_b0_1 t129 = GM_LD(gm_b0_1, (((uint8_t *)&t128) + 1));
            }
            goto ix122;
ie122:;
ix122:;
        }
        {
            uint8_t t131;
            if (((((uint8_t *)&gm_l4))[4] != 0)) goto mn133;
            t131 = ((uint8_t)1);
            goto me132;
mn133:;
            t131 = ((uint8_t)0);
            goto me132;
mn134:;
            gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 128, 8);
me132:;
            if (!t131) goto ie130;
            void * t135 = gm_l0;
            uint32_t t136 = gm_l1;
            uint8_t t137 = gm_f_needs_wrapper_ktlbb7i((void *)NULL, cx, NULL, t135, t136);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!t137) goto ie130;
            {
                void * t139 = gm_l0;
                uint32_t t140 = gm_l1;
                void * t141 = gm_l2;
                gm_b0_1 t142 = gm_f_c_abi_wrapper_33vbmt4((void *)NULL, cx, NULL, t139, t140, t141);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix130;
ie130:;
ix130:;
        }
        {
            void * t143 = gm_l0;
            uint32_t t144 = gm_l1;
            gm_b16_8 t145 = gm_f_borrowed_params_b39va3r((void *)NULL, cx, NULL, t143, t144);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l17 = t145;
        }
        {
            void * t146 = gm_l0;
            uint32_t t147 = gm_l1;
            gm_b16_8 t148 = gm_f_frame_locals_mt4nb6o((void *)NULL, cx, NULL, t146, t147);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l18 = t148;
        }
        {
            uint8_t t149 = 0;
            void * t150 = gm_l0;
            uint8_t t151 = GM_LD(uint8_t, ((((uint8_t *)t150) + 1136) + 2));
            uint8_t t152 = (uint8_t)!t151;
            if (t152) {
                void * t153 = gm_l0;
                uint32_t t154 = gm_l1;
                uint8_t t155 = gm_f_long_body_qgvuu1c((void *)NULL, cx, NULL, t153, t154);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                t149 = t155;
            } else t149 = 0;
            gm_l19 = t149;
        }
        {
            gm_b16_8 t156;
            GM_ST(void *, (uint8_t *)&t156, (void *)(gm_sr4qbrk6tlgaa));
            GM_ST(uintptr_t, (uint8_t *)&t156 + 8, (uintptr_t)(0));
            gm_l20 = t156;
        }
        {
            if (!((((uint8_t *)&gm_l4))[4] != 0)) goto ie157;
            uint32_t t158 = GM_LD(uint32_t, ((uint8_t *)&gm_l4));
            gm_l21 = t158;
            {
                void * t160 = gm_l0;
                uint32_t t161 = gm_l1;
                uint32_t t162 = gm_l21;
                gm_b16_8 t163 = gm_f_spec_name_jptsbbf((void *)NULL, cx, NULL, t160, t161, t162);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l20 = t163;
            }
            goto ix157;
ie157:;
            {
                void * t165 = gm_l0;
                uint32_t t166 = gm_l1;
                gm_b16_8 t167 = gm_f_fn_name_6n34573((void *)NULL, cx, NULL, t165, t166);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l20 = t167;
            }
ix157:;
        }
        {
            gm_tud7oovfvnf43 t168;
            void * t169 = gm_l2;
            gm_t1h8kp9hsu3ua t170 = GM_LD(gm_t1h8kp9hsu3ua, (((uint8_t *)t169) + 145));
            gm_b16_8 t171 = gm_f_hint_attrs_mktdhi1((void *)NULL, cx, NULL, t170);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t168 + 0, t171);
            void * t172 = gm_l2;
            uint8_t t173 = GM_LD(uint8_t, (((uint8_t *)t172) + 144));
            gm_b16_8 t174 = gm_f_conv_attr_m0b89pc(t173);
            GM_ST(gm_b16_8, (uint8_t *)&t168 + 16, t174);
            gm_b16_8 t175 = gm_l9;
            GM_ST(gm_b16_8, (uint8_t *)&t168 + 32, t175);
            gm_b16_8 t176 = gm_l20;
            GM_ST(gm_b16_8, (uint8_t *)&t168 + 48, t176);
            gm_b16_8 t177 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 0));
            gm_b16_8 t178 = GM_LD(gm_b16_8, ((uint8_t *)&t177));
            gm_b16_8 t179;
            GM_ST(void *, (uint8_t *)&t179, (void *)(gm_sg5gdnrith2i6));
            GM_ST(uintptr_t, (uint8_t *)&t179 + 8, (uintptr_t)(2));
            gm_tabb710d9mq2e t180;
            gm_f_join_sfknljb((void *)&t180, (void *)NULL, cx, NULL, t178, t179);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t180))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t180) + 8, gm_srcmok80ho8hh01, 134, 132);
            gm_b16_8 t181 = GM_LD(gm_b16_8, (((uint8_t *)&t180) + 8));
            GM_ST(gm_b16_8, (uint8_t *)&t168 + 64, t181);
            gm_tabb710d9mq2e t182;
            gm_f_format_822rutp((void *)&t182, (void *)NULL, cx, NULL, &t168);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t182))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t182) + 8, gm_srcmok80ho8hh01, 134, 12);
            gm_b16_8 t183 = GM_LD(gm_b16_8, (((uint8_t *)&t182) + 8));
            gm_l22 = t183;
        }
        {
            void * t184 = gm_l0;
            void * t185 = (void *)(((uint8_t *)t184) + 1888);
            gm_b16_8 t186 = gm_l22;
            gm_tnpifv20irnph t187;
            GM_ST(gm_b16_8, ((uint8_t *)&t187 + 0), t186);
            gm_tftb1odc739t5 t188 = gm_f_write_i2vvij8(t185, t187);
            if (((((uint8_t *)&t188))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t188) + 1, gm_srcmok80ho8hh01, 135, 5);
            gm_b0_1 t189 = GM_LD(gm_b0_1, (((uint8_t *)&t188) + 1));
        }
        {
            void * t190 = gm_l0;
            void * t191 = GM_LD(void *, (((uint8_t *)t190) + 0));
            void * t192 = GM_LD(void *, (((uint8_t *)t191) + 0));
            void * t193 = gm_l2;
            gm_b16_8 t194 = GM_LD(gm_b16_8, (((uint8_t *)t193) + 48));
            void * t195 = gm_l2;
            void * t196 = GM_LD(void *, (((uint8_t *)t195) + 152));
            uint64_t t197 = gm_f_frame_cost_oei64tv((void *)NULL, cx, NULL, t192, t194, t196);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l23 = t197;
        }
        {
            uint64_t t199 = gm_l23;
            uint64_t t200 = GM_LD(uint64_t, gm_c1id0n0l1pf);
            uint8_t t201 = (t199 > t200);
            if (!t201) goto ie198;
            {
                void * t203 = gm_l2;
                gm_b16_8 t204 = GM_LD(gm_b16_8, (((uint8_t *)t203) + 0));
                gm_b16_8 t205 = gm_f_c_string_n7h27b3((void *)NULL, cx, NULL, t204);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l24 = t205;
            }
            {
                void * t206 = gm_l0;
                void * t207 = (void *)(((uint8_t *)t206) + 1928);
                gm_tftksr4op114e t208;
                gm_b16_8 t209 = gm_l22;
                GM_ST(gm_b16_8, (uint8_t *)&t208 + 0, t209);
                gm_b16_8 t210 = gm_l24;
                GM_ST(gm_b16_8, (uint8_t *)&t208 + 16, t210);
                uint64_t t211 = gm_l23;
                GM_ST(uint64_t, (uint8_t *)&t208 + 32, t211);
                gm_tftb1odc739t5 t212 = gm_f_write_ajj3a37(t207, &t208);
                if (((((uint8_t *)&t212))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t212) + 1, gm_srcmok80ho8hh01, 141, 9);
                gm_b0_1 t213 = GM_LD(gm_b0_1, (((uint8_t *)&t212) + 1));
            }
            {
                gm_b0_1 t214;
                memset(&(*(gm_tqgtd5gtgj65s *)gm_out), 0, sizeof (*(gm_tqgtd5gtgj65s *)gm_out));
                ((uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out))[0] = 0;
                GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 8, t214);
                goto gm_fdone;
            }
            goto ix198;
ie198:;
ix198:;
        }
        {
            void * t215 = gm_l0;
            uint32_t t216 = gm_l1;
            gm_t9805qtonbsag t217 = gm_l4;
            gm_b16_8 t218 = gm_f_fn_tag_okd90ep((void *)NULL, cx, NULL, t215, t216, t217);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l25 = t218;
        }
        {
            memset((uint8_t *)&F.l26 + 12, 0, 4);
            memset((uint8_t *)&F.l26 + 236, 0, 4);
            memset((uint8_t *)&F.l26 + 299, 0, 5);
            memset((uint8_t *)&F.l26 + 365, 0, 3);
            void * t219 = gm_l0;
            GM_ST(void *, (uint8_t *)&F.l26 + 0, t219);
            uint32_t t220 = gm_l1;
            GM_ST(uint32_t, (uint8_t *)&F.l26 + 8, t220);
            gm_b16_8 t221 = gm_l25;
            GM_ST(gm_b16_8, (uint8_t *)&F.l26 + 16, t221);
            gm_t9805qtonbsag t222 = gm_l4;
            GM_ST(gm_t9805qtonbsag, (uint8_t *)&F.l26 + 32, t222);
            gm_t5lf9i3n1r3co t223;
            memset(&t223, 0, sizeof t223);
            GM_ST(gm_t5lf9i3n1r3co, (uint8_t *)&F.l26 + 40, t223);
            gm_tdh3gh5n00hec t224;
            memset(&t224, 0, sizeof t224);
            GM_ST(gm_tdh3gh5n00hec, (uint8_t *)&F.l26 + 64, t224);
            gm_tjefu848ssvm9 t225;
            memset(&t225, 0, sizeof t225);
            GM_ST(gm_tjefu848ssvm9, (uint8_t *)&F.l26 + 104, t225);
            gm_th2lqbo77pvsc t226;
            memset(&t226, 0, sizeof t226);
            GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&F.l26 + 152, t226);
            void * t227 = gm_l2;
            GM_ST(void *, (uint8_t *)&F.l26 + 176, t227);
            gm_f_list_uvfu1k1((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l26 + 184)), (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint64_t, (uint8_t *)&F.l26 + 224, ((uint64_t)1ULL));
            GM_ST(uint32_t, (uint8_t *)&F.l26 + 232, ((uint32_t)0ULL));
            gm_f_list_ubba23m((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l26 + 240)), (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t228 = GM_LD(gm_b16_8, gm_cgsmfcq937e_4);
            GM_ST(gm_b16_8, (uint8_t *)&F.l26 + 280, t228);
            GM_ST(uint8_t, (uint8_t *)&F.l26 + 296, ((uint8_t)0));
            GM_ST(uint8_t, (uint8_t *)&F.l26 + 297, ((uint8_t)0));
            GM_ST(uint8_t, (uint8_t *)&F.l26 + 298, ((uint8_t)0));
            GM_ST(uint64_t, (uint8_t *)&F.l26 + 304, ((uint64_t)0ULL));
            gm_tf5srh4mp4b3i t229;
            memset(&t229, 0, sizeof t229);
            GM_ST(gm_tf5srh4mp4b3i, (uint8_t *)&F.l26 + 312, t229);
            gm_b16_8 t230 = gm_l17;
            GM_ST(gm_b16_8, (uint8_t *)&F.l26 + 328, t230);
            gm_b16_8 t231 = gm_l18;
            GM_ST(gm_b16_8, (uint8_t *)&F.l26 + 344, t231);
            GM_ST(uint32_t, (uint8_t *)&F.l26 + 360, ((uint32_t)0ULL));
            uint8_t t232 = gm_l19;
            GM_ST(uint8_t, (uint8_t *)&F.l26 + 364, t232);
            gm_f_list_ns53od0((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l26 + 368)), (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t233 = (void *)(((uint8_t *)&F.l26) + 184);
            gm_b16_8 t234 = gm_l22;
            gm_tnpifv20irnph t235;
            GM_ST(gm_b16_8, ((uint8_t *)&t235 + 0), t234);
            gm_tftb1odc739t5 t236 = gm_f_write_n0vcum5(t233, t235);
            if (((((uint8_t *)&t236))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t236) + 1, gm_srcmok80ho8hh01, 158, 5);
            gm_b0_1 t237 = GM_LD(gm_b0_1, (((uint8_t *)&t236) + 1));
        }
        {
            void * t238 = (void *)((uint8_t *)&F.l26);
            void * t239 = gm_l2;
            gm_tl874t2atnlh3 t240 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t239) + 16));
            gm_b0_1 t241 = gm_f_src_line_d24sajl((void *)NULL, cx, NULL, t238, t240);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t243 = gm_l2;
            uint8_t t244 = GM_LD(uint8_t, ((((uint8_t *)t243) + 145) + 0));
            if (!t244) goto ie242;
            {
                void * t246 = (void *)((uint8_t *)&F.l26);
                gm_b16_8 t247;
                GM_ST(void *, (uint8_t *)&t247, (void *)(gm_s2gtin895e08a));
                GM_ST(uintptr_t, (uint8_t *)&t247 + 8, (uintptr_t)(17));
                gm_b0_1 t248 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t246, t247);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix242;
ie242:;
ix242:;
        }
        {
            uint8_t t250 = gm_l5;
            if (!t250) goto ie249;
            {
                void * t252 = (void *)((uint8_t *)&F.l26);
                gm_b16_8 t253;
                GM_ST(void *, (uint8_t *)&t253, (void *)(gm_svdapq3j66qso));
                GM_ST(uintptr_t, (uint8_t *)&t253 + 8, (uintptr_t)(59));
                gm_b0_1 t254 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t252, t253);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix249;
ie249:;
ix249:;
        }
        {
            void * t255 = (void *)((uint8_t *)&F.l26);
            void * t256 = gm_l0;
            uint32_t t257 = gm_l1;
            gm_b16_8 t258 = gm_f_frame_name_stddmgg((void *)NULL, cx, NULL, t256, t257);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tnpifv20irnph t259;
            GM_ST(gm_b16_8, ((uint8_t *)&t259 + 0), t258);
            gm_b0_1 t260 = gm_f_linef_4plvds6((void *)NULL, cx, NULL, t255, t259);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                void * t262 = gm_l2;
                gm_b16_8 t263 = GM_LD(gm_b16_8, (((uint8_t *)t262) + 48));
                gm_l27 = t263;
            }
            {
                gm_l28 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t265 = gm_l28;
                    uint64_t t266 = GM_LD(uintptr_t, ((uint8_t *)&gm_l27) + 8);
                    uint8_t t267 = (t265 < t266);
                    if (!t267) goto lb264;
                    {
                        gm_t5im2v50m3hk6 t270;
                        uint64_t t271 = gm_l28;
                        GM_ST(uint64_t, (uint8_t *)&t270 + 0, t271);
                        uint64_t t272 = gm_l28;
                        uint64_t t273 = (uintptr_t)t272;
                        uint64_t t274 = GM_LD(uintptr_t, ((uint8_t *)&gm_l27) + 8);
                        memcpy((uint8_t *)&t270 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l27)) + t273 * 40), 40);
                        uint64_t t277 = GM_LD(uint64_t, (((uint8_t *)&t270) + 0));
                        gm_l29 = t277;
                        F.l30 = (*(gm_tsoie1ppgdvcg *)(((uint8_t *)&t270) + 8));
                        {
                            uint64_t t280 = gm_l29;
                            uint64_t t281 = (uintptr_t)t280;
                            uint64_t t282 = GM_LD(uintptr_t, (((uint8_t *)&F.l26) + 344) + 8);
                            if (t281 >= (uintptr_t)(t282)) gm_trap_index(t281, t282, gm_srcmok80ho8hh01, 164, 16);
                            uint8_t t283 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l26) + 344)) + t281 * 1));
                            uint8_t t284 = (uint8_t)!t283;
                            if (!t284) goto ie279;
                            {
                                uint64_t t286 = GM_LD(uint64_t, (((uint8_t *)&F.l26) + 304));
                                void * t287 = GM_LD(void *, (((uint8_t *)&F.l26) + 0));
                                void * t288 = (void *)(((uint8_t *)t287) + 8);
                                gm_t88rcucg1b7uk t289 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l30) + 16));
                                uint64_t t290 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t288, t289);
                                if (gm_unw.kind) {
                                    goto gm_ll268;
                                }
                                uint64_t r291; bool o292 = __builtin_add_overflow(t286, t290, &r291);
                                if (o292) gm_trap("integer overflow in `+`", gm_srcmok80ho8hh01, 165, 13);
                                uint64_t t293 = r291;
                                GM_ST(uint64_t, (((uint8_t *)&F.l26) + 304), t293);
                            }
                            {
                                void * t294 = GM_LD(void *, (((uint8_t *)&F.l26) + 0));
                                void * t295 = (void *)(((uint8_t *)t294) + 8);
                                gm_t88rcucg1b7uk t296 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l30) + 16));
                                gm_b16_8 t297 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t295, t296);
                                if (gm_unw.kind) {
                                    goto gm_ll268;
                                }
                                gm_l31 = t297;
                            }
                            {
                                void * t298 = (void *)((uint8_t *)&F.l26);
                                gm_tq3m5qgvkpje8 t299;
                                gm_b16_8 t300 = gm_l31;
                                GM_ST(gm_b16_8, (uint8_t *)&t299 + 0, t300);
                                uint64_t t301 = gm_l29;
                                GM_ST(uint64_t, (uint8_t *)&t299 + 16, t301);
                                gm_b16_8 t302 = GM_LD(gm_b16_8, (((uint8_t *)&F.l30) + 0));
                                GM_ST(gm_b16_8, (uint8_t *)&t299 + 24, t302);
                                gm_b0_1 t303 = gm_f_linef_md5jtn8((void *)NULL, cx, NULL, t298, &t299);
                                if (gm_unw.kind) {
                                    goto gm_ll268;
                                }
                            }
                            goto ix279;
ie279:;
ix279:;
                        }
                        goto me275;
mn276:;
                        gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 163, 5);
me275:;
                    }
lc264:;
                    {
                        uint64_t t305 = gm_l28;
                        uint64_t r306; bool o307 = __builtin_add_overflow(t305, ((uint64_t)1ULL), &r306);
                        uint64_t t308 = r306;
                        gm_l28 = t308;
                    }
                }
gm_ll268:;
                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb264; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc264; }
                }
                goto gm_lad;
lb264:;
            }
        }
        {
            void * t310 = GM_LD(void *, (((uint8_t *)&F.l26) + 0));
            uint8_t t311 = GM_LD(uint8_t, ((((uint8_t *)t310) + 1136) + 0));
            if (!t311) goto ie309;
            void * t312 = GM_LD(void *, (((uint8_t *)&F.l26) + 0));
            void * t313 = gm_l2;
            uint8_t t314 = gm_f_probe_free_4hilimu((void *)NULL, cx, NULL, t312, t313);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t315 = (uint8_t)!t314;
            if (!t315) goto ie309;
            {
                void * t318 = GM_LD(void *, (((uint8_t *)&F.l26) + 0));
                uint32_t t319 = gm_l1;
                uint32_t t320 = gm_f_call_depth_4m6ap7n((void *)NULL, cx, NULL, t318, t319);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint32_t t321 = GM_LD(uint32_t, gm_cdmki9acqi9);
                uint8_t t322 = (t320 <= t321);
                if (!t322) goto ie317;
                {
                    void * t324 = (void *)((uint8_t *)&F.l26);
                    gm_b16_8 t325;
                    GM_ST(void *, (uint8_t *)&t325, (void *)(gm_s8hh4pp2rpnlj));
                    GM_ST(uintptr_t, (uint8_t *)&t325 + 8, (uintptr_t)(42));
                    gm_b0_1 t326 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t324, t325);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                goto ix317;
ie317:;
                {
                    void * t328 = (void *)((uint8_t *)&F.l26);
                    gm_b16_8 t329;
                    GM_ST(void *, (uint8_t *)&t329, (void *)(gm_sjs17a3d2ohel));
                    GM_ST(uintptr_t, (uint8_t *)&t329 + 8, (uintptr_t)(34));
                    gm_b0_1 t330 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t328, t329);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
ix317:;
            }
            {
                uint64_t t331 = GM_LD(uintptr_t, ((((uint8_t *)&F.l26) + 184) + 0) + 8);
                gm_b16_8 t332;
                GM_ST(void *, (uint8_t *)&t332, (void *)(gm_spdntfv7vkd2t));
                GM_ST(uintptr_t, (uint8_t *)&t332 + 8, (uintptr_t)(16));
                uint64_t t333 = GM_LD(uintptr_t, ((uint8_t *)&t332) + 8);
                uint64_t r334; bool o335 = __builtin_sub_overflow(t331, t333, &r334);
                if (o335) gm_trap("integer overflow in `-`", gm_srcmok80ho8hh01, 177, 23);
                uint64_t t336 = r334;
                gm_tf5srh4mp4b3i t337;
                GM_ST(uint8_t, (uint8_t *)&t337 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t337 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t337 + 12, 0);
                GM_ST(uint64_t, ((uint8_t *)&t337), t336);
                ((uint8_t *)&t337)[8] = 1;
                GM_ST(gm_tf5srh4mp4b3i, (((uint8_t *)&F.l26) + 312), t337);
            }
            goto ix309;
ie309:;
ix309:;
        }
        {
            gm_b16_8 t338;
            GM_ST(void *, (uint8_t *)&t338, (void *)(gm_sr4qbrk6tlgaa));
            GM_ST(uintptr_t, (uint8_t *)&t338 + 8, (uintptr_t)(0));
            gm_l32 = t338;
        }
        {
            void * t340 = GM_LD(void *, (((uint8_t *)&F.l26) + 0));
            uint8_t t341 = GM_LD(uint8_t, ((((uint8_t *)t340) + 1136) + 1));
            if (!t341) goto ie339;
            {
                gm_b16_8 t343;
                GM_ST(void *, (uint8_t *)&t343, (void *)(gm_s04qus0luoga5));
                GM_ST(uintptr_t, (uint8_t *)&t343 + 8, (uintptr_t)(4));
                gm_l33 = t343;
            }
            {
                void * t345 = GM_LD(void *, (((uint8_t *)&F.l26) + 0));
                void * t346 = GM_LD(void *, (((uint8_t *)t345) + 0));
                uint32_t t347 = gm_l1;
                gm_th2lqbo77pvsc t348;
                gm_f_backtrace_name_mars429((void *)&t348, (void *)NULL, cx, NULL, t346, t347);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (!((((uint8_t *)&t348))[16] != 0)) goto ie344;
                gm_b16_8 t349 = GM_LD(gm_b16_8, ((uint8_t *)&t348));
                gm_l34 = t349;
                {
                    gm_b16_8 t351 = gm_l34;
                    gm_b16_8 t352 = gm_f_c_string_n7h27b3((void *)NULL, cx, NULL, t351);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l33 = t352;
                }
                goto ix344;
ie344:;
ix344:;
            }
            {
                void * t353 = (void *)((uint8_t *)&F.l26);
                gm_b16_8 t354 = gm_l33;
                gm_tnpifv20irnph t355;
                GM_ST(gm_b16_8, ((uint8_t *)&t355 + 0), t354);
                gm_b0_1 t356 = gm_f_linef_cuactnf((void *)NULL, cx, NULL, t353, t355);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                gm_b16_8 t357;
                GM_ST(void *, (uint8_t *)&t357, (void *)(gm_sn2gg3b98t635));
                GM_ST(uintptr_t, (uint8_t *)&t357 + 8, (uintptr_t)(20));
                gm_l32 = t357;
            }
            goto ix339;
ie339:;
ix339:;
        }
        {
            void * t358 = (void *)((uint8_t *)&F.l26);
            gm_b16_8 t359;
            GM_ST(void *, (uint8_t *)&t359, (void *)(gm_s2micd2hub1nf));
            GM_ST(uintptr_t, (uint8_t *)&t359 + 8, (uintptr_t)(25));
            gm_b0_1 t360 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t358, t359);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            int32_t t361;
            void * t363 = gm_l2;
            uint8_t t364 = ((GM_LD(uint8_t, (((uint8_t *)t363) + 96) + 0) == ((uint8_t)2ULL)));
            if (!t364) goto ie362;
            int32_t t366 = ((int32_t)1LL);
            t361 = t366;
            goto ix362;
ie362:;
            int32_t t368 = ((int32_t)0LL);
            t361 = t368;
ix362:;
            gm_l35 = t361;
        }
        {
            void * t369 = (void *)((uint8_t *)&F.l26);
            int32_t t370 = gm_l35;
            gm_t19enj90vg8bd t371;
            GM_ST(int32_t, ((uint8_t *)&t371 + 0), t370);
            gm_b0_1 t372 = gm_f_linef_cs9kbvs((void *)NULL, cx, NULL, t369, t371);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t373 = (void *)((uint8_t *)&F.l26);
            gm_b16_8 t374;
            GM_ST(void *, (uint8_t *)&t374, (void *)(gm_sdikuksp636di));
            GM_ST(uintptr_t, (uint8_t *)&t374 + 8, (uintptr_t)(11));
            gm_b0_1 t375 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t373, t374);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                void * t377 = gm_l2;
                gm_b16_8 t378 = GM_LD(gm_b16_8, (((uint8_t *)t377) + 32));
                gm_l36 = t378;
            }
            {
                gm_l37 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t380 = gm_l37;
                    uint64_t t381 = GM_LD(uintptr_t, ((uint8_t *)&gm_l36) + 8);
                    uint8_t t382 = (t380 < t381);
                    if (!t382) goto lb379;
                    {
                        uint64_t t385 = gm_l37;
                        uint64_t t386 = gm_l37;
                        uint64_t t387 = (uintptr_t)t386;
                        uint64_t t388 = GM_LD(uintptr_t, ((uint8_t *)&gm_l36) + 8);
                        uint32_t t389 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l36)) + t387 * 4));
                        gm_tn68el8d14vku t390;
                        GM_ST(uint32_t, (uint8_t *)&t390 + 12, 0);
                        GM_ST(uint64_t, ((uint8_t *)&t390 + 0), t385);
                        GM_ST(uint32_t, ((uint8_t *)&t390 + 8), t389);
                        uint64_t t393 = GM_LD(uint64_t, (((uint8_t *)&t390) + 0));
                        gm_l38 = t393;
                        uint32_t t394 = GM_LD(uint32_t, (((uint8_t *)&t390) + 8));
                        gm_l39 = t394;
                        {
                            uint32_t t396 = gm_l39;
                            uint64_t t397 = (uint64_t)t396;
                            uint64_t t398 = (uintptr_t)t397;
                            void * t399 = gm_l2;
                            uint64_t t400 = GM_LD(uintptr_t, (((uint8_t *)t399) + 48) + 8);
                            if (t398 >= (uintptr_t)(t400)) gm_trap_index(t398, t400, gm_srcmok80ho8hh01, 192, 14);
                            gm_t88rcucg1b7uk t401 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, (((uint8_t *)t399) + 48)) + t398 * 40) + 16));
                            gm_l40 = t401;
                        }
                        {
                            uint64_t t403 = gm_l38;
                            uint64_t t404 = (uintptr_t)t403;
                            uint64_t t405 = GM_LD(uintptr_t, (((uint8_t *)&F.l26) + 328) + 8);
                            if (t404 >= (uintptr_t)(t405)) gm_trap_index(t404, t405, gm_srcmok80ho8hh01, 193, 12);
                            uint8_t t406 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l26) + 328)) + t404 * 1));
                            if (!t406) goto ie402;
                            {
                                goto lc379;
                            }
                            goto ix402;
ie402:;
ix402:;
                        }
                        {
                            void * t409 = GM_LD(void *, (((uint8_t *)&F.l26) + 0));
                            void * t410 = (void *)(((uint8_t *)t409) + 8);
                            gm_t88rcucg1b7uk t411 = gm_l40;
                            uint8_t t412 = gm_f_by_ref_fi0o860((void *)NULL, cx, NULL, t410, t411);
                            if (gm_unw.kind) {
                                goto gm_ll383;
                            }
                            if (!t412) goto ie408;
                            {
                                void * t414 = (void *)((uint8_t *)&F.l26);
                                uint32_t t415 = gm_l39;
                                gm_b16_8 t416 = gm_f_local_lv_m2jtf0n((void *)NULL, cx, NULL, t414, t415);
                                if (gm_unw.kind) {
                                    goto gm_ll383;
                                }
                                gm_l41 = t416;
                            }
                            {
                                void * t417 = (void *)((uint8_t *)&F.l26);
                                gm_tq3m5qgvkpje8 t418;
                                gm_b16_8 t419 = gm_l41;
                                GM_ST(gm_b16_8, (uint8_t *)&t418 + 0, t419);
                                uint64_t t420 = gm_l38;
                                GM_ST(uint64_t, (uint8_t *)&t418 + 16, t420);
                                gm_b16_8 t421 = gm_l41;
                                GM_ST(gm_b16_8, (uint8_t *)&t418 + 24, t421);
                                gm_b0_1 t422 = gm_f_linef_nvqsj6t((void *)NULL, cx, NULL, t417, &t418);
                                if (gm_unw.kind) {
                                    goto gm_ll383;
                                }
                            }
                            {
                                goto lc379;
                            }
                            goto ix408;
ie408:;
ix408:;
                        }
                        {
                            uint64_t t423 = gm_l38;
                            gm_t4ce0jd3tdro4 t424;
                            GM_ST(uint64_t, ((uint8_t *)&t424 + 0), t423);
                            gm_tabb710d9mq2e t425;
                            gm_f_format_l05am3t((void *)&t425, (void *)NULL, cx, NULL, t424);
                            if (gm_unw.kind) {
                                goto gm_ll383;
                            }
                            if (((((uint8_t *)&t425))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t425) + 8, gm_srcmok80ho8hh01, 199, 15);
                            gm_b16_8 t426 = GM_LD(gm_b16_8, (((uint8_t *)&t425) + 8));
                            gm_l42 = t426;
                        }
                        {
                            gm_b16_8 t427 = gm_l42;
                            gm_b16_8 t428 = GM_LD(gm_b16_8, ((uint8_t *)&t427));
                            gm_l43 = t428;
                        }
                        {
                            uint8_t t430 = gm_l5;
                            if (!t430) goto ie429;
                            {
                                void * t432 = (void *)((uint8_t *)&F.l26);
                                gm_t88rcucg1b7uk t433 = gm_l40;
                                gm_b16_8 t434 = gm_l42;
                                gm_b16_8 t435 = GM_LD(gm_b16_8, ((uint8_t *)&t434));
                                gm_b16_8 t436 = gm_f_gamma_value_rgbfhcc((void *)NULL, cx, NULL, t432, t433, t435);
                                if (gm_unw.kind) {
                                    goto gm_ll383;
                                }
                                gm_l43 = t436;
                            }
                            goto ix429;
ie429:;
ix429:;
                        }
                        {
                            void * t437 = (void *)((uint8_t *)&F.l26);
                            uint32_t t438 = gm_l39;
                            gm_b16_8 t439 = gm_f_local_lv_m2jtf0n((void *)NULL, cx, NULL, t437, t438);
                            if (gm_unw.kind) {
                                goto gm_ll383;
                            }
                            gm_l44 = t439;
                        }
                        {
                            void * t440 = (void *)((uint8_t *)&F.l26);
                            gm_t6j6smj7qoctu t441;
                            gm_b16_8 t442 = gm_l44;
                            GM_ST(gm_b16_8, (uint8_t *)&t441 + 0, t442);
                            gm_b16_8 t443 = gm_l43;
                            GM_ST(gm_b16_8, (uint8_t *)&t441 + 16, t443);
                            gm_b0_1 t444 = gm_f_linef_uu8i6ue((void *)NULL, cx, NULL, t440, &t441);
                            if (gm_unw.kind) {
                                goto gm_ll383;
                            }
                        }
                        goto me391;
mn392:;
                        gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 191, 5);
me391:;
                    }
lc379:;
                    {
                        uint64_t t446 = gm_l37;
                        uint64_t r447; bool o448 = __builtin_add_overflow(t446, ((uint64_t)1ULL), &r447);
                        uint64_t t449 = r447;
                        gm_l37 = t449;
                    }
                }
gm_ll383:;
                if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb379; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc379; }
                }
                goto gm_lad;
lb379:;
            }
        }
        {
            uint8_t t451 = gm_l6;
            if (!t451) goto ie450;
            void * t452 = (void *)((uint8_t *)&F.l26);
            void * t453 = gm_l3;
            uint8_t t454 = gm_f_builds_in_place_ae1b90u((void *)NULL, cx, NULL, t452, t453);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!t454) goto ie450;
            {
                void * t456 = GM_LD(void *, (((uint8_t *)&F.l26) + 0));
                void * t457 = (void *)(((uint8_t *)t456) + 8);
                void * t458 = gm_l2;
                gm_t88rcucg1b7uk t459 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t458) + 64));
                gm_b16_8 t460 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t457, t459);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l45 = t460;
            }
            {
                void * t461 = (void *)((uint8_t *)&F.l26);
                void * t462 = gm_l3;
                gm_b16_8 t463 = gm_l45;
                gm_tnpifv20irnph t464;
                GM_ST(gm_b16_8, ((uint8_t *)&t464 + 0), t463);
                gm_tabb710d9mq2e t465;
                gm_f_format_9c0uitb((void *)&t465, (void *)NULL, cx, NULL, t464);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t465))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t465) + 8, gm_srcmok80ho8hh01, 208, 32);
                gm_b16_8 t466 = GM_LD(gm_b16_8, (((uint8_t *)&t465) + 8));
                gm_b16_8 t467 = GM_LD(gm_b16_8, ((uint8_t *)&t466));
                gm_tnrltbc61i6v1 t468;
                GM_ST(uint8_t, (uint8_t *)&t468 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t468 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t468 + 4, 0);
                GM_ST(uint32_t, (uint8_t *)&t468 + 8, 0);
                GM_ST(uint8_t, ((uint8_t *)&t468 + 0), ((uint8_t)1ULL));
                gm_tqgtd5gtgj65s t469;
                gm_f_expr_into_dn9kavo((void *)&t469, (void *)NULL, cx, NULL, t461, t462, t467, t468);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t469))[0] != 0)) {
                    gm_tqgtd5gtgj65s t470;
                    GM_ST(uint8_t, (uint8_t *)&t470 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t470 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t470 + 4, 0);
                    ((uint8_t *)&t470)[0] = 1;
                    GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t470 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t469) + 8)));
                    F.ret = t470;
                    goto gm_fret;
                }
                gm_b0_1 t471 = GM_LD(gm_b0_1, (((uint8_t *)&t469) + 8));
            }
            {
                void * t472 = (void *)((uint8_t *)&F.l26);
                gm_b16_8 t473;
                GM_ST(void *, (uint8_t *)&t473, (void *)(gm_svj967tkdfgka));
                GM_ST(uintptr_t, (uint8_t *)&t473 + 8, (uintptr_t)(14));
                gm_b0_1 t474 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t472, t473);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix450;
ie450:;
            uint8_t t476 = gm_l6;
            if (!t476) goto ie475;
            void * t477 = gm_l3;
            gm_t88rcucg1b7uk t478 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t477) + 0));
            void * t479 = gm_l2;
            gm_t88rcucg1b7uk t480 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t479) + 64));
            uint8_t t481 = (gm_eq9m6mvtmgde(((uint8_t *)&t478), ((uint8_t *)&t480)));
            if (!t481) goto ie475;
            void * t482 = (void *)((uint8_t *)&F.l26);
            void * t483 = gm_l3;
            uint8_t t484 = gm_f_reads_place_nji55rm((void *)NULL, cx, NULL, t482, t483);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!t484) goto ie475;
            {
                void * t486 = (void *)((uint8_t *)&F.l26);
                void * t487 = gm_l3;
                gm_t5o0tpv5jka2l t488;
                gm_f_place_addr_7ji15ot((void *)&t488, (void *)NULL, cx, NULL, t486, t487);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t488))[0] != 0)) {
                    gm_tqgtd5gtgj65s t489;
                    GM_ST(uint8_t, (uint8_t *)&t489 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t489 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t489 + 4, 0);
                    ((uint8_t *)&t489)[0] = 1;
                    GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t489 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t488) + 8)));
                    F.ret = t489;
                    goto gm_fret;
                }
                gm_b16_8 t490 = GM_LD(gm_b16_8, (((uint8_t *)&t488) + 8));
                gm_l46 = t490;
            }
            {
                void * t491 = (void *)((uint8_t *)&F.l26);
                gm_t3877pk7lv748 t492;
                gm_b16_8 t493 = gm_l46;
                GM_ST(gm_b16_8, (uint8_t *)&t492 + 0, t493);
                void * t494 = GM_LD(void *, (((uint8_t *)&F.l26) + 0));
                void * t495 = (void *)(((uint8_t *)t494) + 8);
                void * t496 = gm_l3;
                gm_t88rcucg1b7uk t497 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t496) + 0));
                uint64_t t498 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t495, t497);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint64_t, (uint8_t *)&t492 + 16, t498);
                gm_b0_1 t499 = gm_f_linef_iin7hfk((void *)NULL, cx, NULL, t491, &t492);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t500 = (void *)((uint8_t *)&F.l26);
                gm_b16_8 t501;
                GM_ST(void *, (uint8_t *)&t501, (void *)(gm_svj967tkdfgka));
                GM_ST(uintptr_t, (uint8_t *)&t501 + 8, (uintptr_t)(14));
                gm_b0_1 t502 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t500, t501);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix475;
ie475:;
            void * t504 = GM_LD(void *, (((uint8_t *)&F.l26) + 0));
            void * t505 = (void *)(((uint8_t *)t504) + 8);
            void * t506 = gm_l3;
            gm_t88rcucg1b7uk t507 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t506) + 0));
            uint8_t t508 = gm_f_is_never_in5ftma(t505, GM_CONV(gm_csguankko4lmhd, t507));
            uint8_t t509 = (uint8_t)!t508;
            if (!t509) goto ie503;
            {
                void * t511 = (void *)((uint8_t *)&F.l26);
                void * t512 = gm_l3;
                gm_b16_8 t513;
                GM_ST(void *, (uint8_t *)&t513, (void *)(gm_svg1jvtfj7fgp));
                GM_ST(uintptr_t, (uint8_t *)&t513 + 8, (uintptr_t)(5));
                gm_tnrltbc61i6v1 t514;
                GM_ST(uint8_t, (uint8_t *)&t514 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t514 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t514 + 4, 0);
                GM_ST(uint32_t, (uint8_t *)&t514 + 8, 0);
                GM_ST(uint8_t, ((uint8_t *)&t514 + 0), ((uint8_t)0ULL));
                gm_tqgtd5gtgj65s t515;
                gm_f_expr_into_dn9kavo((void *)&t515, (void *)NULL, cx, NULL, t511, t512, t513, t514);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t515))[0] != 0)) {
                    gm_tqgtd5gtgj65s t516;
                    GM_ST(uint8_t, (uint8_t *)&t516 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t516 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t516 + 4, 0);
                    ((uint8_t *)&t516)[0] = 1;
                    GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t516 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t515) + 8)));
                    F.ret = t516;
                    goto gm_fret;
                }
                gm_b0_1 t517 = GM_LD(gm_b0_1, (((uint8_t *)&t515) + 8));
            }
            goto ix503;
ie503:;
            {
                {
                    void * t520 = (void *)((uint8_t *)&F.l26);
                    void * t521 = gm_l3;
                    gm_t5o0tpv5jka2l t522;
                    gm_f_expr_porbc9t((void *)&t522, (void *)NULL, cx, NULL, t520, t521);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t522))[0] != 0)) {
                        gm_tqgtd5gtgj65s t523;
                        GM_ST(uint8_t, (uint8_t *)&t523 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t523 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t523 + 4, 0);
                        ((uint8_t *)&t523)[0] = 1;
                        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t523 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t522) + 8)));
                        F.ret = t523;
                        goto gm_fret;
                    }
                    gm_b16_8 t524 = GM_LD(gm_b16_8, (((uint8_t *)&t522) + 8));
                }
            }
ix503:;
ix475:;
ix450:;
        }
        {
            uint8_t t526 = GM_LD(uint8_t, (((uint8_t *)&F.l26) + 297));
            if (!t526) goto ie525;
            {
                void * t528 = (void *)((uint8_t *)&F.l26);
                gm_b16_8 t529;
                GM_ST(void *, (uint8_t *)&t529, (void *)(gm_s0r850bpi2rnm));
                GM_ST(uintptr_t, (uint8_t *)&t529 + 8, (uintptr_t)(13));
                gm_b0_1 t530 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t528, t529);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t531 = (void *)((uint8_t *)&F.l26);
                gm_b16_8 t532;
                GM_ST(void *, (uint8_t *)&t532, (void *)(gm_s2hi6cc9n75l5));
                GM_ST(uintptr_t, (uint8_t *)&t532 + 8, (uintptr_t)(6));
                gm_b0_1 t533 = gm_f_label_710oev4((void *)NULL, cx, NULL, t531, t532);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t534 = (void *)((uint8_t *)&F.l26);
                gm_b16_8 t535;
                GM_ST(void *, (uint8_t *)&t535, (void *)(gm_spdft68ac8308));
                GM_ST(uintptr_t, (uint8_t *)&t535 + 8, (uintptr_t)(94));
                gm_b0_1 t536 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t534, t535);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t537 = (void *)((uint8_t *)&F.l26);
                gm_b16_8 t538;
                GM_ST(void *, (uint8_t *)&t538, (void *)(gm_sgdqi8a9n3f12));
                GM_ST(uintptr_t, (uint8_t *)&t538 + 8, (uintptr_t)(14));
                gm_b0_1 t539 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t537, t538);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix525;
ie525:;
ix525:;
        }
        {
            gm_b16_8 t540;
            GM_ST(void *, (uint8_t *)&t540, (void *)(gm_sr4qbrk6tlgaa));
            GM_ST(uintptr_t, (uint8_t *)&t540 + 8, (uintptr_t)(0));
            gm_l47 = t540;
        }
        {
            void * t542 = GM_LD(void *, (((uint8_t *)&F.l26) + 0));
            uint8_t t543 = GM_LD(uint8_t, ((((uint8_t *)t542) + 1136) + 1));
            if (!t543) goto ie541;
            {
                gm_b16_8 t545 = gm_l32;
                gm_tnpifv20irnph t546;
                GM_ST(gm_b16_8, ((uint8_t *)&t546 + 0), t545);
                gm_tabb710d9mq2e t547;
                gm_f_format_d6fj9tl((void *)&t547, (void *)NULL, cx, NULL, t546);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t547))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t547) + 8, gm_srcmok80ho8hh01, 228, 33);
                gm_b16_8 t548 = GM_LD(gm_b16_8, (((uint8_t *)&t547) + 8));
                gm_b16_8 t549 = GM_LD(gm_b16_8, ((uint8_t *)&t548));
                gm_l47 = t549;
            }
            goto ix541;
ie541:;
ix541:;
        }
        {
            uint8_t t551 = gm_l6;
            if (!t551) goto ie550;
            {
                void * t553 = (void *)(((uint8_t *)&F.l26) + 184);
                gm_t6j6smj7qoctu t554;
                gm_b16_8 t555 = gm_l47;
                GM_ST(gm_b16_8, (uint8_t *)&t554 + 0, t555);
                gm_b16_8 t556 = gm_l47;
                GM_ST(gm_b16_8, (uint8_t *)&t554 + 16, t556);
                gm_tftb1odc739t5 t557 = gm_f_write_va1cfg2(t553, &t554);
                if (((((uint8_t *)&t557))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t557) + 1, gm_srcmok80ho8hh01, 231, 9);
                gm_b0_1 t558 = GM_LD(gm_b0_1, (((uint8_t *)&t557) + 1));
            }
            goto ix550;
ie550:;
            uint8_t t560 = gm_l7;
            if (!t560) goto ie559;
            {
                void * t562 = (void *)(((uint8_t *)&F.l26) + 184);
                gm_t6j6smj7qoctu t563;
                gm_b16_8 t564 = gm_l47;
                GM_ST(gm_b16_8, (uint8_t *)&t563 + 0, t564);
                gm_b16_8 t565 = gm_l47;
                GM_ST(gm_b16_8, (uint8_t *)&t563 + 16, t565);
                gm_tftb1odc739t5 t566 = gm_f_write_m7rqe8h(t562, &t563);
                if (((((uint8_t *)&t566))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t566) + 1, gm_srcmok80ho8hh01, 233, 9);
                gm_b0_1 t567 = GM_LD(gm_b0_1, (((uint8_t *)&t566) + 1));
            }
            goto ix559;
ie559:;
            uint64_t t569 = GM_LD(uintptr_t, ((uint8_t *)&gm_l47) + 8);
            uint8_t t570 = ((t569 == ((uint64_t)0ULL)));
            if (!t570) goto ie568;
            {
                gm_b16_8 t572;
                GM_ST(void *, (uint8_t *)&t572, (void *)(gm_svg1jvtfj7fgp));
                GM_ST(uintptr_t, (uint8_t *)&t572 + 8, (uintptr_t)(5));
                gm_l48 = t572;
            }
            {
                uint8_t t574 = gm_l8;
                uint8_t t575 = (uint8_t)!t574;
                if (!t575) goto ie573;
                {
                    void * t577 = (void *)((uint8_t *)&F.l26);
                    void * t578 = gm_l2;
                    gm_t88rcucg1b7uk t579 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t578) + 64));
                    gm_b16_8 t580;
                    GM_ST(void *, (uint8_t *)&t580, (void *)(gm_svg1jvtfj7fgp));
                    GM_ST(uintptr_t, (uint8_t *)&t580 + 8, (uintptr_t)(5));
                    gm_b16_8 t581 = gm_f_plain_value_e380k1u((void *)NULL, cx, NULL, t577, t579, t580);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l48 = t581;
                }
                goto ix573;
ie573:;
ix573:;
            }
            {
                void * t582 = (void *)(((uint8_t *)&F.l26) + 184);
                gm_t6j6smj7qoctu t583;
                gm_b16_8 t584 = gm_l48;
                GM_ST(gm_b16_8, (uint8_t *)&t583 + 0, t584);
                gm_b16_8 t585 = gm_l48;
                GM_ST(gm_b16_8, (uint8_t *)&t583 + 16, t585);
                gm_tftb1odc739t5 t586 = gm_f_write_3c9chj4(t582, &t583);
                if (((((uint8_t *)&t586))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t586) + 1, gm_srcmok80ho8hh01, 237, 9);
                gm_b0_1 t587 = GM_LD(gm_b0_1, (((uint8_t *)&t586) + 1));
            }
            goto ix568;
ie568:;
            {
                gm_b16_8 t589;
                GM_ST(void *, (uint8_t *)&t589, (void *)(gm_ss03naco6s5l7));
                GM_ST(uintptr_t, (uint8_t *)&t589 + 8, (uintptr_t)(4));
                gm_l49 = t589;
            }
            {
                uint8_t t591 = gm_l8;
                uint8_t t592 = (uint8_t)!t591;
                if (!t592) goto ie590;
                {
                    void * t594 = (void *)((uint8_t *)&F.l26);
                    void * t595 = gm_l2;
                    gm_t88rcucg1b7uk t596 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t595) + 64));
                    gm_b16_8 t597;
                    GM_ST(void *, (uint8_t *)&t597, (void *)(gm_ss03naco6s5l7));
                    GM_ST(uintptr_t, (uint8_t *)&t597 + 8, (uintptr_t)(4));
                    gm_b16_8 t598 = gm_f_plain_value_e380k1u((void *)NULL, cx, NULL, t594, t596, t597);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l49 = t598;
                }
                goto ix590;
ie590:;
ix590:;
            }
            {
                void * t599 = GM_LD(void *, (((uint8_t *)&F.l26) + 0));
                void * t600 = (void *)(((uint8_t *)t599) + 8);
                void * t601 = gm_l2;
                gm_t88rcucg1b7uk t602 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t601) + 64));
                gm_b16_8 t603 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t600, t602);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l50 = t603;
            }
            {
                gm_tfeab279c16rr t604;
                gm_b16_8 t605 = gm_l50;
                GM_ST(gm_b16_8, (uint8_t *)&t604 + 0, t605);
                gm_b16_8 t606 = gm_l47;
                GM_ST(gm_b16_8, (uint8_t *)&t604 + 16, t606);
                gm_b16_8 t607 = gm_l49;
                GM_ST(gm_b16_8, (uint8_t *)&t604 + 32, t607);
                gm_tabb710d9mq2e t608;
                gm_f_format_hi89jlk((void *)&t608, (void *)NULL, cx, NULL, &t604);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t608))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t608) + 8, gm_srcmok80ho8hh01, 242, 17);
                gm_b16_8 t609 = GM_LD(gm_b16_8, (((uint8_t *)&t608) + 8));
                gm_l51 = t609;
            }
            {
                void * t610 = (void *)(((uint8_t *)&F.l26) + 184);
                gm_t6j6smj7qoctu t611;
                gm_b16_8 t612 = gm_l51;
                GM_ST(gm_b16_8, (uint8_t *)&t611 + 0, t612);
                gm_b16_8 t613 = gm_l51;
                GM_ST(gm_b16_8, (uint8_t *)&t611 + 16, t613);
                gm_tftb1odc739t5 t614 = gm_f_write_ih4qokb(t610, &t611);
                if (((((uint8_t *)&t614))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t614) + 1, gm_srcmok80ho8hh01, 243, 9);
                gm_b0_1 t615 = GM_LD(gm_b0_1, (((uint8_t *)&t614) + 1));
            }
ix568:;
ix559:;
ix550:;
        }
        {
            uint8_t t617 = gm_l6;
            uint8_t t618 = (uint8_t)!t617;
            if (!t618) goto ie616;
            {
                uint64_t t620 = GM_LD(uint64_t, (((uint8_t *)&F.l26) + 304));
                void * t621 = GM_LD(void *, (((uint8_t *)&F.l26) + 0));
                void * t622 = (void *)(((uint8_t *)t621) + 8);
                void * t623 = gm_l2;
                gm_t88rcucg1b7uk t624 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t623) + 64));
                uint64_t t625 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t622, t624);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint64_t r626; bool o627 = __builtin_mul_overflow(((uint64_t)2ULL), t625, &r626);
                if (o627) gm_trap("integer overflow in `*`", gm_srcmok80ho8hh01, 246, 39);
                uint64_t t628 = r626;
                uint64_t r629; bool o630 = __builtin_add_overflow(t620, t628, &r629);
                if (o630) gm_trap("integer overflow in `+`", gm_srcmok80ho8hh01, 246, 22);
                uint64_t t631 = r629;
                GM_ST(uint64_t, (((uint8_t *)&F.l26) + 304), t631);
            }
            goto ix616;
ie616:;
ix616:;
        }
        {
            void * t632 = (void *)((uint8_t *)&F.l26);
            gm_b0_1 t633 = gm_f_end_lines_99v0bhd((void *)NULL, cx, NULL, t632);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_b16_8 t634 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l26) + 184) + 0));
            gm_l52 = t634;
        }
        {
            gm_b16_8 t635 = gm_l52;
            gm_b16_8 t636 = GM_LD(gm_b16_8, ((uint8_t *)&t635));
            gm_b16_8 t637;
            GM_ST(void *, (uint8_t *)&t637, (void *)(gm_so514oj5nbhcu));
            GM_ST(uintptr_t, (uint8_t *)&t637 + 8, (uintptr_t)(1));
            gm_tf5srh4mp4b3i t638 = gm_f_find_nab29co(t636, t637);
            uint64_t t639;
            if (((((uint8_t *)&t638))[8] != 0)) {
                uint64_t t640 = GM_LD(uint64_t, (((uint8_t *)&t638) + 0));
                t639 = t640;
            } else {
                uint64_t t641 = GM_LD(uintptr_t, ((uint8_t *)&gm_l52) + 8);
                t639 = t641;
            }
            gm_l53 = t639;
        }
        {
            uint64_t t642 = GM_LD(uintptr_t, ((uint8_t *)&gm_l52) + 8);
            void * t643 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l52)));
            uint64_t t644 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t645 = gm_l53;
            uint64_t t646 = (uintptr_t)t645;
            if (t644 > t646 || t646 > (uintptr_t)t642) gm_trap_slice(t644, t646, t642, gm_srcmok80ho8hh01, 252, 22);
            gm_b16_8 t647;
            GM_ST(void *, (uint8_t *)&t647, (void *)(((uint8_t *)t643 + t644 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t647 + 8, (uintptr_t)(t646 - t644));
            gm_b16_8 t648 = GM_LD(gm_b16_8, ((uint8_t *)&t647));
            gm_l54 = t648;
        }
        {
            void * t650 = GM_LD(void *, (((uint8_t *)&F.l26) + 0));
            uint8_t t651 = GM_LD(uint8_t, ((((uint8_t *)t650) + 1136) + 1));
            uint8_t t652 = (uint8_t)!t651;
            if (!t652) goto ie649;
            uint32_t t653 = GM_LD(uint32_t, (((uint8_t *)&F.l26) + 232));
            uint32_t t654 = GM_LD(uint32_t, gm_csshki16t34);
            uint8_t t655 = (t653 <= t654);
            if (!t655) goto ie649;
            void * t656 = gm_l2;
            uint8_t t657;
            if (((((((uint8_t *)t656) + 145) + 1))[1] != 0)) goto mn659;
            t657 = ((uint8_t)1);
            goto me658;
mn659:;
            t657 = ((uint8_t)0);
            goto me658;
mn660:;
            gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 253, 65);
me658:;
            if (!t657) goto ie649;
            {
                gm_b16_8 t662 = gm_l54;
                gm_b16_8 t663;
                GM_ST(void *, (uint8_t *)&t663, (void *)(gm_skdv3lmatcupg));
                GM_ST(uintptr_t, (uint8_t *)&t663 + 8, (uintptr_t)(20));
                gm_b16_8 t664;
                GM_ST(void *, (uint8_t *)&t664, (void *)(gm_sapbk38ibps8q));
                GM_ST(uintptr_t, (uint8_t *)&t664 + 8, (uintptr_t)(27));
                gm_b16_8 t665 = gm_f_replacen1_7ksfmmn((void *)NULL, cx, NULL, t662, t663, t664);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l54 = t665;
            }
            goto ix649;
ie649:;
ix649:;
        }
        {
            uint32_t t667 = GM_LD(uint32_t, (((uint8_t *)&F.l26) + 360));
            uint32_t t668 = GM_LD(uint32_t, gm_cvpsnk4jni3);
            uint8_t t669 = (t667 > t668);
            if (!t669) goto ie666;
            {
                gm_b16_8 t671 = gm_l54;
                gm_b16_8 t672;
                GM_ST(void *, (uint8_t *)&t672, (void *)(gm_skdv3lmatcupg));
                GM_ST(uintptr_t, (uint8_t *)&t672 + 8, (uintptr_t)(20));
                gm_b16_8 t673;
                GM_ST(void *, (uint8_t *)&t673, (void *)(gm_soah6d9unb7uo));
                GM_ST(uintptr_t, (uint8_t *)&t673 + 8, (uintptr_t)(31));
                gm_b16_8 t674 = gm_f_replacen1_7ksfmmn((void *)NULL, cx, NULL, t671, t672, t673);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l54 = t674;
            }
            goto ix666;
ie666:;
ix666:;
        }
        {
            uint32_t t676 = GM_LD(uint32_t, (((uint8_t *)&F.l26) + 360));
            uint32_t t677 = GM_LD(uint32_t, gm_ctq6t8frtor);
            uint8_t t678 = (t676 > t677);
            if (!t678) goto ie675;
            gm_b16_8 t679 = gm_l54;
            gm_b16_8 t680;
            GM_ST(void *, (uint8_t *)&t680, (void *)(gm_soa9mnl4ahjud));
            GM_ST(uintptr_t, (uint8_t *)&t680 + 8, (uintptr_t)(7));
            uint8_t t681 = gm_f_starts_with_6vvt01e(t679, t680);
            if (!t681) goto ie675;
            gm_b16_8 t682 = gm_l54;
            gm_b16_8 t683;
            GM_ST(void *, (uint8_t *)&t683, (void *)(gm_s65rai75evpi7));
            GM_ST(uintptr_t, (uint8_t *)&t683 + 8, (uintptr_t)(13));
            uint8_t t684 = gm_f_contains_gkki4kq(t682, t683);
            uint8_t t685 = (uint8_t)!t684;
            if (!t685) goto ie675;
            {
                gm_b16_8 t687 = gm_l54;
                gm_b16_8 t688;
                GM_ST(void *, (uint8_t *)&t688, (void *)(gm_soa9mnl4ahjud));
                GM_ST(uintptr_t, (uint8_t *)&t688 + 8, (uintptr_t)(7));
                gm_b16_8 t689;
                GM_ST(void *, (uint8_t *)&t689, (void *)(gm_smap40unho18s));
                GM_ST(uintptr_t, (uint8_t *)&t689 + 8, (uintptr_t)(18));
                gm_b16_8 t690 = gm_f_replacen1_7ksfmmn((void *)NULL, cx, NULL, t687, t688, t689);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l54 = t690;
            }
            goto ix675;
ie675:;
ix675:;
        }
        {
            void * t691 = gm_l0;
            void * t692 = (void *)(((uint8_t *)t691) + 1928);
            gm_b16_8 t693 = gm_l54;
            gm_tftb1odc739t5 t694 = gm_f_extend_0cs85gs(t692, t693);
            if (((((uint8_t *)&t694))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t694) + 1, gm_srcmok80ho8hh01, 264, 5);
            gm_b0_1 t695 = GM_LD(gm_b0_1, (((uint8_t *)&t694) + 1));
        }
        {
            if (!(((((uint8_t *)&F.l26) + 312))[8] != 0)) goto ie696;
            uint64_t t697 = GM_LD(uint64_t, (((uint8_t *)&F.l26) + 312));
            gm_l55 = t697;
            {
                void * t699 = gm_l0;
                void * t700 = (void *)(((uint8_t *)t699) + 1928);
                uint64_t t701 = GM_LD(uintptr_t, ((uint8_t *)&gm_l52) + 8);
                void * t702 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l52)));
                uint64_t t703 = gm_l53;
                uint64_t t704 = (uintptr_t)t703;
                uint64_t t705 = gm_l55;
                uint64_t t706 = (uintptr_t)t705;
                if (t704 > t706 || t706 > (uintptr_t)t701) gm_trap_slice(t704, t706, t701, gm_srcmok80ho8hh01, 266, 25);
                gm_b16_8 t707;
                GM_ST(void *, (uint8_t *)&t707, (void *)(((uint8_t *)t702 + t704 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t707 + 8, (uintptr_t)(t706 - t704));
                gm_b16_8 t708 = GM_LD(gm_b16_8, ((uint8_t *)&t707));
                gm_tftb1odc739t5 t709 = gm_f_extend_0cs85gs(t700, t708);
                if (((((uint8_t *)&t709))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t709) + 1, gm_srcmok80ho8hh01, 266, 9);
                gm_b0_1 t710 = GM_LD(gm_b0_1, (((uint8_t *)&t709) + 1));
            }
            {
                void * t711 = gm_l0;
                void * t712 = (void *)(((uint8_t *)t711) + 1928);
                uint64_t t713 = GM_LD(uint64_t, (((uint8_t *)&F.l26) + 304));
                gm_t4ce0jd3tdro4 t714;
                GM_ST(uint64_t, ((uint8_t *)&t714 + 0), t713);
                gm_tftb1odc739t5 t715 = gm_f_write_l0vqbl8(t712, t714);
                if (((((uint8_t *)&t715))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t715) + 1, gm_srcmok80ho8hh01, 267, 9);
                gm_b0_1 t716 = GM_LD(gm_b0_1, (((uint8_t *)&t715) + 1));
            }
            {
                void * t717 = gm_l0;
                void * t718 = (void *)(((uint8_t *)t717) + 1928);
                uint64_t t719 = GM_LD(uintptr_t, ((uint8_t *)&gm_l52) + 8);
                void * t720 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l52)));
                uint64_t t721 = gm_l55;
                gm_b16_8 t722;
                GM_ST(void *, (uint8_t *)&t722, (void *)(gm_sv394jlms97cd));
                GM_ST(uintptr_t, (uint8_t *)&t722 + 8, (uintptr_t)(13));
                uint64_t t723 = GM_LD(uintptr_t, ((uint8_t *)&t722) + 8);
                uint64_t r724; bool o725 = __builtin_add_overflow(t721, t723, &r724);
                if (o725) gm_trap("integer overflow in `+`", gm_srcmok80ho8hh01, 268, 30);
                uint64_t t726 = r724;
                uint64_t t727 = (uintptr_t)t726;
                if (t727 > (uintptr_t)t719 || (uintptr_t)t719 > (uintptr_t)t719) gm_trap_slice(t727, (uintptr_t)t719, t719, gm_srcmok80ho8hh01, 268, 25);
                gm_b16_8 t728;
                GM_ST(void *, (uint8_t *)&t728, (void *)(((uint8_t *)t720 + t727 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t728 + 8, (uintptr_t)((uintptr_t)t719 - t727));
                gm_b16_8 t729 = GM_LD(gm_b16_8, ((uint8_t *)&t728));
                gm_tftb1odc739t5 t730 = gm_f_extend_0cs85gs(t718, t729);
                if (((((uint8_t *)&t730))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t730) + 1, gm_srcmok80ho8hh01, 268, 9);
                gm_b0_1 t731 = GM_LD(gm_b0_1, (((uint8_t *)&t730) + 1));
            }
            goto ix696;
ie696:;
            {
                void * t733 = gm_l0;
                void * t734 = (void *)(((uint8_t *)t733) + 1928);
                uint64_t t735 = GM_LD(uintptr_t, ((uint8_t *)&gm_l52) + 8);
                void * t736 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l52)));
                uint64_t t737 = gm_l53;
                uint64_t t738 = (uintptr_t)t737;
                if (t738 > (uintptr_t)t735 || (uintptr_t)t735 > (uintptr_t)t735) gm_trap_slice(t738, (uintptr_t)t735, t735, gm_srcmok80ho8hh01, 270, 25);
                gm_b16_8 t739;
                GM_ST(void *, (uint8_t *)&t739, (void *)(((uint8_t *)t736 + t738 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t739 + 8, (uintptr_t)((uintptr_t)t735 - t738));
                gm_b16_8 t740 = GM_LD(gm_b16_8, ((uint8_t *)&t739));
                gm_tftb1odc739t5 t741 = gm_f_extend_0cs85gs(t734, t740);
                if (((((uint8_t *)&t741))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t741) + 1, gm_srcmok80ho8hh01, 270, 9);
                gm_b0_1 t742 = GM_LD(gm_b0_1, (((uint8_t *)&t741) + 1));
            }
ix696:;
        }
    }
    gm_b0_1 t743;
    memset(&(*(gm_tqgtd5gtgj65s *)gm_out), 0, sizeof (*(gm_tqgtd5gtgj65s *)gm_out));
    ((uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out))[0] = 0;
    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 8, t743);
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
__attribute__((aligned(4))) void gm_f_gen_fn_2fqvb90(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_t9805qtonbsag a2) {
    struct gm_fr_gen_fn_2fqvb90 F;
    void * gm_l0; /* g */
    uint32_t gm_l1; /* f */
    gm_t9805qtonbsag gm_l2; /* spec */
    void * gm_l3; /* def */
    void * gm_l4; /* body */
    gm_b16_8 gm_l5; /* in_frame */
    gm_b16_8 gm_l7; /* names */
    gm_b16_8 gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    gm_b16_8 gm_l13; /* <tmp> */
    uint64_t gm_l14; /* <tmp> */
    uint64_t gm_l15; /* i */
    gm_b16_8 gm_l17; /* c */
    gm_b16_8 gm_l18; /* n */
    gm_b16_8 gm_l19; /* alias */
    gm_b16_8 gm_l20; /* dims */
    gm_t6j6smj7qoctu gm_l21; /* view */
    gm_b16_8 gm_l23; /* <tmp> */
    uint64_t gm_l24; /* <tmp> */
    gm_tplpbqf3duvn3 gm_l25; /* w */
    GM_STACK_CHECK(&F, 1939u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        uint32_t t4 = gm_l1;
        void * t5 = gm_f_func_in17r5n((void *)NULL, cx, NULL, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l3 = t5;
    }
    {
        void * t7 = gm_l3;
        uint8_t t8;
        uint8_t t11 = GM_LD(uint8_t, ((((uint8_t *)t7) + 96) + 0));
        if (t11 != ((uint8_t)3ULL)) goto mn10;
        t8 = ((uint8_t)1);
        goto me9;
mn10:;
        t8 = ((uint8_t)0);
        goto me9;
mn12:;
        gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 53, 8);
me9:;
        if (!t8) goto ie6;
        {
            void * t14 = gm_l0;
            uint32_t t15 = gm_l1;
            gm_f_foreign_decl_rld7m78((void *)&(*(gm_tqgtd5gtgj65s *)gm_out), (void *)NULL, cx, NULL, t14, t15);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto ix6;
ie6:;
ix6:;
    }
    {
        void * t16 = gm_l3;
        void * t17 = GM_LD(void *, (((uint8_t *)t16) + 152));
        void * t18;
        if ((GM_LD(void *, ((uint8_t *)&t17)) != 0)) {
            void * t19 = GM_LD(void *, (((uint8_t *)&t17) + 0));
            t18 = t19;
        } else {
            void * t20 = gm_l3;
            gm_tl874t2atnlh3 t21 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t20) + 16));
            void * t22 = gm_l3;
            gm_b16_8 t23 = GM_LD(gm_b16_8, (((uint8_t *)t22) + 0));
            gm_tnpifv20irnph t24;
            GM_ST(gm_b16_8, ((uint8_t *)&t24 + 0), t23);
            gm_tabb710d9mq2e t25;
            gm_f_format_pu04c4a((void *)&t25, (void *)NULL, cx, NULL, t24);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t25))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t25) + 8, gm_srcmok80ho8hh01, 54, 57);
            gm_b16_8 t26 = GM_LD(gm_b16_8, (((uint8_t *)&t25) + 8));
            gm_b16_8 t27 = GM_LD(gm_b16_8, ((uint8_t *)&t26));
            gm_t08olq97rscn3 t28;
            gm_f_unsupported_531q51r((void *)&t28, (void *)NULL, cx, NULL, t21, t27);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out))[0] = 1;
            GM_ST(gm_t08olq97rscn3, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 8, t28);
            goto gm_fdone;
        }
        gm_l4 = t18;
    }
    {
        void * t30 = gm_l0;
        uint32_t t31 = gm_l1;
        uint8_t t32 = gm_f_frame_done_014nc4i((void *)NULL, cx, NULL, t30, t31);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t32) goto ie29;
        {
            void * t34 = gm_l0;
            uint32_t t35 = gm_l1;
            void * t36 = gm_l3;
            void * t37 = gm_l4;
            gm_t9805qtonbsag t38 = gm_l2;
            gm_f_gen_body_j6ku8qk((void *)&(*(gm_tqgtd5gtgj65s *)gm_out), (void *)NULL, cx, NULL, t34, t35, t36, t37, t38);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto ix29;
ie29:;
ix29:;
    }
    {
        void * t39 = gm_l0;
        uint32_t t40 = gm_l1;
        gm_b16_8 t41 = gm_f_frame_locals_mt4nb6o((void *)NULL, cx, NULL, t39, t40);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l5 = t41;
    }
    {
        gm_f_list_uvfu1k1((void *)&F.l6, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t42 = (void *)((uint8_t *)&F.l6);
        void * t43 = gm_l0;
        uint32_t t44 = gm_l1;
        gm_b16_8 t45 = gm_f_frame_name_stddmgg((void *)NULL, cx, NULL, t43, t44);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t46;
        GM_ST(gm_b16_8, ((uint8_t *)&t46 + 0), t45);
        gm_tftb1odc739t5 t47 = gm_f_write_j67vjku(t42, t46);
        if (((((uint8_t *)&t47))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t47) + 1, gm_srcmok80ho8hh01, 59, 5);
        gm_b0_1 t48 = GM_LD(gm_b0_1, (((uint8_t *)&t47) + 1));
    }
    {
        gm_b16_8 t49 = gm_f_empty_slice_ppl3r45();
        gm_b16_8 t50 = GM_LD(gm_b16_8, ((uint8_t *)&t49));
        gm_l7 = t50;
    }
    {
        void * t52 = gm_l0;
        uint8_t t53 = GM_LD(uint8_t, ((((uint8_t *)t52) + 1136) + 2));
        if (!t53) goto ie51;
        {
            gm_f_list_epvducg((void *)&F.l8, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                void * t56 = gm_l3;
                gm_b16_8 t57 = GM_LD(gm_b16_8, (((uint8_t *)t56) + 48));
                gm_l9 = t57;
            }
            {
                gm_l10 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t59 = gm_l10;
                    uint64_t t60 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                    uint8_t t61 = (t59 < t60);
                    if (!t61) goto lb58;
                    {
                        uint64_t t64 = gm_l10;
                        uint64_t t65 = (uintptr_t)t64;
                        uint64_t t66 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                        gm_tsoie1ppgdvcg t69 = GM_LD(gm_tsoie1ppgdvcg, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t65 * 40));
                        F.l11 = t69;
                        {
                            void * t71 = (void *)((uint8_t *)&F.l8);
                            gm_b16_8 t72 = GM_LD(gm_b16_8, (((uint8_t *)&F.l11) + 0));
                            gm_tftb1odc739t5 t73 = gm_f_push_3ef746b(t71, t72);
                            if (((((uint8_t *)&t73))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t73) + 1, gm_srcmok80ho8hh01, 64, 31);
                            gm_b0_1 t74 = GM_LD(gm_b0_1, (((uint8_t *)&t73) + 1));
                        }
                        goto me67;
mn68:;
                        gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 64, 9);
me67:;
                    }
lc58:;
                    {
                        uint64_t t76 = gm_l10;
                        uint64_t r77; bool o78 = __builtin_add_overflow(t76, ((uint64_t)1ULL), &r77);
                        uint64_t t79 = r77;
                        gm_l10 = t79;
                    }
                }
lb58:;
            }
        }
        {
            gm_f_hash_map_cuh4fs9((void *)&F.l12, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_b16_8 t80 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 0));
            gm_b16_8 t81 = GM_LD(gm_b16_8, ((uint8_t *)&t80));
            void * t82 = (void *)((uint8_t *)&F.l12);
            gm_b16_8 t83 = gm_f_unique_members_mrojmjf((void *)NULL, cx, NULL, t81, t82);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t84 = GM_LD(gm_b16_8, ((uint8_t *)&t83));
            gm_l7 = t84;
        }
        goto ix51;
ie51:;
ix51:;
    }
    {
        {
            void * t86 = gm_l3;
            gm_b16_8 t87 = GM_LD(gm_b16_8, (((uint8_t *)t86) + 48));
            gm_l13 = t87;
        }
        {
            gm_l14 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t89 = gm_l14;
                uint64_t t90 = GM_LD(uintptr_t, ((uint8_t *)&gm_l13) + 8);
                uint8_t t91 = (t89 < t90);
                if (!t91) goto lb88;
                {
                    gm_t5im2v50m3hk6 t94;
                    uint64_t t95 = gm_l14;
                    GM_ST(uint64_t, (uint8_t *)&t94 + 0, t95);
                    uint64_t t96 = gm_l14;
                    uint64_t t97 = (uintptr_t)t96;
                    uint64_t t98 = GM_LD(uintptr_t, ((uint8_t *)&gm_l13) + 8);
                    memcpy((uint8_t *)&t94 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l13)) + t97 * 40), 40);
                    uint64_t t101 = GM_LD(uint64_t, (((uint8_t *)&t94) + 0));
                    gm_l15 = t101;
                    F.l16 = (*(gm_tsoie1ppgdvcg *)(((uint8_t *)&t94) + 8));
                    {
                        uint64_t t104 = gm_l15;
                        uint64_t t105 = (uintptr_t)t104;
                        uint64_t t106 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                        if (t105 >= (uintptr_t)(t106)) gm_trap_index(t105, t106, gm_srcmok80ho8hh01, 69, 16);
                        uint8_t t107 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t105 * 1));
                        uint8_t t108 = (uint8_t)!t107;
                        if (!t108) goto ie103;
                        {
                            goto lc88;
                        }
                        goto ix103;
ie103:;
ix103:;
                    }
                    {
                        void * t110 = gm_l0;
                        void * t111 = (void *)(((uint8_t *)t110) + 8);
                        gm_t88rcucg1b7uk t112 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l16) + 16));
                        gm_b16_8 t113 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t111, t112);
                        if (gm_unw.kind) {
                            goto gm_ll92;
                        }
                        gm_l17 = t113;
                    }
                    {
                        uint64_t t115 = gm_l15;
                        uint64_t t116 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                        uint8_t t117 = (t115 < t116);
                        if (!t117) goto ie114;
                        uint64_t t118 = gm_l15;
                        uint64_t t119 = (uintptr_t)t118;
                        uint64_t t120 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                        if (t119 >= (uintptr_t)(t120)) gm_trap_index(t119, t120, gm_srcmok80ho8hh01, 71, 30);
                        uint8_t t121;
                        if (((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t119 * 24))[16] != 0)) goto mn123;
                        t121 = ((uint8_t)0);
                        goto me122;
mn123:;
                        t121 = ((uint8_t)1);
                        goto me122;
mn124:;
                        gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 71, 30);
me122:;
                        if (!t121) goto ie114;
                        {
                            uint64_t t126 = gm_l15;
                            uint64_t t127 = (uintptr_t)t126;
                            uint64_t t128 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                            if (t127 >= (uintptr_t)(t128)) gm_trap_index(t127, t128, gm_srcmok80ho8hh01, 72, 18);
                            gm_th2lqbo77pvsc t129 = GM_LD(gm_th2lqbo77pvsc, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t127 * 24));
                            if (!((((uint8_t *)&t129))[16] != 0)) gm_trap("unwrap of null", gm_srcmok80ho8hh01, 72, 18);
                            gm_b16_8 t130 = GM_LD(gm_b16_8, ((uint8_t *)&t129));
                            gm_l18 = t130;
                        }
                        {
                            gm_b16_8 t131 = gm_l17;
                            gm_l19 = t131;
                        }
                        {
                            gm_b16_8 t132;
                            GM_ST(void *, (uint8_t *)&t132, (void *)(gm_sr4qbrk6tlgaa));
                            GM_ST(uintptr_t, (uint8_t *)&t132 + 8, (uintptr_t)(0));
                            gm_l20 = t132;
                        }
                        {
                            void * t134 = gm_l0;
                            void * t135 = (void *)(((uint8_t *)t134) + 8);
                            gm_t88rcucg1b7uk t136 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l16) + 16));
                            gm_ttu09lij36f8c t137;
                            gm_f_debug_view_gp38euq((void *)&t137, (void *)NULL, cx, NULL, t135, t136);
                            if (gm_unw.kind) {
                                goto gm_ll92;
                            }
                            if (!((((uint8_t *)&t137))[32] != 0)) goto ie133;
                            gm_l21 = (*(gm_t6j6smj7qoctu *)(((uint8_t *)&t137) + 0));
                            {
                                gm_b16_8 t139 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l21) + 0));
                                gm_l19 = t139;
                            }
                            {
                                gm_b16_8 t140 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l21) + 16));
                                gm_l20 = t140;
                            }
                            goto ix133;
ie133:;
ix133:;
                        }
                        {
                            void * t141 = gm_l0;
                            void * t142 = (void *)(((uint8_t *)t141) + 8);
                            gm_b16_8 t143 = gm_l18;
                            gm_b0_1 t144 = gm_f_add_member_name_j3i0eje((void *)NULL, cx, NULL, t142, t143);
                            if (gm_unw.kind) {
                                goto gm_ll92;
                            }
                        }
                        {
                            void * t145 = (void *)((uint8_t *)&F.l6);
                            gm_tcrdb1inj22os t146;
                            gm_b16_8 t147 = gm_l17;
                            GM_ST(gm_b16_8, (uint8_t *)&t146 + 0, t147);
                            uint64_t t148 = gm_l15;
                            GM_ST(uint64_t, (uint8_t *)&t146 + 16, t148);
                            gm_b16_8 t149 = gm_l19;
                            GM_ST(gm_b16_8, (uint8_t *)&t146 + 24, t149);
                            gm_b16_8 t150 = gm_l18;
                            GM_ST(gm_b16_8, (uint8_t *)&t146 + 40, t150);
                            gm_b16_8 t151 = gm_l20;
                            GM_ST(gm_b16_8, (uint8_t *)&t146 + 56, t151);
                            gm_tftb1odc739t5 t152 = gm_f_write_s858s21(t145, &t146);
                            if (((((uint8_t *)&t152))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t152) + 1, gm_srcmok80ho8hh01, 80, 13);
                            gm_b0_1 t153 = GM_LD(gm_b0_1, (((uint8_t *)&t152) + 1));
                        }
                        goto ix114;
ie114:;
                        {
                            void * t155 = (void *)((uint8_t *)&F.l6);
                            gm_tq3m5qgvkpje8 t156;
                            gm_b16_8 t157 = gm_l17;
                            GM_ST(gm_b16_8, (uint8_t *)&t156 + 0, t157);
                            uint64_t t158 = gm_l15;
                            GM_ST(uint64_t, (uint8_t *)&t156 + 16, t158);
                            gm_b16_8 t159 = GM_LD(gm_b16_8, (((uint8_t *)&F.l16) + 0));
                            GM_ST(gm_b16_8, (uint8_t *)&t156 + 24, t159);
                            gm_tftb1odc739t5 t160 = gm_f_write_s0f8vaq(t155, &t156);
                            if (((((uint8_t *)&t160))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t160) + 1, gm_srcmok80ho8hh01, 82, 13);
                            gm_b0_1 t161 = GM_LD(gm_b0_1, (((uint8_t *)&t160) + 1));
                        }
ix114:;
                    }
                    goto me99;
mn100:;
                    gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 68, 5);
me99:;
                }
lc88:;
                {
                    uint64_t t163 = gm_l14;
                    uint64_t r164; bool o165 = __builtin_add_overflow(t163, ((uint64_t)1ULL), &r164);
                    uint64_t t166 = r164;
                    gm_l14 = t166;
                }
            }
gm_ll92:;
            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb88; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc88; }
            }
            goto gm_lad;
lb88:;
        }
    }
    {
        void * t167 = (void *)((uint8_t *)&F.l6);
        void * t168 = gm_l0;
        void * t169 = (void *)(((uint8_t *)t168) + 8);
        void * t170 = gm_l3;
        gm_t88rcucg1b7uk t171 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t170) + 64));
        gm_b16_8 t172 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t169, t171);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t173;
        GM_ST(gm_b16_8, ((uint8_t *)&t173 + 0), t172);
        gm_tftb1odc739t5 t174 = gm_f_write_shh24f1(t167, t173);
        if (((((uint8_t *)&t174))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t174) + 1, gm_srcmok80ho8hh01, 85, 5);
        gm_b0_1 t175 = GM_LD(gm_b0_1, (((uint8_t *)&t174) + 1));
    }
    {
        gm_f_list_9ljroit((void *)&F.l22, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t176 = gm_l4;
        void * t177 = (void *)((uint8_t *)&F.l22);
        gm_b0_1 t178 = gm_f_collect_withs_fa29mk1((void *)NULL, cx, NULL, t176, t177);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t180 = GM_LD(gm_b16_8, (((uint8_t *)&F.l22) + 0));
            gm_l23 = t180;
        }
        {
            gm_l24 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t182 = gm_l24;
                uint64_t t183 = GM_LD(uintptr_t, ((uint8_t *)&gm_l23) + 8);
                uint8_t t184 = (t182 < t183);
                if (!t184) goto lb181;
                {
                    uint64_t t187 = gm_l24;
                    uint64_t t188 = (uintptr_t)t187;
                    uint64_t t189 = GM_LD(uintptr_t, ((uint8_t *)&gm_l23) + 8);
                    gm_tplpbqf3duvn3 t192 = GM_LD(gm_tplpbqf3duvn3, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l23)) + t188 * 8));
                    gm_l25 = t192;
                    {
                        void * t194 = (void *)((uint8_t *)&F.l6);
                        gm_tommf4j9upomc t195;
                        memset((uint8_t *)&t195 + 20, 0, 4);
                        void * t196 = gm_l0;
                        void * t197 = (void *)(((uint8_t *)t196) + 8);
                        gm_t88rcucg1b7uk t198 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l25) + 4));
                        gm_b16_8 t199 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t197, t198);
                        if (gm_unw.kind) {
                            goto gm_ll185;
                        }
                        GM_ST(gm_b16_8, (uint8_t *)&t195 + 0, t199);
                        uint32_t t200 = GM_LD(uint32_t, (((uint8_t *)&gm_l25) + 0));
                        GM_ST(uint32_t, (uint8_t *)&t195 + 16, t200);
                        gm_tftb1odc739t5 t201 = gm_f_write_ihqa4kk(t194, &t195);
                        if (((((uint8_t *)&t201))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t201) + 1, gm_srcmok80ho8hh01, 89, 9);
                        gm_b0_1 t202 = GM_LD(gm_b0_1, (((uint8_t *)&t201) + 1));
                    }
                    goto me190;
mn191:;
                    gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 88, 5);
me190:;
                }
lc181:;
                {
                    uint64_t t204 = gm_l24;
                    uint64_t r205; bool o206 = __builtin_add_overflow(t204, ((uint64_t)1ULL), &r205);
                    uint64_t t207 = r205;
                    gm_l24 = t207;
                }
            }
gm_ll185:;
            if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb181; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc181; }
            }
            goto gm_lad;
lb181:;
        }
    }
    {
        void * t208 = (void *)((uint8_t *)&F.l6);
        gm_b16_8 t209;
        GM_ST(void *, (uint8_t *)&t209, (void *)(gm_sg398rgmck67b));
        GM_ST(uintptr_t, (uint8_t *)&t209 + 8, (uintptr_t)(3));
        gm_tftb1odc739t5 t210 = gm_f_extend_0cs85gs(t208, t209);
        if (((((uint8_t *)&t210))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t210) + 1, gm_srcmok80ho8hh01, 91, 5);
        gm_b0_1 t211 = GM_LD(gm_b0_1, (((uint8_t *)&t210) + 1));
    }
    {
        void * t212 = gm_l0;
        void * t213 = (void *)(((uint8_t *)t212) + 1848);
        gm_b16_8 t214 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
        gm_b16_8 t215 = GM_LD(gm_b16_8, ((uint8_t *)&t214));
        gm_tftb1odc739t5 t216 = gm_f_extend_0cs85gs(t213, t215);
        if (((((uint8_t *)&t216))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t216) + 1, gm_srcmok80ho8hh01, 92, 5);
        gm_b0_1 t217 = GM_LD(gm_b0_1, (((uint8_t *)&t216) + 1));
    }
    {
        void * t218 = gm_l0;
        uint32_t t219 = gm_l1;
        void * t220 = gm_l3;
        void * t221 = gm_l4;
        gm_t9805qtonbsag t222 = gm_l2;
        gm_f_gen_body_j6ku8qk((void *)&(*(gm_tqgtd5gtgj65s *)gm_out), (void *)NULL, cx, NULL, t218, t219, t220, t221, t222);
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
inline __attribute__((aligned(4))) uint8_t gm_f_has_fiber_with_53c3jik(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_has_fiber_with_53c3jik F;
    void * gm_l0; /* e */
    GM_STACK_CHECK(&F, 103u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t3 = gm_l0;
        uint8_t t4 = GM_LD(uint8_t, ((((uint8_t *)t3) + 8) + 0));
        if (t4 != ((uint8_t)58ULL)) goto ie2;
        gm_t710lsrq1m8gk t5 = GM_LD(gm_t710lsrq1m8gk, ((((uint8_t *)t3) + 8) + 8));
        F.l1 = t5;
        {
            void * t8 = GM_LD(void *, (((uint8_t *)&F.l1) + 24));
            uint8_t t9;
            if ((GM_LD(void *, ((uint8_t *)&t8)) != 0)) goto mn11;
            t9 = ((uint8_t)0);
            goto me10;
mn11:;
            t9 = ((uint8_t)1);
            goto me10;
mn12:;
            gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 1292, 12);
me10:;
            if (!t9) goto ie7;
            {
                uint8_t t14 = 0;
                F.ret = ((uint8_t)1);
                goto gm_fret;
            }
            goto ix7;
ie7:;
ix7:;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t15 = gm_l0;
        gm_closure t16 = ((gm_closure){GM_PTR_TAG(gm_f_fn_i2el8vi), NULL, cx});
        uint8_t t17 = gm_f_any_child_ffc8h1k(t15, t16);
        if (gm_unw.kind) {
            goto gm_lad;
        }
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_hint_attrs_mktdhi1(void *env, gm_node *cx, gm_node *inst, gm_t1h8kp9hsu3ua a0) {
    struct gm_fr_hint_attrs_mktdhi1 F;
    gm_t1h8kp9hsu3ua gm_l0; /* h */
    uint8_t gm_l2; /* x */
    GM_STACK_CHECK(&F, 178u);
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
        if (!(((((uint8_t *)&gm_l0) + 1))[1] != 0)) goto ie2;
        uint8_t t3 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 1));
        gm_l2 = t3;
        {
            uint8_t t6 = gm_l2;
            if (!t6) goto ie5;
            {
                void * t8 = (void *)((uint8_t *)&F.l1);
                gm_b16_8 t9;
                GM_ST(void *, (uint8_t *)&t9, (void *)(gm_so0h0lkl6bp4l));
                GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(38));
                gm_tftb1odc739t5 t10 = gm_f_extend_0cs85gs(t8, t9);
                if (((((uint8_t *)&t10))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t10) + 1, gm_srcmok80ho8hh01, 407, 16);
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            }
            goto ix5;
ie5:;
            {
                void * t13 = (void *)((uint8_t *)&F.l1);
                gm_b16_8 t14;
                GM_ST(void *, (uint8_t *)&t14, (void *)(gm_s39jha51ulivf));
                GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(26));
                gm_tftb1odc739t5 t15 = gm_f_extend_0cs85gs(t13, t14);
                if (((((uint8_t *)&t15))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t15) + 1, gm_srcmok80ho8hh01, 407, 77);
                gm_b0_1 t16 = GM_LD(gm_b0_1, (((uint8_t *)&t15) + 1));
            }
ix5:;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 3));
        if (!t18) goto ie17;
        {
            void * t20 = (void *)((uint8_t *)&F.l1);
            gm_b16_8 t21;
            GM_ST(void *, (uint8_t *)&t21, (void *)(gm_s3cu59itn73k7));
            GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(22));
            gm_tftb1odc739t5 t22 = gm_f_extend_0cs85gs(t20, t21);
            if (((((uint8_t *)&t22))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t22) + 1, gm_srcmok80ho8hh01, 409, 17);
            gm_b0_1 t23 = GM_LD(gm_b0_1, (((uint8_t *)&t22) + 1));
        }
        goto ix17;
ie17:;
ix17:;
    }
    {
        uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_b16_8 t28;
            GM_ST(void *, (uint8_t *)&t28, (void *)(gm_slnuijmj5p1vk));
            GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(18));
            gm_tftb1odc739t5 t29 = gm_f_extend_0cs85gs(t27, t28);
            if (((((uint8_t *)&t29))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t29) + 1, gm_srcmok80ho8hh01, 410, 22);
            gm_b0_1 t30 = GM_LD(gm_b0_1, (((uint8_t *)&t29) + 1));
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        gm_b16_8 t31 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
        gm_b16_8 t32 = GM_LD(gm_b16_8, ((uint8_t *)&t31));
        uint8_t t33 = 0;
        F.ret = t32;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_indent_line_b3mckqa(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_indent_line_b3mckqa F;
    void * gm_l0; /* fg */
    uint64_t gm_l1; /* n */
    GM_STACK_CHECK(&F, 189u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)t2) + 224));
        uint64_t r4; bool o5 = __builtin_mul_overflow(t3, ((uint64_t)4ULL), &r4);
        if (o5) gm_trap("integer overflow in `*`", gm_srcmok80ho8hh01, 450, 10);
        uint64_t t6 = r4;
        gm_l1 = t6;
    }
    {
        for (;;) {
            uint64_t t8 = gm_l1;
            uint64_t t9 = GM_LD(uintptr_t, ((uint8_t *)gm_cc9imq1spis) + 8);
            uint8_t t10 = (t8 > t9);
            if (!t10) goto lb7;
            {
                void * t13 = gm_l0;
                void * t14 = (void *)(((uint8_t *)t13) + 184);
                gm_b16_8 t15 = GM_LD(gm_b16_8, gm_cc9imq1spis);
                gm_tftb1odc739t5 t16 = gm_f_extend_0cs85gs(t14, t15);
                if (((((uint8_t *)&t16))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t16) + 1, gm_srcmok80ho8hh01, 452, 9);
                gm_b0_1 t17 = GM_LD(gm_b0_1, (((uint8_t *)&t16) + 1));
            }
            {
                uint64_t t18 = gm_l1;
                uint64_t t19 = GM_LD(uintptr_t, ((uint8_t *)gm_cc9imq1spis) + 8);
                uint64_t r20; bool o21 = __builtin_sub_overflow(t18, t19, &r20);
                if (o21) gm_trap("integer overflow in `-`", gm_srcmok80ho8hh01, 453, 9);
                uint64_t t22 = r20;
                gm_l1 = t22;
            }
lc7:;
        }
lb7:;
    }
    {
        void * t23 = gm_l0;
        void * t24 = (void *)(((uint8_t *)t23) + 184);
        uint64_t t25 = GM_LD(uintptr_t, ((uint8_t *)gm_cc9imq1spis) + 8);
        void * t26 = ((uint8_t *)GM_LD(void *, ((uint8_t *)gm_cc9imq1spis)));
        uint64_t t27 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t28 = gm_l1;
        uint64_t t29 = (uintptr_t)t28;
        if (t27 > t29 || t29 > (uintptr_t)t25) gm_trap_slice(t27, t29, t25, gm_srcmok80ho8hh01, 455, 19);
        gm_b16_8 t30;
        GM_ST(void *, (uint8_t *)&t30, (void *)(((uint8_t *)t26 + t27 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t30 + 8, (uintptr_t)(t29 - t27));
        gm_tftb1odc739t5 t31 = gm_f_extend_0cs85gs(t24, t30);
        if (((((uint8_t *)&t31))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t31) + 1, gm_srcmok80ho8hh01, 455, 5);
        gm_b0_1 t32 = GM_LD(gm_b0_1, (((uint8_t *)&t31) + 1));
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) uint8_t gm_f_is_borrowed_qi4apdt(void * a0, uint32_t a1);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_label_710oev4(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_ladder_36amdbn(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_ladder_target_84n3aep(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_ladder_target_84n3aep F;
    void * gm_l0; /* fg */
    uint64_t gm_l1; /* i */
    void * gm_l2; /* s */
    gm_tf5srh4mp4b3i gm_l3; /* found */
    gm_tf5srh4mp4b3i gm_l4; /* none */
    GM_STACK_CHECK(&F, 555u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uintptr_t, ((((uint8_t *)t2) + 240) + 0) + 8);
        gm_l1 = t3;
    }
    {
        for (;;) {
            uint64_t t5 = gm_l1;
            uint8_t t6 = (t5 > ((uint64_t)0ULL));
            if (!t6) goto lb4;
            {
                uint64_t t9 = gm_l1;
                uint64_t r10; bool o11 = __builtin_sub_overflow(t9, ((uint64_t)1ULL), &r10);
                if (o11) gm_trap("integer overflow in `-`", gm_srcmok80ho8hh01, 640, 9);
                uint64_t t12 = r10;
                gm_l1 = t12;
            }
            {
                uint64_t t13 = gm_l1;
                uint64_t t14 = (uintptr_t)t13;
                void * t15 = gm_l0;
                uint64_t t16 = GM_LD(uintptr_t, ((((uint8_t *)t15) + 240) + 0) + 8);
                if (t14 >= (uintptr_t)(t16)) gm_trap_index(t14, t16, gm_srcmok80ho8hh01, 641, 15);
                void * t17 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t15) + 240) + 0)) + t14 * 104);
                gm_l2 = t17;
            }
            {
                uint64_t t18 = gm_l1;
                gm_tf5srh4mp4b3i t19;
                GM_ST(uint8_t, (uint8_t *)&t19 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t19 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t19 + 12, 0);
                GM_ST(uint64_t, ((uint8_t *)&t19), t18);
                ((uint8_t *)&t19)[8] = 1;
                gm_l3 = t19;
            }
            {
                void * t20 = gm_l2;
                uint8_t t23 = GM_LD(uint8_t, ((((uint8_t *)t20) + 0) + 0));
                if (t23 != ((uint8_t)0ULL)) goto mn22;
                {
                    void * t26 = gm_l2;
                    uint64_t t27 = GM_LD(uintptr_t, ((((uint8_t *)t26) + 56) + 0) + 8);
                    uint8_t t28 = ((t27 == ((uint64_t)0ULL)));
                    if (!t28) goto ie25;
                    {
                        goto lc4;
                    }
                    goto ix25;
ie25:;
ix25:;
                }
                {
                    void * t31 = gm_l2;
                    uint64_t t32 = GM_LD(uintptr_t, ((((uint8_t *)t31) + 56) + 0) + 8);
                    void * t33 = gm_l2;
                    uint64_t t34 = GM_LD(uint64_t, (((uint8_t *)t33) + 96));
                    uint8_t t35 = (t32 > t34);
                    if (!t35) goto ie30;
                    {
                        void * t37 = gm_l2;
                        void * t38 = gm_l2;
                        uint64_t t39 = GM_LD(uintptr_t, ((((uint8_t *)t38) + 56) + 0) + 8);
                        GM_ST(uint64_t, (((uint8_t *)t37) + 96), t39);
                    }
                    goto ix30;
ie30:;
ix30:;
                }
                {
                    void * t40 = gm_l2;
                    uint32_t t41 = GM_LD(uint32_t, (((uint8_t *)t40) + 48));
                    void * t42 = gm_l2;
                    uint64_t t43 = GM_LD(uintptr_t, ((((uint8_t *)t42) + 56) + 0) + 8);
                    gm_tb31k5l794t7p t44;
                    GM_ST(uint32_t, (uint8_t *)&t44 + 4, 0);
                    GM_ST(uint32_t, ((uint8_t *)&t44 + 0), t41);
                    GM_ST(uint64_t, ((uint8_t *)&t44 + 8), t43);
                    gm_tabb710d9mq2e t45;
                    gm_f_format_5bcbbmm((void *)&t45, (void *)NULL, cx, NULL, t44);
                    if (gm_unw.kind) {
                        goto gm_ll7;
                    }
                    if (((((uint8_t *)&t45))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t45) + 8, gm_srcmok80ho8hh01, 647, 25);
                    gm_b16_8 t46 = GM_LD(gm_b16_8, (((uint8_t *)&t45) + 8));
                    gm_b16_8 t47 = GM_LD(gm_b16_8, ((uint8_t *)&t46));
                    gm_tf5srh4mp4b3i t48 = gm_l3;
                    GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tj64ui6kruni6 *)gm_out) + 0, t47);
                    GM_ST(gm_tf5srh4mp4b3i, (uint8_t *)&(*(gm_tj64ui6kruni6 *)gm_out) + 16, t48);
                    goto gm_fdone;
                }
                goto me21;
mn22:;
                uint8_t t52 = GM_LD(uint8_t, ((((uint8_t *)t20) + 0) + 0));
                if (t52 != ((uint8_t)1ULL)) goto pa51;
                goto po50;
pa51:;
                uint8_t t54 = GM_LD(uint8_t, ((((uint8_t *)t20) + 0) + 0));
                if (t54 != ((uint8_t)2ULL)) goto pa53;
                goto po50;
pa53:;
                goto mn49;
po50:;
                {
                    void * t56 = gm_l2;
                    GM_ST(uint64_t, (((uint8_t *)t56) + 96), ((uint64_t)1ULL));
                }
                {
                    void * t57 = gm_l2;
                    uint32_t t58 = GM_LD(uint32_t, (((uint8_t *)t57) + 48));
                    gm_t88rcucg1b7uk t59;
                    GM_ST(uint32_t, ((uint8_t *)&t59 + 0), t58);
                    gm_tabb710d9mq2e t60;
                    gm_f_format_dd6d4bt((void *)&t60, (void *)NULL, cx, NULL, t59);
                    if (gm_unw.kind) {
                        goto gm_ll7;
                    }
                    if (((((uint8_t *)&t60))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t60) + 8, gm_srcmok80ho8hh01, 651, 25);
                    gm_b16_8 t61 = GM_LD(gm_b16_8, (((uint8_t *)&t60) + 8));
                    gm_b16_8 t62 = GM_LD(gm_b16_8, ((uint8_t *)&t61));
                    gm_tf5srh4mp4b3i t63 = gm_l3;
                    GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tj64ui6kruni6 *)gm_out) + 0, t62);
                    GM_ST(gm_tf5srh4mp4b3i, (uint8_t *)&(*(gm_tj64ui6kruni6 *)gm_out) + 16, t63);
                    goto gm_fdone;
                }
                goto me21;
mn49:;
                gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 643, 9);
me21:;
            }
lc4:;
        }
gm_ll7:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb4; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc4; }
        }
        goto gm_lad;
lb4:;
    }
    {
        gm_tf5srh4mp4b3i t64;
        memset(&t64, 0, sizeof t64);
        gm_l4 = t64;
    }
    {
        void * t66 = gm_l0;
        uint8_t t67 = GM_LD(uint8_t, (((uint8_t *)t66) + 298));
        if (!t67) goto ie65;
        {
            gm_b16_8 t69;
            GM_ST(void *, (uint8_t *)&t69, (void *)(gm_sjibpf3glbc54));
            GM_ST(uintptr_t, (uint8_t *)&t69 + 8, (uintptr_t)(8));
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tj64ui6kruni6 *)gm_out) + 0, t69);
            gm_tf5srh4mp4b3i t70 = gm_l4;
            GM_ST(gm_tf5srh4mp4b3i, (uint8_t *)&(*(gm_tj64ui6kruni6 *)gm_out) + 16, t70);
            goto gm_fdone;
        }
        goto ix65;
ie65:;
ix65:;
    }
    {
        void * t71 = gm_l0;
        GM_ST(uint8_t, (((uint8_t *)t71) + 297), ((uint8_t)1));
    }
    {
        gm_b16_8 t72;
        GM_ST(void *, (uint8_t *)&t72, (void *)(gm_s2hi6cc9n75l5));
        GM_ST(uintptr_t, (uint8_t *)&t72 + 8, (uintptr_t)(6));
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tj64ui6kruni6 *)gm_out) + 0, t72);
        gm_tf5srh4mp4b3i t73 = gm_l4;
        GM_ST(gm_tf5srh4mp4b3i, (uint8_t *)&(*(gm_tj64ui6kruni6 *)gm_out) + 16, t73);
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
extern __attribute__((aligned(4))) gm_b0_1 gm_f_line_l6u87p9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_local_addr_g9lkr6l(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_local_addr_g9lkr6l F;
    void * gm_l0; /* fg */
    uint32_t gm_l1; /* l */
    uint64_t gm_l2; /* i */
    GM_STACK_CHECK(&F, 295u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        uint32_t t4 = gm_l1;
        gm_tf5srh4mp4b3i t5 = gm_f_param_index_rp1nn6k(t3, t4);
        if (!((((uint8_t *)&t5))[8] != 0)) goto ie2;
        uint64_t t6 = GM_LD(uint64_t, ((uint8_t *)&t5));
        gm_l2 = t6;
        {
            uint64_t t9 = gm_l2;
            uint64_t t10 = (uintptr_t)t9;
            void * t11 = gm_l0;
            uint64_t t12 = GM_LD(uintptr_t, (((uint8_t *)t11) + 328) + 8);
            if (t10 >= (uintptr_t)(t12)) gm_trap_index(t10, t12, gm_srcmok80ho8hh01, 552, 12);
            uint8_t t13 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t11) + 328)) + t10 * 1));
            if (!t13) goto ie8;
            {
                uint64_t t15 = gm_l2;
                gm_t4ce0jd3tdro4 t16;
                GM_ST(uint64_t, ((uint8_t *)&t16 + 0), t15);
                gm_tabb710d9mq2e t17;
                gm_f_format_9pumrco((void *)&t17, (void *)NULL, cx, NULL, t16);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t17))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t17) + 8, gm_srcmok80ho8hh01, 552, 36);
                gm_b16_8 t18 = GM_LD(gm_b16_8, (((uint8_t *)&t17) + 8));
                gm_b16_8 t19 = GM_LD(gm_b16_8, ((uint8_t *)&t18));
                uint8_t t20 = 0;
                F.ret = t19;
                goto gm_fret;
            }
            goto ix8;
ie8:;
ix8:;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t21 = gm_l0;
        uint32_t t22 = gm_l1;
        gm_b16_8 t23 = gm_f_local_lv_m2jtf0n((void *)NULL, cx, NULL, t21, t22);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t24;
        GM_ST(gm_b16_8, ((uint8_t *)&t24 + 0), t23);
        gm_tabb710d9mq2e t25;
        gm_f_format_4un19t8((void *)&t25, (void *)NULL, cx, NULL, t24);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t25))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t25) + 8, gm_srcmok80ho8hh01, 554, 12);
        gm_b16_8 t26 = GM_LD(gm_b16_8, (((uint8_t *)&t25) + 8));
        gm_b16_8 t27 = GM_LD(gm_b16_8, ((uint8_t *)&t26));
        uint8_t t28 = 0;
        F.ret = t27;
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
__attribute__((aligned(4))) uint8_t gm_f_local_exit_4aogt00(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, gm_b16_8 a2, gm_b16_8 a3) {
    struct gm_fr_local_exit_4aogt00 F;
    void * gm_l0; /* fg */
    gm_t9qkh0rsooc8d gm_l1; /* kind */
    gm_b16_8 gm_l2; /* label */
    gm_b16_8 gm_l3; /* is_err */
    gm_tf5srh4mp4b3i gm_l4; /* target */
    gm_tf5srh4mp4b3i gm_l5; /* found */
    uint64_t gm_l6; /* i */
    uint8_t gm_l8; /* hit */
    gm_tiilmfd16v3fc gm_l10; /* w */
    uint64_t gm_l11; /* start */
    uint64_t gm_l12; /* t */
    uint8_t gm_l13; /* err */
    uint64_t gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    uint64_t gm_l16; /* j */
    gm_b16_8 gm_l18; /* <tmp> */
    uint64_t gm_l19; /* <tmp> */
    gm_tgtub0v94d5u5 gm_l20; /* d */
    gm_b16_8 gm_l21; /* dest */
    uint64_t gm_l22; /* t */
    gm_tiilmfd16v3fc gm_l25; /* w */
    GM_STACK_CHECK(&F, 1380u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        gm_tf5srh4mp4b3i t2;
        memset(&t2, 0, sizeof t2);
        gm_l4 = t2;
    }
    {
        uint8_t t4 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l1) + 0) == ((uint8_t)0ULL)));
        if (!t4) goto ie3;
        goto ix3;
ie3:;
        gm_t9qkh0rsooc8d t7 = gm_l1;
        gm_t9qkh0rsooc8d t8;
        GM_ST(uint8_t, ((uint8_t *)&t8 + 0), ((uint8_t)1ULL));
        gm_t9qkh0rsooc8d t9;
        GM_ST(uint8_t, ((uint8_t *)&t9 + 0), ((uint8_t)2ULL));
        gm_t0am46iohlpol t10;
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t10 + 0), t8);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t10 + 1), t9);
        uint8_t t11 = 0;
        if (gm_eqgnrg8h3j6k(((uint8_t *)&t10) + 0, ((uint8_t *)&t7))) t11 = 1;
        if (gm_eqgnrg8h3j6k(((uint8_t *)&t10) + 1, ((uint8_t *)&t7))) t11 = 1;
        uint8_t t12 = (uint8_t)(t11);
        if (!t12) goto ie6;
        {
            gm_tf5srh4mp4b3i t14;
            memset(&t14, 0, sizeof t14);
            gm_l5 = t14;
        }
        {
            void * t15 = gm_l0;
            uint64_t t16 = GM_LD(uintptr_t, ((((uint8_t *)t15) + 240) + 0) + 8);
            gm_l6 = t16;
        }
        {
            for (;;) {
                uint64_t t18 = gm_l6;
                uint8_t t19 = (t18 > ((uint64_t)0ULL));
                if (!t19) goto lb17;
                {
                    uint64_t t22 = gm_l6;
                    uint64_t r23; bool o24 = __builtin_sub_overflow(t22, ((uint64_t)1ULL), &r23);
                    if (o24) gm_trap("integer overflow in `-`", gm_srcmok80ho8hh01, 772, 13);
                    uint64_t t25 = r23;
                    gm_l6 = t25;
                }
                {
                    uint64_t t26 = gm_l6;
                    uint64_t t27 = (uintptr_t)t26;
                    void * t28 = gm_l0;
                    uint64_t t29 = GM_LD(uintptr_t, ((((uint8_t *)t28) + 240) + 0) + 8);
                    if (t27 >= (uintptr_t)(t29)) gm_trap_index(t27, t29, gm_srcmok80ho8hh01, 773, 18);
                    gm_tehgl6lkqgvt0 t30 = GM_LD(gm_tehgl6lkqgvt0, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t28) + 240) + 0)) + t27 * 104));
                    F.l7 = t30;
                }
                {
                    gm_l8 = ((uint8_t)0);
                }
                {
                    uint8_t t36 = GM_LD(uint8_t, ((((uint8_t *)&F.l7) + 0) + 0));
                    switch (t36) { case ((uint8_t)0ULL): goto ma34; case ((uint8_t)1ULL): goto ma32; case ((uint8_t)2ULL): goto ma33; default: goto ma35; }
ma32:;
                    uint8_t t38 = GM_LD(uint8_t, ((((uint8_t *)&F.l7) + 0) + 0));
                    if (t38 != ((uint8_t)1ULL)) goto mn37;
                    gm_thognv9odov03 t39 = GM_LD(gm_thognv9odov03, ((((uint8_t *)&F.l7) + 0) + 8));
                    F.l9 = t39;
                    {
                        uint32_t t41 = GM_LD(uint32_t, (((uint8_t *)&F.l9) + 0));
                        gm_t88rcucg1b7uk t42;
                        GM_ST(uint32_t, ((uint8_t *)&t42 + 0), t41);
                        gm_tabb710d9mq2e t43;
                        gm_f_format_32n178h((void *)&t43, (void *)NULL, cx, NULL, t42);
                        if (gm_unw.kind) {
                            goto gm_ll20;
                        }
                        if (((((uint8_t *)&t43))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t43) + 8, gm_srcmok80ho8hh01, 776, 37);
                        gm_b16_8 t44 = GM_LD(gm_b16_8, (((uint8_t *)&t43) + 8));
                        gm_b16_8 t45 = GM_LD(gm_b16_8, ((uint8_t *)&t44));
                        gm_b16_8 t46 = gm_l2;
                        uint8_t t47 = (gm_eq0iqiubfm7e(((uint8_t *)&t45), ((uint8_t *)&t46)));
                        gm_l8 = t47;
                    }
                    goto me31;
mn37:;
ma33:;
                    uint8_t t49 = GM_LD(uint8_t, ((((uint8_t *)&F.l7) + 0) + 0));
                    if (t49 != ((uint8_t)2ULL)) goto mn48;
                    gm_tiilmfd16v3fc t50 = GM_LD(gm_tiilmfd16v3fc, ((((uint8_t *)&F.l7) + 0) + 8));
                    gm_l10 = t50;
                    {
                        uint8_t t52 = 0;
                        uint8_t t53 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l1) + 0) == ((uint8_t)1ULL)));
                        if (t53) {
                            uint32_t t54 = GM_LD(uint32_t, (((uint8_t *)&gm_l10) + 0));
                            gm_t88rcucg1b7uk t55;
                            GM_ST(uint32_t, ((uint8_t *)&t55 + 0), t54);
                            gm_tabb710d9mq2e t56;
                            gm_f_format_32n178h((void *)&t56, (void *)NULL, cx, NULL, t55);
                            if (gm_unw.kind) {
                                goto gm_ll20;
                            }
                            if (((((uint8_t *)&t56))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t56) + 8, gm_srcmok80ho8hh01, 777, 56);
                            gm_b16_8 t57 = GM_LD(gm_b16_8, (((uint8_t *)&t56) + 8));
                            gm_b16_8 t58 = GM_LD(gm_b16_8, ((uint8_t *)&t57));
                            gm_b16_8 t59 = gm_l2;
                            uint8_t t60 = (gm_eq0iqiubfm7e(((uint8_t *)&t58), ((uint8_t *)&t59)));
                            t52 = t60;
                        } else t52 = 0;
                        gm_l8 = t52;
                    }
                    goto me31;
mn48:;
ma34:;
                    uint8_t t62 = GM_LD(uint8_t, ((((uint8_t *)&F.l7) + 0) + 0));
                    if (t62 != ((uint8_t)0ULL)) goto mn61;
                    goto me31;
mn61:;
ma35:;
                    gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 775, 13);
me31:;
                }
                {
                    uint8_t t65 = gm_l8;
                    if (!t65) goto ie64;
                    {
                        uint64_t t67 = gm_l6;
                        gm_tf5srh4mp4b3i t68;
                        GM_ST(uint8_t, (uint8_t *)&t68 + 9, 0);
                        GM_ST(uint16_t, (uint8_t *)&t68 + 10, 0);
                        GM_ST(uint32_t, (uint8_t *)&t68 + 12, 0);
                        GM_ST(uint64_t, ((uint8_t *)&t68), t67);
                        ((uint8_t *)&t68)[8] = 1;
                        gm_l5 = t68;
                    }
                    {
                        goto lb17;
                    }
                    goto ix64;
ie64:;
ix64:;
                }
lc17:;
            }
gm_ll20:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb17; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc17; }
            }
            goto gm_lad;
lb17:;
        }
        {
            uint8_t t70;
            if (((((uint8_t *)&gm_l5))[8] != 0)) goto mn72;
            t70 = ((uint8_t)1);
            goto me71;
mn72:;
            t70 = ((uint8_t)0);
            goto me71;
mn73:;
            gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 785, 12);
me71:;
            if (!t70) goto ie69;
            {
                uint8_t t75 = 0;
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            goto ix69;
ie69:;
ix69:;
        }
        {
            gm_tf5srh4mp4b3i t76 = gm_l5;
            gm_l4 = t76;
        }
        goto ix6;
ie6:;
        {
            uint8_t t78 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
ix6:;
ix3:;
    }
    {
        gm_l11 = ((uint64_t)0ULL);
    }
    {
        if (!((((uint8_t *)&gm_l4))[8] != 0)) goto ie79;
        uint64_t t80 = GM_LD(uint64_t, ((uint8_t *)&gm_l4));
        gm_l12 = t80;
        {
            uint64_t t82 = gm_l12;
            uint64_t r83; bool o84 = __builtin_add_overflow(t82, ((uint64_t)1ULL), &r83);
            if (o84) gm_trap("integer overflow in `+`", gm_srcmok80ho8hh01, 791, 30);
            uint64_t t85 = r83;
            gm_l11 = t85;
        }
        goto ix79;
ie79:;
ix79:;
    }
    {
        uint8_t t86 = 0;
        uint8_t t87 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l1) + 0) == ((uint8_t)0ULL)));
        if (t87) {
            uint8_t t88 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 1) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_sicgmpca63jln, 1) == 0));
            uint8_t t89 = (uint8_t)!t88;
            t86 = t89;
        } else t86 = 0;
        gm_l13 = t86;
    }
    {
        {
            uint64_t t91 = gm_l11;
            gm_l15 = t91;
        }
        {
            void * t92 = gm_l0;
            uint64_t t93 = GM_LD(uintptr_t, ((((uint8_t *)t92) + 240) + 0) + 8);
            gm_l14 = t93;
        }
        {
            for (;;) {
                uint64_t t95 = gm_l15;
                uint64_t t96 = gm_l14;
                uint8_t t97 = (t95 < t96);
                if (!t97) goto lb94;
                {
                    uint64_t t100 = gm_l15;
                    uint64_t t103 = GM_LD(uint64_t, ((uint8_t *)&t100));
                    gm_l16 = t103;
                    {
                        uint64_t t105 = gm_l16;
                        uint64_t t106 = (uintptr_t)t105;
                        void * t107 = gm_l0;
                        uint64_t t108 = GM_LD(uintptr_t, ((((uint8_t *)t107) + 240) + 0) + 8);
                        if (t106 >= (uintptr_t)(t108)) gm_trap_index(t106, t108, gm_srcmok80ho8hh01, 797, 14);
                        gm_tehgl6lkqgvt0 t109 = GM_LD(gm_tehgl6lkqgvt0, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t107) + 240) + 0)) + t106 * 104));
                        F.l17 = t109;
                    }
                    {
                        uint8_t t111 = ((GM_LD(uint8_t, (((uint8_t *)&F.l17) + 0) + 0) == ((uint8_t)0ULL)));
                        if (!t111) goto ie110;
                        {
                            {
                                gm_b16_8 t114 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l17) + 56) + 0));
                                gm_l18 = t114;
                            }
                            {
                                gm_l19 = ((uint64_t)0ULL);
                            }
                            {
                                for (;;) {
                                    uint64_t t116 = gm_l19;
                                    uint64_t t117 = GM_LD(uintptr_t, ((uint8_t *)&gm_l18) + 8);
                                    uint8_t t118 = (t116 < t117);
                                    if (!t118) goto lb115;
                                    {
                                        uint64_t t121 = gm_l19;
                                        uint64_t t122 = (uintptr_t)t121;
                                        uint64_t t123 = GM_LD(uintptr_t, ((uint8_t *)&gm_l18) + 8);
                                        gm_tgtub0v94d5u5 t126 = GM_LD(gm_tgtub0v94d5u5, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l18)) + t122 * 16));
                                        gm_l20 = t126;
                                        {
                                            uint8_t t129 = 0;
                                            uint8_t t130 = GM_LD(uint8_t, (((uint8_t *)&gm_l20) + 0));
                                            uint8_t t131 = (uint8_t)!t130;
                                            if (!t131) {
                                                uint8_t t132 = gm_l13;
                                                t129 = t132;
                                            } else t129 = 1;
                                            if (!t129) goto ie128;
                                            {
                                                uint8_t t134 = 0;
                                                F.ret = ((uint8_t)0);
                                                goto gm_fret;
                                            }
                                            goto ix128;
ie128:;
ix128:;
                                        }
                                        goto me124;
mn125:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 799, 13);
me124:;
                                    }
lc115:;
                                    {
                                        uint64_t t136 = gm_l19;
                                        uint64_t r137; bool o138 = __builtin_add_overflow(t136, ((uint64_t)1ULL), &r137);
                                        uint64_t t139 = r137;
                                        gm_l19 = t139;
                                    }
                                }
lb115:;
                            }
                        }
                        goto ix110;
ie110:;
ix110:;
                    }
                    goto me101;
mn102:;
                    gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 796, 5);
me101:;
                }
lc94:;
                {
                    uint64_t t141 = gm_l15;
                    uint64_t r142; bool o143 = __builtin_add_overflow(t141, ((uint64_t)1ULL), &r142);
                    uint64_t t144 = r142;
                    gm_l15 = t144;
                }
            }
lb94:;
        }
    }
    {
        gm_b16_8 t145;
        GM_ST(void *, (uint8_t *)&t145, (void *)(gm_s0m07bui94rjq));
        GM_ST(uintptr_t, (uint8_t *)&t145 + 8, (uintptr_t)(7));
        gm_l21 = t145;
    }
    {
        if (!((((uint8_t *)&gm_l4))[8] != 0)) goto ie146;
        uint64_t t147 = GM_LD(uint64_t, ((uint8_t *)&gm_l4));
        gm_l22 = t147;
        {
            uint64_t t149 = gm_l22;
            uint64_t t150 = (uintptr_t)t149;
            void * t151 = gm_l0;
            uint64_t t152 = GM_LD(uintptr_t, ((((uint8_t *)t151) + 240) + 0) + 8);
            if (t150 >= (uintptr_t)(t152)) gm_trap_index(t150, t152, gm_srcmok80ho8hh01, 806, 14);
            gm_tehgl6lkqgvt0 t153 = GM_LD(gm_tehgl6lkqgvt0, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t151) + 240) + 0)) + t150 * 104));
            F.l23 = t153;
        }
        {
            uint8_t t159 = GM_LD(uint8_t, ((((uint8_t *)&F.l23) + 0) + 0));
            switch (t159) { case ((uint8_t)0ULL): goto ma157; case ((uint8_t)1ULL): goto ma155; case ((uint8_t)2ULL): goto ma156; default: goto ma158; }
ma155:;
            uint8_t t161 = GM_LD(uint8_t, ((((uint8_t *)&F.l23) + 0) + 0));
            if (t161 != ((uint8_t)1ULL)) goto mn160;
            gm_thognv9odov03 t162 = GM_LD(gm_thognv9odov03, ((((uint8_t *)&F.l23) + 0) + 8));
            F.l24 = t162;
            uint8_t t164 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l1) + 0) == ((uint8_t)1ULL)));
            if (!t164) goto ie163;
            {
                gm_b16_8 t166 = GM_LD(gm_b16_8, (((uint8_t *)&F.l24) + 8));
                gm_l21 = t166;
            }
            goto ix163;
ie163:;
            {
                gm_b16_8 t168 = GM_LD(gm_b16_8, (((uint8_t *)&F.l24) + 24));
                gm_l21 = t168;
            }
ix163:;
            goto me154;
mn160:;
ma156:;
            uint8_t t170 = GM_LD(uint8_t, ((((uint8_t *)&F.l23) + 0) + 0));
            if (t170 != ((uint8_t)2ULL)) goto mn169;
            gm_tiilmfd16v3fc t171 = GM_LD(gm_tiilmfd16v3fc, ((((uint8_t *)&F.l23) + 0) + 8));
            gm_l25 = t171;
            {
                gm_b16_8 t173 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l25) + 8));
                gm_l21 = t173;
            }
            goto me154;
mn169:;
ma157:;
            uint8_t t175 = GM_LD(uint8_t, ((((uint8_t *)&F.l23) + 0) + 0));
            if (t175 != ((uint8_t)0ULL)) goto mn174;
            gm_trap("reached `@unreachable()`", gm_srcmok80ho8hh01, 810, 23);
            goto me154;
mn174:;
ma158:;
            gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 807, 9);
me154:;
        }
        goto ix146;
ie146:;
ix146:;
    }
    {
        void * t176 = gm_l0;
        gm_b16_8 t177 = gm_l21;
        gm_tf5srh4mp4b3i t178 = gm_l4;
        gm_b0_1 t179 = gm_f_jump_pvnkvme((void *)NULL, cx, NULL, t176, t177, t178);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint8_t t180 = 0;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_local_lv_m2jtf0n(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_local_lv_m2jtf0n F;
    void * gm_l0; /* fg */
    uint32_t gm_l1; /* l */
    void * gm_l2; /* g */
    uint64_t gm_l3; /* i */
    GM_STACK_CHECK(&F, 465u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        gm_l2 = t3;
    }
    {
        uint32_t t4 = gm_l1;
        uint64_t t5 = (uint64_t)t4;
        gm_l3 = t5;
    }
    {
        uint64_t t7 = gm_l3;
        uint64_t t8 = (uintptr_t)t7;
        void * t9 = gm_l0;
        uint64_t t10 = GM_LD(uintptr_t, (((uint8_t *)t9) + 344) + 8);
        if (t8 >= (uintptr_t)(t10)) gm_trap_index(t8, t10, gm_srcmok80ho8hh01, 567, 8);
        uint8_t t11 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t9) + 344)) + t8 * 1));
        if (!t11) goto ie6;
        {
            for (;;) {
                void * t14 = gm_l2;
                uint64_t t15 = GM_LD(uintptr_t, ((((uint8_t *)t14) + 3160) + 0) + 8);
                uint64_t t16 = gm_l3;
                uint8_t t17 = (t15 <= t16);
                if (!t17) goto lb13;
                {
                    void * t20 = gm_l2;
                    void * t21 = (void *)(((uint8_t *)t20) + 3160);
                    void * t22 = gm_l2;
                    uint64_t t23 = GM_LD(uintptr_t, ((((uint8_t *)t22) + 3160) + 0) + 8);
                    gm_t4ce0jd3tdro4 t24;
                    GM_ST(uint64_t, ((uint8_t *)&t24 + 0), t23);
                    gm_tabb710d9mq2e t25;
                    gm_f_format_gt1sds7((void *)&t25, (void *)NULL, cx, NULL, t24);
                    if (gm_unw.kind) {
                        goto gm_ll18;
                    }
                    if (((((uint8_t *)&t25))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t25) + 8, gm_srcmok80ho8hh01, 568, 65);
                    gm_b16_8 t26 = GM_LD(gm_b16_8, (((uint8_t *)&t25) + 8));
                    gm_b16_8 t27 = GM_LD(gm_b16_8, ((uint8_t *)&t26));
                    gm_tftb1odc739t5 t28 = gm_f_push_3ef746b(t21, t27);
                    if (((((uint8_t *)&t28))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t28) + 1, gm_srcmok80ho8hh01, 568, 46);
                    gm_b0_1 t29 = GM_LD(gm_b0_1, (((uint8_t *)&t28) + 1));
                }
lc13:;
            }
gm_ll18:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb13; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc13; }
            }
            goto gm_lad;
lb13:;
        }
        {
            uint64_t t30 = gm_l3;
            uint64_t t31 = (uintptr_t)t30;
            void * t32 = gm_l2;
            uint64_t t33 = GM_LD(uintptr_t, ((((uint8_t *)t32) + 3160) + 0) + 8);
            if (t31 >= (uintptr_t)(t33)) gm_trap_index(t31, t33, gm_srcmok80ho8hh01, 569, 16);
            gm_b16_8 t34 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t32) + 3160) + 0)) + t31 * 16));
            uint8_t t35 = 0;
            F.ret = t34;
            goto gm_fret;
        }
        goto ix6;
ie6:;
ix6:;
    }
    {
        for (;;) {
            void * t37 = gm_l2;
            uint64_t t38 = GM_LD(uintptr_t, ((((uint8_t *)t37) + 3200) + 0) + 8);
            uint64_t t39 = gm_l3;
            uint8_t t40 = (t38 <= t39);
            if (!t40) goto lb36;
            {
                void * t43 = gm_l2;
                void * t44 = (void *)(((uint8_t *)t43) + 3200);
                void * t45 = gm_l2;
                uint64_t t46 = GM_LD(uintptr_t, ((((uint8_t *)t45) + 3200) + 0) + 8);
                gm_t4ce0jd3tdro4 t47;
                GM_ST(uint64_t, ((uint8_t *)&t47 + 0), t46);
                gm_tabb710d9mq2e t48;
                gm_f_format_ghqt9ar((void *)&t48, (void *)NULL, cx, NULL, t47);
                if (gm_unw.kind) {
                    goto gm_ll41;
                }
                if (((((uint8_t *)&t48))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t48) + 8, gm_srcmok80ho8hh01, 571, 61);
                gm_b16_8 t49 = GM_LD(gm_b16_8, (((uint8_t *)&t48) + 8));
                gm_b16_8 t50 = GM_LD(gm_b16_8, ((uint8_t *)&t49));
                gm_tftb1odc739t5 t51 = gm_f_push_3ef746b(t44, t50);
                if (((((uint8_t *)&t51))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t51) + 1, gm_srcmok80ho8hh01, 571, 42);
                gm_b0_1 t52 = GM_LD(gm_b0_1, (((uint8_t *)&t51) + 1));
            }
lc36:;
        }
gm_ll41:;
        if (gm_unw.frame == &F.h && gm_unw.label == 1) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb36; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc36; }
        }
        goto gm_lad;
lb36:;
    }
    {
        uint64_t t53 = gm_l3;
        uint64_t t54 = (uintptr_t)t53;
        void * t55 = gm_l2;
        uint64_t t56 = GM_LD(uintptr_t, ((((uint8_t *)t55) + 3200) + 0) + 8);
        if (t54 >= (uintptr_t)(t56)) gm_trap_index(t54, t56, gm_srcmok80ho8hh01, 572, 12);
        gm_b16_8 t57 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t55) + 3200) + 0)) + t54 * 16));
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
inline __attribute__((aligned(4))) void gm_f_loop_scope_ee94ihm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2, gm_b16_8 a3) {
    struct gm_fr_loop_scope_ee94ihm F;
    void * gm_l0; /* fg */
    uint32_t gm_l1; /* label */
    gm_b16_8 gm_l2; /* brk */
    gm_b16_8 gm_l3; /* cont_ */
    GM_STACK_CHECK(&F, 220u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    memset((uint8_t *)&(*(gm_tehgl6lkqgvt0 *)gm_out) + 52, 0, 4);
    gm_thognv9odov03 t1;
    memset((uint8_t *)&t1 + 4, 0, 4);
    uint32_t t2 = gm_l1;
    GM_ST(uint32_t, (uint8_t *)&t1 + 0, t2);
    gm_b16_8 t3 = gm_l2;
    GM_ST(gm_b16_8, (uint8_t *)&t1 + 8, t3);
    gm_b16_8 t4 = gm_l3;
    GM_ST(gm_b16_8, (uint8_t *)&t1 + 24, t4);
    gm_tbf8u1b86cgmi t5;
    GM_ST(uint8_t, (uint8_t *)&t5 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t5 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t5 + 4, 0);
    GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)1ULL));
    GM_ST(gm_thognv9odov03, ((uint8_t *)&t5 + 8), t1);
    void * t6 = gm_l0;
    uint32_t t7 = gm_f_fresh_5kokjmg(t6);
    gm_trqfvmjtt4sij t8;
    gm_f_list_rl2scuf((void *)&t8, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_tbf8u1b86cgmi, (uint8_t *)&(*(gm_tehgl6lkqgvt0 *)gm_out) + 0, t5);
    GM_ST(uint32_t, (uint8_t *)&(*(gm_tehgl6lkqgvt0 *)gm_out) + 48, t7);
    GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_tehgl6lkqgvt0 *)gm_out) + 56, t8);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tehgl6lkqgvt0 *)gm_out) + 96, ((uint64_t)0ULL));
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
__attribute__((aligned(4))) uint8_t gm_f_may_write_702phjq(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_may_write_702phjq F;
    void * gm_l0; /* e */
    uint8_t gm_l1; /* writes */
    gm_tvdg94bk7aa22 gm_l2; /* op */
    GM_STACK_CHECK(&F, 104u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_l1 = ((uint8_t)0);
    }
    {
        void * t2 = gm_l0;
        uint8_t t8 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        switch (t8) { case ((uint8_t)0ULL): goto ma6; case ((uint8_t)1ULL): goto ma6; case ((uint8_t)2ULL): goto ma6; case ((uint8_t)3ULL): goto ma6; case ((uint8_t)4ULL): goto ma6; case ((uint8_t)5ULL): goto ma6; case ((uint8_t)6ULL): goto ma6; case ((uint8_t)7ULL): goto ma6; case ((uint8_t)8ULL): goto ma6; case ((uint8_t)9ULL): goto ma6; case ((uint8_t)10ULL): goto ma6; case ((uint8_t)11ULL): goto ma6; case ((uint8_t)12ULL): goto ma6; case ((uint8_t)13ULL): goto ma6; case ((uint8_t)14ULL): goto ma6; case ((uint8_t)15ULL): goto ma6; case ((uint8_t)16ULL): goto ma6; case ((uint8_t)17ULL): goto ma6; case ((uint8_t)18ULL): goto ma6; case ((uint8_t)19ULL): goto ma6; case ((uint8_t)20ULL): goto ma6; case ((uint8_t)21ULL): goto ma6; case ((uint8_t)22ULL): goto ma6; case ((uint8_t)23ULL): goto ma6; case ((uint8_t)24ULL): goto ma6; case ((uint8_t)25ULL): goto ma6; case ((uint8_t)26ULL): goto ma6; case ((uint8_t)27ULL): goto ma4; case ((uint8_t)28ULL): goto ma6; case ((uint8_t)29ULL): goto ma6; case ((uint8_t)30ULL): goto ma6; case ((uint8_t)31ULL): goto ma6; case ((uint8_t)32ULL): goto ma6; case ((uint8_t)33ULL): goto ma5; case ((uint8_t)34ULL): goto ma4; case ((uint8_t)35ULL): goto ma6; case ((uint8_t)36ULL): goto ma6; case ((uint8_t)37ULL): goto ma6; case ((uint8_t)38ULL): goto ma6; case ((uint8_t)39ULL): goto ma6; case ((uint8_t)40ULL): goto ma6; case ((uint8_t)41ULL): goto ma6; case ((uint8_t)42ULL): goto ma6; case ((uint8_t)43ULL): goto ma6; case ((uint8_t)44ULL): goto ma6; case ((uint8_t)45ULL): goto ma6; case ((uint8_t)46ULL): goto ma4; case ((uint8_t)47ULL): goto ma6; case ((uint8_t)48ULL): goto ma4; case ((uint8_t)49ULL): goto ma6; case ((uint8_t)50ULL): goto ma6; case ((uint8_t)51ULL): goto ma6; case ((uint8_t)52ULL): goto ma6; case ((uint8_t)53ULL): goto ma4; case ((uint8_t)54ULL): goto ma4; case ((uint8_t)55ULL): goto ma4; case ((uint8_t)56ULL): goto ma4; case ((uint8_t)57ULL): goto ma4; case ((uint8_t)58ULL): goto ma4; default: goto ma6; }
ma4:;
        uint8_t t12 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t12 != ((uint8_t)34ULL)) goto pa11;
        goto po10;
pa11:;
        uint8_t t14 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t14 != ((uint8_t)46ULL)) goto pa13;
        goto po10;
pa13:;
        uint8_t t16 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t16 != ((uint8_t)48ULL)) goto pa15;
        goto po10;
pa15:;
        uint8_t t18 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t18 != ((uint8_t)58ULL)) goto pa17;
        goto po10;
pa17:;
        uint8_t t20 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t20 != ((uint8_t)27ULL)) goto pa19;
        goto po10;
pa19:;
        uint8_t t22 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t22 != ((uint8_t)53ULL)) goto pa21;
        goto po10;
pa21:;
        uint8_t t24 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t24 != ((uint8_t)54ULL)) goto pa23;
        goto po10;
pa23:;
        uint8_t t26 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t26 != ((uint8_t)55ULL)) goto pa25;
        goto po10;
pa25:;
        uint8_t t28 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t28 != ((uint8_t)56ULL)) goto pa27;
        goto po10;
pa27:;
        uint8_t t30 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t30 != ((uint8_t)57ULL)) goto pa29;
        goto po10;
pa29:;
        goto mn9;
po10:;
        {
            gm_l1 = ((uint8_t)1);
        }
        goto me3;
mn9:;
ma5:;
        uint8_t t33 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t33 != ((uint8_t)33ULL)) goto mn32;
        gm_tvdg94bk7aa22 t34 = GM_LD(gm_tvdg94bk7aa22, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l2 = t34;
        uint8_t t39 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t39 != ((uint8_t)65ULL)) goto pa38;
        goto po37;
pa38:;
        uint8_t t41 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t41 != ((uint8_t)66ULL)) goto pa40;
        goto po37;
pa40:;
        uint8_t t43 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t43 != ((uint8_t)67ULL)) goto pa42;
        goto po37;
pa42:;
        uint8_t t45 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t45 != ((uint8_t)68ULL)) goto pa44;
        goto po37;
pa44:;
        uint8_t t47 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t47 != ((uint8_t)71ULL)) goto pa46;
        goto po37;
pa46:;
        uint8_t t49 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t49 != ((uint8_t)72ULL)) goto pa48;
        goto po37;
pa48:;
        uint8_t t51 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t51 != ((uint8_t)73ULL)) goto pa50;
        goto po37;
pa50:;
        uint8_t t53 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t53 != ((uint8_t)74ULL)) goto pa52;
        goto po37;
pa52:;
        uint8_t t55 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t55 != ((uint8_t)75ULL)) goto pa54;
        goto po37;
pa54:;
        uint8_t t57 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t57 != ((uint8_t)76ULL)) goto pa56;
        goto po37;
pa56:;
        uint8_t t59 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t59 != ((uint8_t)77ULL)) goto pa58;
        goto po37;
pa58:;
        uint8_t t61 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t61 != ((uint8_t)78ULL)) goto pa60;
        goto po37;
pa60:;
        uint8_t t63 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t63 != ((uint8_t)79ULL)) goto pa62;
        goto po37;
pa62:;
        goto mn36;
po37:;
        {
            gm_l1 = ((uint8_t)1);
        }
        goto me35;
mn36:;
        goto me35;
mn65:;
        gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 1325, 28);
me35:;
        goto me3;
mn32:;
ma6:;
        goto me3;
mn67:;
ma7:;
        gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 1323, 5);
me3:;
    }
    {
        uint8_t t70 = gm_l1;
        if (!t70) goto ie69;
        {
            uint8_t t72 = 0;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
        goto ix69;
ie69:;
ix69:;
    }
    {
        void * t73 = gm_l0;
        gm_closure t74 = ((gm_closure){GM_PTR_TAG(gm_f_fn_480otki), NULL, cx});
        uint8_t t75 = gm_f_any_child_ffc8h1k(t73, t74);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t76 = 0;
        F.ret = t75;
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
inline __attribute__((aligned(4))) uint8_t gm_f_nests_in_place_jc41ehn(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tnrltbc61i6v1 a2) {
    struct gm_fr_nests_in_place_jc41ehn F;
    void * gm_l0; /* fg */
    void * gm_l1; /* x */
    gm_tnrltbc61i6v1 gm_l2; /* obs */
    GM_STACK_CHECK(&F, 65u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t3 = gm_l0;
        void * t4 = gm_l1;
        uint8_t t5 = gm_f_builds_in_place_ae1b90u((void *)NULL, cx, NULL, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t6 = (uint8_t)!t5;
        if (!t6) goto ie2;
        {
            uint8_t t8 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t9 = gm_l1;
        uint8_t t15 = GM_LD(uint8_t, ((((uint8_t *)t9) + 8) + 0));
        switch (t15) { case ((uint8_t)0ULL): goto ma13; case ((uint8_t)1ULL): goto ma13; case ((uint8_t)2ULL): goto ma13; case ((uint8_t)3ULL): goto ma13; case ((uint8_t)4ULL): goto ma13; case ((uint8_t)5ULL): goto ma13; case ((uint8_t)6ULL): goto ma13; case ((uint8_t)7ULL): goto ma13; case ((uint8_t)8ULL): goto ma13; case ((uint8_t)9ULL): goto ma13; case ((uint8_t)10ULL): goto ma13; case ((uint8_t)11ULL): goto ma13; case ((uint8_t)12ULL): goto ma13; case ((uint8_t)13ULL): goto ma13; case ((uint8_t)14ULL): goto ma13; case ((uint8_t)15ULL): goto ma13; case ((uint8_t)16ULL): goto ma13; case ((uint8_t)17ULL): goto ma13; case ((uint8_t)18ULL): goto ma13; case ((uint8_t)19ULL): goto ma13; case ((uint8_t)20ULL): goto ma13; case ((uint8_t)21ULL): goto ma13; case ((uint8_t)22ULL): goto ma13; case ((uint8_t)23ULL): goto ma13; case ((uint8_t)24ULL): goto ma13; case ((uint8_t)25ULL): goto ma13; case ((uint8_t)26ULL): goto ma13; case ((uint8_t)27ULL): goto ma13; case ((uint8_t)28ULL): goto ma13; case ((uint8_t)29ULL): goto ma13; case ((uint8_t)30ULL): goto ma13; case ((uint8_t)31ULL): goto ma13; case ((uint8_t)32ULL): goto ma13; case ((uint8_t)33ULL): goto ma13; case ((uint8_t)34ULL): goto ma12; case ((uint8_t)35ULL): goto ma11; case ((uint8_t)36ULL): goto ma11; case ((uint8_t)37ULL): goto ma13; case ((uint8_t)38ULL): goto ma13; case ((uint8_t)39ULL): goto ma13; case ((uint8_t)40ULL): goto ma13; case ((uint8_t)41ULL): goto ma13; case ((uint8_t)42ULL): goto ma13; case ((uint8_t)43ULL): goto ma13; case ((uint8_t)44ULL): goto ma13; case ((uint8_t)45ULL): goto ma13; case ((uint8_t)46ULL): goto ma13; case ((uint8_t)47ULL): goto ma13; case ((uint8_t)48ULL): goto ma13; case ((uint8_t)49ULL): goto ma13; case ((uint8_t)50ULL): goto ma13; case ((uint8_t)51ULL): goto ma13; case ((uint8_t)52ULL): goto ma13; case ((uint8_t)53ULL): goto ma13; case ((uint8_t)54ULL): goto ma13; case ((uint8_t)55ULL): goto ma13; case ((uint8_t)56ULL): goto ma13; case ((uint8_t)57ULL): goto ma13; case ((uint8_t)58ULL): goto ma13; default: goto ma13; }
ma11:;
        uint8_t t19 = GM_LD(uint8_t, ((((uint8_t *)t9) + 8) + 0));
        if (t19 != ((uint8_t)35ULL)) goto pa18;
        goto po17;
pa18:;
        uint8_t t21 = GM_LD(uint8_t, ((((uint8_t *)t9) + 8) + 0));
        if (t21 != ((uint8_t)36ULL)) goto pa20;
        goto po17;
pa20:;
        goto mn16;
po17:;
        uint8_t t22 = 0;
        F.ret = ((uint8_t)1);
        goto gm_fret;
        goto me10;
mn16:;
ma12:;
        uint8_t t24 = GM_LD(uint8_t, ((((uint8_t *)t9) + 8) + 0));
        if (t24 != ((uint8_t)34ULL)) goto mn23;
        uint8_t t25 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l2) + 0) == ((uint8_t)0ULL)));
        uint8_t t26 = 0;
        F.ret = t25;
        goto gm_fret;
        goto me10;
mn23:;
ma13:;
        uint8_t t28 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
        goto me10;
mn27:;
ma14:;
        gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 1024, 5);
me10:;
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
inline __attribute__((aligned(4))) uint8_t gm_f_no_defers_hci755r(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_no_defers_hci755r F;
    void * gm_l0; /* fg */
    gm_b16_8 gm_l1; /* <tmp> */
    uint64_t gm_l2; /* <tmp> */
    GM_STACK_CHECK_SHALLOW(&F, 231u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        {
            void * t3 = gm_l0;
            gm_b16_8 t4 = GM_LD(gm_b16_8, ((((uint8_t *)t3) + 240) + 0));
            gm_l1 = t4;
        }
        {
            gm_l2 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t6 = gm_l2;
                uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                uint8_t t8 = (t6 < t7);
                if (!t8) goto lb5;
                {
                    uint64_t t11 = gm_l2;
                    uint64_t t12 = (uintptr_t)t11;
                    uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                    gm_tehgl6lkqgvt0 t16 = GM_LD(gm_tehgl6lkqgvt0, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t12 * 104));
                    F.l3 = t16;
                    {
                        uint8_t t19 = ((GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0) + 0) == ((uint8_t)0ULL)));
                        if (!t19) goto ie18;
                        uint64_t t20 = GM_LD(uintptr_t, ((((uint8_t *)&F.l3) + 56) + 0) + 8);
                        uint8_t t21 = (t20 > ((uint64_t)0ULL));
                        if (!t21) goto ie18;
                        {
                            uint8_t t23 = 0;
                            F.ret = ((uint8_t)0);
                            goto gm_fret;
                        }
                        goto ix18;
ie18:;
ix18:;
                    }
                    goto me14;
mn15:;
                    gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 841, 5);
me14:;
                }
lc5:;
                {
                    uint64_t t25 = gm_l2;
                    uint64_t r26; bool o27 = __builtin_add_overflow(t25, ((uint64_t)1ULL), &r26);
                    uint64_t t28 = r26;
                    gm_l2 = t28;
                }
            }
lb5:;
        }
    }
    {
        uint8_t t29 = 0;
        F.ret = ((uint8_t)1);
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_param_index_rp1nn6k(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_param_index_rp1nn6k F;
    void * gm_l0; /* fg */
    uint32_t gm_l1; /* l */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* i */
    uint32_t gm_l5; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 260u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        {
            void * t3 = gm_l0;
            void * t4 = GM_LD(void *, (((uint8_t *)t3) + 176));
            gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)t4) + 32));
            gm_l2 = t5;
        }
        {
            gm_l3 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t7 = gm_l3;
                uint64_t t8 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                uint8_t t9 = (t7 < t8);
                if (!t9) goto lb6;
                {
                    uint64_t t12 = gm_l3;
                    uint64_t t13 = gm_l3;
                    uint64_t t14 = (uintptr_t)t13;
                    uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    uint32_t t16 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t14 * 4));
                    gm_tn68el8d14vku t17;
                    GM_ST(uint32_t, (uint8_t *)&t17 + 12, 0);
                    GM_ST(uint64_t, ((uint8_t *)&t17 + 0), t12);
                    GM_ST(uint32_t, ((uint8_t *)&t17 + 8), t16);
                    uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)&t17) + 0));
                    gm_l4 = t20;
                    uint32_t t21 = GM_LD(uint32_t, (((uint8_t *)&t17) + 8));
                    gm_l5 = t21;
                    {
                        uint32_t t24 = gm_l5;
                        uint32_t t25 = gm_l1;
                        uint8_t t26 = ((t24 == t25));
                        if (!t26) goto ie23;
                        {
                            uint64_t t28 = gm_l4;
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
                        goto ix23;
ie23:;
ix23:;
                    }
                    goto me18;
mn19:;
                    gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 542, 5);
me18:;
                }
lc6:;
                {
                    uint64_t t32 = gm_l3;
                    uint64_t r33; bool o34 = __builtin_add_overflow(t32, ((uint64_t)1ULL), &r33);
                    uint64_t t35 = r33;
                    gm_l3 = t35;
                }
            }
lb6:;
        }
    }
    {
        gm_tf5srh4mp4b3i t36;
        memset(&t36, 0, sizeof t36);
        uint8_t t37 = 0;
        F.ret = t36;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_probe_free_4hilimu(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_probe_free_4hilimu F;
    void * gm_l0; /* g */
    void * gm_l1; /* def */
    uint8_t gm_l2; /* x */
    void * gm_l3; /* b */
    GM_STACK_CHECK(&F, 99u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        uint8_t t4 = GM_LD(uint8_t, ((((uint8_t *)t3) + 1136) + 1));
        if (!t4) goto ie2;
        {
            uint8_t t6 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t8 = gm_l1;
        if (!((((((uint8_t *)t8) + 145) + 1))[1] != 0)) goto ie7;
        uint8_t t9 = GM_LD(uint8_t, ((((uint8_t *)t8) + 145) + 1));
        gm_l2 = t9;
        uint8_t t10 = gm_l2;
        if (!t10) goto ie7;
        {
            void * t12 = gm_l1;
            void * t13 = GM_LD(void *, (((uint8_t *)t12) + 152));
            void * t14;
            if ((GM_LD(void *, ((uint8_t *)&t13)) != 0)) {
                void * t15 = GM_LD(void *, (((uint8_t *)&t13) + 0));
                t14 = t15;
            } else {
                uint8_t t16 = 0;
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            gm_l3 = t14;
        }
        {
            void * t17 = gm_l0;
            void * t18 = gm_l3;
            uint8_t t19 = gm_f_calls_probe_2ia3rhf((void *)NULL, cx, NULL, t17, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t20 = 0;
            F.ret = t19;
            goto gm_fret;
        }
        goto ix7;
ie7:;
ix7:;
    }
    {
        uint8_t t21 = 0;
        F.ret = ((uint8_t)0);
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
inline __attribute__((aligned(4))) uint8_t gm_f_reads_place_nji55rm(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_reads_place_nji55rm F;
    void * gm_l0; /* fg */
    void * gm_l1; /* e */
    void * gm_l2; /* base */
    GM_STACK_CHECK(&F, 122u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l1;
        uint8_t t9 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        switch (t9) { case ((uint8_t)0ULL): goto ma7; case ((uint8_t)1ULL): goto ma7; case ((uint8_t)2ULL): goto ma7; case ((uint8_t)3ULL): goto ma7; case ((uint8_t)4ULL): goto ma7; case ((uint8_t)5ULL): goto ma7; case ((uint8_t)6ULL): goto ma7; case ((uint8_t)7ULL): goto ma7; case ((uint8_t)8ULL): goto ma7; case ((uint8_t)9ULL): goto ma7; case ((uint8_t)10ULL): goto ma7; case ((uint8_t)11ULL): goto ma7; case ((uint8_t)12ULL): goto ma7; case ((uint8_t)13ULL): goto ma7; case ((uint8_t)14ULL): goto ma7; case ((uint8_t)15ULL): goto ma4; case ((uint8_t)16ULL): goto ma4; case ((uint8_t)17ULL): goto ma4; case ((uint8_t)18ULL): goto ma4; case ((uint8_t)19ULL): goto ma5; case ((uint8_t)20ULL): goto ma6; case ((uint8_t)21ULL): goto ma7; case ((uint8_t)22ULL): goto ma7; case ((uint8_t)23ULL): goto ma7; case ((uint8_t)24ULL): goto ma7; case ((uint8_t)25ULL): goto ma7; case ((uint8_t)26ULL): goto ma7; case ((uint8_t)27ULL): goto ma7; case ((uint8_t)28ULL): goto ma7; case ((uint8_t)29ULL): goto ma7; case ((uint8_t)30ULL): goto ma7; case ((uint8_t)31ULL): goto ma7; case ((uint8_t)32ULL): goto ma7; case ((uint8_t)33ULL): goto ma7; case ((uint8_t)34ULL): goto ma7; case ((uint8_t)35ULL): goto ma7; case ((uint8_t)36ULL): goto ma7; case ((uint8_t)37ULL): goto ma7; case ((uint8_t)38ULL): goto ma7; case ((uint8_t)39ULL): goto ma7; case ((uint8_t)40ULL): goto ma7; case ((uint8_t)41ULL): goto ma7; case ((uint8_t)42ULL): goto ma7; case ((uint8_t)43ULL): goto ma7; case ((uint8_t)44ULL): goto ma7; case ((uint8_t)45ULL): goto ma7; case ((uint8_t)46ULL): goto ma7; case ((uint8_t)47ULL): goto ma7; case ((uint8_t)48ULL): goto ma7; case ((uint8_t)49ULL): goto ma7; case ((uint8_t)50ULL): goto ma7; case ((uint8_t)51ULL): goto ma7; case ((uint8_t)52ULL): goto ma7; case ((uint8_t)53ULL): goto ma7; case ((uint8_t)54ULL): goto ma7; case ((uint8_t)55ULL): goto ma7; case ((uint8_t)56ULL): goto ma7; case ((uint8_t)57ULL): goto ma7; case ((uint8_t)58ULL): goto ma7; default: goto ma7; }
ma4:;
        uint8_t t13 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t13 != ((uint8_t)15ULL)) goto pa12;
        goto po11;
pa12:;
        uint8_t t15 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t15 != ((uint8_t)16ULL)) goto pa14;
        goto po11;
pa14:;
        uint8_t t17 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t17 != ((uint8_t)18ULL)) goto pa16;
        goto po11;
pa16:;
        uint8_t t19 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t19 != ((uint8_t)17ULL)) goto pa18;
        goto po11;
pa18:;
        goto mn10;
po11:;
        uint8_t t20 = 0;
        F.ret = ((uint8_t)1);
        goto gm_fret;
        goto me3;
mn10:;
ma5:;
        uint8_t t22 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t22 != ((uint8_t)19ULL)) goto mn21;
        void * t23 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l2 = t23;
        void * t24 = gm_l0;
        void * t25 = GM_LD(void *, (((uint8_t *)t24) + 0));
        void * t26 = (void *)(((uint8_t *)t25) + 8);
        void * t27 = gm_l2;
        gm_t88rcucg1b7uk t28 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t27) + 0));
        gm_t57euh612ligr t29;
        gm_f_bits_layout_23rect9((void *)&t29, (void *)NULL, cx, NULL, t26, t28);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t30;
        if (((((uint8_t *)&t29))[24] != 0)) goto mn32;
        t30 = ((uint8_t)1);
        goto me31;
mn32:;
        t30 = ((uint8_t)0);
        goto me31;
mn33:;
        gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 1035, 35);
me31:;
        uint8_t t34 = 0;
        F.ret = t30;
        goto gm_fret;
        goto me3;
mn21:;
ma6:;
        uint8_t t36 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t36 != ((uint8_t)20ULL)) goto mn35;
        uint8_t t37 = 0;
        F.ret = ((uint8_t)1);
        goto gm_fret;
        goto me3;
mn35:;
ma7:;
        uint8_t t39 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
        goto me3;
mn38:;
ma8:;
        gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 1033, 5);
me3:;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_replacen1_7ksfmmn(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, gm_b16_8 a2) {
    struct gm_fr_replacen1_7ksfmmn F;
    gm_b16_8 gm_l0; /* text */
    gm_b16_8 gm_l1; /* from */
    gm_b16_8 gm_l2; /* to */
    uint64_t gm_l3; /* i */
    GM_STACK_CHECK(&F, 410u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        gm_b16_8 t2 = gm_l0;
        gm_b16_8 t3 = gm_l1;
        gm_tf5srh4mp4b3i t4 = gm_f_find_nab29co(t2, t3);
        uint64_t t5;
        if (((((uint8_t *)&t4))[8] != 0)) {
            uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)&t4) + 0));
            t5 = t6;
        } else {
            gm_b16_8 t7 = gm_l0;
            uint8_t t8 = 0;
            F.ret = t7;
            goto gm_fret;
        }
        gm_l3 = t5;
    }
    {
        gm_tfeab279c16rr t9;
        uint64_t t10 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        void * t11 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
        uint64_t t12 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t13 = gm_l3;
        uint64_t t14 = (uintptr_t)t13;
        if (t12 > t14 || t14 > (uintptr_t)t10) gm_trap_slice(t12, t14, t10, gm_srcmok80ho8hh01, 47, 33);
        gm_b16_8 t15;
        GM_ST(void *, (uint8_t *)&t15, (void *)(((uint8_t *)t11 + t12 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)(t14 - t12));
        GM_ST(gm_b16_8, (uint8_t *)&t9 + 0, t15);
        gm_b16_8 t16 = gm_l2;
        GM_ST(gm_b16_8, (uint8_t *)&t9 + 16, t16);
        uint64_t t17 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        void * t18 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
        uint64_t t19 = gm_l3;
        uint64_t t20 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint64_t r21; bool o22 = __builtin_add_overflow(t19, t20, &r21);
        if (o22) gm_trap("integer overflow in `+`", gm_srcmok80ho8hh01, 47, 54);
        uint64_t t23 = r21;
        uint64_t t24 = (uintptr_t)t23;
        if (t24 > (uintptr_t)t17 || (uintptr_t)t17 > (uintptr_t)t17) gm_trap_slice(t24, (uintptr_t)t17, t17, gm_srcmok80ho8hh01, 47, 49);
        gm_b16_8 t25;
        GM_ST(void *, (uint8_t *)&t25, (void *)(((uint8_t *)t18 + t24 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t25 + 8, (uintptr_t)((uintptr_t)t17 - t24));
        GM_ST(gm_b16_8, (uint8_t *)&t9 + 32, t25);
        gm_tabb710d9mq2e t26;
        gm_f_format_dsta2ql((void *)&t26, (void *)NULL, cx, NULL, &t9);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t26))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t26) + 8, gm_srcmok80ho8hh01, 47, 12);
        gm_b16_8 t27 = GM_LD(gm_b16_8, (((uint8_t *)&t26) + 8));
        gm_b16_8 t28 = GM_LD(gm_b16_8, ((uint8_t *)&t27));
        uint8_t t29 = 0;
        F.ret = t28;
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
extern __attribute__((aligned(4))) gm_b16_8 gm_f_size_attrs_abk1l3m(uint32_t a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_src_line_d24sajl(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1) {
    struct gm_fr_src_line_d24sajl F;
    void * gm_l0; /* fg */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* d */
    GM_STACK_CHECK(&F, 154u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        void * t4 = GM_LD(void *, (((uint8_t *)t3) + 0));
        gm_tl874t2atnlh3 t5 = gm_l1;
        gm_th2lqbo77pvsc t6;
        gm_f_line_directive_fjg94gc((void *)&t6, (void *)NULL, cx, NULL, t4, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!((((uint8_t *)&t6))[16] != 0)) goto ie2;
        gm_b16_8 t7 = GM_LD(gm_b16_8, ((uint8_t *)&t6));
        gm_l2 = t7;
        {
            void * t9 = gm_l0;
            void * t10 = (void *)(((uint8_t *)t9) + 184);
            gm_b16_8 t11 = gm_l2;
            gm_tnpifv20irnph t12;
            GM_ST(gm_b16_8, ((uint8_t *)&t12 + 0), t11);
            gm_tftb1odc739t5 t13 = gm_f_write_cdouvhd(t10, t12);
            if (((((uint8_t *)&t13))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t13) + 1, gm_srcmok80ho8hh01, 440, 41);
            gm_b0_1 t14 = GM_LD(gm_b0_1, (((uint8_t *)&t13) + 1));
        }
        goto ix2;
ie2:;
ix2:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_start_exit_m57dnn5(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, gm_b16_8 a2, gm_b16_8 a3, gm_b16_8 a4) {
    struct gm_fr_start_exit_m57dnn5 F;
    void * gm_l0; /* fg */
    gm_t9qkh0rsooc8d gm_l1; /* kind */
    gm_b16_8 gm_l2; /* frame */
    gm_b16_8 gm_l3; /* label */
    gm_b16_8 gm_l4; /* is_err */
    uint8_t gm_l5; /* saved */
    GM_STACK_CHECK(&F, 283u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        uint8_t t3 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8) == 6) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l2)), gm_sb8kl54a499bf, 6) == 0));
        if (!t3) goto ie2;
        void * t4 = gm_l0;
        uint8_t t5 = GM_LD(uint8_t, (((uint8_t *)t4) + 298));
        uint8_t t6 = (uint8_t)!t5;
        if (!t6) goto ie2;
        void * t7 = gm_l0;
        gm_t9qkh0rsooc8d t8 = gm_l1;
        gm_b16_8 t9 = gm_l3;
        gm_b16_8 t10 = gm_l4;
        uint8_t t11 = gm_f_local_exit_4aogt00((void *)NULL, cx, NULL, t7, t8, t9, t10);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t11) goto ie2;
        {
            gm_b0_1 t13;
            uint8_t t14 = 0;
            F.ret = t13;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t15 = gm_l0;
        gm_t0uuhaljsf410 t16;
        gm_t9qkh0rsooc8d t17 = gm_l1;
        gm_b16_8 t18 = gm_f_exit_c_name_i72r98r(t17);
        GM_ST(gm_b16_8, (uint8_t *)&t16 + 0, t18);
        gm_b16_8 t19 = gm_l2;
        GM_ST(gm_b16_8, (uint8_t *)&t16 + 16, t19);
        gm_b16_8 t20 = gm_l3;
        GM_ST(gm_b16_8, (uint8_t *)&t16 + 32, t20);
        gm_b16_8 t21 = gm_l4;
        GM_ST(gm_b16_8, (uint8_t *)&t16 + 48, t21);
        gm_b0_1 t22 = gm_f_linef_7p5m9ll((void *)NULL, cx, NULL, t15, &t16);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t23 = gm_l0;
        uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)t23) + 296));
        gm_l5 = t24;
    }
    {
        void * t25 = gm_l0;
        GM_ST(uint8_t, (((uint8_t *)t25) + 296), ((uint8_t)0));
    }
    {
        void * t26 = gm_l0;
        gm_b0_1 t27 = gm_f_ladder_36amdbn((void *)NULL, cx, NULL, t26);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t28 = gm_l0;
        uint8_t t29 = gm_l5;
        GM_ST(uint8_t, (((uint8_t *)t28) + 296), t29);
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
__attribute__((aligned(4))) void gm_f_stmt_1t29bfr(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_stmt_1t29bfr F;
    void * gm_l0; /* fg */
    void * gm_l1; /* s */
    uint32_t gm_l2; /* local */
    void * gm_l3; /* init */
    void * gm_l4; /* x */
    gm_b16_8 gm_l5; /* lv */
    gm_b16_8 gm_l6; /* a */
    gm_b16_8 gm_l7; /* lv */
    gm_b16_8 gm_l8; /* lv */
    gm_b16_8 gm_l9; /* locals */
    void * gm_l10; /* init */
    gm_b16_8 gm_l11; /* v */
    gm_b16_8 gm_l12; /* t */
    gm_b16_8 gm_l13; /* offs */
    gm_b16_8 gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    uint64_t gm_l16; /* i */
    gm_t9805qtonbsag gm_l17; /* l */
    uint32_t gm_l18; /* x */
    gm_t88rcucg1b7uk gm_l19; /* lt */
    gm_b16_8 gm_l20; /* c */
    gm_b16_8 gm_l21; /* lv */
    void * gm_l22; /* place */
    void * gm_l23; /* value */
    void * gm_l24; /* base */
    uint32_t gm_l25; /* index */
    gm_tcmkompbl09il gm_l26; /* bl */
    uint32_t gm_l27; /* foff */
    uint32_t gm_l28; /* w */
    void * gm_l29; /* outer */
    uint32_t gm_l30; /* off */
    void * gm_l31; /* b */
    uint32_t gm_l32; /* i */
    gm_tcmkompbl09il gm_l33; /* fs */
    gm_tcmkompbl09il gm_l34; /* ol */
    gm_b16_8 gm_l35; /* a */
    gm_b16_8 gm_l36; /* v */
    gm_b16_8 gm_l37; /* raw */
    gm_b16_8 gm_l38; /* uc */
    gm_b16_8 gm_l39; /* m */
    gm_b16_8 gm_l40; /* p */
    gm_b16_8 gm_l41; /* a */
    gm_b16_8 gm_l42; /* p */
    gm_b16_8 gm_l43; /* c */
    gm_t9805qtonbsag gm_l44; /* root */
    uint32_t gm_l45; /* l */
    gm_b16_8 gm_l46; /* v */
    gm_b16_8 gm_l47; /* lv */
    gm_b16_8 gm_l48; /* p */
    gm_b16_8 gm_l49; /* v */
    gm_b16_8 gm_l50; /* c */
    gm_b16_8 gm_l51; /* places */
    void * gm_l52; /* value */
    gm_b16_8 gm_l54; /* <tmp> */
    uint64_t gm_l55; /* <tmp> */
    void * gm_l56; /* pl */
    void * gm_l57; /* x */
    gm_b16_8 gm_l58; /* a */
    gm_b16_8 gm_l59; /* v */
    gm_b16_8 gm_l60; /* t */
    gm_b16_8 gm_l61; /* offs */
    uint64_t gm_l62; /* <tmp> */
    uint64_t gm_l63; /* <tmp> */
    uint64_t gm_l64; /* i */
    gm_b16_8 gm_l65; /* a */
    void * gm_l66; /* pe */
    gm_b16_8 gm_l67; /* c */
    void * gm_l68; /* e */
    uint8_t gm_l69; /* is_err */
    void * gm_l70; /* body */
    uint8_t gm_l72; /* saved_split */
    void * gm_l75; /* top */
    GM_STACK_CHECK(&F, 6112u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        {
            void * t3 = gm_l1;
            uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)t3) + 0));
            switch (t12) { case ((uint8_t)0ULL): goto ma5; case ((uint8_t)1ULL): goto ma6; case ((uint8_t)2ULL): goto ma7; case ((uint8_t)3ULL): goto ma8; case ((uint8_t)4ULL): goto ma9; case ((uint8_t)5ULL): goto ma10; default: goto ma11; }
ma5:;
            uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)t3) + 0));
            if (t14 != ((uint8_t)0ULL)) goto mn13;
            uint32_t t15 = GM_LD(uint32_t, ((((uint8_t *)t3) + 8) + 0));
            gm_l2 = t15;
            void * t16 = GM_LD(void *, ((((uint8_t *)t3) + 8) + 8));
            gm_l3 = t16;
            {
                void * t18 = gm_l3;
                void * t19;
                if ((GM_LD(void *, ((uint8_t *)&t18)) != 0)) {
                    void * t20 = GM_LD(void *, (((uint8_t *)&t18) + 0));
                    t19 = t20;
                } else {
                    gm_b0_1 t21;
                    memset(&(*(gm_tqgtd5gtgj65s *)gm_out), 0, sizeof (*(gm_tqgtd5gtgj65s *)gm_out));
                    ((uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out))[0] = 0;
                    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 8, t21);
                    goto gm_fdone;
                }
                gm_l4 = t19;
            }
            {
                void * t23 = gm_l4;
                uint8_t t24 = ((GM_LD(uint8_t, (((uint8_t *)t23) + 8) + 0) == ((uint8_t)8ULL)));
                if (!t24) goto ie22;
                {
                    void * t27 = gm_l0;
                    void * t28 = GM_LD(void *, (((uint8_t *)t27) + 0));
                    uint8_t t29 = GM_LD(uint8_t, ((((uint8_t *)t28) + 1136) + 1));
                    if (!t29) goto ie26;
                    {
                        void * t31 = gm_l0;
                        uint32_t t32 = gm_l2;
                        gm_b16_8 t33 = gm_f_local_lv_m2jtf0n((void *)NULL, cx, NULL, t31, t32);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l5 = t33;
                    }
                    {
                        void * t34 = gm_l0;
                        gm_t6j6smj7qoctu t35;
                        gm_b16_8 t36 = gm_l5;
                        GM_ST(gm_b16_8, (uint8_t *)&t35 + 0, t36);
                        gm_b16_8 t37 = gm_l5;
                        GM_ST(gm_b16_8, (uint8_t *)&t35 + 16, t37);
                        gm_b0_1 t38 = gm_f_linef_caq6p77((void *)NULL, cx, NULL, t34, &t35);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    goto ix26;
ie26:;
ix26:;
                }
                {
                    gm_b0_1 t39;
                    memset(&(*(gm_tqgtd5gtgj65s *)gm_out), 0, sizeof (*(gm_tqgtd5gtgj65s *)gm_out));
                    ((uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out))[0] = 0;
                    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 8, t39);
                    goto gm_fdone;
                }
                goto ix22;
ie22:;
ix22:;
            }
            {
                void * t41 = gm_l0;
                void * t42 = gm_l4;
                uint8_t t43 = gm_f_big_place_raoupin((void *)NULL, cx, NULL, t41, t42);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (!t43) goto ie40;
                {
                    void * t45 = gm_l0;
                    void * t46 = gm_l4;
                    gm_t5o0tpv5jka2l t47;
                    gm_f_place_addr_7ji15ot((void *)&t47, (void *)NULL, cx, NULL, t45, t46);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t47))[0] != 0)) {
                        gm_tqgtd5gtgj65s t48;
                        GM_ST(uint8_t, (uint8_t *)&t48 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t48 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t48 + 4, 0);
                        ((uint8_t *)&t48)[0] = 1;
                        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t48 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t47) + 8)));
                        F.ret = t48;
                        goto gm_fret;
                    }
                    gm_b16_8 t49 = GM_LD(gm_b16_8, (((uint8_t *)&t47) + 8));
                    gm_l6 = t49;
                }
                {
                    void * t50 = gm_l0;
                    uint32_t t51 = gm_l2;
                    gm_b16_8 t52 = gm_f_local_lv_m2jtf0n((void *)NULL, cx, NULL, t50, t51);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l7 = t52;
                }
                {
                    void * t53 = gm_l0;
                    gm_tfeab279c16rr t54;
                    gm_b16_8 t55 = gm_l7;
                    GM_ST(gm_b16_8, (uint8_t *)&t54 + 0, t55);
                    gm_b16_8 t56 = gm_l6;
                    GM_ST(gm_b16_8, (uint8_t *)&t54 + 16, t56);
                    gm_b16_8 t57 = gm_l7;
                    GM_ST(gm_b16_8, (uint8_t *)&t54 + 32, t57);
                    gm_b0_1 t58 = gm_f_linef_heeeedc((void *)NULL, cx, NULL, t53, &t54);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    gm_b0_1 t59;
                    memset(&(*(gm_tqgtd5gtgj65s *)gm_out), 0, sizeof (*(gm_tqgtd5gtgj65s *)gm_out));
                    ((uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out))[0] = 0;
                    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 8, t59);
                    goto gm_fdone;
                }
                goto ix40;
ie40:;
ix40:;
            }
            {
                void * t60 = gm_l0;
                uint32_t t61 = gm_l2;
                gm_b16_8 t62 = gm_f_local_lv_m2jtf0n((void *)NULL, cx, NULL, t60, t61);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l8 = t62;
            }
            {
                void * t63 = gm_l0;
                void * t64 = gm_l4;
                gm_b16_8 t65 = gm_l8;
                gm_tnrltbc61i6v1 t66;
                GM_ST(uint8_t, (uint8_t *)&t66 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t66 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t66 + 4, 0);
                GM_ST(uint32_t, (uint8_t *)&t66 + 8, 0);
                GM_ST(uint8_t, ((uint8_t *)&t66 + 0), ((uint8_t)0ULL));
                gm_tqgtd5gtgj65s t67;
                gm_f_expr_into_dn9kavo((void *)&t67, (void *)NULL, cx, NULL, t63, t64, t65, t66);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t67))[0] != 0)) {
                    gm_tqgtd5gtgj65s t68;
                    GM_ST(uint8_t, (uint8_t *)&t68 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t68 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t68 + 4, 0);
                    ((uint8_t *)&t68)[0] = 1;
                    GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t68 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t67) + 8)));
                    F.ret = t68;
                    goto gm_fret;
                }
                gm_b0_1 t69 = GM_LD(gm_b0_1, (((uint8_t *)&t67) + 8));
            }
            goto me4;
mn13:;
ma6:;
            uint8_t t71 = GM_LD(uint8_t, (((uint8_t *)t3) + 0));
            if (t71 != ((uint8_t)1ULL)) goto mn70;
            gm_b16_8 t72 = GM_LD(gm_b16_8, ((((uint8_t *)t3) + 8) + 0));
            gm_l9 = t72;
            void * t73 = GM_LD(void *, ((((uint8_t *)t3) + 8) + 16));
            gm_l10 = t73;
            {
                void * t75 = gm_l0;
                void * t76 = gm_l10;
                gm_t5o0tpv5jka2l t77;
                gm_f_expr_porbc9t((void *)&t77, (void *)NULL, cx, NULL, t75, t76);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t77))[0] != 0)) {
                    gm_tqgtd5gtgj65s t78;
                    GM_ST(uint8_t, (uint8_t *)&t78 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t78 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t78 + 4, 0);
                    ((uint8_t *)&t78)[0] = 1;
                    GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t78 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t77) + 8)));
                    F.ret = t78;
                    goto gm_fret;
                }
                gm_b16_8 t79 = GM_LD(gm_b16_8, (((uint8_t *)&t77) + 8));
                gm_l11 = t79;
            }
            {
                void * t80 = gm_l0;
                void * t81 = gm_l10;
                gm_t88rcucg1b7uk t82 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t81) + 0));
                gm_b16_8 t83 = gm_l11;
                gm_b16_8 t84 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t80, t82, t83);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l12 = t84;
            }
            {
                void * t85 = gm_l0;
                void * t86 = GM_LD(void *, (((uint8_t *)t85) + 0));
                void * t87 = (void *)(((uint8_t *)t86) + 8);
                void * t88 = gm_l10;
                gm_t88rcucg1b7uk t89 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t88) + 0));
                gm_b16_8 t90 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t87, t89);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l13 = t90;
            }
            {
                {
                    gm_b16_8 t92 = gm_l9;
                    gm_b16_8 t93 = GM_LD(gm_b16_8, ((uint8_t *)&t92));
                    gm_l14 = t93;
                }
                {
                    gm_l15 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t95 = gm_l15;
                        uint64_t t96 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                        uint8_t t97 = (t95 < t96);
                        if (!t97) goto lb94;
                        {
                            uint64_t t100 = gm_l15;
                            uint64_t t101 = gm_l15;
                            uint64_t t102 = (uintptr_t)t101;
                            uint64_t t103 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                            gm_t9805qtonbsag t104 = GM_LD(gm_t9805qtonbsag, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l14)) + t102 * 8));
                            gm_tp0pp2cmr6tvd t105;
                            GM_ST(uint64_t, ((uint8_t *)&t105 + 0), t100);
                            GM_ST(gm_t9805qtonbsag, ((uint8_t *)&t105 + 8), t104);
                            uint64_t t108 = GM_LD(uint64_t, (((uint8_t *)&t105) + 0));
                            gm_l16 = t108;
                            gm_t9805qtonbsag t109 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)&t105) + 8));
                            gm_l17 = t109;
                            {
                                if (!((((uint8_t *)&gm_l17))[4] != 0)) goto ie111;
                                uint32_t t112 = GM_LD(uint32_t, ((uint8_t *)&gm_l17));
                                gm_l18 = t112;
                                {
                                    uint32_t t114 = gm_l18;
                                    uint64_t t115 = (uint64_t)t114;
                                    uint64_t t116 = (uintptr_t)t115;
                                    void * t117 = gm_l0;
                                    void * t118 = GM_LD(void *, (((uint8_t *)t117) + 176));
                                    uint64_t t119 = GM_LD(uintptr_t, (((uint8_t *)t118) + 48) + 8);
                                    if (t116 >= (uintptr_t)(t119)) gm_trap_index(t116, t119, gm_srcmok80ho8hh01, 1137, 27);
                                    gm_t88rcucg1b7uk t120 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, (((uint8_t *)t118) + 48)) + t116 * 40) + 16));
                                    gm_l19 = t120;
                                }
                                {
                                    void * t121 = gm_l0;
                                    void * t122 = GM_LD(void *, (((uint8_t *)t121) + 0));
                                    void * t123 = (void *)(((uint8_t *)t122) + 8);
                                    gm_t88rcucg1b7uk t124 = gm_l19;
                                    gm_b16_8 t125 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t123, t124);
                                    if (gm_unw.kind) {
                                        goto gm_ll98;
                                    }
                                    gm_l20 = t125;
                                }
                                {
                                    void * t126 = gm_l0;
                                    uint32_t t127 = gm_l18;
                                    gm_b16_8 t128 = gm_f_local_lv_m2jtf0n((void *)NULL, cx, NULL, t126, t127);
                                    if (gm_unw.kind) {
                                        goto gm_ll98;
                                    }
                                    gm_l21 = t128;
                                }
                                {
                                    void * t129 = gm_l0;
                                    gm_tttl9nsdo1iu6 t130;
                                    gm_b16_8 t131 = gm_l21;
                                    GM_ST(gm_b16_8, (uint8_t *)&t130 + 0, t131);
                                    gm_b16_8 t132 = gm_l20;
                                    GM_ST(gm_b16_8, (uint8_t *)&t130 + 16, t132);
                                    gm_b16_8 t133 = gm_l12;
                                    GM_ST(gm_b16_8, (uint8_t *)&t130 + 32, t133);
                                    uint64_t t134 = gm_l16;
                                    uint64_t t135 = (uintptr_t)t134;
                                    uint64_t t136 = GM_LD(uintptr_t, ((uint8_t *)&gm_l13) + 8);
                                    if (t135 >= (uintptr_t)(t136)) gm_trap_index(t135, t136, gm_srcmok80ho8hh01, 1140, 80);
                                    uint64_t t137 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l13)) + t135 * 8));
                                    GM_ST(uint64_t, (uint8_t *)&t130 + 48, t137);
                                    gm_b0_1 t138 = gm_f_linef_h4kb1fp((void *)NULL, cx, NULL, t129, &t130);
                                    if (gm_unw.kind) {
                                        goto gm_ll98;
                                    }
                                }
                                goto ix111;
ie111:;
ix111:;
                            }
                            goto me106;
mn107:;
                            gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 1135, 13);
me106:;
                        }
lc94:;
                        {
                            uint64_t t140 = gm_l15;
                            uint64_t r141; bool o142 = __builtin_add_overflow(t140, ((uint64_t)1ULL), &r141);
                            uint64_t t143 = r141;
                            gm_l15 = t143;
                        }
                    }
gm_ll98:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb94; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc94; }
                    }
                    goto gm_lad;
lb94:;
                }
            }
            goto me4;
mn70:;
ma7:;
            uint8_t t145 = GM_LD(uint8_t, (((uint8_t *)t3) + 0));
            if (t145 != ((uint8_t)2ULL)) goto mn144;
            void * t146 = GM_LD(void *, ((((uint8_t *)t3) + 8) + 0));
            gm_l22 = t146;
            void * t147 = GM_LD(void *, ((((uint8_t *)t3) + 8) + 8));
            gm_l23 = t147;
            {
                void * t150 = gm_l22;
                uint8_t t151 = GM_LD(uint8_t, ((((uint8_t *)t150) + 8) + 0));
                if (t151 != ((uint8_t)19ULL)) goto ie149;
                void * t152 = GM_LD(void *, (((((uint8_t *)t150) + 8) + 8) + 0));
                gm_l24 = t152;
                uint32_t t153 = GM_LD(uint32_t, (((((uint8_t *)t150) + 8) + 8) + 8));
                gm_l25 = t153;
                {
                    void * t156 = gm_l0;
                    void * t157 = GM_LD(void *, (((uint8_t *)t156) + 0));
                    void * t158 = (void *)(((uint8_t *)t157) + 8);
                    void * t159 = gm_l24;
                    gm_t88rcucg1b7uk t160 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t159) + 0));
                    gm_t57euh612ligr t161;
                    gm_f_bits_layout_23rect9((void *)&t161, (void *)NULL, cx, NULL, t158, t160);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (!((((uint8_t *)&t161))[24] != 0)) goto ie155;
                    gm_l26 = (*(gm_tcmkompbl09il *)(((uint8_t *)&t161) + 0));
                    {
                        uint32_t t163 = gm_l25;
                        uint64_t t164 = (uint64_t)t163;
                        uint64_t t165 = (uintptr_t)t164;
                        uint64_t t166 = GM_LD(uintptr_t, (((uint8_t *)&gm_l26) + 8) + 8);
                        if (t165 >= (uintptr_t)(t166)) gm_trap_index(t165, t166, gm_srcmok80ho8hh01, 1148, 29);
                        uint32_t t167 = GM_LD(uint32_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)&gm_l26) + 8)) + t165 * 12) + 0));
                        gm_l27 = t167;
                    }
                    {
                        uint32_t t168 = gm_l25;
                        uint64_t t169 = (uint64_t)t168;
                        uint64_t t170 = (uintptr_t)t169;
                        uint64_t t171 = GM_LD(uintptr_t, (((uint8_t *)&gm_l26) + 8) + 8);
                        if (t170 >= (uintptr_t)(t171)) gm_trap_index(t170, t171, gm_srcmok80ho8hh01, 1149, 26);
                        uint32_t t172 = GM_LD(uint32_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)&gm_l26) + 8)) + t170 * 12) + 4));
                        gm_l28 = t172;
                    }
                    {
                        void * t173 = gm_l24;
                        gm_l29 = t173;
                    }
                    {
                        uint32_t t174 = gm_l27;
                        gm_l30 = t174;
                    }
                    {
                        for (;;) {
                            if (!((uint8_t)1)) goto lb175;
                            {
                                void * t179 = gm_l29;
                                uint8_t t180 = GM_LD(uint8_t, ((((uint8_t *)t179) + 8) + 0));
                                if (t180 != ((uint8_t)19ULL)) goto ie178;
                                void * t181 = GM_LD(void *, (((((uint8_t *)t179) + 8) + 8) + 0));
                                gm_l31 = t181;
                                uint32_t t182 = GM_LD(uint32_t, (((((uint8_t *)t179) + 8) + 8) + 8));
                                gm_l32 = t182;
                                {
                                    void * t184 = gm_l0;
                                    void * t185 = GM_LD(void *, (((uint8_t *)t184) + 0));
                                    void * t186 = (void *)(((uint8_t *)t185) + 8);
                                    void * t187 = gm_l31;
                                    gm_t88rcucg1b7uk t188 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t187) + 0));
                                    gm_t57euh612ligr t189;
                                    gm_f_bits_layout_23rect9((void *)&t189, (void *)NULL, cx, NULL, t186, t188);
                                    if (gm_unw.kind) {
                                        goto gm_ll176;
                                    }
                                    gm_tcmkompbl09il t190;
                                    if (((((uint8_t *)&t189))[24] != 0)) {
                                        t190 = (*(gm_tcmkompbl09il *)(((uint8_t *)&t189) + 0));
                                    } else {
                                        goto lb175;
                                    }
                                    gm_l33 = t190;
                                }
                                {
                                    uint32_t t191 = gm_l30;
                                    uint32_t t192 = gm_l32;
                                    uint64_t t193 = (uint64_t)t192;
                                    uint64_t t194 = (uintptr_t)t193;
                                    uint64_t t195 = GM_LD(uintptr_t, (((uint8_t *)&gm_l33) + 8) + 8);
                                    if (t194 >= (uintptr_t)(t195)) gm_trap_index(t194, t195, gm_srcmok80ho8hh01, 1156, 32);
                                    uint32_t t196 = GM_LD(uint32_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)&gm_l33) + 8)) + t194 * 12) + 0));
                                    uint32_t r197; bool o198 = __builtin_add_overflow(t191, t196, &r197);
                                    if (o198) gm_trap("integer overflow in `+`", gm_srcmok80ho8hh01, 1156, 25);
                                    uint32_t t199 = r197;
                                    gm_l30 = t199;
                                }
                                {
                                    void * t200 = gm_l31;
                                    gm_l29 = t200;
                                }
                                goto ix178;
ie178:;
                                {
                                    goto lb175;
                                }
ix178:;
                            }
lc175:;
                        }
gm_ll176:;
                        if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb175; }
                            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc175; }
                        }
                        goto gm_lad;
lb175:;
                    }
                    {
                        void * t202 = gm_l0;
                        void * t203 = GM_LD(void *, (((uint8_t *)t202) + 0));
                        void * t204 = (void *)(((uint8_t *)t203) + 8);
                        void * t205 = gm_l29;
                        gm_t88rcucg1b7uk t206 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t205) + 0));
                        gm_t57euh612ligr t207;
                        gm_f_bits_layout_23rect9((void *)&t207, (void *)NULL, cx, NULL, t204, t206);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        if (!((((uint8_t *)&t207))[24] != 0)) gm_trap("unwrap of null", gm_srcmok80ho8hh01, 1159, 27);
                        gm_l34 = (*(gm_tcmkompbl09il *)(((uint8_t *)&t207) + 0));
                    }
                    {
                        void * t208 = gm_l0;
                        void * t209 = gm_l29;
                        void * t210 = gm_l23;
                        gm_t5o0tpv5jka2l t211;
                        gm_f_assign_addr_rlofoa5((void *)&t211, (void *)NULL, cx, NULL, t208, t209, t210);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        if (((((uint8_t *)&t211))[0] != 0)) {
                            gm_tqgtd5gtgj65s t212;
                            GM_ST(uint8_t, (uint8_t *)&t212 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t212 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t212 + 4, 0);
                            ((uint8_t *)&t212)[0] = 1;
                            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t212 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t211) + 8)));
                            F.ret = t212;
                            goto gm_fret;
                        }
                        gm_b16_8 t213 = GM_LD(gm_b16_8, (((uint8_t *)&t211) + 8));
                        gm_l35 = t213;
                    }
                    {
                        void * t214 = gm_l0;
                        void * t215 = gm_l23;
                        gm_t5o0tpv5jka2l t216;
                        gm_f_expr_porbc9t((void *)&t216, (void *)NULL, cx, NULL, t214, t215);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        if (((((uint8_t *)&t216))[0] != 0)) {
                            gm_tqgtd5gtgj65s t217;
                            GM_ST(uint8_t, (uint8_t *)&t217 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t217 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t217 + 4, 0);
                            ((uint8_t *)&t217)[0] = 1;
                            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t217 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t216) + 8)));
                            F.ret = t217;
                            goto gm_fret;
                        }
                        gm_b16_8 t218 = GM_LD(gm_b16_8, (((uint8_t *)&t216) + 8));
                        gm_l36 = t218;
                    }
                    {
                        void * t219 = gm_l0;
                        void * t220 = gm_l23;
                        gm_t88rcucg1b7uk t221 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t220) + 0));
                        gm_b16_8 t222 = gm_l36;
                        gm_b16_8 t223 = gm_f_bits_raw_e0qg9mb((void *)NULL, cx, NULL, t219, t221, t222);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l37 = t223;
                    }
                    {
                        void * t224 = gm_l0;
                        void * t225 = GM_LD(void *, (((uint8_t *)t224) + 0));
                        void * t226 = (void *)(((uint8_t *)t225) + 8);
                        gm_t88rcucg1b7uk t227 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l34) + 0));
                        gm_b16_8 t228 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t226, t227);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l38 = t228;
                    }
                    {
                        uint32_t t229 = gm_l28;
                        gm_b16_8 t230 = gm_f_mask_epjl3to((void *)NULL, cx, NULL, t229);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l39 = t230;
                    }
                    {
                        void * t231 = gm_l0;
                        gm_tt4j8kuecfafr t232;
                        memset((uint8_t *)&t232 + 100, 0, 4);
                        memset((uint8_t *)&t232 + 140, 0, 4);
                        gm_b16_8 t233 = gm_l38;
                        GM_ST(gm_b16_8, (uint8_t *)&t232 + 0, t233);
                        gm_b16_8 t234 = gm_l35;
                        GM_ST(gm_b16_8, (uint8_t *)&t232 + 16, t234);
                        gm_b16_8 t235 = gm_l38;
                        GM_ST(gm_b16_8, (uint8_t *)&t232 + 32, t235);
                        gm_b16_8 t236 = gm_l38;
                        GM_ST(gm_b16_8, (uint8_t *)&t232 + 48, t236);
                        gm_b16_8 t237 = gm_l35;
                        GM_ST(gm_b16_8, (uint8_t *)&t232 + 64, t237);
                        gm_b16_8 t238 = gm_l39;
                        GM_ST(gm_b16_8, (uint8_t *)&t232 + 80, t238);
                        uint32_t t239 = gm_l30;
                        GM_ST(uint32_t, (uint8_t *)&t232 + 96, t239);
                        gm_b16_8 t240 = gm_l37;
                        GM_ST(gm_b16_8, (uint8_t *)&t232 + 104, t240);
                        gm_b16_8 t241 = gm_l39;
                        GM_ST(gm_b16_8, (uint8_t *)&t232 + 120, t241);
                        uint32_t t242 = gm_l30;
                        GM_ST(uint32_t, (uint8_t *)&t232 + 136, t242);
                        gm_b0_1 t243 = gm_f_linef_rgkpmgb((void *)NULL, cx, NULL, t231, &t232);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    {
                        gm_b0_1 t244;
                        memset(&(*(gm_tqgtd5gtgj65s *)gm_out), 0, sizeof (*(gm_tqgtd5gtgj65s *)gm_out));
                        ((uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out))[0] = 0;
                        GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 8, t244);
                        goto gm_fdone;
                    }
                    goto ix155;
ie155:;
ix155:;
                }
                goto ix149;
ie149:;
ix149:;
            }
            {
                void * t246 = gm_l0;
                void * t247 = gm_l23;
                uint8_t t248 = gm_f_big_place_raoupin((void *)NULL, cx, NULL, t246, t247);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (!t248) goto ie245;
                {
                    void * t250 = gm_l0;
                    void * t251 = gm_l22;
                    void * t252 = gm_l23;
                    gm_t5o0tpv5jka2l t253;
                    gm_f_assign_addr_rlofoa5((void *)&t253, (void *)NULL, cx, NULL, t250, t251, t252);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t253))[0] != 0)) {
                        gm_tqgtd5gtgj65s t254;
                        GM_ST(uint8_t, (uint8_t *)&t254 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t254 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t254 + 4, 0);
                        ((uint8_t *)&t254)[0] = 1;
                        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t254 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t253) + 8)));
                        F.ret = t254;
                        goto gm_fret;
                    }
                    gm_b16_8 t255 = GM_LD(gm_b16_8, (((uint8_t *)&t253) + 8));
                    gm_l40 = t255;
                }
                {
                    void * t256 = gm_l0;
                    void * t257 = gm_l23;
                    gm_t5o0tpv5jka2l t258;
                    gm_f_place_addr_7ji15ot((void *)&t258, (void *)NULL, cx, NULL, t256, t257);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t258))[0] != 0)) {
                        gm_tqgtd5gtgj65s t259;
                        GM_ST(uint8_t, (uint8_t *)&t259 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t259 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t259 + 4, 0);
                        ((uint8_t *)&t259)[0] = 1;
                        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t259 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t258) + 8)));
                        F.ret = t259;
                        goto gm_fret;
                    }
                    gm_b16_8 t260 = GM_LD(gm_b16_8, (((uint8_t *)&t258) + 8));
                    gm_l41 = t260;
                }
                {
                    void * t261 = gm_l0;
                    gm_tftksr4op114e t262;
                    gm_b16_8 t263 = gm_l40;
                    GM_ST(gm_b16_8, (uint8_t *)&t262 + 0, t263);
                    gm_b16_8 t264 = gm_l41;
                    GM_ST(gm_b16_8, (uint8_t *)&t262 + 16, t264);
                    void * t265 = gm_l0;
                    void * t266 = GM_LD(void *, (((uint8_t *)t265) + 0));
                    void * t267 = (void *)(((uint8_t *)t266) + 8);
                    void * t268 = gm_l23;
                    gm_t88rcucg1b7uk t269 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t268) + 0));
                    uint64_t t270 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t267, t269);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(uint64_t, (uint8_t *)&t262 + 32, t270);
                    gm_b0_1 t271 = gm_f_linef_848m8fq((void *)NULL, cx, NULL, t261, &t262);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    gm_b0_1 t272;
                    memset(&(*(gm_tqgtd5gtgj65s *)gm_out), 0, sizeof (*(gm_tqgtd5gtgj65s *)gm_out));
                    ((uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out))[0] = 0;
                    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 8, t272);
                    goto gm_fdone;
                }
                goto ix245;
ie245:;
ix245:;
            }
            {
                void * t274 = gm_l0;
                void * t275 = gm_l23;
                uint8_t t276 = gm_f_builds_in_place_ae1b90u((void *)NULL, cx, NULL, t274, t275);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (!t276) goto ie273;
                {
                    void * t278 = gm_l0;
                    void * t279 = gm_l22;
                    void * t280 = gm_l23;
                    gm_t5o0tpv5jka2l t281;
                    gm_f_assign_addr_rlofoa5((void *)&t281, (void *)NULL, cx, NULL, t278, t279, t280);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t281))[0] != 0)) {
                        gm_tqgtd5gtgj65s t282;
                        GM_ST(uint8_t, (uint8_t *)&t282 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t282 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t282 + 4, 0);
                        ((uint8_t *)&t282)[0] = 1;
                        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t282 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t281) + 8)));
                        F.ret = t282;
                        goto gm_fret;
                    }
                    gm_b16_8 t283 = GM_LD(gm_b16_8, (((uint8_t *)&t281) + 8));
                    gm_l42 = t283;
                }
                {
                    void * t284 = gm_l0;
                    void * t285 = GM_LD(void *, (((uint8_t *)t284) + 0));
                    void * t286 = (void *)(((uint8_t *)t285) + 8);
                    void * t287 = gm_l22;
                    gm_t88rcucg1b7uk t288 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t287) + 0));
                    gm_b16_8 t289 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t286, t288);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l43 = t289;
                }
                {
                    void * t290 = gm_l0;
                    void * t291 = GM_LD(void *, (((uint8_t *)t290) + 0));
                    void * t292 = GM_LD(void *, (((uint8_t *)t291) + 0));
                    void * t293 = GM_LD(void *, (((uint8_t *)t292) + 0));
                    void * t294 = gm_l22;
                    gm_t9805qtonbsag t295 = gm_f_root_local_40frg7f(t293, t294);
                    gm_l44 = t295;
                }
                {
                    void * t296 = gm_l0;
                    void * t297 = gm_l23;
                    gm_t6j6smj7qoctu t298;
                    gm_b16_8 t299 = gm_l43;
                    GM_ST(gm_b16_8, (uint8_t *)&t298 + 0, t299);
                    gm_b16_8 t300 = gm_l42;
                    GM_ST(gm_b16_8, (uint8_t *)&t298 + 16, t300);
                    gm_tabb710d9mq2e t301;
                    gm_f_format_8lfevbr((void *)&t301, (void *)NULL, cx, NULL, &t298);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t301))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t301) + 8, gm_srcmok80ho8hh01, 1179, 41);
                    gm_b16_8 t302 = GM_LD(gm_b16_8, (((uint8_t *)&t301) + 8));
                    gm_b16_8 t303 = GM_LD(gm_b16_8, ((uint8_t *)&t302));
                    gm_t9805qtonbsag t304 = gm_l44;
                    gm_tnrltbc61i6v1 t305;
                    GM_ST(uint8_t, (uint8_t *)&t305 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t305 + 2, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t305 + 0), ((uint8_t)2ULL));
                    GM_ST(gm_t9805qtonbsag, ((uint8_t *)&t305 + 4), t304);
                    gm_tqgtd5gtgj65s t306;
                    gm_f_expr_into_dn9kavo((void *)&t306, (void *)NULL, cx, NULL, t296, t297, t303, t305);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t306))[0] != 0)) {
                        gm_tqgtd5gtgj65s t307;
                        GM_ST(uint8_t, (uint8_t *)&t307 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t307 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t307 + 4, 0);
                        ((uint8_t *)&t307)[0] = 1;
                        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t307 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t306) + 8)));
                        F.ret = t307;
                        goto gm_fret;
                    }
                    gm_b0_1 t308 = GM_LD(gm_b0_1, (((uint8_t *)&t306) + 8));
                }
                {
                    gm_b0_1 t309;
                    memset(&(*(gm_tqgtd5gtgj65s *)gm_out), 0, sizeof (*(gm_tqgtd5gtgj65s *)gm_out));
                    ((uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out))[0] = 0;
                    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 8, t309);
                    goto gm_fdone;
                }
                goto ix273;
ie273:;
ix273:;
            }
            {
                void * t311 = gm_l22;
                uint8_t t312 = GM_LD(uint8_t, ((((uint8_t *)t311) + 8) + 0));
                if (t312 != ((uint8_t)15ULL)) goto ie310;
                uint32_t t313 = GM_LD(uint32_t, ((((uint8_t *)t311) + 8) + 8));
                gm_l45 = t313;
                {
                    void * t316 = gm_l23;
                    gm_t88rcucg1b7uk t317 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t316) + 0));
                    void * t318 = gm_l22;
                    gm_t88rcucg1b7uk t319 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t318) + 0));
                    uint8_t t320 = (gm_eq9m6mvtmgde(((uint8_t *)&t317), ((uint8_t *)&t319)));
                    if (!t320) goto ie315;
                    uint32_t t321 = gm_l45;
                    uint64_t t322 = (uint64_t)t321;
                    uint64_t t323 = (uintptr_t)t322;
                    void * t324 = gm_l0;
                    void * t325 = GM_LD(void *, (((uint8_t *)t324) + 176));
                    uint64_t t326 = GM_LD(uintptr_t, (((uint8_t *)t325) + 48) + 8);
                    if (t323 >= (uintptr_t)(t326)) gm_trap_index(t323, t326, gm_srcmok80ho8hh01, 1184, 45);
                    gm_t88rcucg1b7uk t327 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, (((uint8_t *)t325) + 48)) + t323 * 40) + 16));
                    void * t328 = gm_l22;
                    gm_t88rcucg1b7uk t329 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t328) + 0));
                    uint8_t t330 = (gm_eq9m6mvtmgde(((uint8_t *)&t327), ((uint8_t *)&t329)));
                    if (!t330) goto ie315;
                    void * t331 = gm_l0;
                    uint32_t t332 = gm_l45;
                    uint8_t t333 = gm_f_is_borrowed_qi4apdt(t331, t332);
                    uint8_t t334 = (uint8_t)!t333;
                    if (!t334) goto ie315;
                    {
                        void * t336 = gm_l0;
                        void * t337 = gm_l23;
                        gm_t5o0tpv5jka2l t338;
                        gm_f_expr_porbc9t((void *)&t338, (void *)NULL, cx, NULL, t336, t337);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        if (((((uint8_t *)&t338))[0] != 0)) {
                            gm_tqgtd5gtgj65s t339;
                            GM_ST(uint8_t, (uint8_t *)&t339 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t339 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t339 + 4, 0);
                            ((uint8_t *)&t339)[0] = 1;
                            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t339 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t338) + 8)));
                            F.ret = t339;
                            goto gm_fret;
                        }
                        gm_b16_8 t340 = GM_LD(gm_b16_8, (((uint8_t *)&t338) + 8));
                        gm_l46 = t340;
                    }
                    {
                        void * t341 = gm_l0;
                        uint32_t t342 = gm_l45;
                        gm_b16_8 t343 = gm_f_local_lv_m2jtf0n((void *)NULL, cx, NULL, t341, t342);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l47 = t343;
                    }
                    {
                        void * t344 = gm_l0;
                        gm_t6j6smj7qoctu t345;
                        gm_b16_8 t346 = gm_l47;
                        GM_ST(gm_b16_8, (uint8_t *)&t345 + 0, t346);
                        gm_b16_8 t347 = gm_l46;
                        GM_ST(gm_b16_8, (uint8_t *)&t345 + 16, t347);
                        gm_b0_1 t348 = gm_f_linef_uu8i6ue((void *)NULL, cx, NULL, t344, &t345);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    {
                        gm_b0_1 t349;
                        memset(&(*(gm_tqgtd5gtgj65s *)gm_out), 0, sizeof (*(gm_tqgtd5gtgj65s *)gm_out));
                        ((uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out))[0] = 0;
                        GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 8, t349);
                        goto gm_fdone;
                    }
                    goto ix315;
ie315:;
ix315:;
                }
                goto ix310;
ie310:;
ix310:;
            }
            {
                void * t350 = gm_l0;
                void * t351 = gm_l22;
                void * t352 = gm_l23;
                gm_t5o0tpv5jka2l t353;
                gm_f_assign_addr_rlofoa5((void *)&t353, (void *)NULL, cx, NULL, t350, t351, t352);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t353))[0] != 0)) {
                    gm_tqgtd5gtgj65s t354;
                    GM_ST(uint8_t, (uint8_t *)&t354 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t354 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t354 + 4, 0);
                    ((uint8_t *)&t354)[0] = 1;
                    GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t354 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t353) + 8)));
                    F.ret = t354;
                    goto gm_fret;
                }
                gm_b16_8 t355 = GM_LD(gm_b16_8, (((uint8_t *)&t353) + 8));
                gm_l48 = t355;
            }
            {
                void * t356 = gm_l0;
                void * t357 = gm_l23;
                gm_t5o0tpv5jka2l t358;
                gm_f_expr_porbc9t((void *)&t358, (void *)NULL, cx, NULL, t356, t357);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t358))[0] != 0)) {
                    gm_tqgtd5gtgj65s t359;
                    GM_ST(uint8_t, (uint8_t *)&t359 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t359 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t359 + 4, 0);
                    ((uint8_t *)&t359)[0] = 1;
                    GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t359 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t358) + 8)));
                    F.ret = t359;
                    goto gm_fret;
                }
                gm_b16_8 t360 = GM_LD(gm_b16_8, (((uint8_t *)&t358) + 8));
                gm_l49 = t360;
            }
            {
                void * t361 = gm_l0;
                void * t362 = GM_LD(void *, (((uint8_t *)t361) + 0));
                void * t363 = (void *)(((uint8_t *)t362) + 8);
                void * t364 = gm_l22;
                gm_t88rcucg1b7uk t365 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t364) + 0));
                gm_b16_8 t366 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t363, t365);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l50 = t366;
            }
            {
                void * t367 = gm_l0;
                gm_tfeab279c16rr t368;
                gm_b16_8 t369 = gm_l50;
                GM_ST(gm_b16_8, (uint8_t *)&t368 + 0, t369);
                gm_b16_8 t370 = gm_l48;
                GM_ST(gm_b16_8, (uint8_t *)&t368 + 16, t370);
                gm_b16_8 t371 = gm_l49;
                GM_ST(gm_b16_8, (uint8_t *)&t368 + 32, t371);
                gm_b0_1 t372 = gm_f_linef_f1jrb6n((void *)NULL, cx, NULL, t367, &t368);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto me4;
mn144:;
ma8:;
            uint8_t t374 = GM_LD(uint8_t, (((uint8_t *)t3) + 0));
            if (t374 != ((uint8_t)3ULL)) goto mn373;
            gm_b16_8 t375 = GM_LD(gm_b16_8, ((((uint8_t *)t3) + 8) + 0));
            gm_l51 = t375;
            void * t376 = GM_LD(void *, ((((uint8_t *)t3) + 8) + 16));
            gm_l52 = t376;
            {
                gm_f_list_0c2v515((void *)&F.l53, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                {
                    gm_b16_8 t379 = gm_l51;
                    gm_l54 = t379;
                }
                {
                    gm_l55 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t381 = gm_l55;
                        uint64_t t382 = GM_LD(uintptr_t, ((uint8_t *)&gm_l54) + 8);
                        uint8_t t383 = (t381 < t382);
                        if (!t383) goto lb380;
                        {
                            uint64_t t386 = gm_l55;
                            uint64_t t387 = (uintptr_t)t386;
                            uint64_t t388 = GM_LD(uintptr_t, ((uint8_t *)&gm_l54) + 8);
                            void * t389 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l54)) + t387 * 8));
                            void * t392 = GM_LD(void *, ((uint8_t *)&t389));
                            gm_l56 = t392;
                            {
                                void * t395 = gm_l56;
                                if (!(GM_LD(void *, ((uint8_t *)&t395)) != 0)) goto ie394;
                                void * t396 = GM_LD(void *, ((uint8_t *)&t395));
                                gm_l57 = t396;
                                {
                                    void * t398 = gm_l0;
                                    void * t399 = gm_l57;
                                    void * t400 = gm_l52;
                                    gm_t5o0tpv5jka2l t401;
                                    gm_f_assign_addr_rlofoa5((void *)&t401, (void *)NULL, cx, NULL, t398, t399, t400);
                                    if (gm_unw.kind) {
                                        goto gm_ll384;
                                    }
                                    if (((((uint8_t *)&t401))[0] != 0)) {
                                        gm_tqgtd5gtgj65s t402;
                                        GM_ST(uint8_t, (uint8_t *)&t402 + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t402 + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t402 + 4, 0);
                                        ((uint8_t *)&t402)[0] = 1;
                                        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t402 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t401) + 8)));
                                        F.ret = t402;
                                        goto gm_fret;
                                    }
                                    gm_b16_8 t403 = GM_LD(gm_b16_8, (((uint8_t *)&t401) + 8));
                                    gm_l58 = t403;
                                }
                                {
                                    void * t404 = (void *)((uint8_t *)&F.l53);
                                    gm_b16_8 t405 = gm_l58;
                                    gm_th2lqbo77pvsc t406;
                                    GM_ST(uint8_t, (uint8_t *)&t406 + 17, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t406 + 18, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t406 + 20, 0);
                                    GM_ST(gm_b16_8, ((uint8_t *)&t406), t405);
                                    ((uint8_t *)&t406)[16] = 1;
                                    gm_tftb1odc739t5 t407 = gm_f_push_d0271rf(t404, &t406);
                                    if (((((uint8_t *)&t407))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t407) + 1, gm_srcmok80ho8hh01, 1201, 21);
                                    gm_b0_1 t408 = GM_LD(gm_b0_1, (((uint8_t *)&t407) + 1));
                                }
                                goto ix394;
ie394:;
                                {
                                    void * t410 = (void *)((uint8_t *)&F.l53);
                                    gm_th2lqbo77pvsc t411;
                                    memset(&t411, 0, sizeof t411);
                                    gm_tftb1odc739t5 t412 = gm_f_push_d0271rf(t410, &t411);
                                    if (((((uint8_t *)&t412))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t412) + 1, gm_srcmok80ho8hh01, 1203, 21);
                                    gm_b0_1 t413 = GM_LD(gm_b0_1, (((uint8_t *)&t412) + 1));
                                }
ix394:;
                            }
                            goto me390;
mn391:;
                            gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 1198, 13);
me390:;
                        }
lc380:;
                        {
                            uint64_t t415 = gm_l55;
                            uint64_t r416; bool o417 = __builtin_add_overflow(t415, ((uint64_t)1ULL), &r416);
                            uint64_t t418 = r416;
                            gm_l55 = t418;
                        }
                    }
gm_ll384:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb380; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc380; }
                    }
                    goto gm_lad;
lb380:;
                }
            }
            {
                void * t419 = gm_l0;
                void * t420 = gm_l52;
                gm_t5o0tpv5jka2l t421;
                gm_f_expr_porbc9t((void *)&t421, (void *)NULL, cx, NULL, t419, t420);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t421))[0] != 0)) {
                    gm_tqgtd5gtgj65s t422;
                    GM_ST(uint8_t, (uint8_t *)&t422 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t422 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t422 + 4, 0);
                    ((uint8_t *)&t422)[0] = 1;
                    GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t422 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t421) + 8)));
                    F.ret = t422;
                    goto gm_fret;
                }
                gm_b16_8 t423 = GM_LD(gm_b16_8, (((uint8_t *)&t421) + 8));
                gm_l59 = t423;
            }
            {
                void * t424 = gm_l0;
                void * t425 = gm_l52;
                gm_t88rcucg1b7uk t426 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t425) + 0));
                gm_b16_8 t427 = gm_l59;
                gm_b16_8 t428 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t424, t426, t427);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l60 = t428;
            }
            {
                void * t429 = gm_l0;
                void * t430 = GM_LD(void *, (((uint8_t *)t429) + 0));
                void * t431 = (void *)(((uint8_t *)t430) + 8);
                void * t432 = gm_l52;
                gm_t88rcucg1b7uk t433 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t432) + 0));
                gm_b16_8 t434 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t431, t433);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l61 = t434;
            }
            {
                {
                    gm_l63 = ((uint64_t)0ULL);
                }
                {
                    uint64_t t436 = GM_LD(uintptr_t, (((uint8_t *)&F.l53) + 0) + 8);
                    gm_l62 = t436;
                }
                {
                    for (;;) {
                        uint64_t t438 = gm_l63;
                        uint64_t t439 = gm_l62;
                        uint8_t t440 = (t438 < t439);
                        if (!t440) goto lb437;
                        {
                            uint64_t t443 = gm_l63;
                            uint64_t t446 = GM_LD(uint64_t, ((uint8_t *)&t443));
                            gm_l64 = t446;
                            {
                                uint64_t t449 = gm_l64;
                                uint64_t t450 = (uintptr_t)t449;
                                uint64_t t451 = GM_LD(uintptr_t, (((uint8_t *)&F.l53) + 0) + 8);
                                if (t450 >= (uintptr_t)(t451)) gm_trap_index(t450, t451, gm_srcmok80ho8hh01, 1210, 25);
                                if (!((((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l53) + 0)) + t450 * 24))[16] != 0)) goto ie448;
                                gm_b16_8 t452 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l53) + 0)) + t450 * 24));
                                gm_l65 = t452;
                                {
                                    uint64_t t455 = gm_l64;
                                    uint64_t t456 = (uintptr_t)t455;
                                    uint64_t t457 = GM_LD(uintptr_t, ((uint8_t *)&gm_l51) + 8);
                                    if (t456 >= (uintptr_t)(t457)) gm_trap_index(t456, t457, gm_srcmok80ho8hh01, 1211, 30);
                                    void * t458 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l51)) + t456 * 8));
                                    if (!(GM_LD(void *, ((uint8_t *)&t458)) != 0)) goto ie454;
                                    void * t459 = GM_LD(void *, ((uint8_t *)&t458));
                                    gm_l66 = t459;
                                    {
                                        void * t461 = gm_l0;
                                        void * t462 = GM_LD(void *, (((uint8_t *)t461) + 0));
                                        void * t463 = (void *)(((uint8_t *)t462) + 8);
                                        void * t464 = gm_l66;
                                        gm_t88rcucg1b7uk t465 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t464) + 0));
                                        gm_b16_8 t466 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t463, t465);
                                        if (gm_unw.kind) {
                                            goto gm_ll441;
                                        }
                                        gm_l67 = t466;
                                    }
                                    {
                                        void * t467 = gm_l0;
                                        gm_t0j6181meiu4s t468;
                                        gm_b16_8 t469 = gm_l67;
                                        GM_ST(gm_b16_8, (uint8_t *)&t468 + 0, t469);
                                        gm_b16_8 t470 = gm_l65;
                                        GM_ST(gm_b16_8, (uint8_t *)&t468 + 16, t470);
                                        gm_b16_8 t471 = gm_l67;
                                        GM_ST(gm_b16_8, (uint8_t *)&t468 + 32, t471);
                                        gm_b16_8 t472 = gm_l60;
                                        GM_ST(gm_b16_8, (uint8_t *)&t468 + 48, t472);
                                        uint64_t t473 = gm_l64;
                                        uint64_t t474 = (uintptr_t)t473;
                                        uint64_t t475 = GM_LD(uintptr_t, ((uint8_t *)&gm_l61) + 8);
                                        if (t474 >= (uintptr_t)(t475)) gm_trap_index(t474, t475, gm_srcmok80ho8hh01, 1213, 96);
                                        uint64_t t476 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l61)) + t474 * 8));
                                        GM_ST(uint64_t, (uint8_t *)&t468 + 64, t476);
                                        gm_b0_1 t477 = gm_f_linef_5n29svs((void *)NULL, cx, NULL, t467, &t468);
                                        if (gm_unw.kind) {
                                            goto gm_ll441;
                                        }
                                    }
                                    goto ix454;
ie454:;
ix454:;
                                }
                                goto ix448;
ie448:;
ix448:;
                            }
                            goto me444;
mn445:;
                            gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 1209, 13);
me444:;
                        }
lc437:;
                        {
                            uint64_t t479 = gm_l63;
                            uint64_t r480; bool o481 = __builtin_add_overflow(t479, ((uint64_t)1ULL), &r480);
                            uint64_t t482 = r480;
                            gm_l63 = t482;
                        }
                    }
gm_ll441:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 3) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb437; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc437; }
                    }
                    goto gm_lad;
lb437:;
                }
            }
            goto me4;
mn373:;
ma9:;
            uint8_t t484 = GM_LD(uint8_t, (((uint8_t *)t3) + 0));
            if (t484 != ((uint8_t)4ULL)) goto mn483;
            void * t485 = GM_LD(void *, (((uint8_t *)t3) + 8));
            gm_l68 = t485;
            {
                {
                    void * t488 = gm_l0;
                    void * t489 = gm_l68;
                    gm_t5o0tpv5jka2l t490;
                    gm_f_expr_porbc9t((void *)&t490, (void *)NULL, cx, NULL, t488, t489);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t490))[0] != 0)) {
                        gm_tqgtd5gtgj65s t491;
                        GM_ST(uint8_t, (uint8_t *)&t491 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t491 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t491 + 4, 0);
                        ((uint8_t *)&t491)[0] = 1;
                        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t491 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t490) + 8)));
                        F.ret = t491;
                        goto gm_fret;
                    }
                    gm_b16_8 t492 = GM_LD(gm_b16_8, (((uint8_t *)&t490) + 8));
                }
            }
            goto me4;
mn483:;
ma10:;
            uint8_t t494 = GM_LD(uint8_t, (((uint8_t *)t3) + 0));
            if (t494 != ((uint8_t)5ULL)) goto mn493;
            uint8_t t495 = GM_LD(uint8_t, ((((uint8_t *)t3) + 8) + 0));
            gm_l69 = t495;
            void * t496 = GM_LD(void *, ((((uint8_t *)t3) + 8) + 8));
            gm_l70 = t496;
            {
                void * t498 = gm_l0;
                gm_trqfvmjtt4sij t499 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)t498) + 184));
                F.l71 = t499;
            }
            {
                void * t500 = gm_l0;
                uint8_t *t501 = (((uint8_t *)t500) + 184);
                gm_f_list_uvfu1k1((void *)&(*(gm_trqfvmjtt4sij *)t501), (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t502 = gm_l0;
                uint8_t t503 = GM_LD(uint8_t, (((uint8_t *)t502) + 364));
                gm_l72 = t503;
            }
            {
                void * t504 = gm_l0;
                GM_ST(uint8_t, (((uint8_t *)t504) + 364), ((uint8_t)0));
            }
            {
                void * t505 = gm_l0;
                gm_f_set_aside_scopes_ho12if8((void *)&F.l73, (void *)NULL, cx, NULL, t505);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t506 = gm_l0;
                void * t507 = gm_l70;
                gm_f_expr_porbc9t((void *)&F.l74, (void *)NULL, cx, NULL, t506, t507);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t508 = gm_l0;
                gm_f_restore_scopes_f62992p(t508, &(*(gm_td7pkamr64l0c *)((uint8_t *)&F.l73)));
            }
            {
                void * t509 = gm_l0;
                uint8_t t510 = gm_l72;
                GM_ST(uint8_t, (((uint8_t *)t509) + 364), t510);
            }
            {
                void * t511 = gm_l0;
                gm_trqfvmjtt4sij t512 = F.l71;
                GM_ST(gm_trqfvmjtt4sij, (((uint8_t *)t511) + 184), t512);
            }
            {
                {
                    gm_t5o0tpv5jka2l t514 = F.l74;
                    if (((((uint8_t *)&t514))[0] != 0)) {
                        gm_tqgtd5gtgj65s t515;
                        GM_ST(uint8_t, (uint8_t *)&t515 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t515 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t515 + 4, 0);
                        ((uint8_t *)&t515)[0] = 1;
                        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t515 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t514) + 8)));
                        F.ret = t515;
                        goto gm_fret;
                    }
                    gm_b16_8 t516 = GM_LD(gm_b16_8, (((uint8_t *)&t514) + 8));
                }
            }
            {
                void * t518 = gm_l0;
                uint64_t t519 = GM_LD(uintptr_t, ((((uint8_t *)t518) + 240) + 0) + 8);
                uint8_t t520 = (t519 > ((uint64_t)0ULL));
                if (!t520) goto ie517;
                {
                    void * t522 = gm_l0;
                    uint64_t t523 = GM_LD(uintptr_t, ((((uint8_t *)t522) + 240) + 0) + 8);
                    uint64_t r524; bool o525 = __builtin_sub_overflow(t523, ((uint64_t)1ULL), &r524);
                    if (o525) gm_trap("integer overflow in `-`", gm_srcmok80ho8hh01, 1234, 41);
                    uint64_t t526 = r524;
                    uint64_t t527 = (uintptr_t)t526;
                    void * t528 = gm_l0;
                    uint64_t t529 = GM_LD(uintptr_t, ((((uint8_t *)t528) + 240) + 0) + 8);
                    if (t527 >= (uintptr_t)(t529)) gm_trap_index(t527, t529, gm_srcmok80ho8hh01, 1234, 25);
                    void * t530 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t528) + 240) + 0)) + t527 * 104);
                    gm_l75 = t530;
                }
                {
                    void * t532 = gm_l75;
                    uint8_t t533 = ((GM_LD(uint8_t, (((uint8_t *)t532) + 0) + 0) == ((uint8_t)0ULL)));
                    if (!t533) goto ie531;
                    {
                        void * t535 = gm_l75;
                        void * t536 = (void *)(((uint8_t *)t535) + 56);
                        uint8_t t537 = gm_l69;
                        void * t538 = gm_l70;
                        gm_tgtub0v94d5u5 t539;
                        GM_ST(uint8_t, (uint8_t *)&t539 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t539 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t539 + 4, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t539 + 0), t537);
                        GM_ST(void *, ((uint8_t *)&t539 + 8), t538);
                        gm_tftb1odc739t5 t540 = gm_f_push_2lk6rs2(t536, GM_CONV(gm_csn5gfg3c8ooaj, t539));
                        if (((((uint8_t *)&t540))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t540) + 1, gm_srcmok80ho8hh01, 1235, 41);
                        gm_b0_1 t541 = GM_LD(gm_b0_1, (((uint8_t *)&t540) + 1));
                    }
                    goto ix531;
ie531:;
ix531:;
                }
                goto ix517;
ie517:;
ix517:;
            }
            goto me4;
mn493:;
ma11:;
            gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 1110, 5);
me4:;
        }
    }
    gm_b0_1 t542;
    memset(&(*(gm_tqgtd5gtgj65s *)gm_out), 0, sizeof (*(gm_tqgtd5gtgj65s *)gm_out));
    ((uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out))[0] = 0;
    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 8, t542);
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
inline __attribute__((aligned(4))) void gm_f_stmt_block_5vkbn0n(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_stmt_block_5vkbn0n F;
    void * gm_l0; /* fg */
    void * gm_l1; /* s */
    gm_tl874t2atnlh3 gm_l2; /* span */
    GM_STACK_CHECK(&F, 212u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l1;
        gm_toceffb697p0u t4 = gm_f_stmt_span_d0pel0p(t3);
        if (!((((uint8_t *)&t4))[12] != 0)) goto ie2;
        gm_tl874t2atnlh3 t5 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)&t4));
        gm_l2 = t5;
        {
            void * t7 = gm_l0;
            gm_tl874t2atnlh3 t8 = gm_l2;
            gm_b0_1 t9 = gm_f_src_line_d24sajl((void *)NULL, cx, NULL, t7, t8);
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
        gm_b16_8 t11;
        GM_ST(void *, (uint8_t *)&t11, (void *)(gm_sh40na9qbga3l));
        GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(1));
        gm_b0_1 t12 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t10, t11);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t13 = gm_l0;
        void * t14 = gm_l0;
        uint64_t t15 = GM_LD(uint64_t, (((uint8_t *)t14) + 224));
        uint64_t r16; bool o17 = __builtin_add_overflow(t15, ((uint64_t)1ULL), &r16);
        if (o17) gm_trap("integer overflow in `+`", gm_srcmok80ho8hh01, 1102, 5);
        uint64_t t18 = r16;
        GM_ST(uint64_t, (((uint8_t *)t13) + 224), t18);
    }
    {
        void * t19 = gm_l0;
        void * t20 = gm_l1;
        gm_f_stmt_1t29bfr((void *)&F.l3, (void *)NULL, cx, NULL, t19, t20);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t21 = gm_l0;
        void * t22 = gm_l0;
        uint64_t t23 = GM_LD(uint64_t, (((uint8_t *)t22) + 224));
        uint64_t r24; bool o25 = __builtin_sub_overflow(t23, ((uint64_t)1ULL), &r24);
        if (o25) gm_trap("integer overflow in `-`", gm_srcmok80ho8hh01, 1104, 5);
        uint64_t t26 = r24;
        GM_ST(uint64_t, (((uint8_t *)t21) + 224), t26);
    }
    {
        void * t27 = gm_l0;
        gm_b16_8 t28;
        GM_ST(void *, (uint8_t *)&t28, (void *)(gm_s2cuflhqkkuaq));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(1));
        gm_b0_1 t29 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t27, t28);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        memmove(gm_out, ((uint8_t *)&F.l3), 40);
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
__attribute__((aligned(4))) gm_toceffb697p0u gm_f_stmt_span_d0pel0p(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_stmt_span_d0pel0p F;
    void * gm_l0; /* s */
    void * gm_l1; /* e */
    void * gm_l2; /* e */
    void * gm_l3; /* e */
    void * gm_l4; /* place */
    gm_b16_8 gm_l5; /* places */
    void * gm_l6; /* value */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    void * gm_l9; /* p */
    void * gm_l10; /* x */
    GM_STACK_CHECK_SHALLOW(&F, 494u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        switch (t10) { case ((uint8_t)0ULL): goto ma4; case ((uint8_t)1ULL): goto ma5; case ((uint8_t)2ULL): goto ma6; case ((uint8_t)3ULL): goto ma7; case ((uint8_t)4ULL): goto ma5; case ((uint8_t)5ULL): goto ma8; default: goto ma8; }
ma4:;
        uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t12 != ((uint8_t)0ULL)) goto mn11;
        if ((GM_LD(void *, ((((uint8_t *)t2) + 8) + 8)) != 0)) goto mn11;
        gm_toceffb697p0u t13;
        memset(&t13, 0, sizeof t13);
        uint8_t t14 = 0;
        F.ret = t13;
        goto gm_fret;
        goto me3;
mn11:;
ma5:;
        uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t18 != ((uint8_t)0ULL)) goto pa17;
        if (!(GM_LD(void *, ((((uint8_t *)t2) + 8) + 8)) != 0)) goto pa17;
        void * t19 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l1 = t19;
        goto po16;
pa17:;
        uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t21 != ((uint8_t)1ULL)) goto pa20;
        void * t22 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 16));
        gm_l1 = t22;
        goto po16;
pa20:;
        uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t24 != ((uint8_t)4ULL)) goto pa23;
        void * t25 = GM_LD(void *, (((uint8_t *)t2) + 8));
        gm_l1 = t25;
        goto po16;
pa23:;
        goto mn15;
po16:;
        void * t26 = gm_l1;
        gm_tl874t2atnlh3 t27 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t26) + 72));
        gm_toceffb697p0u t28;
        GM_ST(uint8_t, (uint8_t *)&t28 + 13, 0);
        GM_ST(uint16_t, (uint8_t *)&t28 + 14, 0);
        GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t28), t27);
        ((uint8_t *)&t28)[12] = 1;
        uint8_t t29 = 0;
        F.ret = t28;
        goto gm_fret;
        goto me3;
mn15:;
ma6:;
        uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t31 != ((uint8_t)2ULL)) goto mn30;
        void * t32 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 0));
        gm_l4 = t32;
        void * t33 = gm_l4;
        gm_tl874t2atnlh3 t34 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t33) + 72));
        gm_toceffb697p0u t35;
        GM_ST(uint8_t, (uint8_t *)&t35 + 13, 0);
        GM_ST(uint16_t, (uint8_t *)&t35 + 14, 0);
        GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t35), t34);
        ((uint8_t *)&t35)[12] = 1;
        uint8_t t36 = 0;
        F.ret = t35;
        goto gm_fret;
        goto me3;
mn30:;
ma7:;
        uint8_t t38 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t38 != ((uint8_t)3ULL)) goto mn37;
        gm_b16_8 t39 = GM_LD(gm_b16_8, ((((uint8_t *)t2) + 8) + 0));
        gm_l5 = t39;
        void * t40 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 16));
        gm_l6 = t40;
        {
            {
                gm_b16_8 t43 = gm_l5;
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
                        uint64_t t50 = gm_l8;
                        uint64_t t51 = (uintptr_t)t50;
                        uint64_t t52 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                        void * t53 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t51 * 8));
                        void * t56 = GM_LD(void *, ((uint8_t *)&t53));
                        gm_l9 = t56;
                        {
                            void * t59 = gm_l9;
                            if (!(GM_LD(void *, ((uint8_t *)&t59)) != 0)) goto ie58;
                            void * t60 = GM_LD(void *, ((uint8_t *)&t59));
                            gm_l10 = t60;
                            {
                                void * t62 = gm_l10;
                                gm_tl874t2atnlh3 t63 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t62) + 72));
                                gm_toceffb697p0u t64;
                                GM_ST(uint8_t, (uint8_t *)&t64 + 13, 0);
                                GM_ST(uint16_t, (uint8_t *)&t64 + 14, 0);
                                GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t64), t63);
                                ((uint8_t *)&t64)[12] = 1;
                                uint8_t t65 = 0;
                                F.ret = t64;
                                goto gm_fret;
                            }
                            goto ix58;
ie58:;
ix58:;
                        }
                        goto me54;
mn55:;
                        gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 421, 13);
me54:;
                    }
lc44:;
                    {
                        uint64_t t67 = gm_l8;
                        uint64_t r68; bool o69 = __builtin_add_overflow(t67, ((uint64_t)1ULL), &r68);
                        uint64_t t70 = r68;
                        gm_l8 = t70;
                    }
                }
lb44:;
            }
        }
        {
            void * t71 = gm_l6;
            gm_tl874t2atnlh3 t72 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t71) + 72));
            gm_toceffb697p0u t73;
            GM_ST(uint8_t, (uint8_t *)&t73 + 13, 0);
            GM_ST(uint16_t, (uint8_t *)&t73 + 14, 0);
            GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t73), t72);
            ((uint8_t *)&t73)[12] = 1;
            uint8_t t74 = 0;
            F.ret = t73;
            goto gm_fret;
        }
        goto me3;
mn37:;
ma8:;
        gm_toceffb697p0u t76;
        memset(&t76, 0, sizeof t76);
        uint8_t t77 = 0;
        F.ret = t76;
        goto gm_fret;
        goto me3;
mn75:;
ma9:;
        gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 416, 5);
me3:;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) void gm_f_stmts_1199hll(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_tagged_into_ncahfuo(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, uint64_t a3, uint64_t a4, void * a5, uint64_t a6, gm_b16_8 a7, gm_tnrltbc61i6v1 a8) {
    struct gm_fr_tagged_into_ncahfuo F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* tag */
    uint64_t gm_l3; /* tag_off */
    uint64_t gm_l4; /* tag_len */
    void * gm_l5; /* x */
    uint64_t gm_l6; /* pay_off */
    gm_b16_8 gm_l7; /* dest */
    gm_tnrltbc61i6v1 gm_l8; /* obs */
    gm_t2lqu8jsj511d gm_l9; /* tag_part */
    gm_tomkd2bemeags gm_l10; /* <temporary array> */
    void * gm_l11; /* px */
    gm_te3hi449i91a8 gm_l12; /* parts */
    gm_b16_8 gm_l13; /* c */
    gm_b16_8 gm_l14; /* a */
    gm_b16_8 gm_l15; /* p */
    gm_b16_8 gm_l16; /* v */
    GM_STACK_CHECK(&F, 1756u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    gm_l6 = a6;
    gm_l7 = a7;
    gm_l8 = a8;
    {
        uint64_t t2 = gm_l3;
        uint64_t t3 = gm_l4;
        gm_t2lqu8jsj511d t4;
        GM_ST(uint64_t, ((uint8_t *)&t4 + 0), t2);
        GM_ST(uint64_t, ((uint8_t *)&t4 + 8), t3);
        gm_l9 = t4;
    }
    {
        void * t5 = gm_l5;
        void * t6;
        if ((GM_LD(void *, ((uint8_t *)&t5)) != 0)) {
            void * t7 = GM_LD(void *, (((uint8_t *)&t5) + 0));
            t6 = t7;
        } else {
            {
            }
            {
                void * t9 = gm_l0;
                gm_t88rcucg1b7uk t10 = gm_l1;
                gm_b16_8 t11 = gm_l7;
                {
                    gm_t2lqu8jsj511d t13 = gm_l9;
                    gm_tomkd2bemeags t14;
                    GM_ST(gm_t2lqu8jsj511d, ((uint8_t *)&t14 + 0), t13);
                    gm_l10 = t14;
                }
                void * t15 = ((uint8_t *)&gm_l10);
                if ((uintptr_t)0 > (uintptr_t)1 || (uintptr_t)1 > (uintptr_t)1) gm_trap_slice((uintptr_t)0, (uintptr_t)1, 1, gm_srcmok80ho8hh01, 964, 35);
                gm_b16_8 t16;
                GM_ST(void *, (uint8_t *)&t16, (void *)(((uint8_t *)t15 + (uintptr_t)0 * 16)));
                GM_ST(uintptr_t, (uint8_t *)&t16 + 8, (uintptr_t)((uintptr_t)1 - (uintptr_t)0));
                gm_b16_8 t17 = t16;
                gm_b0_1 t18 = gm_f_clear_outside_q01mr8i((void *)NULL, cx, NULL, t9, t10, t11, t17);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t19 = gm_l0;
                gm_b16_8 t20 = gm_l2;
                gm_b0_1 t21 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t19, t20);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_b0_1 t22;
            memset(&(*(gm_tqgtd5gtgj65s *)gm_out), 0, sizeof (*(gm_tqgtd5gtgj65s *)gm_out));
            ((uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out))[0] = 0;
            GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 8, t22);
            goto gm_fdone;
        }
        gm_l11 = t6;
    }
    {
        uint64_t t23 = gm_l6;
        void * t24 = gm_l0;
        void * t25 = GM_LD(void *, (((uint8_t *)t24) + 0));
        void * t26 = (void *)(((uint8_t *)t25) + 8);
        void * t27 = gm_l11;
        gm_t88rcucg1b7uk t28 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t27) + 0));
        uint64_t t29 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t26, t28);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t2lqu8jsj511d t30;
        GM_ST(uint64_t, ((uint8_t *)&t30 + 0), t23);
        GM_ST(uint64_t, ((uint8_t *)&t30 + 8), t29);
        GM_ST(gm_t2lqu8jsj511d, (uint8_t *)&gm_l12 + 0, t30);
        gm_t2lqu8jsj511d t31 = gm_l9;
        GM_ST(gm_t2lqu8jsj511d, (uint8_t *)&gm_l12 + 16, t31);
    }
    {
        void * t32 = gm_l0;
        void * t33 = GM_LD(void *, (((uint8_t *)t32) + 0));
        void * t34 = (void *)(((uint8_t *)t33) + 8);
        void * t35 = gm_l11;
        gm_t88rcucg1b7uk t36 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t35) + 0));
        gm_b16_8 t37 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t34, t36);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l13 = t37;
    }
    {
        uint8_t t39 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l8) + 0) == ((uint8_t)0ULL)));
        if (!t39) goto ie38;
        void * t40 = gm_l0;
        void * t41 = gm_l11;
        gm_tnrltbc61i6v1 t42 = gm_l8;
        uint8_t t43 = gm_f_nests_in_place_jc41ehn((void *)NULL, cx, NULL, t40, t41, t42);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t43) goto ie38;
        {
            void * t45 = gm_l0;
            gm_t88rcucg1b7uk t46 = gm_l1;
            gm_b16_8 t47 = gm_l7;
            void * t48 = ((uint8_t *)&gm_l12);
            if ((uintptr_t)0 > (uintptr_t)2 || (uintptr_t)2 > (uintptr_t)2) gm_trap_slice((uintptr_t)0, (uintptr_t)2, 2, gm_srcmok80ho8hh01, 971, 35);
            gm_b16_8 t49;
            GM_ST(void *, (uint8_t *)&t49, (void *)(((uint8_t *)t48 + (uintptr_t)0 * 16)));
            GM_ST(uintptr_t, (uint8_t *)&t49 + 8, (uintptr_t)((uintptr_t)2 - (uintptr_t)0));
            gm_b16_8 t50 = GM_LD(gm_b16_8, ((uint8_t *)&t49));
            gm_b0_1 t51 = gm_f_clear_outside_q01mr8i((void *)NULL, cx, NULL, t45, t46, t47, t50);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t52 = gm_l0;
            gm_b16_8 t53 = gm_l2;
            gm_b0_1 t54 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t52, t53);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t55 = gm_l0;
            void * t56 = gm_l11;
            gm_tftksr4op114e t57;
            gm_b16_8 t58 = gm_l13;
            GM_ST(gm_b16_8, (uint8_t *)&t57 + 0, t58);
            gm_b16_8 t59 = gm_l7;
            GM_ST(gm_b16_8, (uint8_t *)&t57 + 16, t59);
            uint64_t t60 = gm_l6;
            GM_ST(uint64_t, (uint8_t *)&t57 + 32, t60);
            gm_tabb710d9mq2e t61;
            gm_f_format_e1d93qv((void *)&t61, (void *)NULL, cx, NULL, &t57);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t61))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t61) + 8, gm_srcmok80ho8hh01, 973, 30);
            gm_b16_8 t62 = GM_LD(gm_b16_8, (((uint8_t *)&t61) + 8));
            gm_b16_8 t63 = GM_LD(gm_b16_8, ((uint8_t *)&t62));
            gm_tnrltbc61i6v1 t64 = gm_l8;
            gm_tqgtd5gtgj65s t65;
            gm_f_expr_into_dn9kavo((void *)&t65, (void *)NULL, cx, NULL, t55, t56, t63, t64);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t65))[0] != 0)) {
                gm_tqgtd5gtgj65s t66;
                GM_ST(uint8_t, (uint8_t *)&t66 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t66 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t66 + 4, 0);
                ((uint8_t *)&t66)[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t66 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t65) + 8)));
                F.ret = t66;
                goto gm_fret;
            }
            gm_b0_1 t67 = GM_LD(gm_b0_1, (((uint8_t *)&t65) + 8));
        }
        {
            gm_b0_1 t68;
            memset(&(*(gm_tqgtd5gtgj65s *)gm_out), 0, sizeof (*(gm_tqgtd5gtgj65s *)gm_out));
            ((uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out))[0] = 0;
            GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 8, t68);
            goto gm_fdone;
        }
        goto ix38;
ie38:;
ix38:;
    }
    {
        void * t70 = gm_l0;
        void * t71 = GM_LD(void *, (((uint8_t *)t70) + 0));
        void * t72 = (void *)(((uint8_t *)t71) + 8);
        void * t73 = gm_l11;
        gm_t88rcucg1b7uk t74 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t73) + 0));
        uint8_t t75 = gm_f_by_ref_fi0o860((void *)NULL, cx, NULL, t72, t74);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t75) goto ie69;
        void * t76 = gm_l0;
        void * t77 = gm_l11;
        uint8_t t78 = gm_f_reads_place_nji55rm((void *)NULL, cx, NULL, t76, t77);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t78) goto ie69;
        {
            void * t80 = gm_l0;
            void * t81 = gm_l11;
            gm_t5o0tpv5jka2l t82;
            gm_f_place_addr_7ji15ot((void *)&t82, (void *)NULL, cx, NULL, t80, t81);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t82))[0] != 0)) {
                gm_tqgtd5gtgj65s t83;
                GM_ST(uint8_t, (uint8_t *)&t83 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t83 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t83 + 4, 0);
                ((uint8_t *)&t83)[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t83 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t82) + 8)));
                F.ret = t83;
                goto gm_fret;
            }
            gm_b16_8 t84 = GM_LD(gm_b16_8, (((uint8_t *)&t82) + 8));
            gm_l14 = t84;
        }
        {
            void * t85 = gm_l0;
            void * t86 = gm_l0;
            uint64_t t87 = GM_LD(uint64_t, (((uint8_t *)t86) + 304));
            uint64_t r88; bool o89 = __builtin_add_overflow(t87, ((uint64_t)8ULL), &r88);
            if (o89) gm_trap("integer overflow in `+`", gm_srcmok80ho8hh01, 979, 9);
            uint64_t t90 = r88;
            GM_ST(uint64_t, (((uint8_t *)t85) + 304), t90);
        }
        {
            void * t91 = gm_l0;
            gm_b16_8 t92 = gm_f_fresh_tmp_6tadl69((void *)NULL, cx, NULL, t91);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l15 = t92;
        }
        {
            void * t93 = gm_l0;
            gm_t6j6smj7qoctu t94;
            gm_b16_8 t95 = gm_l15;
            GM_ST(gm_b16_8, (uint8_t *)&t94 + 0, t95);
            gm_b16_8 t96 = gm_l14;
            GM_ST(gm_b16_8, (uint8_t *)&t94 + 16, t96);
            gm_b0_1 t97 = gm_f_linef_kta085d((void *)NULL, cx, NULL, t93, &t94);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t98 = gm_l0;
            gm_t88rcucg1b7uk t99 = gm_l1;
            gm_b16_8 t100 = gm_l7;
            void * t101 = ((uint8_t *)&gm_l12);
            if ((uintptr_t)0 > (uintptr_t)2 || (uintptr_t)2 > (uintptr_t)2) gm_trap_slice((uintptr_t)0, (uintptr_t)2, 2, gm_srcmok80ho8hh01, 982, 35);
            gm_b16_8 t102;
            GM_ST(void *, (uint8_t *)&t102, (void *)(((uint8_t *)t101 + (uintptr_t)0 * 16)));
            GM_ST(uintptr_t, (uint8_t *)&t102 + 8, (uintptr_t)((uintptr_t)2 - (uintptr_t)0));
            gm_b16_8 t103 = GM_LD(gm_b16_8, ((uint8_t *)&t102));
            gm_b0_1 t104 = gm_f_clear_outside_q01mr8i((void *)NULL, cx, NULL, t98, t99, t100, t103);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t105 = gm_l0;
            gm_b16_8 t106 = gm_l2;
            gm_b0_1 t107 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t105, t106);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t108 = gm_l0;
            gm_thddbqb6snabm t109;
            gm_b16_8 t110 = gm_l7;
            GM_ST(gm_b16_8, (uint8_t *)&t109 + 0, t110);
            uint64_t t111 = gm_l6;
            GM_ST(uint64_t, (uint8_t *)&t109 + 16, t111);
            gm_b16_8 t112 = gm_l15;
            GM_ST(gm_b16_8, (uint8_t *)&t109 + 24, t112);
            void * t113 = gm_l0;
            void * t114 = GM_LD(void *, (((uint8_t *)t113) + 0));
            void * t115 = (void *)(((uint8_t *)t114) + 8);
            void * t116 = gm_l11;
            gm_t88rcucg1b7uk t117 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t116) + 0));
            uint64_t t118 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t115, t117);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint64_t, (uint8_t *)&t109 + 40, t118);
            gm_b0_1 t119 = gm_f_linef_ligf694((void *)NULL, cx, NULL, t108, &t109);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_b0_1 t120;
            memset(&(*(gm_tqgtd5gtgj65s *)gm_out), 0, sizeof (*(gm_tqgtd5gtgj65s *)gm_out));
            ((uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out))[0] = 0;
            GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 8, t120);
            goto gm_fdone;
        }
        goto ix69;
ie69:;
ix69:;
    }
    {
        void * t121 = gm_l0;
        void * t122 = gm_l11;
        gm_t5o0tpv5jka2l t123;
        gm_f_expr_porbc9t((void *)&t123, (void *)NULL, cx, NULL, t121, t122);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t123))[0] != 0)) {
            gm_tqgtd5gtgj65s t124;
            GM_ST(uint8_t, (uint8_t *)&t124 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t124 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t124 + 4, 0);
            ((uint8_t *)&t124)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t124 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t123) + 8)));
            F.ret = t124;
            goto gm_fret;
        }
        gm_b16_8 t125 = GM_LD(gm_b16_8, (((uint8_t *)&t123) + 8));
        gm_l16 = t125;
    }
    {
        void * t126 = gm_l0;
        gm_t88rcucg1b7uk t127 = gm_l1;
        gm_b16_8 t128 = gm_l7;
        void * t129 = ((uint8_t *)&gm_l12);
        if ((uintptr_t)0 > (uintptr_t)2 || (uintptr_t)2 > (uintptr_t)2) gm_trap_slice((uintptr_t)0, (uintptr_t)2, 2, gm_srcmok80ho8hh01, 988, 31);
        gm_b16_8 t130;
        GM_ST(void *, (uint8_t *)&t130, (void *)(((uint8_t *)t129 + (uintptr_t)0 * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t130 + 8, (uintptr_t)((uintptr_t)2 - (uintptr_t)0));
        gm_b16_8 t131 = GM_LD(gm_b16_8, ((uint8_t *)&t130));
        gm_b0_1 t132 = gm_f_clear_outside_q01mr8i((void *)NULL, cx, NULL, t126, t127, t128, t131);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t133 = gm_l0;
        gm_b16_8 t134 = gm_l2;
        gm_b0_1 t135 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t133, t134);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t136 = gm_l0;
        gm_tmr5oep1578tb t137;
        gm_b16_8 t138 = gm_l13;
        GM_ST(gm_b16_8, (uint8_t *)&t137 + 0, t138);
        gm_b16_8 t139 = gm_l7;
        GM_ST(gm_b16_8, (uint8_t *)&t137 + 16, t139);
        uint64_t t140 = gm_l6;
        GM_ST(uint64_t, (uint8_t *)&t137 + 32, t140);
        gm_b16_8 t141 = gm_l16;
        GM_ST(gm_b16_8, (uint8_t *)&t137 + 40, t141);
        gm_b0_1 t142 = gm_f_linef_8i4929c((void *)NULL, cx, NULL, t136, &t137);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b0_1 t143;
        memset(&(*(gm_tqgtd5gtgj65s *)gm_out), 0, sizeof (*(gm_tqgtd5gtgj65s *)gm_out));
        ((uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out))[0] = 0;
        GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 8, t143);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_tmp_uninit_jr39s5u(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_tmp_uninit_jr39s5u F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* c */
    gm_b16_8 gm_l3; /* n */
    GM_STACK_CHECK(&F, 309u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        uint8_t *t3 = (((uint8_t *)t2) + 304);
        void * t4 = gm_l0;
        uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)t4) + 304));
        void * t6 = gm_l0;
        void * t7 = GM_LD(void *, (((uint8_t *)t6) + 0));
        void * t8 = (void *)(((uint8_t *)t7) + 8);
        gm_t88rcucg1b7uk t9 = gm_l1;
        uint64_t t10 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t8, t9);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint64_t r11; bool o12 = __builtin_add_overflow(t5, t10, &r11);
        if (o12) gm_trap("integer overflow in `+`", gm_srcmok80ho8hh01, 504, 5);
        uint64_t t13 = r11;
        GM_ST(uint64_t, t3, t13);
    }
    {
        void * t14 = gm_l0;
        void * t15 = GM_LD(void *, (((uint8_t *)t14) + 0));
        void * t16 = (void *)(((uint8_t *)t15) + 8);
        gm_t88rcucg1b7uk t17 = gm_l1;
        gm_b16_8 t18 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t16, t17);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l2 = t18;
    }
    {
        void * t19 = gm_l0;
        gm_b16_8 t20 = gm_f_fresh_tmp_6tadl69((void *)NULL, cx, NULL, t19);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l3 = t20;
    }
    {
        void * t21 = gm_l0;
        gm_t6j6smj7qoctu t22;
        gm_b16_8 t23 = gm_l2;
        GM_ST(gm_b16_8, (uint8_t *)&t22 + 0, t23);
        gm_b16_8 t24 = gm_l3;
        GM_ST(gm_b16_8, (uint8_t *)&t22 + 16, t24);
        gm_b0_1 t25 = gm_f_linef_eils77b((void *)NULL, cx, NULL, t21, &t22);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t26 = gm_l3;
        uint8_t t27 = 0;
        F.ret = t26;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_tmp_uohsptq(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2) {
    struct gm_fr_tmp_uohsptq F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* init */
    gm_b16_8 gm_l3; /* c */
    gm_b16_8 gm_l4; /* n */
    GM_STACK_CHECK(&F, 357u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        uint8_t *t3 = (((uint8_t *)t2) + 304);
        void * t4 = gm_l0;
        uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)t4) + 304));
        void * t6 = gm_l0;
        void * t7 = GM_LD(void *, (((uint8_t *)t6) + 0));
        void * t8 = (void *)(((uint8_t *)t7) + 8);
        gm_t88rcucg1b7uk t9 = gm_l1;
        uint64_t t10 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t8, t9);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint64_t r11; bool o12 = __builtin_add_overflow(t5, t10, &r11);
        if (o12) gm_trap("integer overflow in `+`", gm_srcmok80ho8hh01, 495, 5);
        uint64_t t13 = r11;
        GM_ST(uint64_t, t3, t13);
    }
    {
        void * t14 = gm_l0;
        void * t15 = GM_LD(void *, (((uint8_t *)t14) + 0));
        void * t16 = (void *)(((uint8_t *)t15) + 8);
        gm_t88rcucg1b7uk t17 = gm_l1;
        gm_b16_8 t18 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t16, t17);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l3 = t18;
    }
    {
        void * t19 = gm_l0;
        gm_b16_8 t20 = gm_f_fresh_tmp_6tadl69((void *)NULL, cx, NULL, t19);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l4 = t20;
    }
    {
        void * t21 = gm_l0;
        gm_tfeab279c16rr t22;
        gm_b16_8 t23 = gm_l3;
        GM_ST(gm_b16_8, (uint8_t *)&t22 + 0, t23);
        gm_b16_8 t24 = gm_l4;
        GM_ST(gm_b16_8, (uint8_t *)&t22 + 16, t24);
        gm_b16_8 t25 = gm_l2;
        GM_ST(gm_b16_8, (uint8_t *)&t22 + 32, t25);
        gm_b0_1 t26 = gm_f_linef_bi02v43((void *)NULL, cx, NULL, t21, &t22);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t27 = gm_l4;
        uint8_t t28 = 0;
        F.ret = t27;
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
extern __attribute__((aligned(4))) gm_b16_8 gm_f_unit_8ekedsl(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_unwind_call_4k8ulav(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_unwind_call_4k8ulav F;
    void * gm_l0; /* fg */
    gm_b16_8 gm_l1; /* call */
    GM_STACK_CHECK(&F, 121u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)t3) + 296));
        if (!t4) goto ie2;
        {
            void * t6 = gm_l0;
            gm_b16_8 t7 = gm_l1;
            gm_tnpifv20irnph t8;
            GM_ST(gm_b16_8, ((uint8_t *)&t8 + 0), t7);
            gm_b0_1 t9 = gm_f_linef_qqk2cu4((void *)NULL, cx, NULL, t6, t8);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix2;
ie2:;
        {
            void * t11 = gm_l0;
            gm_b16_8 t12 = gm_l1;
            gm_tnpifv20irnph t13;
            GM_ST(gm_b16_8, ((uint8_t *)&t13 + 0), t12);
            gm_b0_1 t14 = gm_f_linef_g6msml8((void *)NULL, cx, NULL, t11, t13);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t15 = gm_l0;
            gm_b0_1 t16 = gm_f_unwind_ladder_a9ij1gj((void *)NULL, cx, NULL, t15);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
ix2:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_unwind_ladder_a9ij1gj(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_unwind_ladder_a9ij1gj F;
    void * gm_l0; /* fg */
    GM_STACK_CHECK(&F, 104u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        void * t3 = gm_l0;
        uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t3) + 224));
        uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)1ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_srcmok80ho8hh01, 622, 5);
        uint64_t t7 = r5;
        GM_ST(uint64_t, (((uint8_t *)t2) + 224), t7);
    }
    {
        void * t8 = gm_l0;
        gm_b0_1 t9 = gm_f_ladder_36amdbn((void *)NULL, cx, NULL, t8);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t10 = gm_l0;
        void * t11 = gm_l0;
        uint64_t t12 = GM_LD(uint64_t, (((uint8_t *)t11) + 224));
        uint64_t r13; bool o14 = __builtin_sub_overflow(t12, ((uint64_t)1ULL), &r13);
        if (o14) gm_trap("integer overflow in `-`", gm_srcmok80ho8hh01, 624, 5);
        uint64_t t15 = r13;
        GM_ST(uint64_t, (((uint8_t *)t10) + 224), t15);
    }
    {
        void * t16 = gm_l0;
        gm_b16_8 t17;
        GM_ST(void *, (uint8_t *)&t17, (void *)(gm_s2cuflhqkkuaq));
        GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)(1));
        gm_b0_1 t18 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t16, t17);
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
__attribute__((aligned(4))) void gm_f_variant_into_2smodtg(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, void * a2, gm_b16_8 a3, gm_tnrltbc61i6v1 a4) {
    struct gm_fr_variant_into_2smodtg F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* t */
    void * gm_l2; /* vx */
    gm_b16_8 gm_l3; /* dest */
    gm_tnrltbc61i6v1 gm_l4; /* obs */
    void * gm_l5; /* def */
    gm_t88rcucg1b7uk gm_l6; /* tag_ty */
    gm_b16_8 gm_l7; /* variants */
    gm_b16_8 gm_l9; /* offs */
    gm_b16_8 gm_l10; /* tag_val */
    gm_b16_8 gm_l11; /* tag */
    void * gm_l12; /* x */
    void * gm_l13; /* px */
    GM_STACK_CHECK(&F, 874u);
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
        gm_t88rcucg1b7uk t3 = gm_l1;
        void * t4 = gm_f_adt_def_ni54he1(t2, GM_CONV(gm_csguankko4lmhd, t3));
        gm_l5 = t4;
    }
    {
        void * t6 = gm_l5;
        uint8_t t7 = GM_LD(uint8_t, ((((uint8_t *)t6) + 24) + 0));
        if (t7 != ((uint8_t)1ULL)) goto ie5;
        gm_t88rcucg1b7uk t8 = GM_LD(gm_t88rcucg1b7uk, (((((uint8_t *)t6) + 24) + 8) + 0));
        gm_l6 = t8;
        gm_b16_8 t9 = GM_LD(gm_b16_8, (((((uint8_t *)t6) + 24) + 8) + 8));
        gm_l7 = t9;
        {
            void * t11 = gm_l2;
            uint32_t t12 = GM_LD(uint32_t, (((uint8_t *)t11) + 0));
            uint64_t t13 = (uint64_t)t12;
            uint64_t t14 = (uintptr_t)t13;
            uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
            if (t14 >= (uintptr_t)(t15)) gm_trap_index(t14, t15, gm_srcmok80ho8hh01, 1007, 16);
            gm_tckdobunaulee t16 = GM_LD(gm_tckdobunaulee, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t14 * 72));
            F.l8 = t16;
        }
        {
            void * t17 = gm_l0;
            void * t18 = GM_LD(void *, (((uint8_t *)t17) + 0));
            void * t19 = (void *)(((uint8_t *)t18) + 8);
            gm_t88rcucg1b7uk t20 = gm_l1;
            gm_b16_8 t21 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t19, t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l9 = t21;
        }
        {
            void * t22 = gm_l0;
            gm_t88rcucg1b7uk t23 = gm_l6;
            gm_b16_8 t24 = gm_f_int_lit_ai37456((void *)NULL, cx, NULL, t22, t23, &(*(gm_tbci5ospk5nnn *)(((uint8_t *)&F.l8) + 24)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l10 = t24;
        }
        {
            gm_tmr5oep1578tb t25;
            void * t26 = gm_l0;
            void * t27 = GM_LD(void *, (((uint8_t *)t26) + 0));
            void * t28 = (void *)(((uint8_t *)t27) + 8);
            gm_t88rcucg1b7uk t29 = gm_l6;
            gm_b16_8 t30 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t28, t29);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t25 + 0, t30);
            gm_b16_8 t31 = gm_l3;
            GM_ST(gm_b16_8, (uint8_t *)&t25 + 16, t31);
            uint64_t t32 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t33 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
            if (t32 >= (uintptr_t)(t33)) gm_trap_index(t32, t33, gm_srcmok80ho8hh01, 1010, 89);
            uint64_t t34 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t32 * 8));
            GM_ST(uint64_t, (uint8_t *)&t25 + 32, t34);
            gm_b16_8 t35 = gm_l10;
            GM_ST(gm_b16_8, (uint8_t *)&t25 + 40, t35);
            gm_tabb710d9mq2e t36;
            gm_f_format_rgk51pp((void *)&t36, (void *)NULL, cx, NULL, &t25);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t36))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t36) + 8, gm_srcmok80ho8hh01, 1010, 12);
            gm_b16_8 t37 = GM_LD(gm_b16_8, (((uint8_t *)&t36) + 8));
            gm_l11 = t37;
        }
        {
            void * t38 = gm_l2;
            void * t39 = GM_LD(void *, (((uint8_t *)t38) + 8));
            gm_l12 = t39;
        }
        {
            uint8_t t41;
            if ((((((uint8_t *)&F.l8) + 16))[4] != 0)) goto mn43;
            t41 = ((uint8_t)1);
            goto me42;
mn43:;
            t41 = ((uint8_t)0);
            goto me42;
mn44:;
            gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 1012, 8);
me42:;
            if (!t41) goto ie40;
            {
                void * t47 = gm_l12;
                if (!(GM_LD(void *, ((uint8_t *)&t47)) != 0)) goto ie46;
                void * t48 = GM_LD(void *, ((uint8_t *)&t47));
                gm_l13 = t48;
                {
                    {
                        void * t51 = gm_l0;
                        void * t52 = gm_l13;
                        gm_t5o0tpv5jka2l t53;
                        gm_f_expr_porbc9t((void *)&t53, (void *)NULL, cx, NULL, t51, t52);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        if (((((uint8_t *)&t53))[0] != 0)) {
                            gm_tqgtd5gtgj65s t54;
                            GM_ST(uint8_t, (uint8_t *)&t54 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t54 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t54 + 4, 0);
                            ((uint8_t *)&t54)[0] = 1;
                            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t54 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t53) + 8)));
                            F.ret = t54;
                            goto gm_fret;
                        }
                        gm_b16_8 t55 = GM_LD(gm_b16_8, (((uint8_t *)&t53) + 8));
                    }
                }
                goto ix46;
ie46:;
ix46:;
            }
            {
                gm_l12 = ((void *)0);
            }
            goto ix40;
ie40:;
ix40:;
        }
        {
            void * t56 = gm_l0;
            gm_t88rcucg1b7uk t57 = gm_l1;
            gm_b16_8 t58 = gm_l11;
            gm_b16_8 t59 = GM_LD(gm_b16_8, ((uint8_t *)&t58));
            uint64_t t60 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t61 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
            if (t60 >= (uintptr_t)(t61)) gm_trap_index(t60, t61, gm_srcmok80ho8hh01, 1016, 35);
            uint64_t t62 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t60 * 8));
            void * t63 = gm_l0;
            void * t64 = GM_LD(void *, (((uint8_t *)t63) + 0));
            void * t65 = (void *)(((uint8_t *)t64) + 8);
            gm_t88rcucg1b7uk t66 = gm_l6;
            uint64_t t67 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t65, t66);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t68 = gm_l12;
            uint64_t t69 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t70 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
            if (t69 >= (uintptr_t)(t70)) gm_trap_index(t69, t70, gm_srcmok80ho8hh01, 1016, 69);
            uint64_t t71 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t69 * 8));
            gm_b16_8 t72 = gm_l3;
            gm_tnrltbc61i6v1 t73 = gm_l4;
            gm_f_tagged_into_ncahfuo((void *)&(*(gm_tqgtd5gtgj65s *)gm_out), (void *)NULL, cx, NULL, t56, t57, t59, t62, t67, t68, t71, t72, t73);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto ix5;
ie5:;
        {
            gm_trap("reached `@unreachable()`", gm_srcmok80ho8hh01, 1006, 47);
        }
ix5:;
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
inline __attribute__((aligned(4))) void gm_f_with_scope_o670qbc(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2) {
    struct gm_fr_with_scope_o670qbc F;
    void * gm_l0; /* fg */
    uint32_t gm_l1; /* label */
    gm_b16_8 gm_l2; /* end */
    GM_STACK_CHECK(&F, 172u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    memset((uint8_t *)&(*(gm_tehgl6lkqgvt0 *)gm_out) + 52, 0, 4);
    gm_tiilmfd16v3fc t1;
    memset((uint8_t *)&t1 + 4, 0, 4);
    uint32_t t2 = gm_l1;
    GM_ST(uint32_t, (uint8_t *)&t1 + 0, t2);
    gm_b16_8 t3 = gm_l2;
    GM_ST(gm_b16_8, (uint8_t *)&t1 + 8, t3);
    gm_tbf8u1b86cgmi t4;
    GM_ST(uint8_t, (uint8_t *)&t4 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t4 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t4 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t4 + 32, 0);
    GM_ST(uint64_t, (uint8_t *)&t4 + 40, 0);
    GM_ST(uint8_t, ((uint8_t *)&t4 + 0), ((uint8_t)2ULL));
    GM_ST(gm_tiilmfd16v3fc, ((uint8_t *)&t4 + 8), t1);
    void * t5 = gm_l0;
    uint32_t t6 = gm_f_fresh_5kokjmg(t5);
    gm_trqfvmjtt4sij t7;
    gm_f_list_rl2scuf((void *)&t7, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_tbf8u1b86cgmi, (uint8_t *)&(*(gm_tehgl6lkqgvt0 *)gm_out) + 0, t4);
    GM_ST(uint32_t, (uint8_t *)&(*(gm_tehgl6lkqgvt0 *)gm_out) + 48, t6);
    GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_tehgl6lkqgvt0 *)gm_out) + 56, t7);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tehgl6lkqgvt0 *)gm_out) + 96, ((uint64_t)0ULL));
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
inline __attribute__((aligned(4))) void gm_f_wrap_into_lm24d33(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, uint8_t a2, uint8_t a3, void * a4, gm_b16_8 a5, gm_tnrltbc61i6v1 a6) {
    struct gm_fr_wrap_into_lm24d33 F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* t */
    uint8_t gm_l2; /* some */
    uint8_t gm_l3; /* err */
    void * gm_l4; /* x */
    gm_b16_8 gm_l5; /* dest */
    gm_tnrltbc61i6v1 gm_l6; /* obs */
    gm_b16_8 gm_l7; /* offs */
    uint64_t gm_l8; /* pay_off */
    uint64_t gm_l9; /* tag_off */
    gm_b16_8 gm_l10; /* tag */
    GM_STACK_CHECK(&F, 471u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    gm_l6 = a6;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_t88rcucg1b7uk t5 = gm_l1;
        gm_b16_8 t6 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t4, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l7 = t6;
    }
    {
        uint64_t t7;
        uint8_t t9 = gm_l2;
        if (!t9) goto ie8;
        uint64_t t11 = ((uint64_t)0ULL);
        t7 = t11;
        goto ix8;
ie8:;
        uint64_t t13 = (uintptr_t)((uint64_t)1ULL);
        uint64_t t14 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
        if (t13 >= (uintptr_t)(t14)) gm_trap_index(t13, t14, gm_srcmok80ho8hh01, 997, 41);
        uint64_t t15 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t13 * 8));
        uint64_t t16 = t15;
        t7 = t16;
ix8:;
        gm_l8 = t7;
    }
    {
        uint64_t t17;
        uint8_t t19 = gm_l2;
        if (!t19) goto ie18;
        uint64_t t21 = (uintptr_t)((uint64_t)1ULL);
        uint64_t t22 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
        if (t21 >= (uintptr_t)(t22)) gm_trap_index(t21, t22, gm_srcmok80ho8hh01, 998, 26);
        uint64_t t23 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t21 * 8));
        uint64_t t24 = t23;
        t17 = t24;
        goto ix18;
ie18:;
        uint64_t t26 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t27 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
        if (t26 >= (uintptr_t)(t27)) gm_trap_index(t26, t27, gm_srcmok80ho8hh01, 998, 43);
        uint64_t t28 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t26 * 8));
        uint64_t t29 = t28;
        t17 = t29;
ix18:;
        gm_l9 = t17;
    }
    {
        gm_tlk7da9ssbu7q t30;
        memset((uint8_t *)&t30 + 28, 0, 4);
        gm_b16_8 t31 = gm_l5;
        GM_ST(gm_b16_8, (uint8_t *)&t30 + 0, t31);
        uint64_t t32 = gm_l9;
        GM_ST(uint64_t, (uint8_t *)&t30 + 16, t32);
        int32_t t33;
        uint8_t t35 = 0;
        uint8_t t36 = gm_l2;
        if (!t36) {
            uint8_t t37 = gm_l3;
            t35 = t37;
        } else t35 = 1;
        if (!t35) goto ie34;
        int32_t t39 = ((int32_t)1LL);
        t33 = t39;
        goto ix34;
ie34:;
        int32_t t41 = ((int32_t)0LL);
        t33 = t41;
ix34:;
        GM_ST(int32_t, (uint8_t *)&t30 + 24, t33);
        gm_tabb710d9mq2e t42;
        gm_f_format_43nlbju((void *)&t42, (void *)NULL, cx, NULL, &t30);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t42))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t42) + 8, gm_srcmok80ho8hh01, 999, 12);
        gm_b16_8 t43 = GM_LD(gm_b16_8, (((uint8_t *)&t42) + 8));
        gm_l10 = t43;
    }
    {
        void * t44 = gm_l0;
        gm_t88rcucg1b7uk t45 = gm_l1;
        gm_b16_8 t46 = gm_l10;
        gm_b16_8 t47 = GM_LD(gm_b16_8, ((uint8_t *)&t46));
        uint64_t t48 = gm_l9;
        void * t49 = gm_l4;
        uint64_t t50 = gm_l8;
        gm_b16_8 t51 = gm_l5;
        gm_tnrltbc61i6v1 t52 = gm_l6;
        gm_f_tagged_into_ncahfuo((void *)&(*(gm_tqgtd5gtgj65s *)gm_out), (void *)NULL, cx, NULL, t44, t45, t47, t48, ((uint64_t)1ULL), t49, t50, t51, t52);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_zero_tlk13aa(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_zero_tlk13aa F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* cty */
    GM_STACK_CHECK(&F, 340u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_t88rcucg1b7uk t5 = gm_l1;
        gm_b16_8 t6 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t4, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l2 = t6;
    }
    {
        void * t8 = gm_l0;
        void * t9 = GM_LD(void *, (((uint8_t *)t8) + 0));
        void * t10 = (void *)(((uint8_t *)t9) + 8);
        gm_t88rcucg1b7uk t11 = gm_l1;
        gm_t5v8n6qrn4rh4 t12;
        gm_f_repr_53quqki((void *)&t12, (void *)NULL, cx, NULL, t10, t11);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t13;
        uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)&t12) + 0));
        switch (t21) { case ((uint8_t)0ULL): goto ma15; case ((uint8_t)1ULL): goto ma16; case ((uint8_t)2ULL): goto ma17; case ((uint8_t)3ULL): goto ma18; case ((uint8_t)4ULL): goto ma19; case ((uint8_t)5ULL): goto ma19; case ((uint8_t)6ULL): goto ma19; default: goto ma19; }
ma15:;
        uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)&t12) + 0));
        if (t23 != ((uint8_t)0ULL)) goto mn22;
        t13 = ((uint8_t)1);
        goto me14;
mn22:;
ma16:;
        uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)&t12) + 0));
        if (t25 != ((uint8_t)1ULL)) goto mn24;
        t13 = ((uint8_t)1);
        goto me14;
mn24:;
ma17:;
        uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)&t12) + 0));
        if (t27 != ((uint8_t)2ULL)) goto mn26;
        t13 = ((uint8_t)1);
        goto me14;
mn26:;
ma18:;
        uint8_t t29 = GM_LD(uint8_t, (((uint8_t *)&t12) + 0));
        if (t29 != ((uint8_t)3ULL)) goto mn28;
        t13 = ((uint8_t)1);
        goto me14;
mn28:;
ma19:;
        t13 = ((uint8_t)0);
        goto me14;
mn30:;
ma20:;
        gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 528, 8);
me14:;
        if (!t13) goto ie7;
        {
            gm_b16_8 t32 = gm_l2;
            gm_tnpifv20irnph t33;
            GM_ST(gm_b16_8, ((uint8_t *)&t33 + 0), t32);
            gm_tabb710d9mq2e t34;
            gm_f_format_07f8le4((void *)&t34, (void *)NULL, cx, NULL, t33);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t34))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t34) + 8, gm_srcmok80ho8hh01, 528, 76);
            gm_b16_8 t35 = GM_LD(gm_b16_8, (((uint8_t *)&t34) + 8));
            gm_b16_8 t36 = GM_LD(gm_b16_8, ((uint8_t *)&t35));
            uint8_t t37 = 0;
            F.ret = t36;
            goto gm_fret;
        }
        goto ix7;
ie7:;
ix7:;
    }
    {
        gm_b16_8 t38 = gm_l2;
        gm_tnpifv20irnph t39;
        GM_ST(gm_b16_8, ((uint8_t *)&t39 + 0), t38);
        gm_tabb710d9mq2e t40;
        gm_f_format_loh175m((void *)&t40, (void *)NULL, cx, NULL, t39);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t40))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t40) + 8, gm_srcmok80ho8hh01, 529, 12);
        gm_b16_8 t41 = GM_LD(gm_b16_8, (((uint8_t *)&t40) + 8));
        gm_b16_8 t42 = GM_LD(gm_b16_8, ((uint8_t *)&t41));
        uint8_t t43 = 0;
        F.ret = t42;
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

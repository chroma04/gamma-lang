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
typedef struct gm_csc7lc6jlkqkuq { uint64_t f0; uint64_t f1; } gm_csc7lc6jlkqkuq;
typedef struct gm_csguankko4lmhd { uint32_t f0; } gm_csguankko4lmhd;
typedef struct { uint8_t f0; uint8_t p1[3]; int32_t f1; int32_t f2; int32_t f3; int32_t f4; int32_t f5; uint32_t f6; uint8_t p7[4]; int64_t f7; } gm_t01jnheg9lqvd;
typedef struct { uint64_t f0; uint64_t f1; } gm_t2lqu8jsj511d;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint64_t f0; } gm_t4ce0jd3tdro4;
typedef struct { unsigned __int128 f0; unsigned __int128 f1; } gm_t793vlgpa3b88;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { __int128 f0; uint8_t f1; uint8_t pe[15]; } gm_ta24rd13gupia;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; void * f2; } gm_ta612fil812pm;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { void * f0; uint32_t f1; uint8_t pe[4]; } gm_tbl8il92l97ut;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint8_t f2; uint8_t pe[7]; } gm_tlcl4u558l6p5;
typedef struct { void * f0; void * f1; uint32_t f2; uint32_t f3; } gm_tmm67r2c94d5j;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; } f1; } gm_tmn7n0n9raou5;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; } gm_toh0ootodhjue;
typedef struct { void * f0; uint8_t f1; uint8_t pe[7]; } gm_tsplfr33dsvji;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
typedef struct { uint64_t a[4]; } gm_tu703bl1jkggd;
typedef struct { uint8_t f0; uint8_t p1[3]; uint32_t f1; uint64_t f2; } gm_tuglhjjmj2g58;
_Static_assert(sizeof(gm_csc7lc6jlkqkuq) == 16 && _Alignof(gm_csc7lc6jlkqkuq) == 8 && offsetof(gm_csc7lc6jlkqkuq, f0) == 0 && offsetof(gm_csc7lc6jlkqkuq, f1) == 8, "layout of `BlobPart`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
_Static_assert(sizeof(gm_t01jnheg9lqvd) == 40 && _Alignof(gm_t01jnheg9lqvd) == 8 && offsetof(gm_t01jnheg9lqvd, f0) == 0 && offsetof(gm_t01jnheg9lqvd, f1) == 4 && offsetof(gm_t01jnheg9lqvd, f2) == 8 && offsetof(gm_t01jnheg9lqvd, f3) == 12 && offsetof(gm_t01jnheg9lqvd, f4) == 16 && offsetof(gm_t01jnheg9lqvd, f5) == 20 && offsetof(gm_t01jnheg9lqvd, f6) == 24 && offsetof(gm_t01jnheg9lqvd, f7) == 32, "layout of `(u8, i32, i32, i32, i32, i32, u32, i64)`");
typedef struct { void * f0; gm_t9805qtonbsag f1; uint32_t f2; uint8_t pe[4]; } gm_t083kd7b1kkag;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_b16_8 f0; int32_t f1; uint8_t pe[4]; } gm_t0bf4lga76ktk;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; } gm_t0uuhaljsf410;
_Static_assert(sizeof(gm_t2lqu8jsj511d) == 16 && _Alignof(gm_t2lqu8jsj511d) == 8 && offsetof(gm_t2lqu8jsj511d, f0) == 0 && offsetof(gm_t2lqu8jsj511d, f1) == 8, "layout of `(u64, u64)`");
typedef struct { gm_b16_8 f0; double f1; } gm_t2mvr8fpgjkg6;
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
typedef struct { void * f0; gm_t9805qtonbsag f1; void * f2; } gm_t3dcoc7077t1q;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; uint8_t f2; uint8_t pe[3]; } gm_t3rcjibl8kkg4;
_Static_assert(sizeof(gm_t4ce0jd3tdro4) == 8 && _Alignof(gm_t4ce0jd3tdro4) == 8 && offsetof(gm_t4ce0jd3tdro4, f0) == 0, "layout of `Instant`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint32_t f2; uint8_t p3[4]; gm_b16_8 f3; gm_b16_8 f4; gm_b16_8 f5; gm_b16_8 f6; gm_b16_8 f7; } gm_t586m754bse2v;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tuglhjjmj2g58 v0; uint16_t v1; } f1; } gm_t5v8n6qrn4rh4;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; void * f3; } gm_t710lsrq1m8gk;
_Static_assert(sizeof(gm_t793vlgpa3b88) == 32 && _Alignof(gm_t793vlgpa3b88) == 16 && offsetof(gm_t793vlgpa3b88, f0) == 0 && offsetof(gm_t793vlgpa3b88, f1) == 16, "layout of `(lo: u128, hi: u128)`");
typedef struct { uint8_t f0; gm_t9qkh0rsooc8d f1; uint8_t f2; uint8_t f3; uint8_t f4; uint8_t p5[3]; uint32_t f5; gm_t9805qtonbsag f6; gm_t9qkh0rsooc8d f7; uint8_t pe[3]; } gm_t7g88i52rrtm5;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; uint64_t f3; gm_b16_8 f4; } gm_t89vrq05jgle5;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; void * f1; } gm_t8ghgl2jj0qha;
typedef struct { gm_tmn7n0n9raou5 f0; void * f1; } gm_t8jsc4n9vp2ib;
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
_Static_assert(sizeof(gm_ta24rd13gupia) == 32 && _Alignof(gm_ta24rd13gupia) == 16 && offsetof(gm_ta24rd13gupia, f0) == 0 && offsetof(gm_ta24rd13gupia, f1) == 16, "layout of `?i128`");
_Static_assert(sizeof(gm_ta612fil812pm) == 24 && _Alignof(gm_ta612fil812pm) == 8 && offsetof(gm_ta612fil812pm, f0) == 0 && offsetof(gm_ta612fil812pm, f1) == 8 && offsetof(gm_ta612fil812pm, f2) == 16, "layout of `(is_and: bool, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tao63pk3r18v5) == 32 && _Alignof(gm_tao63pk3r18v5) == 8 && offsetof(gm_tao63pk3r18v5, f0) == 0 && offsetof(gm_tao63pk3r18v5, f1) == 8 && offsetof(gm_tao63pk3r18v5, f2) == 16 && offsetof(gm_tao63pk3r18v5, f3) == 24, "layout of `SliceExpr`");
typedef struct { uint64_t f0; uint64_t f1; gm_b16_8 f2; } gm_tbccvikg6b96e;
_Static_assert(sizeof(gm_tbl8il92l97ut) == 16 && _Alignof(gm_tbl8il92l97ut) == 8 && offsetof(gm_tbl8il92l97ut, f0) == 0 && offsetof(gm_tbl8il92l97ut, f1) == 8, "layout of `SpecCall`");
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { gm_b16_8 f0; __int128 f1; } gm_tctdkl637nfjf;
typedef struct { uint64_t f0; gm_b16_8 f1; gm_b16_8 f2; uint64_t f3; gm_b16_8 f4; } gm_tctj0rue4hgoo;
typedef struct { gm_t2lqu8jsj511d a[2]; } gm_te3hi449i91a8;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_tcc1rq4n1avb2 v0; } f1; } gm_te9hrk0u35rev;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t pe[4]; } gm_tep2drsl9408b;
typedef struct { uint8_t f0; union { gm_toh0ootodhjue v0; } f1; } gm_tesaltf0mhcki;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint64_t f2; } gm_tftksr4op114e;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; gm_b16_8 f5; gm_b16_8 f6; gm_b16_8 f7; } gm_tg1t0c5hkovn5;
typedef struct { gm_b16_8 f0; uint64_t f1; uint64_t f2; } gm_tgbgti82mf879;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; gm_b16_8 f5; gm_b16_8 f6; } gm_th2d9c7aju8hp;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_b16_8 f2; } gm_thognv9odov03;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; uint32_t f3; uint8_t pe[4]; } gm_tj4tbafenpsjm;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; uint64_t f5; } gm_tjftu5su6fh8e;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; gm_b16_8 f5; gm_b16_8 f6; gm_b16_8 f7; uint64_t f8; } gm_tjg6nvivto1u9;
typedef struct { void * f0; gm_b16_8 f1; } gm_tkaapcii86udk;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tlcl4u558l6p5) == 24 && _Alignof(gm_tlcl4u558l6p5) == 8 && offsetof(gm_tlcl4u558l6p5, f0) == 0 && offsetof(gm_tlcl4u558l6p5, f1) == 8 && offsetof(gm_tlcl4u558l6p5, f2) == 16, "layout of `(lo: ^Expr, hi: ^Expr, inclusive: bool)`");
typedef struct { gm_b16_8 f0; uint64_t f1; int32_t f2; uint8_t pe[4]; } gm_tlk7da9ssbu7q;
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
_Static_assert(sizeof(gm_tmn7n0n9raou5) == 8 && _Alignof(gm_tmn7n0n9raou5) == 4 && offsetof(gm_tmn7n0n9raou5, f0) == 0 && offsetof(gm_tmn7n0n9raou5, f1) == 4, "layout of `Coercion`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_t88rcucg1b7uk a[1]; } gm_tnqk2qt7j42o9;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t9805qtonbsag v0; } f1; } gm_tnrltbc61i6v1;
typedef struct { uint64_t f0; gm_b16_8 f1; uint64_t f2; } gm_tob1snuii5bme;
_Static_assert(sizeof(gm_toh0ootodhjue) == 3 && _Alignof(gm_toh0ootodhjue) == 1 && offsetof(gm_toh0ootodhjue, f0) == 0 && offsetof(gm_toh0ootodhjue, f1) == 1 && offsetof(gm_toh0ootodhjue, f2) == 2, "layout of `Restr`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint8_t pe[4]; } gm_tommf4j9upomc;
typedef struct { uint32_t f0; uint32_t f1; gm_t88rcucg1b7uk f2; } gm_tov4mtrfp5e7v;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; gm_b16_8 f5; uint64_t f6; gm_b16_8 f7; } gm_tpbrs0bkofuth;
typedef struct { gm_b16_8 a[1]; } gm_tpmdr2hmdhsqr;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_b16_8 f2; } gm_tq3m5qgvkpje8;
typedef struct { gm_tuglhjjmj2g58 f0; uint8_t f1; uint8_t pe[7]; } gm_tqcc6dmlne11b;
typedef struct { gm_b16_8 f0; void * f1; void * f2; } gm_trl2khmuphncd;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
_Static_assert(sizeof(gm_tsplfr33dsvji) == 16 && _Alignof(gm_tsplfr33dsvji) == 8 && offsetof(gm_tsplfr33dsvji, f0) == 0 && offsetof(gm_tsplfr33dsvji, f1) == 8, "layout of `(expr: ^Expr, is_result: bool)`");
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
_Static_assert(sizeof(gm_tu703bl1jkggd) == 32 && _Alignof(gm_tu703bl1jkggd) == 8 && offsetof(gm_tu703bl1jkggd, a) == 0, "layout of `[4]usize`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint32_t f2; uint8_t p3[4]; gm_b16_8 f3; } gm_tu8mt0h8clq1l;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; } gm_tud7oovfvnf43;
_Static_assert(sizeof(gm_tuglhjjmj2g58) == 16 && _Alignof(gm_tuglhjjmj2g58) == 8 && offsetof(gm_tuglhjjmj2g58, f0) == 0 && offsetof(gm_tuglhjjmj2g58, f1) == 4 && offsetof(gm_tuglhjjmj2g58, f2) == 8, "layout of `(signed: bool, bits: u32, storage: u64)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_tcc1rq4n1avb2 v3; uint32_t v4; uint32_t v5; } f1; } gm_tvdg94bk7aa22;
_Static_assert(sizeof(gm_t083kd7b1kkag) == 24 && _Alignof(gm_t083kd7b1kkag) == 8 && offsetof(gm_t083kd7b1kkag, f0) == 0 && offsetof(gm_t083kd7b1kkag, f1) == 8 && offsetof(gm_t083kd7b1kkag, f2) == 16, "layout of `TryExpr`");
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t0bf4lga76ktk) == 24 && _Alignof(gm_t0bf4lga76ktk) == 8 && offsetof(gm_t0bf4lga76ktk, f0) == 0 && offsetof(gm_t0bf4lga76ktk, f1) == 16, "layout of `(string, i32)`");
_Static_assert(sizeof(gm_t0uuhaljsf410) == 64 && _Alignof(gm_t0uuhaljsf410) == 8 && offsetof(gm_t0uuhaljsf410, f0) == 0 && offsetof(gm_t0uuhaljsf410, f1) == 16 && offsetof(gm_t0uuhaljsf410, f2) == 32 && offsetof(gm_t0uuhaljsf410, f3) == 48, "layout of `(string, string, string, string)`");
_Static_assert(sizeof(gm_t2mvr8fpgjkg6) == 24 && _Alignof(gm_t2mvr8fpgjkg6) == 8 && offsetof(gm_t2mvr8fpgjkg6, f0) == 0 && offsetof(gm_t2mvr8fpgjkg6, f1) == 16, "layout of `(string, f64)`");
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `WasmReader`");
_Static_assert(sizeof(gm_t3dcoc7077t1q) == 24 && _Alignof(gm_t3dcoc7077t1q) == 8 && offsetof(gm_t3dcoc7077t1q, f0) == 0 && offsetof(gm_t3dcoc7077t1q, f1) == 8 && offsetof(gm_t3dcoc7077t1q, f2) == 16, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_t3rcjibl8kkg4) == 12 && _Alignof(gm_t3rcjibl8kkg4) == 4 && offsetof(gm_t3rcjibl8kkg4, f0) == 0 && offsetof(gm_t3rcjibl8kkg4, f1) == 4 && offsetof(gm_t3rcjibl8kkg4, f2) == 8, "layout of `(a: Ty, b: Ty, negated: bool)`");
typedef struct { gm_tiilmfd16v3fc f0; uint32_t f1; uint8_t pe[4]; } gm_t419mo45g7j0a;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
typedef struct { gm_tcmkompbl09il f0; uint8_t f1; uint8_t pe[7]; } gm_t57euh612ligr;
_Static_assert(sizeof(gm_t586m754bse2v) == 120 && _Alignof(gm_t586m754bse2v) == 8 && offsetof(gm_t586m754bse2v, f0) == 0 && offsetof(gm_t586m754bse2v, f1) == 16 && offsetof(gm_t586m754bse2v, f2) == 32 && offsetof(gm_t586m754bse2v, f3) == 40 && offsetof(gm_t586m754bse2v, f4) == 56 && offsetof(gm_t586m754bse2v, f5) == 72 && offsetof(gm_t586m754bse2v, f6) == 88 && offsetof(gm_t586m754bse2v, f7) == 104, "layout of `([]u8, string, u32, []u8, []u8, []u8, []u8, []u8)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t08olq97rscn3 v0; gm_b16_8 v1; } f1; } gm_t5o0tpv5jka2l;
_Static_assert(sizeof(gm_t5v8n6qrn4rh4) == 24 && _Alignof(gm_t5v8n6qrn4rh4) == 8 && offsetof(gm_t5v8n6qrn4rh4, f0) == 0 && offsetof(gm_t5v8n6qrn4rh4, f1) == 8, "layout of `Repr`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t08olq97rscn3 v0; gm_th2lqbo77pvsc v1; } f1; } gm_t6j4d6ei14c2t;
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t710lsrq1m8gk) == 40 && _Alignof(gm_t710lsrq1m8gk) == 8 && offsetof(gm_t710lsrq1m8gk, f0) == 0 && offsetof(gm_t710lsrq1m8gk, f1) == 8 && offsetof(gm_t710lsrq1m8gk, f2) == 24 && offsetof(gm_t710lsrq1m8gk, f3) == 32, "layout of `WithExpr`");
_Static_assert(sizeof(gm_t7g88i52rrtm5) == 24 && _Alignof(gm_t7g88i52rrtm5) == 4 && offsetof(gm_t7g88i52rrtm5, f0) == 0 && offsetof(gm_t7g88i52rrtm5, f1) == 1 && offsetof(gm_t7g88i52rrtm5, f2) == 2 && offsetof(gm_t7g88i52rrtm5, f3) == 3 && offsetof(gm_t7g88i52rrtm5, f4) == 4 && offsetof(gm_t7g88i52rrtm5, f5) == 8 && offsetof(gm_t7g88i52rrtm5, f6) == 12 && offsetof(gm_t7g88i52rrtm5, f7) == 20, "layout of `FmtSpec`");
_Static_assert(sizeof(gm_t89vrq05jgle5) == 72 && _Alignof(gm_t89vrq05jgle5) == 8 && offsetof(gm_t89vrq05jgle5, f0) == 0 && offsetof(gm_t89vrq05jgle5, f1) == 16 && offsetof(gm_t89vrq05jgle5, f2) == 32 && offsetof(gm_t89vrq05jgle5, f3) == 48 && offsetof(gm_t89vrq05jgle5, f4) == 56, "layout of `(string, string, string, usize, string)`");
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
_Static_assert(sizeof(gm_t8jsc4n9vp2ib) == 16 && _Alignof(gm_t8jsc4n9vp2ib) == 8 && offsetof(gm_t8jsc4n9vp2ib, f0) == 0 && offsetof(gm_t8jsc4n9vp2ib, f1) == 8, "layout of `(kind: Coercion, expr: ^Expr)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
typedef struct { gm_t3877pk7lv748 f0; uint8_t f1; uint8_t pe[7]; } gm_t9geo0l9gngfn;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { gm_tesaltf0mhcki f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tandvbt63uk7n;
_Static_assert(sizeof(gm_tbccvikg6b96e) == 32 && _Alignof(gm_tbccvikg6b96e) == 8 && offsetof(gm_tbccvikg6b96e, f0) == 0 && offsetof(gm_tbccvikg6b96e, f1) == 8 && offsetof(gm_tbccvikg6b96e, f2) == 16, "layout of `Layout`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_thognv9odov03 v0; gm_tiilmfd16v3fc v1; } f1; } gm_tbf8u1b86cgmi;
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
_Static_assert(sizeof(gm_tctdkl637nfjf) == 32 && _Alignof(gm_tctdkl637nfjf) == 16 && offsetof(gm_tctdkl637nfjf, f0) == 0 && offsetof(gm_tctdkl637nfjf, f1) == 16, "layout of `(string, i128)`");
_Static_assert(sizeof(gm_tctj0rue4hgoo) == 64 && _Alignof(gm_tctj0rue4hgoo) == 8 && offsetof(gm_tctj0rue4hgoo, f0) == 0 && offsetof(gm_tctj0rue4hgoo, f1) == 8 && offsetof(gm_tctj0rue4hgoo, f2) == 24 && offsetof(gm_tctj0rue4hgoo, f3) == 40 && offsetof(gm_tctj0rue4hgoo, f4) == 48, "layout of `(u64, string, string, u64, string)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
typedef struct { gm_tep2drsl9408b f0; uint8_t f1; uint8_t pe[7]; } gm_tdlpv0q69lov1;
_Static_assert(sizeof(gm_te3hi449i91a8) == 32 && _Alignof(gm_te3hi449i91a8) == 8 && offsetof(gm_te3hi449i91a8, a) == 0, "layout of `[2]BlobPart`");
_Static_assert(sizeof(gm_te9hrk0u35rev) == 12 && _Alignof(gm_te9hrk0u35rev) == 4 && offsetof(gm_te9hrk0u35rev, f0) == 0 && offsetof(gm_te9hrk0u35rev, f1) == 4, "layout of `Target`");
typedef struct { gm_te9hrk0u35rev f0; uint8_t p1[4]; void * f1; } gm_teo9d4mncjfdu;
_Static_assert(sizeof(gm_tep2drsl9408b) == 24 && _Alignof(gm_tep2drsl9408b) == 8 && offsetof(gm_tep2drsl9408b, f0) == 0 && offsetof(gm_tep2drsl9408b, f1) == 16, "layout of `ValTy`");
_Static_assert(sizeof(gm_tesaltf0mhcki) == 4 && _Alignof(gm_tesaltf0mhcki) == 1 && offsetof(gm_tesaltf0mhcki, f0) == 0 && offsetof(gm_tesaltf0mhcki, f1) == 1, "layout of `SoaOp`");
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `Toolchain`");
typedef struct { gm_t7g88i52rrtm5 f0; uint8_t f1; uint8_t pe[3]; } gm_tflpan0ud4tim;
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tftksr4op114e) == 40 && _Alignof(gm_tftksr4op114e) == 8 && offsetof(gm_tftksr4op114e, f0) == 0 && offsetof(gm_tftksr4op114e, f1) == 16 && offsetof(gm_tftksr4op114e, f2) == 32, "layout of `(string, string, usize)`");
_Static_assert(sizeof(gm_tg1t0c5hkovn5) == 128 && _Alignof(gm_tg1t0c5hkovn5) == 8 && offsetof(gm_tg1t0c5hkovn5, f0) == 0 && offsetof(gm_tg1t0c5hkovn5, f1) == 16 && offsetof(gm_tg1t0c5hkovn5, f2) == 32 && offsetof(gm_tg1t0c5hkovn5, f3) == 48 && offsetof(gm_tg1t0c5hkovn5, f4) == 64 && offsetof(gm_tg1t0c5hkovn5, f5) == 80 && offsetof(gm_tg1t0c5hkovn5, f6) == 96 && offsetof(gm_tg1t0c5hkovn5, f7) == 112, "layout of `([]u8, string, string, string, string, string, string, string)`");
_Static_assert(sizeof(gm_tgbgti82mf879) == 32 && _Alignof(gm_tgbgti82mf879) == 8 && offsetof(gm_tgbgti82mf879, f0) == 0 && offsetof(gm_tgbgti82mf879, f1) == 16 && offsetof(gm_tgbgti82mf879, f2) == 24, "layout of `PayloadUnion`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2d9c7aju8hp) == 112 && _Alignof(gm_th2d9c7aju8hp) == 8 && offsetof(gm_th2d9c7aju8hp, f0) == 0 && offsetof(gm_th2d9c7aju8hp, f1) == 16 && offsetof(gm_th2d9c7aju8hp, f2) == 32 && offsetof(gm_th2d9c7aju8hp, f3) == 48 && offsetof(gm_th2d9c7aju8hp, f4) == 64 && offsetof(gm_th2d9c7aju8hp, f5) == 80 && offsetof(gm_th2d9c7aju8hp, f6) == 96, "layout of `(string, string, string, string, string, string, string)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_thognv9odov03) == 40 && _Alignof(gm_thognv9odov03) == 8 && offsetof(gm_thognv9odov03, f0) == 0 && offsetof(gm_thognv9odov03, f1) == 8 && offsetof(gm_thognv9odov03, f2) == 24, "layout of `ForeignFn`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tj4tbafenpsjm) == 56 && _Alignof(gm_tj4tbafenpsjm) == 8 && offsetof(gm_tj4tbafenpsjm, f0) == 0 && offsetof(gm_tj4tbafenpsjm, f1) == 16 && offsetof(gm_tj4tbafenpsjm, f2) == 32 && offsetof(gm_tj4tbafenpsjm, f3) == 48, "layout of `(string, string, string, u32)`");
_Static_assert(sizeof(gm_tjftu5su6fh8e) == 88 && _Alignof(gm_tjftu5su6fh8e) == 8 && offsetof(gm_tjftu5su6fh8e, f0) == 0 && offsetof(gm_tjftu5su6fh8e, f1) == 16 && offsetof(gm_tjftu5su6fh8e, f2) == 32 && offsetof(gm_tjftu5su6fh8e, f3) == 48 && offsetof(gm_tjftu5su6fh8e, f4) == 64 && offsetof(gm_tjftu5su6fh8e, f5) == 80, "layout of `(string, string, string, []u8, string, usize)`");
_Static_assert(sizeof(gm_tjg6nvivto1u9) == 128 && _Alignof(gm_tjg6nvivto1u9) == 8 && offsetof(gm_tjg6nvivto1u9, f0) == 0 && offsetof(gm_tjg6nvivto1u9, f1) == 16 && offsetof(gm_tjg6nvivto1u9, f2) == 24 && offsetof(gm_tjg6nvivto1u9, f3) == 40 && offsetof(gm_tjg6nvivto1u9, f4) == 56 && offsetof(gm_tjg6nvivto1u9, f5) == 72 && offsetof(gm_tjg6nvivto1u9, f6) == 88 && offsetof(gm_tjg6nvivto1u9, f7) == 104 && offsetof(gm_tjg6nvivto1u9, f8) == 120, "layout of `(string, u64, string, string, string, []u8, string, string, u64)`");
_Static_assert(sizeof(gm_tkaapcii86udk) == 24 && _Alignof(gm_tkaapcii86udk) == 8 && offsetof(gm_tkaapcii86udk, f0) == 0 && offsetof(gm_tkaapcii86udk, f1) == 8, "layout of `Place`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tlk7da9ssbu7q) == 32 && _Alignof(gm_tlk7da9ssbu7q) == 8 && offsetof(gm_tlk7da9ssbu7q, f0) == 0 && offsetof(gm_tlk7da9ssbu7q, f1) == 16 && offsetof(gm_tlk7da9ssbu7q, f2) == 24, "layout of `(string, u64, i32)`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tnqk2qt7j42o9) == 4 && _Alignof(gm_tnqk2qt7j42o9) == 4 && offsetof(gm_tnqk2qt7j42o9, a) == 0, "layout of `[1]Ty`");
_Static_assert(sizeof(gm_tnrltbc61i6v1) == 12 && _Alignof(gm_tnrltbc61i6v1) == 4 && offsetof(gm_tnrltbc61i6v1, f0) == 0 && offsetof(gm_tnrltbc61i6v1, f1) == 4, "layout of `Observed`");
_Static_assert(sizeof(gm_tob1snuii5bme) == 32 && _Alignof(gm_tob1snuii5bme) == 8 && offsetof(gm_tob1snuii5bme, f0) == 0 && offsetof(gm_tob1snuii5bme, f1) == 8 && offsetof(gm_tob1snuii5bme, f2) == 24, "layout of `Reloc`");
_Static_assert(sizeof(gm_tommf4j9upomc) == 24 && _Alignof(gm_tommf4j9upomc) == 8 && offsetof(gm_tommf4j9upomc, f0) == 0 && offsetof(gm_tommf4j9upomc, f1) == 16, "layout of `(string, u32)`");
_Static_assert(sizeof(gm_tov4mtrfp5e7v) == 12 && _Alignof(gm_tov4mtrfp5e7v) == 4 && offsetof(gm_tov4mtrfp5e7v, f0) == 0 && offsetof(gm_tov4mtrfp5e7v, f1) == 4 && offsetof(gm_tov4mtrfp5e7v, f2) == 8, "layout of `BitField`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t p2[7]; gm_tf5srh4mp4b3i f2; gm_tkn42u7na3t81 f3; } gm_tpautr4br7h3l;
_Static_assert(sizeof(gm_tpbrs0bkofuth) == 112 && _Alignof(gm_tpbrs0bkofuth) == 8 && offsetof(gm_tpbrs0bkofuth, f0) == 0 && offsetof(gm_tpbrs0bkofuth, f1) == 16 && offsetof(gm_tpbrs0bkofuth, f2) == 24 && offsetof(gm_tpbrs0bkofuth, f3) == 40 && offsetof(gm_tpbrs0bkofuth, f4) == 56 && offsetof(gm_tpbrs0bkofuth, f5) == 72 && offsetof(gm_tpbrs0bkofuth, f6) == 88 && offsetof(gm_tpbrs0bkofuth, f7) == 96, "layout of `(string, u64, string, string, string, string, u64, string)`");
_Static_assert(sizeof(gm_tpmdr2hmdhsqr) == 16 && _Alignof(gm_tpmdr2hmdhsqr) == 8 && offsetof(gm_tpmdr2hmdhsqr, a) == 0, "layout of `[1]string`");
_Static_assert(sizeof(gm_tq3m5qgvkpje8) == 40 && _Alignof(gm_tq3m5qgvkpje8) == 8 && offsetof(gm_tq3m5qgvkpje8, f0) == 0 && offsetof(gm_tq3m5qgvkpje8, f1) == 16 && offsetof(gm_tq3m5qgvkpje8, f2) == 24, "layout of `(string, usize, string)`");
typedef struct { uint64_t f0; gm_b16_8 f1; gm_t88rcucg1b7uk f2; uint8_t p3[4]; uint64_t f3; void * f4; uint8_t f5; uint8_t p6[7]; gm_th2lqbo77pvsc f6; gm_tf5srh4mp4b3i f7; void * f8; } gm_tq7qjpkmqmc5s;
_Static_assert(sizeof(gm_tqcc6dmlne11b) == 24 && _Alignof(gm_tqcc6dmlne11b) == 8 && offsetof(gm_tqcc6dmlne11b, f0) == 0 && offsetof(gm_tqcc6dmlne11b, f1) == 16, "layout of `?IntInfo`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t08olq97rscn3 v0; } f1; } gm_tqgtd5gtgj65s;
_Static_assert(sizeof(gm_trl2khmuphncd) == 32 && _Alignof(gm_trl2khmuphncd) == 8 && offsetof(gm_trl2khmuphncd, f0) == 0 && offsetof(gm_trl2khmuphncd, f1) == 16 && offsetof(gm_trl2khmuphncd, f2) == 24, "layout of `IfExpr`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
typedef struct { gm_tvdg94bk7aa22 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tt8o5kebou62r;
_Static_assert(sizeof(gm_tu8mt0h8clq1l) == 56 && _Alignof(gm_tu8mt0h8clq1l) == 8 && offsetof(gm_tu8mt0h8clq1l, f0) == 0 && offsetof(gm_tu8mt0h8clq1l, f1) == 16 && offsetof(gm_tu8mt0h8clq1l, f2) == 32 && offsetof(gm_tu8mt0h8clq1l, f3) == 40, "layout of `(string, string, u32, string)`");
_Static_assert(sizeof(gm_tud7oovfvnf43) == 80 && _Alignof(gm_tud7oovfvnf43) == 8 && offsetof(gm_tud7oovfvnf43, f0) == 0 && offsetof(gm_tud7oovfvnf43, f1) == 16 && offsetof(gm_tud7oovfvnf43, f2) == 32 && offsetof(gm_tud7oovfvnf43, f3) == 48 && offsetof(gm_tud7oovfvnf43, f4) == 64, "layout of `(string, string, string, string, string)`");
_Static_assert(sizeof(gm_tvdg94bk7aa22) == 12 && _Alignof(gm_tvdg94bk7aa22) == 4 && offsetof(gm_tvdg94bk7aa22, f0) == 0 && offsetof(gm_tvdg94bk7aa22, f1) == 4, "layout of `Builtin`");
_Static_assert(sizeof(gm_t419mo45g7j0a) == 32 && _Alignof(gm_t419mo45g7j0a) == 8 && offsetof(gm_t419mo45g7j0a, f0) == 0 && offsetof(gm_t419mo45g7j0a, f1) == 24, "layout of `(effect_: EffectRef, op: u32)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t57euh612ligr) == 32 && _Alignof(gm_t57euh612ligr) == 8 && offsetof(gm_t57euh612ligr, f0) == 0 && offsetof(gm_t57euh612ligr, f1) == 24, "layout of `?BitsLayout`");
_Static_assert(sizeof(gm_t5o0tpv5jka2l) == 40 && _Alignof(gm_t5o0tpv5jka2l) == 8 && offsetof(gm_t5o0tpv5jka2l, f0) == 0 && offsetof(gm_t5o0tpv5jka2l, f1) == 8, "layout of `Unsup!string`");
_Static_assert(sizeof(gm_t6j4d6ei14c2t) == 40 && _Alignof(gm_t6j4d6ei14c2t) == 8 && offsetof(gm_t6j4d6ei14c2t, f0) == 0 && offsetof(gm_t6j4d6ei14c2t, f1) == 8, "layout of `Unsup!?string`");
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_t9geo0l9gngfn) == 32 && _Alignof(gm_t9geo0l9gngfn) == 8 && offsetof(gm_t9geo0l9gngfn, f0) == 0 && offsetof(gm_t9geo0l9gngfn, f1) == 24, "layout of `?TempPiece`");
_Static_assert(sizeof(gm_tandvbt63uk7n) == 24 && _Alignof(gm_tandvbt63uk7n) == 8 && offsetof(gm_tandvbt63uk7n, f0) == 0 && offsetof(gm_tandvbt63uk7n, f1) == 8, "layout of `(op: SoaOp, args: []Expr)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
_Static_assert(sizeof(gm_tbf8u1b86cgmi) == 48 && _Alignof(gm_tbf8u1b86cgmi) == 8 && offsetof(gm_tbf8u1b86cgmi, f0) == 0 && offsetof(gm_tbf8u1b86cgmi, f1) == 8, "layout of `ScopeKind`");
typedef struct { gm_b16_8 f0; gm_tkn42u7na3t81 f1; gm_tbci5ospk5nnn f2; } gm_tckdobunaulee;
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_tdlpv0q69lov1) == 32 && _Alignof(gm_tdlpv0q69lov1) == 8 && offsetof(gm_tdlpv0q69lov1, f0) == 0 && offsetof(gm_tdlpv0q69lov1, f1) == 24, "layout of `?ValTy`");
typedef struct { gm_tbf8u1b86cgmi f0; uint32_t f1; uint8_t p2[4]; gm_trqfvmjtt4sij f2; uint64_t f3; } gm_tehgl6lkqgvt0;
_Static_assert(sizeof(gm_teo9d4mncjfdu) == 24 && _Alignof(gm_teo9d4mncjfdu) == 8 && offsetof(gm_teo9d4mncjfdu, f0) == 0 && offsetof(gm_teo9d4mncjfdu, f1) == 16, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_tflpan0ud4tim) == 28 && _Alignof(gm_tflpan0ud4tim) == 4 && offsetof(gm_tflpan0ud4tim, f0) == 0 && offsetof(gm_tflpan0ud4tim, f1) == 24, "layout of `?FmtSpec`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t419mo45g7j0a v0; void * v1; } f1; } gm_th1g69d7vpipg;
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; } gm_tngtuiuskn938;
_Static_assert(sizeof(gm_tpautr4br7h3l) == 48 && _Alignof(gm_tpautr4br7h3l) == 8 && offsetof(gm_tpautr4br7h3l, f0) == 0 && offsetof(gm_tpautr4br7h3l, f1) == 16 && offsetof(gm_tpautr4br7h3l, f2) == 24 && offsetof(gm_tpautr4br7h3l, f3) == 40, "layout of `StructDef`");
_Static_assert(sizeof(gm_tq7qjpkmqmc5s) == 104 && _Alignof(gm_tq7qjpkmqmc5s) == 8 && offsetof(gm_tq7qjpkmqmc5s, f0) == 0 && offsetof(gm_tq7qjpkmqmc5s, f1) == 8 && offsetof(gm_tq7qjpkmqmc5s, f2) == 24 && offsetof(gm_tq7qjpkmqmc5s, f3) == 32 && offsetof(gm_tq7qjpkmqmc5s, f4) == 40 && offsetof(gm_tq7qjpkmqmc5s, f5) == 48 && offsetof(gm_tq7qjpkmqmc5s, f6) == 56 && offsetof(gm_tq7qjpkmqmc5s, f7) == 80 && offsetof(gm_tq7qjpkmqmc5s, f8) == 96, "layout of `GlobalInfo`");
_Static_assert(sizeof(gm_tqgtd5gtgj65s) == 40 && _Alignof(gm_tqgtd5gtgj65s) == 8 && offsetof(gm_tqgtd5gtgj65s, f0) == 0 && offsetof(gm_tqgtd5gtgj65s, f1) == 8, "layout of `Unsup!()`");
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t419mo45g7j0a v1; void * v2; } f1; } gm_tsth9qu1dv0g3;
_Static_assert(sizeof(gm_tt8o5kebou62r) == 32 && _Alignof(gm_tt8o5kebou62r) == 8 && offsetof(gm_tt8o5kebou62r, f0) == 0 && offsetof(gm_tt8o5kebou62r, f1) == 16, "layout of `(op: Builtin, args: []Expr)`");
typedef struct { gm_tehgl6lkqgvt0 f0; uint8_t f1; uint8_t pe[7]; } gm_t1g28mkc78m0c;
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_tckdobunaulee) == 72 && _Alignof(gm_tckdobunaulee) == 8 && offsetof(gm_tckdobunaulee, f0) == 0 && offsetof(gm_tckdobunaulee, f1) == 16 && offsetof(gm_tckdobunaulee, f2) == 24, "layout of `VariantDef`");
_Static_assert(sizeof(gm_tehgl6lkqgvt0) == 104 && _Alignof(gm_tehgl6lkqgvt0) == 8 && offsetof(gm_tehgl6lkqgvt0, f0) == 0 && offsetof(gm_tehgl6lkqgvt0, f1) == 48 && offsetof(gm_tehgl6lkqgvt0, f2) == 56 && offsetof(gm_tehgl6lkqgvt0, f3) == 96, "layout of `Scope`");
_Static_assert(sizeof(gm_th1g69d7vpipg) == 40 && _Alignof(gm_th1g69d7vpipg) == 8 && offsetof(gm_th1g69d7vpipg, f0) == 0 && offsetof(gm_th1g69d7vpipg, f1) == 8, "layout of `WriteSink`");
_Static_assert(sizeof(gm_tngtuiuskn938) == 96 && _Alignof(gm_tngtuiuskn938) == 8 && offsetof(gm_tngtuiuskn938, f0) == 0 && offsetof(gm_tngtuiuskn938, f1) == 48, "layout of `Rat`");
typedef struct { gm_tsth9qu1dv0g3 f0; gm_b16_8 f1; } gm_to76jli3bqddq;
_Static_assert(sizeof(gm_tsth9qu1dv0g3) == 40 && _Alignof(gm_tsth9qu1dv0g3) == 8 && offsetof(gm_tsth9qu1dv0g3, f0) == 0 && offsetof(gm_tsth9qu1dv0g3, f1) == 8, "layout of `Callee`");
_Static_assert(sizeof(gm_t1g28mkc78m0c) == 112 && _Alignof(gm_t1g28mkc78m0c) == 8 && offsetof(gm_t1g28mkc78m0c, f0) == 0 && offsetof(gm_t1g28mkc78m0c, f1) == 104, "layout of `?Scope`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; double v1; void * v2; void * v3; uint8_t v4; gm_b16_8 v5; gm_b16_8 v6; uint32_t v7; gm_tkn42u7na3t81 v8; gm_tkn42u7na3t81 v9; gm_t88rcucg1b7uk v10; gm_t3rcjibl8kkg4 v11; gm_t08olq97rscn3 v12; uint32_t v13; gm_tcc1rq4n1avb2 v14; gm_t3877pk7lv748 v15; void * v16; gm_tbl8il92l97ut v17; gm_tlcl4u558l6p5 v18; gm_tao63pk3r18v5 v19; void * v20; void * v21; void * v22; void * v23; uint32_t v24; uint32_t v25; uint32_t v26; gm_t8ghgl2jj0qha v27; gm_trl9d7m1k5lbd v28; gm_ta612fil812pm v29; gm_t8jsc4n9vp2ib v30; gm_tt8o5kebou62r v31; gm_to76jli3bqddq v32; gm_b16_8 v33; gm_b16_8 v34; void * v35; gm_ttggq7ng4kp3p v36; void * v37; gm_tsplfr33dsvji v38; gm_t3dcoc7077t1q v39; gm_t083kd7b1kkag v40; gm_tlcl4u558l6p5 v41; gm_tkaapcii86udk v42; gm_tlcl4u558l6p5 v43; void * v44; gm_trl2khmuphncd v45; gm_t710lsrq1m8gk v46; gm_teo9d4mncjfdu v47; gm_te9hrk0u35rev v48; gm_ttggq7ng4kp3p v49; void * v50; gm_t3mf58gv9kf3n v51; void * v52; void * v53; void * v54; gm_tandvbt63uk7n v55; gm_t710lsrq1m8gk v56; } f1; } gm_teti411eujvf4;
_Static_assert(sizeof(gm_to76jli3bqddq) == 56 && _Alignof(gm_to76jli3bqddq) == 8 && offsetof(gm_to76jli3bqddq, f0) == 0 && offsetof(gm_to76jli3bqddq, f1) == 40, "layout of `(callee: Callee, args: []Expr)`");
_Static_assert(sizeof(gm_teti411eujvf4) == 64 && _Alignof(gm_teti411eujvf4) == 8 && offsetof(gm_teti411eujvf4, f0) == 0 && offsetof(gm_teti411eujvf4, f1) == 8, "layout of `ExprKind`");
struct gm_fr_addr_of_hiaqkn0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_adt_def_ni54he1 {
    gm_frame h;
    void * ret;
};
struct gm_fr_aggregate_fkp0pdv {
    gm_frame h;
    gm_trqfvmjtt4sij l4; /* vals */
    gm_trqfvmjtt4sij l9; /* parts */
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_align_of_k6eqqrn {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_align_uu2harl {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_any_child_ffc8h1k {
    gm_frame h;
    gm_closure l1; /* pred */
    uint8_t l2; /* found */
    uint8_t ret;
};
struct gm_fr_array_60b0dn9 {
    gm_frame h;
    gm_trqfvmjtt4sij l4; /* vals */
    gm_trqfvmjtt4sij l8; /* parts */
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_array_fill_ppt75jf {
    gm_frame h;
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_bits_aggregate_bu0ircf {
    gm_frame h;
    gm_trqfvmjtt4sij l5; /* vals */
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_bits_field_os47f03 {
    gm_frame h;
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_bits_layout_23rect9 {
    gm_frame h;
    gm_t57euh612ligr ret;
};
struct gm_fr_blob_d5apnrc {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_blob_over_qs5fusr {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_bool_ty_kotvo8t {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_bool_ty_pn2p277 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_bound_n4p0vpb {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_break_expr_71qc2ca {
    gm_frame h;
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_by_ref_fi0o860 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_check_unwind_2hji4bl {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_clear_outside_q01mr8i {
    gm_frame h;
    gm_trqfvmjtt4sij l5; /* covered */
    gm_trqfvmjtt4sij l17; /* stores */
    gm_b0_1 ret;
};
struct gm_fr_coerce_gphlj1h {
    gm_frame h;
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_continue_expr_6bhkncl {
    gm_frame h;
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_else_expr_lvoo6l1 {
    gm_frame h;
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_emit_buf_mqofii1 {
    gm_frame h;
    gm_th1g69d7vpipg l1; /* sink */
    gm_tqgtd5gtgj65s ret;
};
struct gm_fr_emit_to_o7js99p {
    gm_frame h;
    gm_th1g69d7vpipg l1; /* sink */
    gm_tqgtd5gtgj65s ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_6b65ik7 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_jjs3scl {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_lv6i4jh {
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
struct gm_fr_expr_node_sj12d0p {
    gm_frame h;
    gm_tbci5ospk5nnn l3; /* v */
    gm_tsth9qu1dv0g3 l54; /* callee */
    gm_t710lsrq1m8gk l84; /* x */
    gm_t710lsrq1m8gk l89; /* w */
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_expr_porbc9t {
    gm_frame h;
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_extend_0cs85gs {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_find_with_gcs5p4r {
    gm_frame h;
    uint32_t l1; /* label */
    gm_t710lsrq1m8gk l2; /* w */
    uint8_t ret;
};
struct gm_fr_float_lit_26lt2tp {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_fn_7u87bne {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_dfds699 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_gdv4o16 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_fn_okfag60 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_fresh_5kokjmg {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_from_int_ofjuoqh {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* i */
    gm_tngtuiuskn938 ret;
};
struct gm_fr_if_expr_fn1cpni {
    gm_frame h;
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_index_check_m2p0u1k {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_int_ci6airg {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_int_lit_ai37456 {
    gm_frame h;
    gm_tbci5ospk5nnn l2; /* v */
    gm_b16_8 ret;
};
struct gm_fr_int_ty_7nma1b7 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_is_borrowed_qi4apdt {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_never_in5ftma {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_temp_rrllgdr {
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
struct gm_fr_is_void_vs2outo {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_with_label_rskslsd {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_zero_8tcct9o {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
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
struct gm_fr_layout_of_e6hfj1r {
    gm_frame h;
    gm_tbccvikg6b96e ret;
};
struct gm_fr_line_l6u87p9 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_list_26fbt7s {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_epvducg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_s83jn2q {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_v5d7fqe {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_load_0hebde7 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_loc_u9c0aqb {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_logic_aafpj13 {
    gm_frame h;
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_loop_e5nllj2 {
    gm_frame h;
    gm_tehgl6lkqgvt0 l5; /* scope */
    gm_t5o0tpv5jka2l l6; /* r */
    gm_tehgl6lkqgvt0 l7; /* popped */
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_make_none_v2s8s0l {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_make_res_riv19po {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_make_slice_sa50efe {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_make_some_9aqq928 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_make_variant_srnk8ha {
    gm_frame h;
    gm_tckdobunaulee l7; /* variant */
    gm_trqfvmjtt4sij l9; /* parts */
    gm_b16_8 ret;
};
struct gm_fr_mask_epjl3to {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_no_writes_p1jjjfj {
    gm_frame h;
    uint8_t l2; /* ok */
    uint8_t ret;
};
struct gm_fr_offsets_eh6vfle {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_one_08qg1op {
    gm_frame h;
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_opt_no_writes_fjqh1hi {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_opt_present_joptd3k {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_place_addr_7ji15ot {
    gm_frame h;
    gm_tq7qjpkmqmc5s l5; /* gl */
    gm_tpautr4br7h3l l16; /* sd */
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_pop_g2usrvd {
    gm_frame h;
    gm_tehgl6lkqgvt0 l2; /* x */
    gm_t1g28mkc78m0c ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_ptr_bytes_eocneng {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_ptr_t1h2m2d {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_push_3ef746b {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_p5tbf4u {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_pcqb56b {
    gm_frame h;
    gm_tehgl6lkqgvt0 l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_t83rg2m {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_u0rp7b1 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_vd1290p {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_0jmnnpk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_3cp9o8d {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_cvgrcrj {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_gb34qsp {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_hkmvuvc {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_r9l0iqd {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_res_is_err_11p8qlp {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_return_expr_ts5pbp2 {
    gm_frame h;
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_show_5qbi623 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_show_kmjeh8s {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* out */
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
struct gm_fr_slice_5dt9121 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_slice_expr_bln8glj {
    gm_frame h;
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_slice_len_a3383br {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_slice_ptr_hojfgr5 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_spec_literal_lmkpbc1 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_split_u128_vrmr6en {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* v */
    gm_t793vlgpa3b88 ret;
};
struct gm_fr_starts_with_6vvt01e {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_store_at7m90t {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_string_ty_j74ff0o {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_string_ty_kk94re3 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_target_ret_q7qjs0g {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_temp_piece_572g74q {
    gm_frame h;
    gm_t9geo0l9gngfn ret;
};
struct gm_fr_trap_digu1sk {
    gm_frame h;
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_u8_ty_afkvj25 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
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
struct gm_fr_unwrap_9lhqi3k {
    gm_frame h;
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_uses_effect_7j7sn5a {
    gm_frame h;
    void * l0; /* prog */
    void * l2; /* effect_ */
    gm_tsth9qu1dv0g3 l4; /* callee */
    uint8_t ret;
};
struct gm_fr_usize_ty_9mijhtc {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_usize_ty_qnrd30f {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_view_addr_nj9md2j {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_write_1uql06r {
    gm_frame h;
    gm_trqfvmjtt4sij l5; /* vals */
    gm_tddtjfggdat0d l18; /* sig */
    gm_t5o0tpv5jka2l ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eq929lennfd3(const uint8_t *a, const uint8_t *b);
inline bool gm_eq988v99hgkn(const uint8_t *a, const uint8_t *b);
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b);
inline bool gm_eq9oqmmiu8qc(const uint8_t *a, const uint8_t *b);
inline bool gm_eqau1qc90fpr(const uint8_t *a, const uint8_t *b);
inline bool gm_eqb2nno7n67r(const uint8_t *a, const uint8_t *b);
inline bool gm_eqd5a8dm2ae7(const uint8_t *a, const uint8_t *b);
inline bool gm_eqfavatugqhv(const uint8_t *a, const uint8_t *b);
inline bool gm_eqfji27tihfo(const uint8_t *a, const uint8_t *b);
inline bool gm_eqhcfklnokct(const uint8_t *a, const uint8_t *b);
inline bool gm_eqssuf05leel(const uint8_t *a, const uint8_t *b);
inline bool gm_eqvs1pd373b1(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) gm_b16_8 gm_f_addr_name_g5lrlt3(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_addr_of_hiaqkn0(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void * gm_f_adt_def_ni54he1(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void * gm_f_adt_of_gej99vn(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_aggregate_fkp0pdv(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) uint64_t gm_f_align_of_k6eqqrn(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) uint64_t gm_f_align_uu2harl(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) uint8_t gm_f_any_child_ffc8h1k(void * a0, gm_closure a1);
__attribute__((aligned(4))) void gm_f_array_60b0dn9(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_array_fill_ppt75jf(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) void gm_f_binary_k69ukj0(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_ts6oqppjh0ba3 a2, void * a3, void * a4);
__attribute__((aligned(4))) unsigned __int128 gm_f_bits128_jigkui4(const void * a0);
__attribute__((aligned(4))) void gm_f_bits_aggregate_bu0ircf(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_bits_field_os47f03(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint32_t a2);
inline __attribute__((aligned(4))) void gm_f_bits_layout_23rect9(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_bits_layout_u6kkccp(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_bits_raw_e0qg9mb(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_bits_value_u15lfan(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_blob_d5apnrc(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_blob_over_qs5fusr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_block_31qi85g(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_bool_ty_kotvo8t(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_bool_ty_pn2p277(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_bound_n4p0vpb(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_break_expr_71qc2ca(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) uint8_t gm_f_builds_in_place_ae1b90u(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_builtin_s7ark4p(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tvdg94bk7aa22 a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) uint8_t gm_f_by_ref_fi0o860(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_call_closure_mha9p1e(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_b16_8 a4, gm_t88rcucg1b7uk a5);
__attribute__((aligned(4))) void gm_f_call_nk38pfa(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) void gm_f_cancel__isj44kg(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) uint64_t gm_f_chain_links_ma0qg8l(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_chain_operand_m986l4t(void *gm_out, void * a0, void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_check_unwind_2hji4bl(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_clear_outside_q01mr8i(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_b16_8 a3);
__attribute__((aligned(4))) void gm_f_coerce_gphlj1h(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tmn7n0n9raou5 a2, void * a3);
__attribute__((aligned(4))) void gm_f_conds_79r4f4d(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_const_storage_0d7nj5d(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void gm_f_continue_expr_6bhkncl(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_te9hrk0u35rev a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_cty_66ee0kf(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_desc_0cag2kq(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) uint32_t gm_f_effect_id_5588nqa(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_else_expr_lvoo6l1(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) void gm_f_emit_buf_mqofii1(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, const void * a2, gm_b16_8 a3, void * a4);
__attribute__((aligned(4))) void gm_f_emit_chain_iu95l01(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_emit_to_o7js99p(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, const void * a2, gm_b16_8 a3, void * a4);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_6b65ik7(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_jjs3scl(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lv6i4jh(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rcd2jln(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_s1lbe5s(void);
__attribute__((aligned(4))) gm_b0_1 gm_f_end_scope_45ar5qv(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, uint8_t a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_equal_hcuag9d(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_b16_8 a3);
__attribute__((aligned(4))) void gm_f_expr_into_dn9kavo(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2, gm_tnrltbc61i6v1 a3);
__attribute__((aligned(4))) void gm_f_expr_node_sj12d0p(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_expr_porbc9t(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint8_t gm_f_find_with_gcs5p4r(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_float_lit_26lt2tp(void *env, gm_node *cx, gm_node *inst, uint16_t a0, double a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_7u87bne(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_dfds699(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_fn_format_stub_53r45gt(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint8_t gm_f_fn_gdv4o16(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_fn_name_6n34573(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
inline __attribute__((aligned(4))) uint8_t gm_f_fn_okfag60(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_fn_sink_stub_5ipff9h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_for_each_child_fm49s73(void * a0, gm_closure a1);
__attribute__((aligned(4))) void gm_f_format_0017fui(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_08505jg(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_0dl6hna(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_0gjpujh(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_0lf0h5l(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_12uv53h(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_1amnh72(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_1coo7na(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_29fhse4(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_2hfpta7(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_32n178h(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_3mj8462(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_3vtl3te(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_40ni57j(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_4g0prpe(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_4un19t8(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_5b908c8(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_6df13cd(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_7lru3c2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_7peeku4(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_8933odu(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_8b7o2bp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_8lfevbr(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_8sjjbd9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_8sn6h96(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_9c0uitb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_avab68e(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_be3fee1(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_bofepup(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_c2otgb4(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_ccpi8au(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_da6unq6(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_dp1qh3k(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_elsuh0a(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_fk1icrl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_fsfutor(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_gjujk09(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_h6b2ofo(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_he684oe(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_ij8u0vj(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_iq6v6ia(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_j7jgble(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_jb7bqji(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_jet3e97(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_jha63mc(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_jlajof4(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_mgf81l5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_mhned9o(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_nd352q9(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_nptl5af(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_oikdiom(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_p7hqke6(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_p7tc45u(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_q2e4fnf(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_qr5c6un(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_rvm8ibl(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_sa1940l(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_sdje4ev(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_t9gsufu(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_tbdgr6h(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_tcr2jpk(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_tg9hp73(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_tt4n370(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_vjen3ng(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_voj344o(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_frame_field_nj8m22n(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_frame_up_g8486eu(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
inline __attribute__((aligned(4))) uint32_t gm_f_fresh_5kokjmg(void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_fresh_tmp_6tadl69(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void gm_f_from_int_ofjuoqh(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_from_u64_ud13q5v(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0);
__attribute__((aligned(4))) void * gm_f_func_in17r5n(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_global_index_nhjk80t(void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_global_tag_gs7s3p6(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_if_expr_fn1cpni(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) void gm_f_in_expr_6j2ajh7(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, uint8_t a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_indent_line_b3mckqa(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_index_check_m2p0u1k(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_b16_8 a2, gm_b16_8 a3, gm_b16_8 a4);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_int_ci6airg(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, uint16_t a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_int_cty_s62af0i(uint8_t a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_int_info_2tcdf1n(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_int_lit_ai37456(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, const void * a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_int_ty_7nma1b7(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, uint16_t a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_intern_ia19mhl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_borrowed_qi4apdt(void * a0, uint32_t a1);
__attribute__((aligned(4))) uint8_t gm_f_is_default_spec_cdd8jng(void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_never_in5ftma(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_is_plain_qr08i40(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) uint8_t gm_f_is_temp_rrllgdr(gm_b16_8 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_unit_17uennd(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_unit_n83ttnb(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_is_vector_fm7q9br(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_void_vs2outo(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_is_with_label_rskslsd(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint32_t a2);
inline __attribute__((aligned(4))) uint8_t gm_f_is_zero_8tcct9o(const void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_jump_pvnkvme(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tf5srh4mp4b3i a2);
inline __attribute__((aligned(4))) void gm_f_kind_ghvav22(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_label_710oev4(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_layout_a5ovnrm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_layout_of_e6hfj1r(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_line_l6u87p9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_3dvovo0(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_584jv2g(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_6f120gc(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_amf91ik(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_aoq4lf0(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_caq6p77(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_cf5thm0(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_eiea5ou(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_f1jrb6n(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_fch1rvr(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_g6msml8(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_g6n88t2(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_glh29gu(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_gnj41u6(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_h995d88(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_hg1enfq(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_iin7hfk(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_k4dha91(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_knskrol(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_lhoje7n(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_lot5c36(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_m7tlafn(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_o1esdvv(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_q912sub(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_qch21ro(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_qjbq226(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_sgpievq(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_sm7hlk3(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_t20u0i3(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_t58vnnv(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_ubknr7g(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_uc25pl5(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_um0rj1a(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_uu8i6ue(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_v3hik15(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_v5l63rv(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_list_26fbt7s(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_s83jn2q(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_v5d7fqe(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b16_8 gm_f_load_0hebde7(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_loc_u9c0aqb(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_local_addr_g9lkr6l(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_local_lv_m2jtf0n(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_logic_aafpj13(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, void * a2, void * a3);
__attribute__((aligned(4))) void gm_f_loop_e5nllj2(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_loop_scope_ee94ihm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b16_8 gm_f_make_none_v2s8s0l(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_make_res_riv19po(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, uint8_t a2, gm_t88rcucg1b7uk a3, gm_b16_8 a4);
__attribute__((aligned(4))) gm_b16_8 gm_f_make_slice_sa50efe(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b16_8 gm_f_make_some_9aqq928(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_make_variant_srnk8ha(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, uint64_t a2, const void * a3);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_mask_epjl3to(void *env, gm_node *cx, gm_node *inst, uint32_t a0);
__attribute__((aligned(4))) void gm_f_match_expr_4m6qrp7(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) uint8_t gm_f_no_defers_hci755r(void * a0);
__attribute__((aligned(4))) uint8_t gm_f_no_writes_p1jjjfj(void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_normalize_jch381e(void *env, gm_node *cx, gm_node *inst, gm_tuglhjjmj2g58 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_offsets_eh6vfle(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_one_08qg1op(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) uint8_t gm_f_opt_no_writes_fjqh1hi(void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_opt_present_joptd3k(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_out_stub_ils4a69(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint32_t a2, uint32_t a3, gm_b16_8 a4, gm_t88rcucg1b7uk a5);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_param_index_rp1nn6k(void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_perform_t77ghfg(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, uint32_t a3, gm_b16_8 a4, gm_b16_8 a5, gm_t88rcucg1b7uk a6);
__attribute__((aligned(4))) gm_b16_8 gm_f_piece_stub_m8mb7rc(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint8_t a2, gm_t88rcucg1b7uk a3);
__attribute__((aligned(4))) void gm_f_place_addr_7ji15ot(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_g2usrvd(void *gm_out, void * a0);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
inline __attribute__((aligned(4))) uint64_t gm_f_ptr_bytes_eocneng(void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_ptr_t1h2m2d(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) uint8_t gm_f_ptr_type_plain_sanetd4(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_p5tbf4u(void * a0, gm_t2lqu8jsj511d a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_pcqb56b(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_t83rg2m(void * a0, uint8_t a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_u0rp7b1(void * a0, gm_csc7lc6jlkqkuq a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_vd1290p(void * a0, uint8_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_queue_cd51r8c(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) uint8_t gm_f_reads_place_nji55rm(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0jmnnpk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_3cp9o8d(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_cvgrcrj(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_gb34qsp(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_hkmvuvc(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_r9l0iqd(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
__attribute__((aligned(4))) void gm_f_repr_53quqki(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_res_is_err_11p8qlp(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_resume__rhq91rs(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, void * a3);
__attribute__((aligned(4))) void gm_f_return_expr_ts5pbp2(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint32_t a2, void * a3);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_5qbi623(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) uint64_t gm_f_size_dmhtkib(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) uint64_t gm_f_size_of_k0fluj2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_slice_5dt9121(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void gm_f_slice_expr_bln8glj(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, void * a3, void * a4, uint8_t a5);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_slice_len_a3383br(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_slice_ptr_hojfgr5(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_spec_literal_lmkpbc1(void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_split_u128_vrmr6en(void *gm_out, const void * a0);
__attribute__((aligned(4))) gm_t793vlgpa3b88 gm_f_split_u128_vrmr6en_c(gm_tbci5ospk5nnn a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_src_loc_lrhtj7i(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_start_exit_m57dnn5(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, gm_b16_8 a2, gm_b16_8 a3, gm_b16_8 a4);
inline __attribute__((aligned(4))) uint8_t gm_f_starts_with_6vvt01e(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_static_bytes_u0uvco3(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_store_at7m90t(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_string_ty_j74ff0o(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_string_ty_kk94re3(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_target_ret_q7qjs0g(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_temp_piece_572g74q(void *gm_out, gm_b16_8 a0);
__attribute__((aligned(4))) gm_t9geo0l9gngfn gm_f_temp_piece_572g74q_c(gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_tmp_uninit_jr39s5u(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_tmp_uohsptq(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) double gm_f_to_f64_4avhrht(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_to_i128_o7ud1ue(void *gm_out, const void * a0);
__attribute__((aligned(4))) void gm_f_trap_digu1sk(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) void gm_f_try_expr_ln1m7b8(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, gm_t9805qtonbsag a3, uint32_t a4);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_u8_ty_afkvj25(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_unary_i1ekgvj(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t9qkh0rsooc8d a2, void * a3);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unit_2age34u(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_unit_8ekedsl(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unit_ty_vgceilu(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void gm_f_unsupported_531q51r(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_unwind_call_4k8ulav(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_unwind_ladder_a9ij1gj(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_unwrap_9lhqi3k(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, uint8_t a3);
__attribute__((aligned(4))) uint8_t gm_f_uses_effect_7j7sn5a(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_usize_ty_9mijhtc(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_usize_ty_qnrd30f(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_variant_equal_bgud9j0(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) void gm_f_vector_binary_etnngue(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_ts6oqppjh0ba3 a2, void * a3, void * a4);
__attribute__((aligned(4))) void gm_f_vector_unary_h7ar7ml(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t9qkh0rsooc8d a2, void * a3);
__attribute__((aligned(4))) void gm_f_view_addr_nj9md2j(void *gm_out, gm_b16_8 a0);
__attribute__((aligned(4))) gm_th2lqbo77pvsc gm_f_view_addr_nj9md2j_c(gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_with_expr_l1uk88k(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint32_t a2, gm_b16_8 a3, void * a4, void * a5);
__attribute__((aligned(4))) void gm_f_write_1uql06r(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_f1cbljc(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_zero_tlk13aa(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
extern uint8_t gm_cjs2aavhl2o[8] __attribute__((aligned(8)));
extern const uint8_t gm_s02uuedsru1qu[9];
extern const uint8_t gm_s04qus0luoga5[5];
extern const uint8_t gm_s0fg7tcl0p4ij[13];
extern const uint8_t gm_s0m0qaokk51g0[4];
extern const uint8_t gm_s18dhen1d87oi[20];
extern const uint8_t gm_s1k3lfare8sei[4];
extern const uint8_t gm_s2cuflhqkkuaq[2];
extern const uint8_t gm_s3ierue9bs27g[46];
extern const uint8_t gm_s43jqu37ghtoq[19];
extern const uint8_t gm_s4cess8gum6i7[6];
extern const uint8_t gm_s4inmcuim4088[46];
extern const uint8_t gm_s4s2nr1qc94po[17];
extern const uint8_t gm_s5pra70i8o734[14];
extern const uint8_t gm_s7hi4f3a8v74u[13];
extern const uint8_t gm_s81vh0gpuu9rd[36];
extern const uint8_t gm_s866ofni9sb8u[7];
extern const uint8_t gm_s8tfl6snglqci[19];
extern const uint8_t gm_sb8kl54a499bf[7];
extern const uint8_t gm_scaaj480d4i26[9];
extern const uint8_t gm_scj0sm2kpivk4[9];
extern const uint8_t gm_sd5qvckqda7oh[13];
extern const uint8_t gm_sf9a2kg3eo1gs[2];
extern const uint8_t gm_sfrlts7iu6kt3[23];
extern const uint8_t gm_shpi1i86ollcb[59];
extern const uint8_t gm_si2pj4mpn1kn5[11];
extern const uint8_t gm_sicgmpca63jln[2];
extern const uint8_t gm_sk7tgk6pi62vu[14];
extern const uint8_t gm_sk8b751vs73ev[67];
extern const uint8_t gm_skf0mm42l0tsk[17];
extern const uint8_t gm_skolmnfc0ccbd[2];
extern const uint8_t gm_sl576h51kqj52[24];
extern const uint8_t gm_slmqf5nrs9f6b[13];
extern const uint8_t gm_sm3nel32emjc9[10];
extern const uint8_t gm_smr5ddngcjuao[19];
extern const uint8_t gm_smuhqaa14mehe[16];
extern const uint8_t gm_somhu4vltiil1[4];
extern const uint8_t gm_spm6tgoorr19q[29];
extern const uint8_t gm_sr3mniof9fusf[12];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src0gtuh94b1fce[];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcc24pnihhgkug[];
extern const char gm_srcd5arorrf8jkg[];
extern const char gm_srcmok80ho8hh01[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_sugv53k5dm8d7[24];
extern const uint8_t gm_svubtddeidu1d[2];
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
inline bool gm_eq929lennfd3(const uint8_t *a, const uint8_t *b) {
    return (a[0] != 0) == (b[0] != 0);
}
inline bool gm_eq988v99hgkn(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq467d5cm1d5(a + 0, b + 0)) return false;
    if (!gm_eq0iqiubfm7e(a + 8, b + 8)) return false;
    return true;
}
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq467d5cm1d5(a + 0, b + 0)) return false;
    return true;
}
inline bool gm_eq9oqmmiu8qc(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint16_t, a) == GM_LD(uint16_t, b);
}
inline bool gm_eqau1qc90fpr(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    if (ta == (uint8_t)0) return gm_eq9m6mvtmgde(a + 8, b + 8);
    if (ta == (uint8_t)1) return gm_eqfavatugqhv(a + 8, b + 8);
    return true;
}
inline bool gm_eqb2nno7n67r(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint64_t, a) == GM_LD(uint64_t, b);
}
inline bool gm_eqd5a8dm2ae7(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq467d5cm1d5(a + 0, b + 0)) return false;
    if (!gm_eqfji27tihfo(a + 8, b + 8)) return false;
    return true;
}
inline bool gm_eqfavatugqhv(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    if (ta == (uint8_t)0) return gm_eqssuf05leel(a + 8, b + 8);
    if (ta == (uint8_t)1) return gm_eq988v99hgkn(a + 8, b + 8);
    if (ta == (uint8_t)3) return gm_eq0iqiubfm7e(a + 8, b + 8);
    return true;
}
inline bool gm_eqfji27tihfo(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    for (uintptr_t i = 0; i < n; i++) if (!gm_eqau1qc90fpr(pa + i * 40, pb + i * 40)) return false;
    return true;
}
inline bool gm_eqhcfklnokct(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    if (ta == (uint8_t)0) return gm_eqvs1pd373b1(a + 8, b + 8);
    if (ta == (uint8_t)1) return gm_eq9oqmmiu8qc(a + 8, b + 8);
    return true;
}
inline bool gm_eqssuf05leel(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq9m6mvtmgde(a + 0, b + 0)) return false;
    if (!gm_eq0iqiubfm7e(a + 8, b + 8)) return false;
    return true;
}
inline bool gm_eqvs1pd373b1(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq929lennfd3(a + 0, b + 0)) return false;
    if (!gm_eq467d5cm1d5(a + 4, b + 4)) return false;
    if (!gm_eqb2nno7n67r(a + 8, b + 8)) return false;
    return true;
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
inline __attribute__((aligned(4))) uint64_t gm_f_align_of_k6eqqrn(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_align_of_k6eqqrn F;
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
    uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)&t4) + 8));
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
inline __attribute__((aligned(4))) uint64_t gm_f_align_uu2harl(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_align_uu2harl F;
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
    uint64_t t4 = gm_f_align_of_k6eqqrn((void *)NULL, cx, NULL, t2, t3);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_blob_d5apnrc(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_blob_d5apnrc F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* n */
    GM_STACK_CHECK(&F, 177u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        gm_t88rcucg1b7uk t3 = gm_l1;
        gm_b16_8 t4 = gm_f_tmp_uninit_jr39s5u((void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l2 = t4;
    }
    {
        void * t5 = gm_l0;
        gm_t6j6smj7qoctu t6;
        gm_b16_8 t7 = gm_l2;
        GM_ST(gm_b16_8, (uint8_t *)&t6 + 0, t7);
        gm_b16_8 t8 = gm_l2;
        GM_ST(gm_b16_8, (uint8_t *)&t6 + 16, t8);
        gm_b0_1 t9 = gm_f_linef_cf5thm0((void *)NULL, cx, NULL, t5, &t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t10 = gm_l2;
        uint8_t t11 = 0;
        F.ret = t10;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_blob_over_qs5fusr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2) {
    struct gm_fr_blob_over_qs5fusr F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* parts */
    gm_b16_8 gm_l3; /* n */
    GM_STACK_CHECK(&F, 165u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        gm_t88rcucg1b7uk t3 = gm_l1;
        gm_b16_8 t4 = gm_f_tmp_uninit_jr39s5u((void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l3 = t4;
    }
    {
        void * t5 = gm_l0;
        gm_t88rcucg1b7uk t6 = gm_l1;
        gm_b16_8 t7 = gm_l3;
        gm_b16_8 t8 = gm_l2;
        gm_b0_1 t9 = gm_f_clear_outside_q01mr8i((void *)NULL, cx, NULL, t5, t6, t7, t8);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t10 = gm_l3;
        uint8_t t11 = 0;
        F.ret = t10;
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_bool_ty_kotvo8t(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_bool_ty_kotvo8t F;
    void * gm_l0; /* ty */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    void * t3 = (void *)(((uint8_t *)t2) + 8);
    gm_t88rcucg1b7uk t4 = gm_f_bool_ty_pn2p277((void *)NULL, cx, NULL, t3);
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_bool_ty_pn2p277(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_bool_ty_pn2p277 F;
    void * gm_l0; /* t */
    GM_STACK_CHECK(&F, 84u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_t53at80pfdus3 t2;
    memset(&t2, 0, sizeof t2);
    GM_ST(uint8_t, ((uint8_t *)&t2 + 0), ((uint8_t)4ULL));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_6b65ik7(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_6b65ik7 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_jjs3scl(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_jjs3scl F;
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
inline __attribute__((aligned(4))) uint8_t gm_f_find_with_gcs5p4r(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_find_with_gcs5p4r F;
    void * gm_l0; /* e */
    GM_STACK_CHECK(&F, 103u);
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
            uint32_t t8 = GM_LD(uint32_t, (((uint8_t *)&F.l2) + 0));
            uint32_t t9 = F.l1;
            uint8_t t10 = ((t8 == t9));
            if (!t10) goto ie7;
            {
                uint8_t t12 = 0;
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
        void * t13 = gm_l0;
        gm_closure t14 = ((gm_closure){(void *)gm_f_fn_gdv4o16, (void *)&F.h, cx});
        uint8_t t15 = gm_f_any_child_ffc8h1k(t13, t14);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t16 = 0;
        F.ret = t15;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_dfds699(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_dfds699 F;
    void * gm_l0; /* c */
    GM_STACK_CHECK(&F, 19u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t2 = 0;
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_no_writes_p1jjjfj *)((&F.h)->up))->l2));
        if (t3) {
            void * t4 = gm_l0;
            uint8_t t5 = gm_f_no_writes_p1jjjfj(t4);
            t2 = t5;
        } else t2 = 0;
        GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_no_writes_p1jjjfj *)((&F.h)->up))->l2), t2);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_fn_gdv4o16(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_gdv4o16 F;
    void * gm_l0; /* c */
    GM_STACK_CHECK(&F, 23u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    uint32_t t2 = GM_LD(uint32_t, ((uint8_t *)&((struct gm_fr_find_with_gcs5p4r *)((&F.h)->up))->l1));
    uint8_t t3 = gm_f_find_with_gcs5p4r(t1, t2);
    F.ret = t3;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_fn_okfag60(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_okfag60 F;
    void * gm_l0; /* c */
    GM_STACK_CHECK(&F, 35u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_uses_effect_7j7sn5a *)((&F.h)->up))->l0));
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_uses_effect_7j7sn5a *)((&F.h)->up))->l2));
    uint8_t t4 = gm_f_uses_effect_7j7sn5a((void *)NULL, cx, NULL, t1, t2, t3);
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
inline __attribute__((aligned(4))) void gm_f_from_int_ofjuoqh(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_from_int_ofjuoqh F;
    GM_STACK_CHECK(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    gm_tbci5ospk5nnn t1 = F.l0;
    gm_tbci5ospk5nnn t2;
    gm_f_one_08qg1op((void *)&t2, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_tbci5ospk5nnn, (uint8_t *)&(*(gm_tngtuiuskn938 *)gm_out) + 0, t1);
    GM_ST(gm_tbci5ospk5nnn, (uint8_t *)&(*(gm_tngtuiuskn938 *)gm_out) + 48, t2);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_index_check_m2p0u1k(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_b16_8 a2, gm_b16_8 a3, gm_b16_8 a4) {
    struct gm_fr_index_check_m2p0u1k F;
    void * gm_l0; /* fg */
    uint8_t gm_l1; /* checked */
    gm_b16_8 gm_l2; /* i */
    gm_b16_8 gm_l3; /* len */
    gm_b16_8 gm_l4; /* at */
    GM_STACK_CHECK(&F, 226u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        uint8_t t3 = gm_l1;
        if (!t3) goto ie2;
        {
            void * t5 = gm_l0;
            gm_tud7oovfvnf43 t6;
            gm_b16_8 t7 = gm_l2;
            GM_ST(gm_b16_8, (uint8_t *)&t6 + 0, t7);
            gm_b16_8 t8 = gm_l3;
            GM_ST(gm_b16_8, (uint8_t *)&t6 + 16, t8);
            gm_b16_8 t9 = gm_l2;
            GM_ST(gm_b16_8, (uint8_t *)&t6 + 32, t9);
            gm_b16_8 t10 = gm_l3;
            GM_ST(gm_b16_8, (uint8_t *)&t6 + 48, t10);
            gm_b16_8 t11 = gm_l4;
            GM_ST(gm_b16_8, (uint8_t *)&t6 + 64, t11);
            gm_b0_1 t12 = gm_f_linef_gnj41u6((void *)NULL, cx, NULL, t5, &t6);
            if (gm_unw.kind) {
                goto gm_lad;
            }
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_int_ty_7nma1b7(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, uint16_t a2) {
    struct gm_fr_int_ty_7nma1b7 F;
    void * gm_l0; /* ty */
    uint8_t gm_l1; /* signed */
    uint16_t gm_l2; /* bits */
    GM_STACK_CHECK(&F, 50u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    void * t3 = (void *)(((uint8_t *)t2) + 8);
    uint8_t t4 = gm_l1;
    uint16_t t5 = gm_l2;
    gm_t88rcucg1b7uk t6 = gm_f_int_ci6airg((void *)NULL, cx, NULL, t3, t4, t5);
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_void_vs2outo(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_void_vs2outo F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 41u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    uint8_t t1 = 0;
    void * t2 = gm_l0;
    gm_t88rcucg1b7uk t3 = gm_l1;
    uint8_t t4 = gm_f_is_unit_17uennd(t2, GM_CONV(gm_csguankko4lmhd, t3));
    if (!t4) {
        void * t5 = gm_l0;
        gm_t88rcucg1b7uk t6 = gm_l1;
        uint8_t t7 = gm_f_is_never_in5ftma(t5, GM_CONV(gm_csguankko4lmhd, t6));
        t1 = t7;
    } else t1 = 1;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_zero_8tcct9o(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_zero_8tcct9o F;
    GM_STACK_CHECK_SHALLOW(&F, 11u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint64_t t1 = GM_LD(uintptr_t, ((((uint8_t *)a0) + 8) + 0) + 8);
    uint8_t t2 = ((t1 == ((uint64_t)0ULL)));
    F.ret = t2;
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
inline __attribute__((aligned(4))) void gm_f_list_26fbt7s(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_26fbt7s F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_jjs3scl();
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
inline __attribute__((aligned(4))) void gm_f_list_v5d7fqe(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_v5d7fqe F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_6b65ik7();
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_loc_u9c0aqb(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_loc_u9c0aqb F;
    void * gm_l0; /* fg */
    void * gm_l1; /* e */
    GM_STACK_CHECK(&F, 100u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    void * t3 = gm_l1;
    gm_tl874t2atnlh3 t4 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t3) + 72));
    gm_b16_8 t5 = gm_f_src_loc_lrhtj7i((void *)NULL, cx, NULL, t2, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
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
        if (((((uint8_t *)&t10))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t10) + 8, gm_src0gtuh94b1fce, 1270, 12);
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
inline __attribute__((aligned(4))) uint8_t gm_f_opt_no_writes_fjqh1hi(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_opt_no_writes_fjqh1hi F;
    void * gm_l0; /* e */
    void * gm_l1; /* x */
    GM_STACK_CHECK(&F, 45u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t3 = gm_l0;
        if (!(GM_LD(void *, ((uint8_t *)&t3)) != 0)) goto ie2;
        void * t4 = GM_LD(void *, ((uint8_t *)&t3));
        gm_l1 = t4;
        {
            void * t6 = gm_l1;
            uint8_t t7 = gm_f_no_writes_p1jjjfj(t6);
            uint8_t t8 = 0;
            F.ret = t7;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint8_t t9 = 0;
        F.ret = ((uint8_t)1);
        goto gm_fret;
    }
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
        if (o12) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 133, 18);
        uint64_t t13 = r11;
        uint64_t t14 = (uintptr_t)t13;
        void * t15 = gm_l0;
        uint64_t t16 = GM_LD(uintptr_t, (((uint8_t *)t15) + 0) + 8);
        if (t14 >= (uintptr_t)(t16)) gm_trap_index(t14, t16, gm_srcrgc9bkquf6nr, 133, 10);
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
        if (o25) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 134, 26);
        uint64_t t26 = r24;
        uint64_t t27 = (uintptr_t)t26;
        if (t22 > t27 || t27 > (uintptr_t)t20) gm_trap_slice(t22, t27, t20, gm_srcrgc9bkquf6nr, 134, 15);
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
inline __attribute__((aligned(4))) uint64_t gm_f_ptr_bytes_eocneng(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_ptr_bytes_eocneng F;
    void * gm_l0; /* fg */
    GM_STACK_CHECK_SHALLOW(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    uint64_t t3 = GM_LD(uint64_t, ((((((uint8_t *)t2) + 8) + 8) + 0) + 0));
    F.ret = t3;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_p5tbf4u(void * a0, gm_t2lqu8jsj511d a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_p5tbf4u F;
    void * gm_l0; /* l */
    gm_t2lqu8jsj511d gm_l1; /* x */
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
            gm_b16_8 t11 = gm_f_empty_slice_6b65ik7();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_gb34qsp(t10, ((uint64_t)1ULL), t12);
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
        gm_t2lqu8jsj511d t21 = gm_l1;
        GM_ST(gm_t2lqu8jsj511d, ((uint8_t *)t20 + t18 * 16), t21);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_pcqb56b(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_pcqb56b F;
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
            gm_b16_8 t11 = gm_f_empty_slice_lv6i4jh();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_cvgrcrj(t10, ((uint64_t)1ULL), t12);
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
        gm_tehgl6lkqgvt0 t21 = GM_LD(gm_tehgl6lkqgvt0, ((uint8_t *)a1));
        GM_ST(gm_tehgl6lkqgvt0, ((uint8_t *)t20 + t18 * 104), t21);
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
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 104)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_t83rg2m(void * a0, uint8_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_t83rg2m F;
    void * gm_l0; /* l */
    uint8_t gm_l1; /* x */
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
            gm_b16_8 t11 = gm_f_empty_slice_ijtp9lo();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_3cp9o8d(t10, ((uint64_t)1ULL), t12);
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
        uint8_t t21 = gm_l1;
        GM_ST(uint8_t, ((uint8_t *)t20 + t18 * 1), t21);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_u0rp7b1(void * a0, gm_csc7lc6jlkqkuq a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_u0rp7b1 F;
    void * gm_l0; /* l */
    gm_t2lqu8jsj511d gm_l1; /* x */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t2lqu8jsj511d, a1);
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
            gm_b16_8 t11 = gm_f_empty_slice_jjs3scl();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_r9l0iqd(t10, ((uint64_t)1ULL), t12);
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
        gm_t2lqu8jsj511d t21 = gm_l1;
        GM_ST(gm_t2lqu8jsj511d, ((uint8_t *)t20 + t18 * 16), t21);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_vd1290p(void * a0, uint8_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_vd1290p F;
    void * gm_l0; /* l */
    uint8_t gm_l1; /* x */
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
            gm_b16_8 t11 = gm_f_empty_slice_s1lbe5s();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_hkmvuvc(t10, ((uint64_t)1ULL), t12);
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
        uint8_t t21 = gm_l1;
        GM_ST(uint8_t, ((uint8_t *)t20 + t18 * 1), t21);
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_cvgrcrj(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_cvgrcrj F;
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
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)104ULL), ((uint64_t)8ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_gb34qsp(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_gb34qsp F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_hkmvuvc(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_hkmvuvc F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_r9l0iqd(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_r9l0iqd F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_5qbi623(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_show_5qbi623 F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    void * t3 = (void *)(((uint8_t *)t2) + 8);
    gm_t88rcucg1b7uk t4 = gm_l1;
    gm_b16_8 t5 = gm_f_show_kmjeh8s((void *)NULL, cx, NULL, t3, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_slice_len_a3383br(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_slice_len_a3383br F;
    void * gm_l0; /* fg */
    gm_b16_8 gm_l1; /* addr */
    GM_STACK_CHECK(&F, 168u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_t3877pk7lv748 t1;
    gm_b16_8 t2 = gm_l1;
    GM_ST(gm_b16_8, (uint8_t *)&t1 + 0, t2);
    void * t3 = gm_l0;
    uint64_t t4 = gm_f_ptr_bytes_eocneng(t3);
    GM_ST(uint64_t, (uint8_t *)&t1 + 16, t4);
    gm_tabb710d9mq2e t5;
    gm_f_format_0gjpujh((void *)&t5, (void *)NULL, cx, NULL, &t1);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (((((uint8_t *)&t5))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t5) + 8, gm_src0gtuh94b1fce, 291, 72);
    gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)&t5) + 8));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_slice_ptr_hojfgr5(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_slice_ptr_hojfgr5 F;
    gm_b16_8 gm_l0; /* addr */
    GM_STACK_CHECK(&F, 136u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t1 = gm_l0;
    gm_tnpifv20irnph t2;
    GM_ST(gm_b16_8, ((uint8_t *)&t2 + 0), t1);
    gm_tabb710d9mq2e t3;
    gm_f_format_7lru3c2((void *)&t3, (void *)NULL, cx, NULL, t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (((((uint8_t *)&t3))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t3) + 8, gm_src0gtuh94b1fce, 289, 60);
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
inline __attribute__((aligned(4))) void gm_f_split_u128_vrmr6en(void *gm_out, const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_split_u128_vrmr6en F;
    unsigned __int128 gm_l1; /* m */
    GM_STACK_CHECK_SHALLOW(&F, 129u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        unsigned __int128 t2 = gm_f_bits128_jigkui4(&(*(gm_tbci5ospk5nnn *)((uint8_t *)a0)));
        gm_l1 = t2;
    }
    {
        gm_t793vlgpa3b88 t3;
        unsigned __int128 t4 = gm_l1;
        unsigned __int128 t5 = (unsigned __int128)(t4 & ((unsigned __int128)(((unsigned __int128)0u << 64) | 18446744073709551615u)));
        GM_ST(unsigned __int128, (uint8_t *)&t3 + 0, t5);
        unsigned __int128 t6 = gm_l1;
        unsigned __int128 n7 = (unsigned __int128)((uint64_t)64ULL);
        if (n7 >= 128) gm_trap_shift((uint64_t)n7, "u128", gm_src0gtuh94b1fce, 1251, 40);
        unsigned __int128 t8 = (unsigned __int128)(t6 >> (int)n7);
        GM_ST(unsigned __int128, (uint8_t *)&t3 + 16, t8);
        uint8_t t9 = 0;
        F.ret = (*(gm_t793vlgpa3b88 *)(((uint8_t *)&t3) + 0));
        goto gm_fret;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_store_at7m90t(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_b16_8 a3) {
    struct gm_fr_store_at7m90t F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* addr */
    gm_b16_8 gm_l3; /* v */
    gm_b16_8 gm_l4; /* c */
    GM_STACK_CHECK(&F, 208u);
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
        gm_t88rcucg1b7uk t5 = gm_l1;
        gm_b16_8 t6 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t4, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l4 = t6;
    }
    {
        void * t7 = gm_l0;
        gm_tfeab279c16rr t8;
        gm_b16_8 t9 = gm_l4;
        GM_ST(gm_b16_8, (uint8_t *)&t8 + 0, t9);
        gm_b16_8 t10 = gm_l2;
        GM_ST(gm_b16_8, (uint8_t *)&t8 + 16, t10);
        gm_b16_8 t11 = gm_l3;
        GM_ST(gm_b16_8, (uint8_t *)&t8 + 32, t11);
        gm_b0_1 t12 = gm_f_linef_f1jrb6n((void *)NULL, cx, NULL, t7, &t8);
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_string_ty_j74ff0o(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_string_ty_j74ff0o F;
    void * gm_l0; /* t */
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    void * t2 = gm_l0;
    gm_t88rcucg1b7uk t3 = gm_f_u8_ty_afkvj25((void *)NULL, cx, NULL, t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t88rcucg1b7uk t4 = gm_f_slice_5dt9121((void *)NULL, cx, NULL, t1, ((uint8_t)1), t3);
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_string_ty_kk94re3(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_string_ty_kk94re3 F;
    void * gm_l0; /* ty */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    void * t3 = (void *)(((uint8_t *)t2) + 8);
    gm_t88rcucg1b7uk t4 = gm_f_string_ty_j74ff0o((void *)NULL, cx, NULL, t3);
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_target_ret_q7qjs0g(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_target_ret_q7qjs0g F;
    void * gm_l0; /* fg */
    uint32_t gm_l1; /* levels */
    uint32_t gm_l2; /* f */
    GM_STACK_CHECK(&F, 125u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        uint32_t t3 = gm_l1;
        gm_tommf4j9upomc t4;
        gm_f_frame_up_g8486eu((void *)&t4, (void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tommf4j9upomc t5 = t4;
        gm_l2 = GM_LD(uint32_t, (uint8_t *)&t5 + 16);
    }
    {
        void * t6 = gm_l0;
        void * t7 = GM_LD(void *, (((uint8_t *)t6) + 0));
        void * t8 = GM_LD(void *, (((uint8_t *)t7) + 0));
        uint32_t t9 = gm_l2;
        void * t10 = gm_f_func_in17r5n((void *)NULL, cx, NULL, t8, t9);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t88rcucg1b7uk t11 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t10) + 64));
        uint8_t t12 = 0;
        F.ret = t11;
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_usize_ty_9mijhtc(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_usize_ty_9mijhtc F;
    void * gm_l0; /* ty */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    void * t3 = (void *)(((uint8_t *)t2) + 8);
    gm_t88rcucg1b7uk t4 = gm_f_usize_ty_qnrd30f((void *)NULL, cx, NULL, t3);
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_usize_ty_qnrd30f(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_usize_ty_qnrd30f F;
    void * gm_l0; /* t */
    GM_STACK_CHECK(&F, 84u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_t53at80pfdus3 t2;
    memset(&t2, 0, sizeof t2);
    GM_ST(uint8_t, ((uint8_t *)&t2 + 0), ((uint8_t)1ULL));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_addr_of_hiaqkn0(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2) {
    struct gm_fr_addr_of_hiaqkn0 F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* v */
    gm_b16_8 gm_l3; /* a */
    gm_b16_8 gm_l4; /* n */
    GM_STACK_CHECK(&F, 404u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        gm_b16_8 t3 = gm_l2;
        uint8_t t4 = gm_f_is_temp_rrllgdr(t3);
        if (!t4) goto ie2;
        {
            gm_b16_8 t6 = gm_l2;
            gm_tnpifv20irnph t7;
            GM_ST(gm_b16_8, ((uint8_t *)&t7 + 0), t6);
            gm_tabb710d9mq2e t8;
            gm_f_format_4un19t8((void *)&t8, (void *)NULL, cx, NULL, t7);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t8))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t8) + 8, gm_src0gtuh94b1fce, 37, 28);
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
        gm_b16_8 t13 = gm_l2;
        gm_th2lqbo77pvsc t14;
        gm_f_view_addr_nj9md2j((void *)&t14, t13);
        if (!((((uint8_t *)&t14))[16] != 0)) goto ie12;
        gm_b16_8 t15 = GM_LD(gm_b16_8, ((uint8_t *)&t14));
        gm_l3 = t15;
        {
            gm_b16_8 t17 = gm_l3;
            uint8_t t18 = 0;
            F.ret = t17;
            goto gm_fret;
        }
        goto ix12;
ie12:;
ix12:;
    }
    {
        void * t19 = gm_l0;
        gm_t88rcucg1b7uk t20 = gm_l1;
        gm_b16_8 t21 = gm_l2;
        gm_b16_8 t22 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t19, t20, t21);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l4 = t22;
    }
    {
        gm_b16_8 t23 = gm_l4;
        gm_tnpifv20irnph t24;
        GM_ST(gm_b16_8, ((uint8_t *)&t24 + 0), t23);
        gm_tabb710d9mq2e t25;
        gm_f_format_4un19t8((void *)&t25, (void *)NULL, cx, NULL, t24);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t25))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t25) + 8, gm_src0gtuh94b1fce, 40, 12);
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
extern __attribute__((aligned(4))) void * gm_f_adt_def_ni54he1(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_aggregate_fkp0pdv(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2) {
    struct gm_fr_aggregate_fkp0pdv F;
    void * gm_l0; /* fg */
    void * gm_l1; /* e */
    gm_b16_8 gm_l2; /* elems */
    gm_t88rcucg1b7uk gm_l3; /* t */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* i */
    gm_b16_8 gm_l8; /* offs */
    uint64_t gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* i */
    gm_b16_8 gm_l13; /* b */
    uint64_t gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    uint64_t gm_l16; /* i */
    GM_STACK_CHECK(&F, 911u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l1;
        gm_t88rcucg1b7uk t3 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t2) + 0));
        gm_l3 = t3;
    }
    {
        gm_f_list_epvducg((void *)&F.l4, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_l6 = ((uint64_t)0ULL);
        }
        {
            uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            gm_l5 = t5;
        }
        {
            for (;;) {
                uint64_t t7 = gm_l6;
                uint64_t t8 = gm_l5;
                uint8_t t9 = (t7 < t8);
                if (!t9) goto lb6;
                {
                    uint64_t t12 = gm_l6;
                    uint64_t t15 = GM_LD(uint64_t, ((uint8_t *)&t12));
                    gm_l7 = t15;
                    {
                        void * t17 = (void *)((uint8_t *)&F.l4);
                        void * t18 = gm_l0;
                        uint64_t t19 = gm_l7;
                        uint64_t t20 = (uintptr_t)t19;
                        uint64_t t21 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                        if (t20 >= (uintptr_t)(t21)) gm_trap_index(t20, t21, gm_src0gtuh94b1fce, 544, 52);
                        void * t22 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t20 * 88);
                        gm_t5o0tpv5jka2l t23;
                        gm_f_expr_porbc9t((void *)&t23, (void *)NULL, cx, NULL, t18, t22);
                        if (gm_unw.kind) {
                            goto gm_ll10;
                        }
                        if (((((uint8_t *)&t23))[0] != 0)) {
                            gm_t5o0tpv5jka2l t24;
                            GM_ST(uint8_t, (uint8_t *)&t24 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t24 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t24 + 4, 0);
                            ((uint8_t *)&t24)[0] = 1;
                            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t24 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t23) + 8)));
                            F.ret = t24;
                            goto gm_fret;
                        }
                        gm_b16_8 t25 = GM_LD(gm_b16_8, (((uint8_t *)&t23) + 8));
                        gm_tftb1odc739t5 t26 = gm_f_push_3ef746b(t17, t25);
                        if (((((uint8_t *)&t26))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t26) + 1, gm_src0gtuh94b1fce, 544, 29);
                        gm_b0_1 t27 = GM_LD(gm_b0_1, (((uint8_t *)&t26) + 1));
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_src0gtuh94b1fce, 544, 5);
me13:;
                }
lc6:;
                {
                    uint64_t t29 = gm_l6;
                    uint64_t r30; bool o31 = __builtin_add_overflow(t29, ((uint64_t)1ULL), &r30);
                    uint64_t t32 = r30;
                    gm_l6 = t32;
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
        void * t33 = gm_l0;
        void * t34 = GM_LD(void *, (((uint8_t *)t33) + 0));
        void * t35 = (void *)(((uint8_t *)t34) + 8);
        gm_t88rcucg1b7uk t36 = gm_l3;
        gm_b16_8 t37 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t35, t36);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l8 = t37;
    }
    {
        gm_f_list_26fbt7s((void *)&F.l9, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_l11 = ((uint64_t)0ULL);
        }
        {
            uint64_t t39 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            gm_l10 = t39;
        }
        {
            for (;;) {
                uint64_t t41 = gm_l11;
                uint64_t t42 = gm_l10;
                uint8_t t43 = (t41 < t42);
                if (!t43) goto lb40;
                {
                    uint64_t t46 = gm_l11;
                    uint64_t t49 = GM_LD(uint64_t, ((uint8_t *)&t46));
                    gm_l12 = t49;
                    {
                        void * t51 = (void *)((uint8_t *)&F.l9);
                        uint64_t t52 = gm_l12;
                        uint64_t t53 = (uintptr_t)t52;
                        uint64_t t54 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                        if (t53 >= (uintptr_t)(t54)) gm_trap_index(t53, t54, gm_src0gtuh94b1fce, 547, 54);
                        uint64_t t55 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l8)) + t53 * 8));
                        void * t56 = gm_l0;
                        void * t57 = GM_LD(void *, (((uint8_t *)t56) + 0));
                        void * t58 = (void *)(((uint8_t *)t57) + 8);
                        uint64_t t59 = gm_l12;
                        uint64_t t60 = (uintptr_t)t59;
                        uint64_t t61 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                        if (t60 >= (uintptr_t)(t61)) gm_trap_index(t60, t61, gm_src0gtuh94b1fce, 547, 81);
                        gm_t88rcucg1b7uk t62 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t60 * 88) + 0));
                        uint64_t t63 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t58, t62);
                        if (gm_unw.kind) {
                            goto gm_ll44;
                        }
                        gm_t2lqu8jsj511d t64;
                        GM_ST(uint64_t, ((uint8_t *)&t64 + 0), t55);
                        GM_ST(uint64_t, ((uint8_t *)&t64 + 8), t63);
                        gm_tftb1odc739t5 t65 = gm_f_push_u0rp7b1(t51, GM_CONV(gm_csc7lc6jlkqkuq, t64));
                        if (((((uint8_t *)&t65))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t65) + 1, gm_src0gtuh94b1fce, 547, 29);
                        gm_b0_1 t66 = GM_LD(gm_b0_1, (((uint8_t *)&t65) + 1));
                    }
                    goto me47;
mn48:;
                    gm_trap("no match arm matched the scrutinee", gm_src0gtuh94b1fce, 547, 5);
me47:;
                }
lc40:;
                {
                    uint64_t t68 = gm_l11;
                    uint64_t r69; bool o70 = __builtin_add_overflow(t68, ((uint64_t)1ULL), &r69);
                    uint64_t t71 = r69;
                    gm_l11 = t71;
                }
            }
gm_ll44:;
            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb40; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc40; }
            }
            goto gm_lad;
lb40:;
        }
    }
    {
        void * t72 = gm_l0;
        gm_t88rcucg1b7uk t73 = gm_l3;
        gm_b16_8 t74 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 0));
        gm_b16_8 t75 = GM_LD(gm_b16_8, ((uint8_t *)&t74));
        gm_b16_8 t76 = gm_f_blob_over_qs5fusr((void *)NULL, cx, NULL, t72, t73, t75);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l13 = t76;
    }
    {
        {
            gm_l15 = ((uint64_t)0ULL);
        }
        {
            uint64_t t78 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            gm_l14 = t78;
        }
        {
            for (;;) {
                uint64_t t80 = gm_l15;
                uint64_t t81 = gm_l14;
                uint8_t t82 = (t80 < t81);
                if (!t82) goto lb79;
                {
                    uint64_t t85 = gm_l15;
                    uint64_t t88 = GM_LD(uint64_t, ((uint8_t *)&t85));
                    gm_l16 = t88;
                    {
                        void * t90 = gm_l0;
                        uint64_t t91 = gm_l16;
                        uint64_t t92 = (uintptr_t)t91;
                        uint64_t t93 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                        if (t92 >= (uintptr_t)(t93)) gm_trap_index(t92, t93, gm_src0gtuh94b1fce, 549, 38);
                        gm_t88rcucg1b7uk t94 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t92 * 88) + 0));
                        gm_t3877pk7lv748 t95;
                        gm_b16_8 t96 = gm_l13;
                        GM_ST(gm_b16_8, (uint8_t *)&t95 + 0, t96);
                        uint64_t t97 = gm_l16;
                        uint64_t t98 = (uintptr_t)t97;
                        uint64_t t99 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                        if (t98 >= (uintptr_t)(t99)) gm_trap_index(t98, t99, gm_src0gtuh94b1fce, 549, 90);
                        uint64_t t100 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l8)) + t98 * 8));
                        GM_ST(uint64_t, (uint8_t *)&t95 + 16, t100);
                        gm_tabb710d9mq2e t101;
                        gm_f_format_tcr2jpk((void *)&t101, (void *)NULL, cx, NULL, &t95);
                        if (gm_unw.kind) {
                            goto gm_ll83;
                        }
                        if (((((uint8_t *)&t101))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t101) + 8, gm_src0gtuh94b1fce, 549, 51);
                        gm_b16_8 t102 = GM_LD(gm_b16_8, (((uint8_t *)&t101) + 8));
                        gm_b16_8 t103 = GM_LD(gm_b16_8, ((uint8_t *)&t102));
                        uint64_t t104 = gm_l16;
                        uint64_t t105 = (uintptr_t)t104;
                        uint64_t t106 = GM_LD(uintptr_t, (((uint8_t *)&F.l4) + 0) + 8);
                        if (t105 >= (uintptr_t)(t106)) gm_trap_index(t105, t106, gm_src0gtuh94b1fce, 549, 101);
                        gm_b16_8 t107 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l4) + 0)) + t105 * 16));
                        gm_b0_1 t108 = gm_f_store_at7m90t((void *)NULL, cx, NULL, t90, t94, t103, t107);
                        if (gm_unw.kind) {
                            goto gm_ll83;
                        }
                    }
                    goto me86;
mn87:;
                    gm_trap("no match arm matched the scrutinee", gm_src0gtuh94b1fce, 549, 5);
me86:;
                }
lc79:;
                {
                    uint64_t t110 = gm_l15;
                    uint64_t r111; bool o112 = __builtin_add_overflow(t110, ((uint64_t)1ULL), &r111);
                    uint64_t t113 = r111;
                    gm_l15 = t113;
                }
            }
gm_ll83:;
            if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb79; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc79; }
            }
            goto gm_lad;
lb79:;
        }
    }
    {
        gm_b16_8 t114 = gm_l13;
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t114);
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
__attribute__((aligned(4))) void gm_f_array_60b0dn9(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2) {
    struct gm_fr_array_60b0dn9 F;
    void * gm_l0; /* fg */
    void * gm_l1; /* e */
    gm_b16_8 gm_l2; /* elems */
    gm_t88rcucg1b7uk gm_l3; /* t */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* i */
    uint64_t gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    uint64_t gm_l11; /* i */
    uint64_t gm_l12; /* esz */
    gm_b16_8 gm_l13; /* b */
    uint64_t gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    uint64_t gm_l16; /* i */
    uint64_t gm_l17; /* esz */
    GM_STACK_CHECK(&F, 943u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l1;
        gm_t88rcucg1b7uk t3 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t2) + 0));
        gm_l3 = t3;
    }
    {
        gm_f_list_epvducg((void *)&F.l4, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_l6 = ((uint64_t)0ULL);
        }
        {
            uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            gm_l5 = t5;
        }
        {
            for (;;) {
                uint64_t t7 = gm_l6;
                uint64_t t8 = gm_l5;
                uint8_t t9 = (t7 < t8);
                if (!t9) goto lb6;
                {
                    uint64_t t12 = gm_l6;
                    uint64_t t15 = GM_LD(uint64_t, ((uint8_t *)&t12));
                    gm_l7 = t15;
                    {
                        void * t17 = (void *)((uint8_t *)&F.l4);
                        void * t18 = gm_l0;
                        uint64_t t19 = gm_l7;
                        uint64_t t20 = (uintptr_t)t19;
                        uint64_t t21 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                        if (t20 >= (uintptr_t)(t21)) gm_trap_index(t20, t21, gm_src0gtuh94b1fce, 557, 52);
                        void * t22 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t20 * 88);
                        gm_t5o0tpv5jka2l t23;
                        gm_f_expr_porbc9t((void *)&t23, (void *)NULL, cx, NULL, t18, t22);
                        if (gm_unw.kind) {
                            goto gm_ll10;
                        }
                        if (((((uint8_t *)&t23))[0] != 0)) {
                            gm_t5o0tpv5jka2l t24;
                            GM_ST(uint8_t, (uint8_t *)&t24 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t24 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t24 + 4, 0);
                            ((uint8_t *)&t24)[0] = 1;
                            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t24 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t23) + 8)));
                            F.ret = t24;
                            goto gm_fret;
                        }
                        gm_b16_8 t25 = GM_LD(gm_b16_8, (((uint8_t *)&t23) + 8));
                        gm_tftb1odc739t5 t26 = gm_f_push_3ef746b(t17, t25);
                        if (((((uint8_t *)&t26))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t26) + 1, gm_src0gtuh94b1fce, 557, 29);
                        gm_b0_1 t27 = GM_LD(gm_b0_1, (((uint8_t *)&t26) + 1));
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_src0gtuh94b1fce, 557, 5);
me13:;
                }
lc6:;
                {
                    uint64_t t29 = gm_l6;
                    uint64_t r30; bool o31 = __builtin_add_overflow(t29, ((uint64_t)1ULL), &r30);
                    uint64_t t32 = r30;
                    gm_l6 = t32;
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
        gm_f_list_26fbt7s((void *)&F.l8, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_l10 = ((uint64_t)0ULL);
        }
        {
            uint64_t t34 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            gm_l9 = t34;
        }
        {
            for (;;) {
                uint64_t t36 = gm_l10;
                uint64_t t37 = gm_l9;
                uint8_t t38 = (t36 < t37);
                if (!t38) goto lb35;
                {
                    uint64_t t41 = gm_l10;
                    uint64_t t44 = GM_LD(uint64_t, ((uint8_t *)&t41));
                    gm_l11 = t44;
                    {
                        void * t46 = gm_l0;
                        void * t47 = GM_LD(void *, (((uint8_t *)t46) + 0));
                        void * t48 = (void *)(((uint8_t *)t47) + 8);
                        uint64_t t49 = gm_l11;
                        uint64_t t50 = (uintptr_t)t49;
                        uint64_t t51 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                        if (t50 >= (uintptr_t)(t51)) gm_trap_index(t50, t51, gm_src0gtuh94b1fce, 560, 29);
                        gm_t88rcucg1b7uk t52 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t50 * 88) + 0));
                        uint64_t t53 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t48, t52);
                        if (gm_unw.kind) {
                            goto gm_ll39;
                        }
                        gm_l12 = t53;
                    }
                    {
                        void * t54 = (void *)((uint8_t *)&F.l8);
                        uint64_t t55 = gm_l11;
                        uint32_t t56 = (uint32_t)t55;
                        uint64_t t57 = (uint64_t)t56;
                        uint64_t t58 = gm_l12;
                        uint64_t r59; bool o60 = __builtin_mul_overflow(t57, t58, &r59);
                        if (o60) gm_trap("integer overflow in `*`", gm_src0gtuh94b1fce, 561, 34);
                        uint64_t t61 = r59;
                        uint64_t t62 = gm_l12;
                        gm_t2lqu8jsj511d t63;
                        GM_ST(uint64_t, ((uint8_t *)&t63 + 0), t61);
                        GM_ST(uint64_t, ((uint8_t *)&t63 + 8), t62);
                        gm_tftb1odc739t5 t64 = gm_f_push_u0rp7b1(t54, GM_CONV(gm_csc7lc6jlkqkuq, t63));
                        if (((((uint8_t *)&t64))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t64) + 1, gm_src0gtuh94b1fce, 561, 9);
                        gm_b0_1 t65 = GM_LD(gm_b0_1, (((uint8_t *)&t64) + 1));
                    }
                    goto me42;
mn43:;
                    gm_trap("no match arm matched the scrutinee", gm_src0gtuh94b1fce, 559, 5);
me42:;
                }
lc35:;
                {
                    uint64_t t67 = gm_l10;
                    uint64_t r68; bool o69 = __builtin_add_overflow(t67, ((uint64_t)1ULL), &r68);
                    uint64_t t70 = r68;
                    gm_l10 = t70;
                }
            }
gm_ll39:;
            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb35; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc35; }
            }
            goto gm_lad;
lb35:;
        }
    }
    {
        void * t71 = gm_l0;
        gm_t88rcucg1b7uk t72 = gm_l3;
        gm_b16_8 t73 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 0));
        gm_b16_8 t74 = GM_LD(gm_b16_8, ((uint8_t *)&t73));
        gm_b16_8 t75 = gm_f_blob_over_qs5fusr((void *)NULL, cx, NULL, t71, t72, t74);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l13 = t75;
    }
    {
        {
            gm_l15 = ((uint64_t)0ULL);
        }
        {
            uint64_t t77 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            gm_l14 = t77;
        }
        {
            for (;;) {
                uint64_t t79 = gm_l15;
                uint64_t t80 = gm_l14;
                uint8_t t81 = (t79 < t80);
                if (!t81) goto lb78;
                {
                    uint64_t t84 = gm_l15;
                    uint64_t t87 = GM_LD(uint64_t, ((uint8_t *)&t84));
                    gm_l16 = t87;
                    {
                        void * t89 = gm_l0;
                        void * t90 = GM_LD(void *, (((uint8_t *)t89) + 0));
                        void * t91 = (void *)(((uint8_t *)t90) + 8);
                        uint64_t t92 = gm_l16;
                        uint64_t t93 = (uintptr_t)t92;
                        uint64_t t94 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                        if (t93 >= (uintptr_t)(t94)) gm_trap_index(t93, t94, gm_src0gtuh94b1fce, 565, 29);
                        gm_t88rcucg1b7uk t95 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t93 * 88) + 0));
                        uint64_t t96 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t91, t95);
                        if (gm_unw.kind) {
                            goto gm_ll82;
                        }
                        gm_l17 = t96;
                    }
                    {
                        void * t97 = gm_l0;
                        uint64_t t98 = gm_l16;
                        uint64_t t99 = (uintptr_t)t98;
                        uint64_t t100 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                        if (t99 >= (uintptr_t)(t100)) gm_trap_index(t99, t100, gm_src0gtuh94b1fce, 566, 18);
                        gm_t88rcucg1b7uk t101 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t99 * 88) + 0));
                        gm_t3877pk7lv748 t102;
                        gm_b16_8 t103 = gm_l13;
                        GM_ST(gm_b16_8, (uint8_t *)&t102 + 0, t103);
                        uint64_t t104 = gm_l16;
                        uint32_t t105 = (uint32_t)t104;
                        uint64_t t106 = (uint64_t)t105;
                        uint64_t t107 = gm_l17;
                        uint64_t r108; bool o109 = __builtin_mul_overflow(t106, t107, &r108);
                        if (o109) gm_trap("integer overflow in `*`", gm_src0gtuh94b1fce, 566, 70);
                        uint64_t t110 = r108;
                        GM_ST(uint64_t, (uint8_t *)&t102 + 16, t110);
                        gm_tabb710d9mq2e t111;
                        gm_f_format_tcr2jpk((void *)&t111, (void *)NULL, cx, NULL, &t102);
                        if (gm_unw.kind) {
                            goto gm_ll82;
                        }
                        if (((((uint8_t *)&t111))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t111) + 8, gm_src0gtuh94b1fce, 566, 31);
                        gm_b16_8 t112 = GM_LD(gm_b16_8, (((uint8_t *)&t111) + 8));
                        gm_b16_8 t113 = GM_LD(gm_b16_8, ((uint8_t *)&t112));
                        uint64_t t114 = gm_l16;
                        uint64_t t115 = (uintptr_t)t114;
                        uint64_t t116 = GM_LD(uintptr_t, (((uint8_t *)&F.l4) + 0) + 8);
                        if (t115 >= (uintptr_t)(t116)) gm_trap_index(t115, t116, gm_src0gtuh94b1fce, 566, 97);
                        gm_b16_8 t117 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l4) + 0)) + t115 * 16));
                        gm_b0_1 t118 = gm_f_store_at7m90t((void *)NULL, cx, NULL, t97, t101, t113, t117);
                        if (gm_unw.kind) {
                            goto gm_ll82;
                        }
                    }
                    goto me85;
mn86:;
                    gm_trap("no match arm matched the scrutinee", gm_src0gtuh94b1fce, 564, 5);
me85:;
                }
lc78:;
                {
                    uint64_t t120 = gm_l15;
                    uint64_t r121; bool o122 = __builtin_add_overflow(t120, ((uint64_t)1ULL), &r121);
                    uint64_t t123 = r121;
                    gm_l15 = t123;
                }
            }
gm_ll82:;
            if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb78; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc78; }
            }
            goto gm_lad;
lb78:;
        }
    }
    {
        gm_b16_8 t124 = gm_l13;
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t124);
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
inline __attribute__((aligned(4))) void gm_f_array_fill_ppt75jf(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2) {
    struct gm_fr_array_fill_ppt75jf F;
    void * gm_l0; /* fg */
    void * gm_l1; /* e */
    void * gm_l2; /* x */
    gm_t88rcucg1b7uk gm_l3; /* t */
    gm_b16_8 gm_l4; /* v */
    uint64_t gm_l5; /* len */
    gm_t88rcucg1b7uk gm_l6; /* elem */
    gm_b16_8 gm_l7; /* b */
    uint64_t gm_l8; /* esz */
    gm_b16_8 gm_l9; /* c */
    GM_STACK_CHECK(&F, 645u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l1;
        gm_t88rcucg1b7uk t3 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t2) + 0));
        gm_l3 = t3;
    }
    {
        void * t4 = gm_l0;
        void * t5 = gm_l2;
        gm_t5o0tpv5jka2l t6;
        gm_f_expr_porbc9t((void *)&t6, (void *)NULL, cx, NULL, t4, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t6))[0] != 0)) {
            gm_t5o0tpv5jka2l t7;
            GM_ST(uint8_t, (uint8_t *)&t7 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t7 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t7 + 4, 0);
            ((uint8_t *)&t7)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t7 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t6) + 8)));
            F.ret = t7;
            goto gm_fret;
        }
        gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)&t6) + 8));
        gm_l4 = t8;
    }
    {
        void * t10 = gm_l0;
        void * t11 = GM_LD(void *, (((uint8_t *)t10) + 0));
        void * t12 = (void *)(((uint8_t *)t11) + 8);
        gm_t88rcucg1b7uk t13 = gm_l3;
        gm_t53at80pfdus3 t14;
        gm_f_kind_ghvav22((void *)&t14, t12, GM_CONV(gm_csguankko4lmhd, t13));
        uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)&t14) + 0));
        if (t15 != ((uint8_t)11ULL)) goto ie9;
        uint64_t t16 = GM_LD(uint64_t, ((((uint8_t *)&t14) + 8) + 0));
        gm_l5 = t16;
        gm_t88rcucg1b7uk t17 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t14) + 8) + 8));
        gm_l6 = t17;
        {
            void * t19 = gm_l0;
            gm_t88rcucg1b7uk t20 = gm_l3;
            gm_b16_8 t21 = gm_f_tmp_uninit_jr39s5u((void *)NULL, cx, NULL, t19, t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l7 = t21;
        }
        {
            void * t22 = gm_l0;
            void * t23 = GM_LD(void *, (((uint8_t *)t22) + 0));
            void * t24 = (void *)(((uint8_t *)t23) + 8);
            gm_t88rcucg1b7uk t25 = gm_l6;
            uint64_t t26 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t24, t25);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l8 = t26;
        }
        {
            void * t27 = gm_l0;
            void * t28 = GM_LD(void *, (((uint8_t *)t27) + 0));
            void * t29 = (void *)(((uint8_t *)t28) + 8);
            gm_t88rcucg1b7uk t30 = gm_l6;
            gm_b16_8 t31 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t29, t30);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l9 = t31;
        }
        {
            void * t32 = gm_l0;
            gm_tctj0rue4hgoo t33;
            uint64_t t34 = gm_l5;
            GM_ST(uint64_t, (uint8_t *)&t33 + 0, t34);
            gm_b16_8 t35 = gm_l9;
            GM_ST(gm_b16_8, (uint8_t *)&t33 + 8, t35);
            gm_b16_8 t36 = gm_l7;
            GM_ST(gm_b16_8, (uint8_t *)&t33 + 24, t36);
            uint64_t t37 = gm_l8;
            GM_ST(uint64_t, (uint8_t *)&t33 + 40, t37);
            gm_b16_8 t38 = gm_l4;
            GM_ST(gm_b16_8, (uint8_t *)&t33 + 48, t38);
            gm_b0_1 t39 = gm_f_linef_584jv2g((void *)NULL, cx, NULL, t32, &t33);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_b16_8 t40 = gm_l7;
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
            ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t40);
            goto gm_fdone;
        }
        goto ix9;
ie9:;
        {
            void * t42 = gm_l1;
            gm_tl874t2atnlh3 t43 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t42) + 72));
            gm_b16_8 t44;
            GM_ST(void *, (uint8_t *)&t44, (void *)(gm_smuhqaa14mehe));
            GM_ST(uintptr_t, (uint8_t *)&t44 + 8, (uintptr_t)(15));
            gm_t08olq97rscn3 t45;
            gm_f_unsupported_531q51r((void *)&t45, (void *)NULL, cx, NULL, t43, t44);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 1;
            GM_ST(gm_t08olq97rscn3, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t45);
            goto gm_fdone;
        }
ix9:;
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
__attribute__((aligned(4))) void gm_f_bits_aggregate_bu0ircf(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2) {
    struct gm_fr_bits_aggregate_bu0ircf F;
    void * gm_l0; /* fg */
    void * gm_l1; /* e */
    gm_b16_8 gm_l2; /* elems */
    gm_t88rcucg1b7uk gm_l3; /* t */
    gm_tcmkompbl09il gm_l4; /* bl */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* i */
    gm_b16_8 gm_l9; /* acc */
    uint64_t gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* i */
    gm_tov4mtrfp5e7v gm_l13; /* f */
    gm_b16_8 gm_l14; /* raw */
    gm_b16_8 gm_l15; /* b */
    gm_b16_8 gm_l16; /* uc */
    GM_STACK_CHECK(&F, 1056u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l1;
        gm_t88rcucg1b7uk t3 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t2) + 0));
        gm_l3 = t3;
    }
    {
        void * t4 = gm_l0;
        void * t5 = GM_LD(void *, (((uint8_t *)t4) + 0));
        void * t6 = (void *)(((uint8_t *)t5) + 8);
        gm_t88rcucg1b7uk t7 = gm_l3;
        gm_t57euh612ligr t8;
        gm_f_bits_layout_23rect9((void *)&t8, (void *)NULL, cx, NULL, t6, t7);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!((((uint8_t *)&t8))[24] != 0)) gm_trap("unwrap of null", gm_src0gtuh94b1fce, 525, 11);
        gm_l4 = (*(gm_tcmkompbl09il *)(((uint8_t *)&t8) + 0));
    }
    {
        gm_f_list_epvducg((void *)&F.l5, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_l7 = ((uint64_t)0ULL);
        }
        {
            uint64_t t10 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            gm_l6 = t10;
        }
        {
            for (;;) {
                uint64_t t12 = gm_l7;
                uint64_t t13 = gm_l6;
                uint8_t t14 = (t12 < t13);
                if (!t14) goto lb11;
                {
                    uint64_t t17 = gm_l7;
                    uint64_t t20 = GM_LD(uint64_t, ((uint8_t *)&t17));
                    gm_l8 = t20;
                    {
                        void * t22 = (void *)((uint8_t *)&F.l5);
                        void * t23 = gm_l0;
                        uint64_t t24 = gm_l8;
                        uint64_t t25 = (uintptr_t)t24;
                        uint64_t t26 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                        if (t25 >= (uintptr_t)(t26)) gm_trap_index(t25, t26, gm_src0gtuh94b1fce, 527, 52);
                        void * t27 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t25 * 88);
                        gm_t5o0tpv5jka2l t28;
                        gm_f_expr_porbc9t((void *)&t28, (void *)NULL, cx, NULL, t23, t27);
                        if (gm_unw.kind) {
                            goto gm_ll15;
                        }
                        if (((((uint8_t *)&t28))[0] != 0)) {
                            gm_t5o0tpv5jka2l t29;
                            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
                            ((uint8_t *)&t29)[0] = 1;
                            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t29 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t28) + 8)));
                            F.ret = t29;
                            goto gm_fret;
                        }
                        gm_b16_8 t30 = GM_LD(gm_b16_8, (((uint8_t *)&t28) + 8));
                        gm_tftb1odc739t5 t31 = gm_f_push_3ef746b(t22, t30);
                        if (((((uint8_t *)&t31))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t31) + 1, gm_src0gtuh94b1fce, 527, 29);
                        gm_b0_1 t32 = GM_LD(gm_b0_1, (((uint8_t *)&t31) + 1));
                    }
                    goto me18;
mn19:;
                    gm_trap("no match arm matched the scrutinee", gm_src0gtuh94b1fce, 527, 5);
me18:;
                }
lc11:;
                {
                    uint64_t t34 = gm_l7;
                    uint64_t r35; bool o36 = __builtin_add_overflow(t34, ((uint64_t)1ULL), &r35);
                    uint64_t t37 = r35;
                    gm_l7 = t37;
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
        void * t38 = gm_l0;
        void * t39 = gm_l0;
        void * t40 = GM_LD(void *, (((uint8_t *)t39) + 0));
        void * t41 = (void *)(((uint8_t *)t40) + 8);
        gm_t88rcucg1b7uk t42 = gm_f_int_ty_7nma1b7((void *)NULL, cx, NULL, t41, ((uint8_t)0), ((uint16_t)128ULL));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t43;
        GM_ST(void *, (uint8_t *)&t43, (void *)(gm_sicgmpca63jln));
        GM_ST(uintptr_t, (uint8_t *)&t43 + 8, (uintptr_t)(1));
        gm_b16_8 t44 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t38, t42, t43);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l9 = t44;
    }
    {
        {
            gm_l11 = ((uint64_t)0ULL);
        }
        {
            uint64_t t46 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            gm_l10 = t46;
        }
        {
            for (;;) {
                uint64_t t48 = gm_l11;
                uint64_t t49 = gm_l10;
                uint8_t t50 = (t48 < t49);
                if (!t50) goto lb47;
                {
                    uint64_t t53 = gm_l11;
                    uint64_t t56 = GM_LD(uint64_t, ((uint8_t *)&t53));
                    gm_l12 = t56;
                    {
                        uint64_t t58 = gm_l12;
                        uint64_t t59 = (uintptr_t)t58;
                        uint64_t t60 = GM_LD(uintptr_t, (((uint8_t *)&gm_l4) + 8) + 8);
                        if (t59 >= (uintptr_t)(t60)) gm_trap_index(t59, t60, gm_src0gtuh94b1fce, 530, 14);
                        gm_tov4mtrfp5e7v t61 = GM_LD(gm_tov4mtrfp5e7v, ((uint8_t *)GM_LD(void *, (((uint8_t *)&gm_l4) + 8)) + t59 * 12));
                        gm_l13 = t61;
                    }
                    {
                        void * t62 = gm_l0;
                        uint64_t t63 = gm_l12;
                        uint64_t t64 = (uintptr_t)t63;
                        uint64_t t65 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                        if (t64 >= (uintptr_t)(t65)) gm_trap_index(t64, t65, gm_src0gtuh94b1fce, 531, 28);
                        gm_t88rcucg1b7uk t66 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t64 * 88) + 0));
                        uint64_t t67 = gm_l12;
                        uint64_t t68 = (uintptr_t)t67;
                        uint64_t t69 = GM_LD(uintptr_t, (((uint8_t *)&F.l5) + 0) + 8);
                        if (t68 >= (uintptr_t)(t69)) gm_trap_index(t68, t69, gm_src0gtuh94b1fce, 531, 41);
                        gm_b16_8 t70 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l5) + 0)) + t68 * 16));
                        gm_b16_8 t71 = gm_f_bits_raw_e0qg9mb((void *)NULL, cx, NULL, t62, t66, t70);
                        if (gm_unw.kind) {
                            goto gm_ll51;
                        }
                        gm_l14 = t71;
                    }
                    {
                        void * t72 = gm_l0;
                        gm_tj4tbafenpsjm t73;
                        memset((uint8_t *)&t73 + 52, 0, 4);
                        gm_b16_8 t74 = gm_l9;
                        GM_ST(gm_b16_8, (uint8_t *)&t73 + 0, t74);
                        gm_b16_8 t75 = gm_l14;
                        GM_ST(gm_b16_8, (uint8_t *)&t73 + 16, t75);
                        uint32_t t76 = GM_LD(uint32_t, (((uint8_t *)&gm_l13) + 4));
                        gm_b16_8 t77 = gm_f_mask_epjl3to((void *)NULL, cx, NULL, t76);
                        if (gm_unw.kind) {
                            goto gm_ll51;
                        }
                        GM_ST(gm_b16_8, (uint8_t *)&t73 + 32, t77);
                        uint32_t t78 = GM_LD(uint32_t, (((uint8_t *)&gm_l13) + 0));
                        GM_ST(uint32_t, (uint8_t *)&t73 + 48, t78);
                        gm_b0_1 t79 = gm_f_linef_glh29gu((void *)NULL, cx, NULL, t72, &t73);
                        if (gm_unw.kind) {
                            goto gm_ll51;
                        }
                    }
                    goto me54;
mn55:;
                    gm_trap("no match arm matched the scrutinee", gm_src0gtuh94b1fce, 529, 5);
me54:;
                }
lc47:;
                {
                    uint64_t t81 = gm_l11;
                    uint64_t r82; bool o83 = __builtin_add_overflow(t81, ((uint64_t)1ULL), &r82);
                    uint64_t t84 = r82;
                    gm_l11 = t84;
                }
            }
gm_ll51:;
            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb47; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc47; }
            }
            goto gm_lad;
lb47:;
        }
    }
    {
        void * t85 = gm_l0;
        gm_t88rcucg1b7uk t86 = gm_l3;
        gm_b16_8 t87 = gm_f_blob_d5apnrc((void *)NULL, cx, NULL, t85, t86);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l15 = t87;
    }
    {
        void * t88 = gm_l0;
        void * t89 = GM_LD(void *, (((uint8_t *)t88) + 0));
        void * t90 = (void *)(((uint8_t *)t89) + 8);
        gm_t88rcucg1b7uk t91 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l4) + 0));
        gm_b16_8 t92 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t90, t91);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l16 = t92;
    }
    {
        void * t93 = gm_l0;
        gm_t0uuhaljsf410 t94;
        gm_b16_8 t95 = gm_l16;
        GM_ST(gm_b16_8, (uint8_t *)&t94 + 0, t95);
        gm_b16_8 t96 = gm_l15;
        GM_ST(gm_b16_8, (uint8_t *)&t94 + 16, t96);
        gm_b16_8 t97 = gm_l16;
        GM_ST(gm_b16_8, (uint8_t *)&t94 + 32, t97);
        gm_b16_8 t98 = gm_l9;
        GM_ST(gm_b16_8, (uint8_t *)&t94 + 48, t98);
        gm_b0_1 t99 = gm_f_linef_uc25pl5((void *)NULL, cx, NULL, t93, &t94);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t100 = gm_l15;
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t100);
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
inline __attribute__((aligned(4))) void gm_f_bits_field_os47f03(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint32_t a2) {
    struct gm_fr_bits_field_os47f03 F;
    void * gm_l0; /* fg */
    void * gm_l1; /* base */
    uint32_t gm_l2; /* index */
    gm_tcmkompbl09il gm_l3; /* bl */
    gm_tov4mtrfp5e7v gm_l4; /* f */
    gm_b16_8 gm_l5; /* v */
    gm_b16_8 gm_l6; /* a */
    gm_b16_8 gm_l7; /* uc */
    gm_b16_8 gm_l8; /* raw */
    gm_b16_8 gm_l9; /* raw2 */
    GM_STACK_CHECK(&F, 732u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        void * t5 = gm_l1;
        gm_t88rcucg1b7uk t6 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t5) + 0));
        gm_t57euh612ligr t7;
        gm_f_bits_layout_23rect9((void *)&t7, (void *)NULL, cx, NULL, t4, t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!((((uint8_t *)&t7))[24] != 0)) gm_trap("unwrap of null", gm_src0gtuh94b1fce, 495, 11);
        gm_l3 = (*(gm_tcmkompbl09il *)(((uint8_t *)&t7) + 0));
    }
    {
        uint32_t t8 = gm_l2;
        uint64_t t9 = (uint64_t)t8;
        uint64_t t10 = (uintptr_t)t9;
        uint64_t t11 = GM_LD(uintptr_t, (((uint8_t *)&gm_l3) + 8) + 8);
        if (t10 >= (uintptr_t)(t11)) gm_trap_index(t10, t11, gm_src0gtuh94b1fce, 496, 10);
        gm_tov4mtrfp5e7v t12 = GM_LD(gm_tov4mtrfp5e7v, ((uint8_t *)GM_LD(void *, (((uint8_t *)&gm_l3) + 8)) + t10 * 12));
        gm_l4 = t12;
    }
    {
        void * t13 = gm_l0;
        void * t14 = gm_l1;
        gm_t5o0tpv5jka2l t15;
        gm_f_expr_porbc9t((void *)&t15, (void *)NULL, cx, NULL, t13, t14);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t15))[0] != 0)) {
            gm_t5o0tpv5jka2l t16;
            GM_ST(uint8_t, (uint8_t *)&t16 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t16 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t16 + 4, 0);
            ((uint8_t *)&t16)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t16 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t15) + 8)));
            F.ret = t16;
            goto gm_fret;
        }
        gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)&t15) + 8));
        gm_l5 = t17;
    }
    {
        void * t18 = gm_l0;
        void * t19 = gm_l1;
        gm_t88rcucg1b7uk t20 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t19) + 0));
        gm_b16_8 t21 = gm_l5;
        gm_b16_8 t22 = gm_f_addr_of_hiaqkn0((void *)NULL, cx, NULL, t18, t20, t21);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l6 = t22;
    }
    {
        void * t23 = gm_l0;
        void * t24 = GM_LD(void *, (((uint8_t *)t23) + 0));
        void * t25 = (void *)(((uint8_t *)t24) + 8);
        gm_t88rcucg1b7uk t26 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l3) + 0));
        gm_b16_8 t27 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t25, t26);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l7 = t27;
    }
    {
        gm_tu8mt0h8clq1l t28;
        memset((uint8_t *)&t28 + 36, 0, 4);
        gm_b16_8 t29 = gm_l7;
        GM_ST(gm_b16_8, (uint8_t *)&t28 + 0, t29);
        gm_b16_8 t30 = gm_l6;
        GM_ST(gm_b16_8, (uint8_t *)&t28 + 16, t30);
        uint32_t t31 = GM_LD(uint32_t, (((uint8_t *)&gm_l4) + 0));
        GM_ST(uint32_t, (uint8_t *)&t28 + 32, t31);
        uint32_t t32 = GM_LD(uint32_t, (((uint8_t *)&gm_l4) + 4));
        gm_b16_8 t33 = gm_f_mask_epjl3to((void *)NULL, cx, NULL, t32);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t28 + 40, t33);
        gm_tabb710d9mq2e t34;
        gm_f_format_tg9hp73((void *)&t34, (void *)NULL, cx, NULL, &t28);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t34))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t34) + 8, gm_src0gtuh94b1fce, 500, 12);
        gm_b16_8 t35 = GM_LD(gm_b16_8, (((uint8_t *)&t34) + 8));
        gm_l8 = t35;
    }
    {
        void * t36 = gm_l0;
        void * t37 = gm_l0;
        void * t38 = GM_LD(void *, (((uint8_t *)t37) + 0));
        void * t39 = (void *)(((uint8_t *)t38) + 8);
        gm_t88rcucg1b7uk t40 = gm_f_int_ty_7nma1b7((void *)NULL, cx, NULL, t39, ((uint8_t)0), ((uint16_t)128ULL));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t41 = gm_l8;
        gm_b16_8 t42 = GM_LD(gm_b16_8, ((uint8_t *)&t41));
        gm_b16_8 t43 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t36, t40, t42);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l9 = t43;
    }
    {
        void * t44 = gm_l0;
        gm_t88rcucg1b7uk t45 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l4) + 8));
        gm_b16_8 t46 = gm_l9;
        gm_b16_8 t47 = gm_f_bits_value_u15lfan((void *)NULL, cx, NULL, t44, t45, t46);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t47);
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
extern __attribute__((aligned(4))) gm_b16_8 gm_f_blob_d5apnrc(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
extern __attribute__((aligned(4))) gm_b16_8 gm_f_blob_over_qs5fusr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_bound_n4p0vpb(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2) {
    struct gm_fr_bound_n4p0vpb F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* v */
    GM_STACK_CHECK(&F, 331u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t3 = gm_l0;
        void * t4 = GM_LD(void *, (((uint8_t *)t3) + 0));
        void * t5 = (void *)(((uint8_t *)t4) + 8);
        gm_t88rcucg1b7uk t6 = gm_l1;
        uint64_t t7 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t5, t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t8 = gm_l0;
        void * t9 = GM_LD(void *, (((uint8_t *)t8) + 0));
        uint64_t t10 = GM_LD(uint64_t, ((((((uint8_t *)t9) + 8) + 8) + 0) + 0));
        uint8_t t11 = (t7 > t10);
        if (!t11) goto ie2;
        {
            gm_t6j6smj7qoctu t13;
            gm_b16_8 t14 = gm_l2;
            GM_ST(gm_b16_8, (uint8_t *)&t13 + 0, t14);
            gm_b16_8 t15 = gm_l2;
            GM_ST(gm_b16_8, (uint8_t *)&t13 + 16, t15);
            gm_tabb710d9mq2e t16;
            gm_f_format_nptl5af((void *)&t16, (void *)NULL, cx, NULL, &t13);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t16))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t16) + 8, gm_src0gtuh94b1fce, 1070, 16);
            gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)&t16) + 8));
            gm_b16_8 t18 = GM_LD(gm_b16_8, ((uint8_t *)&t17));
            uint8_t t19 = 0;
            F.ret = t18;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_b16_8 t20 = gm_l2;
        gm_tnpifv20irnph t21;
        GM_ST(gm_b16_8, ((uint8_t *)&t21 + 0), t20);
        gm_tabb710d9mq2e t22;
        gm_f_format_6df13cd((void *)&t22, (void *)NULL, cx, NULL, t21);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t22))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t22) + 8, gm_src0gtuh94b1fce, 1072, 12);
        gm_b16_8 t23 = GM_LD(gm_b16_8, (((uint8_t *)&t22) + 8));
        gm_b16_8 t24 = GM_LD(gm_b16_8, ((uint8_t *)&t23));
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
__attribute__((aligned(4))) void gm_f_break_expr_71qc2ca(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2) {
    struct gm_fr_break_expr_71qc2ca F;
    void * gm_l0; /* fg */
    void * gm_l1; /* e */
    void * gm_l2; /* x */
    gm_t88rcucg1b7uk gm_l3; /* t */
    gm_tdlpv0q69lov1 gm_l4; /* val */
    void * gm_l5; /* bx */
    uint32_t gm_l6; /* levels */
    uint32_t gm_l7; /* lbl */
    gm_tep2drsl9408b gm_l8; /* bv */
    gm_b16_8 gm_l9; /* slot */
    gm_b16_8 gm_l10; /* fr */
    gm_tep2drsl9408b gm_l11; /* bv */
    GM_STACK_CHECK(&F, 973u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l1;
        gm_t88rcucg1b7uk t3 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t2) + 0));
        gm_l3 = t3;
    }
    {
        gm_tdlpv0q69lov1 t4;
        memset(&t4, 0, sizeof t4);
        gm_l4 = t4;
    }
    {
        void * t6 = gm_l2;
        void * t7 = GM_LD(void *, (((uint8_t *)t6) + 16));
        if (!(GM_LD(void *, ((uint8_t *)&t7)) != 0)) goto ie5;
        void * t8 = GM_LD(void *, ((uint8_t *)&t7));
        gm_l5 = t8;
        {
            gm_tep2drsl9408b t10;
            memset((uint8_t *)&t10 + 20, 0, 4);
            void * t11 = gm_l0;
            void * t12 = gm_l5;
            gm_t5o0tpv5jka2l t13;
            gm_f_expr_porbc9t((void *)&t13, (void *)NULL, cx, NULL, t11, t12);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t13))[0] != 0)) {
                gm_t5o0tpv5jka2l t14;
                GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
                ((uint8_t *)&t14)[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t14 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t13) + 8)));
                F.ret = t14;
                goto gm_fret;
            }
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&t13) + 8));
            GM_ST(gm_b16_8, (uint8_t *)&t10 + 0, t15);
            void * t16 = gm_l5;
            gm_t88rcucg1b7uk t17 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t16) + 0));
            GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t10 + 16, t17);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tdlpv0q69lov1 *)((uint8_t *)&gm_l4)) + 25, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tdlpv0q69lov1 *)((uint8_t *)&gm_l4)) + 26, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tdlpv0q69lov1 *)((uint8_t *)&gm_l4)) + 28, 0);
            ((uint8_t *)&(*(gm_tdlpv0q69lov1 *)((uint8_t *)&gm_l4)))[24] = 1;
            GM_ST(gm_tep2drsl9408b, (uint8_t *)&(*(gm_tdlpv0q69lov1 *)((uint8_t *)&gm_l4)) + 0, t10);
        }
        goto ix5;
ie5:;
ix5:;
    }
    {
        void * t18 = gm_l2;
        uint8_t t21 = GM_LD(uint8_t, ((((uint8_t *)t18) + 0) + 0));
        if (t21 != ((uint8_t)0ULL)) goto mn20;
        uint32_t t22 = GM_LD(uint32_t, (((((uint8_t *)t18) + 0) + 4) + 0));
        gm_l6 = t22;
        uint32_t t23 = GM_LD(uint32_t, (((((uint8_t *)t18) + 0) + 4) + 4));
        gm_l7 = t23;
        {
            if (!((((uint8_t *)&gm_l4))[24] != 0)) goto ie25;
            gm_tep2drsl9408b t26 = GM_LD(gm_tep2drsl9408b, ((uint8_t *)&gm_l4));
            gm_l8 = t26;
            {
                void * t29 = gm_l0;
                uint32_t t30 = gm_l6;
                uint32_t t31 = gm_l7;
                uint8_t t32 = gm_f_is_with_label_rskslsd((void *)NULL, cx, NULL, t29, t30, t31);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (!t32) goto ie28;
                {
                    void * t34 = gm_l0;
                    uint32_t t35 = gm_l6;
                    uint32_t t36 = gm_l7;
                    gm_t88rcucg1b7uk t37;
                    GM_ST(uint32_t, ((uint8_t *)&t37 + 0), t36);
                    gm_tabb710d9mq2e t38;
                    gm_f_format_0dl6hna((void *)&t38, (void *)NULL, cx, NULL, t37);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t38))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t38) + 8, gm_src0gtuh94b1fce, 725, 52);
                    gm_b16_8 t39 = GM_LD(gm_b16_8, (((uint8_t *)&t38) + 8));
                    gm_b16_8 t40 = GM_LD(gm_b16_8, ((uint8_t *)&t39));
                    gm_b16_8 t41 = gm_f_frame_field_nj8m22n((void *)NULL, cx, NULL, t34, t35, t40);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l9 = t41;
                }
                {
                    void * t43 = gm_l0;
                    void * t44 = GM_LD(void *, (((uint8_t *)t43) + 0));
                    void * t45 = (void *)(((uint8_t *)t44) + 8);
                    gm_t88rcucg1b7uk t46 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l8) + 16));
                    uint8_t t47 = gm_f_is_never_in5ftma(t45, GM_CONV(gm_csguankko4lmhd, t46));
                    uint8_t t48 = (uint8_t)!t47;
                    if (!t48) goto ie42;
                    {
                        void * t50 = gm_l0;
                        gm_t6j6smj7qoctu t51;
                        gm_b16_8 t52 = gm_l9;
                        GM_ST(gm_b16_8, (uint8_t *)&t51 + 0, t52);
                        gm_b16_8 t53 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l8) + 0));
                        GM_ST(gm_b16_8, (uint8_t *)&t51 + 16, t53);
                        gm_b0_1 t54 = gm_f_linef_uu8i6ue((void *)NULL, cx, NULL, t50, &t51);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    goto ix42;
ie42:;
ix42:;
                }
                goto ix28;
ie28:;
ix28:;
            }
            goto ix25;
ie25:;
ix25:;
        }
        {
            void * t55 = gm_l0;
            uint32_t t56 = gm_l6;
            gm_tommf4j9upomc t57;
            gm_f_frame_up_g8486eu((void *)&t57, (void *)NULL, cx, NULL, t55, t56);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tommf4j9upomc t58 = t57;
            gm_l10 = GM_LD(gm_b16_8, (uint8_t *)&t58 + 0);
        }
        {
            void * t59 = gm_l0;
            gm_t9qkh0rsooc8d t60;
            GM_ST(uint8_t, ((uint8_t *)&t60 + 0), ((uint8_t)1ULL));
            gm_b16_8 t61 = gm_l10;
            uint32_t t62 = gm_l7;
            gm_t88rcucg1b7uk t63;
            GM_ST(uint32_t, ((uint8_t *)&t63 + 0), t62);
            gm_tabb710d9mq2e t64;
            gm_f_format_32n178h((void *)&t64, (void *)NULL, cx, NULL, t63);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t64))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t64) + 8, gm_src0gtuh94b1fce, 730, 39);
            gm_b16_8 t65 = GM_LD(gm_b16_8, (((uint8_t *)&t64) + 8));
            gm_b16_8 t66 = GM_LD(gm_b16_8, ((uint8_t *)&t65));
            gm_b16_8 t67;
            GM_ST(void *, (uint8_t *)&t67, (void *)(gm_sicgmpca63jln));
            GM_ST(uintptr_t, (uint8_t *)&t67 + 8, (uintptr_t)(1));
            gm_b0_1 t68 = gm_f_start_exit_m57dnn5((void *)NULL, cx, NULL, t59, t60, t61, t66, t67);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto me19;
mn20:;
        uint8_t t70 = GM_LD(uint8_t, ((((uint8_t *)t18) + 0) + 0));
        if (t70 != ((uint8_t)1ULL)) goto mn69;
        {
            if (!((((uint8_t *)&gm_l4))[24] != 0)) goto ie72;
            gm_tep2drsl9408b t73 = GM_LD(gm_tep2drsl9408b, ((uint8_t *)&gm_l4));
            gm_l11 = t73;
            {
                void * t76 = gm_l0;
                void * t77 = GM_LD(void *, (((uint8_t *)t76) + 0));
                void * t78 = (void *)(((uint8_t *)t77) + 8);
                gm_t88rcucg1b7uk t79 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l11) + 16));
                uint8_t t80 = gm_f_is_void_vs2outo(t78, GM_CONV(gm_csguankko4lmhd, t79));
                uint8_t t81 = (uint8_t)!t80;
                if (!t81) goto ie75;
                {
                    void * t83 = gm_l0;
                    gm_t88rcucg1b7uk t84 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l11) + 16));
                    gm_b16_8 t85;
                    GM_ST(void *, (uint8_t *)&t85, (void *)(gm_s7hi4f3a8v74u));
                    GM_ST(uintptr_t, (uint8_t *)&t85 + 8, (uintptr_t)(12));
                    gm_b16_8 t86 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l11) + 0));
                    gm_b0_1 t87 = gm_f_store_at7m90t((void *)NULL, cx, NULL, t83, t84, t85, t86);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                goto ix75;
ie75:;
ix75:;
            }
            goto ix72;
ie72:;
ix72:;
        }
        {
            void * t88 = gm_l0;
            gm_t9qkh0rsooc8d t89;
            GM_ST(uint8_t, ((uint8_t *)&t89 + 0), ((uint8_t)1ULL));
            gm_b16_8 t90;
            GM_ST(void *, (uint8_t *)&t90, (void *)(gm_skf0mm42l0tsk));
            GM_ST(uintptr_t, (uint8_t *)&t90 + 8, (uintptr_t)(16));
            gm_b16_8 t91;
            GM_ST(void *, (uint8_t *)&t91, (void *)(gm_s4s2nr1qc94po));
            GM_ST(uintptr_t, (uint8_t *)&t91 + 8, (uintptr_t)(16));
            gm_b16_8 t92;
            GM_ST(void *, (uint8_t *)&t92, (void *)(gm_sicgmpca63jln));
            GM_ST(uintptr_t, (uint8_t *)&t92 + 8, (uintptr_t)(1));
            gm_b0_1 t93 = gm_f_start_exit_m57dnn5((void *)NULL, cx, NULL, t88, t89, t90, t91, t92);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto me19;
mn69:;
        gm_trap("no match arm matched the scrutinee", gm_src0gtuh94b1fce, 721, 5);
me19:;
    }
    {
        void * t94 = gm_l0;
        gm_t88rcucg1b7uk t95 = gm_l3;
        gm_b16_8 t96 = gm_f_zero_tlk13aa((void *)NULL, cx, NULL, t94, t95);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t96);
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
__attribute__((aligned(4))) gm_b0_1 gm_f_clear_outside_q01mr8i(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_b16_8 a3) {
    struct gm_fr_clear_outside_q01mr8i F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* dest */
    gm_b16_8 gm_l3; /* parts */
    uint64_t gm_l4; /* size */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    gm_t2lqu8jsj511d gm_l10; /* p */
    uint64_t gm_l11; /* i */
    uint64_t gm_l12; /* end */
    uint64_t gm_l13; /* gaps */
    gm_b16_8 gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    uint8_t gm_l16; /* c */
    uint64_t gm_l18; /* i */
    gm_tu703bl1jkggd gm_l19; /* sizes */
    uint64_t gm_l20; /* k */
    gm_b16_8 gm_l21; /* <tmp> */
    uint64_t gm_l22; /* <tmp> */
    uint64_t gm_l23; /* s */
    uint8_t gm_l24; /* all_free */
    uint64_t gm_l25; /* <tmp> */
    uint64_t gm_l26; /* <tmp> */
    uint64_t gm_l27; /* j */
    gm_b16_8 gm_l28; /* <tmp> */
    uint64_t gm_l29; /* <tmp> */
    gm_t2lqu8jsj511d gm_l30; /* st */
    GM_STACK_CHECK(&F, 1480u);
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
        gm_t88rcucg1b7uk t5 = gm_l1;
        uint64_t t6 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t4, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint64_t t7 = (uint64_t)t6;
        gm_l4 = t7;
    }
    {
        gm_f_list_s83jn2q((void *)&F.l5, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_l7 = ((uint64_t)0ULL);
        }
        {
            uint64_t t9 = gm_l4;
            gm_l6 = t9;
        }
        {
            for (;;) {
                uint64_t t11 = gm_l7;
                uint64_t t12 = gm_l6;
                uint8_t t13 = (t11 < t12);
                if (!t13) goto lb10;
                {
                    uint64_t t16 = gm_l7;
                    {
                        void * t20 = (void *)((uint8_t *)&F.l5);
                        gm_tftb1odc739t5 t21 = gm_f_push_vd1290p(t20, ((uint8_t)0));
                        if (((((uint8_t *)&t21))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t21) + 1, gm_src0gtuh94b1fce, 131, 24);
                        gm_b0_1 t22 = GM_LD(gm_b0_1, (((uint8_t *)&t21) + 1));
                    }
                    goto me17;
mn18:;
                    gm_trap("no match arm matched the scrutinee", gm_src0gtuh94b1fce, 131, 5);
me17:;
                }
lc10:;
                {
                    uint64_t t24 = gm_l7;
                    uint64_t r25; bool o26 = __builtin_add_overflow(t24, ((uint64_t)1ULL), &r25);
                    uint64_t t27 = r25;
                    gm_l7 = t27;
                }
            }
lb10:;
        }
    }
    {
        {
            gm_b16_8 t29 = gm_l3;
            gm_l8 = t29;
        }
        {
            gm_l9 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t31 = gm_l9;
                uint64_t t32 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                uint8_t t33 = (t31 < t32);
                if (!t33) goto lb30;
                {
                    uint64_t t36 = gm_l9;
                    uint64_t t37 = (uintptr_t)t36;
                    uint64_t t38 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                    gm_t2lqu8jsj511d t41 = GM_LD(gm_t2lqu8jsj511d, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l8)) + t37 * 16));
                    gm_l10 = t41;
                    {
                        uint64_t t43 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 0));
                        uint64_t t44 = (uint64_t)t43;
                        gm_l11 = t44;
                    }
                    {
                        uint64_t t45 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 0));
                        uint64_t t46 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                        uint64_t r47; bool o48 = __builtin_add_overflow(t45, t46, &r47);
                        if (o48) gm_trap("integer overflow in `+`", gm_src0gtuh94b1fce, 134, 17);
                        uint64_t t49 = r47;
                        uint64_t t50 = (uint64_t)t49;
                        gm_l12 = t50;
                    }
                    {
                        for (;;) {
                            uint64_t t52 = gm_l11;
                            uint64_t t53 = gm_l12;
                            uint8_t t54 = (t52 < t53);
                            if (!t54) goto lb51;
                            uint64_t t55 = gm_l11;
                            uint64_t t56 = gm_l4;
                            uint8_t t57 = (t55 < t56);
                            if (!t57) goto lb51;
                            {
                                uint64_t t60 = gm_l11;
                                uint64_t t61 = (uintptr_t)t60;
                                uint64_t t62 = GM_LD(uintptr_t, (((uint8_t *)&F.l5) + 0) + 8);
                                if (t61 >= (uintptr_t)(t62)) gm_trap_index(t61, t62, gm_src0gtuh94b1fce, 136, 13);
                                GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l5) + 0)) + t61 * 1), ((uint8_t)1));
                            }
                            {
                                uint64_t t63 = gm_l11;
                                uint64_t r64; bool o65 = __builtin_add_overflow(t63, ((uint64_t)1ULL), &r64);
                                if (o65) gm_trap("integer overflow in `+`", gm_src0gtuh94b1fce, 137, 13);
                                uint64_t t66 = r64;
                                gm_l11 = t66;
                            }
lc51:;
                        }
lb51:;
                    }
                    goto me39;
mn40:;
                    gm_trap("no match arm matched the scrutinee", gm_src0gtuh94b1fce, 132, 5);
me39:;
                }
lc30:;
                {
                    uint64_t t68 = gm_l9;
                    uint64_t r69; bool o70 = __builtin_add_overflow(t68, ((uint64_t)1ULL), &r69);
                    uint64_t t71 = r69;
                    gm_l9 = t71;
                }
            }
lb30:;
        }
    }
    {
        gm_l13 = ((uint64_t)0ULL);
    }
    {
        {
            gm_b16_8 t73 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 0));
            gm_l14 = t73;
        }
        {
            gm_l15 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t75 = gm_l15;
                uint64_t t76 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                uint8_t t77 = (t75 < t76);
                if (!t77) goto lb74;
                {
                    uint64_t t80 = gm_l15;
                    uint64_t t81 = (uintptr_t)t80;
                    uint64_t t82 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                    uint8_t t83 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l14)) + t81 * 1));
                    uint8_t t86 = GM_LD(uint8_t, ((uint8_t *)&t83));
                    gm_l16 = t86;
                    {
                        uint8_t t89 = gm_l16;
                        uint8_t t90 = (uint8_t)!t89;
                        if (!t90) goto ie88;
                        {
                            uint64_t t92 = gm_l13;
                            uint64_t r93; bool o94 = __builtin_add_overflow(t92, ((uint64_t)1ULL), &r93);
                            if (o94) gm_trap("integer overflow in `+`", gm_src0gtuh94b1fce, 142, 20);
                            uint64_t t95 = r93;
                            gm_l13 = t95;
                        }
                        goto ix88;
ie88:;
ix88:;
                    }
                    goto me84;
mn85:;
                    gm_trap("no match arm matched the scrutinee", gm_src0gtuh94b1fce, 141, 5);
me84:;
                }
lc74:;
                {
                    uint64_t t97 = gm_l15;
                    uint64_t r98; bool o99 = __builtin_add_overflow(t97, ((uint64_t)1ULL), &r98);
                    uint64_t t100 = r98;
                    gm_l15 = t100;
                }
            }
lb74:;
        }
    }
    {
        uint64_t t102 = gm_l13;
        uint8_t t103 = (t102 > ((uint64_t)32ULL));
        if (!t103) goto ie101;
        {
            void * t105 = gm_l0;
            gm_t6j6smj7qoctu t106;
            gm_b16_8 t107 = gm_l2;
            GM_ST(gm_b16_8, (uint8_t *)&t106 + 0, t107);
            gm_b16_8 t108 = gm_l2;
            GM_ST(gm_b16_8, (uint8_t *)&t106 + 16, t108);
            gm_b0_1 t109 = gm_f_linef_cf5thm0((void *)NULL, cx, NULL, t105, &t106);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_b0_1 t110;
            uint8_t t111 = 0;
            F.ret = t110;
            goto gm_fret;
        }
        goto ix101;
ie101:;
ix101:;
    }
    {
        gm_f_list_v5d7fqe((void *)&F.l17, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_l18 = ((uint64_t)0ULL);
    }
    {
        for (;;) {
            uint64_t t113 = gm_l18;
            uint64_t t114 = gm_l4;
            uint8_t t115 = (t113 < t114);
            if (!t115) goto lb112;
            {
                uint64_t t119 = gm_l18;
                uint64_t t120 = (uintptr_t)t119;
                uint64_t t121 = GM_LD(uintptr_t, (((uint8_t *)&F.l5) + 0) + 8);
                if (t120 >= (uintptr_t)(t121)) gm_trap_index(t120, t121, gm_src0gtuh94b1fce, 151, 12);
                uint8_t t122 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l5) + 0)) + t120 * 1));
                if (!t122) goto ie118;
                {
                    uint64_t t124 = gm_l18;
                    uint64_t r125; bool o126 = __builtin_add_overflow(t124, ((uint64_t)1ULL), &r125);
                    if (o126) gm_trap("integer overflow in `+`", gm_src0gtuh94b1fce, 152, 13);
                    uint64_t t127 = r125;
                    gm_l18 = t127;
                }
                {
                    goto lc112;
                }
                goto ix118;
ie118:;
ix118:;
            }
            {
                GM_ST(uint64_t, (uint8_t *)&gm_l19 + 0, ((uint64_t)8ULL));
                GM_ST(uint64_t, (uint8_t *)&gm_l19 + 8, ((uint64_t)4ULL));
                GM_ST(uint64_t, (uint8_t *)&gm_l19 + 16, ((uint64_t)2ULL));
                GM_ST(uint64_t, (uint8_t *)&gm_l19 + 24, ((uint64_t)1ULL));
            }
            {
                gm_l20 = ((uint64_t)1ULL);
            }
            {
                {
                    void * t129 = ((uint8_t *)&gm_l19);
                    if ((uintptr_t)0 > (uintptr_t)4 || (uintptr_t)4 > (uintptr_t)4) gm_trap_slice((uintptr_t)0, (uintptr_t)4, 4, gm_src0gtuh94b1fce, 158, 9);
                    gm_b16_8 t130;
                    GM_ST(void *, (uint8_t *)&t130, (void *)(((uint8_t *)t129 + (uintptr_t)0 * 8)));
                    GM_ST(uintptr_t, (uint8_t *)&t130 + 8, (uintptr_t)((uintptr_t)4 - (uintptr_t)0));
                    gm_l21 = t130;
                }
                {
                    gm_l22 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t132 = gm_l22;
                        uint64_t t133 = GM_LD(uintptr_t, ((uint8_t *)&gm_l21) + 8);
                        uint8_t t134 = (t132 < t133);
                        if (!t134) goto lb131;
                        {
                            uint64_t t137 = gm_l22;
                            uint64_t t138 = (uintptr_t)t137;
                            uint64_t t139 = GM_LD(uintptr_t, ((uint8_t *)&gm_l21) + 8);
                            uint64_t t140 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l21)) + t138 * 8));
                            uint64_t t143 = GM_LD(uint64_t, ((uint8_t *)&t140));
                            gm_l23 = t143;
                            {
                                uint64_t t146 = gm_l18;
                                uint64_t t147 = gm_l23;
                                if (t147 == 0) gm_trap("division by zero", gm_src0gtuh94b1fce, 159, 16);
                                uint64_t t148 = (uint64_t)(t146 % t147);
                                uint8_t t149 = ((t148 == ((uint64_t)0ULL)));
                                if (!t149) goto ie145;
                                uint64_t t150 = gm_l18;
                                uint64_t t151 = gm_l23;
                                uint64_t r152; bool o153 = __builtin_add_overflow(t150, t151, &r152);
                                if (o153) gm_trap("integer overflow in `+`", gm_src0gtuh94b1fce, 159, 31);
                                uint64_t t154 = r152;
                                uint64_t t155 = gm_l4;
                                uint8_t t156 = (t154 <= t155);
                                if (!t156) goto ie145;
                                {
                                    gm_l24 = ((uint8_t)1);
                                }
                                {
                                    {
                                        uint64_t t159 = gm_l18;
                                        gm_l26 = t159;
                                    }
                                    {
                                        uint64_t t160 = gm_l18;
                                        uint64_t t161 = gm_l23;
                                        uint64_t r162; bool o163 = __builtin_add_overflow(t160, t161, &r162);
                                        if (o163) gm_trap("integer overflow in `+`", gm_src0gtuh94b1fce, 161, 29);
                                        uint64_t t164 = r162;
                                        gm_l25 = t164;
                                    }
                                    {
                                        for (;;) {
                                            uint64_t t166 = gm_l26;
                                            uint64_t t167 = gm_l25;
                                            uint8_t t168 = (t166 < t167);
                                            if (!t168) goto lb165;
                                            {
                                                uint64_t t171 = gm_l26;
                                                uint64_t t174 = GM_LD(uint64_t, ((uint8_t *)&t171));
                                                gm_l27 = t174;
                                                {
                                                    uint64_t t177 = gm_l27;
                                                    uint64_t t178 = (uintptr_t)t177;
                                                    uint64_t t179 = GM_LD(uintptr_t, (((uint8_t *)&F.l5) + 0) + 8);
                                                    if (t178 >= (uintptr_t)(t179)) gm_trap_index(t178, t179, gm_src0gtuh94b1fce, 162, 24);
                                                    uint8_t t180 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l5) + 0)) + t178 * 1));
                                                    if (!t180) goto ie176;
                                                    {
                                                        gm_l24 = ((uint8_t)0);
                                                    }
                                                    goto ix176;
ie176:;
ix176:;
                                                }
                                                goto me172;
mn173:;
                                                gm_trap("no match arm matched the scrutinee", gm_src0gtuh94b1fce, 161, 17);
me172:;
                                            }
lc165:;
                                            {
                                                uint64_t t183 = gm_l26;
                                                uint64_t r184; bool o185 = __builtin_add_overflow(t183, ((uint64_t)1ULL), &r184);
                                                uint64_t t186 = r184;
                                                gm_l26 = t186;
                                            }
                                        }
lb165:;
                                    }
                                }
                                {
                                    uint8_t t188 = gm_l24;
                                    if (!t188) goto ie187;
                                    {
                                        uint64_t t190 = gm_l23;
                                        gm_l20 = t190;
                                    }
                                    {
                                        goto lb131;
                                    }
                                    goto ix187;
ie187:;
ix187:;
                                }
                                goto ix145;
ie145:;
ix145:;
                            }
                            goto me141;
mn142:;
                            gm_trap("no match arm matched the scrutinee", gm_src0gtuh94b1fce, 158, 9);
me141:;
                        }
lc131:;
                        {
                            uint64_t t192 = gm_l22;
                            uint64_t r193; bool o194 = __builtin_add_overflow(t192, ((uint64_t)1ULL), &r193);
                            uint64_t t195 = r193;
                            gm_l22 = t195;
                        }
                    }
lb131:;
                }
            }
            {
                void * t196 = (void *)((uint8_t *)&F.l17);
                uint64_t t197 = gm_l18;
                uint64_t t198 = gm_l20;
                gm_t2lqu8jsj511d t199;
                GM_ST(uint64_t, ((uint8_t *)&t199 + 0), t197);
                GM_ST(uint64_t, ((uint8_t *)&t199 + 8), t198);
                gm_tftb1odc739t5 t200 = gm_f_push_p5tbf4u(t196, t199);
                if (((((uint8_t *)&t200))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t200) + 1, gm_src0gtuh94b1fce, 170, 9);
                gm_b0_1 t201 = GM_LD(gm_b0_1, (((uint8_t *)&t200) + 1));
            }
            {
                uint64_t t202 = gm_l18;
                uint64_t t203 = gm_l20;
                uint64_t r204; bool o205 = __builtin_add_overflow(t202, t203, &r204);
                if (o205) gm_trap("integer overflow in `+`", gm_src0gtuh94b1fce, 171, 9);
                uint64_t t206 = r204;
                gm_l18 = t206;
            }
lc112:;
        }
lb112:;
    }
    {
        void * t208 = gm_l0;
        void * t209 = GM_LD(void *, (((uint8_t *)t208) + 0));
        uint8_t t210 = GM_LD(uint8_t, ((((uint8_t *)t209) + 1136) + 1));
        if (!t210) goto ie207;
        uint64_t t211 = GM_LD(uintptr_t, (((uint8_t *)&F.l17) + 0) + 8);
        uint8_t t212 = (t211 > ((uint64_t)1ULL));
        if (!t212) goto ie207;
        uint64_t t213 = gm_l4;
        uint8_t t214 = (t213 <= ((uint64_t)64ULL));
        if (!t214) goto ie207;
        void * t215 = gm_l0;
        void * t216 = GM_LD(void *, (((uint8_t *)t215) + 0));
        uint8_t t217 = GM_LD(uint8_t, ((((((uint8_t *)t216) + 8) + 8) + 0) + 56));
        uint8_t t218 = (uint8_t)!t217;
        if (!t218) goto ie207;
        {
            void * t220 = gm_l0;
            gm_t6j6smj7qoctu t221;
            gm_b16_8 t222 = gm_l2;
            GM_ST(gm_b16_8, (uint8_t *)&t221 + 0, t222);
            gm_b16_8 t223 = gm_l2;
            GM_ST(gm_b16_8, (uint8_t *)&t221 + 16, t223);
            gm_b0_1 t224 = gm_f_linef_cf5thm0((void *)NULL, cx, NULL, t220, &t221);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_b0_1 t225;
            uint8_t t226 = 0;
            F.ret = t225;
            goto gm_fret;
        }
        goto ix207;
ie207:;
ix207:;
    }
    {
        {
            gm_b16_8 t228 = GM_LD(gm_b16_8, (((uint8_t *)&F.l17) + 0));
            gm_l28 = t228;
        }
        {
            gm_l29 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t230 = gm_l29;
                uint64_t t231 = GM_LD(uintptr_t, ((uint8_t *)&gm_l28) + 8);
                uint8_t t232 = (t230 < t231);
                if (!t232) goto lb229;
                {
                    uint64_t t235 = gm_l29;
                    uint64_t t236 = (uintptr_t)t235;
                    uint64_t t237 = GM_LD(uintptr_t, ((uint8_t *)&gm_l28) + 8);
                    gm_t2lqu8jsj511d t240 = GM_LD(gm_t2lqu8jsj511d, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l28)) + t236 * 16));
                    gm_l30 = t240;
                    {
                        void * t242 = gm_l0;
                        gm_tob1snuii5bme t243;
                        uint64_t t244 = GM_LD(uint64_t, (((uint8_t *)&gm_l30) + 8));
                        uint64_t r245; bool o246 = __builtin_mul_overflow(t244, ((uint64_t)8ULL), &r245);
                        if (o246) gm_trap("integer overflow in `*`", gm_src0gtuh94b1fce, 177, 83);
                        uint64_t t247 = r245;
                        GM_ST(uint64_t, (uint8_t *)&t243 + 0, t247);
                        gm_b16_8 t248 = gm_l2;
                        GM_ST(gm_b16_8, (uint8_t *)&t243 + 8, t248);
                        uint64_t t249 = GM_LD(uint64_t, (((uint8_t *)&gm_l30) + 0));
                        GM_ST(uint64_t, (uint8_t *)&t243 + 24, t249);
                        gm_b0_1 t250 = gm_f_linef_aoq4lf0((void *)NULL, cx, NULL, t242, &t243);
                        if (gm_unw.kind) {
                            goto gm_ll233;
                        }
                    }
                    goto me238;
mn239:;
                    gm_trap("no match arm matched the scrutinee", gm_src0gtuh94b1fce, 177, 5);
me238:;
                }
lc229:;
                {
                    uint64_t t252 = gm_l29;
                    uint64_t r253; bool o254 = __builtin_add_overflow(t252, ((uint64_t)1ULL), &r253);
                    uint64_t t255 = r253;
                    gm_l29 = t255;
                }
            }
gm_ll233:;
            if (gm_unw.frame == &F.h && gm_unw.label == 7) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb229; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc229; }
            }
            goto gm_lad;
lb229:;
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
__attribute__((aligned(4))) void gm_f_coerce_gphlj1h(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tmn7n0n9raou5 a2, void * a3) {
    struct gm_fr_coerce_gphlj1h F;
    void * gm_l0; /* fg */
    void * gm_l1; /* e */
    gm_tmn7n0n9raou5 gm_l2; /* kind */
    void * gm_l3; /* x */
    gm_b16_8 gm_l4; /* v */
    gm_t88rcucg1b7uk gm_l5; /* t */
    gm_b16_8 gm_l6; /* c */
    uint32_t gm_l7; /* variant */
    gm_b16_8 gm_l8; /* a */
    gm_t88rcucg1b7uk gm_l9; /* e1 */
    gm_t88rcucg1b7uk gm_l10; /* ok */
    gm_t88rcucg1b7uk gm_l11; /* e2 */
    gm_b16_8 gm_l12; /* res */
    gm_b16_8 gm_l13; /* bad */
    uint64_t gm_l14; /* off */
    gm_b16_8 gm_l15; /* ev */
    gm_b16_8 gm_l16; /* wrapped */
    gm_b16_8 gm_l17; /* r */
    gm_b16_8 gm_l18; /* ov */
    gm_b16_8 gm_l19; /* r2 */
    gm_b16_8 gm_l20; /* a */
    gm_t88rcucg1b7uk gm_l21; /* pointee */
    uint64_t gm_l22; /* len */
    gm_b16_8 gm_l23; /* code */
    GM_STACK_CHECK(&F, 3484u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        void * t3 = gm_l3;
        gm_t5o0tpv5jka2l t4;
        gm_f_expr_porbc9t((void *)&t4, (void *)NULL, cx, NULL, t2, t3);
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
        gm_l4 = t6;
    }
    {
        void * t7 = gm_l1;
        gm_t88rcucg1b7uk t8 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t7) + 0));
        gm_l5 = t8;
    }
    {
        gm_tmn7n0n9raou5 t9 = gm_l2;
        uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)&t9) + 0));
        switch (t22) { case ((uint8_t)0ULL): goto ma11; case ((uint8_t)1ULL): goto ma11; case ((uint8_t)2ULL): goto ma12; case ((uint8_t)3ULL): goto ma13; case ((uint8_t)4ULL): goto ma14; case ((uint8_t)5ULL): goto ma15; case ((uint8_t)6ULL): goto ma17; case ((uint8_t)7ULL): goto ma18; case ((uint8_t)8ULL): goto ma17; case ((uint8_t)9ULL): goto ma19; case ((uint8_t)10ULL): goto ma16; case ((uint8_t)11ULL): goto ma17; case ((uint8_t)12ULL): goto ma20; default: goto ma21; }
ma11:;
        uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&t9) + 0));
        if (t26 != ((uint8_t)0ULL)) goto pa25;
        goto po24;
pa25:;
        uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)&t9) + 0));
        if (t28 != ((uint8_t)1ULL)) goto pa27;
        goto po24;
pa27:;
        goto mn23;
po24:;
        {
            void * t30 = gm_l0;
            void * t31 = GM_LD(void *, (((uint8_t *)t30) + 0));
            void * t32 = (void *)(((uint8_t *)t31) + 8);
            gm_t88rcucg1b7uk t33 = gm_l5;
            gm_b16_8 t34 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t32, t33);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l6 = t34;
        }
        {
            void * t35 = gm_l0;
            gm_t88rcucg1b7uk t36 = gm_l5;
            gm_t6j6smj7qoctu t37;
            gm_b16_8 t38 = gm_l6;
            GM_ST(gm_b16_8, (uint8_t *)&t37 + 0, t38);
            gm_b16_8 t39 = gm_l4;
            GM_ST(gm_b16_8, (uint8_t *)&t37 + 16, t39);
            gm_tabb710d9mq2e t40;
            gm_f_format_3vtl3te((void *)&t40, (void *)NULL, cx, NULL, &t37);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t40))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t40) + 8, gm_src0gtuh94b1fce, 1148, 30);
            gm_b16_8 t41 = GM_LD(gm_b16_8, (((uint8_t *)&t40) + 8));
            gm_b16_8 t42 = GM_LD(gm_b16_8, ((uint8_t *)&t41));
            gm_b16_8 t43 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t35, t36, t42);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
            ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t43);
            goto gm_fdone;
        }
        goto me10;
mn23:;
ma12:;
        uint8_t t45 = GM_LD(uint8_t, (((uint8_t *)&t9) + 0));
        if (t45 != ((uint8_t)2ULL)) goto mn44;
        void * t46 = gm_l0;
        gm_t88rcucg1b7uk t47 = gm_l5;
        gm_b16_8 t48 = gm_l4;
        gm_b16_8 t49 = gm_f_make_some_9aqq928((void *)NULL, cx, NULL, t46, t47, t48);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t49);
        goto gm_fdone;
        goto me10;
mn44:;
ma13:;
        uint8_t t51 = GM_LD(uint8_t, (((uint8_t *)&t9) + 0));
        if (t51 != ((uint8_t)3ULL)) goto mn50;
        void * t52 = gm_l0;
        gm_t88rcucg1b7uk t53 = gm_l5;
        void * t54 = gm_l3;
        gm_t88rcucg1b7uk t55 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t54) + 0));
        gm_b16_8 t56 = gm_l4;
        gm_b16_8 t57 = gm_f_make_res_riv19po((void *)NULL, cx, NULL, t52, t53, ((uint8_t)0), t55, t56);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t57);
        goto gm_fdone;
        goto me10;
mn50:;
ma14:;
        uint8_t t59 = GM_LD(uint8_t, (((uint8_t *)&t9) + 0));
        if (t59 != ((uint8_t)4ULL)) goto mn58;
        uint32_t t60 = GM_LD(uint32_t, (((uint8_t *)&t9) + 4));
        gm_l7 = t60;
        {
            void * t62 = gm_l0;
            void * t63 = gm_l3;
            gm_t88rcucg1b7uk t64 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t63) + 0));
            gm_b16_8 t65 = gm_l4;
            gm_b16_8 t66 = gm_f_addr_of_hiaqkn0((void *)NULL, cx, NULL, t62, t64, t65);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l8 = t66;
        }
        {
            void * t68 = gm_l0;
            void * t69 = GM_LD(void *, (((uint8_t *)t68) + 0));
            void * t70 = (void *)(((uint8_t *)t69) + 8);
            void * t71 = gm_l3;
            gm_t88rcucg1b7uk t72 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t71) + 0));
            gm_t53at80pfdus3 t73;
            gm_f_kind_ghvav22((void *)&t73, t70, GM_CONV(gm_csguankko4lmhd, t72));
            uint8_t t74 = GM_LD(uint8_t, (((uint8_t *)&t73) + 0));
            if (t74 != ((uint8_t)14ULL)) goto ie67;
            gm_t88rcucg1b7uk t75 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t73) + 8) + 0));
            gm_l9 = t75;
            gm_t88rcucg1b7uk t76 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t73) + 8) + 4));
            gm_l10 = t76;
            {
                void * t79 = gm_l0;
                void * t80 = GM_LD(void *, (((uint8_t *)t79) + 0));
                void * t81 = (void *)(((uint8_t *)t80) + 8);
                gm_t88rcucg1b7uk t82 = gm_l5;
                gm_t53at80pfdus3 t83;
                gm_f_kind_ghvav22((void *)&t83, t81, GM_CONV(gm_csguankko4lmhd, t82));
                uint8_t t84 = GM_LD(uint8_t, (((uint8_t *)&t83) + 0));
                if (t84 != ((uint8_t)14ULL)) goto ie78;
                gm_t88rcucg1b7uk t85 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t83) + 8) + 0));
                gm_l11 = t85;
                {
                    void * t87 = gm_l0;
                    gm_t88rcucg1b7uk t88 = gm_l5;
                    gm_b16_8 t89 = gm_f_tmp_uninit_jr39s5u((void *)NULL, cx, NULL, t87, t88);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l12 = t89;
                }
                {
                    void * t90 = gm_l0;
                    void * t91 = gm_l3;
                    gm_t88rcucg1b7uk t92 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t91) + 0));
                    gm_b16_8 t93 = gm_l8;
                    gm_b16_8 t94 = gm_f_res_is_err_11p8qlp((void *)NULL, cx, NULL, t90, t92, t93);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l13 = t94;
                }
                {
                    uint64_t t95 = (uintptr_t)((uint64_t)1ULL);
                    void * t96 = gm_l0;
                    void * t97 = GM_LD(void *, (((uint8_t *)t96) + 0));
                    void * t98 = (void *)(((uint8_t *)t97) + 8);
                    void * t99 = gm_l3;
                    gm_t88rcucg1b7uk t100 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t99) + 0));
                    gm_b16_8 t101 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t98, t100);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint64_t t102 = GM_LD(uintptr_t, ((uint8_t *)&t101) + 8);
                    if (t95 >= (uintptr_t)(t102)) gm_trap_index(t95, t102, gm_src0gtuh94b1fce, 1158, 20);
                    uint64_t t103 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t101)) + t95 * 8));
                    gm_l14 = t103;
                }
                {
                    void * t104 = gm_l0;
                    gm_b16_8 t105 = gm_l13;
                    gm_tnpifv20irnph t106;
                    GM_ST(gm_b16_8, ((uint8_t *)&t106 + 0), t105);
                    gm_b0_1 t107 = gm_f_linef_g6msml8((void *)NULL, cx, NULL, t104, t106);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    void * t108 = gm_l0;
                    void * t109 = gm_l0;
                    uint64_t t110 = GM_LD(uint64_t, (((uint8_t *)t109) + 224));
                    uint64_t r111; bool o112 = __builtin_add_overflow(t110, ((uint64_t)1ULL), &r111);
                    if (o112) gm_trap("integer overflow in `+`", gm_src0gtuh94b1fce, 1160, 13);
                    uint64_t t113 = r111;
                    GM_ST(uint64_t, (((uint8_t *)t108) + 224), t113);
                }
                {
                    void * t114 = gm_l0;
                    gm_t88rcucg1b7uk t115 = gm_l9;
                    gm_t3877pk7lv748 t116;
                    gm_b16_8 t117 = gm_l8;
                    GM_ST(gm_b16_8, (uint8_t *)&t116 + 0, t117);
                    uint64_t t118 = gm_l14;
                    GM_ST(uint64_t, (uint8_t *)&t116 + 16, t118);
                    gm_tabb710d9mq2e t119;
                    gm_f_format_gjujk09((void *)&t119, (void *)NULL, cx, NULL, &t116);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t119))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t119) + 8, gm_src0gtuh94b1fce, 1161, 31);
                    gm_b16_8 t120 = GM_LD(gm_b16_8, (((uint8_t *)&t119) + 8));
                    gm_b16_8 t121 = GM_LD(gm_b16_8, ((uint8_t *)&t120));
                    gm_b16_8 t122 = gm_f_load_0hebde7((void *)NULL, cx, NULL, t114, t115, t121);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l15 = t122;
                }
                {
                    void * t123 = gm_l0;
                    gm_t88rcucg1b7uk t124 = gm_l11;
                    uint32_t t125 = gm_l7;
                    uint64_t t126 = (uint64_t)t125;
                    gm_b16_8 t127 = gm_l15;
                    gm_th2lqbo77pvsc t128;
                    GM_ST(uint8_t, (uint8_t *)&t128 + 17, 0);
                    GM_ST(uint16_t, (uint8_t *)&t128 + 18, 0);
                    GM_ST(uint32_t, (uint8_t *)&t128 + 20, 0);
                    GM_ST(gm_b16_8, ((uint8_t *)&t128), t127);
                    ((uint8_t *)&t128)[16] = 1;
                    gm_b16_8 t129 = gm_f_make_variant_srnk8ha((void *)NULL, cx, NULL, t123, t124, t126, &t128);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l16 = t129;
                }
                {
                    void * t130 = gm_l0;
                    gm_t88rcucg1b7uk t131 = gm_l5;
                    gm_t88rcucg1b7uk t132 = gm_l11;
                    gm_b16_8 t133 = gm_l16;
                    gm_b16_8 t134 = gm_f_make_res_riv19po((void *)NULL, cx, NULL, t130, t131, ((uint8_t)1), t132, t133);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l17 = t134;
                }
                {
                    void * t135 = gm_l0;
                    gm_t6j6smj7qoctu t136;
                    gm_b16_8 t137 = gm_l12;
                    GM_ST(gm_b16_8, (uint8_t *)&t136 + 0, t137);
                    gm_b16_8 t138 = gm_l17;
                    GM_ST(gm_b16_8, (uint8_t *)&t136 + 16, t138);
                    gm_b0_1 t139 = gm_f_linef_uu8i6ue((void *)NULL, cx, NULL, t135, &t136);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    void * t140 = gm_l0;
                    void * t141 = gm_l0;
                    uint64_t t142 = GM_LD(uint64_t, (((uint8_t *)t141) + 224));
                    uint64_t r143; bool o144 = __builtin_sub_overflow(t142, ((uint64_t)1ULL), &r143);
                    if (o144) gm_trap("integer overflow in `-`", gm_src0gtuh94b1fce, 1165, 13);
                    uint64_t t145 = r143;
                    GM_ST(uint64_t, (((uint8_t *)t140) + 224), t145);
                }
                {
                    void * t146 = gm_l0;
                    gm_b16_8 t147;
                    GM_ST(void *, (uint8_t *)&t147, (void *)(gm_s02uuedsru1qu));
                    GM_ST(uintptr_t, (uint8_t *)&t147 + 8, (uintptr_t)(8));
                    gm_b0_1 t148 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t146, t147);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    void * t149 = gm_l0;
                    void * t150 = gm_l0;
                    uint64_t t151 = GM_LD(uint64_t, (((uint8_t *)t150) + 224));
                    uint64_t r152; bool o153 = __builtin_add_overflow(t151, ((uint64_t)1ULL), &r152);
                    if (o153) gm_trap("integer overflow in `+`", gm_src0gtuh94b1fce, 1167, 13);
                    uint64_t t154 = r152;
                    GM_ST(uint64_t, (((uint8_t *)t149) + 224), t154);
                }
                {
                    void * t155 = gm_l0;
                    gm_t88rcucg1b7uk t156 = gm_l10;
                    gm_t3877pk7lv748 t157;
                    gm_b16_8 t158 = gm_l8;
                    GM_ST(gm_b16_8, (uint8_t *)&t157 + 0, t158);
                    uint64_t t159 = gm_l14;
                    GM_ST(uint64_t, (uint8_t *)&t157 + 16, t159);
                    gm_tabb710d9mq2e t160;
                    gm_f_format_gjujk09((void *)&t160, (void *)NULL, cx, NULL, &t157);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t160))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t160) + 8, gm_src0gtuh94b1fce, 1168, 31);
                    gm_b16_8 t161 = GM_LD(gm_b16_8, (((uint8_t *)&t160) + 8));
                    gm_b16_8 t162 = GM_LD(gm_b16_8, ((uint8_t *)&t161));
                    gm_b16_8 t163 = gm_f_load_0hebde7((void *)NULL, cx, NULL, t155, t156, t162);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l18 = t163;
                }
                {
                    void * t164 = gm_l0;
                    gm_t88rcucg1b7uk t165 = gm_l5;
                    gm_t88rcucg1b7uk t166 = gm_l10;
                    gm_b16_8 t167 = gm_l18;
                    gm_b16_8 t168 = gm_f_make_res_riv19po((void *)NULL, cx, NULL, t164, t165, ((uint8_t)0), t166, t167);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l19 = t168;
                }
                {
                    void * t169 = gm_l0;
                    gm_t6j6smj7qoctu t170;
                    gm_b16_8 t171 = gm_l12;
                    GM_ST(gm_b16_8, (uint8_t *)&t170 + 0, t171);
                    gm_b16_8 t172 = gm_l19;
                    GM_ST(gm_b16_8, (uint8_t *)&t170 + 16, t172);
                    gm_b0_1 t173 = gm_f_linef_uu8i6ue((void *)NULL, cx, NULL, t169, &t170);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    void * t174 = gm_l0;
                    void * t175 = gm_l0;
                    uint64_t t176 = GM_LD(uint64_t, (((uint8_t *)t175) + 224));
                    uint64_t r177; bool o178 = __builtin_sub_overflow(t176, ((uint64_t)1ULL), &r177);
                    if (o178) gm_trap("integer overflow in `-`", gm_src0gtuh94b1fce, 1171, 13);
                    uint64_t t179 = r177;
                    GM_ST(uint64_t, (((uint8_t *)t174) + 224), t179);
                }
                {
                    void * t180 = gm_l0;
                    gm_b16_8 t181;
                    GM_ST(void *, (uint8_t *)&t181, (void *)(gm_s2cuflhqkkuaq));
                    GM_ST(uintptr_t, (uint8_t *)&t181 + 8, (uintptr_t)(1));
                    gm_b0_1 t182 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t180, t181);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    gm_b16_8 t183 = gm_l12;
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
                    ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
                    GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t183);
                    goto gm_fdone;
                }
                goto ix78;
ie78:;
                {
                    void * t185 = gm_l1;
                    gm_tl874t2atnlh3 t186 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t185) + 72));
                    gm_b16_8 t187;
                    GM_ST(void *, (uint8_t *)&t187, (void *)(gm_s43jqu37ghtoq));
                    GM_ST(uintptr_t, (uint8_t *)&t187 + 8, (uintptr_t)(18));
                    gm_t08olq97rscn3 t188;
                    gm_f_unsupported_531q51r((void *)&t188, (void *)NULL, cx, NULL, t186, t187);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 1;
                    GM_ST(gm_t08olq97rscn3, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t188);
                    goto gm_fdone;
                }
ix78:;
            }
            goto ix67;
ie67:;
            {
                void * t190 = gm_l1;
                gm_tl874t2atnlh3 t191 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t190) + 72));
                gm_b16_8 t192;
                GM_ST(void *, (uint8_t *)&t192, (void *)(gm_s43jqu37ghtoq));
                GM_ST(uintptr_t, (uint8_t *)&t192 + 8, (uintptr_t)(18));
                gm_t08olq97rscn3 t193;
                gm_f_unsupported_531q51r((void *)&t193, (void *)NULL, cx, NULL, t191, t192);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 1;
                GM_ST(gm_t08olq97rscn3, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t193);
                goto gm_fdone;
            }
ix67:;
        }
        goto me10;
mn58:;
ma15:;
        uint8_t t195 = GM_LD(uint8_t, (((uint8_t *)&t9) + 0));
        if (t195 != ((uint8_t)5ULL)) goto mn194;
        void * t196 = gm_l0;
        gm_t88rcucg1b7uk t197 = gm_l5;
        gm_b16_8 t198 = gm_f_zero_tlk13aa((void *)NULL, cx, NULL, t196, t197);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t198);
        goto gm_fdone;
        goto me10;
mn194:;
ma16:;
        uint8_t t200 = GM_LD(uint8_t, (((uint8_t *)&t9) + 0));
        if (t200 != ((uint8_t)10ULL)) goto mn199;
        uint8_t t201 = 0;
        void * t202 = gm_l0;
        void * t203 = gm_l3;
        gm_t88rcucg1b7uk t204 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t203) + 0));
        uint8_t t205 = gm_f_ptr_type_plain_sanetd4((void *)NULL, cx, NULL, t202, t204);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (t205) {
            void * t206 = gm_l0;
            gm_t88rcucg1b7uk t207 = gm_l5;
            uint8_t t208 = gm_f_ptr_type_plain_sanetd4((void *)NULL, cx, NULL, t206, t207);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t209 = (uint8_t)!t208;
            t201 = t209;
        } else t201 = 0;
        if (!t201) goto mn199;
        void * t210 = gm_l0;
        gm_t88rcucg1b7uk t211 = gm_l5;
        gm_b16_8 t212 = gm_l4;
        gm_tnpifv20irnph t213;
        GM_ST(gm_b16_8, ((uint8_t *)&t213 + 0), t212);
        gm_tabb710d9mq2e t214;
        gm_f_format_8sjjbd9((void *)&t214, (void *)NULL, cx, NULL, t213);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t214))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t214) + 8, gm_src0gtuh94b1fce, 1177, 97);
        gm_b16_8 t215 = GM_LD(gm_b16_8, (((uint8_t *)&t214) + 8));
        gm_b16_8 t216 = GM_LD(gm_b16_8, ((uint8_t *)&t215));
        gm_b16_8 t217 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t210, t211, t216);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t217);
        goto gm_fdone;
        goto me10;
mn199:;
ma17:;
        uint8_t t221 = GM_LD(uint8_t, (((uint8_t *)&t9) + 0));
        if (t221 != ((uint8_t)6ULL)) goto pa220;
        goto po219;
pa220:;
        uint8_t t223 = GM_LD(uint8_t, (((uint8_t *)&t9) + 0));
        if (t223 != ((uint8_t)10ULL)) goto pa222;
        goto po219;
pa222:;
        uint8_t t225 = GM_LD(uint8_t, (((uint8_t *)&t9) + 0));
        if (t225 != ((uint8_t)8ULL)) goto pa224;
        goto po219;
pa224:;
        uint8_t t227 = GM_LD(uint8_t, (((uint8_t *)&t9) + 0));
        if (t227 != ((uint8_t)11ULL)) goto pa226;
        goto po219;
pa226:;
        goto mn218;
po219:;
        {
            void * t230 = gm_l0;
            void * t231 = GM_LD(void *, (((uint8_t *)t230) + 0));
            void * t232 = (void *)(((uint8_t *)t231) + 8);
            gm_t88rcucg1b7uk t233 = gm_l5;
            gm_t5v8n6qrn4rh4 t234;
            gm_f_repr_53quqki((void *)&t234, (void *)NULL, cx, NULL, t232, t233);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t235 = gm_l0;
            void * t236 = GM_LD(void *, (((uint8_t *)t235) + 0));
            void * t237 = (void *)(((uint8_t *)t236) + 8);
            void * t238 = gm_l3;
            gm_t88rcucg1b7uk t239 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t238) + 0));
            gm_t5v8n6qrn4rh4 t240;
            gm_f_repr_53quqki((void *)&t240, (void *)NULL, cx, NULL, t237, t239);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t241 = (gm_eqhcfklnokct(((uint8_t *)&t234), ((uint8_t *)&t240)));
            if (!t241) goto ie229;
            void * t242 = gm_l0;
            void * t243 = GM_LD(void *, (((uint8_t *)t242) + 0));
            void * t244 = (void *)(((uint8_t *)t243) + 8);
            gm_t88rcucg1b7uk t245 = gm_l5;
            gm_t5v8n6qrn4rh4 t246;
            gm_f_repr_53quqki((void *)&t246, (void *)NULL, cx, NULL, t244, t245);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t247 = (!(GM_LD(uint8_t, ((uint8_t *)&t246) + 0) == ((uint8_t)6ULL)));
            if (!t247) goto ie229;
            {
                gm_b16_8 t249 = gm_l4;
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
                ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t249);
                goto gm_fdone;
            }
            goto ix229;
ie229:;
ix229:;
        }
        {
            void * t250 = gm_l0;
            void * t251 = gm_l3;
            gm_t88rcucg1b7uk t252 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t251) + 0));
            gm_b16_8 t253 = gm_l4;
            gm_b16_8 t254 = gm_f_addr_of_hiaqkn0((void *)NULL, cx, NULL, t250, t252, t253);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l20 = t254;
        }
        {
            void * t255 = gm_l0;
            gm_t88rcucg1b7uk t256 = gm_l5;
            gm_b16_8 t257 = gm_l20;
            gm_b16_8 t258 = gm_f_load_0hebde7((void *)NULL, cx, NULL, t255, t256, t257);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
            ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t258);
            goto gm_fdone;
        }
        goto me10;
mn218:;
ma18:;
        uint8_t t260 = GM_LD(uint8_t, (((uint8_t *)&t9) + 0));
        if (t260 != ((uint8_t)7ULL)) goto mn259;
        {
            void * t263 = gm_l0;
            void * t264 = GM_LD(void *, (((uint8_t *)t263) + 0));
            void * t265 = (void *)(((uint8_t *)t264) + 8);
            void * t266 = gm_l3;
            gm_t88rcucg1b7uk t267 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t266) + 0));
            gm_t53at80pfdus3 t268;
            gm_f_kind_ghvav22((void *)&t268, t265, GM_CONV(gm_csguankko4lmhd, t267));
            uint8_t t269 = GM_LD(uint8_t, (((uint8_t *)&t268) + 0));
            if (t269 != ((uint8_t)8ULL)) goto ie262;
            gm_t88rcucg1b7uk t270 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t268) + 8) + 4));
            gm_l21 = t270;
            {
                void * t273 = gm_l0;
                void * t274 = GM_LD(void *, (((uint8_t *)t273) + 0));
                void * t275 = (void *)(((uint8_t *)t274) + 8);
                gm_t88rcucg1b7uk t276 = gm_l21;
                gm_t53at80pfdus3 t277;
                gm_f_kind_ghvav22((void *)&t277, t275, GM_CONV(gm_csguankko4lmhd, t276));
                uint8_t t278 = GM_LD(uint8_t, (((uint8_t *)&t277) + 0));
                if (t278 != ((uint8_t)11ULL)) goto ie272;
                uint64_t t279 = GM_LD(uint64_t, ((((uint8_t *)&t277) + 8) + 0));
                gm_l22 = t279;
                {
                    void * t281 = gm_l0;
                    gm_t88rcucg1b7uk t282 = gm_l5;
                    gm_b16_8 t283 = gm_l4;
                    uint64_t t284 = gm_l22;
                    gm_t4ce0jd3tdro4 t285;
                    GM_ST(uint64_t, ((uint8_t *)&t285 + 0), t284);
                    gm_tabb710d9mq2e t286;
                    gm_f_format_1amnh72((void *)&t286, (void *)NULL, cx, NULL, t285);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t286))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t286) + 8, gm_src0gtuh94b1fce, 1186, 40);
                    gm_b16_8 t287 = GM_LD(gm_b16_8, (((uint8_t *)&t286) + 8));
                    gm_b16_8 t288 = GM_LD(gm_b16_8, ((uint8_t *)&t287));
                    gm_b16_8 t289 = gm_f_make_slice_sa50efe((void *)NULL, cx, NULL, t281, t282, t283, t288);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
                    ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
                    GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t289);
                    goto gm_fdone;
                }
                goto ix272;
ie272:;
                {
                    void * t291 = gm_l1;
                    gm_tl874t2atnlh3 t292 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t291) + 72));
                    gm_b16_8 t293;
                    GM_ST(void *, (uint8_t *)&t293, (void *)(gm_spm6tgoorr19q));
                    GM_ST(uintptr_t, (uint8_t *)&t293 + 8, (uintptr_t)(28));
                    gm_t08olq97rscn3 t294;
                    gm_f_unsupported_531q51r((void *)&t294, (void *)NULL, cx, NULL, t292, t293);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 1;
                    GM_ST(gm_t08olq97rscn3, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t294);
                    goto gm_fdone;
                }
ix272:;
            }
            goto ix262;
ie262:;
            {
                void * t296 = gm_l1;
                gm_tl874t2atnlh3 t297 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t296) + 72));
                gm_b16_8 t298;
                GM_ST(void *, (uint8_t *)&t298, (void *)(gm_spm6tgoorr19q));
                GM_ST(uintptr_t, (uint8_t *)&t298 + 8, (uintptr_t)(28));
                gm_t08olq97rscn3 t299;
                gm_f_unsupported_531q51r((void *)&t299, (void *)NULL, cx, NULL, t297, t298);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 1;
                GM_ST(gm_t08olq97rscn3, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t299);
                goto gm_fdone;
            }
ix262:;
        }
        goto me10;
mn259:;
ma19:;
        uint8_t t301 = GM_LD(uint8_t, (((uint8_t *)&t9) + 0));
        if (t301 != ((uint8_t)9ULL)) goto mn300;
        {
            void * t304 = gm_l0;
            void * t305 = GM_LD(void *, (((uint8_t *)t304) + 0));
            void * t306 = (void *)(((uint8_t *)t305) + 8);
            void * t307 = gm_l3;
            gm_t88rcucg1b7uk t308 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t307) + 0));
            gm_t5v8n6qrn4rh4 t309;
            gm_f_repr_53quqki((void *)&t309, (void *)NULL, cx, NULL, t306, t308);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t310 = ((GM_LD(uint8_t, ((uint8_t *)&t309) + 0) == ((uint8_t)3ULL)));
            if (!t310) goto ie303;
            void * t311 = gm_l0;
            void * t312 = GM_LD(void *, (((uint8_t *)t311) + 0));
            void * t313 = (void *)(((uint8_t *)t312) + 8);
            gm_t88rcucg1b7uk t314 = gm_l5;
            gm_t5v8n6qrn4rh4 t315;
            gm_f_repr_53quqki((void *)&t315, (void *)NULL, cx, NULL, t313, t314);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t316 = ((GM_LD(uint8_t, ((uint8_t *)&t315) + 0) == ((uint8_t)4ULL)));
            if (!t316) goto ie303;
            {
                gm_b16_8 t318;
                void * t320 = gm_l0;
                void * t321 = gm_l3;
                gm_t88rcucg1b7uk t322 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t321) + 0));
                uint8_t t323 = gm_f_ptr_type_plain_sanetd4((void *)NULL, cx, NULL, t320, t322);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (!t323) goto ie319;
                gm_b16_8 t325 = gm_l4;
                gm_tnpifv20irnph t326;
                GM_ST(gm_b16_8, ((uint8_t *)&t326 + 0), t325);
                gm_tabb710d9mq2e t327;
                gm_f_format_8sjjbd9((void *)&t327, (void *)NULL, cx, NULL, t326);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t327))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t327) + 8, gm_src0gtuh94b1fce, 1190, 61);
                gm_b16_8 t328 = GM_LD(gm_b16_8, (((uint8_t *)&t327) + 8));
                gm_b16_8 t329 = t328;
                gm_b16_8 t330 = GM_LD(gm_b16_8, ((uint8_t *)&t329));
                t318 = t330;
                goto ix319;
ie319:;
                gm_b16_8 t332 = gm_l4;
                gm_b16_8 t333 = t332;
                t318 = t333;
ix319:;
                gm_l23 = t318;
            }
            {
                void * t334 = gm_l0;
                gm_t88rcucg1b7uk t335 = gm_l5;
                gm_t6j6smj7qoctu t336;
                gm_b16_8 t337 = gm_l23;
                GM_ST(gm_b16_8, (uint8_t *)&t336 + 0, t337);
                void * t338 = gm_l0;
                gm_b16_8 t339 = GM_LD(gm_b16_8, (((uint8_t *)t338) + 280));
                GM_ST(gm_b16_8, (uint8_t *)&t336 + 16, t339);
                gm_tabb710d9mq2e t340;
                gm_f_format_0017fui((void *)&t340, (void *)NULL, cx, NULL, &t336);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t340))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t340) + 8, gm_src0gtuh94b1fce, 1191, 34);
                gm_b16_8 t341 = GM_LD(gm_b16_8, (((uint8_t *)&t340) + 8));
                gm_b16_8 t342 = GM_LD(gm_b16_8, ((uint8_t *)&t341));
                gm_b16_8 t343 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t334, t335, t342);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
                ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t343);
                goto gm_fdone;
            }
            goto ix303;
ie303:;
ix303:;
        }
        {
            gm_b16_8 t344 = gm_l4;
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
            ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t344);
            goto gm_fdone;
        }
        goto me10;
mn300:;
ma20:;
        uint8_t t346 = GM_LD(uint8_t, (((uint8_t *)&t9) + 0));
        if (t346 != ((uint8_t)12ULL)) goto mn345;
        void * t347 = gm_l1;
        gm_tl874t2atnlh3 t348 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t347) + 72));
        gm_b16_8 t349;
        GM_ST(void *, (uint8_t *)&t349, (void *)(gm_sfrlts7iu6kt3));
        GM_ST(uintptr_t, (uint8_t *)&t349 + 8, (uintptr_t)(22));
        gm_t08olq97rscn3 t350;
        gm_f_unsupported_531q51r((void *)&t350, (void *)NULL, cx, NULL, t348, t349);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 1;
        GM_ST(gm_t08olq97rscn3, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t350);
        goto gm_fdone;
        goto me10;
mn345:;
ma21:;
        gm_trap("no match arm matched the scrutinee", gm_src0gtuh94b1fce, 1145, 5);
me10:;
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
inline __attribute__((aligned(4))) void gm_f_continue_expr_6bhkncl(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_te9hrk0u35rev a2) {
    struct gm_fr_continue_expr_6bhkncl F;
    void * gm_l0; /* fg */
    void * gm_l1; /* e */
    gm_te9hrk0u35rev gm_l2; /* target */
    gm_t88rcucg1b7uk gm_l3; /* t */
    uint32_t gm_l4; /* levels */
    uint32_t gm_l5; /* lbl */
    gm_b16_8 gm_l6; /* fr */
    gm_b16_8 gm_l7; /* u */
    GM_STACK_CHECK(&F, 408u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l1;
        gm_t88rcucg1b7uk t3 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t2) + 0));
        gm_l3 = t3;
    }
    {
        uint8_t t6 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t6 != ((uint8_t)0ULL)) goto mn5;
        uint32_t t7 = GM_LD(uint32_t, ((((uint8_t *)&gm_l2) + 4) + 0));
        gm_l4 = t7;
        uint32_t t8 = GM_LD(uint32_t, ((((uint8_t *)&gm_l2) + 4) + 4));
        gm_l5 = t8;
        {
            void * t10 = gm_l0;
            uint32_t t11 = gm_l4;
            gm_tommf4j9upomc t12;
            gm_f_frame_up_g8486eu((void *)&t12, (void *)NULL, cx, NULL, t10, t11);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tommf4j9upomc t13 = t12;
            gm_l6 = GM_LD(gm_b16_8, (uint8_t *)&t13 + 0);
        }
        {
            void * t14 = gm_l0;
            gm_t9qkh0rsooc8d t15;
            GM_ST(uint8_t, ((uint8_t *)&t15 + 0), ((uint8_t)2ULL));
            gm_b16_8 t16 = gm_l6;
            uint32_t t17 = gm_l5;
            gm_t88rcucg1b7uk t18;
            GM_ST(uint32_t, ((uint8_t *)&t18 + 0), t17);
            gm_tabb710d9mq2e t19;
            gm_f_format_32n178h((void *)&t19, (void *)NULL, cx, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t19))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t19) + 8, gm_src0gtuh94b1fce, 748, 42);
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&t19) + 8));
            gm_b16_8 t21 = GM_LD(gm_b16_8, ((uint8_t *)&t20));
            gm_b16_8 t22;
            GM_ST(void *, (uint8_t *)&t22, (void *)(gm_sicgmpca63jln));
            GM_ST(uintptr_t, (uint8_t *)&t22 + 8, (uintptr_t)(1));
            gm_b0_1 t23 = gm_f_start_exit_m57dnn5((void *)NULL, cx, NULL, t14, t15, t16, t21, t22);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto me4;
mn5:;
        uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t25 != ((uint8_t)1ULL)) goto mn24;
        {
            void * t27 = gm_l0;
            gm_b16_8 t28 = gm_f_unit_8ekedsl((void *)NULL, cx, NULL, t27);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l7 = t28;
        }
        {
            void * t29 = gm_l0;
            gm_b16_8 t30 = gm_l7;
            gm_tnpifv20irnph t31;
            GM_ST(gm_b16_8, ((uint8_t *)&t31 + 0), t30);
            gm_b0_1 t32 = gm_f_linef_q912sub((void *)NULL, cx, NULL, t29, t31);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t33 = gm_l0;
            gm_t9qkh0rsooc8d t34;
            GM_ST(uint8_t, ((uint8_t *)&t34 + 0), ((uint8_t)0ULL));
            gm_b16_8 t35;
            GM_ST(void *, (uint8_t *)&t35, (void *)(gm_sb8kl54a499bf));
            GM_ST(uintptr_t, (uint8_t *)&t35 + 8, (uintptr_t)(6));
            gm_b16_8 t36;
            GM_ST(void *, (uint8_t *)&t36, (void *)(gm_sicgmpca63jln));
            GM_ST(uintptr_t, (uint8_t *)&t36 + 8, (uintptr_t)(1));
            gm_b16_8 t37;
            GM_ST(void *, (uint8_t *)&t37, (void *)(gm_sicgmpca63jln));
            GM_ST(uintptr_t, (uint8_t *)&t37 + 8, (uintptr_t)(1));
            gm_b0_1 t38 = gm_f_start_exit_m57dnn5((void *)NULL, cx, NULL, t33, t34, t35, t36, t37);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto me4;
mn24:;
        gm_trap("no match arm matched the scrutinee", gm_src0gtuh94b1fce, 745, 5);
me4:;
    }
    {
        void * t39 = gm_l0;
        gm_t88rcucg1b7uk t40 = gm_l3;
        gm_b16_8 t41 = gm_f_zero_tlk13aa((void *)NULL, cx, NULL, t39, t40);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t41);
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
__attribute__((aligned(4))) void gm_f_else_expr_lvoo6l1(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2) {
    struct gm_fr_else_expr_lvoo6l1 F;
    void * gm_l0; /* fg */
    void * gm_l1; /* e */
    void * gm_l2; /* el */
    gm_t88rcucg1b7uk gm_l3; /* t */
    void * gm_l4; /* lhs */
    gm_b16_8 gm_l5; /* l */
    gm_b16_8 gm_l6; /* a */
    gm_th2lqbo77pvsc gm_l7; /* res */
    gm_b16_8 gm_l8; /* present */
    gm_t88rcucg1b7uk gm_l9; /* payload_ty */
    uint64_t gm_l10; /* payload_off */
    gm_t88rcucg1b7uk gm_l11; /* p */
    gm_t88rcucg1b7uk gm_l12; /* ok */
    gm_b16_8 gm_l13; /* bad */
    gm_b16_8 gm_l14; /* r */
    gm_b16_8 gm_l15; /* v */
    gm_b16_8 gm_l16; /* p */
    gm_b16_8 gm_l17; /* p */
    uint32_t gm_l18; /* c */
    gm_t88rcucg1b7uk gm_l19; /* err */
    uint64_t gm_l20; /* off */
    gm_b16_8 gm_l21; /* ev */
    gm_b16_8 gm_l22; /* lv */
    gm_b16_8 gm_l23; /* rv */
    gm_b16_8 gm_l24; /* r */
    gm_b16_8 gm_l25; /* r */
    GM_STACK_CHECK(&F, 2712u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l1;
        gm_t88rcucg1b7uk t3 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t2) + 0));
        gm_l3 = t3;
    }
    {
        void * t4 = gm_l2;
        void * t5 = GM_LD(void *, (((uint8_t *)t4) + 0));
        gm_l4 = t5;
    }
    {
        void * t6 = gm_l0;
        void * t7 = gm_l4;
        gm_t5o0tpv5jka2l t8;
        gm_f_expr_porbc9t((void *)&t8, (void *)NULL, cx, NULL, t6, t7);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t8))[0] != 0)) {
            gm_t5o0tpv5jka2l t9;
            GM_ST(uint8_t, (uint8_t *)&t9 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t9 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t9 + 4, 0);
            ((uint8_t *)&t9)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t9 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t8) + 8)));
            F.ret = t9;
            goto gm_fret;
        }
        gm_b16_8 t10 = GM_LD(gm_b16_8, (((uint8_t *)&t8) + 8));
        gm_l5 = t10;
    }
    {
        void * t11 = gm_l0;
        void * t12 = gm_l4;
        gm_t88rcucg1b7uk t13 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t12) + 0));
        gm_b16_8 t14 = gm_l5;
        gm_b16_8 t15 = gm_f_addr_of_hiaqkn0((void *)NULL, cx, NULL, t11, t13, t14);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l6 = t15;
    }
    {
        gm_th2lqbo77pvsc t16;
        memset(&t16, 0, sizeof t16);
        gm_l7 = t16;
    }
    {
        void * t18 = gm_l0;
        void * t19 = GM_LD(void *, (((uint8_t *)t18) + 0));
        void * t20 = (void *)(((uint8_t *)t19) + 8);
        gm_t88rcucg1b7uk t21 = gm_l3;
        uint8_t t22 = gm_f_is_never_in5ftma(t20, GM_CONV(gm_csguankko4lmhd, t21));
        uint8_t t23 = (uint8_t)!t22;
        if (!t23) goto ie17;
        {
            void * t25 = gm_l0;
            gm_t88rcucg1b7uk t26 = gm_l3;
            gm_b16_8 t27 = gm_f_tmp_uninit_jr39s5u((void *)NULL, cx, NULL, t25, t26);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l7)) + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l7)) + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l7)) + 20, 0);
            ((uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l7)))[16] = 1;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l7)) + 0, t27);
        }
        goto ix17;
ie17:;
ix17:;
    }
    {
        gm_b16_8 t28;
        GM_ST(void *, (uint8_t *)&t28, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(0));
        gm_l8 = t28;
    }
    {
        gm_t88rcucg1b7uk t29 = gm_l3;
        gm_l9 = t29;
    }
    {
        gm_l10 = ((uint64_t)0ULL);
    }
    {
        void * t30 = gm_l0;
        void * t31 = GM_LD(void *, (((uint8_t *)t30) + 0));
        void * t32 = (void *)(((uint8_t *)t31) + 8);
        void * t33 = gm_l4;
        gm_t88rcucg1b7uk t34 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t33) + 0));
        gm_t53at80pfdus3 t35;
        gm_f_kind_ghvav22((void *)&t35, t32, GM_CONV(gm_csguankko4lmhd, t34));
        uint8_t t41 = GM_LD(uint8_t, (((uint8_t *)&t35) + 0));
        switch (t41) { case ((uint8_t)0ULL): goto ma39; case ((uint8_t)1ULL): goto ma39; case ((uint8_t)2ULL): goto ma39; case ((uint8_t)3ULL): goto ma39; case ((uint8_t)4ULL): goto ma39; case ((uint8_t)5ULL): goto ma39; case ((uint8_t)6ULL): goto ma39; case ((uint8_t)7ULL): goto ma39; case ((uint8_t)8ULL): goto ma39; case ((uint8_t)9ULL): goto ma39; case ((uint8_t)10ULL): goto ma39; case ((uint8_t)11ULL): goto ma39; case ((uint8_t)12ULL): goto ma39; case ((uint8_t)13ULL): goto ma37; case ((uint8_t)14ULL): goto ma38; case ((uint8_t)15ULL): goto ma39; case ((uint8_t)16ULL): goto ma39; case ((uint8_t)17ULL): goto ma39; case ((uint8_t)18ULL): goto ma39; case ((uint8_t)19ULL): goto ma39; case ((uint8_t)20ULL): goto ma39; case ((uint8_t)21ULL): goto ma39; case ((uint8_t)22ULL): goto ma39; case ((uint8_t)23ULL): goto ma39; case ((uint8_t)24ULL): goto ma39; case ((uint8_t)25ULL): goto ma39; case ((uint8_t)26ULL): goto ma39; default: goto ma39; }
ma37:;
        uint8_t t43 = GM_LD(uint8_t, (((uint8_t *)&t35) + 0));
        if (t43 != ((uint8_t)13ULL)) goto mn42;
        gm_t88rcucg1b7uk t44 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t35) + 8));
        gm_l11 = t44;
        {
            void * t46 = gm_l0;
            void * t47 = gm_l4;
            gm_t88rcucg1b7uk t48 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t47) + 0));
            gm_b16_8 t49 = gm_l6;
            gm_b16_8 t50 = gm_f_opt_present_joptd3k((void *)NULL, cx, NULL, t46, t48, t49);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l8 = t50;
        }
        {
            gm_t88rcucg1b7uk t51 = gm_l11;
            gm_l9 = t51;
        }
        goto me36;
mn42:;
ma38:;
        uint8_t t53 = GM_LD(uint8_t, (((uint8_t *)&t35) + 0));
        if (t53 != ((uint8_t)14ULL)) goto mn52;
        gm_t88rcucg1b7uk t54 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t35) + 8) + 4));
        gm_l12 = t54;
        {
            void * t56 = gm_l0;
            void * t57 = gm_l4;
            gm_t88rcucg1b7uk t58 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t57) + 0));
            gm_b16_8 t59 = gm_l6;
            gm_b16_8 t60 = gm_f_res_is_err_11p8qlp((void *)NULL, cx, NULL, t56, t58, t59);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l13 = t60;
        }
        {
            gm_b16_8 t61 = gm_l13;
            gm_tnpifv20irnph t62;
            GM_ST(gm_b16_8, ((uint8_t *)&t62 + 0), t61);
            gm_tabb710d9mq2e t63;
            gm_f_format_08505jg((void *)&t63, (void *)NULL, cx, NULL, t62);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t63))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t63) + 8, gm_src0gtuh94b1fce, 649, 23);
            gm_b16_8 t64 = GM_LD(gm_b16_8, (((uint8_t *)&t63) + 8));
            gm_b16_8 t65 = GM_LD(gm_b16_8, ((uint8_t *)&t64));
            gm_l8 = t65;
        }
        {
            gm_t88rcucg1b7uk t66 = gm_l12;
            gm_l9 = t66;
        }
        {
            uint64_t t67 = (uintptr_t)((uint64_t)1ULL);
            void * t68 = gm_l0;
            void * t69 = GM_LD(void *, (((uint8_t *)t68) + 0));
            void * t70 = (void *)(((uint8_t *)t69) + 8);
            void * t71 = gm_l4;
            gm_t88rcucg1b7uk t72 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t71) + 0));
            gm_b16_8 t73 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t70, t72);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint64_t t74 = GM_LD(uintptr_t, ((uint8_t *)&t73) + 8);
            if (t67 >= (uintptr_t)(t74)) gm_trap_index(t67, t74, gm_src0gtuh94b1fce, 651, 27);
            uint64_t t75 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t73)) + t67 * 8));
            gm_l10 = t75;
        }
        goto me36;
mn52:;
ma39:;
        void * t77 = gm_l1;
        gm_tl874t2atnlh3 t78 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t77) + 72));
        gm_b16_8 t79;
        GM_ST(void *, (uint8_t *)&t79, (void *)(gm_s18dhen1d87oi));
        GM_ST(uintptr_t, (uint8_t *)&t79 + 8, (uintptr_t)(19));
        gm_t08olq97rscn3 t80;
        gm_f_unsupported_531q51r((void *)&t80, (void *)NULL, cx, NULL, t78, t79);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 1;
        GM_ST(gm_t08olq97rscn3, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t80);
        goto gm_fdone;
        goto me36;
mn76:;
ma40:;
        gm_trap("no match arm matched the scrutinee", gm_src0gtuh94b1fce, 642, 5);
me36:;
    }
    {
        void * t81 = gm_l0;
        gm_b16_8 t82 = gm_l8;
        gm_tnpifv20irnph t83;
        GM_ST(gm_b16_8, ((uint8_t *)&t83 + 0), t82);
        gm_b0_1 t84 = gm_f_linef_g6msml8((void *)NULL, cx, NULL, t81, t83);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t85 = gm_l0;
        void * t86 = gm_l0;
        uint64_t t87 = GM_LD(uint64_t, (((uint8_t *)t86) + 224));
        uint64_t r88; bool o89 = __builtin_add_overflow(t87, ((uint64_t)1ULL), &r88);
        if (o89) gm_trap("integer overflow in `+`", gm_src0gtuh94b1fce, 656, 5);
        uint64_t t90 = r88;
        GM_ST(uint64_t, (((uint8_t *)t85) + 224), t90);
    }
    {
        if (!((((uint8_t *)&gm_l7))[16] != 0)) goto ie91;
        gm_b16_8 t92 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l7));
        gm_l14 = t92;
        {
            gm_b16_8 t94;
            GM_ST(void *, (uint8_t *)&t94, (void *)(gm_sr4qbrk6tlgaa));
            GM_ST(uintptr_t, (uint8_t *)&t94 + 8, (uintptr_t)(0));
            gm_l15 = t94;
        }
        {
            gm_t88rcucg1b7uk t96 = gm_l3;
            gm_t88rcucg1b7uk t97 = gm_l9;
            uint8_t t98 = (gm_eq9m6mvtmgde(((uint8_t *)&t96), ((uint8_t *)&t97)));
            if (!t98) goto ie95;
            {
                void * t100 = gm_l0;
                gm_t88rcucg1b7uk t101 = gm_l9;
                gm_t3877pk7lv748 t102;
                gm_b16_8 t103 = gm_l6;
                GM_ST(gm_b16_8, (uint8_t *)&t102 + 0, t103);
                uint64_t t104 = gm_l10;
                GM_ST(uint64_t, (uint8_t *)&t102 + 16, t104);
                gm_tabb710d9mq2e t105;
                gm_f_format_gjujk09((void *)&t105, (void *)NULL, cx, NULL, &t102);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t105))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t105) + 8, gm_src0gtuh94b1fce, 660, 37);
                gm_b16_8 t106 = GM_LD(gm_b16_8, (((uint8_t *)&t105) + 8));
                gm_b16_8 t107 = GM_LD(gm_b16_8, ((uint8_t *)&t106));
                gm_b16_8 t108 = gm_f_load_0hebde7((void *)NULL, cx, NULL, t100, t101, t107);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l15 = t108;
            }
            goto ix95;
ie95:;
            gm_t88rcucg1b7uk t110 = gm_l3;
            void * t111 = gm_l4;
            gm_t88rcucg1b7uk t112 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t111) + 0));
            uint8_t t113 = (gm_eq9m6mvtmgde(((uint8_t *)&t110), ((uint8_t *)&t112)));
            if (!t113) goto ie109;
            {
                gm_b16_8 t115 = gm_l5;
                gm_l15 = t115;
            }
            goto ix109;
ie109:;
            void * t117 = gm_l0;
            void * t118 = GM_LD(void *, (((uint8_t *)t117) + 0));
            void * t119 = (void *)(((uint8_t *)t118) + 8);
            gm_t88rcucg1b7uk t120 = gm_l3;
            gm_t53at80pfdus3 t121;
            gm_f_kind_ghvav22((void *)&t121, t119, GM_CONV(gm_csguankko4lmhd, t120));
            uint8_t t122;
            uint8_t t125 = GM_LD(uint8_t, (((uint8_t *)&t121) + 0));
            if (t125 != ((uint8_t)13ULL)) goto mn124;
            t122 = ((uint8_t)1);
            goto me123;
mn124:;
            t122 = ((uint8_t)0);
            goto me123;
mn126:;
            gm_trap("no match arm matched the scrutinee", gm_src0gtuh94b1fce, 663, 19);
me123:;
            if (!t122) goto ie116;
            {
                void * t128 = gm_l0;
                gm_t88rcucg1b7uk t129 = gm_l9;
                gm_t3877pk7lv748 t130;
                gm_b16_8 t131 = gm_l6;
                GM_ST(gm_b16_8, (uint8_t *)&t130 + 0, t131);
                uint64_t t132 = gm_l10;
                GM_ST(uint64_t, (uint8_t *)&t130 + 16, t132);
                gm_tabb710d9mq2e t133;
                gm_f_format_gjujk09((void *)&t133, (void *)NULL, cx, NULL, &t130);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t133))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t133) + 8, gm_src0gtuh94b1fce, 664, 38);
                gm_b16_8 t134 = GM_LD(gm_b16_8, (((uint8_t *)&t133) + 8));
                gm_b16_8 t135 = GM_LD(gm_b16_8, ((uint8_t *)&t134));
                gm_b16_8 t136 = gm_f_load_0hebde7((void *)NULL, cx, NULL, t128, t129, t135);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l16 = t136;
            }
            {
                void * t137 = gm_l0;
                gm_t88rcucg1b7uk t138 = gm_l3;
                gm_b16_8 t139 = gm_l16;
                gm_b16_8 t140 = gm_f_make_some_9aqq928((void *)NULL, cx, NULL, t137, t138, t139);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l15 = t140;
            }
            goto ix116;
ie116:;
            {
                void * t142 = gm_l0;
                gm_t88rcucg1b7uk t143 = gm_l9;
                gm_t3877pk7lv748 t144;
                gm_b16_8 t145 = gm_l6;
                GM_ST(gm_b16_8, (uint8_t *)&t144 + 0, t145);
                uint64_t t146 = gm_l10;
                GM_ST(uint64_t, (uint8_t *)&t144 + 16, t146);
                gm_tabb710d9mq2e t147;
                gm_f_format_gjujk09((void *)&t147, (void *)NULL, cx, NULL, &t144);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t147))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t147) + 8, gm_src0gtuh94b1fce, 667, 38);
                gm_b16_8 t148 = GM_LD(gm_b16_8, (((uint8_t *)&t147) + 8));
                gm_b16_8 t149 = GM_LD(gm_b16_8, ((uint8_t *)&t148));
                gm_b16_8 t150 = gm_f_load_0hebde7((void *)NULL, cx, NULL, t142, t143, t149);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l17 = t150;
            }
            {
                void * t151 = gm_l0;
                gm_t88rcucg1b7uk t152 = gm_l3;
                gm_t88rcucg1b7uk t153 = gm_l9;
                gm_b16_8 t154 = gm_l17;
                gm_b16_8 t155 = gm_f_make_res_riv19po((void *)NULL, cx, NULL, t151, t152, ((uint8_t)0), t153, t154);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l15 = t155;
            }
ix116:;
ix109:;
ix95:;
        }
        {
            void * t156 = gm_l0;
            gm_t6j6smj7qoctu t157;
            gm_b16_8 t158 = gm_l14;
            GM_ST(gm_b16_8, (uint8_t *)&t157 + 0, t158);
            gm_b16_8 t159 = gm_l15;
            GM_ST(gm_b16_8, (uint8_t *)&t157 + 16, t159);
            gm_b0_1 t160 = gm_f_linef_uu8i6ue((void *)NULL, cx, NULL, t156, &t157);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix91;
ie91:;
ix91:;
    }
    {
        void * t161 = gm_l0;
        void * t162 = gm_l0;
        uint64_t t163 = GM_LD(uint64_t, (((uint8_t *)t162) + 224));
        uint64_t r164; bool o165 = __builtin_sub_overflow(t163, ((uint64_t)1ULL), &r164);
        if (o165) gm_trap("integer overflow in `-`", gm_src0gtuh94b1fce, 672, 5);
        uint64_t t166 = r164;
        GM_ST(uint64_t, (((uint8_t *)t161) + 224), t166);
    }
    {
        void * t167 = gm_l0;
        gm_b16_8 t168;
        GM_ST(void *, (uint8_t *)&t168, (void *)(gm_s02uuedsru1qu));
        GM_ST(uintptr_t, (uint8_t *)&t168 + 8, (uintptr_t)(8));
        gm_b0_1 t169 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t167, t168);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t170 = gm_l0;
        void * t171 = gm_l0;
        uint64_t t172 = GM_LD(uint64_t, (((uint8_t *)t171) + 224));
        uint64_t r173; bool o174 = __builtin_add_overflow(t172, ((uint64_t)1ULL), &r173);
        if (o174) gm_trap("integer overflow in `+`", gm_src0gtuh94b1fce, 674, 5);
        uint64_t t175 = r173;
        GM_ST(uint64_t, (((uint8_t *)t170) + 224), t175);
    }
    {
        void * t177 = gm_l2;
        if (!(((((uint8_t *)t177) + 8))[4] != 0)) goto ie176;
        uint32_t t178 = GM_LD(uint32_t, (((uint8_t *)t177) + 8));
        gm_l18 = t178;
        {
            void * t181 = gm_l0;
            void * t182 = GM_LD(void *, (((uint8_t *)t181) + 0));
            void * t183 = (void *)(((uint8_t *)t182) + 8);
            void * t184 = gm_l4;
            gm_t88rcucg1b7uk t185 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t184) + 0));
            gm_t53at80pfdus3 t186;
            gm_f_kind_ghvav22((void *)&t186, t183, GM_CONV(gm_csguankko4lmhd, t185));
            uint8_t t187 = GM_LD(uint8_t, (((uint8_t *)&t186) + 0));
            if (t187 != ((uint8_t)14ULL)) goto ie180;
            gm_t88rcucg1b7uk t188 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t186) + 8) + 0));
            gm_l19 = t188;
            {
                uint64_t t190 = (uintptr_t)((uint64_t)1ULL);
                void * t191 = gm_l0;
                void * t192 = GM_LD(void *, (((uint8_t *)t191) + 0));
                void * t193 = (void *)(((uint8_t *)t192) + 8);
                void * t194 = gm_l4;
                gm_t88rcucg1b7uk t195 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t194) + 0));
                gm_b16_8 t196 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t193, t195);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint64_t t197 = GM_LD(uintptr_t, ((uint8_t *)&t196) + 8);
                if (t190 >= (uintptr_t)(t197)) gm_trap_index(t190, t197, gm_src0gtuh94b1fce, 677, 20);
                uint64_t t198 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t196)) + t190 * 8));
                gm_l20 = t198;
            }
            {
                void * t199 = gm_l0;
                gm_t88rcucg1b7uk t200 = gm_l19;
                gm_t3877pk7lv748 t201;
                gm_b16_8 t202 = gm_l6;
                GM_ST(gm_b16_8, (uint8_t *)&t201 + 0, t202);
                uint64_t t203 = gm_l20;
                GM_ST(uint64_t, (uint8_t *)&t201 + 16, t203);
                gm_tabb710d9mq2e t204;
                gm_f_format_gjujk09((void *)&t204, (void *)NULL, cx, NULL, &t201);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t204))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t204) + 8, gm_src0gtuh94b1fce, 678, 32);
                gm_b16_8 t205 = GM_LD(gm_b16_8, (((uint8_t *)&t204) + 8));
                gm_b16_8 t206 = GM_LD(gm_b16_8, ((uint8_t *)&t205));
                gm_b16_8 t207 = gm_f_load_0hebde7((void *)NULL, cx, NULL, t199, t200, t206);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l21 = t207;
            }
            {
                void * t208 = gm_l0;
                uint32_t t209 = gm_l18;
                gm_b16_8 t210 = gm_f_local_lv_m2jtf0n((void *)NULL, cx, NULL, t208, t209);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l22 = t210;
            }
            {
                void * t211 = gm_l0;
                gm_t6j6smj7qoctu t212;
                gm_b16_8 t213 = gm_l22;
                GM_ST(gm_b16_8, (uint8_t *)&t212 + 0, t213);
                gm_b16_8 t214 = gm_l21;
                GM_ST(gm_b16_8, (uint8_t *)&t212 + 16, t214);
                gm_b0_1 t215 = gm_f_linef_uu8i6ue((void *)NULL, cx, NULL, t211, &t212);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix180;
ie180:;
ix180:;
        }
        goto ix176;
ie176:;
ix176:;
    }
    {
        void * t216 = gm_l0;
        void * t217 = gm_l2;
        void * t218 = GM_LD(void *, (((uint8_t *)t217) + 16));
        gm_t5o0tpv5jka2l t219;
        gm_f_expr_porbc9t((void *)&t219, (void *)NULL, cx, NULL, t216, t218);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t219))[0] != 0)) {
            gm_t5o0tpv5jka2l t220;
            GM_ST(uint8_t, (uint8_t *)&t220 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t220 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t220 + 4, 0);
            ((uint8_t *)&t220)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t220 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t219) + 8)));
            F.ret = t220;
            goto gm_fret;
        }
        gm_b16_8 t221 = GM_LD(gm_b16_8, (((uint8_t *)&t219) + 8));
        gm_l23 = t221;
    }
    {
        if (!((((uint8_t *)&gm_l7))[16] != 0)) goto ie222;
        gm_b16_8 t223 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l7));
        gm_l24 = t223;
        {
            void * t226 = gm_l0;
            void * t227 = GM_LD(void *, (((uint8_t *)t226) + 0));
            void * t228 = (void *)(((uint8_t *)t227) + 8);
            void * t229 = gm_l2;
            void * t230 = GM_LD(void *, (((uint8_t *)t229) + 16));
            gm_t88rcucg1b7uk t231 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t230) + 0));
            uint8_t t232 = gm_f_is_never_in5ftma(t228, GM_CONV(gm_csguankko4lmhd, t231));
            uint8_t t233 = (uint8_t)!t232;
            if (!t233) goto ie225;
            {
                void * t235 = gm_l0;
                gm_t6j6smj7qoctu t236;
                gm_b16_8 t237 = gm_l24;
                GM_ST(gm_b16_8, (uint8_t *)&t236 + 0, t237);
                gm_b16_8 t238 = gm_l23;
                GM_ST(gm_b16_8, (uint8_t *)&t236 + 16, t238);
                gm_b0_1 t239 = gm_f_linef_uu8i6ue((void *)NULL, cx, NULL, t235, &t236);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix225;
ie225:;
ix225:;
        }
        goto ix222;
ie222:;
ix222:;
    }
    {
        void * t240 = gm_l0;
        void * t241 = gm_l0;
        uint64_t t242 = GM_LD(uint64_t, (((uint8_t *)t241) + 224));
        uint64_t r243; bool o244 = __builtin_sub_overflow(t242, ((uint64_t)1ULL), &r243);
        if (o244) gm_trap("integer overflow in `-`", gm_src0gtuh94b1fce, 687, 5);
        uint64_t t245 = r243;
        GM_ST(uint64_t, (((uint8_t *)t240) + 224), t245);
    }
    {
        void * t246 = gm_l0;
        gm_b16_8 t247;
        GM_ST(void *, (uint8_t *)&t247, (void *)(gm_s2cuflhqkkuaq));
        GM_ST(uintptr_t, (uint8_t *)&t247 + 8, (uintptr_t)(1));
        gm_b0_1 t248 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t246, t247);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        if (!((((uint8_t *)&gm_l7))[16] != 0)) goto ie249;
        gm_b16_8 t250 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l7));
        gm_l25 = t250;
        {
            gm_b16_8 t252 = gm_l25;
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
            ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t252);
            goto gm_fdone;
        }
        goto ix249;
ie249:;
ix249:;
    }
    {
        void * t253 = gm_l0;
        gm_t88rcucg1b7uk t254 = gm_l3;
        gm_b16_8 t255 = gm_f_zero_tlk13aa((void *)NULL, cx, NULL, t253, t254);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t255);
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
inline __attribute__((aligned(4))) void gm_f_emit_buf_mqofii1(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, const void * a2, gm_b16_8 a3, void * a4) {
    struct gm_fr_emit_buf_mqofii1 F;
    void * gm_l0; /* fg */
    gm_tdlpv0q69lov1 gm_l2; /* sv */
    gm_b16_8 gm_l3; /* b */
    void * gm_l4; /* e */
    gm_b16_8 gm_l5; /* sl */
    GM_STACK_CHECK(&F, 588u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    memcpy(&gm_l2, a2, sizeof gm_l2);
    gm_l3 = a3;
    gm_l4 = a4;
    {
        {
            void * t3 = gm_l0;
            gm_b16_8 t4 = gm_l3;
            gm_tnpifv20irnph t5;
            GM_ST(gm_b16_8, ((uint8_t *)&t5 + 0), t4);
            gm_b0_1 t6 = gm_f_linef_qch21ro((void *)NULL, cx, NULL, t3, t5);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t7 = gm_l0;
            void * t8 = gm_l0;
            uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)t8) + 224));
            uint64_t r10; bool o11 = __builtin_add_overflow(t9, ((uint64_t)1ULL), &r10);
            if (o11) gm_trap("integer overflow in `+`", gm_src0gtuh94b1fce, 912, 5);
            uint64_t t12 = r10;
            GM_ST(uint64_t, (((uint8_t *)t7) + 224), t12);
        }
        {
            void * t13 = gm_l0;
            void * t14 = gm_l0;
            void * t15 = GM_LD(void *, (((uint8_t *)t14) + 0));
            void * t16 = (void *)(((uint8_t *)t15) + 8);
            gm_t88rcucg1b7uk t17 = gm_f_string_ty_kk94re3((void *)NULL, cx, NULL, t16);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t18 = gm_l3;
            gm_tnpifv20irnph t19;
            GM_ST(gm_b16_8, ((uint8_t *)&t19 + 0), t18);
            gm_tabb710d9mq2e t20;
            gm_f_format_he684oe((void *)&t20, (void *)NULL, cx, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t20))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t20) + 8, gm_src0gtuh94b1fce, 913, 46);
            gm_b16_8 t21 = GM_LD(gm_b16_8, (((uint8_t *)&t20) + 8));
            gm_b16_8 t22 = GM_LD(gm_b16_8, ((uint8_t *)&t21));
            gm_b16_8 t23 = gm_l3;
            gm_tnpifv20irnph t24;
            GM_ST(gm_b16_8, ((uint8_t *)&t24 + 0), t23);
            gm_tabb710d9mq2e t25;
            gm_f_format_0lf0h5l((void *)&t25, (void *)NULL, cx, NULL, t24);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t25))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t25) + 8, gm_src0gtuh94b1fce, 913, 73);
            gm_b16_8 t26 = GM_LD(gm_b16_8, (((uint8_t *)&t25) + 8));
            gm_b16_8 t27 = GM_LD(gm_b16_8, ((uint8_t *)&t26));
            gm_b16_8 t28 = gm_f_make_slice_sa50efe((void *)NULL, cx, NULL, t13, t17, t22, t27);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l5 = t28;
        }
        {
            void * t29 = gm_l0;
            gm_b16_8 t30 = gm_l5;
            void * t31 = gm_l4;
            gm_tqgtd5gtgj65s t32;
            gm_f_emit_to_o7js99p((void *)&t32, (void *)NULL, cx, NULL, t29, &(*(gm_th1g69d7vpipg *)((uint8_t *)&F.l1)), &(*(gm_tdlpv0q69lov1 *)((uint8_t *)&gm_l2)), t30, t31);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t32))[0] != 0)) {
                gm_tqgtd5gtgj65s t33;
                GM_ST(uint8_t, (uint8_t *)&t33 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t33 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t33 + 4, 0);
                ((uint8_t *)&t33)[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t33 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t32) + 8)));
                F.ret = t33;
                goto gm_fret;
            }
            gm_b0_1 t34 = GM_LD(gm_b0_1, (((uint8_t *)&t32) + 8));
        }
        {
            void * t35 = gm_l0;
            void * t36 = gm_l0;
            uint64_t t37 = GM_LD(uint64_t, (((uint8_t *)t36) + 224));
            uint64_t r38; bool o39 = __builtin_sub_overflow(t37, ((uint64_t)1ULL), &r38);
            if (o39) gm_trap("integer overflow in `-`", gm_src0gtuh94b1fce, 915, 5);
            uint64_t t40 = r38;
            GM_ST(uint64_t, (((uint8_t *)t35) + 224), t40);
        }
        {
            void * t41 = gm_l0;
            gm_b16_8 t42;
            GM_ST(void *, (uint8_t *)&t42, (void *)(gm_s2cuflhqkkuaq));
            GM_ST(uintptr_t, (uint8_t *)&t42 + 8, (uintptr_t)(1));
            gm_b0_1 t43 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t41, t42);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t44 = gm_l0;
            gm_b16_8 t45 = gm_l3;
            gm_tnpifv20irnph t46;
            GM_ST(gm_b16_8, ((uint8_t *)&t46 + 0), t45);
            gm_b0_1 t47 = gm_f_linef_sgpievq((void *)NULL, cx, NULL, t44, t46);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    gm_b0_1 t48;
    memset(&(*(gm_tqgtd5gtgj65s *)gm_out), 0, sizeof (*(gm_tqgtd5gtgj65s *)gm_out));
    ((uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out))[0] = 0;
    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 8, t48);
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
inline __attribute__((aligned(4))) void gm_f_emit_to_o7js99p(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, const void * a2, gm_b16_8 a3, void * a4) {
    struct gm_fr_emit_to_o7js99p F;
    void * gm_l0; /* fg */
    gm_tdlpv0q69lov1 gm_l2; /* sv */
    gm_b16_8 gm_l3; /* slice */
    void * gm_l4; /* e */
    gm_t88rcucg1b7uk gm_l5; /* unit_ty */
    gm_tpmdr2hmdhsqr gm_l6; /* vals */
    gm_tnqk2qt7j42o9 gm_l7; /* tys */
    gm_tep2drsl9408b gm_l8; /* cv */
    gm_tiilmfd16v3fc gm_l9; /* effect_ */
    uint32_t gm_l10; /* op */
    GM_STACK_CHECK(&F, 710u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    memcpy(&gm_l2, a2, sizeof gm_l2);
    gm_l3 = a3;
    gm_l4 = a4;
    {
        {
            void * t3 = gm_l0;
            void * t4 = GM_LD(void *, (((uint8_t *)t3) + 0));
            void * t5 = (void *)(((uint8_t *)t4) + 8);
            gm_t88rcucg1b7uk t6 = gm_f_unit_ty_vgceilu((void *)NULL, cx, NULL, t5);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l5 = t6;
        }
        {
            gm_b16_8 t7 = gm_l3;
            gm_tpmdr2hmdhsqr t8;
            GM_ST(gm_b16_8, ((uint8_t *)&t8 + 0), t7);
            gm_l6 = t8;
        }
        {
            void * t9 = gm_l0;
            void * t10 = GM_LD(void *, (((uint8_t *)t9) + 0));
            void * t11 = (void *)(((uint8_t *)t10) + 8);
            gm_t88rcucg1b7uk t12 = gm_f_string_ty_kk94re3((void *)NULL, cx, NULL, t11);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tnqk2qt7j42o9 t13;
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t13 + 0), t12);
            gm_l7 = t13;
        }
        {
            uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
            if (t16 != ((uint8_t)1ULL)) goto mn15;
            {
                gm_tdlpv0q69lov1 t18 = gm_l2;
                if (!((((uint8_t *)&t18))[24] != 0)) gm_trap("unwrap of null", gm_src0gtuh94b1fce, 927, 19);
                gm_l8 = (*(gm_tep2drsl9408b *)(((uint8_t *)&t18) + 0));
            }
            {
                {
                    void * t20 = gm_l0;
                    gm_b16_8 t21 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l8) + 0));
                    gm_t88rcucg1b7uk t22 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l8) + 16));
                    void * t23 = ((uint8_t *)&gm_l6);
                    if ((uintptr_t)0 > (uintptr_t)1 || (uintptr_t)1 > (uintptr_t)1) gm_trap_slice((uintptr_t)0, (uintptr_t)1, 1, gm_src0gtuh94b1fce, 928, 50);
                    gm_b16_8 t24;
                    GM_ST(void *, (uint8_t *)&t24, (void *)(((uint8_t *)t23 + (uintptr_t)0 * 16)));
                    GM_ST(uintptr_t, (uint8_t *)&t24 + 8, (uintptr_t)((uintptr_t)1 - (uintptr_t)0));
                    gm_b16_8 t25 = GM_LD(gm_b16_8, ((uint8_t *)&t24));
                    void * t26 = ((uint8_t *)&gm_l7);
                    if ((uintptr_t)0 > (uintptr_t)1 || (uintptr_t)1 > (uintptr_t)1) gm_trap_slice((uintptr_t)0, (uintptr_t)1, 1, gm_src0gtuh94b1fce, 928, 60);
                    gm_b16_8 t27;
                    GM_ST(void *, (uint8_t *)&t27, (void *)(((uint8_t *)t26 + (uintptr_t)0 * 4)));
                    GM_ST(uintptr_t, (uint8_t *)&t27 + 8, (uintptr_t)((uintptr_t)1 - (uintptr_t)0));
                    gm_b16_8 t28 = GM_LD(gm_b16_8, ((uint8_t *)&t27));
                    gm_t88rcucg1b7uk t29 = gm_l5;
                    gm_t5o0tpv5jka2l t30;
                    gm_f_call_closure_mha9p1e((void *)&t30, (void *)NULL, cx, NULL, t20, t21, t22, t25, t28, t29);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t30))[0] != 0)) {
                        gm_tqgtd5gtgj65s t31;
                        GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
                        ((uint8_t *)&t31)[0] = 1;
                        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t31 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t30) + 8)));
                        F.ret = t31;
                        goto gm_fret;
                    }
                    gm_b16_8 t32 = GM_LD(gm_b16_8, (((uint8_t *)&t30) + 8));
                }
            }
            goto me14;
mn15:;
            uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
            if (t34 != ((uint8_t)0ULL)) goto mn33;
            gm_tiilmfd16v3fc t35 = GM_LD(gm_tiilmfd16v3fc, ((((uint8_t *)&F.l1) + 8) + 0));
            gm_l9 = t35;
            uint32_t t36 = GM_LD(uint32_t, ((((uint8_t *)&F.l1) + 8) + 24));
            gm_l10 = t36;
            {
                {
                    void * t39 = gm_l0;
                    void * t40 = gm_l4;
                    void * t41 = (void *)((uint8_t *)&gm_l9);
                    uint32_t t42 = gm_l10;
                    void * t43 = ((uint8_t *)&gm_l6);
                    if ((uintptr_t)0 > (uintptr_t)1 || (uintptr_t)1 > (uintptr_t)1) gm_trap_slice((uintptr_t)0, (uintptr_t)1, 1, gm_src0gtuh94b1fce, 931, 49);
                    gm_b16_8 t44;
                    GM_ST(void *, (uint8_t *)&t44, (void *)(((uint8_t *)t43 + (uintptr_t)0 * 16)));
                    GM_ST(uintptr_t, (uint8_t *)&t44 + 8, (uintptr_t)((uintptr_t)1 - (uintptr_t)0));
                    gm_b16_8 t45 = GM_LD(gm_b16_8, ((uint8_t *)&t44));
                    void * t46 = ((uint8_t *)&gm_l7);
                    if ((uintptr_t)0 > (uintptr_t)1 || (uintptr_t)1 > (uintptr_t)1) gm_trap_slice((uintptr_t)0, (uintptr_t)1, 1, gm_src0gtuh94b1fce, 931, 59);
                    gm_b16_8 t47;
                    GM_ST(void *, (uint8_t *)&t47, (void *)(((uint8_t *)t46 + (uintptr_t)0 * 4)));
                    GM_ST(uintptr_t, (uint8_t *)&t47 + 8, (uintptr_t)((uintptr_t)1 - (uintptr_t)0));
                    gm_b16_8 t48 = GM_LD(gm_b16_8, ((uint8_t *)&t47));
                    gm_t88rcucg1b7uk t49 = gm_l5;
                    gm_t5o0tpv5jka2l t50;
                    gm_f_perform_t77ghfg((void *)&t50, (void *)NULL, cx, NULL, t39, t40, t41, t42, t45, t48, t49);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t50))[0] != 0)) {
                        gm_tqgtd5gtgj65s t51;
                        GM_ST(uint8_t, (uint8_t *)&t51 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t51 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t51 + 4, 0);
                        ((uint8_t *)&t51)[0] = 1;
                        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t51 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t50) + 8)));
                        F.ret = t51;
                        goto gm_fret;
                    }
                    gm_b16_8 t52 = GM_LD(gm_b16_8, (((uint8_t *)&t50) + 8));
                }
            }
            goto me14;
mn33:;
            gm_trap("no match arm matched the scrutinee", gm_src0gtuh94b1fce, 925, 5);
me14:;
        }
    }
    gm_b0_1 t53;
    memset(&(*(gm_tqgtd5gtgj65s *)gm_out), 0, sizeof (*(gm_tqgtd5gtgj65s *)gm_out));
    ((uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out))[0] = 0;
    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tqgtd5gtgj65s *)gm_out) + 8, t53);
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
__attribute__((aligned(4))) void gm_f_expr_node_sj12d0p(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_expr_node_sj12d0p F;
    void * gm_l0; /* fg */
    void * gm_l1; /* e */
    gm_t88rcucg1b7uk gm_l2; /* t */
    double gm_l4; /* x */
    uint16_t gm_l5; /* bits */
    uint8_t gm_l6; /* b */
    gm_b16_8 gm_l7; /* s */
    gm_b16_8 gm_l8; /* a */
    gm_b16_8 gm_l9; /* s */
    gm_b16_8 gm_l10; /* n */
    gm_tl874t2atnlh3 gm_l11; /* span */
    gm_b16_8 gm_l12; /* name */
    void * gm_l13; /* base */
    uint32_t gm_l14; /* index */
    uint32_t gm_l15; /* l */
    gm_b16_8 gm_l16; /* lv */
    gm_b16_8 gm_l17; /* a */
    gm_b16_8 gm_l18; /* a */
    gm_tao63pk3r18v5 gm_l19; /* sl */
    void * gm_l20; /* x */
    gm_b16_8 gm_l21; /* a */
    gm_b16_8 gm_l22; /* l */
    void * gm_l23; /* x */
    gm_b16_8 gm_l24; /* a */
    gm_b16_8 gm_l25; /* p */
    void * gm_l26; /* p */
    gm_b16_8 gm_l27; /* a */
    void * gm_l28; /* x */
    gm_b16_8 gm_l29; /* v */
    gm_b16_8 gm_l30; /* a */
    uint32_t gm_l31; /* f */
    gm_b16_8 gm_l32; /* name */
    uint8_t gm_l33; /* plain */
    gm_b16_8 gm_l34; /* code */
    uint32_t gm_l35; /* f */
    gm_b16_8 gm_l36; /* name */
    gm_t9qkh0rsooc8d gm_l37; /* op */
    void * gm_l38; /* operand */
    gm_ts6oqppjh0ba3 gm_l39; /* op */
    void * gm_l40; /* lhs */
    void * gm_l41; /* rhs */
    gm_t9qkh0rsooc8d gm_l42; /* op */
    void * gm_l43; /* operand */
    gm_ts6oqppjh0ba3 gm_l44; /* op */
    void * gm_l45; /* lhs */
    void * gm_l46; /* rhs */
    uint8_t gm_l47; /* is_and */
    void * gm_l48; /* lhs */
    void * gm_l49; /* rhs */
    gm_tmn7n0n9raou5 gm_l50; /* kind */
    void * gm_l51; /* x */
    gm_tvdg94bk7aa22 gm_l52; /* op */
    gm_b16_8 gm_l53; /* args */
    gm_b16_8 gm_l55; /* args */
    gm_b16_8 gm_l56; /* elems */
    gm_b16_8 gm_l57; /* b */
    gm_b16_8 gm_l58; /* elems */
    gm_b16_8 gm_l59; /* elems */
    void * gm_l60; /* x */
    gm_ttggq7ng4kp3p gm_l61; /* vx */
    gm_th2lqbo77pvsc gm_l62; /* p */
    void * gm_l63; /* px */
    void * gm_l64; /* x */
    gm_b16_8 gm_l65; /* v */
    void * gm_l66; /* x */
    uint8_t gm_l67; /* is_result */
    void * gm_l68; /* lhs */
    void * gm_l69; /* rhs */
    uint8_t gm_l70; /* negated */
    gm_b16_8 gm_l71; /* c */
    gm_b16_8 gm_l72; /* l */
    gm_b16_8 gm_l73; /* r */
    gm_b16_8 gm_l74; /* c */
    void * gm_l75; /* elem */
    void * gm_l76; /* container */
    uint8_t gm_l77; /* negated */
    void * gm_l78; /* b */
    gm_trl2khmuphncd gm_l79; /* x */
    void * gm_l80; /* scrutinee */
    gm_b16_8 gm_l81; /* arms */
    gm_t3dcoc7077t1q gm_l82; /* el */
    gm_t083kd7b1kkag gm_l83; /* x */
    gm_teo9d4mncjfdu gm_l85; /* x */
    gm_te9hrk0u35rev gm_l86; /* target */
    uint32_t gm_l87; /* levels */
    void * gm_l88; /* value */
    void * gm_l90; /* k */
    void * gm_l91; /* value */
    void * gm_l92; /* k */
    void * gm_l93; /* message */
    void * gm_l94; /* w */
    GM_STACK_CHECK(&F, 7623u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l1;
        gm_t88rcucg1b7uk t3 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t2) + 0));
        gm_l2 = t3;
    }
    {
        void * t4 = gm_l1;
        gm_teti411eujvf4 t5 = GM_LD(gm_teti411eujvf4, (((uint8_t *)t4) + 8));
        uint8_t t63 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        switch (t63) { case ((uint8_t)0ULL): goto ma7; case ((uint8_t)1ULL): goto ma8; case ((uint8_t)2ULL): goto ma61; case ((uint8_t)3ULL): goto ma61; case ((uint8_t)4ULL): goto ma9; case ((uint8_t)5ULL): goto ma10; case ((uint8_t)6ULL): goto ma11; case ((uint8_t)7ULL): goto ma12; case ((uint8_t)8ULL): goto ma13; case ((uint8_t)9ULL): goto ma61; case ((uint8_t)10ULL): goto ma61; case ((uint8_t)11ULL): goto ma61; case ((uint8_t)12ULL): goto ma61; case ((uint8_t)13ULL): goto ma61; case ((uint8_t)14ULL): goto ma14; case ((uint8_t)15ULL): goto ma16; case ((uint8_t)16ULL): goto ma17; case ((uint8_t)17ULL): goto ma18; case ((uint8_t)18ULL): goto ma17; case ((uint8_t)19ULL): goto ma15; case ((uint8_t)20ULL): goto ma17; case ((uint8_t)21ULL): goto ma19; case ((uint8_t)22ULL): goto ma20; case ((uint8_t)23ULL): goto ma21; case ((uint8_t)24ULL): goto ma22; case ((uint8_t)25ULL): goto ma23; case ((uint8_t)26ULL): goto ma24; case ((uint8_t)27ULL): goto ma25; case ((uint8_t)28ULL): goto ma26; case ((uint8_t)29ULL): goto ma27; case ((uint8_t)30ULL): goto ma28; case ((uint8_t)31ULL): goto ma31; case ((uint8_t)32ULL): goto ma32; case ((uint8_t)33ULL): goto ma33; case ((uint8_t)34ULL): goto ma34; case ((uint8_t)35ULL): goto ma35; case ((uint8_t)36ULL): goto ma36; case ((uint8_t)37ULL): goto ma39; case ((uint8_t)38ULL): goto ma40; case ((uint8_t)39ULL): goto ma41; case ((uint8_t)40ULL): goto ma42; case ((uint8_t)41ULL): goto ma48; case ((uint8_t)42ULL): goto ma49; case ((uint8_t)43ULL): goto ma43; case ((uint8_t)44ULL): goto ma47; case ((uint8_t)45ULL): goto ma44; case ((uint8_t)46ULL): goto ma45; case ((uint8_t)47ULL): goto ma46; case ((uint8_t)48ULL): goto ma50; case ((uint8_t)49ULL): goto ma51; case ((uint8_t)50ULL): goto ma52; case ((uint8_t)51ULL): goto ma53; case ((uint8_t)52ULL): goto ma57; case ((uint8_t)53ULL): goto ma55; case ((uint8_t)54ULL): goto ma56; case ((uint8_t)55ULL): goto ma60; case ((uint8_t)56ULL): goto ma58; case ((uint8_t)57ULL): goto ma59; case ((uint8_t)58ULL): goto ma54; default: goto ma62; }
ma7:;
        uint8_t t65 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t65 != ((uint8_t)0ULL)) goto mn64;
        F.l3 = (*(gm_tbci5ospk5nnn *)(((uint8_t *)&t5) + 8));
        void * t66 = gm_l0;
        gm_t88rcucg1b7uk t67 = gm_l2;
        gm_b16_8 t68 = gm_f_int_lit_ai37456((void *)NULL, cx, NULL, t66, t67, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l3)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t68);
        goto gm_fdone;
        goto me6;
mn64:;
ma8:;
        uint8_t t70 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t70 != ((uint8_t)1ULL)) goto mn69;
        double t71 = GM_LD(double, (((uint8_t *)&t5) + 8));
        gm_l4 = t71;
        {
            void * t74 = gm_l0;
            void * t75 = GM_LD(void *, (((uint8_t *)t74) + 0));
            void * t76 = (void *)(((uint8_t *)t75) + 8);
            gm_t88rcucg1b7uk t77 = gm_l2;
            gm_t53at80pfdus3 t78;
            gm_f_kind_ghvav22((void *)&t78, t76, GM_CONV(gm_csguankko4lmhd, t77));
            uint8_t t79 = GM_LD(uint8_t, (((uint8_t *)&t78) + 0));
            if (t79 != ((uint8_t)3ULL)) goto ie73;
            uint16_t t80 = GM_LD(uint16_t, (((uint8_t *)&t78) + 8));
            gm_l5 = t80;
            {
                uint16_t t82 = gm_l5;
                double t83 = gm_l4;
                gm_b16_8 t84 = gm_f_float_lit_26lt2tp((void *)NULL, cx, NULL, t82, t83);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
                ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t84);
                goto gm_fdone;
            }
            goto ix73;
ie73:;
ix73:;
        }
        {
            void * t85 = gm_l1;
            gm_tl874t2atnlh3 t86 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t85) + 72));
            gm_b16_8 t87;
            GM_ST(void *, (uint8_t *)&t87, (void *)(gm_s81vh0gpuu9rd));
            GM_ST(uintptr_t, (uint8_t *)&t87 + 8, (uintptr_t)(35));
            gm_t08olq97rscn3 t88;
            gm_f_unsupported_531q51r((void *)&t88, (void *)NULL, cx, NULL, t86, t87);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 1;
            GM_ST(gm_t08olq97rscn3, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t88);
            goto gm_fdone;
        }
        goto me6;
mn69:;
ma9:;
        uint8_t t90 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t90 != ((uint8_t)4ULL)) goto mn89;
        uint8_t t91 = GM_LD(uint8_t, (((uint8_t *)&t5) + 8));
        gm_l6 = t91;
        gm_t5o0tpv5jka2l t92;
        uint8_t t94 = gm_l6;
        if (!t94) goto ie93;
        gm_b16_8 t96;
        GM_ST(void *, (uint8_t *)&t96, (void *)(gm_sd5qvckqda7oh));
        GM_ST(uintptr_t, (uint8_t *)&t96 + 8, (uintptr_t)(12));
        gm_b16_8 t97 = t96;
        gm_t5o0tpv5jka2l t98;
        GM_ST(uint8_t, (uint8_t *)&t98 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t98 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t98 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t98 + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&t98 + 32, 0);
        ((uint8_t *)&t98)[0] = 0;
        GM_ST(gm_b16_8, ((uint8_t *)&t98 + 8), t97);
        t92 = t98;
        goto ix93;
ie93:;
        gm_b16_8 t100;
        GM_ST(void *, (uint8_t *)&t100, (void *)(gm_slmqf5nrs9f6b));
        GM_ST(uintptr_t, (uint8_t *)&t100 + 8, (uintptr_t)(12));
        gm_b16_8 t101 = t100;
        gm_t5o0tpv5jka2l t102;
        GM_ST(uint8_t, (uint8_t *)&t102 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t102 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t102 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t102 + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&t102 + 32, 0);
        ((uint8_t *)&t102)[0] = 0;
        GM_ST(gm_b16_8, ((uint8_t *)&t102 + 8), t101);
        t92 = t102;
ix93:;
        uint8_t t103 = ((((uint8_t *)&t92))[0] != 0);
        F.ret = t92;
        goto gm_fret;
        goto me6;
mn89:;
ma10:;
        uint8_t t105 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t105 != ((uint8_t)5ULL)) goto mn104;
        gm_b16_8 t106 = GM_LD(gm_b16_8, (((uint8_t *)&t5) + 8));
        gm_l7 = t106;
        {
            void * t108 = gm_l0;
            void * t109 = GM_LD(void *, (((uint8_t *)t108) + 0));
            gm_b16_8 t110 = gm_l7;
            gm_b16_8 t111 = gm_f_static_bytes_u0uvco3((void *)NULL, cx, NULL, t109, t110);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l8 = t111;
        }
        {
            void * t112 = gm_l0;
            gm_t88rcucg1b7uk t113 = gm_l2;
            gm_b16_8 t114 = gm_l8;
            uint64_t t115 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
            gm_t4ce0jd3tdro4 t116;
            GM_ST(uint64_t, ((uint8_t *)&t116 + 0), t115);
            gm_tabb710d9mq2e t117;
            gm_f_format_sa1940l((void *)&t117, (void *)NULL, cx, NULL, t116);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t117))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t117) + 8, gm_src0gtuh94b1fce, 313, 40);
            gm_b16_8 t118 = GM_LD(gm_b16_8, (((uint8_t *)&t117) + 8));
            gm_b16_8 t119 = GM_LD(gm_b16_8, ((uint8_t *)&t118));
            gm_b16_8 t120 = gm_f_make_slice_sa50efe((void *)NULL, cx, NULL, t112, t113, t114, t119);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
            ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t120);
            goto gm_fdone;
        }
        goto me6;
mn104:;
ma11:;
        uint8_t t122 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t122 != ((uint8_t)6ULL)) goto mn121;
        gm_b16_8 t123 = GM_LD(gm_b16_8, (((uint8_t *)&t5) + 8));
        gm_l9 = t123;
        void * t124 = gm_l0;
        void * t125 = GM_LD(void *, (((uint8_t *)t124) + 0));
        gm_b16_8 t126 = gm_l9;
        gm_b16_8 t127 = gm_f_static_bytes_u0uvco3((void *)NULL, cx, NULL, t125, t126);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t128;
        GM_ST(gm_b16_8, ((uint8_t *)&t128 + 0), t127);
        gm_tabb710d9mq2e t129;
        gm_f_format_jet3e97((void *)&t129, (void *)NULL, cx, NULL, t128);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t129))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t129) + 8, gm_src0gtuh94b1fce, 315, 28);
        gm_b16_8 t130 = GM_LD(gm_b16_8, (((uint8_t *)&t129) + 8));
        gm_b16_8 t131 = GM_LD(gm_b16_8, ((uint8_t *)&t130));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t131);
        goto gm_fdone;
        goto me6;
mn121:;
ma12:;
        uint8_t t133 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t133 != ((uint8_t)7ULL)) goto mn132;
        void * t134 = gm_l0;
        gm_t88rcucg1b7uk t135 = gm_l2;
        gm_b16_8 t136 = gm_f_make_none_v2s8s0l((void *)NULL, cx, NULL, t134, t135);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t136);
        goto gm_fdone;
        goto me6;
mn132:;
ma13:;
        uint8_t t138 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t138 != ((uint8_t)8ULL)) goto mn137;
        {
            void * t140 = gm_l0;
            gm_t88rcucg1b7uk t141 = gm_l2;
            gm_b16_8 t142 = gm_f_tmp_uninit_jr39s5u((void *)NULL, cx, NULL, t140, t141);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l10 = t142;
        }
        {
            void * t144 = gm_l0;
            void * t145 = GM_LD(void *, (((uint8_t *)t144) + 0));
            uint8_t t146 = GM_LD(uint8_t, ((((uint8_t *)t145) + 1136) + 1));
            if (!t146) goto ie143;
            {
                void * t148 = gm_l0;
                gm_t6j6smj7qoctu t149;
                gm_b16_8 t150 = gm_l10;
                GM_ST(gm_b16_8, (uint8_t *)&t149 + 0, t150);
                gm_b16_8 t151 = gm_l10;
                GM_ST(gm_b16_8, (uint8_t *)&t149 + 16, t151);
                gm_b0_1 t152 = gm_f_linef_caq6p77((void *)NULL, cx, NULL, t148, &t149);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix143;
ie143:;
ix143:;
        }
        {
            gm_b16_8 t153 = gm_l10;
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
            ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t153);
            goto gm_fdone;
        }
        goto me6;
mn137:;
ma14:;
        uint8_t t155 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t155 != ((uint8_t)14ULL)) goto mn154;
        gm_tl874t2atnlh3 t156 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)&t5) + 8) + 16));
        gm_l11 = t156;
        {
            void * t158 = gm_l0;
            void * t159 = GM_LD(void *, (((uint8_t *)t158) + 0));
            gm_tl874t2atnlh3 t160 = gm_l11;
            gm_t88rcucg1b7uk t161 = gm_l2;
            void * t162 = gm_l1;
            gm_tl874t2atnlh3 t163 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t162) + 72));
            gm_t5o0tpv5jka2l t164;
            gm_f_const_storage_0d7nj5d((void *)&t164, (void *)NULL, cx, NULL, t159, t160, t161, t163);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t164))[0] != 0)) {
                gm_t5o0tpv5jka2l t165;
                GM_ST(uint8_t, (uint8_t *)&t165 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t165 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t165 + 4, 0);
                ((uint8_t *)&t165)[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t165 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t164) + 8)));
                F.ret = t165;
                goto gm_fret;
            }
            gm_b16_8 t166 = GM_LD(gm_b16_8, (((uint8_t *)&t164) + 8));
            gm_l12 = t166;
        }
        {
            void * t167 = gm_l0;
            gm_t88rcucg1b7uk t168 = gm_l2;
            gm_b16_8 t169 = gm_l12;
            gm_b16_8 t170 = gm_f_load_0hebde7((void *)NULL, cx, NULL, t167, t168, t169);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
            ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t170);
            goto gm_fdone;
        }
        goto me6;
mn154:;
ma15:;
        uint8_t t172 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t172 != ((uint8_t)19ULL)) goto mn171;
        void * t173 = GM_LD(void *, ((((uint8_t *)&t5) + 8) + 0));
        gm_l13 = t173;
        uint32_t t174 = GM_LD(uint32_t, ((((uint8_t *)&t5) + 8) + 8));
        gm_l14 = t174;
        void * t175 = gm_l0;
        void * t176 = GM_LD(void *, (((uint8_t *)t175) + 0));
        void * t177 = (void *)(((uint8_t *)t176) + 8);
        void * t178 = gm_l13;
        gm_t88rcucg1b7uk t179 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t178) + 0));
        gm_t57euh612ligr t180;
        gm_f_bits_layout_23rect9((void *)&t180, (void *)NULL, cx, NULL, t177, t179);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t181;
        if (((((uint8_t *)&t180))[24] != 0)) goto mn183;
        t181 = ((uint8_t)0);
        goto me182;
mn183:;
        t181 = ((uint8_t)1);
        goto me182;
mn184:;
        gm_trap("no match arm matched the scrutinee", gm_src0gtuh94b1fce, 327, 32);
me182:;
        if (!t181) goto mn171;
        void * t185 = gm_l0;
        void * t186 = gm_l13;
        uint32_t t187 = gm_l14;
        gm_f_bits_field_os47f03((void *)&(*(gm_t5o0tpv5jka2l *)gm_out), (void *)NULL, cx, NULL, t185, t186, t187);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me6;
mn171:;
ma16:;
        uint8_t t189 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t189 != ((uint8_t)15ULL)) goto mn188;
        uint32_t t190 = GM_LD(uint32_t, (((uint8_t *)&t5) + 8));
        gm_l15 = t190;
        uint8_t t191 = 0;
        void * t192 = gm_l0;
        uint32_t t193 = gm_l15;
        uint8_t t194 = gm_f_is_borrowed_qi4apdt(t192, t193);
        uint8_t t195 = (uint8_t)!t194;
        if (t195) {
            uint32_t t196 = gm_l15;
            uint64_t t197 = (uint64_t)t196;
            uint64_t t198 = (uintptr_t)t197;
            void * t199 = gm_l0;
            void * t200 = GM_LD(void *, (((uint8_t *)t199) + 176));
            uint64_t t201 = GM_LD(uintptr_t, (((uint8_t *)t200) + 48) + 8);
            if (t198 >= (uintptr_t)(t201)) gm_trap_index(t198, t201, gm_src0gtuh94b1fce, 329, 48);
            gm_t88rcucg1b7uk t202 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, (((uint8_t *)t200) + 48)) + t198 * 40) + 16));
            gm_t88rcucg1b7uk t203 = gm_l2;
            uint8_t t204 = (gm_eq9m6mvtmgde(((uint8_t *)&t202), ((uint8_t *)&t203)));
            t191 = t204;
        } else t191 = 0;
        if (!t191) goto mn188;
        {
            void * t206 = gm_l0;
            uint32_t t207 = gm_l15;
            gm_b16_8 t208 = gm_f_local_lv_m2jtf0n((void *)NULL, cx, NULL, t206, t207);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l16 = t208;
        }
        {
            void * t209 = gm_l0;
            gm_t88rcucg1b7uk t210 = gm_l2;
            gm_b16_8 t211 = gm_l16;
            gm_b16_8 t212 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t209, t210, t211);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
            ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t212);
            goto gm_fdone;
        }
        goto me6;
mn188:;
ma17:;
        uint8_t t216 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t216 != ((uint8_t)15ULL)) goto pa215;
        goto po214;
pa215:;
        uint8_t t218 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t218 != ((uint8_t)16ULL)) goto pa217;
        goto po214;
pa217:;
        uint8_t t220 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t220 != ((uint8_t)18ULL)) goto pa219;
        goto po214;
pa219:;
        uint8_t t222 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t222 != ((uint8_t)19ULL)) goto pa221;
        goto po214;
pa221:;
        uint8_t t224 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t224 != ((uint8_t)20ULL)) goto pa223;
        goto po214;
pa223:;
        goto mn213;
po214:;
        {
            void * t226 = gm_l0;
            void * t227 = gm_l1;
            gm_t5o0tpv5jka2l t228;
            gm_f_place_addr_7ji15ot((void *)&t228, (void *)NULL, cx, NULL, t226, t227);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t228))[0] != 0)) {
                gm_t5o0tpv5jka2l t229;
                GM_ST(uint8_t, (uint8_t *)&t229 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t229 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t229 + 4, 0);
                ((uint8_t *)&t229)[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t229 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t228) + 8)));
                F.ret = t229;
                goto gm_fret;
            }
            gm_b16_8 t230 = GM_LD(gm_b16_8, (((uint8_t *)&t228) + 8));
            gm_l17 = t230;
        }
        {
            void * t231 = gm_l0;
            gm_t88rcucg1b7uk t232 = gm_l2;
            gm_b16_8 t233 = gm_l17;
            gm_b16_8 t234 = gm_f_load_0hebde7((void *)NULL, cx, NULL, t231, t232, t233);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
            ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t234);
            goto gm_fdone;
        }
        goto me6;
mn213:;
ma18:;
        uint8_t t236 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t236 != ((uint8_t)17ULL)) goto mn235;
        {
            void * t238 = gm_l0;
            void * t239 = gm_l1;
            gm_t5o0tpv5jka2l t240;
            gm_f_place_addr_7ji15ot((void *)&t240, (void *)NULL, cx, NULL, t238, t239);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t240))[0] != 0)) {
                gm_t5o0tpv5jka2l t241;
                GM_ST(uint8_t, (uint8_t *)&t241 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t241 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t241 + 4, 0);
                ((uint8_t *)&t241)[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t241 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t240) + 8)));
                F.ret = t241;
                goto gm_fret;
            }
            gm_b16_8 t242 = GM_LD(gm_b16_8, (((uint8_t *)&t240) + 8));
            gm_l18 = t242;
        }
        {
            void * t243 = gm_l0;
            gm_t88rcucg1b7uk t244 = gm_l2;
            gm_b16_8 t245 = gm_l18;
            gm_b16_8 t246 = gm_f_load_0hebde7((void *)NULL, cx, NULL, t243, t244, t245);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
            ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t246);
            goto gm_fdone;
        }
        goto me6;
mn235:;
ma19:;
        uint8_t t248 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t248 != ((uint8_t)21ULL)) goto mn247;
        gm_l19 = (*(gm_tao63pk3r18v5 *)(((uint8_t *)&t5) + 8));
        void * t249 = gm_l0;
        void * t250 = gm_l1;
        void * t251 = GM_LD(void *, (((uint8_t *)&gm_l19) + 0));
        void * t252 = GM_LD(void *, (((uint8_t *)&gm_l19) + 8));
        void * t253 = GM_LD(void *, (((uint8_t *)&gm_l19) + 16));
        uint8_t t254 = GM_LD(uint8_t, (((uint8_t *)&gm_l19) + 24));
        gm_f_slice_expr_bln8glj((void *)&(*(gm_t5o0tpv5jka2l *)gm_out), (void *)NULL, cx, NULL, t249, t250, t251, t252, t253, t254);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me6;
mn247:;
ma20:;
        uint8_t t256 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t256 != ((uint8_t)22ULL)) goto mn255;
        void * t257 = GM_LD(void *, (((uint8_t *)&t5) + 8));
        gm_l20 = t257;
        {
            void * t259 = gm_l0;
            void * t260 = gm_l20;
            gm_t5o0tpv5jka2l t261;
            gm_f_place_addr_7ji15ot((void *)&t261, (void *)NULL, cx, NULL, t259, t260);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t261))[0] != 0)) {
                gm_t5o0tpv5jka2l t262;
                GM_ST(uint8_t, (uint8_t *)&t262 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t262 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t262 + 4, 0);
                ((uint8_t *)&t262)[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t262 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t261) + 8)));
                F.ret = t262;
                goto gm_fret;
            }
            gm_b16_8 t263 = GM_LD(gm_b16_8, (((uint8_t *)&t261) + 8));
            gm_l21 = t263;
        }
        {
            void * t264 = gm_l0;
            gm_b16_8 t265 = gm_l21;
            gm_b16_8 t266 = gm_f_slice_len_a3383br((void *)NULL, cx, NULL, t264, t265);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l22 = t266;
        }
        {
            void * t267 = gm_l0;
            gm_t88rcucg1b7uk t268 = gm_l2;
            gm_b16_8 t269 = gm_l22;
            gm_b16_8 t270 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t267, t268, t269);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
            ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t270);
            goto gm_fdone;
        }
        goto me6;
mn255:;
ma21:;
        uint8_t t272 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t272 != ((uint8_t)23ULL)) goto mn271;
        void * t273 = GM_LD(void *, (((uint8_t *)&t5) + 8));
        gm_l23 = t273;
        {
            void * t275 = gm_l0;
            void * t276 = gm_l23;
            gm_t5o0tpv5jka2l t277;
            gm_f_place_addr_7ji15ot((void *)&t277, (void *)NULL, cx, NULL, t275, t276);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t277))[0] != 0)) {
                gm_t5o0tpv5jka2l t278;
                GM_ST(uint8_t, (uint8_t *)&t278 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t278 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t278 + 4, 0);
                ((uint8_t *)&t278)[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t278 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t277) + 8)));
                F.ret = t278;
                goto gm_fret;
            }
            gm_b16_8 t279 = GM_LD(gm_b16_8, (((uint8_t *)&t277) + 8));
            gm_l24 = t279;
        }
        {
            gm_b16_8 t280 = gm_l24;
            gm_b16_8 t281 = gm_f_slice_ptr_hojfgr5((void *)NULL, cx, NULL, t280);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l25 = t281;
        }
        {
            void * t282 = gm_l0;
            gm_t88rcucg1b7uk t283 = gm_l2;
            gm_b16_8 t284 = gm_l25;
            gm_b16_8 t285 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t282, t283, t284);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
            ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t285);
            goto gm_fdone;
        }
        goto me6;
mn271:;
ma22:;
        uint8_t t287 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t287 != ((uint8_t)24ULL)) goto mn286;
        void * t288 = GM_LD(void *, (((uint8_t *)&t5) + 8));
        gm_l26 = t288;
        {
            void * t290 = gm_l0;
            void * t291 = gm_l26;
            gm_t5o0tpv5jka2l t292;
            gm_f_place_addr_7ji15ot((void *)&t292, (void *)NULL, cx, NULL, t290, t291);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t292))[0] != 0)) {
                gm_t5o0tpv5jka2l t293;
                GM_ST(uint8_t, (uint8_t *)&t293 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t293 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t293 + 4, 0);
                ((uint8_t *)&t293)[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t293 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t292) + 8)));
                F.ret = t293;
                goto gm_fret;
            }
            gm_b16_8 t294 = GM_LD(gm_b16_8, (((uint8_t *)&t292) + 8));
            gm_l27 = t294;
        }
        {
            void * t295 = gm_l0;
            gm_t88rcucg1b7uk t296 = gm_l2;
            gm_b16_8 t297 = gm_l27;
            gm_tnpifv20irnph t298;
            GM_ST(gm_b16_8, ((uint8_t *)&t298 + 0), t297);
            gm_tabb710d9mq2e t299;
            gm_f_format_be3fee1((void *)&t299, (void *)NULL, cx, NULL, t298);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t299))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t299) + 8, gm_src0gtuh94b1fce, 354, 30);
            gm_b16_8 t300 = GM_LD(gm_b16_8, (((uint8_t *)&t299) + 8));
            gm_b16_8 t301 = GM_LD(gm_b16_8, ((uint8_t *)&t300));
            gm_b16_8 t302 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t295, t296, t301);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
            ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t302);
            goto gm_fdone;
        }
        goto me6;
mn286:;
ma23:;
        uint8_t t304 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t304 != ((uint8_t)25ULL)) goto mn303;
        void * t305 = GM_LD(void *, (((uint8_t *)&t5) + 8));
        gm_l28 = t305;
        {
            void * t307 = gm_l0;
            void * t308 = gm_l28;
            gm_t5o0tpv5jka2l t309;
            gm_f_expr_porbc9t((void *)&t309, (void *)NULL, cx, NULL, t307, t308);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t309))[0] != 0)) {
                gm_t5o0tpv5jka2l t310;
                GM_ST(uint8_t, (uint8_t *)&t310 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t310 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t310 + 4, 0);
                ((uint8_t *)&t310)[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t310 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t309) + 8)));
                F.ret = t310;
                goto gm_fret;
            }
            gm_b16_8 t311 = GM_LD(gm_b16_8, (((uint8_t *)&t309) + 8));
            gm_l29 = t311;
        }
        {
            void * t312 = gm_l0;
            void * t313 = gm_l28;
            gm_t88rcucg1b7uk t314 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t313) + 0));
            gm_b16_8 t315 = gm_l29;
            gm_b16_8 t316 = gm_f_addr_of_hiaqkn0((void *)NULL, cx, NULL, t312, t314, t315);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l30 = t316;
        }
        {
            void * t317 = gm_l0;
            gm_t88rcucg1b7uk t318 = gm_l2;
            gm_b16_8 t319 = gm_l30;
            gm_tnpifv20irnph t320;
            GM_ST(gm_b16_8, ((uint8_t *)&t320 + 0), t319);
            gm_tabb710d9mq2e t321;
            gm_f_format_be3fee1((void *)&t321, (void *)NULL, cx, NULL, t320);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t321))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t321) + 8, gm_src0gtuh94b1fce, 359, 30);
            gm_b16_8 t322 = GM_LD(gm_b16_8, (((uint8_t *)&t321) + 8));
            gm_b16_8 t323 = GM_LD(gm_b16_8, ((uint8_t *)&t322));
            gm_b16_8 t324 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t317, t318, t323);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
            ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t324);
            goto gm_fdone;
        }
        goto me6;
mn303:;
ma24:;
        uint8_t t326 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t326 != ((uint8_t)26ULL)) goto mn325;
        uint32_t t327 = GM_LD(uint32_t, (((uint8_t *)&t5) + 8));
        gm_l31 = t327;
        {
            void * t329 = gm_l0;
            void * t330 = GM_LD(void *, (((uint8_t *)t329) + 0));
            uint32_t t331 = gm_l31;
            gm_b0_1 t332 = gm_f_queue_cd51r8c((void *)NULL, cx, NULL, t330, t331);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t333 = gm_l0;
            void * t334 = GM_LD(void *, (((uint8_t *)t333) + 0));
            uint32_t t335 = gm_l31;
            gm_b16_8 t336 = gm_f_addr_name_g5lrlt3((void *)NULL, cx, NULL, t334, t335);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l32 = t336;
        }
        {
            void * t337 = gm_l0;
            void * t338 = GM_LD(void *, (((uint8_t *)t337) + 0));
            uint32_t t339 = gm_l31;
            uint8_t t340 = gm_f_is_plain_qr08i40((void *)NULL, cx, NULL, t338, t339);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l33 = t340;
        }
        {
            gm_b16_8 t341;
            uint8_t t343 = gm_l33;
            if (!t343) goto ie342;
            void * t344 = gm_l0;
            gm_t88rcucg1b7uk t345 = gm_l2;
            uint8_t t346 = gm_f_ptr_type_plain_sanetd4((void *)NULL, cx, NULL, t344, t345);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t347 = (uint8_t)!t346;
            if (!t347) goto ie342;
            gm_b16_8 t349 = gm_l32;
            gm_tnpifv20irnph t350;
            GM_ST(gm_b16_8, ((uint8_t *)&t350 + 0), t349);
            gm_tabb710d9mq2e t351;
            gm_f_format_8sjjbd9((void *)&t351, (void *)NULL, cx, NULL, t350);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t351))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t351) + 8, gm_src0gtuh94b1fce, 367, 68);
            gm_b16_8 t352 = GM_LD(gm_b16_8, (((uint8_t *)&t351) + 8));
            gm_b16_8 t353 = t352;
            gm_b16_8 t354 = GM_LD(gm_b16_8, ((uint8_t *)&t353));
            t341 = t354;
            goto ix342;
ie342:;
            gm_b16_8 t356 = gm_l32;
            gm_tnpifv20irnph t357;
            GM_ST(gm_b16_8, ((uint8_t *)&t357 + 0), t356);
            gm_tabb710d9mq2e t358;
            gm_f_format_jet3e97((void *)&t358, (void *)NULL, cx, NULL, t357);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t358))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t358) + 8, gm_src0gtuh94b1fce, 367, 113);
            gm_b16_8 t359 = GM_LD(gm_b16_8, (((uint8_t *)&t358) + 8));
            gm_b16_8 t360 = t359;
            gm_b16_8 t361 = GM_LD(gm_b16_8, ((uint8_t *)&t360));
            t341 = t361;
ix342:;
            gm_l34 = t341;
        }
        {
            void * t363 = gm_l0;
            void * t364 = GM_LD(void *, (((uint8_t *)t363) + 0));
            void * t365 = (void *)(((uint8_t *)t364) + 8);
            gm_t88rcucg1b7uk t366 = gm_l2;
            gm_t53at80pfdus3 t367;
            gm_f_kind_ghvav22((void *)&t367, t365, GM_CONV(gm_csguankko4lmhd, t366));
            uint8_t t368;
            uint8_t t371 = GM_LD(uint8_t, (((uint8_t *)&t367) + 0));
            if (t371 != ((uint8_t)15ULL)) goto mn370;
            t368 = ((uint8_t)1);
            goto me369;
mn370:;
            t368 = ((uint8_t)0);
            goto me369;
mn372:;
            gm_trap("no match arm matched the scrutinee", gm_src0gtuh94b1fce, 368, 16);
me369:;
            if (!t368) goto ie362;
            {
                void * t374 = gm_l0;
                gm_t88rcucg1b7uk t375 = gm_l2;
                gm_t6j6smj7qoctu t376;
                gm_b16_8 t377 = gm_l34;
                GM_ST(gm_b16_8, (uint8_t *)&t376 + 0, t377);
                void * t378 = gm_l0;
                gm_b16_8 t379 = GM_LD(gm_b16_8, (((uint8_t *)t378) + 280));
                GM_ST(gm_b16_8, (uint8_t *)&t376 + 16, t379);
                gm_tabb710d9mq2e t380;
                gm_f_format_0017fui((void *)&t380, (void *)NULL, cx, NULL, &t376);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t380))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t380) + 8, gm_src0gtuh94b1fce, 369, 34);
                gm_b16_8 t381 = GM_LD(gm_b16_8, (((uint8_t *)&t380) + 8));
                gm_b16_8 t382 = GM_LD(gm_b16_8, ((uint8_t *)&t381));
                gm_b16_8 t383 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t374, t375, t382);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
                ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t383);
                goto gm_fdone;
            }
            goto ix362;
ie362:;
ix362:;
        }
        {
            gm_b16_8 t384 = gm_l34;
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
            ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t384);
            goto gm_fdone;
        }
        goto me6;
mn325:;
ma25:;
        uint8_t t386 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t386 != ((uint8_t)27ULL)) goto mn385;
        uint32_t t387 = GM_LD(uint32_t, (((uint8_t *)&t5) + 8));
        gm_l35 = t387;
        {
            void * t389 = gm_l0;
            void * t390 = GM_LD(void *, (((uint8_t *)t389) + 0));
            uint32_t t391 = gm_l35;
            gm_b0_1 t392 = gm_f_queue_cd51r8c((void *)NULL, cx, NULL, t390, t391);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t393 = gm_l0;
            void * t394 = GM_LD(void *, (((uint8_t *)t393) + 0));
            uint32_t t395 = gm_l35;
            gm_b16_8 t396 = gm_f_fn_name_6n34573((void *)NULL, cx, NULL, t394, t395);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l36 = t396;
        }
        {
            void * t397 = gm_l0;
            gm_t88rcucg1b7uk t398 = gm_l2;
            gm_t6j6smj7qoctu t399;
            gm_b16_8 t400 = gm_l36;
            GM_ST(gm_b16_8, (uint8_t *)&t399 + 0, t400);
            void * t401 = gm_l0;
            gm_b16_8 t402 = GM_LD(gm_b16_8, (((uint8_t *)t401) + 280));
            GM_ST(gm_b16_8, (uint8_t *)&t399 + 16, t402);
            gm_tabb710d9mq2e t403;
            gm_f_format_nd352q9((void *)&t403, (void *)NULL, cx, NULL, &t399);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t403))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t403) + 8, gm_src0gtuh94b1fce, 376, 30);
            gm_b16_8 t404 = GM_LD(gm_b16_8, (((uint8_t *)&t403) + 8));
            gm_b16_8 t405 = GM_LD(gm_b16_8, ((uint8_t *)&t404));
            gm_b16_8 t406 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t397, t398, t405);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
            ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t406);
            goto gm_fdone;
        }
        goto me6;
mn385:;
ma26:;
        uint8_t t408 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t408 != ((uint8_t)28ULL)) goto mn407;
        void * t409 = gm_l0;
        gm_t88rcucg1b7uk t410 = gm_l2;
        void * t411 = gm_l0;
        gm_b16_8 t412 = GM_LD(gm_b16_8, (((uint8_t *)t411) + 280));
        gm_tnpifv20irnph t413;
        GM_ST(gm_b16_8, ((uint8_t *)&t413 + 0), t412);
        gm_tabb710d9mq2e t414;
        gm_f_format_1coo7na((void *)&t414, (void *)NULL, cx, NULL, t413);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t414))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t414) + 8, gm_src0gtuh94b1fce, 378, 44);
        gm_b16_8 t415 = GM_LD(gm_b16_8, (((uint8_t *)&t414) + 8));
        gm_b16_8 t416 = GM_LD(gm_b16_8, ((uint8_t *)&t415));
        gm_b16_8 t417 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t409, t410, t416);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t417);
        goto gm_fdone;
        goto me6;
mn407:;
ma27:;
        uint8_t t419 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t419 != ((uint8_t)29ULL)) goto mn418;
        gm_t9qkh0rsooc8d t420 = GM_LD(gm_t9qkh0rsooc8d, ((((uint8_t *)&t5) + 8) + 0));
        gm_l37 = t420;
        void * t421 = GM_LD(void *, ((((uint8_t *)&t5) + 8) + 8));
        gm_l38 = t421;
        void * t422 = gm_l0;
        void * t423 = GM_LD(void *, (((uint8_t *)t422) + 0));
        void * t424 = (void *)(((uint8_t *)t423) + 8);
        void * t425 = gm_l38;
        gm_t88rcucg1b7uk t426 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t425) + 0));
        uint8_t t427 = gm_f_is_vector_fm7q9br(t424, GM_CONV(gm_csguankko4lmhd, t426));
        if (!t427) goto mn418;
        void * t428 = gm_l0;
        void * t429 = gm_l1;
        gm_t9qkh0rsooc8d t430 = gm_l37;
        void * t431 = gm_l38;
        gm_f_vector_unary_h7ar7ml((void *)&(*(gm_t5o0tpv5jka2l *)gm_out), (void *)NULL, cx, NULL, t428, t429, t430, t431);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me6;
mn418:;
ma28:;
        uint8_t t433 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t433 != ((uint8_t)30ULL)) goto mn432;
        gm_ts6oqppjh0ba3 t434 = GM_LD(gm_ts6oqppjh0ba3, ((((uint8_t *)&t5) + 8) + 0));
        gm_l39 = t434;
        void * t435 = GM_LD(void *, ((((uint8_t *)&t5) + 8) + 8));
        gm_l40 = t435;
        void * t436 = GM_LD(void *, ((((uint8_t *)&t5) + 8) + 16));
        gm_l41 = t436;
        void * t437 = gm_l0;
        void * t438 = GM_LD(void *, (((uint8_t *)t437) + 0));
        void * t439 = (void *)(((uint8_t *)t438) + 8);
        void * t440 = gm_l40;
        gm_t88rcucg1b7uk t441 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t440) + 0));
        uint8_t t442 = gm_f_is_vector_fm7q9br(t439, GM_CONV(gm_csguankko4lmhd, t441));
        if (!t442) goto mn432;
        void * t443 = gm_l0;
        void * t444 = gm_l1;
        gm_ts6oqppjh0ba3 t445 = gm_l39;
        void * t446 = gm_l40;
        void * t447 = gm_l41;
        gm_f_vector_binary_etnngue((void *)&(*(gm_t5o0tpv5jka2l *)gm_out), (void *)NULL, cx, NULL, t443, t444, t445, t446, t447);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me6;
mn432:;
ma29:;
        uint8_t t449 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t449 != ((uint8_t)29ULL)) goto mn448;
        gm_t9qkh0rsooc8d t450 = GM_LD(gm_t9qkh0rsooc8d, ((((uint8_t *)&t5) + 8) + 0));
        gm_l42 = t450;
        void * t451 = GM_LD(void *, ((((uint8_t *)&t5) + 8) + 8));
        gm_l43 = t451;
        void * t452 = gm_l0;
        void * t453 = gm_l1;
        gm_t9qkh0rsooc8d t454 = gm_l42;
        void * t455 = gm_l43;
        gm_f_unary_i1ekgvj((void *)&(*(gm_t5o0tpv5jka2l *)gm_out), (void *)NULL, cx, NULL, t452, t453, t454, t455);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me6;
mn448:;
ma30:;
        uint8_t t457 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t457 != ((uint8_t)30ULL)) goto mn456;
        gm_ts6oqppjh0ba3 t458 = GM_LD(gm_ts6oqppjh0ba3, ((((uint8_t *)&t5) + 8) + 0));
        gm_l44 = t458;
        void * t459 = GM_LD(void *, ((((uint8_t *)&t5) + 8) + 8));
        gm_l45 = t459;
        void * t460 = GM_LD(void *, ((((uint8_t *)&t5) + 8) + 16));
        gm_l46 = t460;
        void * t461 = gm_l0;
        void * t462 = gm_l1;
        gm_ts6oqppjh0ba3 t463 = gm_l44;
        void * t464 = gm_l45;
        void * t465 = gm_l46;
        gm_f_binary_k69ukj0((void *)&(*(gm_t5o0tpv5jka2l *)gm_out), (void *)NULL, cx, NULL, t461, t462, t463, t464, t465);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me6;
mn456:;
ma31:;
        uint8_t t467 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t467 != ((uint8_t)31ULL)) goto mn466;
        uint8_t t468 = GM_LD(uint8_t, ((((uint8_t *)&t5) + 8) + 0));
        gm_l47 = t468;
        void * t469 = GM_LD(void *, ((((uint8_t *)&t5) + 8) + 8));
        gm_l48 = t469;
        void * t470 = GM_LD(void *, ((((uint8_t *)&t5) + 8) + 16));
        gm_l49 = t470;
        void * t471 = gm_l0;
        uint8_t t472 = gm_l47;
        void * t473 = gm_l48;
        void * t474 = gm_l49;
        gm_f_logic_aafpj13((void *)&(*(gm_t5o0tpv5jka2l *)gm_out), (void *)NULL, cx, NULL, t471, t472, t473, t474);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me6;
mn466:;
ma32:;
        uint8_t t476 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t476 != ((uint8_t)32ULL)) goto mn475;
        gm_tmn7n0n9raou5 t477 = GM_LD(gm_tmn7n0n9raou5, ((((uint8_t *)&t5) + 8) + 0));
        gm_l50 = t477;
        void * t478 = GM_LD(void *, ((((uint8_t *)&t5) + 8) + 8));
        gm_l51 = t478;
        void * t479 = gm_l0;
        void * t480 = gm_l1;
        gm_tmn7n0n9raou5 t481 = gm_l50;
        void * t482 = gm_l51;
        gm_f_coerce_gphlj1h((void *)&(*(gm_t5o0tpv5jka2l *)gm_out), (void *)NULL, cx, NULL, t479, t480, t481, t482);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me6;
mn475:;
ma33:;
        uint8_t t484 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t484 != ((uint8_t)33ULL)) goto mn483;
        gm_tvdg94bk7aa22 t485 = GM_LD(gm_tvdg94bk7aa22, ((((uint8_t *)&t5) + 8) + 0));
        gm_l52 = t485;
        gm_b16_8 t486 = GM_LD(gm_b16_8, ((((uint8_t *)&t5) + 8) + 16));
        gm_l53 = t486;
        void * t487 = gm_l0;
        void * t488 = gm_l1;
        gm_tvdg94bk7aa22 t489 = gm_l52;
        gm_b16_8 t490 = gm_l53;
        gm_f_builtin_s7ark4p((void *)&(*(gm_t5o0tpv5jka2l *)gm_out), (void *)NULL, cx, NULL, t487, t488, t489, t490);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me6;
mn483:;
ma34:;
        uint8_t t492 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t492 != ((uint8_t)34ULL)) goto mn491;
        F.l54 = (*(gm_tsth9qu1dv0g3 *)(((uint8_t *)&t5) + 8));
        gm_b16_8 t493 = GM_LD(gm_b16_8, ((((uint8_t *)&t5) + 8) + 40));
        gm_l55 = t493;
        void * t494 = gm_l0;
        void * t495 = gm_l1;
        gm_b16_8 t496 = gm_l55;
        gm_f_call_nk38pfa((void *)&(*(gm_t5o0tpv5jka2l *)gm_out), (void *)NULL, cx, NULL, t494, t495, &(*(gm_tsth9qu1dv0g3 *)((uint8_t *)&F.l54)), t496);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me6;
mn491:;
ma35:;
        uint8_t t498 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t498 != ((uint8_t)35ULL)) goto mn497;
        gm_b16_8 t499 = GM_LD(gm_b16_8, (((uint8_t *)&t5) + 8));
        gm_l56 = t499;
        void * t500 = gm_l0;
        void * t501 = GM_LD(void *, (((uint8_t *)t500) + 0));
        void * t502 = (void *)(((uint8_t *)t501) + 8);
        gm_t88rcucg1b7uk t503 = gm_l2;
        gm_t57euh612ligr t504;
        gm_f_bits_layout_23rect9((void *)&t504, (void *)NULL, cx, NULL, t502, t503);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t505;
        if (((((uint8_t *)&t504))[24] != 0)) goto mn507;
        t505 = ((uint8_t)0);
        goto me506;
mn507:;
        t505 = ((uint8_t)1);
        goto me506;
mn508:;
        gm_trap("no match arm matched the scrutinee", gm_src0gtuh94b1fce, 387, 30);
me506:;
        if (!t505) goto mn497;
        void * t509 = gm_l0;
        void * t510 = gm_l1;
        gm_b16_8 t511 = gm_l56;
        gm_f_bits_aggregate_bu0ircf((void *)&(*(gm_t5o0tpv5jka2l *)gm_out), (void *)NULL, cx, NULL, t509, t510, t511);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me6;
mn497:;
ma36:;
        uint8_t t515 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t515 != ((uint8_t)35ULL)) goto pa514;
        goto po513;
pa514:;
        uint8_t t517 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t517 != ((uint8_t)36ULL)) goto pa516;
        goto po513;
pa516:;
        goto mn512;
po513:;
        void * t518 = gm_l0;
        void * t519 = GM_LD(void *, (((uint8_t *)t518) + 0));
        void * t520 = (void *)(((uint8_t *)t519) + 8);
        gm_t88rcucg1b7uk t521 = gm_l2;
        uint8_t t522 = gm_f_by_ref_fi0o860((void *)NULL, cx, NULL, t520, t521);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t522) goto mn512;
        {
            void * t524 = gm_l0;
            gm_t88rcucg1b7uk t525 = gm_l2;
            gm_b16_8 t526 = gm_f_tmp_uninit_jr39s5u((void *)NULL, cx, NULL, t524, t525);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l57 = t526;
        }
        {
            void * t527 = gm_l0;
            void * t528 = gm_l1;
            gm_b16_8 t529 = gm_l57;
            gm_tnrltbc61i6v1 t530;
            GM_ST(uint8_t, (uint8_t *)&t530 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t530 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t530 + 4, 0);
            GM_ST(uint32_t, (uint8_t *)&t530 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t530 + 0), ((uint8_t)0ULL));
            gm_tqgtd5gtgj65s t531;
            gm_f_expr_into_dn9kavo((void *)&t531, (void *)NULL, cx, NULL, t527, t528, t529, t530);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t531))[0] != 0)) {
                gm_t5o0tpv5jka2l t532;
                GM_ST(uint8_t, (uint8_t *)&t532 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t532 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t532 + 4, 0);
                ((uint8_t *)&t532)[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t532 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t531) + 8)));
                F.ret = t532;
                goto gm_fret;
            }
            gm_b0_1 t533 = GM_LD(gm_b0_1, (((uint8_t *)&t531) + 8));
        }
        {
            gm_b16_8 t534 = gm_l57;
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
            ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t534);
            goto gm_fdone;
        }
        goto me6;
mn512:;
ma37:;
        uint8_t t536 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t536 != ((uint8_t)35ULL)) goto mn535;
        gm_b16_8 t537 = GM_LD(gm_b16_8, (((uint8_t *)&t5) + 8));
        gm_l58 = t537;
        void * t538 = gm_l0;
        void * t539 = gm_l1;
        gm_b16_8 t540 = gm_l58;
        gm_f_aggregate_fkp0pdv((void *)&(*(gm_t5o0tpv5jka2l *)gm_out), (void *)NULL, cx, NULL, t538, t539, t540);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me6;
mn535:;
ma38:;
        uint8_t t542 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t542 != ((uint8_t)36ULL)) goto mn541;
        gm_b16_8 t543 = GM_LD(gm_b16_8, (((uint8_t *)&t5) + 8));
        gm_l59 = t543;
        void * t544 = gm_l0;
        void * t545 = gm_l1;
        gm_b16_8 t546 = gm_l59;
        gm_f_array_60b0dn9((void *)&(*(gm_t5o0tpv5jka2l *)gm_out), (void *)NULL, cx, NULL, t544, t545, t546);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me6;
mn541:;
ma39:;
        uint8_t t548 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t548 != ((uint8_t)37ULL)) goto mn547;
        void * t549 = GM_LD(void *, (((uint8_t *)&t5) + 8));
        gm_l60 = t549;
        void * t550 = gm_l0;
        void * t551 = gm_l1;
        void * t552 = gm_l60;
        gm_f_array_fill_ppt75jf((void *)&(*(gm_t5o0tpv5jka2l *)gm_out), (void *)NULL, cx, NULL, t550, t551, t552);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me6;
mn547:;
ma40:;
        uint8_t t554 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t554 != ((uint8_t)38ULL)) goto mn553;
        gm_ttggq7ng4kp3p t555 = GM_LD(gm_ttggq7ng4kp3p, (((uint8_t *)&t5) + 8));
        gm_l61 = t555;
        {
            gm_th2lqbo77pvsc t557;
            memset(&t557, 0, sizeof t557);
            gm_l62 = t557;
        }
        {
            void * t559 = GM_LD(void *, (((uint8_t *)&gm_l61) + 8));
            if (!(GM_LD(void *, ((uint8_t *)&t559)) != 0)) goto ie558;
            void * t560 = GM_LD(void *, ((uint8_t *)&t559));
            gm_l63 = t560;
            {
                void * t562 = gm_l0;
                void * t563 = gm_l63;
                gm_t5o0tpv5jka2l t564;
                gm_f_expr_porbc9t((void *)&t564, (void *)NULL, cx, NULL, t562, t563);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t564))[0] != 0)) {
                    gm_t5o0tpv5jka2l t565;
                    GM_ST(uint8_t, (uint8_t *)&t565 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t565 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t565 + 4, 0);
                    ((uint8_t *)&t565)[0] = 1;
                    GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t565 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t564) + 8)));
                    F.ret = t565;
                    goto gm_fret;
                }
                gm_b16_8 t566 = GM_LD(gm_b16_8, (((uint8_t *)&t564) + 8));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l62)) + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l62)) + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l62)) + 20, 0);
                ((uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l62)))[16] = 1;
                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l62)) + 0, t566);
            }
            goto ix558;
ie558:;
ix558:;
        }
        {
            void * t567 = gm_l0;
            gm_t88rcucg1b7uk t568 = gm_l2;
            uint32_t t569 = GM_LD(uint32_t, (((uint8_t *)&gm_l61) + 0));
            uint64_t t570 = (uint64_t)t569;
            gm_b16_8 t571 = gm_f_make_variant_srnk8ha((void *)NULL, cx, NULL, t567, t568, t570, &(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l62)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
            ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t571);
            goto gm_fdone;
        }
        goto me6;
mn553:;
ma41:;
        uint8_t t573 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t573 != ((uint8_t)39ULL)) goto mn572;
        void * t574 = GM_LD(void *, (((uint8_t *)&t5) + 8));
        gm_l64 = t574;
        {
            void * t576 = gm_l0;
            void * t577 = gm_l64;
            gm_t5o0tpv5jka2l t578;
            gm_f_expr_porbc9t((void *)&t578, (void *)NULL, cx, NULL, t576, t577);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t578))[0] != 0)) {
                gm_t5o0tpv5jka2l t579;
                GM_ST(uint8_t, (uint8_t *)&t579 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t579 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t579 + 4, 0);
                ((uint8_t *)&t579)[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t579 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t578) + 8)));
                F.ret = t579;
                goto gm_fret;
            }
            gm_b16_8 t580 = GM_LD(gm_b16_8, (((uint8_t *)&t578) + 8));
            gm_l65 = t580;
        }
        {
            void * t581 = gm_l0;
            gm_t88rcucg1b7uk t582 = gm_l2;
            void * t583 = gm_l64;
            gm_t88rcucg1b7uk t584 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t583) + 0));
            gm_b16_8 t585 = gm_l65;
            gm_b16_8 t586 = gm_f_make_res_riv19po((void *)NULL, cx, NULL, t581, t582, ((uint8_t)1), t584, t585);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
            ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t586);
            goto gm_fdone;
        }
        goto me6;
mn572:;
ma42:;
        uint8_t t588 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t588 != ((uint8_t)40ULL)) goto mn587;
        void * t589 = GM_LD(void *, ((((uint8_t *)&t5) + 8) + 0));
        gm_l66 = t589;
        uint8_t t590 = GM_LD(uint8_t, ((((uint8_t *)&t5) + 8) + 8));
        gm_l67 = t590;
        void * t591 = gm_l0;
        void * t592 = gm_l1;
        void * t593 = gm_l66;
        uint8_t t594 = gm_l67;
        gm_f_unwrap_9lhqi3k((void *)&(*(gm_t5o0tpv5jka2l *)gm_out), (void *)NULL, cx, NULL, t591, t592, t593, t594);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me6;
mn587:;
ma43:;
        uint8_t t596 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t596 != ((uint8_t)43ULL)) goto mn595;
        void * t597 = GM_LD(void *, ((((uint8_t *)&t5) + 8) + 0));
        gm_l68 = t597;
        void * t598 = GM_LD(void *, ((((uint8_t *)&t5) + 8) + 8));
        gm_l69 = t598;
        uint8_t t599 = GM_LD(uint8_t, ((((uint8_t *)&t5) + 8) + 16));
        gm_l70 = t599;
        {
            void * t602 = gm_l0;
            void * t603 = gm_l68;
            void * t604 = gm_l69;
            gm_t6j4d6ei14c2t t605;
            gm_f_variant_equal_bgud9j0((void *)&t605, (void *)NULL, cx, NULL, t602, t603, t604);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t605))[0] != 0)) {
                gm_t5o0tpv5jka2l t606;
                GM_ST(uint8_t, (uint8_t *)&t606 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t606 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t606 + 4, 0);
                ((uint8_t *)&t606)[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t606 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t605) + 8)));
                F.ret = t606;
                goto gm_fret;
            }
            if (!(((((uint8_t *)&t605) + 8))[16] != 0)) goto ie601;
            gm_b16_8 t607 = GM_LD(gm_b16_8, (((uint8_t *)&t605) + 8));
            gm_l71 = t607;
            {
                void * t609 = gm_l0;
                void * t610 = gm_l0;
                void * t611 = GM_LD(void *, (((uint8_t *)t610) + 0));
                void * t612 = (void *)(((uint8_t *)t611) + 8);
                gm_t88rcucg1b7uk t613 = gm_f_bool_ty_kotvo8t((void *)NULL, cx, NULL, t612);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t6j6smj7qoctu t614;
                gm_b16_8 t615;
                uint8_t t617 = gm_l70;
                if (!t617) goto ie616;
                gm_b16_8 t619;
                GM_ST(void *, (uint8_t *)&t619, (void *)(gm_sf9a2kg3eo1gs));
                GM_ST(uintptr_t, (uint8_t *)&t619 + 8, (uintptr_t)(1));
                gm_b16_8 t620 = t619;
                t615 = t620;
                goto ix616;
ie616:;
                gm_b16_8 t622;
                GM_ST(void *, (uint8_t *)&t622, (void *)(gm_sr4qbrk6tlgaa));
                GM_ST(uintptr_t, (uint8_t *)&t622 + 8, (uintptr_t)(0));
                gm_b16_8 t623 = t622;
                t615 = t623;
ix616:;
                GM_ST(gm_b16_8, (uint8_t *)&t614 + 0, t615);
                gm_b16_8 t624 = gm_l71;
                GM_ST(gm_b16_8, (uint8_t *)&t614 + 16, t624);
                gm_tabb710d9mq2e t625;
                gm_f_format_q2e4fnf((void *)&t625, (void *)NULL, cx, NULL, &t614);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t625))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t625) + 8, gm_src0gtuh94b1fce, 409, 50);
                gm_b16_8 t626 = GM_LD(gm_b16_8, (((uint8_t *)&t625) + 8));
                gm_b16_8 t627 = GM_LD(gm_b16_8, ((uint8_t *)&t626));
                gm_b16_8 t628 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t609, t613, t627);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
                ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t628);
                goto gm_fdone;
            }
            goto ix601;
ie601:;
ix601:;
        }
        {
            void * t629 = gm_l0;
            void * t630 = gm_l68;
            gm_t5o0tpv5jka2l t631;
            gm_f_expr_porbc9t((void *)&t631, (void *)NULL, cx, NULL, t629, t630);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t631))[0] != 0)) {
                gm_t5o0tpv5jka2l t632;
                GM_ST(uint8_t, (uint8_t *)&t632 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t632 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t632 + 4, 0);
                ((uint8_t *)&t632)[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t632 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t631) + 8)));
                F.ret = t632;
                goto gm_fret;
            }
            gm_b16_8 t633 = GM_LD(gm_b16_8, (((uint8_t *)&t631) + 8));
            gm_l72 = t633;
        }
        {
            void * t634 = gm_l0;
            void * t635 = gm_l69;
            gm_t5o0tpv5jka2l t636;
            gm_f_expr_porbc9t((void *)&t636, (void *)NULL, cx, NULL, t634, t635);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t636))[0] != 0)) {
                gm_t5o0tpv5jka2l t637;
                GM_ST(uint8_t, (uint8_t *)&t637 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t637 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t637 + 4, 0);
                ((uint8_t *)&t637)[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t637 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t636) + 8)));
                F.ret = t637;
                goto gm_fret;
            }
            gm_b16_8 t638 = GM_LD(gm_b16_8, (((uint8_t *)&t636) + 8));
            gm_l73 = t638;
        }
        {
            void * t639 = gm_l0;
            void * t640 = gm_l68;
            gm_t88rcucg1b7uk t641 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t640) + 0));
            gm_b16_8 t642 = gm_l72;
            gm_b16_8 t643 = gm_l73;
            gm_b16_8 t644 = gm_f_equal_hcuag9d((void *)NULL, cx, NULL, t639, t641, t642, t643);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l74 = t644;
        }
        {
            void * t645 = gm_l0;
            void * t646 = gm_l0;
            void * t647 = GM_LD(void *, (((uint8_t *)t646) + 0));
            void * t648 = (void *)(((uint8_t *)t647) + 8);
            gm_t88rcucg1b7uk t649 = gm_f_bool_ty_kotvo8t((void *)NULL, cx, NULL, t648);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t6j6smj7qoctu t650;
            gm_b16_8 t651;
            uint8_t t653 = gm_l70;
            if (!t653) goto ie652;
            gm_b16_8 t655;
            GM_ST(void *, (uint8_t *)&t655, (void *)(gm_sf9a2kg3eo1gs));
            GM_ST(uintptr_t, (uint8_t *)&t655 + 8, (uintptr_t)(1));
            gm_b16_8 t656 = t655;
            t651 = t656;
            goto ix652;
ie652:;
            gm_b16_8 t658;
            GM_ST(void *, (uint8_t *)&t658, (void *)(gm_sr4qbrk6tlgaa));
            GM_ST(uintptr_t, (uint8_t *)&t658 + 8, (uintptr_t)(0));
            gm_b16_8 t659 = t658;
            t651 = t659;
ix652:;
            GM_ST(gm_b16_8, (uint8_t *)&t650 + 0, t651);
            gm_b16_8 t660 = gm_l74;
            GM_ST(gm_b16_8, (uint8_t *)&t650 + 16, t660);
            gm_tabb710d9mq2e t661;
            gm_f_format_q2e4fnf((void *)&t661, (void *)NULL, cx, NULL, &t650);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t661))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t661) + 8, gm_src0gtuh94b1fce, 414, 46);
            gm_b16_8 t662 = GM_LD(gm_b16_8, (((uint8_t *)&t661) + 8));
            gm_b16_8 t663 = GM_LD(gm_b16_8, ((uint8_t *)&t662));
            gm_b16_8 t664 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t645, t649, t663);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
            ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t664);
            goto gm_fdone;
        }
        goto me6;
mn595:;
ma44:;
        uint8_t t666 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t666 != ((uint8_t)45ULL)) goto mn665;
        void * t667 = GM_LD(void *, ((((uint8_t *)&t5) + 8) + 0));
        gm_l75 = t667;
        void * t668 = GM_LD(void *, ((((uint8_t *)&t5) + 8) + 8));
        gm_l76 = t668;
        uint8_t t669 = GM_LD(uint8_t, ((((uint8_t *)&t5) + 8) + 16));
        gm_l77 = t669;
        void * t670 = gm_l0;
        void * t671 = gm_l75;
        void * t672 = gm_l76;
        uint8_t t673 = gm_l77;
        gm_f_in_expr_6j2ajh7((void *)&(*(gm_t5o0tpv5jka2l *)gm_out), (void *)NULL, cx, NULL, t670, t671, t672, t673);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me6;
mn665:;
ma45:;
        uint8_t t675 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t675 != ((uint8_t)46ULL)) goto mn674;
        void * t676 = GM_LD(void *, (((uint8_t *)&t5) + 8));
        gm_l78 = t676;
        void * t677 = gm_l0;
        void * t678 = gm_l78;
        gm_f_block_31qi85g((void *)&(*(gm_t5o0tpv5jka2l *)gm_out), (void *)NULL, cx, NULL, t677, t678);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me6;
mn674:;
ma46:;
        uint8_t t680 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t680 != ((uint8_t)47ULL)) goto mn679;
        gm_l79 = (*(gm_trl2khmuphncd *)(((uint8_t *)&t5) + 8));
        void * t681 = gm_l0;
        void * t682 = gm_l1;
        void * t683 = (void *)((uint8_t *)&gm_l79);
        gm_f_if_expr_fn1cpni((void *)&(*(gm_t5o0tpv5jka2l *)gm_out), (void *)NULL, cx, NULL, t681, t682, t683);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me6;
mn679:;
ma47:;
        uint8_t t685 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t685 != ((uint8_t)44ULL)) goto mn684;
        void * t686 = GM_LD(void *, ((((uint8_t *)&t5) + 8) + 0));
        gm_l80 = t686;
        gm_b16_8 t687 = GM_LD(gm_b16_8, ((((uint8_t *)&t5) + 8) + 8));
        gm_l81 = t687;
        void * t688 = gm_l0;
        void * t689 = gm_l1;
        void * t690 = gm_l80;
        gm_b16_8 t691 = gm_l81;
        gm_f_match_expr_4m6qrp7((void *)&(*(gm_t5o0tpv5jka2l *)gm_out), (void *)NULL, cx, NULL, t688, t689, t690, t691);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me6;
mn684:;
ma48:;
        uint8_t t693 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t693 != ((uint8_t)41ULL)) goto mn692;
        gm_l82 = (*(gm_t3dcoc7077t1q *)(((uint8_t *)&t5) + 8));
        void * t694 = gm_l0;
        void * t695 = gm_l1;
        void * t696 = (void *)((uint8_t *)&gm_l82);
        gm_f_else_expr_lvoo6l1((void *)&(*(gm_t5o0tpv5jka2l *)gm_out), (void *)NULL, cx, NULL, t694, t695, t696);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me6;
mn692:;
ma49:;
        uint8_t t698 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t698 != ((uint8_t)42ULL)) goto mn697;
        gm_l83 = (*(gm_t083kd7b1kkag *)(((uint8_t *)&t5) + 8));
        void * t699 = gm_l0;
        void * t700 = gm_l1;
        void * t701 = GM_LD(void *, (((uint8_t *)&gm_l83) + 0));
        gm_t9805qtonbsag t702 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)&gm_l83) + 8));
        uint32_t t703 = GM_LD(uint32_t, (((uint8_t *)&gm_l83) + 16));
        gm_f_try_expr_ln1m7b8((void *)&(*(gm_t5o0tpv5jka2l *)gm_out), (void *)NULL, cx, NULL, t699, t700, t701, t702, t703);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me6;
mn697:;
ma50:;
        uint8_t t705 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t705 != ((uint8_t)48ULL)) goto mn704;
        F.l84 = (*(gm_t710lsrq1m8gk *)(((uint8_t *)&t5) + 8));
        void * t706 = gm_l0;
        void * t707 = (void *)((uint8_t *)&F.l84);
        gm_f_loop_e5nllj2((void *)&(*(gm_t5o0tpv5jka2l *)gm_out), (void *)NULL, cx, NULL, t706, t707);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me6;
mn704:;
ma51:;
        uint8_t t709 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t709 != ((uint8_t)49ULL)) goto mn708;
        gm_l85 = (*(gm_teo9d4mncjfdu *)(((uint8_t *)&t5) + 8));
        void * t710 = gm_l0;
        void * t711 = gm_l1;
        void * t712 = (void *)((uint8_t *)&gm_l85);
        gm_f_break_expr_71qc2ca((void *)&(*(gm_t5o0tpv5jka2l *)gm_out), (void *)NULL, cx, NULL, t710, t711, t712);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me6;
mn708:;
ma52:;
        uint8_t t714 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t714 != ((uint8_t)50ULL)) goto mn713;
        gm_te9hrk0u35rev t715 = GM_LD(gm_te9hrk0u35rev, (((uint8_t *)&t5) + 8));
        gm_l86 = t715;
        void * t716 = gm_l0;
        void * t717 = gm_l1;
        gm_te9hrk0u35rev t718 = gm_l86;
        gm_f_continue_expr_6bhkncl((void *)&(*(gm_t5o0tpv5jka2l *)gm_out), (void *)NULL, cx, NULL, t716, t717, t718);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me6;
mn713:;
ma53:;
        uint8_t t720 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t720 != ((uint8_t)51ULL)) goto mn719;
        uint32_t t721 = GM_LD(uint32_t, ((((uint8_t *)&t5) + 8) + 0));
        gm_l87 = t721;
        void * t722 = GM_LD(void *, ((((uint8_t *)&t5) + 8) + 8));
        gm_l88 = t722;
        void * t723 = gm_l0;
        void * t724 = gm_l1;
        uint32_t t725 = gm_l87;
        void * t726 = gm_l88;
        gm_f_return_expr_ts5pbp2((void *)&(*(gm_t5o0tpv5jka2l *)gm_out), (void *)NULL, cx, NULL, t723, t724, t725, t726);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me6;
mn719:;
ma54:;
        uint8_t t728 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t728 != ((uint8_t)58ULL)) goto mn727;
        F.l89 = (*(gm_t710lsrq1m8gk *)(((uint8_t *)&t5) + 8));
        void * t729 = gm_l0;
        void * t730 = gm_l1;
        uint32_t t731 = GM_LD(uint32_t, (((uint8_t *)&F.l89) + 0));
        gm_b16_8 t732 = GM_LD(gm_b16_8, (((uint8_t *)&F.l89) + 8));
        void * t733 = GM_LD(void *, (((uint8_t *)&F.l89) + 24));
        void * t734 = GM_LD(void *, (((uint8_t *)&F.l89) + 32));
        gm_f_with_expr_l1uk88k((void *)&(*(gm_t5o0tpv5jka2l *)gm_out), (void *)NULL, cx, NULL, t729, t730, t731, t732, t733, t734);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me6;
mn727:;
ma55:;
        uint8_t t736 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t736 != ((uint8_t)53ULL)) goto mn735;
        void * t737 = GM_LD(void *, ((((uint8_t *)&t5) + 8) + 0));
        gm_l90 = t737;
        void * t738 = GM_LD(void *, ((((uint8_t *)&t5) + 8) + 8));
        gm_l91 = t738;
        void * t739 = gm_l0;
        void * t740 = gm_l1;
        void * t741 = gm_l90;
        void * t742 = gm_l91;
        gm_f_resume__rhq91rs((void *)&(*(gm_t5o0tpv5jka2l *)gm_out), (void *)NULL, cx, NULL, t739, t740, t741, t742);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me6;
mn735:;
ma56:;
        uint8_t t744 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t744 != ((uint8_t)54ULL)) goto mn743;
        void * t745 = GM_LD(void *, (((uint8_t *)&t5) + 8));
        gm_l92 = t745;
        void * t746 = gm_l0;
        void * t747 = gm_l1;
        void * t748 = gm_l92;
        gm_f_cancel__isj44kg((void *)&(*(gm_t5o0tpv5jka2l *)gm_out), (void *)NULL, cx, NULL, t746, t747, t748);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me6;
mn743:;
ma57:;
        uint8_t t750 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t750 != ((uint8_t)52ULL)) goto mn749;
        void * t751 = GM_LD(void *, (((uint8_t *)&t5) + 8));
        gm_l93 = t751;
        void * t752 = gm_l0;
        void * t753 = gm_l1;
        void * t754 = gm_l93;
        gm_f_trap_digu1sk((void *)&(*(gm_t5o0tpv5jka2l *)gm_out), (void *)NULL, cx, NULL, t752, t753, t754);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me6;
mn749:;
ma58:;
        uint8_t t756 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t756 != ((uint8_t)56ULL)) goto mn755;
        void * t757 = gm_l1;
        gm_tl874t2atnlh3 t758 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t757) + 72));
        gm_b16_8 t759;
        GM_ST(void *, (uint8_t *)&t759, (void *)(gm_shpi1i86ollcb));
        GM_ST(uintptr_t, (uint8_t *)&t759 + 8, (uintptr_t)(58));
        gm_t08olq97rscn3 t760;
        gm_f_unsupported_531q51r((void *)&t760, (void *)NULL, cx, NULL, t758, t759);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 1;
        GM_ST(gm_t08olq97rscn3, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t760);
        goto gm_fdone;
        goto me6;
mn755:;
ma59:;
        uint8_t t762 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t762 != ((uint8_t)57ULL)) goto mn761;
        void * t763 = gm_l1;
        gm_tl874t2atnlh3 t764 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t763) + 72));
        gm_b16_8 t765;
        GM_ST(void *, (uint8_t *)&t765, (void *)(gm_sk8b751vs73ev));
        GM_ST(uintptr_t, (uint8_t *)&t765 + 8, (uintptr_t)(66));
        gm_t08olq97rscn3 t766;
        gm_f_unsupported_531q51r((void *)&t766, (void *)NULL, cx, NULL, t764, t765);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 1;
        GM_ST(gm_t08olq97rscn3, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t766);
        goto gm_fdone;
        goto me6;
mn761:;
ma60:;
        uint8_t t768 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t768 != ((uint8_t)55ULL)) goto mn767;
        void * t769 = GM_LD(void *, (((uint8_t *)&t5) + 8));
        gm_l94 = t769;
        void * t770 = gm_l0;
        void * t771 = gm_l1;
        void * t772 = gm_l94;
        gm_f_write_1uql06r((void *)&(*(gm_t5o0tpv5jka2l *)gm_out), (void *)NULL, cx, NULL, t770, t771, t772);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me6;
mn767:;
ma61:;
        uint8_t t776 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t776 != ((uint8_t)10ULL)) goto pa775;
        goto po774;
pa775:;
        uint8_t t778 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t778 != ((uint8_t)13ULL)) goto pa777;
        goto po774;
pa777:;
        uint8_t t780 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t780 != ((uint8_t)11ULL)) goto pa779;
        goto po774;
pa779:;
        uint8_t t782 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t782 != ((uint8_t)12ULL)) goto pa781;
        goto po774;
pa781:;
        uint8_t t784 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t784 != ((uint8_t)2ULL)) goto pa783;
        goto po774;
pa783:;
        uint8_t t786 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t786 != ((uint8_t)3ULL)) goto pa785;
        goto po774;
pa785:;
        uint8_t t788 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t788 != ((uint8_t)9ULL)) goto pa787;
        goto po774;
pa787:;
        goto mn773;
po774:;
        void * t789 = gm_l1;
        gm_tl874t2atnlh3 t790 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t789) + 72));
        gm_b16_8 t791;
        GM_ST(void *, (uint8_t *)&t791, (void *)(gm_s4inmcuim4088));
        GM_ST(uintptr_t, (uint8_t *)&t791 + 8, (uintptr_t)(45));
        gm_t08olq97rscn3 t792;
        gm_f_unsupported_531q51r((void *)&t792, (void *)NULL, cx, NULL, t790, t791);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 1;
        GM_ST(gm_t08olq97rscn3, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t792);
        goto gm_fdone;
        goto me6;
mn773:;
ma62:;
        gm_trap("no match arm matched the scrutinee", gm_src0gtuh94b1fce, 304, 5);
me6:;
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
inline __attribute__((aligned(4))) void gm_f_expr_porbc9t(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_expr_porbc9t F;
    void * gm_l0; /* fg */
    void * gm_l1; /* e */
    gm_b16_8 gm_l2; /* acc */
    GM_STACK_CHECK(&F, 169u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        void * t4 = gm_l1;
        gm_th2lqbo77pvsc t5;
        gm_f_chain_operand_m986l4t((void *)&t5, t3, t4);
        if (!((((uint8_t *)&t5))[16] != 0)) goto ie2;
        gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)&t5));
        gm_l2 = t6;
        {
            gm_b16_8 t8 = gm_l2;
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
            ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t8);
            goto gm_fdone;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t10 = gm_l0;
        void * t11 = gm_l1;
        uint64_t t12 = gm_f_chain_links_ma0qg8l((void *)NULL, cx, NULL, t10, t11);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint64_t t13 = GM_LD(uint64_t, gm_cjs2aavhl2o);
        uint8_t t14 = (t12 > t13);
        if (!t14) goto ie9;
        {
            void * t16 = gm_l0;
            void * t17 = gm_l1;
            gm_f_emit_chain_iu95l01((void *)&(*(gm_t5o0tpv5jka2l *)gm_out), (void *)NULL, cx, NULL, t16, t17);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto ix9;
ie9:;
ix9:;
    }
    {
        void * t18 = gm_l0;
        void * t19 = gm_l1;
        gm_f_expr_node_sj12d0p((void *)&(*(gm_t5o0tpv5jka2l *)gm_out), (void *)NULL, cx, NULL, t18, t19);
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
extern __attribute__((aligned(4))) uint8_t gm_f_find_with_gcs5p4r(void * a0, uint32_t a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_float_lit_26lt2tp(void *env, gm_node *cx, gm_node *inst, uint16_t a0, double a1) {
    struct gm_fr_float_lit_26lt2tp F;
    uint16_t gm_l0; /* bits */
    double gm_l1; /* x */
    gm_b16_8 gm_l2; /* c */
    GM_STACK_CHECK(&F, 534u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint16_t t2 = gm_l0;
        gm_b16_8 t3;
        uint16_t t6 = GM_LD(uint16_t, ((uint8_t *)&t2));
        if (!(t6 == ((uint16_t)16ULL))) goto mn5;
        gm_b16_8 t7;
        GM_ST(void *, (uint8_t *)&t7, (void *)(gm_scj0sm2kpivk4));
        GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(8));
        t3 = t7;
        goto me4;
mn5:;
        uint16_t t9 = GM_LD(uint16_t, ((uint8_t *)&t2));
        if (!(t9 == ((uint16_t)32ULL))) goto mn8;
        gm_b16_8 t10;
        GM_ST(void *, (uint8_t *)&t10, (void *)(gm_s4cess8gum6i7));
        GM_ST(uintptr_t, (uint8_t *)&t10 + 8, (uintptr_t)(5));
        t3 = t10;
        goto me4;
mn8:;
        gm_b16_8 t12;
        GM_ST(void *, (uint8_t *)&t12, (void *)(gm_s866ofni9sb8u));
        GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)(6));
        t3 = t12;
        goto me4;
mn11:;
        gm_trap("no match arm matched the scrutinee", gm_src0gtuh94b1fce, 1256, 10);
me4:;
        gm_l2 = t3;
    }
    {
        double t14 = gm_l1;
        uint8_t t15 = (uint8_t)__builtin_isnan(t14);
        if (!t15) goto ie13;
        {
            gm_b16_8 t17 = gm_l2;
            gm_tnpifv20irnph t18;
            GM_ST(gm_b16_8, ((uint8_t *)&t18 + 0), t17);
            gm_tabb710d9mq2e t19;
            gm_f_format_8b7o2bp((void *)&t19, (void *)NULL, cx, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t19))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t19) + 8, gm_src0gtuh94b1fce, 1261, 28);
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&t19) + 8));
            gm_b16_8 t21 = GM_LD(gm_b16_8, ((uint8_t *)&t20));
            uint8_t t22 = 0;
            F.ret = t21;
            goto gm_fret;
        }
        goto ix13;
ie13:;
ix13:;
    }
    {
        double t24 = gm_l1;
        uint8_t t25 = (uint8_t)__builtin_isinf(t24);
        if (!t25) goto ie23;
        {
            gm_t6j6smj7qoctu t27;
            gm_b16_8 t28 = gm_l2;
            GM_ST(gm_b16_8, (uint8_t *)&t27 + 0, t28);
            gm_b16_8 t29;
            double t31 = gm_l1;
            uint8_t t32 = (t31 < ((double)0e0));
            if (!t32) goto ie30;
            gm_b16_8 t34;
            GM_ST(void *, (uint8_t *)&t34, (void *)(gm_skolmnfc0ccbd));
            GM_ST(uintptr_t, (uint8_t *)&t34 + 8, (uintptr_t)(1));
            gm_b16_8 t35 = t34;
            t29 = t35;
            goto ix30;
ie30:;
            gm_b16_8 t37;
            GM_ST(void *, (uint8_t *)&t37, (void *)(gm_sr4qbrk6tlgaa));
            GM_ST(uintptr_t, (uint8_t *)&t37 + 8, (uintptr_t)(0));
            gm_b16_8 t38 = t37;
            t29 = t38;
ix30:;
            GM_ST(gm_b16_8, (uint8_t *)&t27 + 16, t29);
            gm_tabb710d9mq2e t39;
            gm_f_format_p7tc45u((void *)&t39, (void *)NULL, cx, NULL, &t27);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t39))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t39) + 8, gm_src0gtuh94b1fce, 1262, 28);
            gm_b16_8 t40 = GM_LD(gm_b16_8, (((uint8_t *)&t39) + 8));
            gm_b16_8 t41 = GM_LD(gm_b16_8, ((uint8_t *)&t40));
            uint8_t t42 = 0;
            F.ret = t41;
            goto gm_fret;
        }
        goto ix23;
ie23:;
ix23:;
    }
    {
        gm_t2mvr8fpgjkg6 t43;
        gm_b16_8 t44 = gm_l2;
        GM_ST(gm_b16_8, (uint8_t *)&t43 + 0, t44);
        double t45 = gm_l1;
        GM_ST(double, (uint8_t *)&t43 + 16, t45);
        gm_tabb710d9mq2e t46;
        gm_f_format_vjen3ng((void *)&t46, (void *)NULL, cx, NULL, &t43);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t46))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t46) + 8, gm_src0gtuh94b1fce, 1264, 12);
        gm_b16_8 t47 = GM_LD(gm_b16_8, (((uint8_t *)&t46) + 8));
        gm_b16_8 t48 = GM_LD(gm_b16_8, ((uint8_t *)&t47));
        uint8_t t49 = 0;
        F.ret = t48;
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
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_dfds699(void *env, gm_node *cx, gm_node *inst, void * a0);
extern __attribute__((aligned(4))) uint8_t gm_f_fn_gdv4o16(void *env, gm_node *cx, gm_node *inst, void * a0);
extern __attribute__((aligned(4))) uint8_t gm_f_fn_okfag60(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_if_expr_fn1cpni(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2) {
    struct gm_fr_if_expr_fn1cpni F;
    void * gm_l0; /* fg */
    void * gm_l1; /* e */
    void * gm_l2; /* x */
    gm_t88rcucg1b7uk gm_l3; /* t */
    gm_th2lqbo77pvsc gm_l4; /* res */
    uint32_t gm_l5; /* n */
    gm_b16_8 gm_l6; /* els */
    gm_b16_8 gm_l7; /* end */
    gm_b16_8 gm_l8; /* v */
    gm_b16_8 gm_l9; /* r */
    void * gm_l10; /* xe */
    gm_b16_8 gm_l11; /* ev */
    gm_b16_8 gm_l12; /* r */
    gm_b16_8 gm_l13; /* r */
    GM_STACK_CHECK(&F, 1248u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l1;
        gm_t88rcucg1b7uk t3 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t2) + 0));
        gm_l3 = t3;
    }
    {
        gm_th2lqbo77pvsc t4;
        memset(&t4, 0, sizeof t4);
        gm_l4 = t4;
    }
    {
        void * t6 = gm_l0;
        void * t7 = GM_LD(void *, (((uint8_t *)t6) + 0));
        void * t8 = (void *)(((uint8_t *)t7) + 8);
        gm_t88rcucg1b7uk t9 = gm_l3;
        uint8_t t10 = gm_f_is_never_in5ftma(t8, GM_CONV(gm_csguankko4lmhd, t9));
        uint8_t t11 = (uint8_t)!t10;
        if (!t11) goto ie5;
        void * t12 = gm_l0;
        void * t13 = GM_LD(void *, (((uint8_t *)t12) + 0));
        void * t14 = (void *)(((uint8_t *)t13) + 8);
        gm_t88rcucg1b7uk t15 = gm_l3;
        uint8_t t16 = gm_f_is_unit_17uennd(t14, GM_CONV(gm_csguankko4lmhd, t15));
        uint8_t t17 = (uint8_t)!t16;
        if (!t17) goto ie5;
        {
            void * t19 = gm_l0;
            gm_t88rcucg1b7uk t20 = gm_l3;
            gm_b16_8 t21 = gm_f_tmp_uninit_jr39s5u((void *)NULL, cx, NULL, t19, t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l4)) + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l4)) + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l4)) + 20, 0);
            ((uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l4)))[16] = 1;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l4)) + 0, t21);
        }
        goto ix5;
ie5:;
ix5:;
    }
    {
        void * t22 = gm_l0;
        uint32_t t23 = gm_f_fresh_5kokjmg(t22);
        gm_l5 = t23;
    }
    {
        uint32_t t24 = gm_l5;
        gm_t88rcucg1b7uk t25;
        GM_ST(uint32_t, ((uint8_t *)&t25 + 0), t24);
        gm_tabb710d9mq2e t26;
        gm_f_format_40ni57j((void *)&t26, (void *)NULL, cx, NULL, t25);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t26))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t26) + 8, gm_src0gtuh94b1fce, 611, 12);
        gm_b16_8 t27 = GM_LD(gm_b16_8, (((uint8_t *)&t26) + 8));
        gm_l6 = t27;
    }
    {
        uint32_t t28 = gm_l5;
        gm_t88rcucg1b7uk t29;
        GM_ST(uint32_t, ((uint8_t *)&t29 + 0), t28);
        gm_tabb710d9mq2e t30;
        gm_f_format_fk1icrl((void *)&t30, (void *)NULL, cx, NULL, t29);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t30))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t30) + 8, gm_src0gtuh94b1fce, 612, 12);
        gm_b16_8 t31 = GM_LD(gm_b16_8, (((uint8_t *)&t30) + 8));
        gm_l7 = t31;
    }
    {
        void * t32 = gm_l0;
        void * t33 = gm_l2;
        gm_b16_8 t34 = GM_LD(gm_b16_8, (((uint8_t *)t33) + 0));
        gm_b16_8 t35 = gm_l6;
        gm_b16_8 t36 = GM_LD(gm_b16_8, ((uint8_t *)&t35));
        gm_tqgtd5gtgj65s t37;
        gm_f_conds_79r4f4d((void *)&t37, (void *)NULL, cx, NULL, t32, t34, t36);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t37))[0] != 0)) {
            gm_t5o0tpv5jka2l t38;
            GM_ST(uint8_t, (uint8_t *)&t38 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t38 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t38 + 4, 0);
            ((uint8_t *)&t38)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t38 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t37) + 8)));
            F.ret = t38;
            goto gm_fret;
        }
        gm_b0_1 t39 = GM_LD(gm_b0_1, (((uint8_t *)&t37) + 8));
    }
    {
        void * t40 = gm_l0;
        void * t41 = gm_l2;
        void * t42 = GM_LD(void *, (((uint8_t *)t41) + 16));
        gm_t5o0tpv5jka2l t43;
        gm_f_expr_porbc9t((void *)&t43, (void *)NULL, cx, NULL, t40, t42);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t43))[0] != 0)) {
            gm_t5o0tpv5jka2l t44;
            GM_ST(uint8_t, (uint8_t *)&t44 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t44 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t44 + 4, 0);
            ((uint8_t *)&t44)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t44 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t43) + 8)));
            F.ret = t44;
            goto gm_fret;
        }
        gm_b16_8 t45 = GM_LD(gm_b16_8, (((uint8_t *)&t43) + 8));
        gm_l8 = t45;
    }
    {
        if (!((((uint8_t *)&gm_l4))[16] != 0)) goto ie46;
        gm_b16_8 t47 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l4));
        gm_l9 = t47;
        {
            void * t50 = gm_l0;
            void * t51 = GM_LD(void *, (((uint8_t *)t50) + 0));
            void * t52 = (void *)(((uint8_t *)t51) + 8);
            void * t53 = gm_l2;
            void * t54 = GM_LD(void *, (((uint8_t *)t53) + 16));
            gm_t88rcucg1b7uk t55 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t54) + 0));
            uint8_t t56 = gm_f_is_never_in5ftma(t52, GM_CONV(gm_csguankko4lmhd, t55));
            uint8_t t57 = (uint8_t)!t56;
            if (!t57) goto ie49;
            {
                void * t59 = gm_l0;
                gm_t6j6smj7qoctu t60;
                gm_b16_8 t61 = gm_l9;
                GM_ST(gm_b16_8, (uint8_t *)&t60 + 0, t61);
                gm_b16_8 t62 = gm_l8;
                GM_ST(gm_b16_8, (uint8_t *)&t60 + 16, t62);
                gm_b0_1 t63 = gm_f_linef_uu8i6ue((void *)NULL, cx, NULL, t59, &t60);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix49;
ie49:;
ix49:;
        }
        goto ix46;
ie46:;
ix46:;
    }
    {
        void * t64 = gm_l0;
        gm_b16_8 t65 = gm_l7;
        gm_tnpifv20irnph t66;
        GM_ST(gm_b16_8, ((uint8_t *)&t66 + 0), t65);
        gm_b0_1 t67 = gm_f_linef_h995d88((void *)NULL, cx, NULL, t64, t66);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t68 = gm_l0;
        gm_b16_8 t69 = gm_l6;
        gm_b16_8 t70 = GM_LD(gm_b16_8, ((uint8_t *)&t69));
        gm_b0_1 t71 = gm_f_label_710oev4((void *)NULL, cx, NULL, t68, t70);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t73 = gm_l2;
        void * t74 = GM_LD(void *, (((uint8_t *)t73) + 24));
        if (!(GM_LD(void *, ((uint8_t *)&t74)) != 0)) goto ie72;
        void * t75 = GM_LD(void *, ((uint8_t *)&t74));
        gm_l10 = t75;
        {
            void * t77 = gm_l0;
            void * t78 = gm_l10;
            gm_t5o0tpv5jka2l t79;
            gm_f_expr_porbc9t((void *)&t79, (void *)NULL, cx, NULL, t77, t78);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t79))[0] != 0)) {
                gm_t5o0tpv5jka2l t80;
                GM_ST(uint8_t, (uint8_t *)&t80 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t80 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t80 + 4, 0);
                ((uint8_t *)&t80)[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t80 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t79) + 8)));
                F.ret = t80;
                goto gm_fret;
            }
            gm_b16_8 t81 = GM_LD(gm_b16_8, (((uint8_t *)&t79) + 8));
            gm_l11 = t81;
        }
        {
            if (!((((uint8_t *)&gm_l4))[16] != 0)) goto ie82;
            gm_b16_8 t83 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l4));
            gm_l12 = t83;
            {
                void * t86 = gm_l0;
                void * t87 = GM_LD(void *, (((uint8_t *)t86) + 0));
                void * t88 = (void *)(((uint8_t *)t87) + 8);
                void * t89 = gm_l10;
                gm_t88rcucg1b7uk t90 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t89) + 0));
                uint8_t t91 = gm_f_is_never_in5ftma(t88, GM_CONV(gm_csguankko4lmhd, t90));
                uint8_t t92 = (uint8_t)!t91;
                if (!t92) goto ie85;
                {
                    void * t94 = gm_l0;
                    gm_t6j6smj7qoctu t95;
                    gm_b16_8 t96 = gm_l12;
                    GM_ST(gm_b16_8, (uint8_t *)&t95 + 0, t96);
                    gm_b16_8 t97 = gm_l11;
                    GM_ST(gm_b16_8, (uint8_t *)&t95 + 16, t97);
                    gm_b0_1 t98 = gm_f_linef_uu8i6ue((void *)NULL, cx, NULL, t94, &t95);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                goto ix85;
ie85:;
ix85:;
            }
            goto ix82;
ie82:;
ix82:;
        }
        goto ix72;
ie72:;
ix72:;
    }
    {
        void * t99 = gm_l0;
        gm_b16_8 t100 = gm_l7;
        gm_b16_8 t101 = GM_LD(gm_b16_8, ((uint8_t *)&t100));
        gm_b0_1 t102 = gm_f_label_710oev4((void *)NULL, cx, NULL, t99, t101);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        if (!((((uint8_t *)&gm_l4))[16] != 0)) goto ie103;
        gm_b16_8 t104 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l4));
        gm_l13 = t104;
        {
            gm_b16_8 t106 = gm_l13;
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
            ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t106);
            goto gm_fdone;
        }
        goto ix103;
ie103:;
ix103:;
    }
    {
        void * t107 = gm_l0;
        gm_t88rcucg1b7uk t108 = gm_l3;
        gm_b16_8 t109 = gm_f_zero_tlk13aa((void *)NULL, cx, NULL, t107, t108);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t109);
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
extern __attribute__((aligned(4))) gm_b0_1 gm_f_index_check_m2p0u1k(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_b16_8 a2, gm_b16_8 a3, gm_b16_8 a4);
__attribute__((aligned(4))) gm_b16_8 gm_f_int_lit_ai37456(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, const void * a2) {
    struct gm_fr_int_lit_ai37456 F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* t */
    uint16_t gm_l3; /* f */
    uint8_t gm_l4; /* signed */
    uint64_t gm_l5; /* storage */
    gm_b16_8 gm_l6; /* c */
    unsigned __int128 gm_l7; /* lo */
    unsigned __int128 gm_l8; /* hi */
    __int128 gm_l9; /* x */
    unsigned __int128 gm_l10; /* lo */
    GM_STACK_CHECK(&F, 1242u);
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
        gm_t5v8n6qrn4rh4 t6;
        gm_f_repr_53quqki((void *)&t6, (void *)NULL, cx, NULL, t4, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        switch (t14) { case ((uint8_t)0ULL): goto ma9; case ((uint8_t)1ULL): goto ma8; case ((uint8_t)2ULL): goto ma10; case ((uint8_t)3ULL): goto ma11; case ((uint8_t)4ULL): goto ma12; case ((uint8_t)5ULL): goto ma12; case ((uint8_t)6ULL): goto ma12; default: goto ma12; }
ma8:;
        uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t16 != ((uint8_t)1ULL)) goto mn15;
        uint16_t t17 = GM_LD(uint16_t, (((uint8_t *)&t6) + 8));
        gm_l3 = t17;
        uint16_t t18 = gm_l3;
        gm_tngtuiuskn938 t19;
        gm_f_from_int_ofjuoqh((void *)&t19, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)a2)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        double t20 = gm_f_to_f64_4avhrht((void *)NULL, cx, NULL, &t19);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t21 = gm_f_float_lit_26lt2tp((void *)NULL, cx, NULL, t18, t20);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t22 = 0;
        F.ret = t21;
        goto gm_fret;
        goto me7;
mn15:;
ma9:;
        uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t24 != ((uint8_t)0ULL)) goto mn23;
        uint8_t t25 = GM_LD(uint8_t, ((((uint8_t *)&t6) + 8) + 0));
        gm_l4 = t25;
        uint64_t t26 = GM_LD(uint64_t, ((((uint8_t *)&t6) + 8) + 8));
        gm_l5 = t26;
        {
            uint8_t t28 = gm_l4;
            uint64_t t29 = gm_l5;
            gm_b16_8 t30 = gm_f_int_cty_s62af0i(t28, t29);
            gm_l6 = t30;
        }
        {
            gm_t793vlgpa3b88 t31;
            gm_f_split_u128_vrmr6en((void *)&t31, &(*(gm_tbci5ospk5nnn *)((uint8_t *)a2)));
            gm_t793vlgpa3b88 t32 = t31;
            gm_l7 = GM_LD(unsigned __int128, (uint8_t *)&t32 + 0);
            gm_l8 = GM_LD(unsigned __int128, (uint8_t *)&t32 + 16);
        }
        {
            uint64_t t34 = gm_l5;
            uint8_t t35 = ((t34 == ((uint64_t)16ULL)));
            if (!t35) goto ie33;
            {
                gm_tgbgti82mf879 t37;
                gm_b16_8 t38 = gm_l6;
                GM_ST(gm_b16_8, (uint8_t *)&t37 + 0, t38);
                unsigned __int128 t39 = gm_l8;
                uint64_t t40 = (uint64_t)t39;
                GM_ST(uint64_t, (uint8_t *)&t37 + 16, t40);
                unsigned __int128 t41 = gm_l7;
                uint64_t t42 = (uint64_t)t41;
                GM_ST(uint64_t, (uint8_t *)&t37 + 24, t42);
                gm_tabb710d9mq2e t43;
                gm_f_format_dp1qh3k((void *)&t43, (void *)NULL, cx, NULL, &t37);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t43))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t43) + 8, gm_src0gtuh94b1fce, 262, 24);
                gm_b16_8 t44 = GM_LD(gm_b16_8, (((uint8_t *)&t43) + 8));
                gm_b16_8 t45 = GM_LD(gm_b16_8, ((uint8_t *)&t44));
                uint8_t t46 = 0;
                F.ret = t45;
                goto gm_fret;
            }
            goto ix33;
ie33:;
            uint8_t t48 = gm_l4;
            if (!t48) goto ie47;
            {
                gm_ta24rd13gupia t50;
                gm_f_to_i128_o7ud1ue((void *)&t50, &(*(gm_tbci5ospk5nnn *)((uint8_t *)a2)));
                __int128 t51;
                if (((((uint8_t *)&t50))[16] != 0)) {
                    __int128 t52 = GM_LD(__int128, (((uint8_t *)&t50) + 0));
                    t51 = t52;
                } else {
                    t51 = ((__int128)(((unsigned __int128)0u << 64) | 0u));
                }
                gm_l9 = t51;
            }
            {
                __int128 t54 = gm_l9;
                uint8_t t55 = ((t54 == ((__int128)(((unsigned __int128)18446744073709551615u << 64) | 9223372036854775808u))));
                if (!t55) goto ie53;
                {
                    gm_b16_8 t57 = gm_l6;
                    gm_tnpifv20irnph t58;
                    GM_ST(gm_b16_8, ((uint8_t *)&t58 + 0), t57);
                    gm_tabb710d9mq2e t59;
                    gm_f_format_ij8u0vj((void *)&t59, (void *)NULL, cx, NULL, t58);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t59))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t59) + 8, gm_src0gtuh94b1fce, 265, 55);
                    gm_b16_8 t60 = GM_LD(gm_b16_8, (((uint8_t *)&t59) + 8));
                    gm_b16_8 t61 = GM_LD(gm_b16_8, ((uint8_t *)&t60));
                    uint8_t t62 = 0;
                    F.ret = t61;
                    goto gm_fret;
                }
                goto ix53;
ie53:;
ix53:;
            }
            {
                gm_tctdkl637nfjf t63;
                gm_b16_8 t64 = gm_l6;
                GM_ST(gm_b16_8, (uint8_t *)&t63 + 0, t64);
                __int128 t65 = gm_l9;
                GM_ST(__int128, (uint8_t *)&t63 + 16, t65);
                gm_tabb710d9mq2e t66;
                gm_f_format_jb7bqji((void *)&t66, (void *)NULL, cx, NULL, &t63);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t66))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t66) + 8, gm_src0gtuh94b1fce, 266, 24);
                gm_b16_8 t67 = GM_LD(gm_b16_8, (((uint8_t *)&t66) + 8));
                gm_b16_8 t68 = GM_LD(gm_b16_8, ((uint8_t *)&t67));
                uint8_t t69 = 0;
                F.ret = t68;
                goto gm_fret;
            }
            goto ix47;
ie47:;
            {
                gm_t3877pk7lv748 t71;
                gm_b16_8 t72 = gm_l6;
                GM_ST(gm_b16_8, (uint8_t *)&t71 + 0, t72);
                unsigned __int128 t73 = gm_l7;
                uint64_t t74 = (uint64_t)t73;
                GM_ST(uint64_t, (uint8_t *)&t71 + 16, t74);
                gm_tabb710d9mq2e t75;
                gm_f_format_tt4n370((void *)&t75, (void *)NULL, cx, NULL, &t71);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t75))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t75) + 8, gm_src0gtuh94b1fce, 268, 24);
                gm_b16_8 t76 = GM_LD(gm_b16_8, (((uint8_t *)&t75) + 8));
                gm_b16_8 t77 = GM_LD(gm_b16_8, ((uint8_t *)&t76));
                uint8_t t78 = 0;
                F.ret = t77;
                goto gm_fret;
            }
ix47:;
ix33:;
        }
        goto me7;
mn23:;
ma10:;
        uint8_t t80 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t80 != ((uint8_t)2ULL)) goto mn79;
        gm_b16_8 t81;
        uint8_t t83 = gm_f_is_zero_8tcct9o(&(*(gm_tbci5ospk5nnn *)((uint8_t *)a2)));
        if (!t83) goto ie82;
        gm_b16_8 t85;
        GM_ST(void *, (uint8_t *)&t85, (void *)(gm_sicgmpca63jln));
        GM_ST(uintptr_t, (uint8_t *)&t85 + 8, (uintptr_t)(1));
        gm_b16_8 t86 = t85;
        t81 = t86;
        goto ix82;
ie82:;
        gm_b16_8 t88;
        GM_ST(void *, (uint8_t *)&t88, (void *)(gm_svubtddeidu1d));
        GM_ST(uintptr_t, (uint8_t *)&t88 + 8, (uintptr_t)(1));
        gm_b16_8 t89 = t88;
        t81 = t89;
ix82:;
        uint8_t t90 = 0;
        F.ret = t81;
        goto gm_fret;
        goto me7;
mn79:;
ma11:;
        uint8_t t92 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t92 != ((uint8_t)3ULL)) goto mn91;
        {
            gm_t793vlgpa3b88 t94;
            gm_f_split_u128_vrmr6en((void *)&t94, &(*(gm_tbci5ospk5nnn *)((uint8_t *)a2)));
            gm_t793vlgpa3b88 t95 = t94;
            gm_l10 = GM_LD(unsigned __int128, (uint8_t *)&t95 + 0);
        }
        {
            unsigned __int128 t96 = gm_l10;
            uint64_t t97 = (uint64_t)t96;
            gm_t4ce0jd3tdro4 t98;
            GM_ST(uint64_t, ((uint8_t *)&t98 + 0), t97);
            gm_tabb710d9mq2e t99;
            gm_f_format_p7hqke6((void *)&t99, (void *)NULL, cx, NULL, t98);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t99))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t99) + 8, gm_src0gtuh94b1fce, 274, 20);
            gm_b16_8 t100 = GM_LD(gm_b16_8, (((uint8_t *)&t99) + 8));
            gm_b16_8 t101 = GM_LD(gm_b16_8, ((uint8_t *)&t100));
            uint8_t t102 = 0;
            F.ret = t101;
            goto gm_fret;
        }
        goto me7;
mn91:;
ma12:;
        gm_b16_8 t104;
        GM_ST(void *, (uint8_t *)&t104, (void *)(gm_sicgmpca63jln));
        GM_ST(uintptr_t, (uint8_t *)&t104 + 8, (uintptr_t)(1));
        uint8_t t105 = 0;
        F.ret = t104;
        goto gm_fret;
        goto me7;
mn103:;
ma13:;
        gm_trap("no match arm matched the scrutinee", gm_src0gtuh94b1fce, 256, 5);
me7:;
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_temp_rrllgdr(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_temp_rrllgdr F;
    gm_b16_8 gm_l0; /* v */
    gm_b16_8 gm_l1; /* <tmp> */
    uint64_t gm_l2; /* <tmp> */
    uint8_t gm_l3; /* b */
    GM_STACK_CHECK_SHALLOW(&F, 178u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = 0;
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint8_t t5 = (t4 < ((uint64_t)2ULL));
        if (!t5) {
            uint64_t t6 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            if (t6 >= (uintptr_t)(t7)) gm_trap_index(t6, t7, gm_src0gtuh94b1fce, 27, 21);
            uint8_t t8 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t6 * 1));
            uint8_t t9 = (!(t8 == ((uint8_t)116ULL)));
            t3 = t9;
        } else t3 = 1;
        if (!t3) goto ie2;
        {
            uint8_t t11 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        {
            uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            void * t14 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
            uint64_t t15 = (uintptr_t)((uint64_t)1ULL);
            if (t15 > (uintptr_t)t13 || (uintptr_t)t13 > (uintptr_t)t13) gm_trap_slice(t15, (uintptr_t)t13, t13, gm_src0gtuh94b1fce, 28, 14);
            gm_b16_8 t16;
            GM_ST(void *, (uint8_t *)&t16, (void *)(((uint8_t *)t14 + t15 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t16 + 8, (uintptr_t)((uintptr_t)t13 - t15));
            gm_l1 = t16;
        }
        {
            gm_l2 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t18 = gm_l2;
                uint64_t t19 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                uint8_t t20 = (t18 < t19);
                if (!t20) goto lb17;
                {
                    uint64_t t23 = gm_l2;
                    uint64_t t24 = (uintptr_t)t23;
                    uint64_t t25 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                    uint8_t t26 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t24 * 1));
                    uint8_t t29 = GM_LD(uint8_t, ((uint8_t *)&t26));
                    gm_l3 = t29;
                    {
                        uint8_t t32 = 0;
                        uint8_t t33 = gm_l3;
                        uint8_t t34 = (t33 < ((uint8_t)48ULL));
                        if (!t34) {
                            uint8_t t35 = gm_l3;
                            uint8_t t36 = (t35 > ((uint8_t)57ULL));
                            t32 = t36;
                        } else t32 = 1;
                        if (!t32) goto ie31;
                        {
                            uint8_t t38 = 0;
                            F.ret = ((uint8_t)0);
                            goto gm_fret;
                        }
                        goto ix31;
ie31:;
ix31:;
                    }
                    goto me27;
mn28:;
                    gm_trap("no match arm matched the scrutinee", gm_src0gtuh94b1fce, 28, 5);
me27:;
                }
lc17:;
                {
                    uint64_t t40 = gm_l2;
                    uint64_t r41; bool o42 = __builtin_add_overflow(t40, ((uint64_t)1ULL), &r41);
                    uint64_t t43 = r41;
                    gm_l2 = t43;
                }
            }
lb17:;
        }
    }
    {
        uint8_t t44 = 0;
        F.ret = ((uint8_t)1);
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_with_label_rskslsd(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint32_t a2) {
    struct gm_fr_is_with_label_rskslsd F;
    void * gm_l0; /* fg */
    uint32_t gm_l1; /* levels */
    uint32_t gm_l2; /* label */
    uint32_t gm_l3; /* f */
    void * gm_l4; /* b */
    GM_STACK_CHECK(&F, 157u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        uint32_t t3 = gm_l1;
        gm_tommf4j9upomc t4;
        gm_f_frame_up_g8486eu((void *)&t4, (void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tommf4j9upomc t5 = t4;
        gm_l3 = GM_LD(uint32_t, (uint8_t *)&t5 + 16);
    }
    {
        void * t7 = gm_l0;
        void * t8 = GM_LD(void *, (((uint8_t *)t7) + 0));
        void * t9 = GM_LD(void *, (((uint8_t *)t8) + 0));
        uint32_t t10 = gm_l3;
        void * t11 = gm_f_func_in17r5n((void *)NULL, cx, NULL, t9, t10);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t12 = GM_LD(void *, (((uint8_t *)t11) + 152));
        if (!(GM_LD(void *, ((uint8_t *)&t12)) != 0)) goto ie6;
        void * t13 = GM_LD(void *, ((uint8_t *)&t12));
        gm_l4 = t13;
        {
            void * t15 = gm_l4;
            uint32_t t16 = gm_l2;
            uint8_t t17 = gm_f_find_with_gcs5p4r(t15, t16);
            uint8_t t18 = 0;
            F.ret = t17;
            goto gm_fret;
        }
        goto ix6;
ie6:;
ix6:;
    }
    {
        uint8_t t19 = 0;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_load_0hebde7(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2) {
    struct gm_fr_load_0hebde7 F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* a */
    gm_b16_8 gm_l3; /* c */
    gm_t3877pk7lv748 gm_l4; /* p */
    gm_b16_8 gm_l5; /* ld */
    gm_tuglhjjmj2g58 gm_l6; /* ii */
    gm_b16_8 gm_l7; /* v */
    GM_STACK_CHECK(&F, 785u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_t88rcucg1b7uk t5 = gm_l1;
        gm_b16_8 t6 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t4, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l3 = t6;
    }
    {
        void * t8 = gm_l0;
        void * t9 = GM_LD(void *, (((uint8_t *)t8) + 0));
        void * t10 = (void *)(((uint8_t *)t9) + 8);
        gm_t88rcucg1b7uk t11 = gm_l1;
        uint8_t t12 = gm_f_by_ref_fi0o860((void *)NULL, cx, NULL, t10, t11);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t12) goto ie7;
        {
            gm_b16_8 t15 = gm_l2;
            gm_t9geo0l9gngfn t16;
            gm_f_temp_piece_572g74q((void *)&t16, t15);
            if (!((((uint8_t *)&t16))[24] != 0)) goto ie14;
            gm_l4 = (*(gm_t3877pk7lv748 *)(((uint8_t *)&t16) + 0));
            {
                uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 16));
                void * t20 = gm_l0;
                void * t21 = GM_LD(void *, (((uint8_t *)t20) + 0));
                void * t22 = (void *)(((uint8_t *)t21) + 8);
                gm_t88rcucg1b7uk t23 = gm_l1;
                uint64_t t24 = gm_f_align_uu2harl((void *)NULL, cx, NULL, t22, t23);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (t24 == 0) gm_trap("division by zero", gm_src0gtuh94b1fce, 94, 16);
                uint64_t t25 = (uint64_t)(t19 % t24);
                uint8_t t26 = ((t25 == ((uint64_t)0ULL)));
                if (!t26) goto ie18;
                {
                    gm_tftksr4op114e t28;
                    gm_b16_8 t29 = gm_l3;
                    GM_ST(gm_b16_8, (uint8_t *)&t28 + 0, t29);
                    gm_b16_8 t30 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 0));
                    GM_ST(gm_b16_8, (uint8_t *)&t28 + 16, t30);
                    uint64_t t31 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 16));
                    GM_ST(uint64_t, (uint8_t *)&t28 + 32, t31);
                    gm_tabb710d9mq2e t32;
                    gm_f_format_ccpi8au((void *)&t32, (void *)NULL, cx, NULL, &t28);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t32))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t32) + 8, gm_src0gtuh94b1fce, 94, 55);
                    gm_b16_8 t33 = GM_LD(gm_b16_8, (((uint8_t *)&t32) + 8));
                    gm_b16_8 t34 = GM_LD(gm_b16_8, ((uint8_t *)&t33));
                    uint8_t t35 = 0;
                    F.ret = t34;
                    goto gm_fret;
                }
                goto ix18;
ie18:;
ix18:;
            }
            goto ix14;
ie14:;
ix14:;
        }
        goto ix7;
ie7:;
ix7:;
    }
    {
        gm_t6j6smj7qoctu t36;
        gm_b16_8 t37 = gm_l3;
        GM_ST(gm_b16_8, (uint8_t *)&t36 + 0, t37);
        gm_b16_8 t38 = gm_l2;
        GM_ST(gm_b16_8, (uint8_t *)&t36 + 16, t38);
        gm_tabb710d9mq2e t39;
        gm_f_format_voj344o((void *)&t39, (void *)NULL, cx, NULL, &t36);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t39))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t39) + 8, gm_src0gtuh94b1fce, 97, 11);
        gm_b16_8 t40 = GM_LD(gm_b16_8, (((uint8_t *)&t39) + 8));
        gm_l5 = t40;
    }
    {
        void * t42 = gm_l0;
        gm_t88rcucg1b7uk t43 = gm_l1;
        gm_tqcc6dmlne11b t44;
        gm_f_int_info_2tcdf1n((void *)&t44, (void *)NULL, cx, NULL, t42, t43);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!((((uint8_t *)&t44))[16] != 0)) goto ie41;
        gm_tuglhjjmj2g58 t45 = GM_LD(gm_tuglhjjmj2g58, ((uint8_t *)&t44));
        gm_l6 = t45;
        {
            gm_tuglhjjmj2g58 t47 = gm_l6;
            gm_b16_8 t48 = gm_l5;
            gm_b16_8 t49 = GM_LD(gm_b16_8, ((uint8_t *)&t48));
            gm_b16_8 t50 = gm_f_normalize_jch381e((void *)NULL, cx, NULL, t47, t49);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l7 = t50;
        }
        {
            void * t51 = gm_l0;
            gm_t88rcucg1b7uk t52 = gm_l1;
            gm_b16_8 t53 = gm_l7;
            gm_b16_8 t54 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t51, t52, t53);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t55 = 0;
            F.ret = t54;
            goto gm_fret;
        }
        goto ix41;
ie41:;
ix41:;
    }
    {
        void * t56 = gm_l0;
        gm_t88rcucg1b7uk t57 = gm_l1;
        gm_b16_8 t58 = gm_l5;
        gm_b16_8 t59 = GM_LD(gm_b16_8, ((uint8_t *)&t58));
        gm_b16_8 t60 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t56, t57, t59);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t61 = 0;
        F.ret = t60;
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
extern __attribute__((aligned(4))) gm_b16_8 gm_f_loc_u9c0aqb(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) void gm_f_logic_aafpj13(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, void * a2, void * a3) {
    struct gm_fr_logic_aafpj13 F;
    void * gm_l0; /* fg */
    uint8_t gm_l1; /* is_and */
    void * gm_l2; /* lhs */
    void * gm_l3; /* rhs */
    gm_b16_8 gm_l4; /* r */
    gm_b16_8 gm_l5; /* l */
    gm_b16_8 gm_l6; /* v */
    GM_STACK_CHECK(&F, 659u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        void * t3 = gm_l0;
        void * t4 = GM_LD(void *, (((uint8_t *)t3) + 0));
        void * t5 = (void *)(((uint8_t *)t4) + 8);
        gm_t88rcucg1b7uk t6 = gm_f_bool_ty_kotvo8t((void *)NULL, cx, NULL, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t7;
        GM_ST(void *, (uint8_t *)&t7, (void *)(gm_sicgmpca63jln));
        GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(1));
        gm_b16_8 t8 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t2, t6, t7);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l4 = t8;
    }
    {
        void * t9 = gm_l0;
        void * t10 = gm_l2;
        gm_t5o0tpv5jka2l t11;
        gm_f_expr_porbc9t((void *)&t11, (void *)NULL, cx, NULL, t9, t10);
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
        gm_b16_8 t13 = GM_LD(gm_b16_8, (((uint8_t *)&t11) + 8));
        gm_l5 = t13;
    }
    {
        uint8_t t15 = gm_l1;
        if (!t15) goto ie14;
        {
            void * t17 = gm_l0;
            gm_b16_8 t18 = gm_l5;
            gm_tnpifv20irnph t19;
            GM_ST(gm_b16_8, ((uint8_t *)&t19 + 0), t18);
            gm_b0_1 t20 = gm_f_linef_g6msml8((void *)NULL, cx, NULL, t17, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix14;
ie14:;
        {
            void * t22 = gm_l0;
            gm_b16_8 t23 = gm_l5;
            gm_tnpifv20irnph t24;
            GM_ST(gm_b16_8, ((uint8_t *)&t24 + 0), t23);
            gm_b0_1 t25 = gm_f_linef_o1esdvv((void *)NULL, cx, NULL, t22, t24);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
ix14:;
    }
    {
        void * t26 = gm_l0;
        void * t27 = gm_l0;
        uint64_t t28 = GM_LD(uint64_t, (((uint8_t *)t27) + 224));
        uint64_t r29; bool o30 = __builtin_add_overflow(t28, ((uint64_t)1ULL), &r29);
        if (o30) gm_trap("integer overflow in `+`", gm_src0gtuh94b1fce, 514, 5);
        uint64_t t31 = r29;
        GM_ST(uint64_t, (((uint8_t *)t26) + 224), t31);
    }
    {
        void * t32 = gm_l0;
        void * t33 = gm_l3;
        gm_t5o0tpv5jka2l t34;
        gm_f_expr_porbc9t((void *)&t34, (void *)NULL, cx, NULL, t32, t33);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t34))[0] != 0)) {
            gm_t5o0tpv5jka2l t35;
            GM_ST(uint8_t, (uint8_t *)&t35 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t35 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t35 + 4, 0);
            ((uint8_t *)&t35)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t35 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t34) + 8)));
            F.ret = t35;
            goto gm_fret;
        }
        gm_b16_8 t36 = GM_LD(gm_b16_8, (((uint8_t *)&t34) + 8));
        gm_l6 = t36;
    }
    {
        void * t37 = gm_l0;
        gm_t6j6smj7qoctu t38;
        gm_b16_8 t39 = gm_l4;
        GM_ST(gm_b16_8, (uint8_t *)&t38 + 0, t39);
        gm_b16_8 t40 = gm_l6;
        GM_ST(gm_b16_8, (uint8_t *)&t38 + 16, t40);
        gm_b0_1 t41 = gm_f_linef_uu8i6ue((void *)NULL, cx, NULL, t37, &t38);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t42 = gm_l0;
        void * t43 = gm_l0;
        uint64_t t44 = GM_LD(uint64_t, (((uint8_t *)t43) + 224));
        uint64_t r45; bool o46 = __builtin_sub_overflow(t44, ((uint64_t)1ULL), &r45);
        if (o46) gm_trap("integer overflow in `-`", gm_src0gtuh94b1fce, 517, 5);
        uint64_t t47 = r45;
        GM_ST(uint64_t, (((uint8_t *)t42) + 224), t47);
    }
    {
        void * t48 = gm_l0;
        gm_t0bf4lga76ktk t49;
        memset((uint8_t *)&t49 + 20, 0, 4);
        gm_b16_8 t50 = gm_l4;
        GM_ST(gm_b16_8, (uint8_t *)&t49 + 0, t50);
        int32_t t51;
        uint8_t t53 = gm_l1;
        if (!t53) goto ie52;
        int32_t t55 = ((int32_t)0LL);
        t51 = t55;
        goto ix52;
ie52:;
        int32_t t57 = ((int32_t)1LL);
        t51 = t57;
ix52:;
        GM_ST(int32_t, (uint8_t *)&t49 + 16, t51);
        gm_b0_1 t58 = gm_f_linef_v5l63rv((void *)NULL, cx, NULL, t48, &t49);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t59 = gm_l4;
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t59);
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
__attribute__((aligned(4))) void gm_f_loop_e5nllj2(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_loop_e5nllj2 F;
    void * gm_l0; /* fg */
    void * gm_l1; /* x */
    uint32_t gm_l2; /* n */
    gm_b16_8 gm_l3; /* brk */
    gm_b16_8 gm_l4; /* cnt */
    void * gm_l8; /* s */
    GM_STACK_CHECK(&F, 1022u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        uint32_t t3 = gm_f_fresh_5kokjmg(t2);
        gm_l2 = t3;
    }
    {
        uint32_t t4 = gm_l2;
        gm_t88rcucg1b7uk t5;
        GM_ST(uint32_t, ((uint8_t *)&t5 + 0), t4);
        gm_tabb710d9mq2e t6;
        gm_f_format_oikdiom((void *)&t6, (void *)NULL, cx, NULL, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t6))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t6) + 8, gm_src0gtuh94b1fce, 696, 12);
        gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&t6) + 8));
        gm_l3 = t7;
    }
    {
        uint32_t t8 = gm_l2;
        gm_t88rcucg1b7uk t9;
        GM_ST(uint32_t, ((uint8_t *)&t9 + 0), t8);
        gm_tabb710d9mq2e t10;
        gm_f_format_j7jgble((void *)&t10, (void *)NULL, cx, NULL, t9);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t10))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t10) + 8, gm_src0gtuh94b1fce, 697, 12);
        gm_b16_8 t11 = GM_LD(gm_b16_8, (((uint8_t *)&t10) + 8));
        gm_l4 = t11;
    }
    {
        void * t12 = gm_l0;
        gm_b16_8 t13;
        GM_ST(void *, (uint8_t *)&t13, (void *)(gm_si2pj4mpn1kn5));
        GM_ST(uintptr_t, (uint8_t *)&t13 + 8, (uintptr_t)(10));
        gm_b0_1 t14 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t12, t13);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t15 = gm_l0;
        void * t16 = gm_l0;
        uint64_t t17 = GM_LD(uint64_t, (((uint8_t *)t16) + 224));
        uint64_t r18; bool o19 = __builtin_add_overflow(t17, ((uint64_t)1ULL), &r18);
        if (o19) gm_trap("integer overflow in `+`", gm_src0gtuh94b1fce, 699, 5);
        uint64_t t20 = r18;
        GM_ST(uint64_t, (((uint8_t *)t15) + 224), t20);
    }
    {
        void * t21 = gm_l0;
        void * t22 = gm_l1;
        gm_b16_8 t23 = GM_LD(gm_b16_8, (((uint8_t *)t22) + 8));
        gm_b16_8 t24 = gm_l3;
        gm_b16_8 t25 = GM_LD(gm_b16_8, ((uint8_t *)&t24));
        gm_tqgtd5gtgj65s t26;
        gm_f_conds_79r4f4d((void *)&t26, (void *)NULL, cx, NULL, t21, t23, t25);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t26))[0] != 0)) {
            gm_t5o0tpv5jka2l t27;
            GM_ST(uint8_t, (uint8_t *)&t27 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t27 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t27 + 4, 0);
            ((uint8_t *)&t27)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t27 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t26) + 8)));
            F.ret = t27;
            goto gm_fret;
        }
        gm_b0_1 t28 = GM_LD(gm_b0_1, (((uint8_t *)&t26) + 8));
    }
    {
        void * t29 = gm_l0;
        void * t30 = gm_l1;
        uint32_t t31 = GM_LD(uint32_t, (((uint8_t *)t30) + 0));
        gm_b16_8 t32 = gm_l3;
        gm_b16_8 t33 = GM_LD(gm_b16_8, ((uint8_t *)&t32));
        gm_b16_8 t34 = gm_l4;
        gm_b16_8 t35 = GM_LD(gm_b16_8, ((uint8_t *)&t34));
        gm_f_loop_scope_ee94ihm((void *)&F.l5, (void *)NULL, cx, NULL, t29, t31, t33, t35);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t36 = gm_l0;
        void * t37 = (void *)(((uint8_t *)t36) + 240);
        gm_tftb1odc739t5 t38 = gm_f_push_pcqb56b(t37, &(*(gm_tehgl6lkqgvt0 *)((uint8_t *)&F.l5)));
        if (((((uint8_t *)&t38))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t38) + 1, gm_src0gtuh94b1fce, 702, 5);
        gm_b0_1 t39 = GM_LD(gm_b0_1, (((uint8_t *)&t38) + 1));
    }
    {
        void * t40 = gm_l0;
        void * t41 = gm_l1;
        void * t42 = GM_LD(void *, (((uint8_t *)t41) + 24));
        gm_f_block_31qi85g((void *)&F.l6, (void *)NULL, cx, NULL, t40, t42);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t43 = gm_l0;
        void * t44 = (void *)(((uint8_t *)t43) + 240);
        gm_t1g28mkc78m0c t45;
        gm_f_pop_g2usrvd((void *)&t45, t44);
        if (!((((uint8_t *)&t45))[104] != 0)) gm_trap("unwrap of null", gm_src0gtuh94b1fce, 704, 15);
        F.l7 = (*(gm_tehgl6lkqgvt0 *)(((uint8_t *)&t45) + 0));
    }
    {
        {
            gm_t5o0tpv5jka2l t47 = F.l6;
            if (((((uint8_t *)&t47))[0] != 0)) {
                gm_t5o0tpv5jka2l t48;
                GM_ST(uint8_t, (uint8_t *)&t48 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t48 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t48 + 4, 0);
                ((uint8_t *)&t48)[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t48 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t47) + 8)));
                F.ret = t48;
                goto gm_fret;
            }
            gm_b16_8 t49 = GM_LD(gm_b16_8, (((uint8_t *)&t47) + 8));
        }
    }
    {
        void * t50 = gm_l0;
        gm_b16_8 t51 = gm_l4;
        gm_b16_8 t52 = GM_LD(gm_b16_8, ((uint8_t *)&t51));
        gm_b0_1 t53 = gm_f_label_710oev4((void *)NULL, cx, NULL, t50, t52);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t55 = gm_l1;
        void * t56 = GM_LD(void *, (((uint8_t *)t55) + 32));
        if (!(GM_LD(void *, ((uint8_t *)&t56)) != 0)) goto ie54;
        void * t57 = GM_LD(void *, ((uint8_t *)&t56));
        gm_l8 = t57;
        {
            {
                void * t60 = gm_l0;
                void * t61 = gm_l8;
                gm_t5o0tpv5jka2l t62;
                gm_f_expr_porbc9t((void *)&t62, (void *)NULL, cx, NULL, t60, t61);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t62))[0] != 0)) {
                    gm_t5o0tpv5jka2l t63;
                    GM_ST(uint8_t, (uint8_t *)&t63 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t63 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t63 + 4, 0);
                    ((uint8_t *)&t63)[0] = 1;
                    GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t63 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t62) + 8)));
                    F.ret = t63;
                    goto gm_fret;
                }
                gm_b16_8 t64 = GM_LD(gm_b16_8, (((uint8_t *)&t62) + 8));
            }
        }
        goto ix54;
ie54:;
ix54:;
    }
    {
        void * t65 = gm_l0;
        void * t66 = gm_l0;
        uint64_t t67 = GM_LD(uint64_t, (((uint8_t *)t66) + 224));
        uint64_t r68; bool o69 = __builtin_sub_overflow(t67, ((uint64_t)1ULL), &r68);
        if (o69) gm_trap("integer overflow in `-`", gm_src0gtuh94b1fce, 708, 5);
        uint64_t t70 = r68;
        GM_ST(uint64_t, (((uint8_t *)t65) + 224), t70);
    }
    {
        void * t71 = gm_l0;
        gm_b16_8 t72;
        GM_ST(void *, (uint8_t *)&t72, (void *)(gm_s2cuflhqkkuaq));
        GM_ST(uintptr_t, (uint8_t *)&t72 + 8, (uintptr_t)(1));
        gm_b0_1 t73 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t71, t72);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t74 = gm_l0;
        gm_b0_1 t75 = gm_f_end_scope_45ar5qv((void *)NULL, cx, NULL, t74, &(*(gm_tehgl6lkqgvt0 *)((uint8_t *)&F.l7)), ((uint8_t)0));
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t76 = gm_l0;
        gm_b16_8 t77 = gm_l3;
        gm_b16_8 t78 = GM_LD(gm_b16_8, ((uint8_t *)&t77));
        gm_b0_1 t79 = gm_f_label_710oev4((void *)NULL, cx, NULL, t76, t78);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t80 = gm_l0;
        gm_b16_8 t81 = gm_f_unit_8ekedsl((void *)NULL, cx, NULL, t80);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t81);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_make_none_v2s8s0l(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_make_none_v2s8s0l F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 143u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        void * t4 = GM_LD(void *, (((uint8_t *)t3) + 0));
        void * t5 = (void *)(((uint8_t *)t4) + 8);
        gm_t88rcucg1b7uk t6 = gm_l1;
        gm_t5v8n6qrn4rh4 t7;
        gm_f_repr_53quqki((void *)&t7, (void *)NULL, cx, NULL, t5, t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t8 = ((GM_LD(uint8_t, ((uint8_t *)&t7) + 0) == ((uint8_t)3ULL)));
        if (!t8) goto ie2;
        {
            gm_b16_8 t10;
            GM_ST(void *, (uint8_t *)&t10, (void *)(gm_sr3mniof9fusf));
            GM_ST(uintptr_t, (uint8_t *)&t10 + 8, (uintptr_t)(11));
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
        gm_t88rcucg1b7uk t13 = gm_l1;
        gm_b16_8 t14 = gm_f_blob_d5apnrc((void *)NULL, cx, NULL, t12, t13);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t15 = 0;
        F.ret = t14;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_make_res_riv19po(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, uint8_t a2, gm_t88rcucg1b7uk a3, gm_b16_8 a4) {
    struct gm_fr_make_res_riv19po F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* t */
    uint8_t gm_l2; /* is_err */
    gm_t88rcucg1b7uk gm_l3; /* payload_ty */
    gm_b16_8 gm_l4; /* payload */
    gm_b16_8 gm_l5; /* offs */
    gm_te3hi449i91a8 gm_l6; /* parts */
    gm_b16_8 gm_l7; /* b */
    GM_STACK_CHECK(&F, 615u);
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
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_t88rcucg1b7uk t5 = gm_l1;
        gm_b16_8 t6 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t4, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l5 = t6;
    }
    {
        uint64_t t7 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t8 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
        if (t7 >= (uintptr_t)(t8)) gm_trap_index(t7, t8, gm_src0gtuh94b1fce, 223, 40);
        uint64_t t9 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t7 * 8));
        gm_t2lqu8jsj511d t10;
        GM_ST(uint64_t, ((uint8_t *)&t10 + 0), t9);
        GM_ST(uint64_t, ((uint8_t *)&t10 + 8), ((uint64_t)1ULL));
        GM_ST(gm_t2lqu8jsj511d, (uint8_t *)&gm_l6 + 0, t10);
        uint64_t t11 = (uintptr_t)((uint64_t)1ULL);
        uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
        if (t11 >= (uintptr_t)(t12)) gm_trap_index(t11, t12, gm_src0gtuh94b1fce, 223, 72);
        uint64_t t13 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t11 * 8));
        void * t14 = gm_l0;
        void * t15 = GM_LD(void *, (((uint8_t *)t14) + 0));
        void * t16 = (void *)(((uint8_t *)t15) + 8);
        gm_t88rcucg1b7uk t17 = gm_l3;
        uint64_t t18 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t16, t17);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t2lqu8jsj511d t19;
        GM_ST(uint64_t, ((uint8_t *)&t19 + 0), t13);
        GM_ST(uint64_t, ((uint8_t *)&t19 + 8), t18);
        GM_ST(gm_t2lqu8jsj511d, (uint8_t *)&gm_l6 + 16, t19);
    }
    {
        void * t20 = gm_l0;
        gm_t88rcucg1b7uk t21 = gm_l1;
        void * t22 = ((uint8_t *)&gm_l6);
        if ((uintptr_t)0 > (uintptr_t)2 || (uintptr_t)2 > (uintptr_t)2) gm_trap_slice((uintptr_t)0, (uintptr_t)2, 2, gm_src0gtuh94b1fce, 224, 26);
        gm_b16_8 t23;
        GM_ST(void *, (uint8_t *)&t23, (void *)(((uint8_t *)t22 + (uintptr_t)0 * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)((uintptr_t)2 - (uintptr_t)0));
        gm_b16_8 t24 = GM_LD(gm_b16_8, ((uint8_t *)&t23));
        gm_b16_8 t25 = gm_f_blob_over_qs5fusr((void *)NULL, cx, NULL, t20, t21, t24);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l7 = t25;
    }
    {
        void * t26 = gm_l0;
        gm_tlk7da9ssbu7q t27;
        memset((uint8_t *)&t27 + 28, 0, 4);
        gm_b16_8 t28 = gm_l7;
        GM_ST(gm_b16_8, (uint8_t *)&t27 + 0, t28);
        uint64_t t29 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t30 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
        if (t29 >= (uintptr_t)(t30)) gm_trap_index(t29, t30, gm_src0gtuh94b1fce, 225, 47);
        uint64_t t31 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t29 * 8));
        GM_ST(uint64_t, (uint8_t *)&t27 + 16, t31);
        int32_t t32;
        uint8_t t34 = gm_l2;
        if (!t34) goto ie33;
        int32_t t36 = ((int32_t)1LL);
        t32 = t36;
        goto ix33;
ie33:;
        int32_t t38 = ((int32_t)0LL);
        t32 = t38;
ix33:;
        GM_ST(int32_t, (uint8_t *)&t27 + 24, t32);
        gm_b0_1 t39 = gm_f_linef_t20u0i3((void *)NULL, cx, NULL, t26, &t27);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t40 = gm_l0;
        gm_t88rcucg1b7uk t41 = gm_l3;
        gm_t3877pk7lv748 t42;
        gm_b16_8 t43 = gm_l7;
        GM_ST(gm_b16_8, (uint8_t *)&t42 + 0, t43);
        uint64_t t44 = (uintptr_t)((uint64_t)1ULL);
        uint64_t t45 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
        if (t44 >= (uintptr_t)(t45)) gm_trap_index(t44, t45, gm_src0gtuh94b1fce, 226, 65);
        uint64_t t46 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t44 * 8));
        GM_ST(uint64_t, (uint8_t *)&t42 + 16, t46);
        gm_tabb710d9mq2e t47;
        gm_f_format_tcr2jpk((void *)&t47, (void *)NULL, cx, NULL, &t42);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t47))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t47) + 8, gm_src0gtuh94b1fce, 226, 26);
        gm_b16_8 t48 = GM_LD(gm_b16_8, (((uint8_t *)&t47) + 8));
        gm_b16_8 t49 = GM_LD(gm_b16_8, ((uint8_t *)&t48));
        gm_b16_8 t50 = gm_l4;
        gm_b0_1 t51 = gm_f_store_at7m90t((void *)NULL, cx, NULL, t40, t41, t49, t50);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t52 = gm_l7;
        uint8_t t53 = 0;
        F.ret = t52;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_make_slice_sa50efe(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_b16_8 a3) {
    struct gm_fr_make_slice_sa50efe F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* ptr */
    gm_b16_8 gm_l3; /* len */
    gm_b16_8 gm_l4; /* b */
    uint64_t gm_l5; /* p */
    GM_STACK_CHECK(&F, 321u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        gm_t88rcucg1b7uk t3 = gm_l1;
        gm_b16_8 t4 = gm_f_tmp_uninit_jr39s5u((void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l4 = t4;
    }
    {
        void * t5 = gm_l0;
        uint64_t t6 = gm_f_ptr_bytes_eocneng(t5);
        gm_l5 = t6;
    }
    {
        void * t7 = gm_l0;
        gm_t6j6smj7qoctu t8;
        gm_b16_8 t9 = gm_l4;
        GM_ST(gm_b16_8, (uint8_t *)&t8 + 0, t9);
        gm_b16_8 t10 = gm_l2;
        GM_ST(gm_b16_8, (uint8_t *)&t8 + 16, t10);
        gm_b0_1 t11 = gm_f_linef_sm7hlk3((void *)NULL, cx, NULL, t7, &t8);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t12 = gm_l0;
        gm_tq3m5qgvkpje8 t13;
        gm_b16_8 t14 = gm_l4;
        GM_ST(gm_b16_8, (uint8_t *)&t13 + 0, t14);
        uint64_t t15 = gm_l5;
        GM_ST(uint64_t, (uint8_t *)&t13 + 16, t15);
        gm_b16_8 t16 = gm_l3;
        GM_ST(gm_b16_8, (uint8_t *)&t13 + 24, t16);
        gm_b0_1 t17 = gm_f_linef_lhoje7n((void *)NULL, cx, NULL, t12, &t13);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t18 = gm_l4;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_make_some_9aqq928(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2) {
    struct gm_fr_make_some_9aqq928 F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* payload */
    gm_t88rcucg1b7uk gm_l3; /* inner */
    uint64_t gm_l4; /* off */
    gm_te3hi449i91a8 gm_l5; /* parts */
    gm_b16_8 gm_l6; /* b */
    GM_STACK_CHECK(&F, 680u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t3 = gm_l0;
        void * t4 = GM_LD(void *, (((uint8_t *)t3) + 0));
        void * t5 = (void *)(((uint8_t *)t4) + 8);
        gm_t88rcucg1b7uk t6 = gm_l1;
        gm_t5v8n6qrn4rh4 t7;
        gm_f_repr_53quqki((void *)&t7, (void *)NULL, cx, NULL, t5, t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t8 = ((GM_LD(uint8_t, ((uint8_t *)&t7) + 0) == ((uint8_t)3ULL)));
        if (!t8) goto ie2;
        {
            gm_b16_8 t10 = gm_l2;
            uint8_t t11 = 0;
            F.ret = t10;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t13 = gm_l0;
        void * t14 = GM_LD(void *, (((uint8_t *)t13) + 0));
        void * t15 = (void *)(((uint8_t *)t14) + 8);
        gm_t88rcucg1b7uk t16 = gm_l1;
        gm_t53at80pfdus3 t17;
        gm_f_kind_ghvav22((void *)&t17, t15, GM_CONV(gm_csguankko4lmhd, t16));
        uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&t17) + 0));
        if (t18 != ((uint8_t)13ULL)) goto ie12;
        gm_t88rcucg1b7uk t19 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t17) + 8));
        gm_l3 = t19;
        {
            uint64_t t21 = (uintptr_t)((uint64_t)1ULL);
            void * t22 = gm_l0;
            void * t23 = GM_LD(void *, (((uint8_t *)t22) + 0));
            void * t24 = (void *)(((uint8_t *)t23) + 8);
            gm_t88rcucg1b7uk t25 = gm_l1;
            gm_b16_8 t26 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t24, t25);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint64_t t27 = GM_LD(uintptr_t, ((uint8_t *)&t26) + 8);
            if (t21 >= (uintptr_t)(t27)) gm_trap_index(t21, t27, gm_src0gtuh94b1fce, 202, 12);
            uint64_t t28 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t26)) + t21 * 8));
            gm_l4 = t28;
        }
        {
            void * t29 = gm_l0;
            void * t30 = GM_LD(void *, (((uint8_t *)t29) + 0));
            void * t31 = (void *)(((uint8_t *)t30) + 8);
            gm_t88rcucg1b7uk t32 = gm_l3;
            uint64_t t33 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t31, t32);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t2lqu8jsj511d t34;
            GM_ST(uint64_t, ((uint8_t *)&t34 + 0), ((uint64_t)0ULL));
            GM_ST(uint64_t, ((uint8_t *)&t34 + 8), t33);
            GM_ST(gm_t2lqu8jsj511d, (uint8_t *)&gm_l5 + 0, t34);
            uint64_t t35 = gm_l4;
            gm_t2lqu8jsj511d t36;
            GM_ST(uint64_t, ((uint8_t *)&t36 + 0), t35);
            GM_ST(uint64_t, ((uint8_t *)&t36 + 8), ((uint64_t)1ULL));
            GM_ST(gm_t2lqu8jsj511d, (uint8_t *)&gm_l5 + 16, t36);
        }
        {
            void * t37 = gm_l0;
            gm_t88rcucg1b7uk t38 = gm_l1;
            void * t39 = ((uint8_t *)&gm_l5);
            if ((uintptr_t)0 > (uintptr_t)2 || (uintptr_t)2 > (uintptr_t)2) gm_trap_slice((uintptr_t)0, (uintptr_t)2, 2, gm_src0gtuh94b1fce, 204, 26);
            gm_b16_8 t40;
            GM_ST(void *, (uint8_t *)&t40, (void *)(((uint8_t *)t39 + (uintptr_t)0 * 16)));
            GM_ST(uintptr_t, (uint8_t *)&t40 + 8, (uintptr_t)((uintptr_t)2 - (uintptr_t)0));
            gm_b16_8 t41 = GM_LD(gm_b16_8, ((uint8_t *)&t40));
            gm_b16_8 t42 = gm_f_blob_over_qs5fusr((void *)NULL, cx, NULL, t37, t38, t41);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l6 = t42;
        }
        {
            void * t43 = gm_l0;
            gm_t88rcucg1b7uk t44 = gm_l3;
            gm_b16_8 t45 = gm_l6;
            gm_tnpifv20irnph t46;
            GM_ST(gm_b16_8, ((uint8_t *)&t46 + 0), t45);
            gm_tabb710d9mq2e t47;
            gm_f_format_4un19t8((void *)&t47, (void *)NULL, cx, NULL, t46);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t47))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t47) + 8, gm_src0gtuh94b1fce, 205, 21);
            gm_b16_8 t48 = GM_LD(gm_b16_8, (((uint8_t *)&t47) + 8));
            gm_b16_8 t49 = GM_LD(gm_b16_8, ((uint8_t *)&t48));
            gm_b16_8 t50 = gm_l2;
            gm_b0_1 t51 = gm_f_store_at7m90t((void *)NULL, cx, NULL, t43, t44, t49, t50);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t52 = gm_l0;
            gm_t3877pk7lv748 t53;
            gm_b16_8 t54 = gm_l6;
            GM_ST(gm_b16_8, (uint8_t *)&t53 + 0, t54);
            uint64_t t55 = gm_l4;
            GM_ST(uint64_t, (uint8_t *)&t53 + 16, t55);
            gm_b0_1 t56 = gm_f_linef_k4dha91((void *)NULL, cx, NULL, t52, &t53);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_b16_8 t57 = gm_l6;
            uint8_t t58 = 0;
            F.ret = t57;
            goto gm_fret;
        }
        goto ix12;
ie12:;
        {
            gm_trap("reached `@unreachable()`", gm_src0gtuh94b1fce, 201, 46);
        }
ix12:;
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
__attribute__((aligned(4))) gm_b16_8 gm_f_make_variant_srnk8ha(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, uint64_t a2, const void * a3) {
    struct gm_fr_make_variant_srnk8ha F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* t */
    uint64_t gm_l2; /* v */
    gm_th2lqbo77pvsc gm_l3; /* payload */
    void * gm_l4; /* def */
    gm_t88rcucg1b7uk gm_l5; /* tag */
    gm_b16_8 gm_l6; /* variants */
    gm_b16_8 gm_l8; /* offs */
    gm_t88rcucg1b7uk gm_l10; /* pt */
    gm_b16_8 gm_l11; /* b */
    gm_b16_8 gm_l12; /* tag_val */
    gm_b16_8 gm_l13; /* p */
    gm_t88rcucg1b7uk gm_l14; /* pt */
    GM_STACK_CHECK(&F, 963u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    memcpy(&gm_l3, a3, sizeof gm_l3);
    {
        void * t2 = gm_l0;
        gm_t88rcucg1b7uk t3 = gm_l1;
        void * t4 = gm_f_adt_def_ni54he1(t2, GM_CONV(gm_csguankko4lmhd, t3));
        gm_l4 = t4;
    }
    {
        void * t6 = gm_l4;
        uint8_t t7 = GM_LD(uint8_t, ((((uint8_t *)t6) + 24) + 0));
        if (t7 != ((uint8_t)1ULL)) goto ie5;
        gm_t88rcucg1b7uk t8 = GM_LD(gm_t88rcucg1b7uk, (((((uint8_t *)t6) + 24) + 8) + 0));
        gm_l5 = t8;
        gm_b16_8 t9 = GM_LD(gm_b16_8, (((((uint8_t *)t6) + 24) + 8) + 8));
        gm_l6 = t9;
        {
            uint64_t t11 = gm_l2;
            uint64_t t12 = (uintptr_t)t11;
            uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
            if (t12 >= (uintptr_t)(t13)) gm_trap_index(t12, t13, gm_src0gtuh94b1fce, 234, 16);
            gm_tckdobunaulee t14 = GM_LD(gm_tckdobunaulee, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t12 * 72));
            F.l7 = t14;
        }
        {
            void * t15 = gm_l0;
            void * t16 = GM_LD(void *, (((uint8_t *)t15) + 0));
            void * t17 = (void *)(((uint8_t *)t16) + 8);
            gm_t88rcucg1b7uk t18 = gm_l1;
            gm_b16_8 t19 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t17, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l8 = t19;
        }
        {
            gm_f_list_26fbt7s((void *)&F.l9, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t20 = (void *)((uint8_t *)&F.l9);
            uint64_t t21 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t22 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
            if (t21 >= (uintptr_t)(t22)) gm_trap_index(t21, t22, gm_src0gtuh94b1fce, 237, 30);
            uint64_t t23 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l8)) + t21 * 8));
            void * t24 = gm_l0;
            void * t25 = GM_LD(void *, (((uint8_t *)t24) + 0));
            void * t26 = (void *)(((uint8_t *)t25) + 8);
            gm_t88rcucg1b7uk t27 = gm_l5;
            uint64_t t28 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t26, t27);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t2lqu8jsj511d t29;
            GM_ST(uint64_t, ((uint8_t *)&t29 + 0), t23);
            GM_ST(uint64_t, ((uint8_t *)&t29 + 8), t28);
            gm_tftb1odc739t5 t30 = gm_f_push_u0rp7b1(t20, GM_CONV(gm_csc7lc6jlkqkuq, t29));
            if (((((uint8_t *)&t30))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t30) + 1, gm_src0gtuh94b1fce, 237, 5);
            gm_b0_1 t31 = GM_LD(gm_b0_1, (((uint8_t *)&t30) + 1));
        }
        {
            uint8_t t33;
            if (((((uint8_t *)&gm_l3))[16] != 0)) goto mn35;
            t33 = ((uint8_t)0);
            goto me34;
mn35:;
            t33 = ((uint8_t)1);
            goto me34;
mn36:;
            gm_trap("no match arm matched the scrutinee", gm_src0gtuh94b1fce, 238, 8);
me34:;
            if (!t33) goto ie32;
            {
                if (!(((((uint8_t *)&F.l7) + 16))[4] != 0)) goto ie38;
                gm_t88rcucg1b7uk t39 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l7) + 16));
                gm_l10 = t39;
                {
                    void * t41 = (void *)((uint8_t *)&F.l9);
                    uint64_t t42 = (uintptr_t)((uint64_t)1ULL);
                    uint64_t t43 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                    if (t42 >= (uintptr_t)(t43)) gm_trap_index(t42, t43, gm_src0gtuh94b1fce, 239, 61);
                    uint64_t t44 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l8)) + t42 * 8));
                    void * t45 = gm_l0;
                    void * t46 = GM_LD(void *, (((uint8_t *)t45) + 0));
                    void * t47 = (void *)(((uint8_t *)t46) + 8);
                    gm_t88rcucg1b7uk t48 = gm_l10;
                    uint64_t t49 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t47, t48);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t2lqu8jsj511d t50;
                    GM_ST(uint64_t, ((uint8_t *)&t50 + 0), t44);
                    GM_ST(uint64_t, ((uint8_t *)&t50 + 8), t49);
                    gm_tftb1odc739t5 t51 = gm_f_push_u0rp7b1(t41, GM_CONV(gm_csc7lc6jlkqkuq, t50));
                    if (((((uint8_t *)&t51))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t51) + 1, gm_src0gtuh94b1fce, 239, 36);
                    gm_b0_1 t52 = GM_LD(gm_b0_1, (((uint8_t *)&t51) + 1));
                }
                goto ix38;
ie38:;
ix38:;
            }
            goto ix32;
ie32:;
ix32:;
        }
        {
            void * t53 = gm_l0;
            gm_t88rcucg1b7uk t54 = gm_l1;
            gm_b16_8 t55 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 0));
            gm_b16_8 t56 = GM_LD(gm_b16_8, ((uint8_t *)&t55));
            gm_b16_8 t57 = gm_f_blob_over_qs5fusr((void *)NULL, cx, NULL, t53, t54, t56);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l11 = t57;
        }
        {
            void * t58 = gm_l0;
            gm_t88rcucg1b7uk t59 = gm_l5;
            gm_b16_8 t60 = gm_f_int_lit_ai37456((void *)NULL, cx, NULL, t58, t59, &(*(gm_tbci5ospk5nnn *)(((uint8_t *)&F.l7) + 24)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l12 = t60;
        }
        {
            void * t61 = gm_l0;
            gm_t88rcucg1b7uk t62 = gm_l5;
            gm_t3877pk7lv748 t63;
            gm_b16_8 t64 = gm_l11;
            GM_ST(gm_b16_8, (uint8_t *)&t63 + 0, t64);
            uint64_t t65 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t66 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
            if (t65 >= (uintptr_t)(t66)) gm_trap_index(t65, t66, gm_src0gtuh94b1fce, 243, 58);
            uint64_t t67 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l8)) + t65 * 8));
            GM_ST(uint64_t, (uint8_t *)&t63 + 16, t67);
            gm_tabb710d9mq2e t68;
            gm_f_format_tcr2jpk((void *)&t68, (void *)NULL, cx, NULL, &t63);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t68))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t68) + 8, gm_src0gtuh94b1fce, 243, 19);
            gm_b16_8 t69 = GM_LD(gm_b16_8, (((uint8_t *)&t68) + 8));
            gm_b16_8 t70 = GM_LD(gm_b16_8, ((uint8_t *)&t69));
            gm_b16_8 t71 = gm_l12;
            gm_b0_1 t72 = gm_f_store_at7m90t((void *)NULL, cx, NULL, t61, t62, t70, t71);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            if (!((((uint8_t *)&gm_l3))[16] != 0)) goto ie73;
            gm_b16_8 t74 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l3));
            gm_l13 = t74;
            {
                if (!(((((uint8_t *)&F.l7) + 16))[4] != 0)) goto ie76;
                gm_t88rcucg1b7uk t77 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l7) + 16));
                gm_l14 = t77;
                {
                    void * t79 = gm_l0;
                    gm_t88rcucg1b7uk t80 = gm_l14;
                    gm_t3877pk7lv748 t81;
                    gm_b16_8 t82 = gm_l11;
                    GM_ST(gm_b16_8, (uint8_t *)&t81 + 0, t82);
                    uint64_t t83 = (uintptr_t)((uint64_t)1ULL);
                    uint64_t t84 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                    if (t83 >= (uintptr_t)(t84)) gm_trap_index(t83, t84, gm_src0gtuh94b1fce, 245, 88);
                    uint64_t t85 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l8)) + t83 * 8));
                    GM_ST(uint64_t, (uint8_t *)&t81 + 16, t85);
                    gm_tabb710d9mq2e t86;
                    gm_f_format_tcr2jpk((void *)&t86, (void *)NULL, cx, NULL, &t81);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t86))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t86) + 8, gm_src0gtuh94b1fce, 245, 49);
                    gm_b16_8 t87 = GM_LD(gm_b16_8, (((uint8_t *)&t86) + 8));
                    gm_b16_8 t88 = GM_LD(gm_b16_8, ((uint8_t *)&t87));
                    gm_b16_8 t89 = gm_l13;
                    gm_b0_1 t90 = gm_f_store_at7m90t((void *)NULL, cx, NULL, t79, t80, t88, t89);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                goto ix76;
ie76:;
ix76:;
            }
            goto ix73;
ie73:;
ix73:;
        }
        {
            gm_b16_8 t91 = gm_l11;
            uint8_t t92 = 0;
            F.ret = t91;
            goto gm_fret;
        }
        goto ix5;
ie5:;
        {
            gm_trap("reached `@unreachable()`", gm_src0gtuh94b1fce, 233, 44);
        }
ix5:;
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
extern __attribute__((aligned(4))) gm_b16_8 gm_f_mask_epjl3to(void *env, gm_node *cx, gm_node *inst, uint32_t a0);
__attribute__((aligned(4))) uint8_t gm_f_no_writes_p1jjjfj(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_no_writes_p1jjjfj F;
    void * gm_l0; /* e */
    uint8_t gm_l1; /* pure */
    GM_STACK_CHECK(&F, 90u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        uint8_t t3;
        uint8_t t8 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t8 != ((uint8_t)0ULL)) goto pa7;
        goto po6;
pa7:;
        uint8_t t10 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t10 != ((uint8_t)1ULL)) goto pa9;
        goto po6;
pa9:;
        uint8_t t12 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t12 != ((uint8_t)10ULL)) goto pa11;
        goto po6;
pa11:;
        uint8_t t14 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t14 != ((uint8_t)13ULL)) goto pa13;
        goto po6;
pa13:;
        uint8_t t16 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t16 != ((uint8_t)11ULL)) goto pa15;
        goto po6;
pa15:;
        uint8_t t18 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t18 != ((uint8_t)12ULL)) goto pa17;
        goto po6;
pa17:;
        uint8_t t20 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t20 != ((uint8_t)2ULL)) goto pa19;
        goto po6;
pa19:;
        uint8_t t22 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t22 != ((uint8_t)3ULL)) goto pa21;
        goto po6;
pa21:;
        uint8_t t24 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t24 != ((uint8_t)4ULL)) goto pa23;
        goto po6;
pa23:;
        uint8_t t26 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t26 != ((uint8_t)5ULL)) goto pa25;
        goto po6;
pa25:;
        uint8_t t28 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t28 != ((uint8_t)6ULL)) goto pa27;
        goto po6;
pa27:;
        uint8_t t30 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t30 != ((uint8_t)7ULL)) goto pa29;
        goto po6;
pa29:;
        uint8_t t32 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t32 != ((uint8_t)9ULL)) goto pa31;
        goto po6;
pa31:;
        uint8_t t34 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t34 != ((uint8_t)8ULL)) goto pa33;
        goto po6;
pa33:;
        uint8_t t36 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t36 != ((uint8_t)14ULL)) goto pa35;
        goto po6;
pa35:;
        uint8_t t38 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t38 != ((uint8_t)15ULL)) goto pa37;
        goto po6;
pa37:;
        uint8_t t40 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t40 != ((uint8_t)16ULL)) goto pa39;
        goto po6;
pa39:;
        uint8_t t42 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t42 != ((uint8_t)17ULL)) goto pa41;
        goto po6;
pa41:;
        uint8_t t44 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t44 != ((uint8_t)18ULL)) goto pa43;
        goto po6;
pa43:;
        uint8_t t46 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t46 != ((uint8_t)19ULL)) goto pa45;
        goto po6;
pa45:;
        uint8_t t48 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t48 != ((uint8_t)20ULL)) goto pa47;
        goto po6;
pa47:;
        uint8_t t50 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t50 != ((uint8_t)21ULL)) goto pa49;
        goto po6;
pa49:;
        uint8_t t52 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t52 != ((uint8_t)22ULL)) goto pa51;
        goto po6;
pa51:;
        uint8_t t54 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t54 != ((uint8_t)23ULL)) goto pa53;
        goto po6;
pa53:;
        uint8_t t56 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t56 != ((uint8_t)24ULL)) goto pa55;
        goto po6;
pa55:;
        uint8_t t58 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t58 != ((uint8_t)26ULL)) goto pa57;
        goto po6;
pa57:;
        uint8_t t60 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t60 != ((uint8_t)29ULL)) goto pa59;
        goto po6;
pa59:;
        uint8_t t62 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t62 != ((uint8_t)30ULL)) goto pa61;
        goto po6;
pa61:;
        uint8_t t64 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t64 != ((uint8_t)31ULL)) goto pa63;
        goto po6;
pa63:;
        uint8_t t66 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t66 != ((uint8_t)32ULL)) goto pa65;
        goto po6;
pa65:;
        uint8_t t68 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t68 != ((uint8_t)35ULL)) goto pa67;
        goto po6;
pa67:;
        uint8_t t70 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t70 != ((uint8_t)36ULL)) goto pa69;
        goto po6;
pa69:;
        uint8_t t72 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t72 != ((uint8_t)37ULL)) goto pa71;
        goto po6;
pa71:;
        uint8_t t74 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t74 != ((uint8_t)38ULL)) goto pa73;
        goto po6;
pa73:;
        goto mn5;
po6:;
        t3 = ((uint8_t)1);
        goto me4;
mn5:;
        t3 = ((uint8_t)0);
        goto me4;
mn75:;
        gm_trap("no match arm matched the scrutinee", gm_src0gtuh94b1fce, 1204, 13);
me4:;
        gm_l1 = t3;
    }
    {
        uint8_t t76 = gm_l1;
        F.l2 = t76;
    }
    {
        uint8_t t78 = F.l2;
        if (!t78) goto ie77;
        {
            void * t80 = gm_l0;
            gm_closure t81 = ((gm_closure){(void *)gm_f_fn_dfds699, (void *)&F.h, cx});
            gm_f_for_each_child_fm49s73(t80, t81);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix77;
ie77:;
ix77:;
    }
    {
        uint8_t t82 = F.l2;
        uint8_t t83 = 0;
        F.ret = t82;
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
extern __attribute__((aligned(4))) uint8_t gm_f_opt_no_writes_fjqh1hi(void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_opt_present_joptd3k(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2) {
    struct gm_fr_opt_present_joptd3k F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* addr */
    uint64_t gm_l3; /* off */
    GM_STACK_CHECK(&F, 383u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t3 = gm_l0;
        void * t4 = GM_LD(void *, (((uint8_t *)t3) + 0));
        void * t5 = (void *)(((uint8_t *)t4) + 8);
        gm_t88rcucg1b7uk t6 = gm_l1;
        gm_t5v8n6qrn4rh4 t7;
        gm_f_repr_53quqki((void *)&t7, (void *)NULL, cx, NULL, t5, t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t8 = ((GM_LD(uint8_t, ((uint8_t *)&t7) + 0) == ((uint8_t)3ULL)));
        if (!t8) goto ie2;
        {
            gm_b16_8 t10 = gm_l2;
            gm_tnpifv20irnph t11;
            GM_ST(gm_b16_8, ((uint8_t *)&t11 + 0), t10);
            gm_tabb710d9mq2e t12;
            gm_f_format_29fhse4((void *)&t12, (void *)NULL, cx, NULL, t11);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t12))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t12) + 8, gm_src0gtuh94b1fce, 194, 41);
            gm_b16_8 t13 = GM_LD(gm_b16_8, (((uint8_t *)&t12) + 8));
            gm_b16_8 t14 = GM_LD(gm_b16_8, ((uint8_t *)&t13));
            uint8_t t15 = 0;
            F.ret = t14;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint64_t t16 = (uintptr_t)((uint64_t)1ULL);
        void * t17 = gm_l0;
        void * t18 = GM_LD(void *, (((uint8_t *)t17) + 0));
        void * t19 = (void *)(((uint8_t *)t18) + 8);
        gm_t88rcucg1b7uk t20 = gm_l1;
        gm_b16_8 t21 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t19, t20);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint64_t t22 = GM_LD(uintptr_t, ((uint8_t *)&t21) + 8);
        if (t16 >= (uintptr_t)(t22)) gm_trap_index(t16, t22, gm_src0gtuh94b1fce, 195, 12);
        uint64_t t23 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t21)) + t16 * 8));
        gm_l3 = t23;
    }
    {
        gm_t3877pk7lv748 t24;
        gm_b16_8 t25 = gm_l2;
        GM_ST(gm_b16_8, (uint8_t *)&t24 + 0, t25);
        uint64_t t26 = gm_l3;
        GM_ST(uint64_t, (uint8_t *)&t24 + 16, t26);
        gm_tabb710d9mq2e t27;
        gm_f_format_c2otgb4((void *)&t27, (void *)NULL, cx, NULL, &t24);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t27))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t27) + 8, gm_src0gtuh94b1fce, 196, 12);
        gm_b16_8 t28 = GM_LD(gm_b16_8, (((uint8_t *)&t27) + 8));
        gm_b16_8 t29 = GM_LD(gm_b16_8, ((uint8_t *)&t28));
        uint8_t t30 = 0;
        F.ret = t29;
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
__attribute__((aligned(4))) void gm_f_place_addr_7ji15ot(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_place_addr_7ji15ot F;
    void * gm_l0; /* fg */
    void * gm_l1; /* e */
    uint32_t gm_l2; /* l */
    uint64_t gm_l3; /* decl */
    uint64_t gm_l4; /* i */
    gm_b16_8 gm_l6; /* t */
    gm_tl874t2atnlh3 gm_l7; /* span */
    gm_b16_8 gm_l8; /* name */
    uint32_t gm_l9; /* levels */
    uint32_t gm_l10; /* local */
    gm_b16_8 gm_l11; /* f */
    void * gm_l12; /* p */
    gm_b16_8 gm_l13; /* v */
    void * gm_l14; /* base */
    uint32_t gm_l15; /* index */
    gm_b16_8 gm_l17; /* b */
    uint64_t gm_l18; /* off */
    void * gm_l19; /* base */
    void * gm_l20; /* index */
    uint8_t gm_l21; /* checked */
    uint8_t gm_l22; /* is_array */
    gm_th2lqbo77pvsc gm_l23; /* early */
    gm_b16_8 gm_l24; /* b0 */
    gm_b16_8 gm_l25; /* n */
    gm_b16_8 gm_l26; /* cty */
    gm_b16_8 gm_l27; /* b1 */
    gm_b16_8 gm_l28; /* iv */
    gm_b16_8 gm_l29; /* bi */
    gm_b16_8 gm_l30; /* i */
    gm_b16_8 gm_l31; /* at */
    uint64_t gm_l32; /* len */
    gm_t88rcucg1b7uk gm_l33; /* elem */
    uint64_t gm_l34; /* esz */
    gm_b16_8 gm_l35; /* b */
    gm_b16_8 gm_l36; /* x */
    gm_t88rcucg1b7uk gm_l37; /* elem */
    uint64_t gm_l38; /* esz */
    gm_b16_8 gm_l39; /* a */
    gm_b16_8 gm_l40; /* v */
    gm_b16_8 gm_l41; /* sl */
    gm_b16_8 gm_l42; /* len */
    gm_b16_8 gm_l43; /* p */
    gm_t88rcucg1b7uk gm_l44; /* elem */
    uint64_t gm_l45; /* esz */
    gm_b16_8 gm_l46; /* v */
    gm_b16_8 gm_l47; /* x */
    gm_b16_8 gm_l48; /* v */
    GM_STACK_CHECK(&F, 5302u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l1;
        uint8_t t13 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        switch (t13) { case ((uint8_t)0ULL): goto ma11; case ((uint8_t)1ULL): goto ma11; case ((uint8_t)2ULL): goto ma11; case ((uint8_t)3ULL): goto ma11; case ((uint8_t)4ULL): goto ma11; case ((uint8_t)5ULL): goto ma11; case ((uint8_t)6ULL): goto ma11; case ((uint8_t)7ULL): goto ma11; case ((uint8_t)8ULL): goto ma11; case ((uint8_t)9ULL): goto ma11; case ((uint8_t)10ULL): goto ma11; case ((uint8_t)11ULL): goto ma11; case ((uint8_t)12ULL): goto ma11; case ((uint8_t)13ULL): goto ma11; case ((uint8_t)14ULL): goto ma6; case ((uint8_t)15ULL): goto ma4; case ((uint8_t)16ULL): goto ma7; case ((uint8_t)17ULL): goto ma5; case ((uint8_t)18ULL): goto ma8; case ((uint8_t)19ULL): goto ma9; case ((uint8_t)20ULL): goto ma10; case ((uint8_t)21ULL): goto ma11; case ((uint8_t)22ULL): goto ma11; case ((uint8_t)23ULL): goto ma11; case ((uint8_t)24ULL): goto ma11; case ((uint8_t)25ULL): goto ma11; case ((uint8_t)26ULL): goto ma11; case ((uint8_t)27ULL): goto ma11; case ((uint8_t)28ULL): goto ma11; case ((uint8_t)29ULL): goto ma11; case ((uint8_t)30ULL): goto ma11; case ((uint8_t)31ULL): goto ma11; case ((uint8_t)32ULL): goto ma11; case ((uint8_t)33ULL): goto ma11; case ((uint8_t)34ULL): goto ma11; case ((uint8_t)35ULL): goto ma11; case ((uint8_t)36ULL): goto ma11; case ((uint8_t)37ULL): goto ma11; case ((uint8_t)38ULL): goto ma11; case ((uint8_t)39ULL): goto ma11; case ((uint8_t)40ULL): goto ma11; case ((uint8_t)41ULL): goto ma11; case ((uint8_t)42ULL): goto ma11; case ((uint8_t)43ULL): goto ma11; case ((uint8_t)44ULL): goto ma11; case ((uint8_t)45ULL): goto ma11; case ((uint8_t)46ULL): goto ma11; case ((uint8_t)47ULL): goto ma11; case ((uint8_t)48ULL): goto ma11; case ((uint8_t)49ULL): goto ma11; case ((uint8_t)50ULL): goto ma11; case ((uint8_t)51ULL): goto ma11; case ((uint8_t)52ULL): goto ma11; case ((uint8_t)53ULL): goto ma11; case ((uint8_t)54ULL): goto ma11; case ((uint8_t)55ULL): goto ma11; case ((uint8_t)56ULL): goto ma11; case ((uint8_t)57ULL): goto ma11; case ((uint8_t)58ULL): goto ma11; default: goto ma11; }
ma4:;
        uint8_t t15 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t15 != ((uint8_t)15ULL)) goto mn14;
        uint32_t t16 = GM_LD(uint32_t, ((((uint8_t *)t2) + 8) + 8));
        gm_l2 = t16;
        void * t17 = gm_l0;
        uint32_t t18 = gm_l2;
        gm_b16_8 t19 = gm_f_local_addr_g9lkr6l((void *)NULL, cx, NULL, t17, t18);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t19);
        goto gm_fdone;
        goto me3;
mn14:;
ma5:;
        uint8_t t21 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t21 != ((uint8_t)17ULL)) goto mn20;
        uint64_t t22 = GM_LD(uint64_t, (((((uint8_t *)t2) + 8) + 8) + 16));
        gm_l3 = t22;
        {
            void * t24 = gm_l0;
            void * t25 = GM_LD(void *, (((uint8_t *)t24) + 0));
            void * t26 = GM_LD(void *, (((uint8_t *)t25) + 0));
            uint64_t t27 = gm_l3;
            gm_tf5srh4mp4b3i t28 = gm_f_global_index_nhjk80t(t26, t27);
            uint64_t t29;
            if (((((uint8_t *)&t28))[8] != 0)) {
                uint64_t t30 = GM_LD(uint64_t, (((uint8_t *)&t28) + 0));
                t29 = t30;
            } else {
                void * t31 = gm_l1;
                gm_tl874t2atnlh3 t32 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t31) + 72));
                gm_b16_8 t33;
                GM_ST(void *, (uint8_t *)&t33, (void *)(gm_sk7tgk6pi62vu));
                GM_ST(uintptr_t, (uint8_t *)&t33 + 8, (uintptr_t)(13));
                gm_t08olq97rscn3 t34;
                gm_f_unsupported_531q51r((void *)&t34, (void *)NULL, cx, NULL, t32, t33);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 1;
                GM_ST(gm_t08olq97rscn3, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t34);
                goto gm_fdone;
            }
            gm_l4 = t29;
        }
        {
            uint64_t t35 = gm_l4;
            uint64_t t36 = (uintptr_t)t35;
            void * t37 = gm_l0;
            void * t38 = GM_LD(void *, (((uint8_t *)t37) + 0));
            void * t39 = GM_LD(void *, (((uint8_t *)t38) + 0));
            void * t40 = GM_LD(void *, (((uint8_t *)t39) + 0));
            uint64_t t41 = GM_LD(uintptr_t, ((((uint8_t *)t40) + 1448) + 0) + 8);
            if (t36 >= (uintptr_t)(t41)) gm_trap_index(t36, t41, gm_src0gtuh94b1fce, 955, 19);
            gm_tq7qjpkmqmc5s t42 = GM_LD(gm_tq7qjpkmqmc5s, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t40) + 1448) + 0)) + t36 * 104));
            F.l5 = t42;
        }
        {
            void * t43 = gm_l0;
            void * t44 = GM_LD(void *, (((uint8_t *)t43) + 0));
            uint64_t t45 = gm_l4;
            gm_b16_8 t46 = gm_f_global_tag_gs7s3p6((void *)NULL, cx, NULL, t44, t45);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l6 = t46;
        }
        {
            uint8_t t48 = GM_LD(uint8_t, (((uint8_t *)&F.l5) + 48));
            if (!t48) goto ie47;
            uint8_t t49;
            if ((((((uint8_t *)&F.l5) + 56))[16] != 0)) goto mn51;
            t49 = ((uint8_t)1);
            goto me50;
mn51:;
            t49 = ((uint8_t)0);
            goto me50;
mn52:;
            gm_trap("no match arm matched the scrutinee", gm_src0gtuh94b1fce, 957, 36);
me50:;
            if (!t49) goto ie47;
            {
                gm_b16_8 t54 = gm_l6;
                gm_tnpifv20irnph t55;
                GM_ST(gm_b16_8, ((uint8_t *)&t55 + 0), t54);
                gm_tabb710d9mq2e t56;
                gm_f_format_4g0prpe((void *)&t56, (void *)NULL, cx, NULL, t55);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t56))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t56) + 8, gm_src0gtuh94b1fce, 957, 66);
                gm_b16_8 t57 = GM_LD(gm_b16_8, (((uint8_t *)&t56) + 8));
                gm_b16_8 t58 = GM_LD(gm_b16_8, ((uint8_t *)&t57));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
                ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t58);
                goto gm_fdone;
            }
            goto ix47;
ie47:;
ix47:;
        }
        {
            gm_b16_8 t59 = gm_l6;
            gm_tnpifv20irnph t60;
            GM_ST(gm_b16_8, ((uint8_t *)&t60 + 0), t59);
            gm_tabb710d9mq2e t61;
            gm_f_format_7peeku4((void *)&t61, (void *)NULL, cx, NULL, t60);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t61))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t61) + 8, gm_src0gtuh94b1fce, 958, 20);
            gm_b16_8 t62 = GM_LD(gm_b16_8, (((uint8_t *)&t61) + 8));
            gm_b16_8 t63 = GM_LD(gm_b16_8, ((uint8_t *)&t62));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
            ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t63);
            goto gm_fdone;
        }
        goto me3;
mn20:;
ma6:;
        uint8_t t65 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t65 != ((uint8_t)14ULL)) goto mn64;
        gm_tl874t2atnlh3 t66 = GM_LD(gm_tl874t2atnlh3, (((((uint8_t *)t2) + 8) + 8) + 16));
        gm_l7 = t66;
        {
            void * t68 = gm_l0;
            void * t69 = GM_LD(void *, (((uint8_t *)t68) + 0));
            gm_tl874t2atnlh3 t70 = gm_l7;
            void * t71 = gm_l1;
            gm_t88rcucg1b7uk t72 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t71) + 0));
            void * t73 = gm_l1;
            gm_tl874t2atnlh3 t74 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t73) + 72));
            gm_t5o0tpv5jka2l t75;
            gm_f_const_storage_0d7nj5d((void *)&t75, (void *)NULL, cx, NULL, t69, t70, t72, t74);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t75))[0] != 0)) {
                gm_t5o0tpv5jka2l t76;
                GM_ST(uint8_t, (uint8_t *)&t76 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t76 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t76 + 4, 0);
                ((uint8_t *)&t76)[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t76 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t75) + 8)));
                F.ret = t76;
                goto gm_fret;
            }
            gm_b16_8 t77 = GM_LD(gm_b16_8, (((uint8_t *)&t75) + 8));
            gm_l8 = t77;
        }
        {
            gm_b16_8 t78 = gm_l8;
            gm_tnpifv20irnph t79;
            GM_ST(gm_b16_8, ((uint8_t *)&t79 + 0), t78);
            gm_tabb710d9mq2e t80;
            gm_f_format_da6unq6((void *)&t80, (void *)NULL, cx, NULL, t79);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t80))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t80) + 8, gm_src0gtuh94b1fce, 963, 20);
            gm_b16_8 t81 = GM_LD(gm_b16_8, (((uint8_t *)&t80) + 8));
            gm_b16_8 t82 = GM_LD(gm_b16_8, ((uint8_t *)&t81));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
            ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t82);
            goto gm_fdone;
        }
        goto me3;
mn64:;
ma7:;
        uint8_t t84 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t84 != ((uint8_t)16ULL)) goto mn83;
        uint32_t t85 = GM_LD(uint32_t, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l9 = t85;
        uint32_t t86 = GM_LD(uint32_t, (((((uint8_t *)t2) + 8) + 8) + 4));
        gm_l10 = t86;
        {
            void * t88 = gm_l0;
            uint32_t t89 = gm_l9;
            uint32_t t90 = gm_l10;
            gm_t88rcucg1b7uk t91;
            GM_ST(uint32_t, ((uint8_t *)&t91 + 0), t90);
            gm_tabb710d9mq2e t92;
            gm_f_format_sdje4ev((void *)&t92, (void *)NULL, cx, NULL, t91);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t92))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t92) + 8, gm_src0gtuh94b1fce, 966, 41);
            gm_b16_8 t93 = GM_LD(gm_b16_8, (((uint8_t *)&t92) + 8));
            gm_b16_8 t94 = GM_LD(gm_b16_8, ((uint8_t *)&t93));
            gm_b16_8 t95 = gm_f_frame_field_nj8m22n((void *)NULL, cx, NULL, t88, t89, t94);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l11 = t95;
        }
        {
            gm_b16_8 t96 = gm_l11;
            gm_tnpifv20irnph t97;
            GM_ST(gm_b16_8, ((uint8_t *)&t97 + 0), t96);
            gm_tabb710d9mq2e t98;
            gm_f_format_4un19t8((void *)&t98, (void *)NULL, cx, NULL, t97);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t98))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t98) + 8, gm_src0gtuh94b1fce, 967, 20);
            gm_b16_8 t99 = GM_LD(gm_b16_8, (((uint8_t *)&t98) + 8));
            gm_b16_8 t100 = GM_LD(gm_b16_8, ((uint8_t *)&t99));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
            ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t100);
            goto gm_fdone;
        }
        goto me3;
mn83:;
ma8:;
        uint8_t t102 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t102 != ((uint8_t)18ULL)) goto mn101;
        void * t103 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l12 = t103;
        {
            void * t105 = gm_l0;
            void * t106 = gm_l12;
            gm_t5o0tpv5jka2l t107;
            gm_f_expr_porbc9t((void *)&t107, (void *)NULL, cx, NULL, t105, t106);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t107))[0] != 0)) {
                gm_t5o0tpv5jka2l t108;
                GM_ST(uint8_t, (uint8_t *)&t108 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t108 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t108 + 4, 0);
                ((uint8_t *)&t108)[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t108 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t107) + 8)));
                F.ret = t108;
                goto gm_fret;
            }
            gm_b16_8 t109 = GM_LD(gm_b16_8, (((uint8_t *)&t107) + 8));
            gm_l13 = t109;
        }
        {
            gm_b16_8 t110 = gm_l13;
            gm_tnpifv20irnph t111;
            GM_ST(gm_b16_8, ((uint8_t *)&t111 + 0), t110);
            gm_tabb710d9mq2e t112;
            gm_f_format_da6unq6((void *)&t112, (void *)NULL, cx, NULL, t111);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t112))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t112) + 8, gm_src0gtuh94b1fce, 971, 20);
            gm_b16_8 t113 = GM_LD(gm_b16_8, (((uint8_t *)&t112) + 8));
            gm_b16_8 t114 = GM_LD(gm_b16_8, ((uint8_t *)&t113));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
            ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t114);
            goto gm_fdone;
        }
        goto me3;
mn101:;
ma9:;
        uint8_t t116 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t116 != ((uint8_t)19ULL)) goto mn115;
        void * t117 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l14 = t117;
        uint32_t t118 = GM_LD(uint32_t, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l15 = t118;
        {
            void * t121 = gm_l0;
            void * t122 = GM_LD(void *, (((uint8_t *)t121) + 0));
            void * t123 = (void *)(((uint8_t *)t122) + 8);
            void * t124 = gm_l14;
            gm_t88rcucg1b7uk t125 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t124) + 0));
            gm_t53at80pfdus3 t126;
            gm_f_kind_ghvav22((void *)&t126, t123, GM_CONV(gm_csguankko4lmhd, t125));
            uint8_t t127;
            uint8_t t130 = GM_LD(uint8_t, (((uint8_t *)&t126) + 0));
            if (t130 != ((uint8_t)17ULL)) goto mn129;
            t127 = ((uint8_t)1);
            goto me128;
mn129:;
            t127 = ((uint8_t)0);
            goto me128;
mn131:;
            gm_trap("no match arm matched the scrutinee", gm_src0gtuh94b1fce, 974, 16);
me128:;
            if (!t127) goto ie120;
            {
                void * t134 = gm_l0;
                void * t135 = gm_l14;
                gm_t88rcucg1b7uk t136 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t135) + 0));
                void * t137 = gm_f_adt_def_ni54he1(t134, GM_CONV(gm_csguankko4lmhd, t136));
                uint8_t t138 = GM_LD(uint8_t, ((((uint8_t *)t137) + 24) + 0));
                if (t138 != ((uint8_t)0ULL)) goto ie133;
                gm_tpautr4br7h3l t139 = GM_LD(gm_tpautr4br7h3l, ((((uint8_t *)t137) + 24) + 8));
                F.l16 = t139;
                {
                    uint8_t t142;
                    if ((((((uint8_t *)&F.l16) + 40))[4] != 0)) goto mn144;
                    t142 = ((uint8_t)0);
                    goto me143;
mn144:;
                    t142 = ((uint8_t)1);
                    goto me143;
mn145:;
                    gm_trap("no match arm matched the scrutinee", gm_src0gtuh94b1fce, 976, 24);
me143:;
                    if (!t142) goto ie141;
                    {
                        void * t147 = gm_l1;
                        gm_tl874t2atnlh3 t148 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t147) + 72));
                        gm_b16_8 t149;
                        GM_ST(void *, (uint8_t *)&t149, (void *)(gm_sl576h51kqj52));
                        GM_ST(uintptr_t, (uint8_t *)&t149 + 8, (uintptr_t)(23));
                        gm_t08olq97rscn3 t150;
                        gm_f_unsupported_531q51r((void *)&t150, (void *)NULL, cx, NULL, t148, t149);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 1;
                        GM_ST(gm_t08olq97rscn3, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t150);
                        goto gm_fdone;
                    }
                    goto ix141;
ie141:;
ix141:;
                }
                goto ix133;
ie133:;
ix133:;
            }
            goto ix120;
ie120:;
ix120:;
        }
        {
            void * t151 = gm_l0;
            void * t152 = gm_l14;
            gm_t5o0tpv5jka2l t153;
            gm_f_place_addr_7ji15ot((void *)&t153, (void *)NULL, cx, NULL, t151, t152);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t153))[0] != 0)) {
                gm_t5o0tpv5jka2l t154;
                GM_ST(uint8_t, (uint8_t *)&t154 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t154 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t154 + 4, 0);
                ((uint8_t *)&t154)[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t154 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t153) + 8)));
                F.ret = t154;
                goto gm_fret;
            }
            gm_b16_8 t155 = GM_LD(gm_b16_8, (((uint8_t *)&t153) + 8));
            gm_l17 = t155;
        }
        {
            uint32_t t156 = gm_l15;
            uint64_t t157 = (uint64_t)t156;
            uint64_t t158 = (uintptr_t)t157;
            void * t159 = gm_l0;
            void * t160 = GM_LD(void *, (((uint8_t *)t159) + 0));
            void * t161 = (void *)(((uint8_t *)t160) + 8);
            void * t162 = gm_l14;
            gm_t88rcucg1b7uk t163 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t162) + 0));
            gm_b16_8 t164 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t161, t163);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint64_t t165 = GM_LD(uintptr_t, ((uint8_t *)&t164) + 8);
            if (t158 >= (uintptr_t)(t165)) gm_trap_index(t158, t165, gm_src0gtuh94b1fce, 980, 20);
            uint64_t t166 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t164)) + t158 * 8));
            gm_l18 = t166;
        }
        {
            gm_t3877pk7lv748 t167;
            gm_b16_8 t168 = gm_l17;
            GM_ST(gm_b16_8, (uint8_t *)&t167 + 0, t168);
            uint64_t t169 = gm_l18;
            GM_ST(uint64_t, (uint8_t *)&t167 + 16, t169);
            gm_tabb710d9mq2e t170;
            gm_f_format_gjujk09((void *)&t170, (void *)NULL, cx, NULL, &t167);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t170))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t170) + 8, gm_src0gtuh94b1fce, 981, 20);
            gm_b16_8 t171 = GM_LD(gm_b16_8, (((uint8_t *)&t170) + 8));
            gm_b16_8 t172 = GM_LD(gm_b16_8, ((uint8_t *)&t171));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
            ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t172);
            goto gm_fdone;
        }
        goto me3;
mn115:;
ma10:;
        uint8_t t174 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t174 != ((uint8_t)20ULL)) goto mn173;
        void * t175 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l19 = t175;
        void * t176 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l20 = t176;
        uint8_t t177 = GM_LD(uint8_t, (((((uint8_t *)t2) + 8) + 8) + 16));
        gm_l21 = t177;
        {
            void * t179 = gm_l0;
            void * t180 = GM_LD(void *, (((uint8_t *)t179) + 0));
            void * t181 = (void *)(((uint8_t *)t180) + 8);
            void * t182 = gm_l19;
            gm_t88rcucg1b7uk t183 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t182) + 0));
            gm_t53at80pfdus3 t184;
            gm_f_kind_ghvav22((void *)&t184, t181, GM_CONV(gm_csguankko4lmhd, t183));
            uint8_t t185;
            uint8_t t188 = GM_LD(uint8_t, (((uint8_t *)&t184) + 0));
            if (t188 != ((uint8_t)11ULL)) goto mn187;
            t185 = ((uint8_t)1);
            goto me186;
mn187:;
            t185 = ((uint8_t)0);
            goto me186;
mn189:;
            gm_trap("no match arm matched the scrutinee", gm_src0gtuh94b1fce, 986, 25);
me186:;
            gm_l22 = t185;
        }
        {
            gm_th2lqbo77pvsc t190;
            memset(&t190, 0, sizeof t190);
            gm_l23 = t190;
        }
        {
            void * t192 = gm_l20;
            uint8_t t193 = gm_f_no_writes_p1jjjfj(t192);
            uint8_t t194 = (uint8_t)!t193;
            if (!t194) goto ie191;
            {
                gm_b16_8 t196;
                GM_ST(void *, (uint8_t *)&t196, (void *)(gm_sr4qbrk6tlgaa));
                GM_ST(uintptr_t, (uint8_t *)&t196 + 8, (uintptr_t)(0));
                gm_l24 = t196;
            }
            {
                uint8_t t198 = gm_l22;
                if (!t198) goto ie197;
                {
                    void * t200 = gm_l0;
                    void * t201 = gm_l19;
                    gm_t5o0tpv5jka2l t202;
                    gm_f_place_addr_7ji15ot((void *)&t202, (void *)NULL, cx, NULL, t200, t201);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t202))[0] != 0)) {
                        gm_t5o0tpv5jka2l t203;
                        GM_ST(uint8_t, (uint8_t *)&t203 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t203 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t203 + 4, 0);
                        ((uint8_t *)&t203)[0] = 1;
                        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t203 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t202) + 8)));
                        F.ret = t203;
                        goto gm_fret;
                    }
                    gm_b16_8 t204 = GM_LD(gm_b16_8, (((uint8_t *)&t202) + 8));
                    gm_l24 = t204;
                }
                goto ix197;
ie197:;
                {
                    void * t206 = gm_l0;
                    void * t207 = gm_l19;
                    gm_t5o0tpv5jka2l t208;
                    gm_f_expr_porbc9t((void *)&t208, (void *)NULL, cx, NULL, t206, t207);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t208))[0] != 0)) {
                        gm_t5o0tpv5jka2l t209;
                        GM_ST(uint8_t, (uint8_t *)&t209 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t209 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t209 + 4, 0);
                        ((uint8_t *)&t209)[0] = 1;
                        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t209 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t208) + 8)));
                        F.ret = t209;
                        goto gm_fret;
                    }
                    gm_b16_8 t210 = GM_LD(gm_b16_8, (((uint8_t *)&t208) + 8));
                    gm_l24 = t210;
                }
ix197:;
            }
            {
                void * t211 = gm_l0;
                gm_b16_8 t212 = gm_f_fresh_tmp_6tadl69((void *)NULL, cx, NULL, t211);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l25 = t212;
            }
            {
                gm_b16_8 t213;
                GM_ST(void *, (uint8_t *)&t213, (void *)(gm_sr4qbrk6tlgaa));
                GM_ST(uintptr_t, (uint8_t *)&t213 + 8, (uintptr_t)(0));
                gm_l26 = t213;
            }
            {
                gm_b16_8 t214 = gm_l24;
                gm_l27 = t214;
            }
            {
                uint8_t t216 = gm_l22;
                if (!t216) goto ie215;
                {
                    gm_b16_8 t218;
                    GM_ST(void *, (uint8_t *)&t218, (void *)(gm_sm3nel32emjc9));
                    GM_ST(uintptr_t, (uint8_t *)&t218 + 8, (uintptr_t)(9));
                    gm_l26 = t218;
                }
                {
                    gm_b16_8 t219 = gm_l24;
                    gm_tnpifv20irnph t220;
                    GM_ST(gm_b16_8, ((uint8_t *)&t220 + 0), t219);
                    gm_tabb710d9mq2e t221;
                    gm_f_format_iq6v6ia((void *)&t221, (void *)NULL, cx, NULL, t220);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t221))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t221) + 8, gm_src0gtuh94b1fce, 1000, 26);
                    gm_b16_8 t222 = GM_LD(gm_b16_8, (((uint8_t *)&t221) + 8));
                    gm_b16_8 t223 = GM_LD(gm_b16_8, ((uint8_t *)&t222));
                    gm_l27 = t223;
                }
                goto ix215;
ie215:;
                {
                    void * t225 = gm_l0;
                    void * t226 = GM_LD(void *, (((uint8_t *)t225) + 0));
                    void * t227 = (void *)(((uint8_t *)t226) + 8);
                    void * t228 = gm_l19;
                    gm_t88rcucg1b7uk t229 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t228) + 0));
                    gm_b16_8 t230 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t227, t229);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tnpifv20irnph t231;
                    GM_ST(gm_b16_8, ((uint8_t *)&t231 + 0), t230);
                    gm_tabb710d9mq2e t232;
                    gm_f_format_bofepup((void *)&t232, (void *)NULL, cx, NULL, t231);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t232))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t232) + 8, gm_src0gtuh94b1fce, 1002, 27);
                    gm_b16_8 t233 = GM_LD(gm_b16_8, (((uint8_t *)&t232) + 8));
                    gm_b16_8 t234 = GM_LD(gm_b16_8, ((uint8_t *)&t233));
                    gm_l26 = t234;
                }
ix215:;
            }
            {
                void * t235 = gm_l0;
                gm_tfeab279c16rr t236;
                gm_b16_8 t237 = gm_l26;
                GM_ST(gm_b16_8, (uint8_t *)&t236 + 0, t237);
                gm_b16_8 t238 = gm_l25;
                GM_ST(gm_b16_8, (uint8_t *)&t236 + 16, t238);
                gm_b16_8 t239 = gm_l27;
                GM_ST(gm_b16_8, (uint8_t *)&t236 + 32, t239);
                gm_b0_1 t240 = gm_f_linef_g6n88t2((void *)NULL, cx, NULL, t235, &t236);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                gm_b16_8 t241 = gm_l25;
                GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l23)) + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l23)) + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l23)) + 20, 0);
                ((uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l23)))[16] = 1;
                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l23)) + 0, t241);
            }
            goto ix191;
ie191:;
ix191:;
        }
        {
            void * t242 = gm_l0;
            void * t243 = gm_l20;
            gm_t5o0tpv5jka2l t244;
            gm_f_expr_porbc9t((void *)&t244, (void *)NULL, cx, NULL, t242, t243);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t244))[0] != 0)) {
                gm_t5o0tpv5jka2l t245;
                GM_ST(uint8_t, (uint8_t *)&t245 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t245 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t245 + 4, 0);
                ((uint8_t *)&t245)[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t245 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t244) + 8)));
                F.ret = t245;
                goto gm_fret;
            }
            gm_b16_8 t246 = GM_LD(gm_b16_8, (((uint8_t *)&t244) + 8));
            gm_l28 = t246;
        }
        {
            gm_b16_8 t247;
            uint8_t t249 = gm_l21;
            if (!t249) goto ie248;
            void * t251 = gm_l0;
            void * t252 = gm_l20;
            gm_t88rcucg1b7uk t253 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t252) + 0));
            gm_b16_8 t254 = gm_l28;
            gm_b16_8 t255 = gm_f_bound_n4p0vpb((void *)NULL, cx, NULL, t251, t253, t254);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t256 = t255;
            t247 = t256;
            goto ix248;
ie248:;
            gm_b16_8 t258 = gm_l28;
            gm_tnpifv20irnph t259;
            GM_ST(gm_b16_8, ((uint8_t *)&t259 + 0), t258);
            gm_tabb710d9mq2e t260;
            gm_f_format_6df13cd((void *)&t260, (void *)NULL, cx, NULL, t259);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t260))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t260) + 8, gm_src0gtuh94b1fce, 1008, 64);
            gm_b16_8 t261 = GM_LD(gm_b16_8, (((uint8_t *)&t260) + 8));
            gm_b16_8 t262 = t261;
            gm_b16_8 t263 = GM_LD(gm_b16_8, ((uint8_t *)&t262));
            t247 = t263;
ix248:;
            gm_l29 = t247;
        }
        {
            void * t264 = gm_l0;
            void * t265 = gm_l0;
            void * t266 = GM_LD(void *, (((uint8_t *)t265) + 0));
            void * t267 = (void *)(((uint8_t *)t266) + 8);
            gm_t88rcucg1b7uk t268 = gm_f_usize_ty_9mijhtc((void *)NULL, cx, NULL, t267);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t269 = gm_l29;
            gm_b16_8 t270 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t264, t268, t269);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l30 = t270;
        }
        {
            void * t271 = gm_l0;
            void * t272 = gm_l1;
            gm_b16_8 t273 = gm_f_loc_u9c0aqb((void *)NULL, cx, NULL, t271, t272);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l31 = t273;
        }
        {
            void * t274 = gm_l0;
            void * t275 = GM_LD(void *, (((uint8_t *)t274) + 0));
            void * t276 = (void *)(((uint8_t *)t275) + 8);
            void * t277 = gm_l19;
            gm_t88rcucg1b7uk t278 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t277) + 0));
            gm_t53at80pfdus3 t279;
            gm_f_kind_ghvav22((void *)&t279, t276, GM_CONV(gm_csguankko4lmhd, t278));
            uint8_t t286 = GM_LD(uint8_t, (((uint8_t *)&t279) + 0));
            switch (t286) { case ((uint8_t)0ULL): goto ma284; case ((uint8_t)1ULL): goto ma284; case ((uint8_t)2ULL): goto ma284; case ((uint8_t)3ULL): goto ma284; case ((uint8_t)4ULL): goto ma284; case ((uint8_t)5ULL): goto ma284; case ((uint8_t)6ULL): goto ma284; case ((uint8_t)7ULL): goto ma284; case ((uint8_t)8ULL): goto ma284; case ((uint8_t)9ULL): goto ma283; case ((uint8_t)10ULL): goto ma282; case ((uint8_t)11ULL): goto ma281; case ((uint8_t)12ULL): goto ma284; case ((uint8_t)13ULL): goto ma284; case ((uint8_t)14ULL): goto ma284; case ((uint8_t)15ULL): goto ma284; case ((uint8_t)16ULL): goto ma284; case ((uint8_t)17ULL): goto ma284; case ((uint8_t)18ULL): goto ma284; case ((uint8_t)19ULL): goto ma284; case ((uint8_t)20ULL): goto ma284; case ((uint8_t)21ULL): goto ma284; case ((uint8_t)22ULL): goto ma284; case ((uint8_t)23ULL): goto ma284; case ((uint8_t)24ULL): goto ma284; case ((uint8_t)25ULL): goto ma284; case ((uint8_t)26ULL): goto ma284; default: goto ma284; }
ma281:;
            uint8_t t288 = GM_LD(uint8_t, (((uint8_t *)&t279) + 0));
            if (t288 != ((uint8_t)11ULL)) goto mn287;
            uint64_t t289 = GM_LD(uint64_t, ((((uint8_t *)&t279) + 8) + 0));
            gm_l32 = t289;
            gm_t88rcucg1b7uk t290 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t279) + 8) + 8));
            gm_l33 = t290;
            {
                void * t292 = gm_l0;
                void * t293 = GM_LD(void *, (((uint8_t *)t292) + 0));
                void * t294 = (void *)(((uint8_t *)t293) + 8);
                gm_t88rcucg1b7uk t295 = gm_l33;
                uint64_t t296 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t294, t295);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l34 = t296;
            }
            {
                gm_b16_8 t297;
                GM_ST(void *, (uint8_t *)&t297, (void *)(gm_sr4qbrk6tlgaa));
                GM_ST(uintptr_t, (uint8_t *)&t297 + 8, (uintptr_t)(0));
                gm_l35 = t297;
            }
            {
                if (!((((uint8_t *)&gm_l23))[16] != 0)) goto ie298;
                gm_b16_8 t299 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l23));
                gm_l36 = t299;
                {
                    gm_b16_8 t301 = gm_l36;
                    gm_l35 = t301;
                }
                goto ix298;
ie298:;
                {
                    void * t303 = gm_l0;
                    void * t304 = gm_l19;
                    gm_t5o0tpv5jka2l t305;
                    gm_f_place_addr_7ji15ot((void *)&t305, (void *)NULL, cx, NULL, t303, t304);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t305))[0] != 0)) {
                        gm_t5o0tpv5jka2l t306;
                        GM_ST(uint8_t, (uint8_t *)&t306 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t306 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t306 + 4, 0);
                        ((uint8_t *)&t306)[0] = 1;
                        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t306 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t305) + 8)));
                        F.ret = t306;
                        goto gm_fret;
                    }
                    gm_b16_8 t307 = GM_LD(gm_b16_8, (((uint8_t *)&t305) + 8));
                    gm_l35 = t307;
                }
ix298:;
            }
            {
                void * t308 = gm_l0;
                uint8_t t309 = gm_l21;
                gm_b16_8 t310 = gm_l30;
                uint64_t t311 = gm_l32;
                gm_t4ce0jd3tdro4 t312;
                GM_ST(uint64_t, ((uint8_t *)&t312 + 0), t311);
                gm_tabb710d9mq2e t313;
                gm_f_format_1amnh72((void *)&t313, (void *)NULL, cx, NULL, t312);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t313))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t313) + 8, gm_src0gtuh94b1fce, 1020, 48);
                gm_b16_8 t314 = GM_LD(gm_b16_8, (((uint8_t *)&t313) + 8));
                gm_b16_8 t315 = GM_LD(gm_b16_8, ((uint8_t *)&t314));
                gm_b16_8 t316 = gm_l31;
                gm_b0_1 t317 = gm_f_index_check_m2p0u1k((void *)NULL, cx, NULL, t308, t309, t310, t315, t316);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                gm_tftksr4op114e t318;
                gm_b16_8 t319 = gm_l35;
                GM_ST(gm_b16_8, (uint8_t *)&t318 + 0, t319);
                gm_b16_8 t320 = gm_l30;
                GM_ST(gm_b16_8, (uint8_t *)&t318 + 16, t320);
                uint64_t t321 = gm_l34;
                GM_ST(uint64_t, (uint8_t *)&t318 + 32, t321);
                gm_tabb710d9mq2e t322;
                gm_f_format_avab68e((void *)&t322, (void *)NULL, cx, NULL, &t318);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t322))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t322) + 8, gm_src0gtuh94b1fce, 1021, 28);
                gm_b16_8 t323 = GM_LD(gm_b16_8, (((uint8_t *)&t322) + 8));
                gm_b16_8 t324 = GM_LD(gm_b16_8, ((uint8_t *)&t323));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
                ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t324);
                goto gm_fdone;
            }
            goto me280;
mn287:;
ma282:;
            uint8_t t326 = GM_LD(uint8_t, (((uint8_t *)&t279) + 0));
            if (t326 != ((uint8_t)10ULL)) goto mn325;
            gm_t88rcucg1b7uk t327 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t279) + 8) + 4));
            gm_l37 = t327;
            {
                void * t329 = gm_l0;
                void * t330 = GM_LD(void *, (((uint8_t *)t329) + 0));
                void * t331 = (void *)(((uint8_t *)t330) + 8);
                gm_t88rcucg1b7uk t332 = gm_l37;
                uint64_t t333 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t331, t332);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l38 = t333;
            }
            {
                gm_b16_8 t334;
                GM_ST(void *, (uint8_t *)&t334, (void *)(gm_sr4qbrk6tlgaa));
                GM_ST(uintptr_t, (uint8_t *)&t334 + 8, (uintptr_t)(0));
                gm_l39 = t334;
            }
            {
                if (!((((uint8_t *)&gm_l23))[16] != 0)) goto ie335;
                gm_b16_8 t336 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l23));
                gm_l40 = t336;
                {
                    void * t338 = gm_l0;
                    void * t339 = gm_l19;
                    gm_t88rcucg1b7uk t340 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t339) + 0));
                    gm_b16_8 t341 = gm_l40;
                    gm_b16_8 t342 = gm_f_addr_of_hiaqkn0((void *)NULL, cx, NULL, t338, t340, t341);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l39 = t342;
                }
                goto ix335;
ie335:;
                {
                    void * t344 = gm_l0;
                    void * t345 = gm_l19;
                    gm_t5o0tpv5jka2l t346;
                    gm_f_place_addr_7ji15ot((void *)&t346, (void *)NULL, cx, NULL, t344, t345);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t346))[0] != 0)) {
                        gm_t5o0tpv5jka2l t347;
                        GM_ST(uint8_t, (uint8_t *)&t347 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t347 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t347 + 4, 0);
                        ((uint8_t *)&t347)[0] = 1;
                        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t347 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t346) + 8)));
                        F.ret = t347;
                        goto gm_fret;
                    }
                    gm_b16_8 t348 = GM_LD(gm_b16_8, (((uint8_t *)&t346) + 8));
                    gm_l39 = t348;
                }
ix335:;
            }
            {
                void * t349 = gm_l0;
                gm_b16_8 t350 = gm_l39;
                gm_b16_8 t351 = gm_f_slice_len_a3383br((void *)NULL, cx, NULL, t349, t350);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l41 = t351;
            }
            {
                void * t352 = gm_l0;
                void * t353 = gm_l0;
                void * t354 = GM_LD(void *, (((uint8_t *)t353) + 0));
                void * t355 = (void *)(((uint8_t *)t354) + 8);
                gm_t88rcucg1b7uk t356 = gm_f_usize_ty_9mijhtc((void *)NULL, cx, NULL, t355);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_b16_8 t357 = gm_l41;
                gm_b16_8 t358 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t352, t356, t357);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l42 = t358;
            }
            {
                void * t359 = gm_l0;
                uint8_t t360 = gm_l21;
                gm_b16_8 t361 = gm_l30;
                gm_b16_8 t362 = gm_l42;
                gm_b16_8 t363 = gm_l31;
                gm_b0_1 t364 = gm_f_index_check_m2p0u1k((void *)NULL, cx, NULL, t359, t360, t361, t362, t363);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                gm_b16_8 t365 = gm_l39;
                gm_b16_8 t366 = gm_f_slice_ptr_hojfgr5((void *)NULL, cx, NULL, t365);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l43 = t366;
            }
            {
                gm_tftksr4op114e t367;
                gm_b16_8 t368 = gm_l43;
                GM_ST(gm_b16_8, (uint8_t *)&t367 + 0, t368);
                gm_b16_8 t369 = gm_l30;
                GM_ST(gm_b16_8, (uint8_t *)&t367 + 16, t369);
                uint64_t t370 = gm_l38;
                GM_ST(uint64_t, (uint8_t *)&t367 + 32, t370);
                gm_tabb710d9mq2e t371;
                gm_f_format_8sn6h96((void *)&t371, (void *)NULL, cx, NULL, &t367);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t371))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t371) + 8, gm_src0gtuh94b1fce, 1036, 28);
                gm_b16_8 t372 = GM_LD(gm_b16_8, (((uint8_t *)&t371) + 8));
                gm_b16_8 t373 = GM_LD(gm_b16_8, ((uint8_t *)&t372));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
                ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t373);
                goto gm_fdone;
            }
            goto me280;
mn325:;
ma283:;
            uint8_t t375 = GM_LD(uint8_t, (((uint8_t *)&t279) + 0));
            if (t375 != ((uint8_t)9ULL)) goto mn374;
            gm_t88rcucg1b7uk t376 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t279) + 8) + 4));
            gm_l44 = t376;
            {
                void * t378 = gm_l0;
                void * t379 = GM_LD(void *, (((uint8_t *)t378) + 0));
                void * t380 = (void *)(((uint8_t *)t379) + 8);
                gm_t88rcucg1b7uk t381 = gm_l44;
                uint64_t t382 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t380, t381);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l45 = t382;
            }
            {
                gm_b16_8 t383;
                GM_ST(void *, (uint8_t *)&t383, (void *)(gm_sr4qbrk6tlgaa));
                GM_ST(uintptr_t, (uint8_t *)&t383 + 8, (uintptr_t)(0));
                gm_l46 = t383;
            }
            {
                if (!((((uint8_t *)&gm_l23))[16] != 0)) goto ie384;
                gm_b16_8 t385 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l23));
                gm_l47 = t385;
                {
                    gm_b16_8 t387 = gm_l47;
                    gm_l46 = t387;
                }
                goto ix384;
ie384:;
                {
                    void * t389 = gm_l0;
                    void * t390 = gm_l19;
                    gm_t5o0tpv5jka2l t391;
                    gm_f_expr_porbc9t((void *)&t391, (void *)NULL, cx, NULL, t389, t390);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t391))[0] != 0)) {
                        gm_t5o0tpv5jka2l t392;
                        GM_ST(uint8_t, (uint8_t *)&t392 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t392 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t392 + 4, 0);
                        ((uint8_t *)&t392)[0] = 1;
                        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t392 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t391) + 8)));
                        F.ret = t392;
                        goto gm_fret;
                    }
                    gm_b16_8 t393 = GM_LD(gm_b16_8, (((uint8_t *)&t391) + 8));
                    gm_l46 = t393;
                }
ix384:;
            }
            {
                gm_tftksr4op114e t394;
                gm_b16_8 t395 = gm_l46;
                GM_ST(gm_b16_8, (uint8_t *)&t394 + 0, t395);
                gm_b16_8 t396 = gm_l30;
                GM_ST(gm_b16_8, (uint8_t *)&t394 + 16, t396);
                uint64_t t397 = gm_l45;
                GM_ST(uint64_t, (uint8_t *)&t394 + 32, t397);
                gm_tabb710d9mq2e t398;
                gm_f_format_8sn6h96((void *)&t398, (void *)NULL, cx, NULL, &t394);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t398))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t398) + 8, gm_src0gtuh94b1fce, 1046, 28);
                gm_b16_8 t399 = GM_LD(gm_b16_8, (((uint8_t *)&t398) + 8));
                gm_b16_8 t400 = GM_LD(gm_b16_8, ((uint8_t *)&t399));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
                ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t400);
                goto gm_fdone;
            }
            goto me280;
mn374:;
ma284:;
            void * t402 = gm_l1;
            gm_tl874t2atnlh3 t403 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t402) + 72));
            void * t404 = gm_l0;
            void * t405 = GM_LD(void *, (((uint8_t *)t404) + 0));
            void * t406 = (void *)(((uint8_t *)t405) + 8);
            void * t407 = gm_l19;
            gm_t88rcucg1b7uk t408 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t407) + 0));
            gm_b16_8 t409 = gm_f_show_5qbi623((void *)NULL, cx, NULL, t406, t408);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tnpifv20irnph t410;
            GM_ST(gm_b16_8, ((uint8_t *)&t410 + 0), t409);
            gm_tabb710d9mq2e t411;
            gm_f_format_jha63mc((void *)&t411, (void *)NULL, cx, NULL, t410);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t411))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t411) + 8, gm_src0gtuh94b1fce, 1048, 50);
            gm_b16_8 t412 = GM_LD(gm_b16_8, (((uint8_t *)&t411) + 8));
            gm_b16_8 t413 = GM_LD(gm_b16_8, ((uint8_t *)&t412));
            gm_t08olq97rscn3 t414;
            gm_f_unsupported_531q51r((void *)&t414, (void *)NULL, cx, NULL, t403, t413);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 1;
            GM_ST(gm_t08olq97rscn3, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t414);
            goto gm_fdone;
            goto me280;
mn401:;
ma285:;
            gm_trap("no match arm matched the scrutinee", gm_src0gtuh94b1fce, 1011, 13);
me280:;
        }
        goto me3;
mn173:;
ma11:;
        {
            void * t417 = gm_l0;
            void * t418 = gm_l1;
            gm_t5o0tpv5jka2l t419;
            gm_f_expr_porbc9t((void *)&t419, (void *)NULL, cx, NULL, t417, t418);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t419))[0] != 0)) {
                gm_t5o0tpv5jka2l t420;
                GM_ST(uint8_t, (uint8_t *)&t420 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t420 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t420 + 4, 0);
                ((uint8_t *)&t420)[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t420 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t419) + 8)));
                F.ret = t420;
                goto gm_fret;
            }
            gm_b16_8 t421 = GM_LD(gm_b16_8, (((uint8_t *)&t419) + 8));
            gm_l48 = t421;
        }
        {
            void * t422 = gm_l0;
            void * t423 = gm_l1;
            gm_t88rcucg1b7uk t424 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t423) + 0));
            gm_b16_8 t425 = gm_l48;
            gm_b16_8 t426 = gm_f_addr_of_hiaqkn0((void *)NULL, cx, NULL, t422, t424, t425);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
            ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t426);
            goto gm_fdone;
        }
        goto me3;
mn415:;
ma12:;
        gm_trap("no match arm matched the scrutinee", gm_src0gtuh94b1fce, 951, 5);
me3:;
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
extern __attribute__((aligned(4))) uint64_t gm_f_ptr_bytes_eocneng(void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_res_is_err_11p8qlp(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2) {
    struct gm_fr_res_is_err_11p8qlp F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* addr */
    uint64_t gm_l3; /* off */
    GM_STACK_CHECK(&F, 241u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        uint64_t t2 = (uintptr_t)((uint64_t)0ULL);
        void * t3 = gm_l0;
        void * t4 = GM_LD(void *, (((uint8_t *)t3) + 0));
        void * t5 = (void *)(((uint8_t *)t4) + 8);
        gm_t88rcucg1b7uk t6 = gm_l1;
        gm_b16_8 t7 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t5, t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint64_t t8 = GM_LD(uintptr_t, ((uint8_t *)&t7) + 8);
        if (t2 >= (uintptr_t)(t8)) gm_trap_index(t2, t8, gm_src0gtuh94b1fce, 217, 12);
        uint64_t t9 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t7)) + t2 * 8));
        gm_l3 = t9;
    }
    {
        gm_t3877pk7lv748 t10;
        gm_b16_8 t11 = gm_l2;
        GM_ST(gm_b16_8, (uint8_t *)&t10 + 0, t11);
        uint64_t t12 = gm_l3;
        GM_ST(uint64_t, (uint8_t *)&t10 + 16, t12);
        gm_tabb710d9mq2e t13;
        gm_f_format_c2otgb4((void *)&t13, (void *)NULL, cx, NULL, &t10);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t13))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t13) + 8, gm_src0gtuh94b1fce, 218, 12);
        gm_b16_8 t14 = GM_LD(gm_b16_8, (((uint8_t *)&t13) + 8));
        gm_b16_8 t15 = GM_LD(gm_b16_8, ((uint8_t *)&t14));
        uint8_t t16 = 0;
        F.ret = t15;
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
__attribute__((aligned(4))) void gm_f_return_expr_ts5pbp2(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint32_t a2, void * a3) {
    struct gm_fr_return_expr_ts5pbp2 F;
    void * gm_l0; /* fg */
    void * gm_l1; /* e */
    uint32_t gm_l2; /* levels */
    void * gm_l3; /* value */
    gm_t88rcucg1b7uk gm_l4; /* t */
    gm_b16_8 gm_l5; /* ct */
    gm_b16_8 gm_l6; /* slot */
    gm_t88rcucg1b7uk gm_l7; /* ret */
    gm_b16_8 gm_l8; /* is_err */
    gm_b16_8 gm_l9; /* a */
    gm_b16_8 gm_l10; /* flag */
    gm_b16_8 gm_l11; /* fr */
    gm_b16_8 gm_l12; /* addr */
    gm_t88rcucg1b7uk gm_l13; /* ret */
    gm_b16_8 gm_l14; /* v */
    gm_b16_8 gm_l15; /* addr */
    gm_b16_8 gm_l16; /* is_err */
    gm_b16_8 gm_l17; /* a */
    gm_b16_8 gm_l18; /* flag */
    gm_b16_8 gm_l19; /* slot */
    gm_b16_8 gm_l20; /* fr */
    GM_STACK_CHECK(&F, 2875u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l1;
        gm_t88rcucg1b7uk t3 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t2) + 0));
        gm_l4 = t3;
    }
    {
        void * t5 = gm_l0;
        void * t6 = gm_l3;
        uint8_t t7 = gm_f_builds_in_place_ae1b90u((void *)NULL, cx, NULL, t5, t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t7) goto ie4;
        {
            void * t9 = gm_l0;
            void * t10 = GM_LD(void *, (((uint8_t *)t9) + 0));
            void * t11 = (void *)(((uint8_t *)t10) + 8);
            void * t12 = gm_l3;
            gm_t88rcucg1b7uk t13 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t12) + 0));
            gm_b16_8 t14 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t11, t13);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l5 = t14;
        }
        {
            uint32_t t16 = gm_l2;
            uint8_t t17 = ((t16 == ((uint32_t)0ULL)));
            if (!t17) goto ie15;
            void * t18 = gm_l0;
            uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)t18) + 298));
            uint8_t t20 = (uint8_t)!t19;
            if (!t20) goto ie15;
            void * t21 = gm_l0;
            uint8_t t22 = gm_f_no_defers_hci755r(t21);
            if (!t22) goto ie15;
            void * t23 = gm_l0;
            void * t24 = GM_LD(void *, (((uint8_t *)t23) + 0));
            void * t25 = (void *)(((uint8_t *)t24) + 8);
            void * t26 = gm_l0;
            void * t27 = GM_LD(void *, (((uint8_t *)t26) + 176));
            gm_t88rcucg1b7uk t28 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t27) + 64));
            uint8_t t29 = gm_f_by_ref_fi0o860((void *)NULL, cx, NULL, t25, t28);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!t29) goto ie15;
            {
                void * t31 = gm_l0;
                void * t32 = gm_l3;
                gm_b16_8 t33 = gm_l5;
                gm_tnpifv20irnph t34;
                GM_ST(gm_b16_8, ((uint8_t *)&t34 + 0), t33);
                gm_tabb710d9mq2e t35;
                gm_f_format_9c0uitb((void *)&t35, (void *)NULL, cx, NULL, t34);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t35))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t35) + 8, gm_src0gtuh94b1fce, 445, 37);
                gm_b16_8 t36 = GM_LD(gm_b16_8, (((uint8_t *)&t35) + 8));
                gm_b16_8 t37 = GM_LD(gm_b16_8, ((uint8_t *)&t36));
                gm_tnrltbc61i6v1 t38;
                GM_ST(uint8_t, (uint8_t *)&t38 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t38 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t38 + 4, 0);
                GM_ST(uint32_t, (uint8_t *)&t38 + 8, 0);
                GM_ST(uint8_t, ((uint8_t *)&t38 + 0), ((uint8_t)1ULL));
                gm_tqgtd5gtgj65s t39;
                gm_f_expr_into_dn9kavo((void *)&t39, (void *)NULL, cx, NULL, t31, t32, t37, t38);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t39))[0] != 0)) {
                    gm_t5o0tpv5jka2l t40;
                    GM_ST(uint8_t, (uint8_t *)&t40 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t40 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t40 + 4, 0);
                    ((uint8_t *)&t40)[0] = 1;
                    GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t40 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t39) + 8)));
                    F.ret = t40;
                    goto gm_fret;
                }
                gm_b0_1 t41 = GM_LD(gm_b0_1, (((uint8_t *)&t39) + 8));
            }
            {
                void * t42 = gm_l0;
                gm_b16_8 t43;
                GM_ST(void *, (uint8_t *)&t43, (void *)(gm_scaaj480d4i26));
                GM_ST(uintptr_t, (uint8_t *)&t43 + 8, (uintptr_t)(8));
                gm_tf5srh4mp4b3i t44;
                memset(&t44, 0, sizeof t44);
                gm_b0_1 t45 = gm_f_jump_pvnkvme((void *)NULL, cx, NULL, t42, t43, t44);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t46 = gm_l0;
                gm_t88rcucg1b7uk t47 = gm_l4;
                gm_b16_8 t48 = gm_f_zero_tlk13aa((void *)NULL, cx, NULL, t46, t47);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
                ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t48);
                goto gm_fdone;
            }
            goto ix15;
ie15:;
ix15:;
        }
        {
            void * t49 = gm_l0;
            uint32_t t50 = gm_l2;
            gm_b16_8 t51;
            GM_ST(void *, (uint8_t *)&t51, (void *)(gm_s1k3lfare8sei));
            GM_ST(uintptr_t, (uint8_t *)&t51 + 8, (uintptr_t)(3));
            gm_b16_8 t52 = gm_f_frame_field_nj8m22n((void *)NULL, cx, NULL, t49, t50, t51);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l6 = t52;
        }
        {
            void * t53 = gm_l0;
            void * t54 = gm_l3;
            gm_b16_8 t55 = gm_l6;
            gm_tnrltbc61i6v1 t56;
            GM_ST(uint8_t, (uint8_t *)&t56 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t56 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t56 + 4, 0);
            GM_ST(uint32_t, (uint8_t *)&t56 + 8, 0);
            GM_ST(uint8_t, ((uint8_t *)&t56 + 0), ((uint8_t)0ULL));
            gm_tqgtd5gtgj65s t57;
            gm_f_expr_into_dn9kavo((void *)&t57, (void *)NULL, cx, NULL, t53, t54, t55, t56);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t57))[0] != 0)) {
                gm_t5o0tpv5jka2l t58;
                GM_ST(uint8_t, (uint8_t *)&t58 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t58 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t58 + 4, 0);
                ((uint8_t *)&t58)[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t58 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t57) + 8)));
                F.ret = t58;
                goto gm_fret;
            }
            gm_b0_1 t59 = GM_LD(gm_b0_1, (((uint8_t *)&t57) + 8));
        }
        {
            void * t60 = gm_l0;
            uint32_t t61 = gm_l2;
            gm_t88rcucg1b7uk t62 = gm_f_target_ret_q7qjs0g((void *)NULL, cx, NULL, t60, t61);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l7 = t62;
        }
        {
            gm_b16_8 t63;
            GM_ST(void *, (uint8_t *)&t63, (void *)(gm_sicgmpca63jln));
            GM_ST(uintptr_t, (uint8_t *)&t63 + 8, (uintptr_t)(1));
            gm_l8 = t63;
        }
        {
            void * t65 = gm_l0;
            void * t66 = GM_LD(void *, (((uint8_t *)t65) + 0));
            void * t67 = (void *)(((uint8_t *)t66) + 8);
            gm_t88rcucg1b7uk t68 = gm_l7;
            gm_t53at80pfdus3 t69;
            gm_f_kind_ghvav22((void *)&t69, t67, GM_CONV(gm_csguankko4lmhd, t68));
            uint8_t t70;
            uint8_t t73 = GM_LD(uint8_t, (((uint8_t *)&t69) + 0));
            if (t73 != ((uint8_t)14ULL)) goto mn72;
            t70 = ((uint8_t)1);
            goto me71;
mn72:;
            t70 = ((uint8_t)0);
            goto me71;
mn74:;
            gm_trap("no match arm matched the scrutinee", gm_src0gtuh94b1fce, 453, 12);
me71:;
            if (!t70) goto ie64;
            {
                gm_b16_8 t76 = gm_l6;
                gm_tnpifv20irnph t77;
                GM_ST(gm_b16_8, ((uint8_t *)&t77 + 0), t76);
                gm_tabb710d9mq2e t78;
                gm_f_format_4un19t8((void *)&t78, (void *)NULL, cx, NULL, t77);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t78))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t78) + 8, gm_src0gtuh94b1fce, 454, 18);
                gm_b16_8 t79 = GM_LD(gm_b16_8, (((uint8_t *)&t78) + 8));
                gm_l9 = t79;
            }
            {
                void * t80 = gm_l0;
                gm_t88rcucg1b7uk t81 = gm_l7;
                gm_b16_8 t82 = gm_l9;
                gm_b16_8 t83 = GM_LD(gm_b16_8, ((uint8_t *)&t82));
                gm_b16_8 t84 = gm_f_res_is_err_11p8qlp((void *)NULL, cx, NULL, t80, t81, t83);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l8 = t84;
            }
            goto ix64;
ie64:;
ix64:;
        }
        {
            void * t85 = gm_l0;
            void * t86 = gm_l0;
            void * t87 = GM_LD(void *, (((uint8_t *)t86) + 0));
            void * t88 = (void *)(((uint8_t *)t87) + 8);
            gm_t88rcucg1b7uk t89 = gm_f_bool_ty_kotvo8t((void *)NULL, cx, NULL, t88);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t90 = gm_l8;
            gm_b16_8 t91 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t85, t89, t90);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l10 = t91;
        }
        {
            void * t92 = gm_l0;
            uint32_t t93 = gm_l2;
            gm_tommf4j9upomc t94;
            gm_f_frame_up_g8486eu((void *)&t94, (void *)NULL, cx, NULL, t92, t93);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tommf4j9upomc t95 = t94;
            gm_l11 = GM_LD(gm_b16_8, (uint8_t *)&t95 + 0);
        }
        {
            void * t96 = gm_l0;
            gm_t9qkh0rsooc8d t97;
            GM_ST(uint8_t, ((uint8_t *)&t97 + 0), ((uint8_t)0ULL));
            gm_b16_8 t98 = gm_l11;
            gm_b16_8 t99;
            GM_ST(void *, (uint8_t *)&t99, (void *)(gm_sicgmpca63jln));
            GM_ST(uintptr_t, (uint8_t *)&t99 + 8, (uintptr_t)(1));
            gm_b16_8 t100 = gm_l10;
            gm_b0_1 t101 = gm_f_start_exit_m57dnn5((void *)NULL, cx, NULL, t96, t97, t98, t99, t100);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t102 = gm_l0;
            gm_t88rcucg1b7uk t103 = gm_l4;
            gm_b16_8 t104 = gm_f_zero_tlk13aa((void *)NULL, cx, NULL, t102, t103);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
            ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t104);
            goto gm_fdone;
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        uint32_t t106 = gm_l2;
        uint8_t t107 = ((t106 == ((uint32_t)0ULL)));
        if (!t107) goto ie105;
        void * t108 = gm_l0;
        void * t109 = GM_LD(void *, (((uint8_t *)t108) + 0));
        void * t110 = (void *)(((uint8_t *)t109) + 8);
        void * t111 = gm_l3;
        gm_t88rcucg1b7uk t112 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t111) + 0));
        uint8_t t113 = gm_f_by_ref_fi0o860((void *)NULL, cx, NULL, t110, t112);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t113) goto ie105;
        void * t114 = gm_l0;
        void * t115 = GM_LD(void *, (((uint8_t *)t114) + 0));
        void * t116 = (void *)(((uint8_t *)t115) + 8);
        void * t117 = gm_l0;
        void * t118 = GM_LD(void *, (((uint8_t *)t117) + 176));
        gm_t88rcucg1b7uk t119 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t118) + 64));
        uint8_t t120 = gm_f_by_ref_fi0o860((void *)NULL, cx, NULL, t116, t119);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t120) goto ie105;
        void * t121 = gm_l0;
        uint8_t t122 = GM_LD(uint8_t, (((uint8_t *)t121) + 298));
        uint8_t t123 = (uint8_t)!t122;
        if (!t123) goto ie105;
        void * t124 = gm_l0;
        uint8_t t125 = gm_f_no_defers_hci755r(t124);
        if (!t125) goto ie105;
        void * t126 = gm_l0;
        void * t127 = gm_l3;
        uint8_t t128 = gm_f_reads_place_nji55rm((void *)NULL, cx, NULL, t126, t127);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t128) goto ie105;
        {
            void * t130 = gm_l0;
            void * t131 = gm_l3;
            gm_t5o0tpv5jka2l t132;
            gm_f_place_addr_7ji15ot((void *)&t132, (void *)NULL, cx, NULL, t130, t131);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t132))[0] != 0)) {
                gm_t5o0tpv5jka2l t133;
                GM_ST(uint8_t, (uint8_t *)&t133 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t133 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t133 + 4, 0);
                ((uint8_t *)&t133)[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t133 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t132) + 8)));
                F.ret = t133;
                goto gm_fret;
            }
            gm_b16_8 t134 = GM_LD(gm_b16_8, (((uint8_t *)&t132) + 8));
            gm_l12 = t134;
        }
        {
            void * t135 = gm_l0;
            gm_t3877pk7lv748 t136;
            gm_b16_8 t137 = gm_l12;
            GM_ST(gm_b16_8, (uint8_t *)&t136 + 0, t137);
            void * t138 = gm_l0;
            void * t139 = GM_LD(void *, (((uint8_t *)t138) + 0));
            void * t140 = (void *)(((uint8_t *)t139) + 8);
            void * t141 = gm_l3;
            gm_t88rcucg1b7uk t142 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t141) + 0));
            uint64_t t143 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t140, t142);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint64_t, (uint8_t *)&t136 + 16, t143);
            gm_b0_1 t144 = gm_f_linef_iin7hfk((void *)NULL, cx, NULL, t135, &t136);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t145 = gm_l0;
            gm_b16_8 t146;
            GM_ST(void *, (uint8_t *)&t146, (void *)(gm_scaaj480d4i26));
            GM_ST(uintptr_t, (uint8_t *)&t146 + 8, (uintptr_t)(8));
            gm_tf5srh4mp4b3i t147;
            memset(&t147, 0, sizeof t147);
            gm_b0_1 t148 = gm_f_jump_pvnkvme((void *)NULL, cx, NULL, t145, t146, t147);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t149 = gm_l0;
            gm_t88rcucg1b7uk t150 = gm_l4;
            gm_b16_8 t151 = gm_f_zero_tlk13aa((void *)NULL, cx, NULL, t149, t150);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
            ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t151);
            goto gm_fdone;
        }
        goto ix105;
ie105:;
ix105:;
    }
    {
        void * t152 = gm_l0;
        uint32_t t153 = gm_l2;
        gm_t88rcucg1b7uk t154 = gm_f_target_ret_q7qjs0g((void *)NULL, cx, NULL, t152, t153);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l13 = t154;
    }
    {
        gm_b16_8 t155;
        GM_ST(void *, (uint8_t *)&t155, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t155 + 8, (uintptr_t)(0));
        gm_l14 = t155;
    }
    {
        void * t157 = gm_l0;
        void * t158 = GM_LD(void *, (((uint8_t *)t157) + 0));
        void * t159 = (void *)(((uint8_t *)t158) + 8);
        void * t160 = gm_l3;
        gm_t88rcucg1b7uk t161 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t160) + 0));
        uint8_t t162 = gm_f_by_ref_fi0o860((void *)NULL, cx, NULL, t159, t161);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t162) goto ie156;
        void * t163 = gm_l0;
        void * t164 = GM_LD(void *, (((uint8_t *)t163) + 0));
        void * t165 = (void *)(((uint8_t *)t164) + 8);
        gm_t88rcucg1b7uk t166 = gm_l13;
        gm_t53at80pfdus3 t167;
        gm_f_kind_ghvav22((void *)&t167, t165, GM_CONV(gm_csguankko4lmhd, t166));
        uint8_t t168;
        uint8_t t171 = GM_LD(uint8_t, (((uint8_t *)&t167) + 0));
        if (t171 != ((uint8_t)14ULL)) goto mn170;
        t168 = ((uint8_t)1);
        goto me169;
mn170:;
        t168 = ((uint8_t)0);
        goto me169;
mn172:;
        gm_trap("no match arm matched the scrutinee", gm_src0gtuh94b1fce, 472, 42);
me169:;
        uint8_t t173 = (uint8_t)!t168;
        if (!t173) goto ie156;
        uint8_t t174 = 0;
        uint8_t t175 = 0;
        void * t176 = gm_l3;
        uint8_t t177;
        uint8_t t180 = GM_LD(uint8_t, ((((uint8_t *)t176) + 8) + 0));
        if (t180 != ((uint8_t)15ULL)) goto mn179;
        t177 = ((uint8_t)1);
        goto me178;
mn179:;
        t177 = ((uint8_t)0);
        goto me178;
mn181:;
        gm_trap("no match arm matched the scrutinee", gm_src0gtuh94b1fce, 472, 80);
me178:;
        if (!t177) {
            void * t182 = gm_l3;
            uint8_t t183;
            uint8_t t186 = GM_LD(uint8_t, ((((uint8_t *)t182) + 8) + 0));
            if (t186 != ((uint8_t)16ULL)) goto mn185;
            t183 = ((uint8_t)1);
            goto me184;
mn185:;
            t183 = ((uint8_t)0);
            goto me184;
mn187:;
            gm_trap("no match arm matched the scrutinee", gm_src0gtuh94b1fce, 472, 107);
me184:;
            t175 = t183;
        } else t175 = 1;
        if (!t175) {
            void * t188 = gm_l3;
            uint8_t t189;
            uint8_t t192 = GM_LD(uint8_t, ((((uint8_t *)t188) + 8) + 0));
            if (t192 != ((uint8_t)18ULL)) goto mn191;
            t189 = ((uint8_t)1);
            goto me190;
mn191:;
            t189 = ((uint8_t)0);
            goto me190;
mn193:;
            gm_trap("no match arm matched the scrutinee", gm_src0gtuh94b1fce, 472, 134);
me190:;
            t174 = t189;
        } else t174 = 1;
        if (!t174) goto ie156;
        {
            void * t195 = gm_l0;
            void * t196 = gm_l3;
            gm_t5o0tpv5jka2l t197;
            gm_f_place_addr_7ji15ot((void *)&t197, (void *)NULL, cx, NULL, t195, t196);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t197))[0] != 0)) {
                gm_t5o0tpv5jka2l t198;
                GM_ST(uint8_t, (uint8_t *)&t198 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t198 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t198 + 4, 0);
                ((uint8_t *)&t198)[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t198 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t197) + 8)));
                F.ret = t198;
                goto gm_fret;
            }
            gm_b16_8 t199 = GM_LD(gm_b16_8, (((uint8_t *)&t197) + 8));
            gm_l15 = t199;
        }
        {
            gm_t6j6smj7qoctu t200;
            void * t201 = gm_l0;
            void * t202 = GM_LD(void *, (((uint8_t *)t201) + 0));
            void * t203 = (void *)(((uint8_t *)t202) + 8);
            void * t204 = gm_l3;
            gm_t88rcucg1b7uk t205 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t204) + 0));
            gm_b16_8 t206 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t203, t205);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t200 + 0, t206);
            gm_b16_8 t207 = gm_l15;
            GM_ST(gm_b16_8, (uint8_t *)&t200 + 16, t207);
            gm_tabb710d9mq2e t208;
            gm_f_format_8lfevbr((void *)&t208, (void *)NULL, cx, NULL, &t200);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t208))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t208) + 8, gm_src0gtuh94b1fce, 474, 13);
            gm_b16_8 t209 = GM_LD(gm_b16_8, (((uint8_t *)&t208) + 8));
            gm_b16_8 t210 = GM_LD(gm_b16_8, ((uint8_t *)&t209));
            gm_l14 = t210;
        }
        goto ix156;
ie156:;
        {
            void * t212 = gm_l0;
            void * t213 = gm_l3;
            gm_t5o0tpv5jka2l t214;
            gm_f_expr_porbc9t((void *)&t214, (void *)NULL, cx, NULL, t212, t213);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t214))[0] != 0)) {
                gm_t5o0tpv5jka2l t215;
                GM_ST(uint8_t, (uint8_t *)&t215 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t215 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t215 + 4, 0);
                ((uint8_t *)&t215)[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t215 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t214) + 8)));
                F.ret = t215;
                goto gm_fret;
            }
            gm_b16_8 t216 = GM_LD(gm_b16_8, (((uint8_t *)&t214) + 8));
            gm_l14 = t216;
        }
ix156:;
    }
    {
        gm_b16_8 t217;
        GM_ST(void *, (uint8_t *)&t217, (void *)(gm_sicgmpca63jln));
        GM_ST(uintptr_t, (uint8_t *)&t217 + 8, (uintptr_t)(1));
        gm_l16 = t217;
    }
    {
        void * t219 = gm_l0;
        void * t220 = GM_LD(void *, (((uint8_t *)t219) + 0));
        void * t221 = (void *)(((uint8_t *)t220) + 8);
        gm_t88rcucg1b7uk t222 = gm_l13;
        gm_t53at80pfdus3 t223;
        gm_f_kind_ghvav22((void *)&t223, t221, GM_CONV(gm_csguankko4lmhd, t222));
        uint8_t t224;
        uint8_t t227 = GM_LD(uint8_t, (((uint8_t *)&t223) + 0));
        if (t227 != ((uint8_t)14ULL)) goto mn226;
        t224 = ((uint8_t)1);
        goto me225;
mn226:;
        t224 = ((uint8_t)0);
        goto me225;
mn228:;
        gm_trap("no match arm matched the scrutinee", gm_src0gtuh94b1fce, 479, 8);
me225:;
        if (!t224) goto ie218;
        void * t229 = gm_l0;
        void * t230 = GM_LD(void *, (((uint8_t *)t229) + 0));
        void * t231 = (void *)(((uint8_t *)t230) + 8);
        void * t232 = gm_l3;
        gm_t88rcucg1b7uk t233 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t232) + 0));
        uint8_t t234 = gm_f_is_never_in5ftma(t231, GM_CONV(gm_csguankko4lmhd, t233));
        uint8_t t235 = (uint8_t)!t234;
        if (!t235) goto ie218;
        {
            void * t237 = gm_l0;
            void * t238 = gm_l3;
            gm_t88rcucg1b7uk t239 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t238) + 0));
            gm_b16_8 t240 = gm_l14;
            gm_b16_8 t241 = gm_f_addr_of_hiaqkn0((void *)NULL, cx, NULL, t237, t239, t240);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l17 = t241;
        }
        {
            void * t242 = gm_l0;
            gm_t88rcucg1b7uk t243 = gm_l13;
            gm_b16_8 t244 = gm_l17;
            gm_b16_8 t245 = gm_f_res_is_err_11p8qlp((void *)NULL, cx, NULL, t242, t243, t244);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l16 = t245;
        }
        goto ix218;
ie218:;
ix218:;
    }
    {
        void * t246 = gm_l0;
        void * t247 = gm_l0;
        void * t248 = GM_LD(void *, (((uint8_t *)t247) + 0));
        void * t249 = (void *)(((uint8_t *)t248) + 8);
        gm_t88rcucg1b7uk t250 = gm_f_bool_ty_kotvo8t((void *)NULL, cx, NULL, t249);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t251 = gm_l16;
        gm_b16_8 t252 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t246, t250, t251);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l18 = t252;
    }
    {
        void * t254 = gm_l0;
        void * t255 = GM_LD(void *, (((uint8_t *)t254) + 0));
        void * t256 = (void *)(((uint8_t *)t255) + 8);
        void * t257 = gm_l3;
        gm_t88rcucg1b7uk t258 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t257) + 0));
        uint8_t t259 = gm_f_is_never_in5ftma(t256, GM_CONV(gm_csguankko4lmhd, t258));
        uint8_t t260 = (uint8_t)!t259;
        if (!t260) goto ie253;
        {
            void * t262 = gm_l0;
            uint32_t t263 = gm_l2;
            gm_b16_8 t264;
            GM_ST(void *, (uint8_t *)&t264, (void *)(gm_s1k3lfare8sei));
            GM_ST(uintptr_t, (uint8_t *)&t264 + 8, (uintptr_t)(3));
            gm_b16_8 t265 = gm_f_frame_field_nj8m22n((void *)NULL, cx, NULL, t262, t263, t264);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l19 = t265;
        }
        {
            void * t266 = gm_l0;
            gm_t6j6smj7qoctu t267;
            gm_b16_8 t268 = gm_l19;
            GM_ST(gm_b16_8, (uint8_t *)&t267 + 0, t268);
            gm_b16_8 t269 = gm_l14;
            GM_ST(gm_b16_8, (uint8_t *)&t267 + 16, t269);
            gm_b0_1 t270 = gm_f_linef_uu8i6ue((void *)NULL, cx, NULL, t266, &t267);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix253;
ie253:;
ix253:;
    }
    {
        void * t271 = gm_l0;
        uint32_t t272 = gm_l2;
        gm_tommf4j9upomc t273;
        gm_f_frame_up_g8486eu((void *)&t273, (void *)NULL, cx, NULL, t271, t272);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tommf4j9upomc t274 = t273;
        gm_l20 = GM_LD(gm_b16_8, (uint8_t *)&t274 + 0);
    }
    {
        void * t275 = gm_l0;
        gm_t9qkh0rsooc8d t276;
        GM_ST(uint8_t, ((uint8_t *)&t276 + 0), ((uint8_t)0ULL));
        gm_b16_8 t277 = gm_l20;
        gm_b16_8 t278;
        GM_ST(void *, (uint8_t *)&t278, (void *)(gm_sicgmpca63jln));
        GM_ST(uintptr_t, (uint8_t *)&t278 + 8, (uintptr_t)(1));
        gm_b16_8 t279 = gm_l18;
        gm_b0_1 t280 = gm_f_start_exit_m57dnn5((void *)NULL, cx, NULL, t275, t276, t277, t278, t279);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t281 = gm_l0;
        gm_t88rcucg1b7uk t282 = gm_l4;
        gm_b16_8 t283 = gm_f_zero_tlk13aa((void *)NULL, cx, NULL, t281, t282);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t283);
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
__attribute__((aligned(4))) void gm_f_slice_expr_bln8glj(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, void * a3, void * a4, uint8_t a5) {
    struct gm_fr_slice_expr_bln8glj F;
    void * gm_l0; /* fg */
    void * gm_l1; /* e */
    void * gm_l2; /* base */
    void * gm_l3; /* lo */
    void * gm_l4; /* hi */
    uint8_t gm_l5; /* inclusive */
    gm_b16_8 gm_l6; /* ptr */
    gm_th2lqbo77pvsc gm_l7; /* len */
    uint64_t gm_l8; /* esz */
    uint64_t gm_l9; /* n */
    gm_t88rcucg1b7uk gm_l10; /* elem */
    gm_t88rcucg1b7uk gm_l11; /* elem */
    gm_b16_8 gm_l12; /* a */
    gm_b16_8 gm_l13; /* v */
    gm_b16_8 gm_l14; /* p */
    gm_b16_8 gm_l15; /* l */
    gm_t88rcucg1b7uk gm_l16; /* elem */
    gm_b16_8 gm_l17; /* v */
    void * gm_l18; /* tys */
    gm_b16_8 gm_l19; /* lo_v */
    void * gm_l20; /* x */
    gm_b16_8 gm_l21; /* v */
    gm_b16_8 gm_l22; /* bv */
    gm_b16_8 gm_l23; /* hi_v */
    void * gm_l24; /* x */
    gm_b16_8 gm_l25; /* v */
    gm_b16_8 gm_l26; /* bv */
    gm_b16_8 gm_l27; /* h */
    gm_b16_8 gm_l28; /* l */
    gm_b16_8 gm_l29; /* l */
    gm_b16_8 gm_l30; /* at */
    GM_STACK_CHECK(&F, 3580u);
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
        gm_b16_8 t2;
        GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(0));
        gm_l6 = t2;
    }
    {
        gm_th2lqbo77pvsc t3;
        memset(&t3, 0, sizeof t3);
        gm_l7 = t3;
    }
    {
        gm_l8 = ((uint64_t)0ULL);
    }
    {
        void * t4 = gm_l0;
        void * t5 = GM_LD(void *, (((uint8_t *)t4) + 0));
        void * t6 = (void *)(((uint8_t *)t5) + 8);
        void * t7 = gm_l2;
        gm_t88rcucg1b7uk t8 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t7) + 0));
        gm_t53at80pfdus3 t9;
        gm_f_kind_ghvav22((void *)&t9, t6, GM_CONV(gm_csguankko4lmhd, t8));
        uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)&t9) + 0));
        switch (t16) { case ((uint8_t)0ULL): goto ma14; case ((uint8_t)1ULL): goto ma14; case ((uint8_t)2ULL): goto ma14; case ((uint8_t)3ULL): goto ma14; case ((uint8_t)4ULL): goto ma14; case ((uint8_t)5ULL): goto ma14; case ((uint8_t)6ULL): goto ma14; case ((uint8_t)7ULL): goto ma14; case ((uint8_t)8ULL): goto ma14; case ((uint8_t)9ULL): goto ma13; case ((uint8_t)10ULL): goto ma12; case ((uint8_t)11ULL): goto ma11; case ((uint8_t)12ULL): goto ma14; case ((uint8_t)13ULL): goto ma14; case ((uint8_t)14ULL): goto ma14; case ((uint8_t)15ULL): goto ma14; case ((uint8_t)16ULL): goto ma14; case ((uint8_t)17ULL): goto ma14; case ((uint8_t)18ULL): goto ma14; case ((uint8_t)19ULL): goto ma14; case ((uint8_t)20ULL): goto ma14; case ((uint8_t)21ULL): goto ma14; case ((uint8_t)22ULL): goto ma14; case ((uint8_t)23ULL): goto ma14; case ((uint8_t)24ULL): goto ma14; case ((uint8_t)25ULL): goto ma14; case ((uint8_t)26ULL): goto ma14; default: goto ma14; }
ma11:;
        uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&t9) + 0));
        if (t18 != ((uint8_t)11ULL)) goto mn17;
        uint64_t t19 = GM_LD(uint64_t, ((((uint8_t *)&t9) + 8) + 0));
        gm_l9 = t19;
        gm_t88rcucg1b7uk t20 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t9) + 8) + 8));
        gm_l10 = t20;
        {
            void * t22 = gm_l0;
            void * t23 = gm_l2;
            gm_t5o0tpv5jka2l t24;
            gm_f_place_addr_7ji15ot((void *)&t24, (void *)NULL, cx, NULL, t22, t23);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t24))[0] != 0)) {
                gm_t5o0tpv5jka2l t25;
                GM_ST(uint8_t, (uint8_t *)&t25 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t25 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t25 + 4, 0);
                ((uint8_t *)&t25)[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t25 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t24) + 8)));
                F.ret = t25;
                goto gm_fret;
            }
            gm_b16_8 t26 = GM_LD(gm_b16_8, (((uint8_t *)&t24) + 8));
            gm_l6 = t26;
        }
        {
            uint64_t t27 = gm_l9;
            gm_t4ce0jd3tdro4 t28;
            GM_ST(uint64_t, ((uint8_t *)&t28 + 0), t27);
            gm_tabb710d9mq2e t29;
            gm_f_format_1amnh72((void *)&t29, (void *)NULL, cx, NULL, t28);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t29))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t29) + 8, gm_src0gtuh94b1fce, 1082, 19);
            gm_b16_8 t30 = GM_LD(gm_b16_8, (((uint8_t *)&t29) + 8));
            gm_b16_8 t31 = GM_LD(gm_b16_8, ((uint8_t *)&t30));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l7)) + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l7)) + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l7)) + 20, 0);
            ((uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l7)))[16] = 1;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l7)) + 0, t31);
        }
        {
            void * t32 = gm_l0;
            void * t33 = GM_LD(void *, (((uint8_t *)t32) + 0));
            void * t34 = (void *)(((uint8_t *)t33) + 8);
            gm_t88rcucg1b7uk t35 = gm_l10;
            uint64_t t36 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t34, t35);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l8 = t36;
        }
        goto me10;
mn17:;
ma12:;
        uint8_t t38 = GM_LD(uint8_t, (((uint8_t *)&t9) + 0));
        if (t38 != ((uint8_t)10ULL)) goto mn37;
        gm_t88rcucg1b7uk t39 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t9) + 8) + 4));
        gm_l11 = t39;
        {
            gm_b16_8 t41;
            GM_ST(void *, (uint8_t *)&t41, (void *)(gm_sr4qbrk6tlgaa));
            GM_ST(uintptr_t, (uint8_t *)&t41 + 8, (uintptr_t)(0));
            gm_l12 = t41;
        }
        {
            void * t43 = gm_l3;
            uint8_t t44 = gm_f_opt_no_writes_fjqh1hi(t43);
            if (!t44) goto ie42;
            void * t45 = gm_l4;
            uint8_t t46 = gm_f_opt_no_writes_fjqh1hi(t45);
            if (!t46) goto ie42;
            {
                void * t48 = gm_l0;
                void * t49 = gm_l2;
                gm_t5o0tpv5jka2l t50;
                gm_f_place_addr_7ji15ot((void *)&t50, (void *)NULL, cx, NULL, t48, t49);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t50))[0] != 0)) {
                    gm_t5o0tpv5jka2l t51;
                    GM_ST(uint8_t, (uint8_t *)&t51 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t51 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t51 + 4, 0);
                    ((uint8_t *)&t51)[0] = 1;
                    GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t51 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t50) + 8)));
                    F.ret = t51;
                    goto gm_fret;
                }
                gm_b16_8 t52 = GM_LD(gm_b16_8, (((uint8_t *)&t50) + 8));
                gm_l12 = t52;
            }
            goto ix42;
ie42:;
            {
                void * t54 = gm_l0;
                void * t55 = gm_l2;
                gm_t5o0tpv5jka2l t56;
                gm_f_expr_porbc9t((void *)&t56, (void *)NULL, cx, NULL, t54, t55);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t56))[0] != 0)) {
                    gm_t5o0tpv5jka2l t57;
                    GM_ST(uint8_t, (uint8_t *)&t57 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t57 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t57 + 4, 0);
                    ((uint8_t *)&t57)[0] = 1;
                    GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t57 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t56) + 8)));
                    F.ret = t57;
                    goto gm_fret;
                }
                gm_b16_8 t58 = GM_LD(gm_b16_8, (((uint8_t *)&t56) + 8));
                gm_l13 = t58;
            }
            {
                void * t59 = gm_l0;
                void * t60 = gm_l2;
                gm_t88rcucg1b7uk t61 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t60) + 0));
                gm_b16_8 t62 = gm_l13;
                gm_b16_8 t63 = gm_f_addr_of_hiaqkn0((void *)NULL, cx, NULL, t59, t61, t62);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l12 = t63;
            }
ix42:;
        }
        {
            gm_b16_8 t64 = gm_l12;
            gm_b16_8 t65 = gm_f_slice_ptr_hojfgr5((void *)NULL, cx, NULL, t64);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l14 = t65;
        }
        {
            void * t66 = gm_l0;
            gm_b16_8 t67 = gm_l12;
            gm_b16_8 t68 = gm_f_slice_len_a3383br((void *)NULL, cx, NULL, t66, t67);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l15 = t68;
        }
        {
            gm_b16_8 t69 = gm_l14;
            gm_tnpifv20irnph t70;
            GM_ST(gm_b16_8, ((uint8_t *)&t70 + 0), t69);
            gm_tabb710d9mq2e t71;
            gm_f_format_da6unq6((void *)&t71, (void *)NULL, cx, NULL, t70);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t71))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t71) + 8, gm_src0gtuh94b1fce, 1097, 19);
            gm_b16_8 t72 = GM_LD(gm_b16_8, (((uint8_t *)&t71) + 8));
            gm_b16_8 t73 = GM_LD(gm_b16_8, ((uint8_t *)&t72));
            gm_l6 = t73;
        }
        {
            void * t74 = gm_l0;
            void * t75 = gm_l0;
            void * t76 = GM_LD(void *, (((uint8_t *)t75) + 0));
            void * t77 = (void *)(((uint8_t *)t76) + 8);
            gm_t88rcucg1b7uk t78 = gm_f_usize_ty_9mijhtc((void *)NULL, cx, NULL, t77);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t79 = gm_l15;
            gm_b16_8 t80 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t74, t78, t79);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l7)) + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l7)) + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l7)) + 20, 0);
            ((uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l7)))[16] = 1;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l7)) + 0, t80);
        }
        {
            void * t81 = gm_l0;
            void * t82 = GM_LD(void *, (((uint8_t *)t81) + 0));
            void * t83 = (void *)(((uint8_t *)t82) + 8);
            gm_t88rcucg1b7uk t84 = gm_l11;
            uint64_t t85 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t83, t84);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l8 = t85;
        }
        goto me10;
mn37:;
ma13:;
        uint8_t t87 = GM_LD(uint8_t, (((uint8_t *)&t9) + 0));
        if (t87 != ((uint8_t)9ULL)) goto mn86;
        gm_t88rcucg1b7uk t88 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t9) + 8) + 4));
        gm_l16 = t88;
        {
            void * t90 = gm_l0;
            void * t91 = gm_l2;
            gm_t5o0tpv5jka2l t92;
            gm_f_expr_porbc9t((void *)&t92, (void *)NULL, cx, NULL, t90, t91);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t92))[0] != 0)) {
                gm_t5o0tpv5jka2l t93;
                GM_ST(uint8_t, (uint8_t *)&t93 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t93 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t93 + 4, 0);
                ((uint8_t *)&t93)[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t93 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t92) + 8)));
                F.ret = t93;
                goto gm_fret;
            }
            gm_b16_8 t94 = GM_LD(gm_b16_8, (((uint8_t *)&t92) + 8));
            gm_l17 = t94;
        }
        {
            gm_b16_8 t95 = gm_l17;
            gm_tnpifv20irnph t96;
            GM_ST(gm_b16_8, ((uint8_t *)&t96 + 0), t95);
            gm_tabb710d9mq2e t97;
            gm_f_format_da6unq6((void *)&t97, (void *)NULL, cx, NULL, t96);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t97))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t97) + 8, gm_src0gtuh94b1fce, 1103, 19);
            gm_b16_8 t98 = GM_LD(gm_b16_8, (((uint8_t *)&t97) + 8));
            gm_b16_8 t99 = GM_LD(gm_b16_8, ((uint8_t *)&t98));
            gm_l6 = t99;
        }
        {
            void * t100 = gm_l0;
            void * t101 = GM_LD(void *, (((uint8_t *)t100) + 0));
            void * t102 = (void *)(((uint8_t *)t101) + 8);
            gm_t88rcucg1b7uk t103 = gm_l16;
            uint64_t t104 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t102, t103);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l8 = t104;
        }
        goto me10;
mn86:;
ma14:;
        void * t106 = gm_l1;
        gm_tl874t2atnlh3 t107 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t106) + 72));
        void * t108 = gm_l0;
        void * t109 = GM_LD(void *, (((uint8_t *)t108) + 0));
        void * t110 = (void *)(((uint8_t *)t109) + 8);
        void * t111 = gm_l2;
        gm_t88rcucg1b7uk t112 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t111) + 0));
        gm_b16_8 t113 = gm_f_show_5qbi623((void *)NULL, cx, NULL, t110, t112);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t114;
        GM_ST(gm_b16_8, ((uint8_t *)&t114 + 0), t113);
        gm_tabb710d9mq2e t115;
        gm_f_format_jlajof4((void *)&t115, (void *)NULL, cx, NULL, t114);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t115))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t115) + 8, gm_src0gtuh94b1fce, 1106, 42);
        gm_b16_8 t116 = GM_LD(gm_b16_8, (((uint8_t *)&t115) + 8));
        gm_b16_8 t117 = GM_LD(gm_b16_8, ((uint8_t *)&t116));
        gm_t08olq97rscn3 t118;
        gm_f_unsupported_531q51r((void *)&t118, (void *)NULL, cx, NULL, t107, t117);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 1;
        GM_ST(gm_t08olq97rscn3, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t118);
        goto gm_fdone;
        goto me10;
mn105:;
ma15:;
        gm_trap("no match arm matched the scrutinee", gm_src0gtuh94b1fce, 1079, 5);
me10:;
    }
    {
        void * t119 = gm_l0;
        void * t120 = GM_LD(void *, (((uint8_t *)t119) + 0));
        void * t121 = GM_LD(void *, ((((uint8_t *)t120) + 8) + 0));
        void * t122 = (void *)(((uint8_t *)t121) + 8);
        gm_l18 = t122;
    }
    {
        void * t123 = gm_l0;
        void * t124 = gm_l18;
        void * t125 = gm_l18;
        gm_t88rcucg1b7uk t126 = gm_f_u8_ty_afkvj25((void *)NULL, cx, NULL, t125);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t88rcucg1b7uk t127 = gm_f_ptr_t1h2m2d((void *)NULL, cx, NULL, t124, ((uint8_t)0), t126);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t128 = gm_l6;
        gm_b16_8 t129 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t123, t127, t128);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l6 = t129;
    }
    {
        gm_b16_8 t130;
        GM_ST(void *, (uint8_t *)&t130, (void *)(gm_s0fg7tcl0p4ij));
        GM_ST(uintptr_t, (uint8_t *)&t130 + 8, (uintptr_t)(12));
        gm_l19 = t130;
    }
    {
        void * t132 = gm_l3;
        if (!(GM_LD(void *, ((uint8_t *)&t132)) != 0)) goto ie131;
        void * t133 = GM_LD(void *, ((uint8_t *)&t132));
        gm_l20 = t133;
        {
            void * t135 = gm_l0;
            void * t136 = gm_l20;
            gm_t5o0tpv5jka2l t137;
            gm_f_expr_porbc9t((void *)&t137, (void *)NULL, cx, NULL, t135, t136);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t137))[0] != 0)) {
                gm_t5o0tpv5jka2l t138;
                GM_ST(uint8_t, (uint8_t *)&t138 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t138 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t138 + 4, 0);
                ((uint8_t *)&t138)[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t138 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t137) + 8)));
                F.ret = t138;
                goto gm_fret;
            }
            gm_b16_8 t139 = GM_LD(gm_b16_8, (((uint8_t *)&t137) + 8));
            gm_l21 = t139;
        }
        {
            gm_b16_8 t140;
            uint8_t t142;
            if (((((uint8_t *)&gm_l7))[16] != 0)) goto mn144;
            t142 = ((uint8_t)0);
            goto me143;
mn144:;
            t142 = ((uint8_t)1);
            goto me143;
mn145:;
            gm_trap("no match arm matched the scrutinee", gm_src0gtuh94b1fce, 1113, 18);
me143:;
            if (!t142) goto ie141;
            void * t147 = gm_l0;
            void * t148 = gm_l20;
            gm_t88rcucg1b7uk t149 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t148) + 0));
            gm_b16_8 t150 = gm_l21;
            gm_b16_8 t151 = gm_f_bound_n4p0vpb((void *)NULL, cx, NULL, t147, t149, t150);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t152 = t151;
            t140 = t152;
            goto ix141;
ie141:;
            gm_b16_8 t154 = gm_l21;
            gm_tnpifv20irnph t155;
            GM_ST(gm_b16_8, ((uint8_t *)&t155 + 0), t154);
            gm_tabb710d9mq2e t156;
            gm_f_format_6df13cd((void *)&t156, (void *)NULL, cx, NULL, t155);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t156))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t156) + 8, gm_src0gtuh94b1fce, 1113, 63);
            gm_b16_8 t157 = GM_LD(gm_b16_8, (((uint8_t *)&t156) + 8));
            gm_b16_8 t158 = t157;
            gm_b16_8 t159 = GM_LD(gm_b16_8, ((uint8_t *)&t158));
            t140 = t159;
ix141:;
            gm_l22 = t140;
        }
        {
            void * t160 = gm_l0;
            void * t161 = gm_l0;
            void * t162 = GM_LD(void *, (((uint8_t *)t161) + 0));
            void * t163 = (void *)(((uint8_t *)t162) + 8);
            gm_t88rcucg1b7uk t164 = gm_f_usize_ty_9mijhtc((void *)NULL, cx, NULL, t163);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t165 = gm_l22;
            gm_b16_8 t166 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t160, t164, t165);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l19 = t166;
        }
        goto ix131;
ie131:;
ix131:;
    }
    {
        gm_b16_8 t167;
        GM_ST(void *, (uint8_t *)&t167, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t167 + 8, (uintptr_t)(0));
        gm_l23 = t167;
    }
    {
        void * t169 = gm_l4;
        if (!(GM_LD(void *, ((uint8_t *)&t169)) != 0)) goto ie168;
        void * t170 = GM_LD(void *, ((uint8_t *)&t169));
        gm_l24 = t170;
        {
            void * t172 = gm_l0;
            void * t173 = gm_l24;
            gm_t5o0tpv5jka2l t174;
            gm_f_expr_porbc9t((void *)&t174, (void *)NULL, cx, NULL, t172, t173);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t174))[0] != 0)) {
                gm_t5o0tpv5jka2l t175;
                GM_ST(uint8_t, (uint8_t *)&t175 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t175 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t175 + 4, 0);
                ((uint8_t *)&t175)[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t175 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t174) + 8)));
                F.ret = t175;
                goto gm_fret;
            }
            gm_b16_8 t176 = GM_LD(gm_b16_8, (((uint8_t *)&t174) + 8));
            gm_l25 = t176;
        }
        {
            gm_b16_8 t177;
            uint8_t t179;
            if (((((uint8_t *)&gm_l7))[16] != 0)) goto mn181;
            t179 = ((uint8_t)0);
            goto me180;
mn181:;
            t179 = ((uint8_t)1);
            goto me180;
mn182:;
            gm_trap("no match arm matched the scrutinee", gm_src0gtuh94b1fce, 1119, 18);
me180:;
            if (!t179) goto ie178;
            void * t184 = gm_l0;
            void * t185 = gm_l24;
            gm_t88rcucg1b7uk t186 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t185) + 0));
            gm_b16_8 t187 = gm_l25;
            gm_b16_8 t188 = gm_f_bound_n4p0vpb((void *)NULL, cx, NULL, t184, t186, t187);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t189 = t188;
            t177 = t189;
            goto ix178;
ie178:;
            gm_b16_8 t191 = gm_l25;
            gm_tnpifv20irnph t192;
            GM_ST(gm_b16_8, ((uint8_t *)&t192 + 0), t191);
            gm_tabb710d9mq2e t193;
            gm_f_format_6df13cd((void *)&t193, (void *)NULL, cx, NULL, t192);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t193))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t193) + 8, gm_src0gtuh94b1fce, 1119, 63);
            gm_b16_8 t194 = GM_LD(gm_b16_8, (((uint8_t *)&t193) + 8));
            gm_b16_8 t195 = t194;
            gm_b16_8 t196 = GM_LD(gm_b16_8, ((uint8_t *)&t195));
            t177 = t196;
ix178:;
            gm_l26 = t177;
        }
        {
            void * t197 = gm_l0;
            void * t198 = gm_l0;
            void * t199 = GM_LD(void *, (((uint8_t *)t198) + 0));
            void * t200 = (void *)(((uint8_t *)t199) + 8);
            gm_t88rcucg1b7uk t201 = gm_f_usize_ty_9mijhtc((void *)NULL, cx, NULL, t200);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t202 = gm_l26;
            gm_b16_8 t203 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t197, t201, t202);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l27 = t203;
        }
        {
            uint8_t t205 = gm_l5;
            if (!t205) goto ie204;
            uint8_t t206;
            if (((((uint8_t *)&gm_l7))[16] != 0)) goto mn208;
            t206 = ((uint8_t)0);
            goto me207;
mn208:;
            t206 = ((uint8_t)1);
            goto me207;
mn209:;
            gm_trap("no match arm matched the scrutinee", gm_src0gtuh94b1fce, 1121, 26);
me207:;
            if (!t206) goto ie204;
            {
                void * t211 = gm_l0;
                gm_t6j6smj7qoctu t212;
                gm_b16_8 t213 = gm_l27;
                GM_ST(gm_b16_8, (uint8_t *)&t212 + 0, t213);
                gm_b16_8 t214 = gm_l27;
                GM_ST(gm_b16_8, (uint8_t *)&t212 + 16, t214);
                gm_b0_1 t215 = gm_f_linef_eiea5ou((void *)NULL, cx, NULL, t211, &t212);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix204;
ie204:;
            uint8_t t217 = gm_l5;
            if (!t217) goto ie216;
            {
                void * t219 = gm_l0;
                gm_b16_8 t220 = gm_l27;
                gm_tnpifv20irnph t221;
                GM_ST(gm_b16_8, ((uint8_t *)&t221 + 0), t220);
                gm_b0_1 t222 = gm_f_linef_v3hik15((void *)NULL, cx, NULL, t219, t221);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix216;
ie216:;
ix216:;
ix204:;
        }
        {
            gm_b16_8 t223 = gm_l27;
            gm_l23 = t223;
        }
        goto ix168;
ie168:;
        {
            gm_th2lqbo77pvsc t225 = gm_l7;
            gm_b16_8 t226;
            if (((((uint8_t *)&t225))[16] != 0)) {
                gm_b16_8 t227 = GM_LD(gm_b16_8, (((uint8_t *)&t225) + 0));
                t226 = t227;
            } else {
                void * t228 = gm_l1;
                gm_tl874t2atnlh3 t229 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t228) + 72));
                gm_b16_8 t230;
                GM_ST(void *, (uint8_t *)&t230, (void *)(gm_s3ierue9bs27g));
                GM_ST(uintptr_t, (uint8_t *)&t230 + 8, (uintptr_t)(45));
                gm_t08olq97rscn3 t231;
                gm_f_unsupported_531q51r((void *)&t231, (void *)NULL, cx, NULL, t229, t230);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 1;
                GM_ST(gm_t08olq97rscn3, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t231);
                goto gm_fdone;
            }
            gm_l28 = t226;
        }
        {
            gm_b16_8 t232 = gm_l28;
            gm_tnpifv20irnph t233;
            GM_ST(gm_b16_8, ((uint8_t *)&t233 + 0), t232);
            gm_tabb710d9mq2e t234;
            gm_f_format_6df13cd((void *)&t234, (void *)NULL, cx, NULL, t233);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t234))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t234) + 8, gm_src0gtuh94b1fce, 1131, 16);
            gm_b16_8 t235 = GM_LD(gm_b16_8, (((uint8_t *)&t234) + 8));
            gm_b16_8 t236 = GM_LD(gm_b16_8, ((uint8_t *)&t235));
            gm_l23 = t236;
        }
ix168:;
    }
    {
        if (!((((uint8_t *)&gm_l7))[16] != 0)) goto ie237;
        gm_b16_8 t238 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l7));
        gm_l29 = t238;
        {
            void * t240 = gm_l0;
            void * t241 = gm_l1;
            gm_b16_8 t242 = gm_f_loc_u9c0aqb((void *)NULL, cx, NULL, t240, t241);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l30 = t242;
        }
        {
            void * t243 = gm_l0;
            gm_tg1t0c5hkovn5 t244;
            gm_b16_8 t245 = gm_l19;
            GM_ST(gm_b16_8, (uint8_t *)&t244 + 0, t245);
            gm_b16_8 t246 = gm_l23;
            GM_ST(gm_b16_8, (uint8_t *)&t244 + 16, t246);
            gm_b16_8 t247 = gm_l23;
            GM_ST(gm_b16_8, (uint8_t *)&t244 + 32, t247);
            gm_b16_8 t248 = gm_l29;
            GM_ST(gm_b16_8, (uint8_t *)&t244 + 48, t248);
            gm_b16_8 t249 = gm_l19;
            GM_ST(gm_b16_8, (uint8_t *)&t244 + 64, t249);
            gm_b16_8 t250 = gm_l23;
            GM_ST(gm_b16_8, (uint8_t *)&t244 + 80, t250);
            gm_b16_8 t251 = gm_l29;
            GM_ST(gm_b16_8, (uint8_t *)&t244 + 96, t251);
            gm_b16_8 t252 = gm_l30;
            GM_ST(gm_b16_8, (uint8_t *)&t244 + 112, t252);
            gm_b0_1 t253 = gm_f_linef_m7tlafn((void *)NULL, cx, NULL, t243, &t244);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix237;
ie237:;
ix237:;
    }
    {
        void * t254 = gm_l0;
        void * t255 = gm_l1;
        gm_t88rcucg1b7uk t256 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t255) + 0));
        gm_tftksr4op114e t257;
        gm_b16_8 t258 = gm_l6;
        GM_ST(gm_b16_8, (uint8_t *)&t257 + 0, t258);
        gm_b16_8 t259 = gm_l19;
        GM_ST(gm_b16_8, (uint8_t *)&t257 + 16, t259);
        uint64_t t260 = gm_l8;
        GM_ST(uint64_t, (uint8_t *)&t257 + 32, t260);
        gm_tabb710d9mq2e t261;
        gm_f_format_8sn6h96((void *)&t261, (void *)NULL, cx, NULL, &t257);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t261))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t261) + 8, gm_src0gtuh94b1fce, 1137, 32);
        gm_b16_8 t262 = GM_LD(gm_b16_8, (((uint8_t *)&t261) + 8));
        gm_b16_8 t263 = GM_LD(gm_b16_8, ((uint8_t *)&t262));
        gm_t6j6smj7qoctu t264;
        gm_b16_8 t265 = gm_l23;
        GM_ST(gm_b16_8, (uint8_t *)&t264 + 0, t265);
        gm_b16_8 t266 = gm_l19;
        GM_ST(gm_b16_8, (uint8_t *)&t264 + 16, t266);
        gm_tabb710d9mq2e t267;
        gm_f_format_3mj8462((void *)&t267, (void *)NULL, cx, NULL, &t264);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t267))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t267) + 8, gm_src0gtuh94b1fce, 1137, 90);
        gm_b16_8 t268 = GM_LD(gm_b16_8, (((uint8_t *)&t267) + 8));
        gm_b16_8 t269 = GM_LD(gm_b16_8, ((uint8_t *)&t268));
        gm_b16_8 t270 = gm_f_make_slice_sa50efe((void *)NULL, cx, NULL, t254, t256, t263, t269);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t270);
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
extern __attribute__((aligned(4))) gm_b16_8 gm_f_slice_len_a3383br(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
extern __attribute__((aligned(4))) gm_b16_8 gm_f_slice_ptr_hojfgr5(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_spec_literal_lmkpbc1(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_spec_literal_lmkpbc1 F;
    gm_t7g88i52rrtm5 gm_l0; /* s */
    int32_t gm_l1; /* align */
    int32_t gm_l2; /* kind */
    int64_t gm_l3; /* precision */
    uint32_t gm_l4; /* p */
    GM_STACK_CHECK(&F, 270u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&gm_l0, a0, sizeof gm_l0);
    {
        int32_t t2;
        uint8_t t9 = GM_LD(uint8_t, ((((uint8_t *)&gm_l0) + 1) + 0));
        switch (t9) { case ((uint8_t)0ULL): goto ma4; case ((uint8_t)1ULL): goto ma5; case ((uint8_t)2ULL): goto ma6; case ((uint8_t)3ULL): goto ma7; default: goto ma8; }
ma4:;
        uint8_t t11 = GM_LD(uint8_t, ((((uint8_t *)&gm_l0) + 1) + 0));
        if (t11 != ((uint8_t)0ULL)) goto mn10;
        t2 = ((int32_t)0LL);
        goto me3;
mn10:;
ma5:;
        uint8_t t13 = GM_LD(uint8_t, ((((uint8_t *)&gm_l0) + 1) + 0));
        if (t13 != ((uint8_t)1ULL)) goto mn12;
        t2 = ((int32_t)1LL);
        goto me3;
mn12:;
ma6:;
        uint8_t t15 = GM_LD(uint8_t, ((((uint8_t *)&gm_l0) + 1) + 0));
        if (t15 != ((uint8_t)2ULL)) goto mn14;
        t2 = ((int32_t)2LL);
        goto me3;
mn14:;
ma7:;
        uint8_t t17 = GM_LD(uint8_t, ((((uint8_t *)&gm_l0) + 1) + 0));
        if (t17 != ((uint8_t)3ULL)) goto mn16;
        t2 = ((int32_t)3LL);
        goto me3;
mn16:;
ma8:;
        gm_trap("no match arm matched the scrutinee", gm_src0gtuh94b1fce, 1275, 14);
me3:;
        gm_l1 = t2;
    }
    {
        int32_t t18;
        uint8_t t28 = GM_LD(uint8_t, ((((uint8_t *)&gm_l0) + 20) + 0));
        switch (t28) { case ((uint8_t)0ULL): goto ma20; case ((uint8_t)1ULL): goto ma21; case ((uint8_t)2ULL): goto ma22; case ((uint8_t)3ULL): goto ma23; case ((uint8_t)4ULL): goto ma24; case ((uint8_t)5ULL): goto ma25; case ((uint8_t)6ULL): goto ma26; default: goto ma27; }
ma20:;
        uint8_t t30 = GM_LD(uint8_t, ((((uint8_t *)&gm_l0) + 20) + 0));
        if (t30 != ((uint8_t)0ULL)) goto mn29;
        t18 = ((int32_t)0LL);
        goto me19;
mn29:;
ma21:;
        uint8_t t32 = GM_LD(uint8_t, ((((uint8_t *)&gm_l0) + 20) + 0));
        if (t32 != ((uint8_t)1ULL)) goto mn31;
        t18 = ((int32_t)1LL);
        goto me19;
mn31:;
ma22:;
        uint8_t t34 = GM_LD(uint8_t, ((((uint8_t *)&gm_l0) + 20) + 0));
        if (t34 != ((uint8_t)2ULL)) goto mn33;
        t18 = ((int32_t)2LL);
        goto me19;
mn33:;
ma23:;
        uint8_t t36 = GM_LD(uint8_t, ((((uint8_t *)&gm_l0) + 20) + 0));
        if (t36 != ((uint8_t)3ULL)) goto mn35;
        t18 = ((int32_t)3LL);
        goto me19;
mn35:;
ma24:;
        uint8_t t38 = GM_LD(uint8_t, ((((uint8_t *)&gm_l0) + 20) + 0));
        if (t38 != ((uint8_t)4ULL)) goto mn37;
        t18 = ((int32_t)4LL);
        goto me19;
mn37:;
ma25:;
        uint8_t t40 = GM_LD(uint8_t, ((((uint8_t *)&gm_l0) + 20) + 0));
        if (t40 != ((uint8_t)5ULL)) goto mn39;
        t18 = ((int32_t)5LL);
        goto me19;
mn39:;
ma26:;
        uint8_t t42 = GM_LD(uint8_t, ((((uint8_t *)&gm_l0) + 20) + 0));
        if (t42 != ((uint8_t)6ULL)) goto mn41;
        t18 = ((int32_t)6LL);
        goto me19;
mn41:;
ma27:;
        gm_trap("no match arm matched the scrutinee", gm_src0gtuh94b1fce, 1281, 13);
me19:;
        gm_l2 = t18;
    }
    {
        gm_l3 = ((int64_t)-1LL);
    }
    {
        if (!(((((uint8_t *)&gm_l0) + 12))[4] != 0)) goto ie43;
        uint32_t t44 = GM_LD(uint32_t, (((uint8_t *)&gm_l0) + 12));
        gm_l4 = t44;
        {
            uint32_t t46 = gm_l4;
            int64_t t47 = (int64_t)t46;
            gm_l3 = t47;
        }
        goto ix43;
ie43:;
ix43:;
    }
    {
        gm_t01jnheg9lqvd t48;
        memset((uint8_t *)&t48 + 1, 0, 3);
        memset((uint8_t *)&t48 + 28, 0, 4);
        uint8_t t49 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        GM_ST(uint8_t, (uint8_t *)&t48 + 0, t49);
        int32_t t50 = gm_l1;
        GM_ST(int32_t, (uint8_t *)&t48 + 4, t50);
        int32_t t51;
        uint8_t t53 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 2));
        if (!t53) goto ie52;
        int32_t t55 = ((int32_t)1LL);
        t51 = t55;
        goto ix52;
ie52:;
        int32_t t57 = ((int32_t)0LL);
        t51 = t57;
ix52:;
        GM_ST(int32_t, (uint8_t *)&t48 + 8, t51);
        int32_t t58;
        uint8_t t60 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 3));
        if (!t60) goto ie59;
        int32_t t62 = ((int32_t)1LL);
        t58 = t62;
        goto ix59;
ie59:;
        int32_t t64 = ((int32_t)0LL);
        t58 = t64;
ix59:;
        GM_ST(int32_t, (uint8_t *)&t48 + 12, t58);
        int32_t t65;
        uint8_t t67 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 4));
        if (!t67) goto ie66;
        int32_t t69 = ((int32_t)1LL);
        t65 = t69;
        goto ix66;
ie66:;
        int32_t t71 = ((int32_t)0LL);
        t65 = t71;
ix66:;
        GM_ST(int32_t, (uint8_t *)&t48 + 16, t65);
        int32_t t72 = gm_l2;
        GM_ST(int32_t, (uint8_t *)&t48 + 20, t72);
        uint32_t t73 = GM_LD(uint32_t, (((uint8_t *)&gm_l0) + 8));
        GM_ST(uint32_t, (uint8_t *)&t48 + 24, t73);
        int64_t t74 = gm_l3;
        GM_ST(int64_t, (uint8_t *)&t48 + 32, t74);
        gm_tabb710d9mq2e t75;
        gm_f_format_8933odu((void *)&t75, (void *)NULL, cx, NULL, &t48);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t75))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t75) + 8, gm_src0gtuh94b1fce, 1292, 12);
        gm_b16_8 t76 = GM_LD(gm_b16_8, (((uint8_t *)&t75) + 8));
        gm_b16_8 t77 = GM_LD(gm_b16_8, ((uint8_t *)&t76));
        uint8_t t78 = 0;
        F.ret = t77;
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
extern __attribute__((aligned(4))) void gm_f_split_u128_vrmr6en(void *gm_out, const void * a0);
__attribute__((aligned(4))) gm_t793vlgpa3b88 gm_f_split_u128_vrmr6en_c(gm_tbci5ospk5nnn a0) { gm_t793vlgpa3b88 gm_r; gm_f_split_u128_vrmr6en(&gm_r, &a0); return gm_r; }
extern __attribute__((aligned(4))) gm_b0_1 gm_f_store_at7m90t(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_b16_8 a3);
extern __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_target_ret_q7qjs0g(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_temp_piece_572g74q(void *gm_out, gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_temp_piece_572g74q F;
    gm_b16_8 gm_l0; /* a */
    gm_b16_8 gm_l1; /* base */
    gm_b16_8 gm_l2; /* n */
    gm_b16_8 gm_l3; /* inner */
    uint64_t gm_l4; /* i */
    uint64_t gm_l5; /* k */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    uint8_t gm_l8; /* c */
    gm_t3877pk7lv748 gm_l9; /* p */
    GM_STACK_CHECK(&F, 1145u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t2;
        GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s5pra70i8o734));
        GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(13));
        gm_l1 = t2;
    }
    {
        gm_b16_8 t4 = gm_l0;
        gm_b16_8 t5 = gm_l1;
        uint8_t t6 = gm_f_starts_with_6vvt01e(t4, t5);
        if (!t6) goto ie3;
        uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint64_t t8 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t9 = (t7 > t8);
        if (!t9) goto ie3;
        uint64_t t10 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint64_t r11; bool o12 = __builtin_sub_overflow(t10, ((uint64_t)1ULL), &r11);
        if (o12) gm_trap("integer overflow in `-`", gm_src0gtuh94b1fce, 54, 55);
        uint64_t t13 = r11;
        uint64_t t14 = (uintptr_t)t13;
        uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        if (t14 >= (uintptr_t)(t15)) gm_trap_index(t14, t15, gm_src0gtuh94b1fce, 54, 53);
        uint8_t t16 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t14 * 1));
        uint8_t t17 = ((t16 == ((uint8_t)41ULL)));
        if (!t17) goto ie3;
        {
            uint64_t t19 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            void * t20 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
            uint64_t t21 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint64_t t22 = (uintptr_t)t21;
            uint64_t t23 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            uint64_t r24; bool o25 = __builtin_sub_overflow(t23, ((uint64_t)1ULL), &r24);
            if (o25) gm_trap("integer overflow in `-`", gm_src0gtuh94b1fce, 55, 26);
            uint64_t t26 = r24;
            uint64_t t27 = (uintptr_t)t26;
            if (t22 > t27 || t27 > (uintptr_t)t19) gm_trap_slice(t22, t27, t19, gm_src0gtuh94b1fce, 55, 14);
            gm_b16_8 t28;
            GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t20 + t22 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
            gm_l2 = t28;
        }
        {
            gm_b16_8 t30 = gm_l2;
            uint8_t t31 = gm_f_is_temp_rrllgdr(t30);
            if (!t31) goto ie29;
            {
                gm_t3877pk7lv748 t33;
                gm_b16_8 t34 = gm_l2;
                GM_ST(gm_b16_8, (uint8_t *)&t33 + 0, t34);
                GM_ST(uint64_t, (uint8_t *)&t33 + 16, ((uint64_t)0ULL));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t9geo0l9gngfn *)gm_out) + 25, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t9geo0l9gngfn *)gm_out) + 26, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t9geo0l9gngfn *)gm_out) + 28, 0);
                ((uint8_t *)&(*(gm_t9geo0l9gngfn *)gm_out))[24] = 1;
                GM_ST(gm_t3877pk7lv748, (uint8_t *)&(*(gm_t9geo0l9gngfn *)gm_out) + 0, t33);
                goto gm_fdone;
            }
            goto ix29;
ie29:;
ix29:;
        }
        {
            gm_t9geo0l9gngfn t35;
            memset(&t35, 0, sizeof t35);
            uint8_t t36 = 0;
            F.ret = t35;
            goto gm_fret;
        }
        goto ix3;
ie3:;
ix3:;
    }
    {
        uint8_t t38 = 0;
        uint8_t t39 = 0;
        uint64_t t40 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint8_t t41 = (t40 < ((uint64_t)2ULL));
        if (!t41) {
            uint64_t t42 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t43 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            if (t42 >= (uintptr_t)(t43)) gm_trap_index(t42, t43, gm_src0gtuh94b1fce, 60, 21);
            uint8_t t44 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t42 * 1));
            uint8_t t45 = (!(t44 == ((uint8_t)40ULL)));
            t39 = t45;
        } else t39 = 1;
        if (!t39) {
            uint64_t t46 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            uint64_t r47; bool o48 = __builtin_sub_overflow(t46, ((uint64_t)1ULL), &r47);
            if (o48) gm_trap("integer overflow in `-`", gm_src0gtuh94b1fce, 60, 42);
            uint64_t t49 = r47;
            uint64_t t50 = (uintptr_t)t49;
            uint64_t t51 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            if (t50 >= (uintptr_t)(t51)) gm_trap_index(t50, t51, gm_src0gtuh94b1fce, 60, 40);
            uint8_t t52 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t50 * 1));
            uint8_t t53 = (!(t52 == ((uint8_t)41ULL)));
            t38 = t53;
        } else t38 = 1;
        if (!t38) goto ie37;
        {
            gm_t9geo0l9gngfn t55;
            memset(&t55, 0, sizeof t55);
            uint8_t t56 = 0;
            F.ret = t55;
            goto gm_fret;
        }
        goto ix37;
ie37:;
ix37:;
    }
    {
        uint64_t t57 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        void * t58 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
        uint64_t t59 = (uintptr_t)((uint64_t)1ULL);
        uint64_t t60 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint64_t r61; bool o62 = __builtin_sub_overflow(t60, ((uint64_t)1ULL), &r61);
        if (o62) gm_trap("integer overflow in `-`", gm_src0gtuh94b1fce, 61, 19);
        uint64_t t63 = r61;
        uint64_t t64 = (uintptr_t)t63;
        if (t59 > t64 || t64 > (uintptr_t)t57) gm_trap_slice(t59, t64, t57, gm_src0gtuh94b1fce, 61, 14);
        gm_b16_8 t65;
        GM_ST(void *, (uint8_t *)&t65, (void *)(((uint8_t *)t58 + t59 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t65 + 8, (uintptr_t)(t64 - t59));
        gm_l3 = t65;
    }
    {
        uint64_t t66 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        gm_l4 = t66;
    }
    {
        for (;;) {
            uint64_t t68 = gm_l4;
            uint8_t t69 = (t68 > ((uint64_t)0ULL));
            if (!t69) goto lb67;
            uint64_t t70 = gm_l4;
            uint64_t r71; bool o72 = __builtin_sub_overflow(t70, ((uint64_t)1ULL), &r71);
            if (o72) gm_trap("integer overflow in `-`", gm_src0gtuh94b1fce, 63, 27);
            uint64_t t73 = r71;
            uint64_t t74 = (uintptr_t)t73;
            uint64_t t75 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            if (t74 >= (uintptr_t)(t75)) gm_trap_index(t74, t75, gm_src0gtuh94b1fce, 63, 21);
            uint8_t t76 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t74 * 1));
            uint8_t t77 = (t76 >= ((uint8_t)48ULL));
            if (!t77) goto lb67;
            uint64_t t78 = gm_l4;
            uint64_t r79; bool o80 = __builtin_sub_overflow(t78, ((uint64_t)1ULL), &r79);
            if (o80) gm_trap("integer overflow in `-`", gm_src0gtuh94b1fce, 63, 51);
            uint64_t t81 = r79;
            uint64_t t82 = (uintptr_t)t81;
            uint64_t t83 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            if (t82 >= (uintptr_t)(t83)) gm_trap_index(t82, t83, gm_src0gtuh94b1fce, 63, 45);
            uint8_t t84 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t82 * 1));
            uint8_t t85 = (t84 <= ((uint8_t)57ULL));
            if (!t85) goto lb67;
            {
                uint64_t t88 = gm_l4;
                uint64_t r89; bool o90 = __builtin_sub_overflow(t88, ((uint64_t)1ULL), &r89);
                if (o90) gm_trap("integer overflow in `-`", gm_src0gtuh94b1fce, 63, 67);
                uint64_t t91 = r89;
                gm_l4 = t91;
            }
lc67:;
        }
lb67:;
    }
    {
        uint8_t t93 = 0;
        uint8_t t94 = 0;
        uint64_t t95 = gm_l4;
        uint64_t t96 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        uint8_t t97 = ((t95 == t96));
        if (!t97) {
            uint64_t t98 = gm_l4;
            uint8_t t99 = (t98 < ((uint64_t)3ULL));
            t94 = t99;
        } else t94 = 1;
        if (!t94) {
            uint64_t t100 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            void * t101 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)));
            uint64_t t102 = gm_l4;
            uint64_t r103; bool o104 = __builtin_sub_overflow(t102, ((uint64_t)3ULL), &r103);
            if (o104) gm_trap("integer overflow in `-`", gm_src0gtuh94b1fce, 64, 46);
            uint64_t t105 = r103;
            uint64_t t106 = (uintptr_t)t105;
            uint64_t t107 = gm_l4;
            uint64_t t108 = (uintptr_t)t107;
            if (t106 > t108 || t108 > (uintptr_t)t100) gm_trap_slice(t106, t108, t100, gm_src0gtuh94b1fce, 64, 40);
            gm_b16_8 t109;
            GM_ST(void *, (uint8_t *)&t109, (void *)(((uint8_t *)t101 + t106 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t109 + 8, (uintptr_t)(t108 - t106));
            uint8_t t110 = (((GM_LD(uintptr_t, ((uint8_t *)&t109) + 8) == 3) && memcmp(GM_LD(void *, ((uint8_t *)&t109)), gm_s0m0qaokk51g0, 3) == 0));
            uint8_t t111 = (uint8_t)!t110;
            t93 = t111;
        } else t93 = 1;
        if (!t93) goto ie92;
        {
            gm_t9geo0l9gngfn t113;
            memset(&t113, 0, sizeof t113);
            uint8_t t114 = 0;
            F.ret = t113;
            goto gm_fret;
        }
        goto ix92;
ie92:;
ix92:;
    }
    {
        gm_l5 = ((uint64_t)0ULL);
    }
    {
        {
            uint64_t t116 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            void * t117 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)));
            uint64_t t118 = gm_l4;
            uint64_t t119 = (uintptr_t)t118;
            if (t119 > (uintptr_t)t116 || (uintptr_t)t116 > (uintptr_t)t116) gm_trap_slice(t119, (uintptr_t)t116, t116, gm_src0gtuh94b1fce, 66, 14);
            gm_b16_8 t120;
            GM_ST(void *, (uint8_t *)&t120, (void *)(((uint8_t *)t117 + t119 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t120 + 8, (uintptr_t)((uintptr_t)t116 - t119));
            gm_l6 = t120;
        }
        {
            gm_l7 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t122 = gm_l7;
                uint64_t t123 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                uint8_t t124 = (t122 < t123);
                if (!t124) goto lb121;
                {
                    uint64_t t127 = gm_l7;
                    uint64_t t128 = (uintptr_t)t127;
                    uint64_t t129 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    uint8_t t130 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t128 * 1));
                    uint8_t t133 = GM_LD(uint8_t, ((uint8_t *)&t130));
                    gm_l8 = t133;
                    {
                        uint64_t t135 = gm_l5;
                        uint64_t r136; bool o137 = __builtin_mul_overflow(t135, ((uint64_t)10ULL), &r136);
                        if (o137) gm_trap("integer overflow in `*`", gm_src0gtuh94b1fce, 66, 31);
                        uint64_t t138 = r136;
                        uint8_t t139 = gm_l8;
                        uint8_t r140; bool o141 = __builtin_sub_overflow(t139, ((uint8_t)48ULL), &r140);
                        if (o141) gm_trap("integer overflow in `-`", gm_src0gtuh94b1fce, 66, 44);
                        uint8_t t142 = r140;
                        uint64_t t143 = (uint64_t)t142;
                        uint64_t r144; bool o145 = __builtin_add_overflow(t138, t143, &r144);
                        if (o145) gm_trap("integer overflow in `+`", gm_src0gtuh94b1fce, 66, 31);
                        uint64_t t146 = r144;
                        gm_l5 = t146;
                    }
                    goto me131;
mn132:;
                    gm_trap("no match arm matched the scrutinee", gm_src0gtuh94b1fce, 66, 5);
me131:;
                }
lc121:;
                {
                    uint64_t t148 = gm_l7;
                    uint64_t r149; bool o150 = __builtin_add_overflow(t148, ((uint64_t)1ULL), &r149);
                    uint64_t t151 = r149;
                    gm_l7 = t151;
                }
            }
lb121:;
        }
    }
    {
        uint64_t t152 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        void * t153 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)));
        uint64_t t154 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t155 = gm_l4;
        uint64_t r156; bool o157 = __builtin_sub_overflow(t155, ((uint64_t)3ULL), &r156);
        if (o157) gm_trap("integer overflow in `-`", gm_src0gtuh94b1fce, 67, 30);
        uint64_t t158 = r156;
        uint64_t t159 = (uintptr_t)t158;
        if (t154 > t159 || t159 > (uintptr_t)t152) gm_trap_slice(t154, t159, t152, gm_src0gtuh94b1fce, 67, 21);
        gm_b16_8 t160;
        GM_ST(void *, (uint8_t *)&t160, (void *)(((uint8_t *)t153 + t154 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t160 + 8, (uintptr_t)(t159 - t154));
        gm_t9geo0l9gngfn t161;
        gm_f_temp_piece_572g74q((void *)&t161, t160);
        gm_t3877pk7lv748 t162;
        if (((((uint8_t *)&t161))[24] != 0)) {
            t162 = (*(gm_t3877pk7lv748 *)(((uint8_t *)&t161) + 0));
        } else {
            gm_t9geo0l9gngfn t163;
            memset(&t163, 0, sizeof t163);
            uint8_t t164 = 0;
            F.ret = t163;
            goto gm_fret;
        }
        gm_l9 = t162;
    }
    {
        gm_t3877pk7lv748 t165;
        gm_b16_8 t166 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l9) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t165 + 0, t166);
        uint64_t t167 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 16));
        uint64_t t168 = gm_l5;
        uint64_t r169; bool o170 = __builtin_add_overflow(t167, t168, &r169);
        if (o170) gm_trap("integer overflow in `+`", gm_src0gtuh94b1fce, 68, 41);
        uint64_t t171 = r169;
        GM_ST(uint64_t, (uint8_t *)&t165 + 16, t171);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t9geo0l9gngfn *)gm_out) + 25, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t9geo0l9gngfn *)gm_out) + 26, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t9geo0l9gngfn *)gm_out) + 28, 0);
        ((uint8_t *)&(*(gm_t9geo0l9gngfn *)gm_out))[24] = 1;
        GM_ST(gm_t3877pk7lv748, (uint8_t *)&(*(gm_t9geo0l9gngfn *)gm_out) + 0, t165);
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_t9geo0l9gngfn gm_f_temp_piece_572g74q_c(gm_b16_8 a0) { gm_t9geo0l9gngfn gm_r; gm_f_temp_piece_572g74q(&gm_r, a0); return gm_r; }
inline __attribute__((aligned(4))) void gm_f_trap_digu1sk(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2) {
    struct gm_fr_trap_digu1sk F;
    void * gm_l0; /* fg */
    void * gm_l1; /* e */
    void * gm_l2; /* message */
    gm_t88rcucg1b7uk gm_l3; /* t */
    gm_b16_8 gm_l4; /* m */
    gm_b16_8 gm_l5; /* a */
    gm_b16_8 gm_l6; /* p */
    gm_b16_8 gm_l7; /* l */
    gm_b16_8 gm_l8; /* at */
    GM_STACK_CHECK(&F, 520u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l1;
        gm_t88rcucg1b7uk t3 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t2) + 0));
        gm_l3 = t3;
    }
    {
        void * t4 = gm_l0;
        void * t5 = gm_l2;
        gm_t5o0tpv5jka2l t6;
        gm_f_expr_porbc9t((void *)&t6, (void *)NULL, cx, NULL, t4, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t6))[0] != 0)) {
            gm_t5o0tpv5jka2l t7;
            GM_ST(uint8_t, (uint8_t *)&t7 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t7 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t7 + 4, 0);
            ((uint8_t *)&t7)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t7 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t6) + 8)));
            F.ret = t7;
            goto gm_fret;
        }
        gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)&t6) + 8));
        gm_l4 = t8;
    }
    {
        void * t9 = gm_l0;
        void * t10 = gm_l2;
        gm_t88rcucg1b7uk t11 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t10) + 0));
        gm_b16_8 t12 = gm_l4;
        gm_b16_8 t13 = gm_f_addr_of_hiaqkn0((void *)NULL, cx, NULL, t9, t11, t12);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l5 = t13;
    }
    {
        gm_b16_8 t14 = gm_l5;
        gm_b16_8 t15 = gm_f_slice_ptr_hojfgr5((void *)NULL, cx, NULL, t14);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l6 = t15;
    }
    {
        void * t16 = gm_l0;
        gm_b16_8 t17 = gm_l5;
        gm_b16_8 t18 = gm_f_slice_len_a3383br((void *)NULL, cx, NULL, t16, t17);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l7 = t18;
    }
    {
        void * t19 = gm_l0;
        void * t20 = gm_l1;
        gm_b16_8 t21 = gm_f_loc_u9c0aqb((void *)NULL, cx, NULL, t19, t20);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l8 = t21;
    }
    {
        void * t22 = gm_l0;
        gm_tfeab279c16rr t23;
        gm_b16_8 t24 = gm_l6;
        GM_ST(gm_b16_8, (uint8_t *)&t23 + 0, t24);
        gm_b16_8 t25 = gm_l7;
        GM_ST(gm_b16_8, (uint8_t *)&t23 + 16, t25);
        gm_b16_8 t26 = gm_l8;
        GM_ST(gm_b16_8, (uint8_t *)&t23 + 32, t26);
        gm_b0_1 t27 = gm_f_linef_6f120gc((void *)NULL, cx, NULL, t22, &t23);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t28 = gm_l0;
        gm_t88rcucg1b7uk t29 = gm_l3;
        gm_b16_8 t30 = gm_f_zero_tlk13aa((void *)NULL, cx, NULL, t28, t29);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t30);
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
__attribute__((aligned(4))) void gm_f_unwrap_9lhqi3k(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, uint8_t a3) {
    struct gm_fr_unwrap_9lhqi3k F;
    void * gm_l0; /* fg */
    void * gm_l1; /* e */
    void * gm_l2; /* x */
    uint8_t gm_l3; /* is_result */
    gm_t88rcucg1b7uk gm_l4; /* t */
    gm_b16_8 gm_l5; /* v */
    gm_b16_8 gm_l6; /* a */
    gm_b16_8 gm_l7; /* at */
    gm_b16_8 gm_l8; /* bad */
    gm_t88rcucg1b7uk gm_l9; /* err */
    uint64_t gm_l10; /* off */
    gm_b16_8 gm_l11; /* d */
    gm_b16_8 gm_l12; /* present */
    GM_STACK_CHECK(&F, 1101u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l1;
        gm_t88rcucg1b7uk t3 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t2) + 0));
        gm_l4 = t3;
    }
    {
        void * t4 = gm_l0;
        void * t5 = gm_l2;
        gm_t5o0tpv5jka2l t6;
        gm_f_expr_porbc9t((void *)&t6, (void *)NULL, cx, NULL, t4, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t6))[0] != 0)) {
            gm_t5o0tpv5jka2l t7;
            GM_ST(uint8_t, (uint8_t *)&t7 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t7 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t7 + 4, 0);
            ((uint8_t *)&t7)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t7 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t6) + 8)));
            F.ret = t7;
            goto gm_fret;
        }
        gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)&t6) + 8));
        gm_l5 = t8;
    }
    {
        void * t9 = gm_l0;
        void * t10 = gm_l2;
        gm_t88rcucg1b7uk t11 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t10) + 0));
        gm_b16_8 t12 = gm_l5;
        gm_b16_8 t13 = gm_f_addr_of_hiaqkn0((void *)NULL, cx, NULL, t9, t11, t12);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l6 = t13;
    }
    {
        void * t14 = gm_l0;
        void * t15 = gm_l1;
        gm_b16_8 t16 = gm_f_loc_u9c0aqb((void *)NULL, cx, NULL, t14, t15);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l7 = t16;
    }
    {
        uint8_t t18 = gm_l3;
        if (!t18) goto ie17;
        {
            void * t20 = gm_l0;
            void * t21 = gm_l2;
            gm_t88rcucg1b7uk t22 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t21) + 0));
            gm_b16_8 t23 = gm_l6;
            gm_b16_8 t24 = gm_f_res_is_err_11p8qlp((void *)NULL, cx, NULL, t20, t22, t23);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l8 = t24;
        }
        {
            void * t26 = gm_l0;
            void * t27 = GM_LD(void *, (((uint8_t *)t26) + 0));
            void * t28 = (void *)(((uint8_t *)t27) + 8);
            void * t29 = gm_l2;
            gm_t88rcucg1b7uk t30 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t29) + 0));
            gm_t53at80pfdus3 t31;
            gm_f_kind_ghvav22((void *)&t31, t28, GM_CONV(gm_csguankko4lmhd, t30));
            uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)&t31) + 0));
            if (t32 != ((uint8_t)14ULL)) goto ie25;
            gm_t88rcucg1b7uk t33 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t31) + 8) + 0));
            gm_l9 = t33;
            {
                uint64_t t35 = (uintptr_t)((uint64_t)1ULL);
                void * t36 = gm_l0;
                void * t37 = GM_LD(void *, (((uint8_t *)t36) + 0));
                void * t38 = (void *)(((uint8_t *)t37) + 8);
                void * t39 = gm_l2;
                gm_t88rcucg1b7uk t40 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t39) + 0));
                gm_b16_8 t41 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t38, t40);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint64_t t42 = GM_LD(uintptr_t, ((uint8_t *)&t41) + 8);
                if (t35 >= (uintptr_t)(t42)) gm_trap_index(t35, t42, gm_src0gtuh94b1fce, 592, 16);
                uint64_t t43 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t41)) + t35 * 8));
                gm_l10 = t43;
            }
            {
                void * t44 = gm_l0;
                void * t45 = GM_LD(void *, (((uint8_t *)t44) + 0));
                void * t46 = (void *)(((uint8_t *)t45) + 8);
                gm_t88rcucg1b7uk t47 = gm_l9;
                gm_b16_8 t48 = gm_f_desc_0cag2kq((void *)NULL, cx, NULL, t46, t47);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l11 = t48;
            }
            {
                void * t50 = gm_l0;
                void * t51 = GM_LD(void *, (((uint8_t *)t50) + 0));
                uint8_t t52 = GM_LD(uint8_t, ((((uint8_t *)t51) + 1136) + 0));
                if (!t52) goto ie49;
                {
                    void * t54 = gm_l0;
                    gm_t89vrq05jgle5 t55;
                    gm_b16_8 t56 = gm_l8;
                    GM_ST(gm_b16_8, (uint8_t *)&t55 + 0, t56);
                    gm_b16_8 t57 = gm_l11;
                    GM_ST(gm_b16_8, (uint8_t *)&t55 + 16, t57);
                    gm_b16_8 t58 = gm_l6;
                    GM_ST(gm_b16_8, (uint8_t *)&t55 + 32, t58);
                    uint64_t t59 = gm_l10;
                    GM_ST(uint64_t, (uint8_t *)&t55 + 48, t59);
                    gm_b16_8 t60 = gm_l7;
                    GM_ST(gm_b16_8, (uint8_t *)&t55 + 56, t60);
                    gm_b0_1 t61 = gm_f_linef_t58vnnv((void *)NULL, cx, NULL, t54, &t55);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                goto ix49;
ie49:;
ix49:;
            }
            {
                void * t62 = gm_l0;
                gm_t88rcucg1b7uk t63 = gm_l4;
                gm_t3877pk7lv748 t64;
                gm_b16_8 t65 = gm_l6;
                GM_ST(gm_b16_8, (uint8_t *)&t64 + 0, t65);
                uint64_t t66 = gm_l10;
                GM_ST(uint64_t, (uint8_t *)&t64 + 16, t66);
                gm_tabb710d9mq2e t67;
                gm_f_format_gjujk09((void *)&t67, (void *)NULL, cx, NULL, &t64);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t67))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t67) + 8, gm_src0gtuh94b1fce, 597, 27);
                gm_b16_8 t68 = GM_LD(gm_b16_8, (((uint8_t *)&t67) + 8));
                gm_b16_8 t69 = GM_LD(gm_b16_8, ((uint8_t *)&t68));
                gm_b16_8 t70 = gm_f_load_0hebde7((void *)NULL, cx, NULL, t62, t63, t69);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
                ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t70);
                goto gm_fdone;
            }
            goto ix25;
ie25:;
            {
                gm_trap("reached `@unreachable()`", gm_src0gtuh94b1fce, 591, 52);
            }
ix25:;
        }
        goto ix17;
ie17:;
        {
            void * t73 = gm_l0;
            void * t74 = gm_l2;
            gm_t88rcucg1b7uk t75 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t74) + 0));
            gm_b16_8 t76 = gm_l6;
            gm_b16_8 t77 = gm_f_opt_present_joptd3k((void *)NULL, cx, NULL, t73, t75, t76);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l12 = t77;
        }
        {
            void * t79 = gm_l0;
            void * t80 = GM_LD(void *, (((uint8_t *)t79) + 0));
            uint8_t t81 = GM_LD(uint8_t, ((((uint8_t *)t80) + 1136) + 0));
            if (!t81) goto ie78;
            {
                void * t83 = gm_l0;
                gm_t6j6smj7qoctu t84;
                gm_b16_8 t85 = gm_l12;
                GM_ST(gm_b16_8, (uint8_t *)&t84 + 0, t85);
                gm_b16_8 t86 = gm_l7;
                GM_ST(gm_b16_8, (uint8_t *)&t84 + 16, t86);
                gm_b0_1 t87 = gm_f_linef_knskrol((void *)NULL, cx, NULL, t83, &t84);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix78;
ie78:;
ix78:;
        }
        {
            void * t88 = gm_l0;
            gm_t88rcucg1b7uk t89 = gm_l4;
            gm_b16_8 t90 = gm_l6;
            gm_b16_8 t91 = gm_f_load_0hebde7((void *)NULL, cx, NULL, t88, t89, t90);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
            ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t91);
            goto gm_fdone;
        }
ix17:;
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
inline __attribute__((aligned(4))) uint8_t gm_f_uses_effect_7j7sn5a(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2) {
    struct gm_fr_uses_effect_7j7sn5a F;
    void * gm_l1; /* e */
    uint8_t gm_l3; /* found */
    gm_tiilmfd16v3fc gm_l5; /* x */
    uint32_t gm_l6; /* f */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    gm_tiilmfd16v3fc gm_l9; /* x */
    GM_STACK_CHECK(&F, 435u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = a1;
    F.l2 = a2;
    {
        gm_l3 = ((uint8_t)0);
    }
    {
        void * t3 = gm_l1;
        uint8_t t4 = GM_LD(uint8_t, ((((uint8_t *)t3) + 8) + 0));
        if (t4 != ((uint8_t)34ULL)) goto ie2;
        gm_tsth9qu1dv0g3 t5 = GM_LD(gm_tsth9qu1dv0g3, (((((uint8_t *)t3) + 8) + 8) + 0));
        F.l4 = t5;
        {
            uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
            switch (t12) { case ((uint8_t)0ULL): goto ma9; case ((uint8_t)1ULL): goto ma8; case ((uint8_t)2ULL): goto ma10; default: goto ma11; }
ma8:;
            uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
            if (t14 != ((uint8_t)1ULL)) goto mn13;
            gm_tiilmfd16v3fc t15 = GM_LD(gm_tiilmfd16v3fc, ((((uint8_t *)&F.l4) + 8) + 0));
            gm_l5 = t15;
            {
                gm_tiilmfd16v3fc t17 = gm_l5;
                void * t18 = F.l2;
                gm_tiilmfd16v3fc t19 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)t18));
                uint8_t t20 = (gm_eqd5a8dm2ae7(((uint8_t *)&t17), ((uint8_t *)&t19)));
                gm_l3 = t20;
            }
            goto me7;
mn13:;
ma9:;
            uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
            if (t22 != ((uint8_t)0ULL)) goto mn21;
            uint32_t t23 = GM_LD(uint32_t, (((uint8_t *)&F.l4) + 8));
            gm_l6 = t23;
            {
                {
                    void * t26 = F.l0;
                    uint32_t t27 = gm_l6;
                    void * t28 = gm_f_func_in17r5n((void *)NULL, cx, NULL, t26, t27);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_b16_8 t29 = GM_LD(gm_b16_8, ((((uint8_t *)t28) + 72) + 0));
                    gm_l7 = t29;
                }
                {
                    gm_l8 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t31 = gm_l8;
                        uint64_t t32 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                        uint8_t t33 = (t31 < t32);
                        if (!t33) goto lb30;
                        {
                            uint64_t t36 = gm_l8;
                            uint64_t t37 = (uintptr_t)t36;
                            uint64_t t38 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                            gm_tiilmfd16v3fc t41 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t37 * 24));
                            gm_l9 = t41;
                            {
                                gm_tiilmfd16v3fc t44 = gm_l9;
                                void * t45 = F.l2;
                                gm_tiilmfd16v3fc t46 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)t45));
                                uint8_t t47 = (gm_eqd5a8dm2ae7(((uint8_t *)&t44), ((uint8_t *)&t46)));
                                if (!t47) goto ie43;
                                {
                                    gm_l3 = ((uint8_t)1);
                                }
                                goto ix43;
ie43:;
ix43:;
                            }
                            goto me39;
mn40:;
                            gm_trap("no match arm matched the scrutinee", gm_src0gtuh94b1fce, 1230, 17);
me39:;
                        }
lc30:;
                        {
                            uint64_t t50 = gm_l8;
                            uint64_t r51; bool o52 = __builtin_add_overflow(t50, ((uint64_t)1ULL), &r51);
                            uint64_t t53 = r51;
                            gm_l8 = t53;
                        }
                    }
lb30:;
                }
            }
            goto me7;
mn21:;
ma10:;
            uint8_t t55 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
            if (t55 != ((uint8_t)2ULL)) goto mn54;
            goto me7;
mn54:;
ma11:;
            gm_trap("no match arm matched the scrutinee", gm_src0gtuh94b1fce, 1227, 9);
me7:;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint8_t t57 = 0;
        uint8_t t58 = gm_l3;
        if (!t58) {
            void * t59 = gm_l1;
            gm_closure t60 = ((gm_closure){(void *)gm_f_fn_okfag60, (void *)&F.h, cx});
            uint8_t t61 = gm_f_any_child_ffc8h1k(t59, t60);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            t57 = t61;
        } else t57 = 1;
        uint8_t t62 = 0;
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
__attribute__((aligned(4))) void gm_f_view_addr_nj9md2j(void *gm_out, gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_view_addr_nj9md2j F;
    gm_b16_8 gm_l0; /* v */
    gm_b16_8 gm_l1; /* mark */
    uint64_t gm_l2; /* i */
    gm_b16_8 gm_l3; /* a */
    GM_STACK_CHECK(&F, 506u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t3 = gm_l0;
        gm_b16_8 t4;
        GM_ST(void *, (uint8_t *)&t4, (void *)(gm_somhu4vltiil1));
        GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(3));
        uint8_t t5 = gm_f_starts_with_6vvt01e(t3, t4);
        uint8_t t6 = (uint8_t)!t5;
        if (!t6) goto ie2;
        {
            gm_th2lqbo77pvsc t8;
            memset(&t8, 0, sizeof t8);
            uint8_t t9 = 0;
            F.ret = t8;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_b16_8 t10;
        GM_ST(void *, (uint8_t *)&t10, (void *)(gm_s8tfl6snglqci));
        GM_ST(uintptr_t, (uint8_t *)&t10 + 8, (uintptr_t)(18));
        gm_l1 = t10;
    }
    {
        gm_l2 = ((uint64_t)3ULL);
    }
    {
        for (;;) {
            uint64_t t12 = gm_l2;
            uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint64_t r14; bool o15 = __builtin_add_overflow(t12, t13, &r14);
            if (o15) gm_trap("integer overflow in `+`", gm_src0gtuh94b1fce, 77, 11);
            uint64_t t16 = r14;
            uint64_t t17 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            uint8_t t18 = (t16 <= t17);
            if (!t18) goto lb11;
            uint64_t t19 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            void * t20 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
            uint64_t t21 = gm_l2;
            uint64_t t22 = (uintptr_t)t21;
            uint64_t t23 = gm_l2;
            uint64_t t24 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint64_t r25; bool o26 = __builtin_add_overflow(t23, t24, &r25);
            if (o26) gm_trap("integer overflow in `+`", gm_src0gtuh94b1fce, 77, 47);
            uint64_t t27 = r25;
            uint64_t t28 = (uintptr_t)t27;
            if (t22 > t28 || t28 > (uintptr_t)t19) gm_trap_slice(t22, t28, t19, gm_src0gtuh94b1fce, 77, 42);
            gm_b16_8 t29;
            GM_ST(void *, (uint8_t *)&t29, (void *)(((uint8_t *)t20 + t22 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t29 + 8, (uintptr_t)(t28 - t22));
            gm_b16_8 t30 = gm_l1;
            uint8_t t31 = (gm_eq0iqiubfm7e(((uint8_t *)&t29), ((uint8_t *)&t30)));
            uint8_t t32 = (uint8_t)!t31;
            if (!t32) goto lb11;
            {
                uint64_t t35 = gm_l2;
                uint64_t r36; bool o37 = __builtin_add_overflow(t35, ((uint64_t)1ULL), &r36);
                if (o37) gm_trap("integer overflow in `+`", gm_src0gtuh94b1fce, 77, 72);
                uint64_t t38 = r36;
                gm_l2 = t38;
            }
lc11:;
        }
lb11:;
    }
    {
        uint64_t t40 = gm_l2;
        uint64_t t41 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint64_t r42; bool o43 = __builtin_add_overflow(t40, t41, &r42);
        if (o43) gm_trap("integer overflow in `+`", gm_src0gtuh94b1fce, 78, 8);
        uint64_t t44 = r42;
        uint64_t t45 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint8_t t46 = (t44 > t45);
        if (!t46) goto ie39;
        {
            gm_th2lqbo77pvsc t48;
            memset(&t48, 0, sizeof t48);
            uint8_t t49 = 0;
            F.ret = t48;
            goto gm_fret;
        }
        goto ix39;
ie39:;
ix39:;
    }
    {
        uint64_t t50 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        void * t51 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
        uint64_t t52 = gm_l2;
        uint64_t r53; bool o54 = __builtin_add_overflow(t52, ((uint64_t)3ULL), &r53);
        if (o54) gm_trap("integer overflow in `+`", gm_src0gtuh94b1fce, 79, 12);
        uint64_t t55 = r53;
        uint64_t t56 = (uintptr_t)t55;
        uint64_t t57 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint64_t r58; bool o59 = __builtin_sub_overflow(t57, ((uint64_t)1ULL), &r58);
        if (o59) gm_trap("integer overflow in `-`", gm_src0gtuh94b1fce, 79, 19);
        uint64_t t60 = r58;
        uint64_t t61 = (uintptr_t)t60;
        if (t56 > t61 || t61 > (uintptr_t)t50) gm_trap_slice(t56, t61, t50, gm_src0gtuh94b1fce, 79, 10);
        gm_b16_8 t62;
        GM_ST(void *, (uint8_t *)&t62, (void *)(((uint8_t *)t51 + t56 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t62 + 8, (uintptr_t)(t61 - t56));
        gm_l3 = t62;
    }
    {
        gm_b16_8 t64 = gm_l3;
        gm_t9geo0l9gngfn t65;
        gm_f_temp_piece_572g74q((void *)&t65, t64);
        uint8_t t66;
        if (((((uint8_t *)&t65))[24] != 0)) goto mn68;
        t66 = ((uint8_t)1);
        goto me67;
mn68:;
        t66 = ((uint8_t)0);
        goto me67;
mn69:;
        gm_trap("no match arm matched the scrutinee", gm_src0gtuh94b1fce, 80, 8);
me67:;
        if (!t66) goto ie63;
        {
            gm_th2lqbo77pvsc t71;
            memset(&t71, 0, sizeof t71);
            uint8_t t72 = 0;
            F.ret = t71;
            goto gm_fret;
        }
        goto ix63;
ie63:;
ix63:;
    }
    {
        gm_b16_8 t73 = gm_l3;
        GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
        ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 0, t73);
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_th2lqbo77pvsc gm_f_view_addr_nj9md2j_c(gm_b16_8 a0) { gm_th2lqbo77pvsc gm_r; gm_f_view_addr_nj9md2j(&gm_r, a0); return gm_r; }
__attribute__((aligned(4))) void gm_f_write_1uql06r(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2) {
    struct gm_fr_write_1uql06r F;
    void * gm_l0; /* fg */
    void * gm_l1; /* e */
    void * gm_l2; /* w */
    gm_tdlpv0q69lov1 gm_l3; /* sink_v */
    void * gm_l4; /* sx */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* i */
    gm_b16_8 gm_l9; /* v */
    gm_t88rcucg1b7uk gm_l10; /* str_ty */
    gm_t88rcucg1b7uk gm_l11; /* unit_ty */
    gm_th2lqbo77pvsc gm_l12; /* stub */
    gm_tiilmfd16v3fc gm_l13; /* effect_ */
    uint32_t gm_l14; /* op */
    uint32_t gm_l15; /* id */
    gm_tnqk2qt7j42o9 gm_l16; /* tys */
    gm_tep2drsl9408b gm_l17; /* cv */
    gm_b16_8 gm_l19; /* c */
    gm_b16_8 gm_l20; /* ccx */
    gm_b16_8 gm_l21; /* <tmp> */
    uint64_t gm_l22; /* <tmp> */
    gm_tiilmfd16v3fc gm_l23; /* eff */
    uint32_t gm_l24; /* id */
    gm_b16_8 gm_l25; /* n */
    gm_b16_8 gm_l26; /* fs */
    gm_b16_8 gm_l27; /* ff */
    gm_th2lqbo77pvsc gm_l28; /* direct */
    void * gm_l29; /* sx */
    uint32_t gm_l30; /* f */
    uint64_t gm_l31; /* <tmp> */
    uint64_t gm_l32; /* <tmp> */
    uint64_t gm_l33; /* i */
    gm_b16_8 gm_l34; /* d */
    gm_b16_8 gm_l35; /* sl */
    gm_b16_8 gm_l36; /* spec */
    gm_t7g88i52rrtm5 gm_l37; /* sp */
    gm_b16_8 gm_l38; /* a */
    uint64_t gm_l39; /* p */
    uint64_t gm_l40; /* p */
    gm_b16_8 gm_l41; /* core */
    gm_b16_8 gm_l42; /* ps */
    gm_b16_8 gm_l43; /* pf */
    gm_b16_8 gm_l44; /* at */
    uint64_t gm_l45; /* <tmp> */
    uint64_t gm_l46; /* <tmp> */
    uint64_t gm_l47; /* i */
    gm_b16_8 gm_l48; /* s */
    gm_b16_8 gm_l49; /* d */
    gm_tflpan0ud4tim gm_l50; /* custom */
    gm_t7g88i52rrtm5 gm_l51; /* sp */
    gm_t7g88i52rrtm5 gm_l52; /* sp */
    gm_b16_8 gm_l53; /* b */
    gm_b16_8 gm_l54; /* c */
    gm_b16_8 gm_l55; /* a */
    uint64_t gm_l56; /* p */
    uint64_t gm_l57; /* <tmp> */
    uint64_t gm_l58; /* <tmp> */
    uint64_t gm_l59; /* i */
    gm_b16_8 gm_l60; /* s */
    gm_b16_8 gm_l61; /* sl */
    gm_b16_8 gm_l62; /* d */
    gm_b16_8 gm_l63; /* b */
    gm_tflpan0ud4tim gm_l64; /* custom */
    gm_t7g88i52rrtm5 gm_l65; /* sp */
    gm_t7g88i52rrtm5 gm_l66; /* sp */
    gm_b16_8 gm_l67; /* c */
    GM_STACK_CHECK(&F, 8230u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        gm_tdlpv0q69lov1 t2;
        memset(&t2, 0, sizeof t2);
        gm_l3 = t2;
    }
    {
        void * t4 = gm_l2;
        uint8_t t5 = GM_LD(uint8_t, ((((uint8_t *)t4) + 0) + 0));
        if (t5 != ((uint8_t)1ULL)) goto ie3;
        void * t6 = GM_LD(void *, ((((uint8_t *)t4) + 0) + 8));
        gm_l4 = t6;
        {
            gm_tep2drsl9408b t8;
            memset((uint8_t *)&t8 + 20, 0, 4);
            void * t9 = gm_l0;
            void * t10 = gm_l4;
            gm_t5o0tpv5jka2l t11;
            gm_f_expr_porbc9t((void *)&t11, (void *)NULL, cx, NULL, t9, t10);
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
            gm_b16_8 t13 = GM_LD(gm_b16_8, (((uint8_t *)&t11) + 8));
            GM_ST(gm_b16_8, (uint8_t *)&t8 + 0, t13);
            void * t14 = gm_l4;
            gm_t88rcucg1b7uk t15 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t14) + 0));
            GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t8 + 16, t15);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tdlpv0q69lov1 *)((uint8_t *)&gm_l3)) + 25, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tdlpv0q69lov1 *)((uint8_t *)&gm_l3)) + 26, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tdlpv0q69lov1 *)((uint8_t *)&gm_l3)) + 28, 0);
            ((uint8_t *)&(*(gm_tdlpv0q69lov1 *)((uint8_t *)&gm_l3)))[24] = 1;
            GM_ST(gm_tep2drsl9408b, (uint8_t *)&(*(gm_tdlpv0q69lov1 *)((uint8_t *)&gm_l3)) + 0, t8);
        }
        goto ix3;
ie3:;
ix3:;
    }
    {
        gm_f_list_epvducg((void *)&F.l5, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_l7 = ((uint64_t)0ULL);
        }
        {
            void * t17 = gm_l2;
            uint64_t t18 = GM_LD(uintptr_t, (((uint8_t *)t17) + 72) + 8);
            gm_l6 = t18;
        }
        {
            for (;;) {
                uint64_t t20 = gm_l7;
                uint64_t t21 = gm_l6;
                uint8_t t22 = (t20 < t21);
                if (!t22) goto lb19;
                {
                    uint64_t t25 = gm_l7;
                    uint64_t t28 = GM_LD(uint64_t, ((uint8_t *)&t25));
                    gm_l8 = t28;
                    {
                        void * t30 = gm_l0;
                        uint64_t t31 = gm_l8;
                        uint64_t t32 = (uintptr_t)t31;
                        void * t33 = gm_l2;
                        uint64_t t34 = GM_LD(uintptr_t, (((uint8_t *)t33) + 72) + 8);
                        if (t32 >= (uintptr_t)(t34)) gm_trap_index(t32, t34, gm_src0gtuh94b1fce, 778, 27);
                        void * t35 = (void *)((uint8_t *)GM_LD(void *, (((uint8_t *)t33) + 72)) + t32 * 88);
                        gm_t5o0tpv5jka2l t36;
                        gm_f_expr_porbc9t((void *)&t36, (void *)NULL, cx, NULL, t30, t35);
                        if (gm_unw.kind) {
                            goto gm_ll23;
                        }
                        if (((((uint8_t *)&t36))[0] != 0)) {
                            gm_t5o0tpv5jka2l t37;
                            GM_ST(uint8_t, (uint8_t *)&t37 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t37 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t37 + 4, 0);
                            ((uint8_t *)&t37)[0] = 1;
                            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t37 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t36) + 8)));
                            F.ret = t37;
                            goto gm_fret;
                        }
                        gm_b16_8 t38 = GM_LD(gm_b16_8, (((uint8_t *)&t36) + 8));
                        gm_l9 = t38;
                    }
                    {
                        void * t39 = (void *)((uint8_t *)&F.l5);
                        void * t40 = gm_l0;
                        uint64_t t41 = gm_l8;
                        uint64_t t42 = (uintptr_t)t41;
                        void * t43 = gm_l2;
                        uint64_t t44 = GM_LD(uintptr_t, (((uint8_t *)t43) + 72) + 8);
                        if (t42 >= (uintptr_t)(t44)) gm_trap_index(t42, t44, gm_src0gtuh94b1fce, 779, 30);
                        gm_t88rcucg1b7uk t45 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, (((uint8_t *)t43) + 72)) + t42 * 88) + 0));
                        gm_b16_8 t46 = gm_l9;
                        gm_b16_8 t47 = gm_f_addr_of_hiaqkn0((void *)NULL, cx, NULL, t40, t45, t46);
                        if (gm_unw.kind) {
                            goto gm_ll23;
                        }
                        gm_tftb1odc739t5 t48 = gm_f_push_3ef746b(t39, t47);
                        if (((((uint8_t *)&t48))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t48) + 1, gm_src0gtuh94b1fce, 779, 9);
                        gm_b0_1 t49 = GM_LD(gm_b0_1, (((uint8_t *)&t48) + 1));
                    }
                    goto me26;
mn27:;
                    gm_trap("no match arm matched the scrutinee", gm_src0gtuh94b1fce, 777, 5);
me26:;
                }
lc19:;
                {
                    uint64_t t51 = gm_l7;
                    uint64_t r52; bool o53 = __builtin_add_overflow(t51, ((uint64_t)1ULL), &r52);
                    uint64_t t54 = r52;
                    gm_l7 = t54;
                }
            }
gm_ll23:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb19; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc19; }
            }
            goto gm_lad;
lb19:;
        }
    }
    {
        void * t55 = gm_l0;
        void * t56 = GM_LD(void *, (((uint8_t *)t55) + 0));
        void * t57 = (void *)(((uint8_t *)t56) + 8);
        gm_t88rcucg1b7uk t58 = gm_f_string_ty_kk94re3((void *)NULL, cx, NULL, t57);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l10 = t58;
    }
    {
        void * t59 = gm_l0;
        void * t60 = GM_LD(void *, (((uint8_t *)t59) + 0));
        void * t61 = (void *)(((uint8_t *)t60) + 8);
        gm_t88rcucg1b7uk t62 = gm_f_unit_ty_vgceilu((void *)NULL, cx, NULL, t61);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l11 = t62;
    }
    {
        gm_th2lqbo77pvsc t63;
        memset(&t63, 0, sizeof t63);
        gm_l12 = t63;
    }
    {
        void * t64 = gm_l2;
        uint8_t t67 = GM_LD(uint8_t, ((((uint8_t *)t64) + 0) + 0));
        if (t67 != ((uint8_t)0ULL)) goto mn66;
        gm_tiilmfd16v3fc t68 = GM_LD(gm_tiilmfd16v3fc, (((((uint8_t *)t64) + 0) + 8) + 0));
        gm_l13 = t68;
        uint32_t t69 = GM_LD(uint32_t, (((((uint8_t *)t64) + 0) + 8) + 24));
        gm_l14 = t69;
        {
            void * t71 = gm_l0;
            void * t72 = GM_LD(void *, (((uint8_t *)t71) + 0));
            void * t73 = (void *)((uint8_t *)&gm_l13);
            uint32_t t74 = gm_f_effect_id_5588nqa((void *)NULL, cx, NULL, t72, t73);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l15 = t74;
        }
        {
            gm_t88rcucg1b7uk t75 = gm_l10;
            gm_tnqk2qt7j42o9 t76;
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t76 + 0), t75);
            gm_l16 = t76;
        }
        {
            void * t77 = gm_l0;
            void * t78 = (void *)((uint8_t *)&gm_l13);
            uint32_t t79 = gm_l15;
            uint32_t t80 = gm_l14;
            void * t81 = ((uint8_t *)&gm_l16);
            if ((uintptr_t)0 > (uintptr_t)1 || (uintptr_t)1 > (uintptr_t)1) gm_trap_slice((uintptr_t)0, (uintptr_t)1, 1, gm_src0gtuh94b1fce, 789, 50);
            gm_b16_8 t82;
            GM_ST(void *, (uint8_t *)&t82, (void *)(((uint8_t *)t81 + (uintptr_t)0 * 4)));
            GM_ST(uintptr_t, (uint8_t *)&t82 + 8, (uintptr_t)((uintptr_t)1 - (uintptr_t)0));
            gm_b16_8 t83 = GM_LD(gm_b16_8, ((uint8_t *)&t82));
            gm_t88rcucg1b7uk t84 = gm_l11;
            gm_f_out_stub_ils4a69((void *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l12)), (void *)NULL, cx, NULL, t77, t78, t79, t80, t83, t84);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto me65;
mn66:;
        uint8_t t86 = GM_LD(uint8_t, ((((uint8_t *)t64) + 0) + 0));
        if (t86 != ((uint8_t)1ULL)) goto mn85;
        goto me65;
mn85:;
        gm_trap("no match arm matched the scrutinee", gm_src0gtuh94b1fce, 785, 5);
me65:;
    }
    {
        void * t89 = gm_l2;
        uint8_t t90 = GM_LD(uint8_t, ((((uint8_t *)t89) + 0) + 0));
        if (t90 != ((uint8_t)1ULL)) goto ie88;
        void * t91 = gm_l0;
        void * t92 = GM_LD(void *, (((uint8_t *)t91) + 0));
        void * t93 = (void *)(((uint8_t *)t92) + 8);
        gm_t88rcucg1b7uk t94 = gm_l10;
        uint8_t t95 = gm_f_by_ref_fi0o860((void *)NULL, cx, NULL, t93, t94);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t96 = (uint8_t)!t95;
        if (!t96) goto ie88;
        {
            gm_tdlpv0q69lov1 t98 = gm_l3;
            if (!((((uint8_t *)&t98))[24] != 0)) gm_trap("unwrap of null", gm_src0gtuh94b1fce, 796, 15);
            gm_l17 = (*(gm_tep2drsl9408b *)(((uint8_t *)&t98) + 0));
        }
        {
            void * t100 = gm_l0;
            void * t101 = GM_LD(void *, (((uint8_t *)t100) + 0));
            void * t102 = (void *)(((uint8_t *)t101) + 8);
            gm_t88rcucg1b7uk t103 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l17) + 16));
            gm_t53at80pfdus3 t104;
            gm_f_kind_ghvav22((void *)&t104, t102, GM_CONV(gm_csguankko4lmhd, t103));
            uint8_t t105 = GM_LD(uint8_t, (((uint8_t *)&t104) + 0));
            if (t105 != ((uint8_t)15ULL)) goto ie99;
            F.l18 = (*(gm_tddtjfggdat0d *)(((uint8_t *)&t104) + 8));
            {
                void * t107 = gm_l0;
                gm_t88rcucg1b7uk t108 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l17) + 16));
                gm_b16_8 t109 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l17) + 0));
                gm_b16_8 t110 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t107, t108, t109);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l19 = t110;
            }
            {
                void * t111 = gm_l0;
                uint32_t t112 = gm_f_fresh_5kokjmg(t111);
                gm_t88rcucg1b7uk t113;
                GM_ST(uint32_t, ((uint8_t *)&t113 + 0), t112);
                gm_tabb710d9mq2e t114;
                gm_f_format_qr5c6un((void *)&t114, (void *)NULL, cx, NULL, t113);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t114))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t114) + 8, gm_src0gtuh94b1fce, 799, 16);
                gm_b16_8 t115 = GM_LD(gm_b16_8, (((uint8_t *)&t114) + 8));
                gm_l20 = t115;
            }
            {
                void * t116 = gm_l0;
                gm_t6j6smj7qoctu t117;
                gm_b16_8 t118 = gm_l20;
                GM_ST(gm_b16_8, (uint8_t *)&t117 + 0, t118);
                gm_b16_8 t119 = gm_l19;
                GM_ST(gm_b16_8, (uint8_t *)&t117 + 16, t119);
                gm_b0_1 t120 = gm_f_linef_lot5c36((void *)NULL, cx, NULL, t116, &t117);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                {
                    gm_b16_8 t122 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l18) + 24) + 0));
                    gm_l21 = t122;
                }
                {
                    gm_l22 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t124 = gm_l22;
                        uint64_t t125 = GM_LD(uintptr_t, ((uint8_t *)&gm_l21) + 8);
                        uint8_t t126 = (t124 < t125);
                        if (!t126) goto lb123;
                        {
                            uint64_t t129 = gm_l22;
                            uint64_t t130 = (uintptr_t)t129;
                            uint64_t t131 = GM_LD(uintptr_t, ((uint8_t *)&gm_l21) + 8);
                            gm_tiilmfd16v3fc t134 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l21)) + t130 * 24));
                            gm_l23 = t134;
                            {
                                void * t136 = gm_l0;
                                void * t137 = GM_LD(void *, (((uint8_t *)t136) + 0));
                                void * t138 = (void *)((uint8_t *)&gm_l23);
                                uint32_t t139 = gm_f_effect_id_5588nqa((void *)NULL, cx, NULL, t137, t138);
                                if (gm_unw.kind) {
                                    goto gm_ll127;
                                }
                                gm_l24 = t139;
                            }
                            {
                                void * t140 = gm_l0;
                                uint32_t t141 = gm_f_fresh_5kokjmg(t140);
                                gm_t88rcucg1b7uk t142;
                                GM_ST(uint32_t, ((uint8_t *)&t142 + 0), t141);
                                gm_tabb710d9mq2e t143;
                                gm_f_format_mgf81l5((void *)&t143, (void *)NULL, cx, NULL, t142);
                                if (gm_unw.kind) {
                                    goto gm_ll127;
                                }
                                if (((((uint8_t *)&t143))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t143) + 8, gm_src0gtuh94b1fce, 803, 18);
                                gm_b16_8 t144 = GM_LD(gm_b16_8, (((uint8_t *)&t143) + 8));
                                gm_l25 = t144;
                            }
                            {
                                void * t145 = gm_l0;
                                gm_t586m754bse2v t146;
                                memset((uint8_t *)&t146 + 36, 0, 4);
                                gm_b16_8 t147 = gm_l25;
                                GM_ST(gm_b16_8, (uint8_t *)&t146 + 0, t147);
                                void * t148 = gm_l0;
                                gm_b16_8 t149 = GM_LD(gm_b16_8, (((uint8_t *)t148) + 280));
                                GM_ST(gm_b16_8, (uint8_t *)&t146 + 16, t149);
                                uint32_t t150 = gm_l24;
                                GM_ST(uint32_t, (uint8_t *)&t146 + 32, t150);
                                gm_b16_8 t151 = gm_l25;
                                GM_ST(gm_b16_8, (uint8_t *)&t146 + 40, t151);
                                gm_b16_8 t152 = gm_l25;
                                GM_ST(gm_b16_8, (uint8_t *)&t146 + 56, t152);
                                gm_b16_8 t153 = gm_l20;
                                GM_ST(gm_b16_8, (uint8_t *)&t146 + 72, t153);
                                gm_b16_8 t154 = gm_l20;
                                GM_ST(gm_b16_8, (uint8_t *)&t146 + 88, t154);
                                gm_b16_8 t155 = gm_l25;
                                GM_ST(gm_b16_8, (uint8_t *)&t146 + 104, t155);
                                gm_b0_1 t156 = gm_f_linef_ubknr7g((void *)NULL, cx, NULL, t145, &t146);
                                if (gm_unw.kind) {
                                    goto gm_ll127;
                                }
                            }
                            goto me132;
mn133:;
                            gm_trap("no match arm matched the scrutinee", gm_src0gtuh94b1fce, 801, 9);
me132:;
                        }
lc123:;
                        {
                            uint64_t t158 = gm_l22;
                            uint64_t r159; bool o160 = __builtin_add_overflow(t158, ((uint64_t)1ULL), &r159);
                            uint64_t t161 = r159;
                            gm_l22 = t161;
                        }
                    }
gm_ll127:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb123; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc123; }
                    }
                    goto gm_lad;
lb123:;
                }
            }
            {
                void * t162 = gm_l0;
                gm_t88rcucg1b7uk t163 = gm_l10;
                gm_b16_8 t164 = gm_f_fn_sink_stub_5ipff9h((void *)NULL, cx, NULL, t162, t163);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l26 = t164;
            }
            {
                void * t165 = gm_l0;
                gm_b16_8 t166 = gm_l26;
                gm_b16_8 t167 = gm_f_fn_format_stub_53r45gt((void *)NULL, cx, NULL, t165, t166);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l27 = t167;
            }
            {
                gm_th2lqbo77pvsc t168;
                memset(&t168, 0, sizeof t168);
                gm_l28 = t168;
            }
            {
                void * t170 = gm_l2;
                uint8_t t171 = GM_LD(uint8_t, ((((uint8_t *)t170) + 0) + 0));
                if (t171 != ((uint8_t)1ULL)) goto ie169;
                void * t172 = GM_LD(void *, ((((uint8_t *)t170) + 0) + 8));
                gm_l29 = t172;
                {
                    void * t175 = gm_l29;
                    uint8_t t176 = GM_LD(uint8_t, ((((uint8_t *)t175) + 8) + 0));
                    if (t176 != ((uint8_t)27ULL)) goto ie174;
                    uint32_t t177 = GM_LD(uint32_t, ((((uint8_t *)t175) + 8) + 8));
                    gm_l30 = t177;
                    {
                        void * t179 = gm_l0;
                        void * t180 = GM_LD(void *, (((uint8_t *)t179) + 0));
                        uint32_t t181 = gm_l30;
                        gm_b16_8 t182 = gm_f_fn_name_6n34573((void *)NULL, cx, NULL, t180, t181);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l28)) + 17, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l28)) + 18, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l28)) + 20, 0);
                        ((uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l28)))[16] = 1;
                        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l28)) + 0, t182);
                    }
                    goto ix174;
ie174:;
ix174:;
                }
                goto ix169;
ie169:;
ix169:;
            }
            {
                {
                    gm_l32 = ((uint64_t)0ULL);
                }
                {
                    void * t184 = gm_l2;
                    uint64_t t185 = GM_LD(uintptr_t, (((uint8_t *)t184) + 40) + 8);
                    gm_l31 = t185;
                }
                {
                    for (;;) {
                        uint64_t t187 = gm_l32;
                        uint64_t t188 = gm_l31;
                        uint8_t t189 = (t187 < t188);
                        if (!t189) goto lb186;
                        {
                            uint64_t t192 = gm_l32;
                            uint64_t t195 = GM_LD(uint64_t, ((uint8_t *)&t192));
                            gm_l33 = t195;
                            {
                                uint64_t t198 = gm_l33;
                                uint64_t t199 = (uintptr_t)t198;
                                void * t200 = gm_l2;
                                uint64_t t201 = GM_LD(uintptr_t, (((uint8_t *)t200) + 40) + 8);
                                if (t199 >= (uintptr_t)(t201)) gm_trap_index(t199, t201, gm_src0gtuh94b1fce, 814, 16);
                                uint64_t t202 = GM_LD(uintptr_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t200) + 40)) + t199 * 16) + 8);
                                uint8_t t203 = (t202 > ((uint64_t)0ULL));
                                if (!t203) goto ie197;
                                {
                                    if (!((((uint8_t *)&gm_l28))[16] != 0)) goto ie205;
                                    gm_b16_8 t206 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l28));
                                    gm_l34 = t206;
                                    {
                                        void * t208 = gm_l0;
                                        gm_t88rcucg1b7uk t209 = gm_l10;
                                        void * t210 = gm_l0;
                                        void * t211 = GM_LD(void *, (((uint8_t *)t210) + 0));
                                        uint64_t t212 = gm_l33;
                                        uint64_t t213 = (uintptr_t)t212;
                                        void * t214 = gm_l2;
                                        uint64_t t215 = GM_LD(uintptr_t, (((uint8_t *)t214) + 40) + 8);
                                        if (t213 >= (uintptr_t)(t215)) gm_trap_index(t213, t215, gm_src0gtuh94b1fce, 816, 67);
                                        gm_b16_8 t216 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)t214) + 40)) + t213 * 16));
                                        gm_b16_8 t217 = gm_f_static_bytes_u0uvco3((void *)NULL, cx, NULL, t211, t216);
                                        if (gm_unw.kind) {
                                            goto gm_ll190;
                                        }
                                        uint64_t t218 = gm_l33;
                                        uint64_t t219 = (uintptr_t)t218;
                                        void * t220 = gm_l2;
                                        uint64_t t221 = GM_LD(uintptr_t, (((uint8_t *)t220) + 40) + 8);
                                        if (t219 >= (uintptr_t)(t221)) gm_trap_index(t219, t221, gm_src0gtuh94b1fce, 816, 98);
                                        uint64_t t222 = GM_LD(uintptr_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t220) + 40)) + t219 * 16) + 8);
                                        gm_t4ce0jd3tdro4 t223;
                                        GM_ST(uint64_t, ((uint8_t *)&t223 + 0), t222);
                                        gm_tabb710d9mq2e t224;
                                        gm_f_format_sa1940l((void *)&t224, (void *)NULL, cx, NULL, t223);
                                        if (gm_unw.kind) {
                                            goto gm_ll190;
                                        }
                                        if (((((uint8_t *)&t224))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t224) + 8, gm_src0gtuh94b1fce, 816, 81);
                                        gm_b16_8 t225 = GM_LD(gm_b16_8, (((uint8_t *)&t224) + 8));
                                        gm_b16_8 t226 = GM_LD(gm_b16_8, ((uint8_t *)&t225));
                                        gm_b16_8 t227 = gm_f_make_slice_sa50efe((void *)NULL, cx, NULL, t208, t209, t217, t226);
                                        if (gm_unw.kind) {
                                            goto gm_ll190;
                                        }
                                        gm_l35 = t227;
                                    }
                                    {
                                        void * t228 = gm_l0;
                                        gm_t0uuhaljsf410 t229;
                                        gm_b16_8 t230 = gm_l34;
                                        GM_ST(gm_b16_8, (uint8_t *)&t229 + 0, t230);
                                        gm_b16_8 t231 = gm_l19;
                                        GM_ST(gm_b16_8, (uint8_t *)&t229 + 16, t231);
                                        gm_b16_8 t232 = gm_l20;
                                        GM_ST(gm_b16_8, (uint8_t *)&t229 + 32, t232);
                                        gm_b16_8 t233 = gm_l35;
                                        GM_ST(gm_b16_8, (uint8_t *)&t229 + 48, t233);
                                        gm_b0_1 t234 = gm_f_linef_amf91ik((void *)NULL, cx, NULL, t228, &t229);
                                        if (gm_unw.kind) {
                                            goto gm_ll190;
                                        }
                                    }
                                    {
                                        void * t235 = gm_l0;
                                        gm_b0_1 t236 = gm_f_check_unwind_2hji4bl((void *)NULL, cx, NULL, t235);
                                        if (gm_unw.kind) {
                                            goto gm_ll190;
                                        }
                                    }
                                    goto ix205;
ie205:;
                                    {
                                        void * t238 = gm_l0;
                                        gm_tjftu5su6fh8e t239;
                                        gm_b16_8 t240 = gm_l26;
                                        GM_ST(gm_b16_8, (uint8_t *)&t239 + 0, t240);
                                        gm_b16_8 t241 = gm_l19;
                                        GM_ST(gm_b16_8, (uint8_t *)&t239 + 16, t241);
                                        gm_b16_8 t242 = gm_l19;
                                        GM_ST(gm_b16_8, (uint8_t *)&t239 + 32, t242);
                                        gm_b16_8 t243 = gm_l20;
                                        GM_ST(gm_b16_8, (uint8_t *)&t239 + 48, t243);
                                        void * t244 = gm_l0;
                                        void * t245 = GM_LD(void *, (((uint8_t *)t244) + 0));
                                        uint64_t t246 = gm_l33;
                                        uint64_t t247 = (uintptr_t)t246;
                                        void * t248 = gm_l2;
                                        uint64_t t249 = GM_LD(uintptr_t, (((uint8_t *)t248) + 40) + 8);
                                        if (t247 >= (uintptr_t)(t249)) gm_trap_index(t247, t249, gm_src0gtuh94b1fce, 820, 115);
                                        gm_b16_8 t250 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)t248) + 40)) + t247 * 16));
                                        gm_b16_8 t251 = gm_f_static_bytes_u0uvco3((void *)NULL, cx, NULL, t245, t250);
                                        if (gm_unw.kind) {
                                            goto gm_ll190;
                                        }
                                        GM_ST(gm_b16_8, (uint8_t *)&t239 + 64, t251);
                                        uint64_t t252 = gm_l33;
                                        uint64_t t253 = (uintptr_t)t252;
                                        void * t254 = gm_l2;
                                        uint64_t t255 = GM_LD(uintptr_t, (((uint8_t *)t254) + 40) + 8);
                                        if (t253 >= (uintptr_t)(t255)) gm_trap_index(t253, t255, gm_src0gtuh94b1fce, 820, 129);
                                        uint64_t t256 = GM_LD(uintptr_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t254) + 40)) + t253 * 16) + 8);
                                        GM_ST(uint64_t, (uint8_t *)&t239 + 80, t256);
                                        gm_tabb710d9mq2e t257;
                                        gm_f_format_t9gsufu((void *)&t257, (void *)NULL, cx, NULL, &t239);
                                        if (gm_unw.kind) {
                                            goto gm_ll190;
                                        }
                                        if (((((uint8_t *)&t257))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t257) + 8, gm_src0gtuh94b1fce, 820, 36);
                                        gm_b16_8 t258 = GM_LD(gm_b16_8, (((uint8_t *)&t257) + 8));
                                        gm_b16_8 t259 = GM_LD(gm_b16_8, ((uint8_t *)&t258));
                                        gm_b0_1 t260 = gm_f_unwind_call_4k8ulav((void *)NULL, cx, NULL, t238, t259);
                                        if (gm_unw.kind) {
                                            goto gm_ll190;
                                        }
                                    }
ix205:;
                                }
                                goto ix197;
ie197:;
ix197:;
                            }
                            {
                                uint64_t t262 = gm_l33;
                                void * t263 = gm_l2;
                                uint64_t t264 = GM_LD(uintptr_t, (((uint8_t *)t263) + 72) + 8);
                                uint8_t t265 = (t262 < t264);
                                if (!t265) goto ie261;
                                {
                                    gm_b16_8 t267;
                                    GM_ST(void *, (uint8_t *)&t267, (void *)(gm_s04qus0luoga5));
                                    GM_ST(uintptr_t, (uint8_t *)&t267 + 8, (uintptr_t)(4));
                                    gm_l36 = t267;
                                }
                                {
                                    void * t269 = gm_l2;
                                    gm_b16_8 t270 = GM_LD(gm_b16_8, (((uint8_t *)t269) + 56));
                                    void * t271 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t270)));
                                    uint64_t t272 = (uintptr_t)GM_LD(uintptr_t, ((uint8_t *)&t270) + 8);
                                    uint64_t t273 = gm_l33;
                                    uint64_t t274 = (uintptr_t)t273;
                                    gm_tflpan0ud4tim t275;
                                    if (t274 < t272) {
                                    gm_t7g88i52rrtm5 t276 = GM_LD(gm_t7g88i52rrtm5, ((uint8_t *)t271 + t274 * 24));
                                    gm_tflpan0ud4tim t277;
                                    GM_ST(uint8_t, (uint8_t *)&t277 + 25, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t277 + 26, 0);
                                    GM_ST(gm_t7g88i52rrtm5, ((uint8_t *)&t277), t276);
                                    ((uint8_t *)&t277)[24] = 1;
                                        t275 = t277;
                                    } else {
                                    gm_tflpan0ud4tim t278;
                                    memset(&t278, 0, sizeof t278);
                                        t275 = t278;
                                    }
                                    if (!((((uint8_t *)&t275))[24] != 0)) goto ie268;
                                    gm_l37 = (*(gm_t7g88i52rrtm5 *)(((uint8_t *)&t275) + 0));
                                    {
                                        void * t281 = (void *)((uint8_t *)&gm_l37);
                                        uint8_t t282 = gm_f_is_default_spec_cdd8jng(t281);
                                        uint8_t t283 = (uint8_t)!t282;
                                        if (!t283) goto ie280;
                                        {
                                            gm_b16_8 t285 = gm_f_spec_literal_lmkpbc1((void *)NULL, cx, NULL, &(*(gm_t7g88i52rrtm5 *)((uint8_t *)&gm_l37)));
                                            if (gm_unw.kind) {
                                                goto gm_ll190;
                                            }
                                            gm_tnpifv20irnph t286;
                                            GM_ST(gm_b16_8, ((uint8_t *)&t286 + 0), t285);
                                            gm_tabb710d9mq2e t287;
                                            gm_f_format_h6b2ofo((void *)&t287, (void *)NULL, cx, NULL, t286);
                                            if (gm_unw.kind) {
                                                goto gm_ll190;
                                            }
                                            if (((((uint8_t *)&t287))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t287) + 8, gm_src0gtuh94b1fce, 826, 62);
                                            gm_b16_8 t288 = GM_LD(gm_b16_8, (((uint8_t *)&t287) + 8));
                                            gm_b16_8 t289 = GM_LD(gm_b16_8, ((uint8_t *)&t288));
                                            gm_l36 = t289;
                                        }
                                        goto ix280;
ie280:;
ix280:;
                                    }
                                    goto ix268;
ie268:;
ix268:;
                                }
                                {
                                    uint64_t t290 = gm_l33;
                                    uint64_t t291 = (uintptr_t)t290;
                                    uint64_t t292 = GM_LD(uintptr_t, (((uint8_t *)&F.l5) + 0) + 8);
                                    if (t291 >= (uintptr_t)(t292)) gm_trap_index(t291, t292, gm_src0gtuh94b1fce, 828, 22);
                                    gm_b16_8 t293 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l5) + 0)) + t291 * 16));
                                    gm_l38 = t293;
                                }
                                {
                                    uint8_t t295 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l36) + 8) == 4) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l36)), gm_s04qus0luoga5, 4) == 0));
                                    if (!t295) goto ie294;
                                    uint64_t t296 = gm_l33;
                                    uint64_t t297 = (uintptr_t)t296;
                                    void * t298 = gm_l2;
                                    uint64_t t299 = GM_LD(uintptr_t, (((uint8_t *)t298) + 72) + 8);
                                    if (t297 >= (uintptr_t)(t299)) gm_trap_index(t297, t299, gm_src0gtuh94b1fce, 829, 39);
                                    gm_t88rcucg1b7uk t300 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, (((uint8_t *)t298) + 72)) + t297 * 88) + 0));
                                    gm_t88rcucg1b7uk t301 = gm_l10;
                                    uint8_t t302 = (gm_eq9m6mvtmgde(((uint8_t *)&t300), ((uint8_t *)&t301)));
                                    if (!t302) goto ie294;
                                    uint8_t t303;
                                    if (((((uint8_t *)&gm_l28))[16] != 0)) goto mn305;
                                    t303 = ((uint8_t)0);
                                    goto me304;
mn305:;
                                    t303 = ((uint8_t)1);
                                    goto me304;
mn306:;
                                    gm_trap("no match arm matched the scrutinee", gm_src0gtuh94b1fce, 829, 66);
me304:;
                                    if (!t303) goto ie294;
                                    {
                                        void * t308 = gm_l0;
                                        uint64_t t309 = gm_f_ptr_bytes_eocneng(t308);
                                        gm_l39 = t309;
                                    }
                                    {
                                        void * t310 = gm_l0;
                                        gm_t3877pk7lv748 t311;
                                        gm_b16_8 t312 = gm_l38;
                                        GM_ST(gm_b16_8, (uint8_t *)&t311 + 0, t312);
                                        uint64_t t313 = gm_l39;
                                        GM_ST(uint64_t, (uint8_t *)&t311 + 16, t313);
                                        gm_b0_1 t314 = gm_f_linef_fch1rvr((void *)NULL, cx, NULL, t310, &t311);
                                        if (gm_unw.kind) {
                                            goto gm_ll190;
                                        }
                                    }
                                    {
                                        void * t315 = gm_l0;
                                        void * t316 = gm_l0;
                                        uint64_t t317 = GM_LD(uint64_t, (((uint8_t *)t316) + 224));
                                        uint64_t r318; bool o319 = __builtin_add_overflow(t317, ((uint64_t)1ULL), &r318);
                                        if (o319) gm_trap("integer overflow in `+`", gm_src0gtuh94b1fce, 833, 21);
                                        uint64_t t320 = r318;
                                        GM_ST(uint64_t, (((uint8_t *)t315) + 224), t320);
                                    }
                                    {
                                        void * t321 = gm_l0;
                                        gm_tud7oovfvnf43 t322;
                                        gm_th2lqbo77pvsc t323 = gm_l28;
                                        if (!((((uint8_t *)&t323))[16] != 0)) gm_trap("unwrap of null", gm_src0gtuh94b1fce, 834, 70);
                                        gm_b16_8 t324 = GM_LD(gm_b16_8, ((uint8_t *)&t323));
                                        GM_ST(gm_b16_8, (uint8_t *)&t322 + 0, t324);
                                        gm_b16_8 t325 = gm_l19;
                                        GM_ST(gm_b16_8, (uint8_t *)&t322 + 16, t325);
                                        gm_b16_8 t326 = gm_l20;
                                        GM_ST(gm_b16_8, (uint8_t *)&t322 + 32, t326);
                                        void * t327 = gm_l0;
                                        void * t328 = GM_LD(void *, (((uint8_t *)t327) + 0));
                                        void * t329 = (void *)(((uint8_t *)t328) + 8);
                                        gm_t88rcucg1b7uk t330 = gm_l10;
                                        gm_b16_8 t331 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t329, t330);
                                        if (gm_unw.kind) {
                                            goto gm_ll190;
                                        }
                                        GM_ST(gm_b16_8, (uint8_t *)&t322 + 48, t331);
                                        gm_b16_8 t332 = gm_l38;
                                        GM_ST(gm_b16_8, (uint8_t *)&t322 + 64, t332);
                                        gm_b0_1 t333 = gm_f_linef_um0rj1a((void *)NULL, cx, NULL, t321, &t322);
                                        if (gm_unw.kind) {
                                            goto gm_ll190;
                                        }
                                    }
                                    {
                                        void * t334 = gm_l0;
                                        gm_b0_1 t335 = gm_f_check_unwind_2hji4bl((void *)NULL, cx, NULL, t334);
                                        if (gm_unw.kind) {
                                            goto gm_ll190;
                                        }
                                    }
                                    {
                                        void * t336 = gm_l0;
                                        void * t337 = gm_l0;
                                        uint64_t t338 = GM_LD(uint64_t, (((uint8_t *)t337) + 224));
                                        uint64_t r339; bool o340 = __builtin_sub_overflow(t338, ((uint64_t)1ULL), &r339);
                                        if (o340) gm_trap("integer overflow in `-`", gm_src0gtuh94b1fce, 836, 21);
                                        uint64_t t341 = r339;
                                        GM_ST(uint64_t, (((uint8_t *)t336) + 224), t341);
                                    }
                                    {
                                        void * t342 = gm_l0;
                                        gm_b16_8 t343;
                                        GM_ST(void *, (uint8_t *)&t343, (void *)(gm_s2cuflhqkkuaq));
                                        GM_ST(uintptr_t, (uint8_t *)&t343 + 8, (uintptr_t)(1));
                                        gm_b0_1 t344 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t342, t343);
                                        if (gm_unw.kind) {
                                            goto gm_ll190;
                                        }
                                    }
                                    goto ix294;
ie294:;
                                    uint8_t t346 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l36) + 8) == 4) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l36)), gm_s04qus0luoga5, 4) == 0));
                                    if (!t346) goto ie345;
                                    uint64_t t347 = gm_l33;
                                    uint64_t t348 = (uintptr_t)t347;
                                    void * t349 = gm_l2;
                                    uint64_t t350 = GM_LD(uintptr_t, (((uint8_t *)t349) + 72) + 8);
                                    if (t348 >= (uintptr_t)(t350)) gm_trap_index(t348, t350, gm_src0gtuh94b1fce, 838, 46);
                                    gm_t88rcucg1b7uk t351 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, (((uint8_t *)t349) + 72)) + t348 * 88) + 0));
                                    gm_t88rcucg1b7uk t352 = gm_l10;
                                    uint8_t t353 = (gm_eq9m6mvtmgde(((uint8_t *)&t351), ((uint8_t *)&t352)));
                                    if (!t353) goto ie345;
                                    {
                                        void * t355 = gm_l0;
                                        uint64_t t356 = gm_f_ptr_bytes_eocneng(t355);
                                        gm_l40 = t356;
                                    }
                                    {
                                        void * t357 = gm_l0;
                                        gm_tjg6nvivto1u9 t358;
                                        gm_b16_8 t359 = gm_l38;
                                        GM_ST(gm_b16_8, (uint8_t *)&t358 + 0, t359);
                                        uint64_t t360 = gm_l40;
                                        GM_ST(uint64_t, (uint8_t *)&t358 + 16, t360);
                                        gm_b16_8 t361 = gm_l26;
                                        GM_ST(gm_b16_8, (uint8_t *)&t358 + 24, t361);
                                        gm_b16_8 t362 = gm_l19;
                                        GM_ST(gm_b16_8, (uint8_t *)&t358 + 40, t362);
                                        gm_b16_8 t363 = gm_l19;
                                        GM_ST(gm_b16_8, (uint8_t *)&t358 + 56, t363);
                                        gm_b16_8 t364 = gm_l20;
                                        GM_ST(gm_b16_8, (uint8_t *)&t358 + 72, t364);
                                        gm_b16_8 t365 = gm_l38;
                                        GM_ST(gm_b16_8, (uint8_t *)&t358 + 88, t365);
                                        gm_b16_8 t366 = gm_l38;
                                        GM_ST(gm_b16_8, (uint8_t *)&t358 + 104, t366);
                                        uint64_t t367 = gm_l40;
                                        GM_ST(uint64_t, (uint8_t *)&t358 + 120, t367);
                                        gm_tabb710d9mq2e t368;
                                        gm_f_format_2hfpta7((void *)&t368, (void *)NULL, cx, NULL, &t358);
                                        if (gm_unw.kind) {
                                            goto gm_ll190;
                                        }
                                        if (((((uint8_t *)&t368))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t368) + 8, gm_src0gtuh94b1fce, 841, 36);
                                        gm_b16_8 t369 = GM_LD(gm_b16_8, (((uint8_t *)&t368) + 8));
                                        gm_b16_8 t370 = GM_LD(gm_b16_8, ((uint8_t *)&t369));
                                        gm_b0_1 t371 = gm_f_unwind_call_4k8ulav((void *)NULL, cx, NULL, t357, t370);
                                        if (gm_unw.kind) {
                                            goto gm_ll190;
                                        }
                                    }
                                    goto ix345;
ie345:;
                                    {
                                        void * t373 = gm_l0;
                                        gm_th2d9c7aju8hp t374;
                                        gm_b16_8 t375 = gm_l27;
                                        GM_ST(gm_b16_8, (uint8_t *)&t374 + 0, t375);
                                        gm_b16_8 t376 = gm_l19;
                                        GM_ST(gm_b16_8, (uint8_t *)&t374 + 16, t376);
                                        gm_b16_8 t377 = gm_l19;
                                        GM_ST(gm_b16_8, (uint8_t *)&t374 + 32, t377);
                                        gm_b16_8 t378 = gm_l20;
                                        GM_ST(gm_b16_8, (uint8_t *)&t374 + 48, t378);
                                        void * t379 = gm_l0;
                                        void * t380 = GM_LD(void *, (((uint8_t *)t379) + 0));
                                        void * t381 = (void *)(((uint8_t *)t380) + 8);
                                        uint64_t t382 = gm_l33;
                                        uint64_t t383 = (uintptr_t)t382;
                                        void * t384 = gm_l2;
                                        uint64_t t385 = GM_LD(uintptr_t, (((uint8_t *)t384) + 72) + 8);
                                        if (t383 >= (uintptr_t)(t385)) gm_trap_index(t383, t385, gm_src0gtuh94b1fce, 843, 114);
                                        gm_t88rcucg1b7uk t386 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, (((uint8_t *)t384) + 72)) + t383 * 88) + 0));
                                        gm_b16_8 t387 = gm_f_desc_0cag2kq((void *)NULL, cx, NULL, t381, t386);
                                        if (gm_unw.kind) {
                                            goto gm_ll190;
                                        }
                                        GM_ST(gm_b16_8, (uint8_t *)&t374 + 64, t387);
                                        gm_b16_8 t388 = gm_l38;
                                        GM_ST(gm_b16_8, (uint8_t *)&t374 + 80, t388);
                                        gm_b16_8 t389 = gm_l36;
                                        GM_ST(gm_b16_8, (uint8_t *)&t374 + 96, t389);
                                        gm_tabb710d9mq2e t390;
                                        gm_f_format_rvm8ibl((void *)&t390, (void *)NULL, cx, NULL, &t374);
                                        if (gm_unw.kind) {
                                            goto gm_ll190;
                                        }
                                        if (((((uint8_t *)&t390))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t390) + 8, gm_src0gtuh94b1fce, 843, 36);
                                        gm_b16_8 t391 = GM_LD(gm_b16_8, (((uint8_t *)&t390) + 8));
                                        gm_b16_8 t392 = GM_LD(gm_b16_8, ((uint8_t *)&t391));
                                        gm_b0_1 t393 = gm_f_unwind_call_4k8ulav((void *)NULL, cx, NULL, t373, t392);
                                        if (gm_unw.kind) {
                                            goto gm_ll190;
                                        }
                                    }
ix345:;
ix294:;
                                }
                                goto ix261;
ie261:;
ix261:;
                            }
                            goto me193;
mn194:;
                            gm_trap("no match arm matched the scrutinee", gm_src0gtuh94b1fce, 813, 9);
me193:;
                        }
lc186:;
                        {
                            uint64_t t395 = gm_l32;
                            uint64_t r396; bool o397 = __builtin_add_overflow(t395, ((uint64_t)1ULL), &r396);
                            uint64_t t398 = r396;
                            gm_l32 = t398;
                        }
                    }
gm_ll190:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb186; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc186; }
                    }
                    goto gm_lad;
lb186:;
                }
            }
            {
                void * t399 = gm_l0;
                gm_b16_8 t400 = gm_f_unit_8ekedsl((void *)NULL, cx, NULL, t399);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
                ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
                GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t400);
                goto gm_fdone;
            }
            goto ix99;
ie99:;
            {
                gm_trap("reached `@unreachable()`", gm_src0gtuh94b1fce, 797, 46);
            }
ix99:;
        }
        goto ix88;
ie88:;
ix88:;
    }
    {
        if (!((((uint8_t *)&gm_l12))[16] != 0)) goto ie402;
        gm_b16_8 t403 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l12));
        gm_l41 = t403;
        {
            void * t405 = gm_l0;
            gm_b16_8 t406 = gm_l41;
            gm_t88rcucg1b7uk t407 = gm_l10;
            gm_b16_8 t408 = gm_f_piece_stub_m8mb7rc((void *)NULL, cx, NULL, t405, t406, ((uint8_t)115ULL), t407);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l42 = t408;
        }
        {
            void * t409 = gm_l0;
            gm_b16_8 t410 = gm_l41;
            gm_t88rcucg1b7uk t411 = gm_l10;
            gm_b16_8 t412 = gm_f_piece_stub_m8mb7rc((void *)NULL, cx, NULL, t409, t410, ((uint8_t)102ULL), t411);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l43 = t412;
        }
        {
            void * t413 = gm_l0;
            void * t414 = gm_l1;
            gm_b16_8 t415 = gm_f_loc_u9c0aqb((void *)NULL, cx, NULL, t413, t414);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l44 = t415;
        }
        {
            {
                gm_l46 = ((uint64_t)0ULL);
            }
            {
                void * t417 = gm_l2;
                uint64_t t418 = GM_LD(uintptr_t, (((uint8_t *)t417) + 40) + 8);
                gm_l45 = t418;
            }
            {
                for (;;) {
                    uint64_t t420 = gm_l46;
                    uint64_t t421 = gm_l45;
                    uint8_t t422 = (t420 < t421);
                    if (!t422) goto lb419;
                    {
                        uint64_t t425 = gm_l46;
                        uint64_t t428 = GM_LD(uint64_t, ((uint8_t *)&t425));
                        gm_l47 = t428;
                        {
                            uint64_t t431 = gm_l47;
                            uint64_t t432 = (uintptr_t)t431;
                            void * t433 = gm_l2;
                            uint64_t t434 = GM_LD(uintptr_t, (((uint8_t *)t433) + 40) + 8);
                            if (t432 >= (uintptr_t)(t434)) gm_trap_index(t432, t434, gm_src0gtuh94b1fce, 854, 16);
                            uint64_t t435 = GM_LD(uintptr_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t433) + 40)) + t432 * 16) + 8);
                            uint8_t t436 = (t435 > ((uint64_t)0ULL));
                            if (!t436) goto ie430;
                            {
                                void * t438 = gm_l0;
                                void * t439 = GM_LD(void *, (((uint8_t *)t438) + 0));
                                uint64_t t440 = gm_l47;
                                uint64_t t441 = (uintptr_t)t440;
                                void * t442 = gm_l2;
                                uint64_t t443 = GM_LD(uintptr_t, (((uint8_t *)t442) + 40) + 8);
                                if (t441 >= (uintptr_t)(t443)) gm_trap_index(t441, t443, gm_src0gtuh94b1fce, 855, 40);
                                gm_b16_8 t444 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)t442) + 40)) + t441 * 16));
                                gm_b16_8 t445 = gm_f_static_bytes_u0uvco3((void *)NULL, cx, NULL, t439, t444);
                                if (gm_unw.kind) {
                                    goto gm_ll423;
                                }
                                gm_l48 = t445;
                            }
                            {
                                void * t446 = gm_l0;
                                gm_t89vrq05jgle5 t447;
                                gm_b16_8 t448 = gm_l42;
                                GM_ST(gm_b16_8, (uint8_t *)&t447 + 0, t448);
                                void * t449 = gm_l0;
                                gm_b16_8 t450 = GM_LD(gm_b16_8, (((uint8_t *)t449) + 280));
                                GM_ST(gm_b16_8, (uint8_t *)&t447 + 16, t450);
                                gm_b16_8 t451 = gm_l48;
                                GM_ST(gm_b16_8, (uint8_t *)&t447 + 32, t451);
                                uint64_t t452 = gm_l47;
                                uint64_t t453 = (uintptr_t)t452;
                                void * t454 = gm_l2;
                                uint64_t t455 = GM_LD(uintptr_t, (((uint8_t *)t454) + 40) + 8);
                                if (t453 >= (uintptr_t)(t455)) gm_trap_index(t453, t455, gm_src0gtuh94b1fce, 856, 79);
                                uint64_t t456 = GM_LD(uintptr_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t454) + 40)) + t453 * 16) + 8);
                                GM_ST(uint64_t, (uint8_t *)&t447 + 48, t456);
                                gm_b16_8 t457 = gm_l44;
                                GM_ST(gm_b16_8, (uint8_t *)&t447 + 56, t457);
                                gm_tabb710d9mq2e t458;
                                gm_f_format_tbdgr6h((void *)&t458, (void *)NULL, cx, NULL, &t447);
                                if (gm_unw.kind) {
                                    goto gm_ll423;
                                }
                                if (((((uint8_t *)&t458))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t458) + 8, gm_src0gtuh94b1fce, 856, 32);
                                gm_b16_8 t459 = GM_LD(gm_b16_8, (((uint8_t *)&t458) + 8));
                                gm_b16_8 t460 = GM_LD(gm_b16_8, ((uint8_t *)&t459));
                                gm_b0_1 t461 = gm_f_unwind_call_4k8ulav((void *)NULL, cx, NULL, t446, t460);
                                if (gm_unw.kind) {
                                    goto gm_ll423;
                                }
                            }
                            goto ix430;
ie430:;
ix430:;
                        }
                        {
                            uint64_t t463 = gm_l47;
                            void * t464 = gm_l2;
                            uint64_t t465 = GM_LD(uintptr_t, (((uint8_t *)t464) + 72) + 8);
                            uint8_t t466 = (t463 < t465);
                            if (!t466) goto ie462;
                            {
                                void * t468 = gm_l0;
                                void * t469 = GM_LD(void *, (((uint8_t *)t468) + 0));
                                void * t470 = (void *)(((uint8_t *)t469) + 8);
                                uint64_t t471 = gm_l47;
                                uint64_t t472 = (uintptr_t)t471;
                                void * t473 = gm_l2;
                                uint64_t t474 = GM_LD(uintptr_t, (((uint8_t *)t473) + 72) + 8);
                                if (t472 >= (uintptr_t)(t474)) gm_trap_index(t472, t474, gm_src0gtuh94b1fce, 859, 35);
                                gm_t88rcucg1b7uk t475 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, (((uint8_t *)t473) + 72)) + t472 * 88) + 0));
                                gm_b16_8 t476 = gm_f_desc_0cag2kq((void *)NULL, cx, NULL, t470, t475);
                                if (gm_unw.kind) {
                                    goto gm_ll423;
                                }
                                gm_l49 = t476;
                            }
                            {
                                gm_tflpan0ud4tim t477;
                                memset(&t477, 0, sizeof t477);
                                gm_l50 = t477;
                            }
                            {
                                void * t479 = gm_l2;
                                gm_b16_8 t480 = GM_LD(gm_b16_8, (((uint8_t *)t479) + 56));
                                void * t481 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t480)));
                                uint64_t t482 = (uintptr_t)GM_LD(uintptr_t, ((uint8_t *)&t480) + 8);
                                uint64_t t483 = gm_l47;
                                uint64_t t484 = (uintptr_t)t483;
                                gm_tflpan0ud4tim t485;
                                if (t484 < t482) {
                                gm_t7g88i52rrtm5 t486 = GM_LD(gm_t7g88i52rrtm5, ((uint8_t *)t481 + t484 * 24));
                                gm_tflpan0ud4tim t487;
                                GM_ST(uint8_t, (uint8_t *)&t487 + 25, 0);
                                GM_ST(uint16_t, (uint8_t *)&t487 + 26, 0);
                                GM_ST(gm_t7g88i52rrtm5, ((uint8_t *)&t487), t486);
                                ((uint8_t *)&t487)[24] = 1;
                                    t485 = t487;
                                } else {
                                gm_tflpan0ud4tim t488;
                                memset(&t488, 0, sizeof t488);
                                    t485 = t488;
                                }
                                if (!((((uint8_t *)&t485))[24] != 0)) goto ie478;
                                gm_l51 = (*(gm_t7g88i52rrtm5 *)(((uint8_t *)&t485) + 0));
                                {
                                    void * t491 = (void *)((uint8_t *)&gm_l51);
                                    uint8_t t492 = gm_f_is_default_spec_cdd8jng(t491);
                                    uint8_t t493 = (uint8_t)!t492;
                                    if (!t493) goto ie490;
                                    {
                                        const uint8_t *t495 = ((uint8_t *)&gm_l51);
                                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tflpan0ud4tim *)((uint8_t *)&gm_l50)) + 25, 0);
                                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tflpan0ud4tim *)((uint8_t *)&gm_l50)) + 26, 0);
                                        ((uint8_t *)&(*(gm_tflpan0ud4tim *)((uint8_t *)&gm_l50)))[24] = 1;
                                        memmove((uint8_t *)&(*(gm_tflpan0ud4tim *)((uint8_t *)&gm_l50)) + 0, t495, 24);
                                    }
                                    goto ix490;
ie490:;
ix490:;
                                }
                                goto ix478;
ie478:;
ix478:;
                            }
                            {
                                if (!((((uint8_t *)&gm_l50))[24] != 0)) goto ie496;
                                gm_t7g88i52rrtm5 t497 = GM_LD(gm_t7g88i52rrtm5, ((uint8_t *)&gm_l50));
                                gm_l52 = t497;
                                {
                                    void * t499 = gm_l0;
                                    uint32_t t500 = gm_f_fresh_5kokjmg(t499);
                                    gm_t88rcucg1b7uk t501;
                                    GM_ST(uint32_t, ((uint8_t *)&t501 + 0), t500);
                                    gm_tabb710d9mq2e t502;
                                    gm_f_format_12uv53h((void *)&t502, (void *)NULL, cx, NULL, t501);
                                    if (gm_unw.kind) {
                                        goto gm_ll423;
                                    }
                                    if (((((uint8_t *)&t502))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t502) + 8, gm_src0gtuh94b1fce, 865, 26);
                                    gm_b16_8 t503 = GM_LD(gm_b16_8, (((uint8_t *)&t502) + 8));
                                    gm_l53 = t503;
                                }
                                {
                                    gm_b16_8 t504 = gm_f_spec_literal_lmkpbc1((void *)NULL, cx, NULL, &(*(gm_t7g88i52rrtm5 *)((uint8_t *)&gm_l52)));
                                    if (gm_unw.kind) {
                                        goto gm_ll423;
                                    }
                                    gm_l54 = t504;
                                }
                                {
                                    void * t505 = gm_l0;
                                    gm_b16_8 t506 = gm_l53;
                                    gm_tnpifv20irnph t507;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t507 + 0), t506);
                                    gm_b0_1 t508 = gm_f_linef_hg1enfq((void *)NULL, cx, NULL, t505, t507);
                                    if (gm_unw.kind) {
                                        goto gm_ll423;
                                    }
                                }
                                {
                                    void * t509 = gm_l0;
                                    gm_t0uuhaljsf410 t510;
                                    gm_b16_8 t511 = gm_l53;
                                    GM_ST(gm_b16_8, (uint8_t *)&t510 + 0, t511);
                                    gm_b16_8 t512 = gm_l49;
                                    GM_ST(gm_b16_8, (uint8_t *)&t510 + 16, t512);
                                    uint64_t t513 = gm_l47;
                                    uint64_t t514 = (uintptr_t)t513;
                                    uint64_t t515 = GM_LD(uintptr_t, (((uint8_t *)&F.l5) + 0) + 8);
                                    if (t514 >= (uintptr_t)(t515)) gm_trap_index(t514, t515, gm_src0gtuh94b1fce, 868, 73);
                                    gm_b16_8 t516 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l5) + 0)) + t514 * 16));
                                    GM_ST(gm_b16_8, (uint8_t *)&t510 + 32, t516);
                                    gm_b16_8 t517 = gm_l54;
                                    GM_ST(gm_b16_8, (uint8_t *)&t510 + 48, t517);
                                    gm_b0_1 t518 = gm_f_linef_qjbq226((void *)NULL, cx, NULL, t509, &t510);
                                    if (gm_unw.kind) {
                                        goto gm_ll423;
                                    }
                                }
                                {
                                    void * t519 = gm_l0;
                                    void * t520 = gm_l2;
                                    gm_b16_8 t521 = gm_l53;
                                    gm_b16_8 t522 = GM_LD(gm_b16_8, ((uint8_t *)&t521));
                                    void * t523 = gm_l1;
                                    gm_tqgtd5gtgj65s t524;
                                    gm_f_emit_buf_mqofii1((void *)&t524, (void *)NULL, cx, NULL, t519, &(*(gm_th1g69d7vpipg *)(((uint8_t *)t520) + 0)), &(*(gm_tdlpv0q69lov1 *)((uint8_t *)&gm_l3)), t522, t523);
                                    if (gm_unw.kind) {
                                        goto gm_ll423;
                                    }
                                    if (((((uint8_t *)&t524))[0] != 0)) {
                                        gm_t5o0tpv5jka2l t525;
                                        GM_ST(uint8_t, (uint8_t *)&t525 + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t525 + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t525 + 4, 0);
                                        ((uint8_t *)&t525)[0] = 1;
                                        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t525 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t524) + 8)));
                                        F.ret = t525;
                                        goto gm_fret;
                                    }
                                    gm_b0_1 t526 = GM_LD(gm_b0_1, (((uint8_t *)&t524) + 8));
                                }
                                goto ix496;
ie496:;
                                uint64_t t528 = gm_l47;
                                uint64_t t529 = (uintptr_t)t528;
                                void * t530 = gm_l2;
                                uint64_t t531 = GM_LD(uintptr_t, (((uint8_t *)t530) + 72) + 8);
                                if (t529 >= (uintptr_t)(t531)) gm_trap_index(t529, t531, gm_src0gtuh94b1fce, 870, 27);
                                gm_t88rcucg1b7uk t532 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, (((uint8_t *)t530) + 72)) + t529 * 88) + 0));
                                gm_t88rcucg1b7uk t533 = gm_l10;
                                uint8_t t534 = (gm_eq9m6mvtmgde(((uint8_t *)&t532), ((uint8_t *)&t533)));
                                if (!t534) goto ie527;
                                {
                                    uint64_t t536 = gm_l47;
                                    uint64_t t537 = (uintptr_t)t536;
                                    uint64_t t538 = GM_LD(uintptr_t, (((uint8_t *)&F.l5) + 0) + 8);
                                    if (t537 >= (uintptr_t)(t538)) gm_trap_index(t537, t538, gm_src0gtuh94b1fce, 872, 26);
                                    gm_b16_8 t539 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l5) + 0)) + t537 * 16));
                                    gm_l55 = t539;
                                }
                                {
                                    void * t540 = gm_l0;
                                    uint64_t t541 = gm_f_ptr_bytes_eocneng(t540);
                                    gm_l56 = t541;
                                }
                                {
                                    void * t542 = gm_l0;
                                    gm_tpbrs0bkofuth t543;
                                    gm_b16_8 t544 = gm_l55;
                                    GM_ST(gm_b16_8, (uint8_t *)&t543 + 0, t544);
                                    uint64_t t545 = gm_l56;
                                    GM_ST(uint64_t, (uint8_t *)&t543 + 16, t545);
                                    gm_b16_8 t546 = gm_l42;
                                    GM_ST(gm_b16_8, (uint8_t *)&t543 + 24, t546);
                                    void * t547 = gm_l0;
                                    gm_b16_8 t548 = GM_LD(gm_b16_8, (((uint8_t *)t547) + 280));
                                    GM_ST(gm_b16_8, (uint8_t *)&t543 + 40, t548);
                                    gm_b16_8 t549 = gm_l55;
                                    GM_ST(gm_b16_8, (uint8_t *)&t543 + 56, t549);
                                    gm_b16_8 t550 = gm_l55;
                                    GM_ST(gm_b16_8, (uint8_t *)&t543 + 72, t550);
                                    uint64_t t551 = gm_l56;
                                    GM_ST(uint64_t, (uint8_t *)&t543 + 88, t551);
                                    gm_b16_8 t552 = gm_l44;
                                    GM_ST(gm_b16_8, (uint8_t *)&t543 + 96, t552);
                                    gm_tabb710d9mq2e t553;
                                    gm_f_format_elsuh0a((void *)&t553, (void *)NULL, cx, NULL, &t543);
                                    if (gm_unw.kind) {
                                        goto gm_ll423;
                                    }
                                    if (((((uint8_t *)&t553))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t553) + 8, gm_src0gtuh94b1fce, 874, 36);
                                    gm_b16_8 t554 = GM_LD(gm_b16_8, (((uint8_t *)&t553) + 8));
                                    gm_b16_8 t555 = GM_LD(gm_b16_8, ((uint8_t *)&t554));
                                    gm_b0_1 t556 = gm_f_unwind_call_4k8ulav((void *)NULL, cx, NULL, t542, t555);
                                    if (gm_unw.kind) {
                                        goto gm_ll423;
                                    }
                                }
                                goto ix527;
ie527:;
                                {
                                    void * t558 = gm_l0;
                                    gm_tud7oovfvnf43 t559;
                                    gm_b16_8 t560 = gm_l43;
                                    GM_ST(gm_b16_8, (uint8_t *)&t559 + 0, t560);
                                    void * t561 = gm_l0;
                                    gm_b16_8 t562 = GM_LD(gm_b16_8, (((uint8_t *)t561) + 280));
                                    GM_ST(gm_b16_8, (uint8_t *)&t559 + 16, t562);
                                    gm_b16_8 t563 = gm_l49;
                                    GM_ST(gm_b16_8, (uint8_t *)&t559 + 32, t563);
                                    uint64_t t564 = gm_l47;
                                    uint64_t t565 = (uintptr_t)t564;
                                    uint64_t t566 = GM_LD(uintptr_t, (((uint8_t *)&F.l5) + 0) + 8);
                                    if (t565 >= (uintptr_t)(t566)) gm_trap_index(t565, t566, gm_src0gtuh94b1fce, 876, 83);
                                    gm_b16_8 t567 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l5) + 0)) + t565 * 16));
                                    GM_ST(gm_b16_8, (uint8_t *)&t559 + 48, t567);
                                    gm_b16_8 t568 = gm_l44;
                                    GM_ST(gm_b16_8, (uint8_t *)&t559 + 64, t568);
                                    gm_tabb710d9mq2e t569;
                                    gm_f_format_5b908c8((void *)&t569, (void *)NULL, cx, NULL, &t559);
                                    if (gm_unw.kind) {
                                        goto gm_ll423;
                                    }
                                    if (((((uint8_t *)&t569))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t569) + 8, gm_src0gtuh94b1fce, 876, 36);
                                    gm_b16_8 t570 = GM_LD(gm_b16_8, (((uint8_t *)&t569) + 8));
                                    gm_b16_8 t571 = GM_LD(gm_b16_8, ((uint8_t *)&t570));
                                    gm_b0_1 t572 = gm_f_unwind_call_4k8ulav((void *)NULL, cx, NULL, t558, t571);
                                    if (gm_unw.kind) {
                                        goto gm_ll423;
                                    }
                                }
ix527:;
ix496:;
                            }
                            goto ix462;
ie462:;
ix462:;
                        }
                        goto me426;
mn427:;
                        gm_trap("no match arm matched the scrutinee", gm_src0gtuh94b1fce, 853, 9);
me426:;
                    }
lc419:;
                    {
                        uint64_t t574 = gm_l46;
                        uint64_t r575; bool o576 = __builtin_add_overflow(t574, ((uint64_t)1ULL), &r575);
                        uint64_t t577 = r575;
                        gm_l46 = t577;
                    }
                }
gm_ll423:;
                if (gm_unw.frame == &F.h && gm_unw.label == 3) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb419; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc419; }
                }
                goto gm_lad;
lb419:;
            }
        }
        {
            void * t578 = gm_l0;
            gm_b16_8 t579 = gm_f_unit_8ekedsl((void *)NULL, cx, NULL, t578);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
            ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t579);
            goto gm_fdone;
        }
        goto ix402;
ie402:;
ix402:;
    }
    {
        {
            gm_l58 = ((uint64_t)0ULL);
        }
        {
            void * t581 = gm_l2;
            uint64_t t582 = GM_LD(uintptr_t, (((uint8_t *)t581) + 40) + 8);
            gm_l57 = t582;
        }
        {
            for (;;) {
                uint64_t t584 = gm_l58;
                uint64_t t585 = gm_l57;
                uint8_t t586 = (t584 < t585);
                if (!t586) goto lb583;
                {
                    uint64_t t589 = gm_l58;
                    uint64_t t592 = GM_LD(uint64_t, ((uint8_t *)&t589));
                    gm_l59 = t592;
                    {
                        uint64_t t595 = gm_l59;
                        uint64_t t596 = (uintptr_t)t595;
                        void * t597 = gm_l2;
                        uint64_t t598 = GM_LD(uintptr_t, (((uint8_t *)t597) + 40) + 8);
                        if (t596 >= (uintptr_t)(t598)) gm_trap_index(t596, t598, gm_src0gtuh94b1fce, 883, 12);
                        uint64_t t599 = GM_LD(uintptr_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t597) + 40)) + t596 * 16) + 8);
                        uint8_t t600 = (t599 > ((uint64_t)0ULL));
                        if (!t600) goto ie594;
                        {
                            void * t602 = gm_l0;
                            void * t603 = GM_LD(void *, (((uint8_t *)t602) + 0));
                            uint64_t t604 = gm_l59;
                            uint64_t t605 = (uintptr_t)t604;
                            void * t606 = gm_l2;
                            uint64_t t607 = GM_LD(uintptr_t, (((uint8_t *)t606) + 40) + 8);
                            if (t605 >= (uintptr_t)(t607)) gm_trap_index(t605, t607, gm_src0gtuh94b1fce, 884, 36);
                            gm_b16_8 t608 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)t606) + 40)) + t605 * 16));
                            gm_b16_8 t609 = gm_f_static_bytes_u0uvco3((void *)NULL, cx, NULL, t603, t608);
                            if (gm_unw.kind) {
                                goto gm_ll587;
                            }
                            gm_l60 = t609;
                        }
                        {
                            void * t610 = gm_l0;
                            gm_t88rcucg1b7uk t611 = gm_l10;
                            gm_b16_8 t612 = gm_l60;
                            uint64_t t613 = gm_l59;
                            uint64_t t614 = (uintptr_t)t613;
                            void * t615 = gm_l2;
                            uint64_t t616 = GM_LD(uintptr_t, (((uint8_t *)t615) + 40) + 8);
                            if (t614 >= (uintptr_t)(t616)) gm_trap_index(t614, t616, gm_src0gtuh94b1fce, 885, 61);
                            uint64_t t617 = GM_LD(uintptr_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t615) + 40)) + t614 * 16) + 8);
                            gm_t4ce0jd3tdro4 t618;
                            GM_ST(uint64_t, ((uint8_t *)&t618 + 0), t617);
                            gm_tabb710d9mq2e t619;
                            gm_f_format_sa1940l((void *)&t619, (void *)NULL, cx, NULL, t618);
                            if (gm_unw.kind) {
                                goto gm_ll587;
                            }
                            if (((((uint8_t *)&t619))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t619) + 8, gm_src0gtuh94b1fce, 885, 44);
                            gm_b16_8 t620 = GM_LD(gm_b16_8, (((uint8_t *)&t619) + 8));
                            gm_b16_8 t621 = GM_LD(gm_b16_8, ((uint8_t *)&t620));
                            gm_b16_8 t622 = gm_f_make_slice_sa50efe((void *)NULL, cx, NULL, t610, t611, t612, t621);
                            if (gm_unw.kind) {
                                goto gm_ll587;
                            }
                            gm_l61 = t622;
                        }
                        {
                            void * t623 = gm_l0;
                            void * t624 = gm_l2;
                            gm_b16_8 t625 = gm_l61;
                            void * t626 = gm_l1;
                            gm_tqgtd5gtgj65s t627;
                            gm_f_emit_to_o7js99p((void *)&t627, (void *)NULL, cx, NULL, t623, &(*(gm_th1g69d7vpipg *)(((uint8_t *)t624) + 0)), &(*(gm_tdlpv0q69lov1 *)((uint8_t *)&gm_l3)), t625, t626);
                            if (gm_unw.kind) {
                                goto gm_ll587;
                            }
                            if (((((uint8_t *)&t627))[0] != 0)) {
                                gm_t5o0tpv5jka2l t628;
                                GM_ST(uint8_t, (uint8_t *)&t628 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t628 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t628 + 4, 0);
                                ((uint8_t *)&t628)[0] = 1;
                                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t628 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t627) + 8)));
                                F.ret = t628;
                                goto gm_fret;
                            }
                            gm_b0_1 t629 = GM_LD(gm_b0_1, (((uint8_t *)&t627) + 8));
                        }
                        goto ix594;
ie594:;
ix594:;
                    }
                    {
                        uint64_t t631 = gm_l59;
                        void * t632 = gm_l2;
                        uint64_t t633 = GM_LD(uintptr_t, (((uint8_t *)t632) + 72) + 8);
                        uint8_t t634 = (t631 < t633);
                        if (!t634) goto ie630;
                        {
                            void * t636 = gm_l0;
                            void * t637 = GM_LD(void *, (((uint8_t *)t636) + 0));
                            void * t638 = (void *)(((uint8_t *)t637) + 8);
                            uint64_t t639 = gm_l59;
                            uint64_t t640 = (uintptr_t)t639;
                            void * t641 = gm_l2;
                            uint64_t t642 = GM_LD(uintptr_t, (((uint8_t *)t641) + 72) + 8);
                            if (t640 >= (uintptr_t)(t642)) gm_trap_index(t640, t642, gm_src0gtuh94b1fce, 889, 31);
                            gm_t88rcucg1b7uk t643 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, (((uint8_t *)t641) + 72)) + t640 * 88) + 0));
                            gm_b16_8 t644 = gm_f_desc_0cag2kq((void *)NULL, cx, NULL, t638, t643);
                            if (gm_unw.kind) {
                                goto gm_ll587;
                            }
                            gm_l62 = t644;
                        }
                        {
                            void * t645 = gm_l0;
                            uint32_t t646 = gm_f_fresh_5kokjmg(t645);
                            gm_t88rcucg1b7uk t647;
                            GM_ST(uint32_t, ((uint8_t *)&t647 + 0), t646);
                            gm_tabb710d9mq2e t648;
                            gm_f_format_12uv53h((void *)&t648, (void *)NULL, cx, NULL, t647);
                            if (gm_unw.kind) {
                                goto gm_ll587;
                            }
                            if (((((uint8_t *)&t648))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t648) + 8, gm_src0gtuh94b1fce, 890, 18);
                            gm_b16_8 t649 = GM_LD(gm_b16_8, (((uint8_t *)&t648) + 8));
                            gm_l63 = t649;
                        }
                        {
                            void * t650 = gm_l0;
                            gm_b16_8 t651 = gm_l63;
                            gm_tnpifv20irnph t652;
                            GM_ST(gm_b16_8, ((uint8_t *)&t652 + 0), t651);
                            gm_b0_1 t653 = gm_f_linef_hg1enfq((void *)NULL, cx, NULL, t650, t652);
                            if (gm_unw.kind) {
                                goto gm_ll587;
                            }
                        }
                        {
                            gm_tflpan0ud4tim t654;
                            memset(&t654, 0, sizeof t654);
                            gm_l64 = t654;
                        }
                        {
                            void * t656 = gm_l2;
                            gm_b16_8 t657 = GM_LD(gm_b16_8, (((uint8_t *)t656) + 56));
                            void * t658 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t657)));
                            uint64_t t659 = (uintptr_t)GM_LD(uintptr_t, ((uint8_t *)&t657) + 8);
                            uint64_t t660 = gm_l59;
                            uint64_t t661 = (uintptr_t)t660;
                            gm_tflpan0ud4tim t662;
                            if (t661 < t659) {
                            gm_t7g88i52rrtm5 t663 = GM_LD(gm_t7g88i52rrtm5, ((uint8_t *)t658 + t661 * 24));
                            gm_tflpan0ud4tim t664;
                            GM_ST(uint8_t, (uint8_t *)&t664 + 25, 0);
                            GM_ST(uint16_t, (uint8_t *)&t664 + 26, 0);
                            GM_ST(gm_t7g88i52rrtm5, ((uint8_t *)&t664), t663);
                            ((uint8_t *)&t664)[24] = 1;
                                t662 = t664;
                            } else {
                            gm_tflpan0ud4tim t665;
                            memset(&t665, 0, sizeof t665);
                                t662 = t665;
                            }
                            if (!((((uint8_t *)&t662))[24] != 0)) goto ie655;
                            gm_l65 = (*(gm_t7g88i52rrtm5 *)(((uint8_t *)&t662) + 0));
                            {
                                void * t668 = (void *)((uint8_t *)&gm_l65);
                                uint8_t t669 = gm_f_is_default_spec_cdd8jng(t668);
                                uint8_t t670 = (uint8_t)!t669;
                                if (!t670) goto ie667;
                                {
                                    const uint8_t *t672 = ((uint8_t *)&gm_l65);
                                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tflpan0ud4tim *)((uint8_t *)&gm_l64)) + 25, 0);
                                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tflpan0ud4tim *)((uint8_t *)&gm_l64)) + 26, 0);
                                    ((uint8_t *)&(*(gm_tflpan0ud4tim *)((uint8_t *)&gm_l64)))[24] = 1;
                                    memmove((uint8_t *)&(*(gm_tflpan0ud4tim *)((uint8_t *)&gm_l64)) + 0, t672, 24);
                                }
                                goto ix667;
ie667:;
ix667:;
                            }
                            goto ix655;
ie655:;
ix655:;
                        }
                        {
                            if (!((((uint8_t *)&gm_l64))[24] != 0)) goto ie673;
                            gm_t7g88i52rrtm5 t674 = GM_LD(gm_t7g88i52rrtm5, ((uint8_t *)&gm_l64));
                            gm_l66 = t674;
                            {
                                gm_b16_8 t676 = gm_f_spec_literal_lmkpbc1((void *)NULL, cx, NULL, &(*(gm_t7g88i52rrtm5 *)((uint8_t *)&gm_l66)));
                                if (gm_unw.kind) {
                                    goto gm_ll587;
                                }
                                gm_l67 = t676;
                            }
                            {
                                void * t677 = gm_l0;
                                gm_t0uuhaljsf410 t678;
                                gm_b16_8 t679 = gm_l63;
                                GM_ST(gm_b16_8, (uint8_t *)&t678 + 0, t679);
                                gm_b16_8 t680 = gm_l62;
                                GM_ST(gm_b16_8, (uint8_t *)&t678 + 16, t680);
                                uint64_t t681 = gm_l59;
                                uint64_t t682 = (uintptr_t)t681;
                                uint64_t t683 = GM_LD(uintptr_t, (((uint8_t *)&F.l5) + 0) + 8);
                                if (t682 >= (uintptr_t)(t683)) gm_trap_index(t682, t683, gm_src0gtuh94b1fce, 898, 69);
                                gm_b16_8 t684 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l5) + 0)) + t682 * 16));
                                GM_ST(gm_b16_8, (uint8_t *)&t678 + 32, t684);
                                gm_b16_8 t685 = gm_l67;
                                GM_ST(gm_b16_8, (uint8_t *)&t678 + 48, t685);
                                gm_b0_1 t686 = gm_f_linef_qjbq226((void *)NULL, cx, NULL, t677, &t678);
                                if (gm_unw.kind) {
                                    goto gm_ll587;
                                }
                            }
                            goto ix673;
ie673:;
                            {
                                void * t688 = gm_l0;
                                gm_tfeab279c16rr t689;
                                gm_b16_8 t690 = gm_l63;
                                GM_ST(gm_b16_8, (uint8_t *)&t689 + 0, t690);
                                gm_b16_8 t691 = gm_l62;
                                GM_ST(gm_b16_8, (uint8_t *)&t689 + 16, t691);
                                uint64_t t692 = gm_l59;
                                uint64_t t693 = (uintptr_t)t692;
                                uint64_t t694 = GM_LD(uintptr_t, (((uint8_t *)&F.l5) + 0) + 8);
                                if (t693 >= (uintptr_t)(t694)) gm_trap_index(t693, t694, gm_src0gtuh94b1fce, 900, 65);
                                gm_b16_8 t695 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l5) + 0)) + t693 * 16));
                                GM_ST(gm_b16_8, (uint8_t *)&t689 + 32, t695);
                                gm_b0_1 t696 = gm_f_linef_3dvovo0((void *)NULL, cx, NULL, t688, &t689);
                                if (gm_unw.kind) {
                                    goto gm_ll587;
                                }
                            }
ix673:;
                        }
                        {
                            void * t697 = gm_l0;
                            void * t698 = gm_l2;
                            gm_b16_8 t699 = gm_l63;
                            gm_b16_8 t700 = GM_LD(gm_b16_8, ((uint8_t *)&t699));
                            void * t701 = gm_l1;
                            gm_tqgtd5gtgj65s t702;
                            gm_f_emit_buf_mqofii1((void *)&t702, (void *)NULL, cx, NULL, t697, &(*(gm_th1g69d7vpipg *)(((uint8_t *)t698) + 0)), &(*(gm_tdlpv0q69lov1 *)((uint8_t *)&gm_l3)), t700, t701);
                            if (gm_unw.kind) {
                                goto gm_ll587;
                            }
                            if (((((uint8_t *)&t702))[0] != 0)) {
                                gm_t5o0tpv5jka2l t703;
                                GM_ST(uint8_t, (uint8_t *)&t703 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t703 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t703 + 4, 0);
                                ((uint8_t *)&t703)[0] = 1;
                                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t703 + 8), (*(gm_t08olq97rscn3 *)(((uint8_t *)&t702) + 8)));
                                F.ret = t703;
                                goto gm_fret;
                            }
                            gm_b0_1 t704 = GM_LD(gm_b0_1, (((uint8_t *)&t702) + 8));
                        }
                        goto ix630;
ie630:;
ix630:;
                    }
                    goto me590;
mn591:;
                    gm_trap("no match arm matched the scrutinee", gm_src0gtuh94b1fce, 882, 5);
me590:;
                }
lc583:;
                {
                    uint64_t t706 = gm_l58;
                    uint64_t r707; bool o708 = __builtin_add_overflow(t706, ((uint64_t)1ULL), &r707);
                    uint64_t t709 = r707;
                    gm_l58 = t709;
                }
            }
gm_ll587:;
            if (gm_unw.frame == &F.h && gm_unw.label == 4) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb583; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc583; }
            }
            goto gm_lad;
lb583:;
        }
    }
    {
        void * t710 = gm_l0;
        gm_b16_8 t711 = gm_f_unit_8ekedsl((void *)NULL, cx, NULL, t710);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8, t711);
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

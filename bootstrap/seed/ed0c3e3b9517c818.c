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
typedef struct gm_csfq64m1m1rt6m { int64_t f0; } gm_csfq64m1m1rt6m;
typedef struct gm_csguankko4lmhd { uint32_t f0; } gm_csguankko4lmhd;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint64_t f0; } gm_t4ce0jd3tdro4;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { void * f0; uint32_t f1; uint8_t pe[4]; } gm_tbl8il92l97ut;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { int64_t f0; } gm_tm98ap4112hee;
typedef struct { uint64_t f0; uint32_t f1; uint8_t pe[4]; } gm_tn68el8d14vku;
typedef struct { uint8_t f0; uint8_t p1[3]; union { int32_t v0; } f1; } gm_toe5fmr3vvf4d;
typedef struct { uint8_t a[4]; } gm_tpvg3mjn3dp06;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
_Static_assert(sizeof(gm_csfq64m1m1rt6m) == 8 && _Alignof(gm_csfq64m1m1rt6m) == 8 && offsetof(gm_csfq64m1m1rt6m, f0) == 0, "layout of `Dir`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_tm98ap4112hee f0; gm_b16_8 f1; } gm_t3olsle24rveu;
_Static_assert(sizeof(gm_t4ce0jd3tdro4) == 8 && _Alignof(gm_t4ce0jd3tdro4) == 8 && offsetof(gm_t4ce0jd3tdro4, f0) == 0, "layout of `Instant`");
typedef struct { gm_tbl8il92l97ut f0; uint8_t f1; uint8_t pe[7]; } gm_t5lf9i3n1r3co;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; gm_tm98ap4112hee v1; } f1; } gm_t62ebbi2i5rkd;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tn68el8d14vku v0; gm_tn68el8d14vku v1; gm_tf5srh4mp4b3i v2; uint8_t v3; void * v4; uint32_t v5; } f1; } gm_t76123gfet73g;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t79rbdv4rafe5;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
_Static_assert(sizeof(gm_tbl8il92l97ut) == 16 && _Alignof(gm_tbl8il92l97ut) == 8 && offsetof(gm_tbl8il92l97ut, f0) == 0 && offsetof(gm_tbl8il92l97ut, f1) == 8, "layout of `SpecCall`");
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; uint64_t v1; } f1; } gm_thf5kr0is9u9i;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { uint64_t f0; gm_tf5srh4mp4b3i f1; } gm_tk4qs2jduhfn8;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tm98ap4112hee) == 8 && _Alignof(gm_tm98ap4112hee) == 8 && offsetof(gm_tm98ap4112hee, f0) == 0, "layout of `File`");
_Static_assert(sizeof(gm_tn68el8d14vku) == 16 && _Alignof(gm_tn68el8d14vku) == 8 && offsetof(gm_tn68el8d14vku, f0) == 0 && offsetof(gm_tn68el8d14vku, f1) == 8, "layout of `(i: usize, x: u32)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; uint8_t p3[4]; gm_b16_8 f3; gm_tl874t2atnlh3 f4; uint8_t pe[4]; } gm_tnsjse4su5jue;
_Static_assert(sizeof(gm_toe5fmr3vvf4d) == 8 && _Alignof(gm_toe5fmr3vvf4d) == 4 && offsetof(gm_toe5fmr3vvf4d, f0) == 0 && offsetof(gm_toe5fmr3vvf4d, f1) == 4, "layout of `IoError`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_b16_8 f2; uint32_t f3; gm_tl874t2atnlh3 f4; } gm_tonn1ba3fjh5g;
typedef struct { gm_b16_8 a[1]; } gm_tpmdr2hmdhsqr;
typedef struct { gm_tm98ap4112hee f0; uint8_t f1; uint8_t pe[7]; } gm_tppo2hmut88dq;
_Static_assert(sizeof(gm_tpvg3mjn3dp06) == 4 && _Alignof(gm_tpvg3mjn3dp06) == 1 && offsetof(gm_tpvg3mjn3dp06, a) == 0, "layout of `[4]u8`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; void * v1; } f1; } gm_tq7s98ihonkju;
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { void * f0; uint32_t f1; uint8_t p2[4]; void * f2; gm_b16_8 f3; } gm_tsbt7g382sgof;
typedef struct { gm_b16_8 f0; uint16_t f1; uint8_t pe[6]; } gm_tvaas22dl64e0;
typedef struct { gm_t76123gfet73g f0; uint8_t f1; uint8_t pe[7]; } gm_t07lf02ct4q61;
_Static_assert(sizeof(gm_t3olsle24rveu) == 24 && _Alignof(gm_t3olsle24rveu) == 8 && offsetof(gm_t3olsle24rveu, f0) == 0 && offsetof(gm_t3olsle24rveu, f1) == 8, "layout of `(file: File, buf: []u8)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
_Static_assert(sizeof(gm_t5lf9i3n1r3co) == 24 && _Alignof(gm_t5lf9i3n1r3co) == 8 && offsetof(gm_t5lf9i3n1r3co, f0) == 0 && offsetof(gm_t5lf9i3n1r3co, f1) == 16, "layout of `?SpecCall`");
_Static_assert(sizeof(gm_t62ebbi2i5rkd) == 16 && _Alignof(gm_t62ebbi2i5rkd) == 8 && offsetof(gm_t62ebbi2i5rkd, f0) == 0 && offsetof(gm_t62ebbi2i5rkd, f1) == 8, "layout of `IoError!File`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t76123gfet73g) == 24 && _Alignof(gm_t76123gfet73g) == 8 && offsetof(gm_t76123gfet73g, f0) == 0 && offsetof(gm_t76123gfet73g, f1) == 8, "layout of `Flow`");
_Static_assert(sizeof(gm_t79rbdv4rafe5) == 40 && _Alignof(gm_t79rbdv4rafe5) == 8 && offsetof(gm_t79rbdv4rafe5, f0) == 0 && offsetof(gm_t79rbdv4rafe5, f1) == 8 && offsetof(gm_t79rbdv4rafe5, f2) == 24, "layout of `Block`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; } f1; } gm_tcfa6l7reoosn;
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_tk4qs2jduhfn8 v1; uint64_t v2; void * v3; } f1; } gm_tdkjirf1g83ud;
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
typedef struct { gm_thf5kr0is9u9i f0; gm_tppo2hmut88dq f1; } gm_tfvg1o4f8mmhi;
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tsbt7g382sgof v0; gm_tonn1ba3fjh5g v1; gm_t79rbdv4rafe5 v2; } f1; } gm_th5tat709fgha;
_Static_assert(sizeof(gm_thf5kr0is9u9i) == 16 && _Alignof(gm_thf5kr0is9u9i) == 8 && offsetof(gm_thf5kr0is9u9i, f0) == 0 && offsetof(gm_thf5kr0is9u9i, f1) == 8, "layout of `IoError!usize`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tk4qs2jduhfn8) == 24 && _Alignof(gm_tk4qs2jduhfn8) == 8 && offsetof(gm_tk4qs2jduhfn8, f0) == 0 && offsetof(gm_tk4qs2jduhfn8, f1) == 8, "layout of `(seen: u64, deadline: ?u64)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_t3olsle24rveu v1; gm_tvaas22dl64e0 v2; gm_tm98ap4112hee v3; gm_t3olsle24rveu v4; gm_t3olsle24rveu v5; } f1; } gm_tmvdbo28o1i6h;
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tnsjse4su5jue) == 48 && _Alignof(gm_tnsjse4su5jue) == 8 && offsetof(gm_tnsjse4su5jue, f0) == 0 && offsetof(gm_tnsjse4su5jue, f1) == 4 && offsetof(gm_tnsjse4su5jue, f2) == 8 && offsetof(gm_tnsjse4su5jue, f3) == 16 && offsetof(gm_tnsjse4su5jue, f4) == 32, "layout of `(fiber: u32, node: u32, op: u32, args: []const []u8, span: Span)`");
typedef struct { gm_trqfvmjtt4sij f0; uint32_t f1; uint32_t f2; uint64_t f3; void * f4; gm_tqs5p4n2tq19v f5; uint8_t pe[6]; } gm_tobnqifq2n0g5;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t p2[4]; gm_trqfvmjtt4sij f2; } gm_tolm6p7jp1hb3;
_Static_assert(sizeof(gm_tonn1ba3fjh5g) == 56 && _Alignof(gm_tonn1ba3fjh5g) == 8 && offsetof(gm_tonn1ba3fjh5g, f0) == 0 && offsetof(gm_tonn1ba3fjh5g, f1) == 16 && offsetof(gm_tonn1ba3fjh5g, f2) == 24 && offsetof(gm_tonn1ba3fjh5g, f3) == 40 && offsetof(gm_tonn1ba3fjh5g, f4) == 44, "layout of `(f: []u8, fty: Ty, arg: []u8, cx: u32, span: Span)`");
_Static_assert(sizeof(gm_tpmdr2hmdhsqr) == 16 && _Alignof(gm_tpmdr2hmdhsqr) == 8 && offsetof(gm_tpmdr2hmdhsqr, a) == 0, "layout of `[1]string`");
_Static_assert(sizeof(gm_tppo2hmut88dq) == 16 && _Alignof(gm_tppo2hmut88dq) == 8 && offsetof(gm_tppo2hmut88dq, f0) == 0 && offsetof(gm_tppo2hmut88dq, f1) == 8, "layout of `?Child`");
_Static_assert(sizeof(gm_tq7s98ihonkju) == 16 && _Alignof(gm_tq7s98ihonkju) == 8 && offsetof(gm_tq7s98ihonkju, f0) == 0 && offsetof(gm_tq7s98ihonkju, f1) == 8, "layout of `AllocError!^HandlerDecl`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_tsbt7g382sgof) == 40 && _Alignof(gm_tsbt7g382sgof) == 8 && offsetof(gm_tsbt7g382sgof, f0) == 0 && offsetof(gm_tsbt7g382sgof, f1) == 8 && offsetof(gm_tsbt7g382sgof, f2) == 16 && offsetof(gm_tsbt7g382sgof, f3) == 24, "layout of `(fr: ^Frame, cx: u32, body: ^Node, dst: []u8)`");
_Static_assert(sizeof(gm_tvaas22dl64e0) == 24 && _Alignof(gm_tvaas22dl64e0) == 8 && offsetof(gm_tvaas22dl64e0, f0) == 0 && offsetof(gm_tvaas22dl64e0, f1) == 16, "layout of `(string, u16)`");
_Static_assert(sizeof(gm_t07lf02ct4q61) == 32 && _Alignof(gm_t07lf02ct4q61) == 8 && offsetof(gm_t07lf02ct4q61, f0) == 0 && offsetof(gm_t07lf02ct4q61, f1) == 24, "layout of `?Flow`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
typedef struct { gm_tmvdbo28o1i6h f0; gm_tfvg1o4f8mmhi f1; uint32_t f2; uint8_t pe[4]; } gm_t683gnj9k3p7d;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; gm_tfvg1o4f8mmhi v1; } f1; } gm_t6j8vdmi81h7c;
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_tdkjirf1g83ud f1; uint8_t f2; uint8_t pe[7]; } gm_t9jnd88rq7ul8;
_Static_assert(sizeof(gm_tcfa6l7reoosn) == 32 && _Alignof(gm_tcfa6l7reoosn) == 8 && offsetof(gm_tcfa6l7reoosn, f0) == 0 && offsetof(gm_tcfa6l7reoosn, f1) == 8, "layout of `Flow!()`");
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_tdkjirf1g83ud) == 32 && _Alignof(gm_tdkjirf1g83ud) == 8 && offsetof(gm_tdkjirf1g83ud, f0) == 0 && offsetof(gm_tdkjirf1g83ud, f1) == 8, "layout of `ThreadState`");
_Static_assert(sizeof(gm_tfvg1o4f8mmhi) == 32 && _Alignof(gm_tfvg1o4f8mmhi) == 8 && offsetof(gm_tfvg1o4f8mmhi, f0) == 0 && offsetof(gm_tfvg1o4f8mmhi, f1) == 16, "layout of `HostResult`");
_Static_assert(sizeof(gm_th5tat709fgha) == 64 && _Alignof(gm_th5tat709fgha) == 8 && offsetof(gm_th5tat709fgha, f0) == 0 && offsetof(gm_th5tat709fgha, f1) == 8, "layout of `Start`");
_Static_assert(sizeof(gm_tmvdbo28o1i6h) == 32 && _Alignof(gm_tmvdbo28o1i6h) == 8 && offsetof(gm_tmvdbo28o1i6h, f0) == 0 && offsetof(gm_tmvdbo28o1i6h, f1) == 8, "layout of `HostOp`");
_Static_assert(sizeof(gm_tobnqifq2n0g5) == 72 && _Alignof(gm_tobnqifq2n0g5) == 8 && offsetof(gm_tobnqifq2n0g5, f0) == 0 && offsetof(gm_tobnqifq2n0g5, f1) == 40 && offsetof(gm_tobnqifq2n0g5, f2) == 44 && offsetof(gm_tobnqifq2n0g5, f3) == 48 && offsetof(gm_tobnqifq2n0g5, f4) == 56 && offsetof(gm_tobnqifq2n0g5, f5) == 64, "layout of `Threads`");
_Static_assert(sizeof(gm_tolm6p7jp1hb3) == 72 && _Alignof(gm_tolm6p7jp1hb3) == 8 && offsetof(gm_tolm6p7jp1hb3, f0) == 0 && offsetof(gm_tolm6p7jp1hb3, f1) == 16 && offsetof(gm_tolm6p7jp1hb3, f2) == 32, "layout of `EvalTrap`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_t07lf02ct4q61 f1; } gm_tslj4ghflekdk;
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_t683gnj9k3p7d) == 72 && _Alignof(gm_t683gnj9k3p7d) == 8 && offsetof(gm_t683gnj9k3p7d, f0) == 0 && offsetof(gm_t683gnj9k3p7d, f1) == 32 && offsetof(gm_t683gnj9k3p7d, f2) == 64, "layout of `HostCall`");
_Static_assert(sizeof(gm_t6j8vdmi81h7c) == 40 && _Alignof(gm_t6j8vdmi81h7c) == 8 && offsetof(gm_t6j8vdmi81h7c, f0) == 0 && offsetof(gm_t6j8vdmi81h7c, f1) == 8, "layout of `Flow!HostResult`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_tnsjse4su5jue v1; gm_tslj4ghflekdk v2; } f1; } gm_t9hl37efuvhj8;
_Static_assert(sizeof(gm_t9jnd88rq7ul8) == 48 && _Alignof(gm_t9jnd88rq7ul8) == 8 && offsetof(gm_t9jnd88rq7ul8, f0) == 0 && offsetof(gm_t9jnd88rq7ul8, f1) == 8 && offsetof(gm_t9jnd88rq7ul8, f2) == 40, "layout of `ThreadRec`");
_Static_assert(sizeof(gm_tslj4ghflekdk) == 40 && _Alignof(gm_tslj4ghflekdk) == 8 && offsetof(gm_tslj4ghflekdk, f0) == 0 && offsetof(gm_tslj4ghflekdk, f1) == 8, "layout of `(fiber: u32, flow: ?Flow)`");
_Static_assert(sizeof(gm_t9hl37efuvhj8) == 56 && _Alignof(gm_t9hl37efuvhj8) == 8 && offsetof(gm_t9hl37efuvhj8, f0) == 0 && offsetof(gm_t9hl37efuvhj8, f1) == 8, "layout of `Msg`");
struct gm_fr_check_fatal_dgfalcm {
    gm_frame h;
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_connected_a7ps01v {
    gm_frame h;
    gm_tfvg1o4f8mmhi ret;
};
struct gm_fr_copy_from_f7j6p05 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_destroy_o4n4nl0 {
    gm_frame h;
    gm_tb40qn7hk0tl9 l1; /* site */
    gm_b0_1 ret;
};
struct gm_fr_empty_slice_5j6fqpv {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_8othtri_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_env_exit_prbvpjt {
    gm_frame h;
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_fill_abmf5g5 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_04kmcqn {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_func_ene90ss {
    gm_frame h;
    void * ret;
};
struct gm_fr_get_u64_tgpb2vh {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_host_main_55puiu2 {
    gm_frame h;
    void * l0; /* j */
    gm_b0_1 ret;
};
struct gm_fr_int_ci6airg {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_join_r8j2dp1 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_list_j8jr9cr_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_jh4e1ji {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_new_coro_06nq12a {
    gm_frame h;
    gm_th5tat709fgha l1; /* start */
    uint32_t ret;
};
struct gm_fr_on_host_thread_kmegc07 {
    gm_frame h;
    gm_t6j8vdmi81h7c ret;
};
struct gm_fr_ptr_of_b77b4c9 {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_rd_word_8flcq6o {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_runnable_o1q905g {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_sleep_gqtie06 {
    gm_frame h;
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_switch_thread_m7aoqko {
    gm_frame h;
    gm_t9jnd88rq7ul8 l8; /* t */
    gm_t9jnd88rq7ul8 l18; /* t */
    gm_t9jnd88rq7ul8 l25; /* t */
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_thread_main_n6r943c {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_thread_op_pfli7b1 {
    gm_frame h;
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_threads_kh5dldj {
    gm_frame h;
    gm_trqfvmjtt4sij l0; /* l */
    gm_tobnqifq2n0g5 ret;
};
struct gm_fr_tick_6bsfu3n {
    gm_frame h;
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_tmp_8k6u9c9 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_us_cuv01fp {
    gm_frame h;
    uint64_t ret;
};
__attribute__((aligned(4))) uint32_t gm_f_add_coro_k13bkpe(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, uint64_t a2, uint32_t a3);
__attribute__((aligned(4))) void gm_f_call_function_u7ovth6(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, uint32_t a2, gm_b16_8 a3, void * a4, uint32_t a5, const void * a6, uint8_t a7, gm_b16_8 a8);
__attribute__((aligned(4))) void gm_f_check_fatal_dgfalcm(void *gm_out, void * a0);
__attribute__((aligned(4))) gm_tcfa6l7reoosn gm_f_check_fatal_dgfalcm_c(void * a0);
__attribute__((aligned(4))) void gm_f_connected_a7ps01v(void *gm_out, gm_t62ebbi2i5rkd a0);
__attribute__((aligned(4))) gm_tfvg1o4f8mmhi gm_f_connected_a7ps01v_c(gm_t62ebbi2i5rkd a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_copy_from_f7j6p05(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_defaults_p077jgf(gm_closure a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_destroy_o4n4nl0(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5j6fqpv(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8othtri_2(void);
__attribute__((aligned(4))) void gm_f_env_exit_prbvpjt(void *gm_out, void * a0, uint8_t a1);
__attribute__((aligned(4))) gm_tcfa6l7reoosn gm_f_env_exit_prbvpjt_c(void * a0, uint8_t a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_fill_abmf5g5(gm_b16_8 a0, uint8_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_04kmcqn(void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_fn_from_addr_7a6bpqd(void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_format_uh8vg1k(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_free_rkuh2rm(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1);
inline __attribute__((aligned(4))) void * gm_f_func_ene90ss(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) void * gm_f_func_in17r5n(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) unsigned __int128 gm_f_get_int_pg7p4gc(void * a0, gm_csguankko4lmhd a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) uint64_t gm_f_get_u64_tgpb2vh(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) unsigned __int128 gm_f_get_uint_bq8c4b7(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_handoff_mtsh8r2(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, const void * a2);
inline __attribute__((aligned(4))) void gm_f_host_main_55puiu2(void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_int_ci6airg(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, uint16_t a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_intern_ia19mhl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_join_r8j2dp1(gm_csfq64m1m1rt6m a0);
inline __attribute__((aligned(4))) void gm_f_list_j8jr9cr_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_jh4e1ji(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_load_at_6u81jcc(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, uint64_t a2, uint64_t a3, gm_b16_8 a4);
__attribute__((aligned(4))) void gm_f_make_2ip78bj(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0, const void * a1);
inline __attribute__((aligned(4))) uint32_t gm_f_new_coro_06nq12a(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, uint32_t a2);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_g0l0iur(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_jd11mvt(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_on_host_thread_kmegc07(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, const void * a2);
inline __attribute__((aligned(4))) uint64_t gm_f_ptr_of_b77b4c9(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_bn7ud1g(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_put_uint_cddin99(void * a0, gm_b16_8 a1, unsigned __int128 a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_rd_word_8flcq6o(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_runnable_o1q905g(void * a0, void * a1, gm_tf5srh4mp4b3i a2);
__attribute__((aligned(4))) uint64_t gm_f_size_hr4b76n(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_sleep_gqtie06(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, uint64_t a2);
__attribute__((aligned(4))) gm_t62ebbi2i5rkd gm_f_spawn_2tmnfpi(void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_switch_thread_m7aoqko(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_thread_main_n6r943c(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void gm_f_thread_op_pfli7b1(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t9qkh0rsooc8d a2, gm_b16_8 a3, gm_b16_8 a4);
__attribute__((aligned(4))) void gm_f_threads_kh5dldj(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_tick_6bsfu3n(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_b16_8 gm_f_tmp_8k6u9c9(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) __attribute__((noinline)) gm_b16_8 gm_f_tmp_next_dalgo1f(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_trap_qh84l9d(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_us_cuv01fp(uint64_t a0);
extern uint8_t gm_cusfosftpfn[4] __attribute__((aligned(4)));
extern uint8_t gm_k02l1tj96tm[40] __attribute__((aligned(8)));
extern uint8_t gm_ki1ml5fl8pd[40] __attribute__((aligned(8)));
extern uint8_t gm_kjs2b0icc8d[40] __attribute__((aligned(8)));
extern uint8_t gm_klqd6h436uv[40] __attribute__((aligned(8)));
extern uint8_t gm_kovthkfk7ta[40] __attribute__((aligned(8)));
extern uint8_t gm_kujrnb073g1[40] __attribute__((aligned(8)));
extern const uint8_t gm_s632oftf9nssu[43];
extern const uint8_t gm_sb5pfucassrk8[34];
extern const uint8_t gm_sqofr58g480ri[39];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcc24pnihhgkug[];
extern const char gm_srchntnl6m6g3am[];
extern const char gm_srcnqtpfma6qe4a[];
extern const char gm_srcp0l171g2m7ho[];
extern const char gm_srcpit8mdn0781q[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_svu0pqir618v4[39];
extern const gm_type gm_tyo86ks9qkvt;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_destroy_o4n4nl0(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_destroy_o4n4nl0 F;
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
        ((void (*)(void *, void *, uint64_t, uint64_t))GM_PTR_UNTAG(t9))(t6, t8, ((uint64_t)72ULL), ((uint64_t)8ULL));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5j6fqpv(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_5j6fqpv F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8othtri_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_8othtri_2 F;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_fill_abmf5g5(gm_b16_8 a0, uint8_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fill_abmf5g5 F;
    gm_b16_8 gm_l0; /* s */
    uint8_t gm_l1; /* x */
    void * gm_l2; /* p */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* i */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = GM_LD(void *, ((uint8_t *)&gm_l0));
        gm_l2 = t2;
    }
    {
        {
            gm_l4 = ((uint64_t)0ULL);
        }
        {
            uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            gm_l3 = t4;
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
                        void * t18 = gm_l2;
                        uint8_t t19 = gm_l1;
                        GM_ST(uint8_t, ((uint8_t *)t18 + t17 * 1), t19);
                    }
                    goto me12;
mn13:;
                    gm_trap("no match arm matched the scrutinee", gm_srcc24pnihhgkug, 18, 5);
me12:;
                }
lc5:;
                {
                    uint64_t t21 = gm_l4;
                    uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
                    uint64_t t24 = r22;
                    gm_l4 = t24;
                }
            }
lb5:;
        }
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void * gm_f_func_ene90ss(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_func_ene90ss F;
    void * gm_l0; /* m */
    uint32_t gm_l1; /* f */
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    uint32_t t3 = gm_l1;
    void * t4 = gm_f_func_in17r5n((void *)NULL, cx, NULL, t2, t3);
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
inline __attribute__((aligned(4))) uint64_t gm_f_get_u64_tgpb2vh(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_get_u64_tgpb2vh F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* b */
    GM_STACK_CHECK_SHALLOW(&F, 153u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint64_t t1;
    uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
    uint8_t t4 = (t3 <= ((uint64_t)8ULL));
    if (!t4) goto ie2;
    void * t6 = gm_l0;
    gm_b16_8 t7 = gm_l1;
    uint64_t t8 = gm_f_rd_word_8flcq6o(t6, t7);
    uint64_t t9 = t8;
    t1 = t9;
    goto ix2;
ie2:;
    void * t11 = gm_l0;
    gm_b16_8 t12 = gm_l1;
    unsigned __int128 t13 = gm_f_get_uint_bq8c4b7(t11, t12);
    uint64_t t14 = (uint64_t)t13;
    uint64_t t15 = t14;
    t1 = t15;
ix2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_host_main_55puiu2(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_host_main_55puiu2 F;
    GM_STACK_CHECK(&F, 41u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    {
        gm_closure t2 = ((gm_closure){(void *)gm_f_fn_04kmcqn, (void *)&F.h, cx});
        gm_f_defaults_p077jgf(t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t3 = F.l0;
        void * t4 = (void *)(((uint8_t *)t3) + 64);
        gm_t9qkh0rsooc8d t5;
        GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)2ULL));
        __atomic_store_n(((uint32_t *)t4), ((uint32_t)1ULL), gm_ord[GM_LD(uint8_t, ((uint8_t *)&t5))]);
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
inline __attribute__((aligned(4))) void gm_f_join_r8j2dp1(gm_csfq64m1m1rt6m a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_join_r8j2dp1 F;
    gm_tm98ap4112hee gm_l0; /* t */
    GM_STACK_CHECK(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = GM_CONV(gm_tm98ap4112hee, a0);
    {
        {
            int64_t t3 = GM_LD(int64_t, (((uint8_t *)&gm_l0) + 0));
            int64_t t4 = gm_thread_join(t3);
        }
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_list_j8jr9cr_2(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_j8jr9cr_2 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_8othtri_2();
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
inline __attribute__((aligned(4))) void gm_f_list_jh4e1ji(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_jh4e1ji F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_5j6fqpv();
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
inline __attribute__((aligned(4))) uint32_t gm_f_new_coro_06nq12a(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, uint32_t a2) {
    struct gm_fr_new_coro_06nq12a F;
    void * gm_l0; /* m */
    uint32_t gm_l2; /* thread */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    gm_l2 = a2;
    void * t1 = gm_l0;
    unsigned __int128 n2 = (unsigned __int128)((uint64_t)20ULL);
    if (n2 >= 64) gm_trap_shift((uint64_t)n2, "usize", gm_srcp0l171g2m7ho, 376, 96);
    uint64_t r3 = (uint64_t)((uint64_t)((uint64_t)8ULL) << (int)n2);
    bool o4 = (uint64_t)(r3 >> (int)n2) != ((uint64_t)8ULL);
    if (o4) gm_trap("integer overflow in `<<`", gm_srcp0l171g2m7ho, 376, 96);
    uint64_t t5 = r3;
    uint32_t t6 = gm_l2;
    uint32_t t7 = gm_f_add_coro_k13bkpe((void *)NULL, cx, NULL, t1, &(*(gm_th5tat709fgha *)((uint8_t *)&F.l1)), t5, t6);
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
inline __attribute__((aligned(4))) uint64_t gm_f_ptr_of_b77b4c9(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_ptr_of_b77b4c9 F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* b */
    GM_STACK_CHECK(&F, 144u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    gm_b16_8 t2 = gm_l1;
    uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&t2) + 8);
    void * t4 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t2)));
    uint64_t t5 = (uintptr_t)((uint64_t)0ULL);
    void * t6 = gm_l0;
    uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)t6) + 176));
    uint64_t t8 = gm_f_us_cuv01fp(t7);
    uint64_t t9 = (uintptr_t)t8;
    if (t5 > t9 || t9 > (uintptr_t)t3) gm_trap_slice(t5, t9, t3, gm_srcnqtpfma6qe4a, 104, 68);
    gm_b16_8 t10;
    GM_ST(void *, (uint8_t *)&t10, (void *)(((uint8_t *)t4 + t5 * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t10 + 8, (uintptr_t)(t9 - t5));
    uint64_t t11 = gm_f_get_u64_tgpb2vh(t1, t10);
    F.ret = t11;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_rd_word_8flcq6o(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_rd_word_8flcq6o F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* b */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)t3) + 89));
        if (!t4) goto ie2;
        {
            uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint8_t t8 = ((t7 == ((uint64_t)8ULL)));
            if (!t8) goto ie6;
            {
                uint64_t t10 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t10 >= (uintptr_t)(t11)) gm_trap_index(t10, t11, gm_srcnqtpfma6qe4a, 316, 36);
                uint8_t t12 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t10 * 1));
                uint64_t t13 = (uint64_t)t12;
                uint64_t t14 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t14 >= (uintptr_t)(t15)) gm_trap_index(t14, t15, gm_srcnqtpfma6qe4a, 316, 49);
                uint8_t t16 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t14 * 1));
                uint64_t t17 = (uint64_t)t16;
                unsigned __int128 n18 = (unsigned __int128)((uint64_t)8ULL);
                if (n18 >= 64) gm_trap_shift((uint64_t)n18, "u64", gm_srcnqtpfma6qe4a, 316, 45);
                uint64_t r19 = (uint64_t)((uint64_t)t17 << (int)n18);
                bool o20 = (uint64_t)(r19 >> (int)n18) != t17;
                if (o20) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 45);
                uint64_t t21 = r19;
                uint64_t t22 = (uint64_t)(t13 | t21);
                uint64_t t23 = (uintptr_t)((uint64_t)2ULL);
                uint64_t t24 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t23 >= (uintptr_t)(t24)) gm_trap_index(t23, t24, gm_srcnqtpfma6qe4a, 316, 68);
                uint8_t t25 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t23 * 1));
                uint64_t t26 = (uint64_t)t25;
                unsigned __int128 n27 = (unsigned __int128)((uint64_t)16ULL);
                if (n27 >= 64) gm_trap_shift((uint64_t)n27, "u64", gm_srcnqtpfma6qe4a, 316, 64);
                uint64_t r28 = (uint64_t)((uint64_t)t26 << (int)n27);
                bool o29 = (uint64_t)(r28 >> (int)n27) != t26;
                if (o29) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 64);
                uint64_t t30 = r28;
                uint64_t t31 = (uint64_t)(t22 | t30);
                uint64_t t32 = (uintptr_t)((uint64_t)3ULL);
                uint64_t t33 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t32 >= (uintptr_t)(t33)) gm_trap_index(t32, t33, gm_srcnqtpfma6qe4a, 316, 88);
                uint8_t t34 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t32 * 1));
                uint64_t t35 = (uint64_t)t34;
                unsigned __int128 n36 = (unsigned __int128)((uint64_t)24ULL);
                if (n36 >= 64) gm_trap_shift((uint64_t)n36, "u64", gm_srcnqtpfma6qe4a, 316, 84);
                uint64_t r37 = (uint64_t)((uint64_t)t35 << (int)n36);
                bool o38 = (uint64_t)(r37 >> (int)n36) != t35;
                if (o38) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 84);
                uint64_t t39 = r37;
                uint64_t t40 = (uint64_t)(t31 | t39);
                uint64_t t41 = (uintptr_t)((uint64_t)4ULL);
                uint64_t t42 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t41 >= (uintptr_t)(t42)) gm_trap_index(t41, t42, gm_srcnqtpfma6qe4a, 316, 108);
                uint8_t t43 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t41 * 1));
                uint64_t t44 = (uint64_t)t43;
                unsigned __int128 n45 = (unsigned __int128)((uint64_t)32ULL);
                if (n45 >= 64) gm_trap_shift((uint64_t)n45, "u64", gm_srcnqtpfma6qe4a, 316, 104);
                uint64_t r46 = (uint64_t)((uint64_t)t44 << (int)n45);
                bool o47 = (uint64_t)(r46 >> (int)n45) != t44;
                if (o47) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 104);
                uint64_t t48 = r46;
                uint64_t t49 = (uint64_t)(t40 | t48);
                uint64_t t50 = (uintptr_t)((uint64_t)5ULL);
                uint64_t t51 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t50 >= (uintptr_t)(t51)) gm_trap_index(t50, t51, gm_srcnqtpfma6qe4a, 316, 128);
                uint8_t t52 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t50 * 1));
                uint64_t t53 = (uint64_t)t52;
                unsigned __int128 n54 = (unsigned __int128)((uint64_t)40ULL);
                if (n54 >= 64) gm_trap_shift((uint64_t)n54, "u64", gm_srcnqtpfma6qe4a, 316, 124);
                uint64_t r55 = (uint64_t)((uint64_t)t53 << (int)n54);
                bool o56 = (uint64_t)(r55 >> (int)n54) != t53;
                if (o56) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 124);
                uint64_t t57 = r55;
                uint64_t t58 = (uint64_t)(t49 | t57);
                uint64_t t59 = (uintptr_t)((uint64_t)6ULL);
                uint64_t t60 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t59 >= (uintptr_t)(t60)) gm_trap_index(t59, t60, gm_srcnqtpfma6qe4a, 316, 148);
                uint8_t t61 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t59 * 1));
                uint64_t t62 = (uint64_t)t61;
                unsigned __int128 n63 = (unsigned __int128)((uint64_t)48ULL);
                if (n63 >= 64) gm_trap_shift((uint64_t)n63, "u64", gm_srcnqtpfma6qe4a, 316, 144);
                uint64_t r64 = (uint64_t)((uint64_t)t62 << (int)n63);
                bool o65 = (uint64_t)(r64 >> (int)n63) != t62;
                if (o65) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 144);
                uint64_t t66 = r64;
                uint64_t t67 = (uint64_t)(t58 | t66);
                uint64_t t68 = (uintptr_t)((uint64_t)7ULL);
                uint64_t t69 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t68 >= (uintptr_t)(t69)) gm_trap_index(t68, t69, gm_srcnqtpfma6qe4a, 316, 168);
                uint8_t t70 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t68 * 1));
                uint64_t t71 = (uint64_t)t70;
                unsigned __int128 n72 = (unsigned __int128)((uint64_t)56ULL);
                if (n72 >= 64) gm_trap_shift((uint64_t)n72, "u64", gm_srcnqtpfma6qe4a, 316, 164);
                uint64_t r73 = (uint64_t)((uint64_t)t71 << (int)n72);
                bool o74 = (uint64_t)(r73 >> (int)n72) != t71;
                if (o74) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 164);
                uint64_t t75 = r73;
                uint64_t t76 = (uint64_t)(t67 | t75);
                uint8_t t77 = 0;
                F.ret = t76;
                goto gm_fret;
            }
            goto ix6;
ie6:;
ix6:;
        }
        {
            uint64_t t79 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint8_t t80 = ((t79 == ((uint64_t)4ULL)));
            if (!t80) goto ie78;
            {
                uint64_t t82 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t83 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t82 >= (uintptr_t)(t83)) gm_trap_index(t82, t83, gm_srcnqtpfma6qe4a, 317, 36);
                uint8_t t84 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t82 * 1));
                uint64_t t85 = (uint64_t)t84;
                uint64_t t86 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t87 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t86 >= (uintptr_t)(t87)) gm_trap_index(t86, t87, gm_srcnqtpfma6qe4a, 317, 49);
                uint8_t t88 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t86 * 1));
                uint64_t t89 = (uint64_t)t88;
                unsigned __int128 n90 = (unsigned __int128)((uint64_t)8ULL);
                if (n90 >= 64) gm_trap_shift((uint64_t)n90, "u64", gm_srcnqtpfma6qe4a, 317, 45);
                uint64_t r91 = (uint64_t)((uint64_t)t89 << (int)n90);
                bool o92 = (uint64_t)(r91 >> (int)n90) != t89;
                if (o92) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 317, 45);
                uint64_t t93 = r91;
                uint64_t t94 = (uint64_t)(t85 | t93);
                uint64_t t95 = (uintptr_t)((uint64_t)2ULL);
                uint64_t t96 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t95 >= (uintptr_t)(t96)) gm_trap_index(t95, t96, gm_srcnqtpfma6qe4a, 317, 68);
                uint8_t t97 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t95 * 1));
                uint64_t t98 = (uint64_t)t97;
                unsigned __int128 n99 = (unsigned __int128)((uint64_t)16ULL);
                if (n99 >= 64) gm_trap_shift((uint64_t)n99, "u64", gm_srcnqtpfma6qe4a, 317, 64);
                uint64_t r100 = (uint64_t)((uint64_t)t98 << (int)n99);
                bool o101 = (uint64_t)(r100 >> (int)n99) != t98;
                if (o101) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 317, 64);
                uint64_t t102 = r100;
                uint64_t t103 = (uint64_t)(t94 | t102);
                uint64_t t104 = (uintptr_t)((uint64_t)3ULL);
                uint64_t t105 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t104 >= (uintptr_t)(t105)) gm_trap_index(t104, t105, gm_srcnqtpfma6qe4a, 317, 88);
                uint8_t t106 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t104 * 1));
                uint64_t t107 = (uint64_t)t106;
                unsigned __int128 n108 = (unsigned __int128)((uint64_t)24ULL);
                if (n108 >= 64) gm_trap_shift((uint64_t)n108, "u64", gm_srcnqtpfma6qe4a, 317, 84);
                uint64_t r109 = (uint64_t)((uint64_t)t107 << (int)n108);
                bool o110 = (uint64_t)(r109 >> (int)n108) != t107;
                if (o110) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 317, 84);
                uint64_t t111 = r109;
                uint64_t t112 = (uint64_t)(t103 | t111);
                uint8_t t113 = 0;
                F.ret = t112;
                goto gm_fret;
            }
            goto ix78;
ie78:;
ix78:;
        }
        {
            uint64_t t115 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint8_t t116 = ((t115 == ((uint64_t)1ULL)));
            if (!t116) goto ie114;
            {
                uint64_t t118 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t119 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t118 >= (uintptr_t)(t119)) gm_trap_index(t118, t119, gm_srcnqtpfma6qe4a, 318, 36);
                uint8_t t120 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t118 * 1));
                uint64_t t121 = (uint64_t)t120;
                uint8_t t122 = 0;
                F.ret = t121;
                goto gm_fret;
            }
            goto ix114;
ie114:;
ix114:;
        }
        {
            uint64_t t124 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint8_t t125 = ((t124 == ((uint64_t)2ULL)));
            if (!t125) goto ie123;
            {
                uint64_t t127 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t128 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t127 >= (uintptr_t)(t128)) gm_trap_index(t127, t128, gm_srcnqtpfma6qe4a, 319, 36);
                uint8_t t129 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t127 * 1));
                uint64_t t130 = (uint64_t)t129;
                uint64_t t131 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t132 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t131 >= (uintptr_t)(t132)) gm_trap_index(t131, t132, gm_srcnqtpfma6qe4a, 319, 49);
                uint8_t t133 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t131 * 1));
                uint64_t t134 = (uint64_t)t133;
                unsigned __int128 n135 = (unsigned __int128)((uint64_t)8ULL);
                if (n135 >= 64) gm_trap_shift((uint64_t)n135, "u64", gm_srcnqtpfma6qe4a, 319, 45);
                uint64_t r136 = (uint64_t)((uint64_t)t134 << (int)n135);
                bool o137 = (uint64_t)(r136 >> (int)n135) != t134;
                if (o137) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 319, 45);
                uint64_t t138 = r136;
                uint64_t t139 = (uint64_t)(t130 | t138);
                uint8_t t140 = 0;
                F.ret = t139;
                goto gm_fret;
            }
            goto ix123;
ie123:;
ix123:;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t141 = gm_l0;
        gm_b16_8 t142 = gm_l1;
        unsigned __int128 t143 = gm_f_get_uint_bq8c4b7(t141, t142);
        uint64_t t144 = (uint64_t)t143;
        uint8_t t145 = 0;
        F.ret = t144;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_b16_8 gm_f_tmp_8k6u9c9(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1) {
    struct gm_fr_tmp_8k6u9c9 F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* n */
    void * gm_l2; /* sc */
    gm_b16_8 gm_l3; /* chunk */
    uint64_t gm_l4; /* base */
    uint64_t gm_l5; /* top */
    uint64_t gm_l6; /* size */
    gm_b16_8 gm_l7; /* b */
    GM_STACK_CHECK(&F, 517u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 360);
        gm_l2 = t3;
    }
    {
        void * t4 = gm_l2;
        uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)t4) + 40));
        uint64_t t6 = (uintptr_t)t5;
        void * t7 = gm_l2;
        uint64_t t8 = GM_LD(uintptr_t, ((((uint8_t *)t7) + 0) + 0) + 8);
        if (t6 >= (uintptr_t)(t8)) gm_trap_index(t6, t8, gm_srcpit8mdn0781q, 386, 14);
        gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t7) + 0) + 0)) + t6 * 16));
        gm_l3 = t9;
    }
    {
        void * t10 = GM_LD(void *, ((uint8_t *)&gm_l3));
        uint64_t t11 = (uintptr_t)t10;
        gm_l4 = t11;
    }
    {
        uint64_t t12 = gm_l4;
        void * t13 = gm_l2;
        uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)t13) + 48));
        uint64_t r15; bool o16 = __builtin_add_overflow(t12, t14, &r15);
        if (o16) gm_trap("integer overflow in `+`", gm_srcpit8mdn0781q, 390, 14);
        uint64_t t17 = r15;
        uint64_t r18; bool o19 = __builtin_add_overflow(t17, ((uint64_t)7ULL), &r18);
        if (o19) gm_trap("integer overflow in `+`", gm_srcpit8mdn0781q, 390, 14);
        uint64_t t20 = r18;
        uint64_t t21 = gm_f_us_cuv01fp(((uint64_t)7ULL));
        uint64_t t22 = (uint64_t)~t21;
        uint64_t t23 = (uint64_t)(t20 & t22);
        uint64_t t24 = gm_l4;
        uint64_t r25; bool o26 = __builtin_sub_overflow(t23, t24, &r25);
        if (o26) gm_trap("integer overflow in `-`", gm_srcpit8mdn0781q, 390, 12);
        uint64_t t27 = r25;
        gm_l5 = t27;
    }
    {
        uint64_t t28 = gm_l1;
        uint64_t t29 = gm_f_us_cuv01fp(t28);
        gm_l6 = t29;
    }
    {
        uint8_t t31 = 0;
        uint64_t t32 = gm_l1;
        uint8_t t33 = ((t32 == ((uint64_t)0ULL)));
        if (!t33) {
            uint64_t t34 = gm_l5;
            uint64_t t35 = gm_l6;
            uint64_t r36; bool o37 = __builtin_add_overflow(t34, t35, &r36);
            if (o37) gm_trap("integer overflow in `+`", gm_srcpit8mdn0781q, 392, 18);
            uint64_t t38 = r36;
            uint64_t t39 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            uint8_t t40 = (t38 > t39);
            t31 = t40;
        } else t31 = 1;
        if (!t31) goto ie30;
        {
            void * t42 = gm_l0;
            uint64_t t43 = gm_l1;
            gm_b16_8 t44 = gm_f_tmp_next_dalgo1f((void *)NULL, cx, NULL, t42, t43);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t45 = 0;
            F.ret = t44;
            goto gm_fret;
        }
        goto ix30;
ie30:;
ix30:;
    }
    {
        uint64_t t46 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
        void * t47 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)));
        uint64_t t48 = gm_l5;
        uint64_t t49 = (uintptr_t)t48;
        uint64_t t50 = gm_l5;
        uint64_t t51 = gm_l6;
        uint64_t r52; bool o53 = __builtin_add_overflow(t50, t51, &r52);
        if (o53) gm_trap("integer overflow in `+`", gm_srcpit8mdn0781q, 393, 21);
        uint64_t t54 = r52;
        uint64_t t55 = (uintptr_t)t54;
        if (t49 > t55 || t55 > (uintptr_t)t46) gm_trap_slice(t49, t55, t46, gm_srcpit8mdn0781q, 393, 10);
        gm_b16_8 t56;
        GM_ST(void *, (uint8_t *)&t56, (void *)(((uint8_t *)t47 + t49 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t56 + 8, (uintptr_t)(t55 - t49));
        gm_l7 = t56;
    }
    {
        void * t57 = gm_l2;
        uint64_t t58 = gm_l5;
        uint64_t t59 = gm_l6;
        uint64_t r60; bool o61 = __builtin_add_overflow(t58, t59, &r60);
        if (o61) gm_trap("integer overflow in `+`", gm_srcpit8mdn0781q, 394, 14);
        uint64_t t62 = r60;
        GM_ST(uint64_t, (((uint8_t *)t57) + 48), t62);
    }
    {
        gm_b16_8 t63 = gm_l7;
        gm_f_fill_abmf5g5(t63, ((uint8_t)0ULL));
    }
    {
        gm_b16_8 t64 = gm_l7;
        uint8_t t65 = 0;
        F.ret = t64;
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
inline __attribute__((aligned(4))) void gm_f_check_fatal_dgfalcm(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_check_fatal_dgfalcm F;
    void * gm_l0; /* m */
    void * gm_l1; /* t */
    uint8_t gm_l2; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 107u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        {
            void * t4 = gm_l0;
            void * t5 = GM_LD(void *, ((((uint8_t *)t4) + 1656) + 56));
            if (!(GM_LD(void *, ((uint8_t *)&t5)) != 0)) goto ie3;
            void * t6 = GM_LD(void *, ((uint8_t *)&t5));
            gm_l1 = t6;
            {
                void * t8 = gm_l1;
                gm_t76123gfet73g t9;
                GM_ST(uint8_t, (uint8_t *)&t9 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t9 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t9 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t9 + 16, 0);
                GM_ST(uint8_t, ((uint8_t *)&t9 + 0), ((uint8_t)4ULL));
                GM_ST(void *, ((uint8_t *)&t9 + 8), t8);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t9);
                goto gm_fdone;
            }
            goto ix3;
ie3:;
ix3:;
        }
        {
            void * t11 = gm_l0;
            if (!((((((uint8_t *)t11) + 1656) + 64))[1] != 0)) goto ie10;
            uint8_t t12 = GM_LD(uint8_t, ((((uint8_t *)t11) + 1656) + 64));
            gm_l2 = t12;
            {
                uint8_t t14 = gm_l2;
                gm_t76123gfet73g t15;
                GM_ST(uint8_t, (uint8_t *)&t15 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t15 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t15 + 4, 0);
                GM_ST(uint8_t, (uint8_t *)&t15 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t15 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t15 + 12, 0);
                GM_ST(uint64_t, (uint8_t *)&t15 + 16, 0);
                GM_ST(uint8_t, ((uint8_t *)&t15 + 0), ((uint8_t)3ULL));
                GM_ST(uint8_t, ((uint8_t *)&t15 + 8), t14);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t15);
                goto gm_fdone;
            }
            goto ix10;
ie10:;
ix10:;
        }
    }
    gm_b0_1 t16;
    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t16);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_tcfa6l7reoosn gm_f_check_fatal_dgfalcm_c(void * a0) { gm_tcfa6l7reoosn gm_r; gm_f_check_fatal_dgfalcm(&gm_r, a0); return gm_r; }
inline __attribute__((aligned(4))) void gm_f_connected_a7ps01v(void *gm_out, gm_t62ebbi2i5rkd a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_connected_a7ps01v F;
    gm_t62ebbi2i5rkd gm_l0; /* r */
    gm_toe5fmr3vvf4d gm_l1; /* e */
    gm_tm98ap4112hee gm_l2; /* s */
    GM_STACK_CHECK_SHALLOW(&F, 152u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_t62ebbi2i5rkd t2 = gm_l0;
        gm_tm98ap4112hee t3;
        if (!((((uint8_t *)&t2))[0] != 0)) {
            gm_tm98ap4112hee t4 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&t2) + 8));
            t3 = t4;
        } else {
            gm_toe5fmr3vvf4d t5 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t2) + 8));
            gm_l1 = t5;
            gm_toe5fmr3vvf4d t6 = gm_l1;
            gm_thf5kr0is9u9i t7;
            GM_ST(uint8_t, (uint8_t *)&t7 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t7 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t7 + 4, 0);
            ((uint8_t *)&t7)[0] = 1;
            GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t7 + 8), t6);
            gm_tppo2hmut88dq t8;
            memset(&t8, 0, sizeof t8);
            GM_ST(gm_thf5kr0is9u9i, (uint8_t *)&(*(gm_tfvg1o4f8mmhi *)gm_out) + 0, t7);
            GM_ST(gm_tppo2hmut88dq, (uint8_t *)&(*(gm_tfvg1o4f8mmhi *)gm_out) + 16, t8);
            goto gm_fdone;
        }
        gm_l2 = t3;
    }
    {
        gm_thf5kr0is9u9i t9;
        GM_ST(uint8_t, (uint8_t *)&t9 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t9 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t9 + 4, 0);
        ((uint8_t *)&t9)[0] = 0;
        GM_ST(uint64_t, ((uint8_t *)&t9 + 8), ((uint64_t)0ULL));
        gm_tm98ap4112hee t10 = gm_l2;
        gm_tppo2hmut88dq t11;
        GM_ST(uint8_t, (uint8_t *)&t11 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t11 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t11 + 12, 0);
        GM_ST(gm_tm98ap4112hee, ((uint8_t *)&t11), t10);
        ((uint8_t *)&t11)[8] = 1;
        GM_ST(gm_thf5kr0is9u9i, (uint8_t *)&(*(gm_tfvg1o4f8mmhi *)gm_out) + 0, t9);
        GM_ST(gm_tppo2hmut88dq, (uint8_t *)&(*(gm_tfvg1o4f8mmhi *)gm_out) + 16, t11);
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_tfvg1o4f8mmhi gm_f_connected_a7ps01v_c(gm_t62ebbi2i5rkd a0) { gm_tfvg1o4f8mmhi gm_r; gm_f_connected_a7ps01v(&gm_r, a0); return gm_r; }
inline __attribute__((aligned(4))) void gm_f_env_exit_prbvpjt(void *gm_out, void * a0, uint8_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_env_exit_prbvpjt F;
    void * gm_l0; /* m */
    uint8_t gm_l1; /* code */
    GM_STACK_CHECK_SHALLOW(&F, 67u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        uint32_t t4 = GM_LD(uint32_t, ((((uint8_t *)t3) + 1656) + 40));
        uint8_t t5 = (!(t4 == ((uint32_t)0ULL)));
        if (!t5) goto ie2;
        void * t6 = gm_l0;
        uint8_t t7;
        if (((((((uint8_t *)t6) + 1656) + 64))[1] != 0)) goto mn9;
        t7 = ((uint8_t)1);
        goto me8;
mn9:;
        t7 = ((uint8_t)0);
        goto me8;
mn10:;
        gm_trap("no match arm matched the scrutinee", gm_srchntnl6m6g3am, 136, 35);
me8:;
        if (!t7) goto ie2;
        {
            void * t12 = gm_l0;
            uint8_t t13 = gm_l1;
            gm_tqs5p4n2tq19v t14;
            GM_ST(uint8_t, ((uint8_t *)&t14), t13);
            ((uint8_t *)&t14)[1] = 1;
            GM_ST(gm_tqs5p4n2tq19v, ((((uint8_t *)t12) + 1656) + 64), t14);
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint8_t t15 = gm_l1;
        gm_t76123gfet73g t16;
        GM_ST(uint8_t, (uint8_t *)&t16 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t16 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t16 + 4, 0);
        GM_ST(uint8_t, (uint8_t *)&t16 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t16 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t16 + 12, 0);
        GM_ST(uint64_t, (uint8_t *)&t16 + 16, 0);
        GM_ST(uint8_t, ((uint8_t *)&t16 + 0), ((uint8_t)3ULL));
        GM_ST(uint8_t, ((uint8_t *)&t16 + 8), t15);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
        GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t16);
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_tcfa6l7reoosn gm_f_env_exit_prbvpjt_c(void * a0, uint8_t a1) { gm_tcfa6l7reoosn gm_r; gm_f_env_exit_prbvpjt(&gm_r, a0, a1); return gm_r; }
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_04kmcqn(void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_fn_04kmcqn F;
    gm_b16_8 gm_l0; /* buf */
    gm_tm98ap4112hee gm_l1; /* f */
    gm_b16_8 gm_l2; /* buf */
    gm_tm98ap4112hee gm_l3; /* s */
    gm_b16_8 gm_l4; /* data */
    gm_tm98ap4112hee gm_l5; /* s */
    gm_b16_8 gm_l6; /* buf */
    gm_b16_8 gm_l7; /* host */
    uint16_t gm_l8; /* port */
    gm_tm98ap4112hee gm_l9; /* l */
    GM_STACK_CHECK(&F, 757u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_host_main_55puiu2 *)((&F.h)->up))->l0));
        uint8_t *t3 = (((uint8_t *)t2) + 32);
        void * t4 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_host_main_55puiu2 *)((&F.h)->up))->l0));
        gm_tfvg1o4f8mmhi t5;
        uint8_t t14 = GM_LD(uint8_t, ((((uint8_t *)t4) + 0) + 0));
        switch (t14) { case ((uint8_t)0ULL): goto ma7; case ((uint8_t)1ULL): goto ma8; case ((uint8_t)2ULL): goto ma11; case ((uint8_t)3ULL): goto ma12; case ((uint8_t)4ULL): goto ma9; case ((uint8_t)5ULL): goto ma10; default: goto ma13; }
ma7:;
        uint8_t t16 = GM_LD(uint8_t, ((((uint8_t *)t4) + 0) + 0));
        if (t16 != ((uint8_t)0ULL)) goto mn15;
        gm_b16_8 t17 = GM_LD(gm_b16_8, ((((uint8_t *)t4) + 0) + 8));
        gm_l0 = t17;
        gm_tfvg1o4f8mmhi t18;
        gm_b16_8 t19 = gm_l0;
        gm_node *pn20 = gm_find(cx, 1018230276);
        if (!pn20) gm_trap("no handler installed for effect `In`", gm_srchntnl6m6g3am, 256, 46);
        gm_thf5kr0is9u9i t21;
        t21 = ((gm_thf5kr0is9u9i (*)(void *, gm_node *, gm_node *, gm_b16_8))pn20->clauses[0])(pn20->env, pn20->hctx, pn20, t19);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_thf5kr0is9u9i, (uint8_t *)&t18 + 0, t21);
        gm_tppo2hmut88dq t22;
        memset(&t22, 0, sizeof t22);
        GM_ST(gm_tppo2hmut88dq, (uint8_t *)&t18 + 16, t22);
        t5 = t18;
        goto me6;
mn15:;
ma8:;
        uint8_t t24 = GM_LD(uint8_t, ((((uint8_t *)t4) + 0) + 0));
        if (t24 != ((uint8_t)1ULL)) goto mn23;
        gm_tm98ap4112hee t25 = GM_LD(gm_tm98ap4112hee, (((((uint8_t *)t4) + 0) + 8) + 0));
        gm_l1 = t25;
        gm_b16_8 t26 = GM_LD(gm_b16_8, (((((uint8_t *)t4) + 0) + 8) + 8));
        gm_l2 = t26;
        gm_tfvg1o4f8mmhi t27;
        gm_tm98ap4112hee t28 = gm_l1;
        gm_b16_8 t29 = gm_l2;
        gm_node *pn30 = gm_find(cx, 2126215853);
        if (!pn30) gm_trap("no handler installed for effect `Fs`", gm_srchntnl6m6g3am, 257, 48);
        gm_thf5kr0is9u9i t31;
        t31 = ((gm_thf5kr0is9u9i (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee, gm_b16_8))pn30->clauses[2])(pn30->env, pn30->hctx, pn30, t28, t29);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_thf5kr0is9u9i, (uint8_t *)&t27 + 0, t31);
        gm_tppo2hmut88dq t32;
        memset(&t32, 0, sizeof t32);
        GM_ST(gm_tppo2hmut88dq, (uint8_t *)&t27 + 16, t32);
        t5 = t27;
        goto me6;
mn23:;
ma9:;
        uint8_t t34 = GM_LD(uint8_t, ((((uint8_t *)t4) + 0) + 0));
        if (t34 != ((uint8_t)4ULL)) goto mn33;
        gm_tm98ap4112hee t35 = GM_LD(gm_tm98ap4112hee, (((((uint8_t *)t4) + 0) + 8) + 0));
        gm_l3 = t35;
        gm_b16_8 t36 = GM_LD(gm_b16_8, (((((uint8_t *)t4) + 0) + 8) + 8));
        gm_l4 = t36;
        gm_tfvg1o4f8mmhi t37;
        gm_tm98ap4112hee t38 = gm_l3;
        gm_b16_8 t39 = gm_l4;
        gm_node *pn40 = gm_find(cx, 163658498);
        if (!pn40) gm_trap("no handler installed for effect `Net`", gm_srchntnl6m6g3am, 258, 49);
        gm_thf5kr0is9u9i t41;
        t41 = ((gm_thf5kr0is9u9i (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee, gm_b16_8))pn40->clauses[4])(pn40->env, pn40->hctx, pn40, t38, t39);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_thf5kr0is9u9i, (uint8_t *)&t37 + 0, t41);
        gm_tppo2hmut88dq t42;
        memset(&t42, 0, sizeof t42);
        GM_ST(gm_tppo2hmut88dq, (uint8_t *)&t37 + 16, t42);
        t5 = t37;
        goto me6;
mn33:;
ma10:;
        uint8_t t44 = GM_LD(uint8_t, ((((uint8_t *)t4) + 0) + 0));
        if (t44 != ((uint8_t)5ULL)) goto mn43;
        gm_tm98ap4112hee t45 = GM_LD(gm_tm98ap4112hee, (((((uint8_t *)t4) + 0) + 8) + 0));
        gm_l5 = t45;
        gm_b16_8 t46 = GM_LD(gm_b16_8, (((((uint8_t *)t4) + 0) + 8) + 8));
        gm_l6 = t46;
        gm_tfvg1o4f8mmhi t47;
        gm_tm98ap4112hee t48 = gm_l5;
        gm_b16_8 t49 = gm_l6;
        gm_node *pn50 = gm_find(cx, 163658498);
        if (!pn50) gm_trap("no handler installed for effect `Net`", gm_srchntnl6m6g3am, 259, 48);
        gm_thf5kr0is9u9i t51;
        t51 = ((gm_thf5kr0is9u9i (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee, gm_b16_8))pn50->clauses[5])(pn50->env, pn50->hctx, pn50, t48, t49);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_thf5kr0is9u9i, (uint8_t *)&t47 + 0, t51);
        gm_tppo2hmut88dq t52;
        memset(&t52, 0, sizeof t52);
        GM_ST(gm_tppo2hmut88dq, (uint8_t *)&t47 + 16, t52);
        t5 = t47;
        goto me6;
mn43:;
ma11:;
        uint8_t t54 = GM_LD(uint8_t, ((((uint8_t *)t4) + 0) + 0));
        if (t54 != ((uint8_t)2ULL)) goto mn53;
        gm_b16_8 t55 = GM_LD(gm_b16_8, (((((uint8_t *)t4) + 0) + 8) + 0));
        gm_l7 = t55;
        uint16_t t56 = GM_LD(uint16_t, (((((uint8_t *)t4) + 0) + 8) + 16));
        gm_l8 = t56;
        gm_b16_8 t57 = gm_l7;
        uint16_t t58 = gm_l8;
        gm_node *pn59 = gm_find(cx, 163658498);
        if (!pn59) gm_trap("no handler installed for effect `Net`", gm_srchntnl6m6g3am, 260, 47);
        gm_t62ebbi2i5rkd t60;
        t60 = ((gm_t62ebbi2i5rkd (*)(void *, gm_node *, gm_node *, gm_b16_8, uint16_t))pn59->clauses[0])(pn59->env, pn59->hctx, pn59, t57, t58);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tfvg1o4f8mmhi t61;
        gm_f_connected_a7ps01v((void *)&t61, t60);
        t5 = t61;
        goto me6;
mn53:;
ma12:;
        uint8_t t63 = GM_LD(uint8_t, ((((uint8_t *)t4) + 0) + 0));
        if (t63 != ((uint8_t)3ULL)) goto mn62;
        gm_tm98ap4112hee t64 = GM_LD(gm_tm98ap4112hee, ((((uint8_t *)t4) + 0) + 8));
        gm_l9 = t64;
        gm_tm98ap4112hee t65 = gm_l9;
        gm_node *pn66 = gm_find(cx, 163658498);
        if (!pn66) gm_trap("no handler installed for effect `Net`", gm_srchntnl6m6g3am, 261, 37);
        gm_t62ebbi2i5rkd t67;
        t67 = ((gm_t62ebbi2i5rkd (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee))pn66->clauses[3])(pn66->env, pn66->hctx, pn66, t65);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tfvg1o4f8mmhi t68;
        gm_f_connected_a7ps01v((void *)&t68, t67);
        t5 = t68;
        goto me6;
mn62:;
ma13:;
        gm_trap("no match arm matched the scrutinee", gm_srchntnl6m6g3am, 255, 20);
me6:;
        GM_ST(gm_tfvg1o4f8mmhi, t3, t5);
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
extern __attribute__((aligned(4))) void gm_f_host_main_55puiu2(void * a0);
inline __attribute__((aligned(4))) void gm_f_on_host_thread_kmegc07(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, const void * a2) {
    struct gm_fr_on_host_thread_kmegc07 F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_tmvdbo28o1i6h gm_l2; /* op */
    void * gm_l3; /* j */
    gm_toe5fmr3vvf4d gm_l4; /* e */
    gm_tm98ap4112hee gm_l5; /* t */
    gm_tcfa6l7reoosn gm_l6; /* r */
    gm_tfvg1o4f8mmhi gm_l7; /* result */
    GM_STACK_CHECK(&F, 612u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    memcpy(&gm_l2, a2, sizeof gm_l2);
    {
        gm_t683gnj9k3p7d t2;
        memset((uint8_t *)&t2 + 68, 0, 4);
        memcpy((uint8_t *)&t2 + 0, ((uint8_t *)&gm_l2), 32);
        gm_thf5kr0is9u9i t3;
        GM_ST(uint8_t, (uint8_t *)&t3 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t3 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t3 + 4, 0);
        ((uint8_t *)&t3)[0] = 0;
        GM_ST(uint64_t, ((uint8_t *)&t3 + 8), ((uint64_t)0ULL));
        GM_ST(gm_thf5kr0is9u9i, (uint8_t *)&(*(gm_tfvg1o4f8mmhi *)((uint8_t *)&t2 + 32)) + 0, t3);
        gm_tppo2hmut88dq t4;
        memset(&t4, 0, sizeof t4);
        GM_ST(gm_tppo2hmut88dq, (uint8_t *)&(*(gm_tfvg1o4f8mmhi *)((uint8_t *)&t2 + 32)) + 16, t4);
        GM_ST(uint32_t, (uint8_t *)&t2 + 64, ((uint32_t)0ULL));
        gm_tq7s98ihonkju t5 = gm_f_new_g0l0iur((void *)NULL, cx, NULL, &t2, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_klqd6h436uv)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t5))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t5) + 8, gm_srchntnl6m6g3am, 275, 10);
        void * t6 = GM_LD(void *, (((uint8_t *)&t5) + 8));
        gm_l3 = t6;
    }
    {
        void * t7 = gm_l3;
        gm_t62ebbi2i5rkd t8 = gm_f_spawn_2tmnfpi(((void *)gm_f_host_main_55puiu2), t7);
        gm_tm98ap4112hee t9;
        if (!((((uint8_t *)&t8))[0] != 0)) {
            gm_tm98ap4112hee t10 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&t8) + 8));
            t9 = t10;
        } else {
            gm_toe5fmr3vvf4d t11 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t8) + 8));
            gm_l4 = t11;
            {
                void * t13 = gm_l3;
                gm_b0_1 t14 = gm_f_destroy_o4n4nl0((void *)NULL, cx, NULL, t13, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_ki1ml5fl8pd)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_tfvg1o4f8mmhi t15;
            gm_toe5fmr3vvf4d t16 = gm_l4;
            gm_thf5kr0is9u9i t17;
            GM_ST(uint8_t, (uint8_t *)&t17 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t17 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t17 + 4, 0);
            ((uint8_t *)&t17)[0] = 1;
            GM_ST(gm_toe5fmr3vvf4d, ((uint8_t *)&t17 + 8), t16);
            GM_ST(gm_thf5kr0is9u9i, (uint8_t *)&t15 + 0, t17);
            gm_tppo2hmut88dq t18;
            memset(&t18, 0, sizeof t18);
            GM_ST(gm_tppo2hmut88dq, (uint8_t *)&t15 + 16, t18);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t6j8vdmi81h7c *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t6j8vdmi81h7c *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t6j8vdmi81h7c *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_t6j8vdmi81h7c *)gm_out))[0] = 0;
            GM_ST(gm_tfvg1o4f8mmhi, (uint8_t *)&(*(gm_t6j8vdmi81h7c *)gm_out) + 8, t15);
            goto gm_fdone;
        }
        gm_l5 = t9;
    }
    {
        void * t19 = gm_l0;
        uint32_t t20 = GM_LD(uint32_t, ((((uint8_t *)t19) + 1656) + 40));
        uint64_t t21 = (uint64_t)t20;
        uint64_t t22 = (uintptr_t)t21;
        void * t23 = gm_l0;
        uint64_t t24 = GM_LD(uintptr_t, (((((uint8_t *)t23) + 1656) + 0) + 0) + 8);
        if (t22 >= (uintptr_t)(t24)) gm_trap_index(t22, t24, gm_srchntnl6m6g3am, 280, 5);
        void * t25 = gm_l3;
        void * t26 = (void *)(((uint8_t *)t25) + 64);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tdkjirf1g83ud *)(((uint8_t *)GM_LD(void *, (((((uint8_t *)t23) + 1656) + 0) + 0)) + t22 * 48) + 8)) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tdkjirf1g83ud *)(((uint8_t *)GM_LD(void *, (((((uint8_t *)t23) + 1656) + 0) + 0)) + t22 * 48) + 8)) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tdkjirf1g83ud *)(((uint8_t *)GM_LD(void *, (((((uint8_t *)t23) + 1656) + 0) + 0)) + t22 * 48) + 8)) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tdkjirf1g83ud *)(((uint8_t *)GM_LD(void *, (((((uint8_t *)t23) + 1656) + 0) + 0)) + t22 * 48) + 8)) + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tdkjirf1g83ud *)(((uint8_t *)GM_LD(void *, (((((uint8_t *)t23) + 1656) + 0) + 0)) + t22 * 48) + 8)) + 24, 0);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tdkjirf1g83ud *)(((uint8_t *)GM_LD(void *, (((((uint8_t *)t23) + 1656) + 0) + 0)) + t22 * 48) + 8)) + 0, ((uint8_t)4ULL));
        GM_ST(void *, (uint8_t *)&(*(gm_tdkjirf1g83ud *)(((uint8_t *)GM_LD(void *, (((((uint8_t *)t23) + 1656) + 0) + 0)) + t22 * 48) + 8)) + 8, t26);
    }
    {
        void * t27 = gm_l0;
        gm_tl874t2atnlh3 t28 = gm_l1;
        gm_f_switch_thread_m7aoqko((void *)&gm_l6, (void *)NULL, cx, NULL, t27, t28);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_tm98ap4112hee t29 = gm_l5;
        gm_f_join_r8j2dp1(GM_CONV(gm_csfq64m1m1rt6m, t29));
    }
    {
        void * t30 = gm_l3;
        gm_tfvg1o4f8mmhi t31 = GM_LD(gm_tfvg1o4f8mmhi, (((uint8_t *)t30) + 32));
        gm_l7 = t31;
    }
    {
        void * t32 = gm_l3;
        gm_b0_1 t33 = gm_f_destroy_o4n4nl0((void *)NULL, cx, NULL, t32, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kovthkfk7ta)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_tcfa6l7reoosn t34 = gm_l6;
        if (((((uint8_t *)&t34))[0] != 0)) {
            gm_t6j8vdmi81h7c t35;
            GM_ST(uint8_t, (uint8_t *)&t35 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t35 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t35 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t35 + 32, 0);
            ((uint8_t *)&t35)[0] = 1;
            GM_ST(gm_t76123gfet73g, ((uint8_t *)&t35 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t34) + 8)));
            F.ret = t35;
            goto gm_fret;
        }
        gm_b0_1 t36 = GM_LD(gm_b0_1, (((uint8_t *)&t34) + 8));
    }
    {
        const uint8_t *t37 = ((uint8_t *)&gm_l7);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t6j8vdmi81h7c *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t6j8vdmi81h7c *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t6j8vdmi81h7c *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_t6j8vdmi81h7c *)gm_out))[0] = 0;
        memmove((uint8_t *)&(*(gm_t6j8vdmi81h7c *)gm_out) + 8, t37, 32);
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
__attribute__((aligned(4))) uint8_t gm_f_runnable_o1q905g(void * a0, void * a1, gm_tf5srh4mp4b3i a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_runnable_o1q905g F;
    void * gm_l0; /* m */
    void * gm_l1; /* t */
    gm_tf5srh4mp4b3i gm_l2; /* now */
    uint32_t gm_l3; /* h */
    uint64_t gm_l4; /* deadline */
    void * gm_l5; /* done */
    uint64_t gm_l6; /* seen */
    gm_tf5srh4mp4b3i gm_l7; /* deadline */
    GM_STACK_CHECK_SHALLOW(&F, 327u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t1 = gm_l1;
    uint8_t t2;
    uint8_t t11 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    switch (t11) { case ((uint8_t)0ULL): goto ma4; case ((uint8_t)1ULL): goto ma5; case ((uint8_t)2ULL): goto ma8; case ((uint8_t)3ULL): goto ma6; case ((uint8_t)4ULL): goto ma7; case ((uint8_t)5ULL): goto ma9; default: goto ma10; }
ma4:;
    uint8_t t13 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    if (t13 != ((uint8_t)0ULL)) goto mn12;
    t2 = ((uint8_t)1);
    goto me3;
mn12:;
ma5:;
    uint8_t t15 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    if (t15 != ((uint8_t)1ULL)) goto mn14;
    uint32_t t16 = GM_LD(uint32_t, ((((uint8_t *)t1) + 8) + 8));
    gm_l3 = t16;
    uint32_t t17 = gm_l3;
    uint64_t t18 = (uint64_t)t17;
    uint64_t t19 = (uintptr_t)t18;
    void * t20 = gm_l0;
    uint64_t t21 = GM_LD(uintptr_t, (((((uint8_t *)t20) + 1656) + 0) + 0) + 8);
    if (t19 >= (uintptr_t)(t21)) gm_trap_index(t19, t21, gm_srchntnl6m6g3am, 71, 17);
    uint8_t t22 = ((GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, (((((uint8_t *)t20) + 1656) + 0) + 0)) + t19 * 48) + 8) + 0) == ((uint8_t)5ULL)));
    t2 = t22;
    goto me3;
mn14:;
ma6:;
    uint8_t t24 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    if (t24 != ((uint8_t)3ULL)) goto mn23;
    uint64_t t25 = GM_LD(uint64_t, ((((uint8_t *)t1) + 8) + 8));
    gm_l4 = t25;
    uint8_t t26 = 0;
    uint8_t t27;
    if (((((uint8_t *)&gm_l2))[8] != 0)) goto mn29;
    t27 = ((uint8_t)0);
    goto me28;
mn29:;
    t27 = ((uint8_t)1);
    goto me28;
mn30:;
    gm_trap("no match arm matched the scrutinee", gm_srchntnl6m6g3am, 72, 25);
me28:;
    if (t27) {
        gm_tf5srh4mp4b3i t31 = gm_l2;
        uint64_t t32;
        if (((((uint8_t *)&t31))[8] != 0)) {
            uint64_t t33 = GM_LD(uint64_t, (((uint8_t *)&t31) + 0));
            t32 = t33;
        } else {
            t32 = ((uint64_t)0ULL);
        }
        uint64_t t34 = gm_l4;
        uint8_t t35 = (t32 >= t34);
        t26 = t35;
    } else t26 = 0;
    t2 = t26;
    goto me3;
mn23:;
ma7:;
    uint8_t t37 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    if (t37 != ((uint8_t)4ULL)) goto mn36;
    void * t38 = GM_LD(void *, ((((uint8_t *)t1) + 8) + 8));
    gm_l5 = t38;
    void * t39 = gm_l5;
    gm_t9qkh0rsooc8d t40;
    GM_ST(uint8_t, ((uint8_t *)&t40 + 0), ((uint8_t)1ULL));
    uint32_t t41 = __atomic_load_n(((uint32_t *)t39), gm_ord[GM_LD(uint8_t, ((uint8_t *)&t40))]);
    uint8_t t42 = (!(t41 == ((uint32_t)0ULL)));
    t2 = t42;
    goto me3;
mn36:;
ma8:;
    uint8_t t44 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    if (t44 != ((uint8_t)2ULL)) goto mn43;
    uint64_t t45 = GM_LD(uint64_t, (((((uint8_t *)t1) + 8) + 8) + 0));
    gm_l6 = t45;
    gm_tf5srh4mp4b3i t46 = GM_LD(gm_tf5srh4mp4b3i, (((((uint8_t *)t1) + 8) + 8) + 8));
    gm_l7 = t46;
    uint8_t t47 = 0;
    uint64_t t48 = gm_l6;
    void * t49 = gm_l0;
    uint64_t t50 = GM_LD(uint64_t, ((((uint8_t *)t49) + 1656) + 48));
    uint8_t t51 = (!(t48 == t50));
    if (!t51) {
        uint8_t t52 = 0;
        uint8_t t53 = 0;
        uint8_t t54;
        if (((((uint8_t *)&gm_l7))[8] != 0)) goto mn56;
        t54 = ((uint8_t)0);
        goto me55;
mn56:;
        t54 = ((uint8_t)1);
        goto me55;
mn57:;
        gm_trap("no match arm matched the scrutinee", gm_srchntnl6m6g3am, 74, 68);
me55:;
        if (t54) {
            uint8_t t58;
            if (((((uint8_t *)&gm_l2))[8] != 0)) goto mn60;
            t58 = ((uint8_t)0);
            goto me59;
mn60:;
            t58 = ((uint8_t)1);
            goto me59;
mn61:;
            gm_trap("no match arm matched the scrutinee", gm_srchntnl6m6g3am, 74, 93);
me59:;
            t53 = t58;
        } else t53 = 0;
        if (t53) {
            gm_tf5srh4mp4b3i t62 = gm_l2;
            uint64_t t63;
            if (((((uint8_t *)&t62))[8] != 0)) {
                uint64_t t64 = GM_LD(uint64_t, (((uint8_t *)&t62) + 0));
                t63 = t64;
            } else {
                t63 = ((uint64_t)0ULL);
            }
            gm_tf5srh4mp4b3i t65 = gm_l7;
            uint64_t t66;
            if (((((uint8_t *)&t65))[8] != 0)) {
                uint64_t t67 = GM_LD(uint64_t, (((uint8_t *)&t65) + 0));
                t66 = t67;
            } else {
                t66 = ((uint64_t)0ULL);
            }
            uint8_t t68 = (t63 >= t66);
            t52 = t68;
        } else t52 = 0;
        t47 = t52;
    } else t47 = 1;
    t2 = t47;
    goto me3;
mn43:;
ma9:;
    uint8_t t70 = GM_LD(uint8_t, ((((uint8_t *)t1) + 8) + 0));
    if (t70 != ((uint8_t)5ULL)) goto mn69;
    t2 = ((uint8_t)0);
    goto me3;
mn69:;
ma10:;
    gm_trap("no match arm matched the scrutinee", gm_srchntnl6m6g3am, 69, 72);
me3:;
    F.ret = t2;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_sleep_gqtie06(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, uint64_t a2) {
    struct gm_fr_sleep_gqtie06 F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    uint64_t gm_l2; /* ns */
    GM_STACK_CHECK(&F, 229u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        {
            void * t4 = gm_l0;
            uint64_t t5 = GM_LD(uintptr_t, (((((uint8_t *)t4) + 1656) + 0) + 0) + 8);
            uint8_t t6 = ((t5 == ((uint64_t)1ULL)));
            if (!t6) goto ie3;
            {
                uint64_t t8 = gm_l2;
                gm_t4ce0jd3tdro4 t9;
                GM_ST(uint64_t, ((uint8_t *)&t9 + 0), t8);
                gm_node *pn10 = gm_find(cx, 1579066750);
                if (!pn10) gm_trap("no handler installed for effect `Clock`", gm_srchntnl6m6g3am, 222, 9);
                gm_b0_1 t11;
                t11 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_t4ce0jd3tdro4))pn10->clauses[2])(pn10->env, pn10->hctx, pn10, t9);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                gm_b0_1 t12;
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
                ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
                GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t12);
                goto gm_fdone;
            }
            goto ix3;
ie3:;
ix3:;
        }
        {
            void * t13 = gm_l0;
            uint32_t t14 = GM_LD(uint32_t, ((((uint8_t *)t13) + 1656) + 40));
            uint64_t t15 = (uint64_t)t14;
            uint64_t t16 = (uintptr_t)t15;
            void * t17 = gm_l0;
            uint64_t t18 = GM_LD(uintptr_t, (((((uint8_t *)t17) + 1656) + 0) + 0) + 8);
            if (t16 >= (uintptr_t)(t18)) gm_trap_index(t16, t18, gm_srchntnl6m6g3am, 225, 5);
            uint8_t *t19 = (((uint8_t *)GM_LD(void *, (((((uint8_t *)t17) + 1656) + 0) + 0)) + t16 * 48) + 8);
            gm_node *pn20 = gm_find(cx, 1579066750);
            if (!pn20) gm_trap("no handler installed for effect `Clock`", gm_srchntnl6m6g3am, 225, 63);
            gm_t4ce0jd3tdro4 t21;
            t21 = ((gm_t4ce0jd3tdro4 (*)(void *, gm_node *, gm_node *))pn20->clauses[0])(pn20->env, pn20->hctx, pn20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)&t21) + 0));
            uint64_t t23 = gm_l2;
            uint64_t r24; bool o25 = __builtin_add_overflow(t22, t23, &r24);
            uint64_t t26 = o25 ? (1 ? ((uint64_t)18446744073709551615ULL) : ((uint64_t)0ULL)) : r24;
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tdkjirf1g83ud *)t19) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tdkjirf1g83ud *)t19) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tdkjirf1g83ud *)t19) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tdkjirf1g83ud *)t19) + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tdkjirf1g83ud *)t19) + 24, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tdkjirf1g83ud *)t19) + 0, ((uint8_t)3ULL));
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tdkjirf1g83ud *)t19) + 8, t26);
        }
        {
            void * t27 = gm_l0;
            gm_tl874t2atnlh3 t28 = gm_l1;
            gm_tcfa6l7reoosn t29;
            gm_f_switch_thread_m7aoqko((void *)&t29, (void *)NULL, cx, NULL, t27, t28);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t29))[0] != 0)) {
                gm_tcfa6l7reoosn t30;
                GM_ST(uint8_t, (uint8_t *)&t30 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t30 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t30 + 4, 0);
                ((uint8_t *)&t30)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t30 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t29) + 8)));
                F.ret = t30;
                goto gm_fret;
            }
            gm_b0_1 t31 = GM_LD(gm_b0_1, (((uint8_t *)&t29) + 8));
        }
    }
    gm_b0_1 t32;
    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t32);
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
__attribute__((aligned(4))) void gm_f_switch_thread_m7aoqko(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1) {
    struct gm_fr_switch_thread_m7aoqko F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    void * gm_l2; /* th */
    uint32_t gm_l3; /* me */
    uint64_t gm_l4; /* n */
    gm_tf5srh4mp4b3i gm_l5; /* now */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    gm_tf5srh4mp4b3i gm_l9; /* d */
    uint64_t gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* k */
    uint64_t gm_l13; /* i */
    void * gm_l14; /* t */
    gm_tf5srh4mp4b3i gm_l15; /* earliest */
    gm_b16_8 gm_l16; /* <tmp> */
    uint64_t gm_l17; /* <tmp> */
    gm_tf5srh4mp4b3i gm_l19; /* d */
    uint64_t gm_l20; /* e */
    uint64_t gm_l21; /* d */
    uint64_t gm_l22; /* e */
    gm_b16_8 gm_l23; /* <tmp> */
    uint64_t gm_l24; /* <tmp> */
    uint64_t gm_l26; /* e */
    uint64_t gm_l27; /* t */
    GM_STACK_CHECK(&F, 1754u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 1656);
        gm_l2 = t3;
    }
    {
        void * t4 = gm_l2;
        uint32_t t5 = GM_LD(uint32_t, (((uint8_t *)t4) + 40));
        gm_l3 = t5;
    }
    {
        uint32_t t6 = gm_l3;
        uint64_t t7 = (uint64_t)t6;
        uint64_t t8 = (uintptr_t)t7;
        void * t9 = gm_l2;
        uint64_t t10 = GM_LD(uintptr_t, ((((uint8_t *)t9) + 0) + 0) + 8);
        if (t8 >= (uintptr_t)(t10)) gm_trap_index(t8, t10, gm_srchntnl6m6g3am, 83, 5);
        void * t11 = gm_l0;
        uint32_t t12 = GM_LD(uint32_t, ((((uint8_t *)t11) + 1088) + 0));
        GM_ST(uint32_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t9) + 0) + 0)) + t8 * 48) + 0), t12);
    }
    {
        void * t13 = gm_l2;
        uint64_t t14 = GM_LD(uintptr_t, ((((uint8_t *)t13) + 0) + 0) + 8);
        gm_l4 = t14;
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb15;
            {
                gm_tf5srh4mp4b3i t18;
                memset(&t18, 0, sizeof t18);
                gm_l5 = t18;
            }
            {
                {
                    void * t20 = gm_l2;
                    gm_b16_8 t21 = GM_LD(gm_b16_8, ((((uint8_t *)t20) + 0) + 0));
                    gm_l6 = t21;
                }
                {
                    gm_l7 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t23 = gm_l7;
                        uint64_t t24 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                        uint8_t t25 = (t23 < t24);
                        if (!t25) goto lb22;
                        {
                            uint64_t t28 = gm_l7;
                            uint64_t t29 = (uintptr_t)t28;
                            uint64_t t30 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                            gm_t9jnd88rq7ul8 t33 = GM_LD(gm_t9jnd88rq7ul8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t29 * 48));
                            F.l8 = t33;
                            {
                                uint8_t t36 = GM_LD(uint8_t, ((((uint8_t *)&F.l8) + 8) + 0));
                                if (t36 != ((uint8_t)2ULL)) goto ie35;
                                gm_tf5srh4mp4b3i t37 = GM_LD(gm_tf5srh4mp4b3i, (((((uint8_t *)&F.l8) + 8) + 8) + 8));
                                gm_l9 = t37;
                                uint8_t t38;
                                if (((((uint8_t *)&gm_l9))[8] != 0)) goto mn40;
                                t38 = ((uint8_t)0);
                                goto me39;
mn40:;
                                t38 = ((uint8_t)1);
                                goto me39;
mn41:;
                                gm_trap("no match arm matched the scrutinee", gm_srchntnl6m6g3am, 89, 45);
me39:;
                                if (!t38) goto ie35;
                                {
                                    gm_node *pn43 = gm_find(cx, 1579066750);
                                    if (!pn43) gm_trap("no handler installed for effect `Clock`", gm_srchntnl6m6g3am, 89, 67);
                                    gm_t4ce0jd3tdro4 t44;
                                    t44 = ((gm_t4ce0jd3tdro4 (*)(void *, gm_node *, gm_node *))pn43->clauses[0])(pn43->env, pn43->hctx, pn43);
                                    if (gm_unw.kind) {
                                        goto gm_ll26;
                                    }
                                    uint64_t t45 = GM_LD(uint64_t, (((uint8_t *)&t44) + 0));
                                    gm_tf5srh4mp4b3i t46;
                                    GM_ST(uint8_t, (uint8_t *)&t46 + 9, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t46 + 10, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t46 + 12, 0);
                                    GM_ST(uint64_t, ((uint8_t *)&t46), t45);
                                    ((uint8_t *)&t46)[8] = 1;
                                    gm_l5 = t46;
                                }
                                goto ix35;
ie35:;
ix35:;
                            }
                            {
                                uint8_t t48;
                                uint8_t t51 = GM_LD(uint8_t, ((((uint8_t *)&F.l8) + 8) + 0));
                                if (t51 != ((uint8_t)3ULL)) goto mn50;
                                t48 = ((uint8_t)1);
                                goto me49;
mn50:;
                                t48 = ((uint8_t)0);
                                goto me49;
mn52:;
                                gm_trap("no match arm matched the scrutinee", gm_srchntnl6m6g3am, 90, 16);
me49:;
                                if (!t48) goto ie47;
                                {
                                    gm_node *pn54 = gm_find(cx, 1579066750);
                                    if (!pn54) gm_trap("no handler installed for effect `Clock`", gm_srchntnl6m6g3am, 90, 45);
                                    gm_t4ce0jd3tdro4 t55;
                                    t55 = ((gm_t4ce0jd3tdro4 (*)(void *, gm_node *, gm_node *))pn54->clauses[0])(pn54->env, pn54->hctx, pn54);
                                    if (gm_unw.kind) {
                                        goto gm_ll26;
                                    }
                                    uint64_t t56 = GM_LD(uint64_t, (((uint8_t *)&t55) + 0));
                                    gm_tf5srh4mp4b3i t57;
                                    GM_ST(uint8_t, (uint8_t *)&t57 + 9, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t57 + 10, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t57 + 12, 0);
                                    GM_ST(uint64_t, ((uint8_t *)&t57), t56);
                                    ((uint8_t *)&t57)[8] = 1;
                                    gm_l5 = t57;
                                }
                                goto ix47;
ie47:;
ix47:;
                            }
                            goto me31;
mn32:;
                            gm_trap("no match arm matched the scrutinee", gm_srchntnl6m6g3am, 88, 9);
me31:;
                        }
lc22:;
                        {
                            uint64_t t59 = gm_l7;
                            uint64_t r60; bool o61 = __builtin_add_overflow(t59, ((uint64_t)1ULL), &r60);
                            uint64_t t62 = r60;
                            gm_l7 = t62;
                        }
                    }
gm_ll26:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb22; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc22; }
                    }
                    goto gm_ll16;
lb22:;
                }
            }
            {
                {
                    gm_l11 = ((uint64_t)1ULL);
                }
                {
                    uint64_t t64 = gm_l4;
                    uint64_t r65; bool o66 = __builtin_add_overflow(t64, ((uint64_t)1ULL), &r65);
                    if (o66) gm_trap("integer overflow in `+`", gm_srchntnl6m6g3am, 92, 21);
                    uint64_t t67 = r65;
                    gm_l10 = t67;
                }
                {
                    for (;;) {
                        uint64_t t69 = gm_l11;
                        uint64_t t70 = gm_l10;
                        uint8_t t71 = (t69 < t70);
                        if (!t71) goto lb68;
                        {
                            uint64_t t74 = gm_l11;
                            uint64_t t77 = GM_LD(uint64_t, ((uint8_t *)&t74));
                            gm_l12 = t77;
                            {
                                uint32_t t79 = gm_l3;
                                uint64_t t80 = (uint64_t)t79;
                                uint64_t t81 = gm_l12;
                                uint64_t r82; bool o83 = __builtin_add_overflow(t80, t81, &r82);
                                if (o83) gm_trap("integer overflow in `+`", gm_srchntnl6m6g3am, 93, 19);
                                uint64_t t84 = r82;
                                uint64_t t85 = gm_l4;
                                if (t85 == 0) gm_trap("division by zero", gm_srchntnl6m6g3am, 93, 18);
                                uint64_t t86 = (uint64_t)(t84 % t85);
                                gm_l13 = t86;
                            }
                            {
                                uint64_t t87 = gm_l13;
                                uint64_t t88 = (uintptr_t)t87;
                                void * t89 = gm_l2;
                                uint64_t t90 = GM_LD(uintptr_t, ((((uint8_t *)t89) + 0) + 0) + 8);
                                if (t88 >= (uintptr_t)(t90)) gm_trap_index(t88, t90, gm_srchntnl6m6g3am, 94, 19);
                                void * t91 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t89) + 0) + 0)) + t88 * 48);
                                gm_l14 = t91;
                            }
                            {
                                void * t93 = gm_l0;
                                void * t94 = gm_l14;
                                gm_tf5srh4mp4b3i t95 = gm_l5;
                                uint8_t t96 = gm_f_runnable_o1q905g(t93, t94, t95);
                                uint8_t t97 = (uint8_t)!t96;
                                if (!t97) goto ie92;
                                {
                                    goto lc68;
                                }
                                goto ix92;
ie92:;
ix92:;
                            }
                            {
                                void * t99 = gm_l14;
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_tdkjirf1g83ud *)(((uint8_t *)t99) + 8)) + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&(*(gm_tdkjirf1g83ud *)(((uint8_t *)t99) + 8)) + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&(*(gm_tdkjirf1g83ud *)(((uint8_t *)t99) + 8)) + 4, 0);
                                GM_ST(uint64_t, (uint8_t *)&(*(gm_tdkjirf1g83ud *)(((uint8_t *)t99) + 8)) + 8, 0);
                                GM_ST(uint64_t, (uint8_t *)&(*(gm_tdkjirf1g83ud *)(((uint8_t *)t99) + 8)) + 16, 0);
                                GM_ST(uint64_t, (uint8_t *)&(*(gm_tdkjirf1g83ud *)(((uint8_t *)t99) + 8)) + 24, 0);
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_tdkjirf1g83ud *)(((uint8_t *)t99) + 8)) + 0, ((uint8_t)0ULL));
                            }
                            {
                                uint64_t t101 = gm_l13;
                                uint32_t t102 = gm_l3;
                                uint64_t t103 = (uint64_t)t102;
                                uint8_t t104 = ((t101 == t103));
                                if (!t104) goto ie100;
                                {
                                    void * t106 = gm_l0;
                                    gm_f_check_fatal_dgfalcm((void *)&(*(gm_tcfa6l7reoosn *)gm_out), t106);
                                    goto gm_fdone;
                                }
                                goto ix100;
ie100:;
ix100:;
                            }
                            {
                                void * t107 = gm_l2;
                                uint64_t t108 = gm_l13;
                                uint32_t t109 = (uint32_t)t108;
                                GM_ST(uint32_t, (((uint8_t *)t107) + 40), t109);
                            }
                            {
                                {
                                    void * t111 = gm_l0;
                                    void * t112 = gm_l14;
                                    uint32_t t113 = GM_LD(uint32_t, (((uint8_t *)t112) + 0));
                                    gm_t9hl37efuvhj8 t114;
                                    memset(&t114, 0, sizeof t114);
                                    GM_ST(uint8_t, ((uint8_t *)&t114 + 0), ((uint8_t)0ULL));
                                    gm_t9hl37efuvhj8 t115;
                                    gm_f_handoff_mtsh8r2((void *)&t115, (void *)NULL, cx, NULL, t111, t113, &t114);
                                    if (gm_unw.kind) {
                                        goto gm_ll72;
                                    }
                                }
                            }
                            {
                                void * t116 = gm_l0;
                                uint32_t t117 = gm_l3;
                                GM_ST(uint32_t, ((((uint8_t *)t116) + 1656) + 40), t117);
                            }
                            {
                                uint32_t t119 = gm_l3;
                                uint64_t t120 = (uint64_t)t119;
                                uint64_t t121 = (uintptr_t)t120;
                                void * t122 = gm_l0;
                                uint64_t t123 = GM_LD(uintptr_t, (((((uint8_t *)t122) + 1656) + 0) + 0) + 8);
                                if (t121 >= (uintptr_t)(t123)) gm_trap_index(t121, t123, gm_srchntnl6m6g3am, 101, 16);
                                uint8_t t124 = ((GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, (((((uint8_t *)t122) + 1656) + 0) + 0)) + t121 * 48) + 8) + 0) == ((uint8_t)0ULL)));
                                if (!t124) goto ie118;
                                {
                                    void * t126 = gm_l0;
                                    gm_f_check_fatal_dgfalcm((void *)&(*(gm_tcfa6l7reoosn *)gm_out), t126);
                                    goto gm_fdone;
                                }
                                goto ix118;
ie118:;
ix118:;
                            }
                            {
                                goto lb68;
                            }
                            goto me75;
mn76:;
                            gm_trap("no match arm matched the scrutinee", gm_srchntnl6m6g3am, 92, 9);
me75:;
                        }
lc68:;
                        {
                            uint64_t t128 = gm_l11;
                            uint64_t r129; bool o130 = __builtin_add_overflow(t128, ((uint64_t)1ULL), &r129);
                            uint64_t t131 = r129;
                            gm_l11 = t131;
                        }
                    }
gm_ll72:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb68; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc68; }
                    }
                    goto gm_ll16;
lb68:;
                }
            }
            {
                void * t133 = gm_l0;
                uint32_t t134 = gm_l3;
                uint64_t t135 = (uint64_t)t134;
                uint64_t t136 = (uintptr_t)t135;
                void * t137 = gm_l2;
                uint64_t t138 = GM_LD(uintptr_t, ((((uint8_t *)t137) + 0) + 0) + 8);
                if (t136 >= (uintptr_t)(t138)) gm_trap_index(t136, t138, gm_srchntnl6m6g3am, 105, 24);
                void * t139 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t137) + 0) + 0)) + t136 * 48);
                gm_tf5srh4mp4b3i t140 = gm_l5;
                uint8_t t141 = gm_f_runnable_o1q905g(t133, t139, t140);
                if (!t141) goto ie132;
                {
                    goto lc15;
                }
                goto ix132;
ie132:;
ix132:;
            }
            {
                gm_tf5srh4mp4b3i t143;
                memset(&t143, 0, sizeof t143);
                gm_l15 = t143;
            }
            {
                {
                    void * t145 = gm_l2;
                    gm_b16_8 t146 = GM_LD(gm_b16_8, ((((uint8_t *)t145) + 0) + 0));
                    gm_l16 = t146;
                }
                {
                    gm_l17 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t148 = gm_l17;
                        uint64_t t149 = GM_LD(uintptr_t, ((uint8_t *)&gm_l16) + 8);
                        uint8_t t150 = (t148 < t149);
                        if (!t150) goto lb147;
                        {
                            uint64_t t153 = gm_l17;
                            uint64_t t154 = (uintptr_t)t153;
                            uint64_t t155 = GM_LD(uintptr_t, ((uint8_t *)&gm_l16) + 8);
                            gm_t9jnd88rq7ul8 t158 = GM_LD(gm_t9jnd88rq7ul8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l16)) + t154 * 48));
                            F.l18 = t158;
                            {
                                uint8_t t161 = GM_LD(uint8_t, ((((uint8_t *)&F.l18) + 8) + 0));
                                if (t161 != ((uint8_t)2ULL)) goto ie160;
                                gm_tf5srh4mp4b3i t162 = GM_LD(gm_tf5srh4mp4b3i, (((((uint8_t *)&F.l18) + 8) + 8) + 8));
                                gm_l19 = t162;
                                uint8_t t163;
                                if (((((uint8_t *)&gm_l19))[8] != 0)) goto mn165;
                                t163 = ((uint8_t)0);
                                goto me164;
mn165:;
                                t163 = ((uint8_t)1);
                                goto me164;
mn166:;
                                gm_trap("no match arm matched the scrutinee", gm_srchntnl6m6g3am, 109, 45);
me164:;
                                if (!t163) goto ie160;
                                {
                                    if (!((((uint8_t *)&gm_l15))[8] != 0)) goto ie168;
                                    uint64_t t169 = GM_LD(uint64_t, ((uint8_t *)&gm_l15));
                                    gm_l20 = t169;
                                    uint64_t t170 = gm_l20;
                                    gm_tf5srh4mp4b3i t171 = gm_l19;
                                    uint64_t t172;
                                    if (((((uint8_t *)&t171))[8] != 0)) {
                                        uint64_t t173 = GM_LD(uint64_t, (((uint8_t *)&t171) + 0));
                                        t172 = t173;
                                    } else {
                                        t172 = ((uint64_t)0ULL);
                                    }
                                    uint8_t t174 = (t170 <= t172);
                                    if (!t174) goto ie168;
                                    goto ix168;
ie168:;
                                    {
                                        gm_tf5srh4mp4b3i t177 = gm_l19;
                                        gm_l15 = t177;
                                    }
ix168:;
                                }
                                goto ix160;
ie160:;
ix160:;
                            }
                            {
                                uint8_t t179 = GM_LD(uint8_t, ((((uint8_t *)&F.l18) + 8) + 0));
                                if (t179 != ((uint8_t)3ULL)) goto ie178;
                                uint64_t t180 = GM_LD(uint64_t, ((((uint8_t *)&F.l18) + 8) + 8));
                                gm_l21 = t180;
                                {
                                    if (!((((uint8_t *)&gm_l15))[8] != 0)) goto ie182;
                                    uint64_t t183 = GM_LD(uint64_t, ((uint8_t *)&gm_l15));
                                    gm_l22 = t183;
                                    uint64_t t184 = gm_l22;
                                    uint64_t t185 = gm_l21;
                                    uint8_t t186 = (t184 <= t185);
                                    if (!t186) goto ie182;
                                    goto ix182;
ie182:;
                                    {
                                        uint64_t t189 = gm_l21;
                                        gm_tf5srh4mp4b3i t190;
                                        GM_ST(uint8_t, (uint8_t *)&t190 + 9, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t190 + 10, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t190 + 12, 0);
                                        GM_ST(uint64_t, ((uint8_t *)&t190), t189);
                                        ((uint8_t *)&t190)[8] = 1;
                                        gm_l15 = t190;
                                    }
ix182:;
                                }
                                goto ix178;
ie178:;
ix178:;
                            }
                            goto me156;
mn157:;
                            gm_trap("no match arm matched the scrutinee", gm_srchntnl6m6g3am, 108, 9);
me156:;
                        }
lc147:;
                        {
                            uint64_t t192 = gm_l17;
                            uint64_t r193; bool o194 = __builtin_add_overflow(t192, ((uint64_t)1ULL), &r193);
                            uint64_t t195 = r193;
                            gm_l17 = t195;
                        }
                    }
lb147:;
                }
            }
            {
                {
                    void * t197 = gm_l2;
                    gm_b16_8 t198 = GM_LD(gm_b16_8, ((((uint8_t *)t197) + 0) + 0));
                    gm_l23 = t198;
                }
                {
                    gm_l24 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t200 = gm_l24;
                        uint64_t t201 = GM_LD(uintptr_t, ((uint8_t *)&gm_l23) + 8);
                        uint8_t t202 = (t200 < t201);
                        if (!t202) goto lb199;
                        {
                            uint64_t t205 = gm_l24;
                            uint64_t t206 = (uintptr_t)t205;
                            uint64_t t207 = GM_LD(uintptr_t, ((uint8_t *)&gm_l23) + 8);
                            gm_t9jnd88rq7ul8 t210 = GM_LD(gm_t9jnd88rq7ul8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l23)) + t206 * 48));
                            F.l25 = t210;
                            {
                                uint8_t t213;
                                uint8_t t216 = GM_LD(uint8_t, ((((uint8_t *)&F.l25) + 8) + 0));
                                if (t216 != ((uint8_t)4ULL)) goto mn215;
                                t213 = ((uint8_t)1);
                                goto me214;
mn215:;
                                t213 = ((uint8_t)0);
                                goto me214;
mn217:;
                                gm_trap("no match arm matched the scrutinee", gm_srchntnl6m6g3am, 118, 16);
me214:;
                                if (!t213) goto ie212;
                                uint8_t t218;
                                if (((((uint8_t *)&gm_l15))[8] != 0)) goto mn220;
                                t218 = ((uint8_t)1);
                                goto me219;
mn220:;
                                t218 = ((uint8_t)0);
                                goto me219;
mn221:;
                                gm_trap("no match arm matched the scrutinee", gm_srchntnl6m6g3am, 118, 40);
me219:;
                                if (!t218) goto ie212;
                                {
                                    gm_tf5srh4mp4b3i t223 = gm_l5;
                                    uint64_t t224;
                                    if (((((uint8_t *)&t223))[8] != 0)) {
                                        uint64_t t225 = GM_LD(uint64_t, (((uint8_t *)&t223) + 0));
                                        t224 = t225;
                                    } else {
                                        gm_node *pn226 = gm_find(cx, 1579066750);
                                        if (!pn226) gm_trap("no handler installed for effect `Clock`", gm_srchntnl6m6g3am, 118, 80);
                                        gm_t4ce0jd3tdro4 t227;
                                        t227 = ((gm_t4ce0jd3tdro4 (*)(void *, gm_node *, gm_node *))pn226->clauses[0])(pn226->env, pn226->hctx, pn226);
                                        if (gm_unw.kind) {
                                            goto gm_ll203;
                                        }
                                        uint64_t t228 = GM_LD(uint64_t, (((uint8_t *)&t227) + 0));
                                        t224 = t228;
                                    }
                                    uint64_t r229; bool o230 = __builtin_add_overflow(t224, ((uint64_t)1000000ULL), &r229);
                                    uint64_t t231 = o230 ? (1 ? ((uint64_t)18446744073709551615ULL) : ((uint64_t)0ULL)) : r229;
                                    gm_tf5srh4mp4b3i t232;
                                    GM_ST(uint8_t, (uint8_t *)&t232 + 9, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t232 + 10, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t232 + 12, 0);
                                    GM_ST(uint64_t, ((uint8_t *)&t232), t231);
                                    ((uint8_t *)&t232)[8] = 1;
                                    gm_l15 = t232;
                                }
                                goto ix212;
ie212:;
ix212:;
                            }
                            goto me208;
mn209:;
                            gm_trap("no match arm matched the scrutinee", gm_srchntnl6m6g3am, 117, 9);
me208:;
                        }
lc199:;
                        {
                            uint64_t t234 = gm_l24;
                            uint64_t r235; bool o236 = __builtin_add_overflow(t234, ((uint64_t)1ULL), &r235);
                            uint64_t t237 = r235;
                            gm_l24 = t237;
                        }
                    }
gm_ll203:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 4) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb199; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc199; }
                    }
                    goto gm_ll16;
lb199:;
                }
            }
            {
                gm_tf5srh4mp4b3i t238 = gm_l15;
                uint64_t t239;
                if (((((uint8_t *)&t238))[8] != 0)) {
                    uint64_t t240 = GM_LD(uint64_t, (((uint8_t *)&t238) + 0));
                    t239 = t240;
                } else {
                    void * t241 = gm_l0;
                    gm_tl874t2atnlh3 t242 = gm_l1;
                    gm_b16_8 t243;
                    GM_ST(void *, (uint8_t *)&t243, (void *)(gm_sb5pfucassrk8));
                    GM_ST(uintptr_t, (uint8_t *)&t243 + 8, (uintptr_t)(33));
                    gm_t76123gfet73g t244;
                    gm_f_trap_qh84l9d((void *)&t244, (void *)NULL, cx, NULL, t241, t242, t243);
                    if (gm_unw.kind) {
                        goto gm_ll16;
                    }
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                    GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t244);
                    goto gm_fdone;
                }
                gm_l26 = t239;
            }
            {
                gm_tf5srh4mp4b3i t245 = gm_l5;
                uint64_t t246;
                if (((((uint8_t *)&t245))[8] != 0)) {
                    uint64_t t247 = GM_LD(uint64_t, (((uint8_t *)&t245) + 0));
                    t246 = t247;
                } else {
                    gm_node *pn248 = gm_find(cx, 1579066750);
                    if (!pn248) gm_trap("no handler installed for effect `Clock`", gm_srchntnl6m6g3am, 122, 23);
                    gm_t4ce0jd3tdro4 t249;
                    t249 = ((gm_t4ce0jd3tdro4 (*)(void *, gm_node *, gm_node *))pn248->clauses[0])(pn248->env, pn248->hctx, pn248);
                    if (gm_unw.kind) {
                        goto gm_ll16;
                    }
                    uint64_t t250 = GM_LD(uint64_t, (((uint8_t *)&t249) + 0));
                    t246 = t250;
                }
                gm_l27 = t246;
            }
            {
                uint64_t t252 = gm_l26;
                uint64_t t253 = gm_l27;
                uint8_t t254 = (t252 > t253);
                if (!t254) goto ie251;
                {
                    uint64_t t256 = gm_l26;
                    uint64_t t257 = gm_l27;
                    uint64_t r258; bool o259 = __builtin_sub_overflow(t256, t257, &r258);
                    if (o259) gm_trap("integer overflow in `-`", gm_srchntnl6m6g3am, 123, 45);
                    uint64_t t260 = r258;
                    gm_t4ce0jd3tdro4 t261;
                    GM_ST(uint64_t, ((uint8_t *)&t261 + 0), t260);
                    gm_node *pn262 = gm_find(cx, 1579066750);
                    if (!pn262) gm_trap("no handler installed for effect `Clock`", gm_srchntnl6m6g3am, 123, 20);
                    gm_b0_1 t263;
                    t263 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_t4ce0jd3tdro4))pn262->clauses[2])(pn262->env, pn262->hctx, pn262, t261);
                    if (gm_unw.kind) {
                        goto gm_ll16;
                    }
                }
                goto ix251;
ie251:;
ix251:;
            }
lc15:;
        }
gm_ll16:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb15; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc15; }
        }
        goto gm_lad;
lb15:;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_thread_main_n6r943c(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_thread_main_n6r943c F;
    void * gm_l0; /* m */
    uint32_t gm_l1; /* f */
    gm_b16_8 gm_l2; /* arg */
    gm_tl874t2atnlh3 gm_l3; /* span */
    gm_tpmdr2hmdhsqr gm_l4; /* args */
    void * gm_l5; /* def */
    gm_b16_8 gm_l6; /* ret */
    gm_t76123gfet73g gm_l7; /* fl */
    uint8_t gm_l8; /* c */
    void * gm_l9; /* x */
    void * gm_l10; /* t */
    GM_STACK_CHECK(&F, 622u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        gm_b16_8 t2 = gm_l2;
        gm_tpmdr2hmdhsqr t3;
        GM_ST(gm_b16_8, ((uint8_t *)&t3 + 0), t2);
        gm_l4 = t3;
    }
    {
        void * t4 = gm_l0;
        uint32_t t5 = gm_l1;
        void * t6 = gm_f_func_ene90ss((void *)NULL, cx, NULL, t4, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l5 = t6;
    }
    {
        void * t7 = gm_l0;
        void * t8 = gm_l0;
        void * t9 = gm_l5;
        gm_t88rcucg1b7uk t10 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t9) + 64));
        uint64_t t11 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t8, t10);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t12 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t7, t11);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l6 = t12;
    }
    {
        void * t13 = gm_l0;
        gm_tl874t2atnlh3 t14 = gm_l3;
        uint32_t t15 = gm_l1;
        void * t16 = ((uint8_t *)&gm_l4);
        if ((uintptr_t)0 > (uintptr_t)1 || (uintptr_t)1 > (uintptr_t)1) gm_trap_slice((uintptr_t)0, (uintptr_t)1, 1, gm_srchntnl6m6g3am, 208, 30);
        gm_b16_8 t17;
        GM_ST(void *, (uint8_t *)&t17, (void *)(((uint8_t *)t16 + (uintptr_t)0 * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)((uintptr_t)1 - (uintptr_t)0));
        gm_b16_8 t18 = GM_LD(gm_b16_8, ((uint8_t *)&t17));
        gm_t5lf9i3n1r3co t19;
        memset(&t19, 0, sizeof t19);
        gm_b16_8 t20 = gm_l6;
        gm_tcfa6l7reoosn t21;
        gm_f_call_function_u7ovth6((void *)&t21, (void *)NULL, cx, NULL, t13, t14, t15, t18, ((void *)0), ((uint32_t)0ULL), &t19, ((uint8_t)0), t20);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b0_1 t22;
        if (!((((uint8_t *)&t21))[0] != 0)) {
            gm_b0_1 t23 = GM_LD(gm_b0_1, (((uint8_t *)&t21) + 8));
            t22 = t23;
        } else {
            gm_l7 = (*(gm_t76123gfet73g *)(((uint8_t *)&t21) + 8));
            uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 0));
            if (t26 != ((uint8_t)3ULL)) goto ie25;
            uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 8));
            gm_l8 = t27;
            {
                void * t30 = gm_l0;
                uint8_t t31;
                if (((((((uint8_t *)t30) + 1656) + 64))[1] != 0)) goto mn33;
                t31 = ((uint8_t)1);
                goto me32;
mn33:;
                t31 = ((uint8_t)0);
                goto me32;
mn34:;
                gm_trap("no match arm matched the scrutinee", gm_srchntnl6m6g3am, 210, 16);
me32:;
                if (!t31) goto ie29;
                {
                    void * t36 = gm_l0;
                    uint8_t t37 = gm_l8;
                    gm_tqs5p4n2tq19v t38;
                    GM_ST(uint8_t, ((uint8_t *)&t38), t37);
                    ((uint8_t *)&t38)[1] = 1;
                    GM_ST(gm_tqs5p4n2tq19v, ((((uint8_t *)t36) + 1656) + 64), t38);
                }
                goto ix29;
ie29:;
ix29:;
            }
            goto ix25;
ie25:;
            void * t40 = gm_l0;
            void * t41 = GM_LD(void *, ((((uint8_t *)t40) + 1656) + 56));
            uint8_t t42;
            if ((GM_LD(void *, ((uint8_t *)&t41)) != 0)) goto mn44;
            t42 = ((uint8_t)1);
            goto me43;
mn44:;
            t42 = ((uint8_t)0);
            goto me43;
mn45:;
            gm_trap("no match arm matched the scrutinee", gm_srchntnl6m6g3am, 211, 19);
me43:;
            if (!t42) goto ie39;
            {
                void * t47;
                uint8_t t49 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 0));
                if (t49 != ((uint8_t)4ULL)) goto ie48;
                void * t50 = GM_LD(void *, (((uint8_t *)&gm_l7) + 8));
                gm_l9 = t50;
                void * t52 = gm_l9;
                void * t53 = t52;
                t47 = t53;
                goto ix48;
ie48:;
                gm_tolm6p7jp1hb3 t55;
                memset((uint8_t *)&t55 + 28, 0, 4);
                gm_b16_8 t56;
                GM_ST(void *, (uint8_t *)&t56, (void *)(gm_s632oftf9nssu));
                GM_ST(uintptr_t, (uint8_t *)&t56 + 8, (uintptr_t)(42));
                GM_ST(gm_b16_8, (uint8_t *)&t55 + 0, t56);
                gm_tl874t2atnlh3 t57 = gm_l3;
                GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t55 + 16, t57);
                gm_f_list_j8jr9cr_2((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&t55 + 32)), (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tq7s98ihonkju t58 = gm_f_new_jd11mvt((void *)NULL, cx, NULL, &t55, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kujrnb073g1)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t58))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t58) + 8, gm_srchntnl6m6g3am, 212, 55);
                void * t59 = GM_LD(void *, (((uint8_t *)&t58) + 8));
                void * t60 = t59;
                t47 = t60;
ix48:;
                gm_l10 = t47;
            }
            {
                void * t61 = gm_l0;
                void * t62 = gm_l10;
                GM_ST(void *, ((((uint8_t *)t61) + 1656) + 56), t62);
            }
            goto ix39;
ie39:;
ix39:;
ix25:;
            gm_b0_1 t63 = GM_ZERO(gm_b0_1);
            t22 = t63;
        }
    }
    {
        uint64_t t65 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        uint8_t t66 = (t65 > ((uint64_t)0ULL));
        if (!t66) goto ie64;
        {
            gm_b16_8 t68 = gm_l2;
            gm_b16_8 t69 = GM_LD(gm_b16_8, ((uint8_t *)&t68));
            gm_b0_1 t70 = gm_f_free_rkuh2rm((void *)NULL, cx, NULL, t69, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k02l1tj96tm)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix64;
ie64:;
ix64:;
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
__attribute__((aligned(4))) void gm_f_thread_op_pfli7b1(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t9qkh0rsooc8d a2, gm_b16_8 a3, gm_b16_8 a4) {
    struct gm_fr_thread_op_pfli7b1 F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_t9qkh0rsooc8d gm_l2; /* op */
    gm_b16_8 gm_l3; /* vals */
    gm_b16_8 gm_l4; /* res */
    uint64_t gm_l5; /* addr */
    uint32_t gm_l6; /* f */
    gm_b16_8 gm_l7; /* arg */
    uint32_t gm_l8; /* id */
    uint32_t gm_l9; /* c */
    __int128 gm_l10; /* h */
    uint32_t gm_l11; /* t */
    __int128 gm_l12; /* h */
    uint64_t gm_l13; /* addr */
    gm_tpvg3mjn3dp06 gm_l14; /* word */
    unsigned __int128 gm_l15; /* expected */
    __int128 gm_l16; /* timeout */
    gm_tf5srh4mp4b3i gm_l17; /* deadline */
    GM_STACK_CHECK(&F, 3015u);
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
            uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            switch (t12) { case ((uint8_t)0ULL): goto ma10; case ((uint8_t)1ULL): goto ma10; case ((uint8_t)2ULL): goto ma10; case ((uint8_t)3ULL): goto ma10; case ((uint8_t)4ULL): goto ma10; case ((uint8_t)5ULL): goto ma10; case ((uint8_t)6ULL): goto ma10; case ((uint8_t)7ULL): goto ma10; case ((uint8_t)8ULL): goto ma10; case ((uint8_t)9ULL): goto ma10; case ((uint8_t)10ULL): goto ma10; case ((uint8_t)11ULL): goto ma10; case ((uint8_t)12ULL): goto ma10; case ((uint8_t)13ULL): goto ma10; case ((uint8_t)14ULL): goto ma10; case ((uint8_t)15ULL): goto ma10; case ((uint8_t)16ULL): goto ma10; case ((uint8_t)17ULL): goto ma10; case ((uint8_t)18ULL): goto ma10; case ((uint8_t)19ULL): goto ma10; case ((uint8_t)20ULL): goto ma10; case ((uint8_t)21ULL): goto ma10; case ((uint8_t)22ULL): goto ma10; case ((uint8_t)23ULL): goto ma10; case ((uint8_t)24ULL): goto ma10; case ((uint8_t)25ULL): goto ma10; case ((uint8_t)26ULL): goto ma10; case ((uint8_t)27ULL): goto ma10; case ((uint8_t)28ULL): goto ma10; case ((uint8_t)29ULL): goto ma10; case ((uint8_t)30ULL): goto ma10; case ((uint8_t)31ULL): goto ma10; case ((uint8_t)32ULL): goto ma10; case ((uint8_t)33ULL): goto ma10; case ((uint8_t)34ULL): goto ma10; case ((uint8_t)35ULL): goto ma10; case ((uint8_t)36ULL): goto ma10; case ((uint8_t)37ULL): goto ma10; case ((uint8_t)38ULL): goto ma10; case ((uint8_t)39ULL): goto ma10; case ((uint8_t)40ULL): goto ma10; case ((uint8_t)41ULL): goto ma10; case ((uint8_t)42ULL): goto ma10; case ((uint8_t)43ULL): goto ma10; case ((uint8_t)44ULL): goto ma10; case ((uint8_t)45ULL): goto ma10; case ((uint8_t)46ULL): goto ma10; case ((uint8_t)47ULL): goto ma4; case ((uint8_t)48ULL): goto ma5; case ((uint8_t)49ULL): goto ma6; case ((uint8_t)50ULL): goto ma7; case ((uint8_t)51ULL): goto ma8; case ((uint8_t)52ULL): goto ma9; case ((uint8_t)53ULL): goto ma10; case ((uint8_t)54ULL): goto ma10; default: goto ma10; }
ma4:;
            uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t14 != ((uint8_t)47ULL)) goto mn13;
            {
                void * t16 = gm_l0;
                uint64_t t17 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t18 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                if (t17 >= (uintptr_t)(t18)) gm_trap_index(t17, t18, gm_srchntnl6m6g3am, 144, 30);
                gm_b16_8 t19 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t17 * 16));
                gm_b16_8 t20 = GM_LD(gm_b16_8, ((uint8_t *)&t19));
                uint64_t t21 = gm_f_ptr_of_b77b4c9(t16, t20);
                gm_l5 = t21;
            }
            {
                void * t22 = gm_l0;
                uint64_t t23 = gm_l5;
                gm_t9805qtonbsag t24 = gm_f_fn_from_addr_7a6bpqd(t22, t23);
                uint32_t t25;
                if (((((uint8_t *)&t24))[4] != 0)) {
                    uint32_t t26 = GM_LD(uint32_t, (((uint8_t *)&t24) + 0));
                    t25 = t26;
                } else {
                    void * t27 = gm_l0;
                    gm_tl874t2atnlh3 t28 = gm_l1;
                    uint64_t t29 = gm_l5;
                    gm_t4ce0jd3tdro4 t30;
                    GM_ST(uint64_t, ((uint8_t *)&t30 + 0), t29);
                    gm_tabb710d9mq2e t31;
                    gm_f_format_uh8vg1k((void *)&t31, (void *)NULL, cx, NULL, t30);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t31))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t31) + 8, gm_srchntnl6m6g3am, 145, 65);
                    gm_b16_8 t32 = GM_LD(gm_b16_8, (((uint8_t *)&t31) + 8));
                    gm_b16_8 t33 = GM_LD(gm_b16_8, ((uint8_t *)&t32));
                    gm_t76123gfet73g t34;
                    gm_f_trap_qh84l9d((void *)&t34, (void *)NULL, cx, NULL, t27, t28, t33);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                    GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t34);
                    goto gm_fdone;
                }
                gm_l6 = t25;
            }
            {
                uint64_t t35 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t36 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                if (t35 >= (uintptr_t)(t36)) gm_trap_index(t35, t36, gm_srchntnl6m6g3am, 146, 29);
                uint64_t t37 = GM_LD(uintptr_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t35 * 16) + 8);
                uint64_t t38 = (t37 > ((uint64_t)1ULL) ? t37 : ((uint64_t)1ULL));
                gm_tabb710d9mq2e t39;
                gm_f_make_2ip78bj((void *)&t39, (void *)NULL, cx, NULL, t38, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kjs2b0icc8d)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t39))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t39) + 8, gm_srchntnl6m6g3am, 146, 20);
                gm_b16_8 t40 = GM_LD(gm_b16_8, (((uint8_t *)&t39) + 8));
                gm_l7 = t40;
            }
            {
                uint64_t t41 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                void * t42 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)));
                uint64_t t43 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t44 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t45 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                if (t44 >= (uintptr_t)(t45)) gm_trap_index(t44, t45, gm_srchntnl6m6g3am, 147, 20);
                uint64_t t46 = GM_LD(uintptr_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t44 * 16) + 8);
                uint64_t t47 = (uintptr_t)t46;
                if (t43 > t47 || t47 > (uintptr_t)t41) gm_trap_slice(t43, t47, t41, gm_srchntnl6m6g3am, 147, 13);
                gm_b16_8 t48;
                GM_ST(void *, (uint8_t *)&t48, (void *)(((uint8_t *)t42 + t43 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t48 + 8, (uintptr_t)(t47 - t43));
                uint64_t t49 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t50 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                if (t49 >= (uintptr_t)(t50)) gm_trap_index(t49, t50, gm_srchntnl6m6g3am, 147, 43);
                gm_b16_8 t51 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t49 * 16));
                gm_b16_8 t52 = GM_LD(gm_b16_8, ((uint8_t *)&t51));
                gm_f_copy_from_f7j6p05(t48, t52);
            }
            {
                void * t53 = gm_l0;
                uint64_t t54 = GM_LD(uintptr_t, (((((uint8_t *)t53) + 1656) + 0) + 0) + 8);
                uint32_t t55 = (uint32_t)t54;
                gm_l8 = t55;
            }
            {
                void * t56 = gm_l0;
                gm_t79rbdv4rafe5 t57;
                memset((uint8_t *)&t57 + 4, 0, 4);
                memset((uint8_t *)&t57 + 36, 0, 4);
                uint32_t t58 = gm_l6;
                GM_ST(uint32_t, (uint8_t *)&t57 + 0, t58);
                uint64_t t59 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                void * t60 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)));
                uint64_t t61 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t62 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t63 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                if (t62 >= (uintptr_t)(t63)) gm_trap_index(t62, t63, gm_srchntnl6m6g3am, 149, 47);
                uint64_t t64 = GM_LD(uintptr_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t62 * 16) + 8);
                uint64_t t65 = (uintptr_t)t64;
                if (t61 > t65 || t65 > (uintptr_t)t59) gm_trap_slice(t61, t65, t59, gm_srchntnl6m6g3am, 149, 40);
                gm_b16_8 t66;
                GM_ST(void *, (uint8_t *)&t66, (void *)(((uint8_t *)t60 + t61 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t66 + 8, (uintptr_t)(t65 - t61));
                GM_ST(gm_b16_8, (uint8_t *)&t57 + 8, t66);
                gm_tl874t2atnlh3 t67 = gm_l1;
                GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t57 + 24, t67);
                gm_th5tat709fgha t68;
                GM_ST(uint8_t, (uint8_t *)&t68 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t68 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t68 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t68 + 48, 0);
                GM_ST(uint64_t, (uint8_t *)&t68 + 56, 0);
                GM_ST(uint8_t, ((uint8_t *)&t68 + 0), ((uint8_t)2ULL));
                GM_ST(gm_t79rbdv4rafe5, ((uint8_t *)&t68 + 8), t57);
                uint32_t t69 = gm_l8;
                uint32_t t70 = gm_f_new_coro_06nq12a((void *)NULL, cx, NULL, t56, &t68, t69);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l9 = t70;
            }
            {
                void * t71 = gm_l0;
                void * t72 = (void *)((((uint8_t *)t71) + 1656) + 0);
                gm_t9jnd88rq7ul8 t73;
                memset((uint8_t *)&t73 + 4, 0, 4);
                memset((uint8_t *)&t73 + 41, 0, 7);
                uint32_t t74 = gm_l9;
                GM_ST(uint32_t, (uint8_t *)&t73 + 0, t74);
                gm_tdkjirf1g83ud t75;
                GM_ST(uint8_t, (uint8_t *)&t75 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t75 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t75 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t75 + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&t75 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t75 + 24, 0);
                GM_ST(uint8_t, ((uint8_t *)&t75 + 0), ((uint8_t)0ULL));
                GM_ST(gm_tdkjirf1g83ud, (uint8_t *)&t73 + 8, t75);
                GM_ST(uint8_t, (uint8_t *)&t73 + 40, ((uint8_t)0));
                gm_tftb1odc739t5 t76 = gm_f_push_bn7ud1g(t72, &t73);
                if (((((uint8_t *)&t76))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t76) + 1, gm_srchntnl6m6g3am, 150, 13);
                gm_b0_1 t77 = GM_LD(gm_b0_1, (((uint8_t *)&t76) + 1));
            }
            {
                void * t78 = gm_l0;
                gm_b16_8 t79 = gm_l4;
                uint32_t t80 = gm_l8;
                unsigned __int128 t81 = (unsigned __int128)t80;
                gm_f_put_uint_cddin99(t78, t79, t81);
            }
            goto me3;
mn13:;
ma5:;
            uint8_t t83 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t83 != ((uint8_t)48ULL)) goto mn82;
            {
                void * t85 = gm_l0;
                void * t86 = gm_l0;
                void * t87 = GM_LD(void *, (((uint8_t *)t86) + 8));
                void * t88 = (void *)(((uint8_t *)t87) + 8);
                gm_t88rcucg1b7uk t89 = gm_f_int_ci6airg((void *)NULL, cx, NULL, t88, ((uint8_t)1), ((uint16_t)64ULL));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint64_t t90 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t91 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                if (t90 >= (uintptr_t)(t91)) gm_trap_index(t90, t91, gm_srchntnl6m6g3am, 154, 51);
                gm_b16_8 t92 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t90 * 16));
                gm_b16_8 t93 = GM_LD(gm_b16_8, ((uint8_t *)&t92));
                unsigned __int128 t94 = gm_f_get_int_pg7p4gc(t85, GM_CONV(gm_csguankko4lmhd, t89), t93);
                __int128 t95;
                memset(&t95, 0, sizeof t95); memcpy(&t95, ((uint8_t *)&t94), sizeof t95 < 16 ? sizeof t95 : 16);
                gm_l10 = t95;
            }
            {
                uint8_t t97 = 0;
                uint8_t t98 = 0;
                __int128 t99 = gm_l10;
                uint8_t t100 = (t99 <= ((__int128)(((unsigned __int128)0u << 64) | 0u)));
                if (!t100) {
                    __int128 t101 = gm_l10;
                    void * t102 = gm_l0;
                    uint64_t t103 = GM_LD(uintptr_t, (((((uint8_t *)t102) + 1656) + 0) + 0) + 8);
                    __int128 t104 = (__int128)t103;
                    uint8_t t105 = (t101 >= t104);
                    t98 = t105;
                } else t98 = 1;
                if (!t98) {
                    void * t106 = gm_l0;
                    gm_b16_8 t107 = GM_LD(gm_b16_8, (((((uint8_t *)t106) + 1656) + 0) + 0));
                    gm_b16_8 t108 = t107;
                    __int128 t109 = gm_l10;
                    uint32_t t110 = (uint32_t)t109;
                    uint64_t t111 = (uint64_t)t110;
                    uint64_t t112 = (uintptr_t)t111;
                    uint64_t t113 = GM_LD(uintptr_t, ((uint8_t *)&t108) + 8);
                    if (t112 >= (uintptr_t)(t113)) gm_trap_index(t112, t113, gm_srchntnl6m6g3am, 156, 65);
                    uint8_t t114 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((uint8_t *)&t108)) + t112 * 48) + 40));
                    t97 = t114;
                } else t97 = 1;
                if (!t97) goto ie96;
                {
                    void * t116 = gm_l0;
                    gm_b16_8 t117 = gm_l4;
                    unsigned __int128 r118; bool o119 = __builtin_sub_overflow(((unsigned __int128)(((unsigned __int128)0u << 64) | 0u)), ((unsigned __int128)(((unsigned __int128)0u << 64) | 1003u)), &r118);
                    unsigned __int128 t120 = r118;
                    gm_f_put_uint_cddin99(t116, t117, t120);
                }
                {
                    gm_b0_1 t121;
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
                    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t121);
                    goto gm_fdone;
                }
                goto ix96;
ie96:;
ix96:;
            }
            {
                __int128 t122 = gm_l10;
                uint32_t t123 = (uint32_t)t122;
                gm_l11 = t123;
            }
            {
                uint32_t t124 = gm_l11;
                uint64_t t125 = (uint64_t)t124;
                uint64_t t126 = (uintptr_t)t125;
                void * t127 = gm_l0;
                uint64_t t128 = GM_LD(uintptr_t, (((((uint8_t *)t127) + 1656) + 0) + 0) + 8);
                if (t126 >= (uintptr_t)(t128)) gm_trap_index(t126, t128, gm_srchntnl6m6g3am, 161, 13);
                GM_ST(uint8_t, (((uint8_t *)GM_LD(void *, (((((uint8_t *)t127) + 1656) + 0) + 0)) + t126 * 48) + 40), ((uint8_t)1));
            }
            {
                uint32_t t130 = gm_l11;
                uint64_t t131 = (uint64_t)t130;
                uint64_t t132 = (uintptr_t)t131;
                void * t133 = gm_l0;
                uint64_t t134 = GM_LD(uintptr_t, (((((uint8_t *)t133) + 1656) + 0) + 0) + 8);
                if (t132 >= (uintptr_t)(t134)) gm_trap_index(t132, t134, gm_srchntnl6m6g3am, 162, 16);
                uint8_t t135 = (!(GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, (((((uint8_t *)t133) + 1656) + 0) + 0)) + t132 * 48) + 8) + 0) == ((uint8_t)5ULL)));
                if (!t135) goto ie129;
                {
                    void * t137 = gm_l0;
                    uint32_t t138 = GM_LD(uint32_t, ((((uint8_t *)t137) + 1656) + 40));
                    uint64_t t139 = (uint64_t)t138;
                    uint64_t t140 = (uintptr_t)t139;
                    void * t141 = gm_l0;
                    uint64_t t142 = GM_LD(uintptr_t, (((((uint8_t *)t141) + 1656) + 0) + 0) + 8);
                    if (t140 >= (uintptr_t)(t142)) gm_trap_index(t140, t142, gm_srchntnl6m6g3am, 163, 17);
                    uint32_t t143 = gm_l11;
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tdkjirf1g83ud *)(((uint8_t *)GM_LD(void *, (((((uint8_t *)t141) + 1656) + 0) + 0)) + t140 * 48) + 8)) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tdkjirf1g83ud *)(((uint8_t *)GM_LD(void *, (((((uint8_t *)t141) + 1656) + 0) + 0)) + t140 * 48) + 8)) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tdkjirf1g83ud *)(((uint8_t *)GM_LD(void *, (((((uint8_t *)t141) + 1656) + 0) + 0)) + t140 * 48) + 8)) + 4, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tdkjirf1g83ud *)(((uint8_t *)GM_LD(void *, (((((uint8_t *)t141) + 1656) + 0) + 0)) + t140 * 48) + 8)) + 12, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tdkjirf1g83ud *)(((uint8_t *)GM_LD(void *, (((((uint8_t *)t141) + 1656) + 0) + 0)) + t140 * 48) + 8)) + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tdkjirf1g83ud *)(((uint8_t *)GM_LD(void *, (((((uint8_t *)t141) + 1656) + 0) + 0)) + t140 * 48) + 8)) + 24, 0);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tdkjirf1g83ud *)(((uint8_t *)GM_LD(void *, (((((uint8_t *)t141) + 1656) + 0) + 0)) + t140 * 48) + 8)) + 0, ((uint8_t)1ULL));
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tdkjirf1g83ud *)(((uint8_t *)GM_LD(void *, (((((uint8_t *)t141) + 1656) + 0) + 0)) + t140 * 48) + 8)) + 8, t143);
                }
                {
                    void * t144 = gm_l0;
                    gm_tl874t2atnlh3 t145 = gm_l1;
                    gm_tcfa6l7reoosn t146;
                    gm_f_switch_thread_m7aoqko((void *)&t146, (void *)NULL, cx, NULL, t144, t145);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t146))[0] != 0)) {
                        gm_tcfa6l7reoosn t147;
                        GM_ST(uint8_t, (uint8_t *)&t147 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t147 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t147 + 4, 0);
                        ((uint8_t *)&t147)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t147 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t146) + 8)));
                        F.ret = t147;
                        goto gm_fret;
                    }
                    gm_b0_1 t148 = GM_LD(gm_b0_1, (((uint8_t *)&t146) + 8));
                }
                goto ix129;
ie129:;
ix129:;
            }
            {
                void * t149 = gm_l0;
                gm_tcfa6l7reoosn t150;
                gm_f_check_fatal_dgfalcm((void *)&t150, t149);
                if (((((uint8_t *)&t150))[0] != 0)) {
                    gm_tcfa6l7reoosn t151;
                    GM_ST(uint8_t, (uint8_t *)&t151 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t151 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t151 + 4, 0);
                    ((uint8_t *)&t151)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t151 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t150) + 8)));
                    F.ret = t151;
                    goto gm_fret;
                }
                gm_b0_1 t152 = GM_LD(gm_b0_1, (((uint8_t *)&t150) + 8));
            }
            {
                void * t153 = gm_l0;
                gm_b16_8 t154 = gm_l4;
                gm_f_put_uint_cddin99(t153, t154, ((unsigned __int128)(((unsigned __int128)0u << 64) | 0u)));
            }
            goto me3;
mn82:;
ma6:;
            uint8_t t156 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t156 != ((uint8_t)49ULL)) goto mn155;
            {
                void * t158 = gm_l0;
                void * t159 = gm_l0;
                void * t160 = GM_LD(void *, (((uint8_t *)t159) + 8));
                void * t161 = (void *)(((uint8_t *)t160) + 8);
                gm_t88rcucg1b7uk t162 = gm_f_int_ci6airg((void *)NULL, cx, NULL, t161, ((uint8_t)1), ((uint16_t)64ULL));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint64_t t163 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t164 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                if (t163 >= (uintptr_t)(t164)) gm_trap_index(t163, t164, gm_srchntnl6m6g3am, 170, 51);
                gm_b16_8 t165 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t163 * 16));
                gm_b16_8 t166 = GM_LD(gm_b16_8, ((uint8_t *)&t165));
                unsigned __int128 t167 = gm_f_get_int_pg7p4gc(t158, GM_CONV(gm_csguankko4lmhd, t162), t166);
                __int128 t168;
                memset(&t168, 0, sizeof t168); memcpy(&t168, ((uint8_t *)&t167), sizeof t168 < 16 ? sizeof t168 : 16);
                gm_l12 = t168;
            }
            {
                uint8_t t170 = 0;
                uint8_t t171 = 0;
                __int128 t172 = gm_l12;
                uint8_t t173 = (t172 <= ((__int128)(((unsigned __int128)0u << 64) | 0u)));
                if (!t173) {
                    __int128 t174 = gm_l12;
                    void * t175 = gm_l0;
                    uint64_t t176 = GM_LD(uintptr_t, (((((uint8_t *)t175) + 1656) + 0) + 0) + 8);
                    __int128 t177 = (__int128)t176;
                    uint8_t t178 = (t174 >= t177);
                    t171 = t178;
                } else t171 = 1;
                if (!t171) {
                    void * t179 = gm_l0;
                    gm_b16_8 t180 = GM_LD(gm_b16_8, (((((uint8_t *)t179) + 1656) + 0) + 0));
                    gm_b16_8 t181 = t180;
                    __int128 t182 = gm_l12;
                    uint32_t t183 = (uint32_t)t182;
                    uint64_t t184 = (uint64_t)t183;
                    uint64_t t185 = (uintptr_t)t184;
                    uint64_t t186 = GM_LD(uintptr_t, ((uint8_t *)&t181) + 8);
                    if (t185 >= (uintptr_t)(t186)) gm_trap_index(t185, t186, gm_srchntnl6m6g3am, 171, 65);
                    uint8_t t187 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((uint8_t *)&t181)) + t185 * 48) + 40));
                    t170 = t187;
                } else t170 = 1;
                if (!t170) goto ie169;
                {
                    void * t189 = gm_l0;
                    gm_b16_8 t190 = gm_l4;
                    unsigned __int128 r191; bool o192 = __builtin_sub_overflow(((unsigned __int128)(((unsigned __int128)0u << 64) | 0u)), ((unsigned __int128)(((unsigned __int128)0u << 64) | 1003u)), &r191);
                    unsigned __int128 t193 = r191;
                    gm_f_put_uint_cddin99(t189, t190, t193);
                }
                {
                    gm_b0_1 t194;
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
                    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t194);
                    goto gm_fdone;
                }
                goto ix169;
ie169:;
ix169:;
            }
            {
                void * t195 = gm_l0;
                gm_b16_8 t196 = GM_LD(gm_b16_8, (((((uint8_t *)t195) + 1656) + 0) + 0));
                gm_b16_8 t197 = t196;
                __int128 t198 = gm_l12;
                uint32_t t199 = (uint32_t)t198;
                uint64_t t200 = (uint64_t)t199;
                uint64_t t201 = (uintptr_t)t200;
                uint64_t t202 = GM_LD(uintptr_t, ((uint8_t *)&t197) + 8);
                if (t201 >= (uintptr_t)(t202)) gm_trap_index(t201, t202, gm_srchntnl6m6g3am, 175, 13);
                GM_ST(uint8_t, (((uint8_t *)GM_LD(void *, ((uint8_t *)&t197)) + t201 * 48) + 40), ((uint8_t)1));
            }
            {
                void * t203 = gm_l0;
                gm_b16_8 t204 = gm_l4;
                gm_f_put_uint_cddin99(t203, t204, ((unsigned __int128)(((unsigned __int128)0u << 64) | 0u)));
            }
            goto me3;
mn155:;
ma7:;
            uint8_t t206 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t206 != ((uint8_t)50ULL)) goto mn205;
            void * t207 = gm_l0;
            gm_tl874t2atnlh3 t208 = gm_l1;
            gm_tcfa6l7reoosn t209;
            gm_f_switch_thread_m7aoqko((void *)&t209, (void *)NULL, cx, NULL, t207, t208);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t209))[0] != 0)) {
                gm_tcfa6l7reoosn t210;
                GM_ST(uint8_t, (uint8_t *)&t210 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t210 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t210 + 4, 0);
                ((uint8_t *)&t210)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t210 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t209) + 8)));
                F.ret = t210;
                goto gm_fret;
            }
            gm_b0_1 t211 = GM_LD(gm_b0_1, (((uint8_t *)&t209) + 8));
            goto me3;
mn205:;
ma8:;
            uint8_t t213 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t213 != ((uint8_t)51ULL)) goto mn212;
            {
                void * t215 = gm_l0;
                gm_tcfa6l7reoosn t216;
                gm_f_check_fatal_dgfalcm((void *)&t216, t215);
                if (((((uint8_t *)&t216))[0] != 0)) {
                    gm_tcfa6l7reoosn t217;
                    GM_ST(uint8_t, (uint8_t *)&t217 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t217 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t217 + 4, 0);
                    ((uint8_t *)&t217)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t217 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t216) + 8)));
                    F.ret = t217;
                    goto gm_fret;
                }
                gm_b0_1 t218 = GM_LD(gm_b0_1, (((uint8_t *)&t216) + 8));
            }
            {
                void * t219 = gm_l0;
                uint64_t t220 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t221 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                if (t220 >= (uintptr_t)(t221)) gm_trap_index(t220, t221, gm_srchntnl6m6g3am, 181, 30);
                gm_b16_8 t222 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t220 * 16));
                gm_b16_8 t223 = GM_LD(gm_b16_8, ((uint8_t *)&t222));
                uint64_t t224 = gm_f_ptr_of_b77b4c9(t219, t223);
                gm_l13 = t224;
            }
            {
                gm_tpvg3mjn3dp06 t225;
                GM_ST(uint8_t, ((uint8_t *)&t225 + 0), ((uint8_t)0ULL));
                GM_ST(uint8_t, ((uint8_t *)&t225 + 1), ((uint8_t)0ULL));
                GM_ST(uint8_t, ((uint8_t *)&t225 + 2), ((uint8_t)0ULL));
                GM_ST(uint8_t, ((uint8_t *)&t225 + 3), ((uint8_t)0ULL));
                gm_l14 = t225;
            }
            {
                void * t226 = gm_l0;
                gm_tl874t2atnlh3 t227 = gm_l1;
                uint64_t t228 = gm_l13;
                void * t229 = ((uint8_t *)&gm_l14);
                if ((uintptr_t)0 > (uintptr_t)4 || (uintptr_t)4 > (uintptr_t)4) gm_trap_slice((uintptr_t)0, (uintptr_t)4, 4, gm_srchntnl6m6g3am, 183, 42);
                gm_b16_8 t230;
                GM_ST(void *, (uint8_t *)&t230, (void *)(((uint8_t *)t229 + (uintptr_t)0 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t230 + 8, (uintptr_t)((uintptr_t)4 - (uintptr_t)0));
                gm_tcfa6l7reoosn t231;
                gm_f_load_at_6u81jcc((void *)&t231, (void *)NULL, cx, NULL, t226, t227, t228, ((uint64_t)4ULL), t230);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t231))[0] != 0)) {
                    gm_tcfa6l7reoosn t232;
                    GM_ST(uint8_t, (uint8_t *)&t232 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t232 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t232 + 4, 0);
                    ((uint8_t *)&t232)[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&t232 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t231) + 8)));
                    F.ret = t232;
                    goto gm_fret;
                }
                gm_b0_1 t233 = GM_LD(gm_b0_1, (((uint8_t *)&t231) + 8));
            }
            {
                void * t234 = gm_l0;
                uint64_t t235 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t236 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                if (t235 >= (uintptr_t)(t236)) gm_trap_index(t235, t236, gm_srchntnl6m6g3am, 184, 36);
                gm_b16_8 t237 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t235 * 16));
                gm_b16_8 t238 = GM_LD(gm_b16_8, ((uint8_t *)&t237));
                unsigned __int128 t239 = gm_f_get_uint_bq8c4b7(t234, t238);
                unsigned __int128 t240 = (unsigned __int128)(t239 & ((unsigned __int128)(((unsigned __int128)0u << 64) | 4294967295u)));
                gm_l15 = t240;
            }
            {
                void * t242 = gm_l0;
                void * t243 = ((uint8_t *)&gm_l14);
                if ((uintptr_t)0 > (uintptr_t)4 || (uintptr_t)4 > (uintptr_t)4) gm_trap_slice((uintptr_t)0, (uintptr_t)4, 4, gm_srchntnl6m6g3am, 185, 27);
                gm_b16_8 t244;
                GM_ST(void *, (uint8_t *)&t244, (void *)(((uint8_t *)t243 + (uintptr_t)0 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t244 + 8, (uintptr_t)((uintptr_t)4 - (uintptr_t)0));
                gm_b16_8 t245 = GM_LD(gm_b16_8, ((uint8_t *)&t244));
                unsigned __int128 t246 = gm_f_get_uint_bq8c4b7(t242, t245);
                unsigned __int128 t247 = gm_l15;
                uint8_t t248 = ((t246 == t247));
                if (!t248) goto ie241;
                {
                    void * t250 = gm_l0;
                    void * t251 = gm_l0;
                    void * t252 = GM_LD(void *, (((uint8_t *)t251) + 8));
                    void * t253 = (void *)(((uint8_t *)t252) + 8);
                    gm_t88rcucg1b7uk t254 = gm_f_int_ci6airg((void *)NULL, cx, NULL, t253, ((uint8_t)1), ((uint16_t)64ULL));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint64_t t255 = (uintptr_t)((uint64_t)2ULL);
                    uint64_t t256 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    if (t255 >= (uintptr_t)(t256)) gm_trap_index(t255, t256, gm_srchntnl6m6g3am, 186, 61);
                    gm_b16_8 t257 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t255 * 16));
                    gm_b16_8 t258 = GM_LD(gm_b16_8, ((uint8_t *)&t257));
                    unsigned __int128 t259 = gm_f_get_int_pg7p4gc(t250, GM_CONV(gm_csguankko4lmhd, t254), t258);
                    __int128 t260;
                    memset(&t260, 0, sizeof t260); memcpy(&t260, ((uint8_t *)&t259), sizeof t260 < 16 ? sizeof t260 : 16);
                    gm_l16 = t260;
                }
                {
                    gm_tf5srh4mp4b3i t261;
                    memset(&t261, 0, sizeof t261);
                    gm_l17 = t261;
                }
                {
                    __int128 t263 = gm_l16;
                    uint8_t t264 = (t263 >= ((__int128)(((unsigned __int128)0u << 64) | 0u)));
                    if (!t264) goto ie262;
                    {
                        gm_node *pn266 = gm_find(cx, 1579066750);
                        if (!pn266) gm_trap("no handler installed for effect `Clock`", gm_srchntnl6m6g3am, 188, 46);
                        gm_t4ce0jd3tdro4 t267;
                        t267 = ((gm_t4ce0jd3tdro4 (*)(void *, gm_node *, gm_node *))pn266->clauses[0])(pn266->env, pn266->hctx, pn266);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint64_t t268 = GM_LD(uint64_t, (((uint8_t *)&t267) + 0));
                        __int128 t269 = gm_l16;
                        uint64_t t270 = (uint64_t)t269;
                        uint64_t r271; bool o272 = __builtin_add_overflow(t268, t270, &r271);
                        uint64_t t273 = o272 ? (1 ? ((uint64_t)18446744073709551615ULL) : ((uint64_t)0ULL)) : r271;
                        gm_tf5srh4mp4b3i t274;
                        GM_ST(uint8_t, (uint8_t *)&t274 + 9, 0);
                        GM_ST(uint16_t, (uint8_t *)&t274 + 10, 0);
                        GM_ST(uint32_t, (uint8_t *)&t274 + 12, 0);
                        GM_ST(uint64_t, ((uint8_t *)&t274), t273);
                        ((uint8_t *)&t274)[8] = 1;
                        gm_l17 = t274;
                    }
                    goto ix262;
ie262:;
ix262:;
                }
                {
                    void * t275 = gm_l0;
                    uint32_t t276 = GM_LD(uint32_t, ((((uint8_t *)t275) + 1656) + 40));
                    uint64_t t277 = (uint64_t)t276;
                    uint64_t t278 = (uintptr_t)t277;
                    void * t279 = gm_l0;
                    uint64_t t280 = GM_LD(uintptr_t, (((((uint8_t *)t279) + 1656) + 0) + 0) + 8);
                    if (t278 >= (uintptr_t)(t280)) gm_trap_index(t278, t280, gm_srchntnl6m6g3am, 189, 17);
                    gm_tk4qs2jduhfn8 t281;
                    void * t282 = gm_l0;
                    uint64_t t283 = GM_LD(uint64_t, ((((uint8_t *)t282) + 1656) + 48));
                    GM_ST(uint64_t, (uint8_t *)&t281 + 0, t283);
                    gm_tf5srh4mp4b3i t284 = gm_l17;
                    GM_ST(gm_tf5srh4mp4b3i, (uint8_t *)&t281 + 8, t284);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tdkjirf1g83ud *)(((uint8_t *)GM_LD(void *, (((((uint8_t *)t279) + 1656) + 0) + 0)) + t278 * 48) + 8)) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tdkjirf1g83ud *)(((uint8_t *)GM_LD(void *, (((((uint8_t *)t279) + 1656) + 0) + 0)) + t278 * 48) + 8)) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tdkjirf1g83ud *)(((uint8_t *)GM_LD(void *, (((((uint8_t *)t279) + 1656) + 0) + 0)) + t278 * 48) + 8)) + 4, 0);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tdkjirf1g83ud *)(((uint8_t *)GM_LD(void *, (((((uint8_t *)t279) + 1656) + 0) + 0)) + t278 * 48) + 8)) + 0, ((uint8_t)2ULL));
                    GM_ST(gm_tk4qs2jduhfn8, (uint8_t *)&(*(gm_tdkjirf1g83ud *)(((uint8_t *)GM_LD(void *, (((((uint8_t *)t279) + 1656) + 0) + 0)) + t278 * 48) + 8)) + 8, t281);
                }
                {
                    void * t285 = gm_l0;
                    gm_tl874t2atnlh3 t286 = gm_l1;
                    gm_tcfa6l7reoosn t287;
                    gm_f_switch_thread_m7aoqko((void *)&t287, (void *)NULL, cx, NULL, t285, t286);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t287))[0] != 0)) {
                        gm_tcfa6l7reoosn t288;
                        GM_ST(uint8_t, (uint8_t *)&t288 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t288 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t288 + 4, 0);
                        ((uint8_t *)&t288)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&t288 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t287) + 8)));
                        F.ret = t288;
                        goto gm_fret;
                    }
                    gm_b0_1 t289 = GM_LD(gm_b0_1, (((uint8_t *)&t287) + 8));
                }
                goto ix241;
ie241:;
ix241:;
            }
            {
                void * t290 = gm_l0;
                gm_b16_8 t291 = gm_l4;
                gm_f_put_uint_cddin99(t290, t291, ((unsigned __int128)(((unsigned __int128)0u << 64) | 0u)));
            }
            goto me3;
mn212:;
ma9:;
            uint8_t t293 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (t293 != ((uint8_t)52ULL)) goto mn292;
            {
                void * t295 = gm_l0;
                void * t296 = gm_l0;
                uint64_t t297 = GM_LD(uint64_t, ((((uint8_t *)t296) + 1656) + 48));
                uint64_t r298; bool o299 = __builtin_add_overflow(t297, ((uint64_t)1ULL), &r298);
                if (o299) gm_trap("integer overflow in `+`", gm_srchntnl6m6g3am, 196, 13);
                uint64_t t300 = r298;
                GM_ST(uint64_t, ((((uint8_t *)t295) + 1656) + 48), t300);
            }
            {
                void * t301 = gm_l0;
                gm_b16_8 t302 = gm_l4;
                gm_f_put_uint_cddin99(t301, t302, ((unsigned __int128)(((unsigned __int128)0u << 64) | 0u)));
            }
            goto me3;
mn292:;
ma10:;
            void * t304 = gm_l0;
            gm_tl874t2atnlh3 t305 = gm_l1;
            gm_b16_8 t306;
            GM_ST(void *, (uint8_t *)&t306, (void *)(gm_svu0pqir618v4));
            GM_ST(uintptr_t, (uint8_t *)&t306 + 8, (uintptr_t)(38));
            gm_t76123gfet73g t307;
            gm_f_trap_qh84l9d((void *)&t307, (void *)NULL, cx, NULL, t304, t305, t306);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
            GM_ST(gm_t76123gfet73g, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t307);
            goto gm_fdone;
            goto me3;
mn303:;
ma11:;
            gm_trap("no match arm matched the scrutinee", gm_srchntnl6m6g3am, 142, 5);
me3:;
        }
    }
    gm_b0_1 t308;
    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t308);
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
inline __attribute__((aligned(4))) void gm_f_threads_kh5dldj(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_threads_kh5dldj F;
    GM_STACK_CHECK(&F, 92u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_jh4e1ji((void *)&F.l0, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l0);
        gm_t9jnd88rq7ul8 t3;
        memset((uint8_t *)&t3 + 4, 0, 4);
        memset((uint8_t *)&t3 + 41, 0, 7);
        GM_ST(uint32_t, (uint8_t *)&t3 + 0, ((uint32_t)0ULL));
        gm_tdkjirf1g83ud t4;
        GM_ST(uint8_t, (uint8_t *)&t4 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t4 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t4 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t4 + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&t4 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t4 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t4 + 0), ((uint8_t)0ULL));
        GM_ST(gm_tdkjirf1g83ud, (uint8_t *)&t3 + 8, t4);
        GM_ST(uint8_t, (uint8_t *)&t3 + 40, ((uint8_t)0));
        gm_tftb1odc739t5 t5 = gm_f_push_bn7ud1g(t2, &t3);
        if (((((uint8_t *)&t5))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t5) + 1, gm_srchntnl6m6g3am, 54, 5);
        gm_b0_1 t6 = GM_LD(gm_b0_1, (((uint8_t *)&t5) + 1));
    }
    {
        memset((uint8_t *)&(*(gm_tobnqifq2n0g5 *)gm_out) + 66, 0, 6);
        memcpy((uint8_t *)&(*(gm_tobnqifq2n0g5 *)gm_out) + 0, ((uint8_t *)&F.l0), 40);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tobnqifq2n0g5 *)gm_out) + 40, ((uint32_t)0ULL));
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tobnqifq2n0g5 *)gm_out) + 44, ((uint32_t)0ULL));
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tobnqifq2n0g5 *)gm_out) + 48, ((uint64_t)0ULL));
        GM_ST(void *, (uint8_t *)&(*(gm_tobnqifq2n0g5 *)gm_out) + 56, ((void *)0));
        gm_tqs5p4n2tq19v t7;
        memset(&t7, 0, sizeof t7);
        GM_ST(gm_tqs5p4n2tq19v, (uint8_t *)&(*(gm_tobnqifq2n0g5 *)gm_out) + 64, t7);
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
inline __attribute__((aligned(4))) void gm_f_tick_6bsfu3n(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1) {
    struct gm_fr_tick_6bsfu3n F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    void * gm_l2; /* th */
    GM_STACK_CHECK(&F, 194u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        {
            void * t3 = gm_l0;
            void * t4 = (void *)(((uint8_t *)t3) + 1656);
            gm_l2 = t4;
        }
        {
            void * t6 = gm_l2;
            uint64_t t7 = GM_LD(uintptr_t, ((((uint8_t *)t6) + 0) + 0) + 8);
            uint8_t t8 = ((t7 == ((uint64_t)1ULL)));
            if (!t8) goto ie5;
            {
                gm_b0_1 t10;
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
                ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
                GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t10);
                goto gm_fdone;
            }
            goto ix5;
ie5:;
ix5:;
        }
        {
            void * t11 = gm_l2;
            void * t12 = gm_l2;
            uint32_t t13 = GM_LD(uint32_t, (((uint8_t *)t12) + 44));
            uint32_t r14; bool o15 = __builtin_add_overflow(t13, ((uint32_t)1ULL), &r14);
            if (o15) gm_trap("integer overflow in `+`", gm_srchntnl6m6g3am, 62, 5);
            uint32_t t16 = r14;
            GM_ST(uint32_t, (((uint8_t *)t11) + 44), t16);
        }
        {
            void * t18 = gm_l2;
            uint32_t t19 = GM_LD(uint32_t, (((uint8_t *)t18) + 44));
            uint32_t t20 = GM_LD(uint32_t, gm_cusfosftpfn);
            uint8_t t21 = (t19 < t20);
            if (!t21) goto ie17;
            {
                gm_b0_1 t23;
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
                ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
                GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t23);
                goto gm_fdone;
            }
            goto ix17;
ie17:;
ix17:;
        }
        {
            void * t24 = gm_l2;
            GM_ST(uint32_t, (((uint8_t *)t24) + 44), ((uint32_t)0ULL));
        }
        {
            void * t25 = gm_l0;
            gm_tl874t2atnlh3 t26 = gm_l1;
            gm_tcfa6l7reoosn t27;
            gm_f_switch_thread_m7aoqko((void *)&t27, (void *)NULL, cx, NULL, t25, t26);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t27))[0] != 0)) {
                gm_tcfa6l7reoosn t28;
                GM_ST(uint8_t, (uint8_t *)&t28 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t28 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t28 + 4, 0);
                ((uint8_t *)&t28)[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&t28 + 8), (*(gm_t76123gfet73g *)(((uint8_t *)&t27) + 8)));
                F.ret = t28;
                goto gm_fret;
            }
            gm_b0_1 t29 = GM_LD(gm_b0_1, (((uint8_t *)&t27) + 8));
        }
    }
    gm_b0_1 t30;
    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
    GM_ST(gm_b0_1, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, t30);
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

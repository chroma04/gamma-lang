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
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; void * f2; } gm_ta612fil812pm;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { void * f0; uint32_t f1; uint8_t pe[4]; } gm_tbl8il92l97ut;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; } gm_tgtub0v94d5u5;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint8_t f2; uint8_t pe[7]; } gm_tlcl4u558l6p5;
typedef struct { void * f0; void * f1; uint32_t f2; uint32_t f3; } gm_tmm67r2c94d5j;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; } f1; } gm_tmn7n0n9raou5;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; } gm_toh0ootodhjue;
typedef struct { void * f0; uint8_t f1; uint8_t pe[7]; } gm_tsplfr33dsvji;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { void * f0; gm_t9805qtonbsag f1; uint32_t f2; uint8_t pe[4]; } gm_t083kd7b1kkag;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; gm_t9qkh0rsooc8d f2; uint8_t p3[3]; gm_tf5srh4mp4b3i f3; } gm_t0mglksrkfa5e;
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
typedef struct { void * f0; gm_t9805qtonbsag f1; void * f2; } gm_t3dcoc7077t1q;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; uint8_t f2; uint8_t pe[3]; } gm_t3rcjibl8kkg4;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; void * f3; } gm_t710lsrq1m8gk;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t79rbdv4rafe5;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; void * f1; } gm_t8ghgl2jj0qha;
typedef struct { gm_tmn7n0n9raou5 f0; void * f1; } gm_t8jsc4n9vp2ib;
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
_Static_assert(sizeof(gm_ta612fil812pm) == 24 && _Alignof(gm_ta612fil812pm) == 8 && offsetof(gm_ta612fil812pm, f0) == 0 && offsetof(gm_ta612fil812pm, f1) == 8 && offsetof(gm_ta612fil812pm, f2) == 16, "layout of `(is_and: bool, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tao63pk3r18v5) == 32 && _Alignof(gm_tao63pk3r18v5) == 8 && offsetof(gm_tao63pk3r18v5, f0) == 0 && offsetof(gm_tao63pk3r18v5, f1) == 8 && offsetof(gm_tao63pk3r18v5, f2) == 16 && offsetof(gm_tao63pk3r18v5, f3) == 24, "layout of `SliceExpr`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
_Static_assert(sizeof(gm_tbl8il92l97ut) == 16 && _Alignof(gm_tbl8il92l97ut) == 8 && offsetof(gm_tbl8il92l97ut, f0) == 0 && offsetof(gm_tbl8il92l97ut, f1) == 8, "layout of `SpecCall`");
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_tcc1rq4n1avb2 v0; } f1; } gm_te9hrk0u35rev;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t pe[4]; } gm_tep2drsl9408b;
typedef struct { uint8_t f0; union { gm_toh0ootodhjue v0; } f1; } gm_tesaltf0mhcki;
typedef struct { uint64_t f0; gm_b16_8 f1; } gm_tf1bjg81u0glv;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
_Static_assert(sizeof(gm_tgtub0v94d5u5) == 16 && _Alignof(gm_tgtub0v94d5u5) == 8 && offsetof(gm_tgtub0v94d5u5, f0) == 0 && offsetof(gm_tgtub0v94d5u5, f1) == 8, "layout of `(is_const: bool, inner: ^Expr)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_tibupt4dmcqai;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_tf5srh4mp4b3i f1; uint8_t f2; uint8_t pe[7]; } gm_tjpqvfd72o62u;
typedef struct { void * f0; gm_b16_8 f1; } gm_tkaapcii86udk;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tlcl4u558l6p5) == 24 && _Alignof(gm_tlcl4u558l6p5) == 8 && offsetof(gm_tlcl4u558l6p5, f0) == 0 && offsetof(gm_tlcl4u558l6p5, f1) == 8 && offsetof(gm_tlcl4u558l6p5, f2) == 16, "layout of `(lo: ^Expr, hi: ^Expr, inclusive: bool)`");
typedef struct { void * f0; gm_b16_8 f1; uint8_t f2; uint8_t pe[7]; } gm_tlljg61a2ctgq;
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
_Static_assert(sizeof(gm_tmn7n0n9raou5) == 8 && _Alignof(gm_tmn7n0n9raou5) == 4 && offsetof(gm_tmn7n0n9raou5, f0) == 0 && offsetof(gm_tmn7n0n9raou5, f1) == 4, "layout of `Coercion`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
_Static_assert(sizeof(gm_toh0ootodhjue) == 3 && _Alignof(gm_toh0ootodhjue) == 1 && offsetof(gm_toh0ootodhjue, f0) == 0 && offsetof(gm_toh0ootodhjue, f1) == 1 && offsetof(gm_toh0ootodhjue, f2) == 2, "layout of `Restr`");
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; void * v1; } f1; } gm_tq7s98ihonkju;
typedef struct { gm_b16_8 f0; void * f1; void * f2; } gm_trl2khmuphncd;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
_Static_assert(sizeof(gm_tsplfr33dsvji) == 16 && _Alignof(gm_tsplfr33dsvji) == 8 && offsetof(gm_tsplfr33dsvji, f0) == 0 && offsetof(gm_tsplfr33dsvji, f1) == 8, "layout of `(expr: ^Expr, is_result: bool)`");
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
typedef struct { void * f0; uint32_t f1; gm_tl874t2atnlh3 f2; } gm_tupk62cr6lbdu;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_tcc1rq4n1avb2 v3; uint32_t v4; uint32_t v5; } f1; } gm_tvdg94bk7aa22;
_Static_assert(sizeof(gm_t083kd7b1kkag) == 24 && _Alignof(gm_t083kd7b1kkag) == 8 && offsetof(gm_t083kd7b1kkag, f0) == 0 && offsetof(gm_t083kd7b1kkag, f1) == 8 && offsetof(gm_t083kd7b1kkag, f2) == 16, "layout of `TryExpr`");
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t0mglksrkfa5e) == 40 && _Alignof(gm_t0mglksrkfa5e) == 8 && offsetof(gm_t0mglksrkfa5e, f0) == 0 && offsetof(gm_t0mglksrkfa5e, f1) == 16 && offsetof(gm_t0mglksrkfa5e, f2) == 20 && offsetof(gm_t0mglksrkfa5e, f3) == 24, "layout of `FieldDef`");
typedef struct { gm_t08olq97rscn3 f0; uint8_t f1; uint8_t pe[7]; } gm_t1s75i08qtptd;
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `WasmReader`");
_Static_assert(sizeof(gm_t3dcoc7077t1q) == 24 && _Alignof(gm_t3dcoc7077t1q) == 8 && offsetof(gm_t3dcoc7077t1q, f0) == 0 && offsetof(gm_t3dcoc7077t1q, f1) == 8 && offsetof(gm_t3dcoc7077t1q, f2) == 16, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_t3rcjibl8kkg4) == 12 && _Alignof(gm_t3rcjibl8kkg4) == 4 && offsetof(gm_t3rcjibl8kkg4, f0) == 0 && offsetof(gm_t3rcjibl8kkg4, f1) == 4 && offsetof(gm_t3rcjibl8kkg4, f2) == 8, "layout of `(a: Ty, b: Ty, negated: bool)`");
typedef struct { gm_tiilmfd16v3fc f0; uint32_t f1; uint8_t pe[4]; } gm_t419mo45g7j0a;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_th2lqbo77pvsc f6; } gm_t4gut2c9fj1kt;
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t710lsrq1m8gk) == 40 && _Alignof(gm_t710lsrq1m8gk) == 8 && offsetof(gm_t710lsrq1m8gk, f0) == 0 && offsetof(gm_t710lsrq1m8gk, f1) == 8 && offsetof(gm_t710lsrq1m8gk, f2) == 24 && offsetof(gm_t710lsrq1m8gk, f3) == 32, "layout of `WithExpr`");
_Static_assert(sizeof(gm_t79rbdv4rafe5) == 40 && _Alignof(gm_t79rbdv4rafe5) == 8 && offsetof(gm_t79rbdv4rafe5, f0) == 0 && offsetof(gm_t79rbdv4rafe5, f1) == 8 && offsetof(gm_t79rbdv4rafe5, f2) == 24, "layout of `Block`");
typedef struct { gm_tjpqvfd72o62u f0; uint8_t f1; uint8_t pe[7]; } gm_t8cc1mtbrhlpt;
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
_Static_assert(sizeof(gm_t8jsc4n9vp2ib) == 16 && _Alignof(gm_t8jsc4n9vp2ib) == 8 && offsetof(gm_t8jsc4n9vp2ib, f0) == 0 && offsetof(gm_t8jsc4n9vp2ib, f1) == 8, "layout of `(kind: Coercion, expr: ^Expr)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
typedef struct { uint64_t f0; gm_t0mglksrkfa5e f1; } gm_t8ta482bl6be0;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { gm_tesaltf0mhcki f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tandvbt63uk7n;
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
typedef struct { gm_tnnjeerolos0a f0; gm_tnnjeerolos0a f1; } gm_tcf2f71ra2nps;
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
typedef struct { gm_tep2drsl9408b f0; uint8_t f1; uint8_t pe[7]; } gm_tdlpv0q69lov1;
_Static_assert(sizeof(gm_te9hrk0u35rev) == 12 && _Alignof(gm_te9hrk0u35rev) == 4 && offsetof(gm_te9hrk0u35rev, f0) == 0 && offsetof(gm_te9hrk0u35rev, f1) == 4, "layout of `Target`");
typedef struct { gm_te9hrk0u35rev f0; uint8_t p1[4]; void * f1; } gm_teo9d4mncjfdu;
_Static_assert(sizeof(gm_tep2drsl9408b) == 24 && _Alignof(gm_tep2drsl9408b) == 8 && offsetof(gm_tep2drsl9408b, f0) == 0 && offsetof(gm_tep2drsl9408b, f1) == 16, "layout of `ValTy`");
_Static_assert(sizeof(gm_tesaltf0mhcki) == 4 && _Alignof(gm_tesaltf0mhcki) == 1 && offsetof(gm_tesaltf0mhcki, f0) == 0 && offsetof(gm_tesaltf0mhcki, f1) == 1, "layout of `SoaOp`");
typedef struct { gm_t08olq97rscn3 f0; gm_th2lqbo77pvsc f1; } gm_tet9guhev5r4u;
_Static_assert(sizeof(gm_tf1bjg81u0glv) == 24 && _Alignof(gm_tf1bjg81u0glv) == 8 && offsetof(gm_tf1bjg81u0glv, f0) == 0 && offsetof(gm_tf1bjg81u0glv, f1) == 8, "layout of `(i: usize, x: string)`");
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `Toolchain`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_tibupt4dmcqai) == 24 && _Alignof(gm_tibupt4dmcqai) == 8 && offsetof(gm_tibupt4dmcqai, f0) == 0 && offsetof(gm_tibupt4dmcqai, f1) == 16, "layout of `StrLit`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
typedef struct { void * f0; gm_t08olq97rscn3 f1; } gm_tioie2ju3q4ul;
_Static_assert(sizeof(gm_tjpqvfd72o62u) == 32 && _Alignof(gm_tjpqvfd72o62u) == 8 && offsetof(gm_tjpqvfd72o62u, f0) == 0 && offsetof(gm_tjpqvfd72o62u, f1) == 8 && offsetof(gm_tjpqvfd72o62u, f2) == 24, "layout of `SoaInfo`");
typedef struct { uint64_t f0; gm_tep2drsl9408b f1; } gm_tjvm1fbs3qbum;
_Static_assert(sizeof(gm_tkaapcii86udk) == 24 && _Alignof(gm_tkaapcii86udk) == 8 && offsetof(gm_tkaapcii86udk, f0) == 0 && offsetof(gm_tkaapcii86udk, f1) == 8, "layout of `Place`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tlljg61a2ctgq) == 32 && _Alignof(gm_tlljg61a2ctgq) == 8 && offsetof(gm_tlljg61a2ctgq, f0) == 0 && offsetof(gm_tlljg61a2ctgq, f1) == 8 && offsetof(gm_tlljg61a2ctgq, f2) == 24, "layout of `(callee: ^Expr, args: []Arg, trailing_comma: bool)`");
typedef struct { gm_th2lqbo77pvsc f0; void * f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tm7a6rksvs6n5;
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t p2[7]; gm_tf5srh4mp4b3i f2; gm_tkn42u7na3t81 f3; } gm_tpautr4br7h3l;
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
typedef struct { gm_th2lqbo77pvsc f0; void * f1; } gm_tpouv9r7lrri2;
_Static_assert(sizeof(gm_tq7s98ihonkju) == 16 && _Alignof(gm_tq7s98ihonkju) == 8 && offsetof(gm_tq7s98ihonkju, f0) == 0 && offsetof(gm_tq7s98ihonkju, f1) == 8, "layout of `AllocError!^HandlerDecl`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_ttggq7ng4kp3p v0; gm_tpe1hv3fekn05 v1; gm_t3mf58gv9kf3n v2; gm_tpe1hv3fekn05 v3; void * v4; gm_tgtub0v94d5u5 v5; } f1; } gm_trcmmd12cefb1;
_Static_assert(sizeof(gm_trl2khmuphncd) == 32 && _Alignof(gm_trl2khmuphncd) == 8 && offsetof(gm_trl2khmuphncd, f0) == 0 && offsetof(gm_trl2khmuphncd, f1) == 16 && offsetof(gm_trl2khmuphncd, f2) == 24, "layout of `IfExpr`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
typedef struct { gm_tvdg94bk7aa22 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tt8o5kebou62r;
typedef struct { gm_t6j6smj7qoctu f0; uint8_t f1; uint8_t pe[7]; } gm_ttu09lij36f8c;
_Static_assert(sizeof(gm_tupk62cr6lbdu) == 24 && _Alignof(gm_tupk62cr6lbdu) == 8 && offsetof(gm_tupk62cr6lbdu, f0) == 0 && offsetof(gm_tupk62cr6lbdu, f1) == 8 && offsetof(gm_tupk62cr6lbdu, f2) == 12, "layout of `(base: ^Expr, index: u32, index_span: Span)`");
_Static_assert(sizeof(gm_tvdg94bk7aa22) == 12 && _Alignof(gm_tvdg94bk7aa22) == 4 && offsetof(gm_tvdg94bk7aa22, f0) == 0 && offsetof(gm_tvdg94bk7aa22, f1) == 4, "layout of `Builtin`");
_Static_assert(sizeof(gm_t1s75i08qtptd) == 40 && _Alignof(gm_t1s75i08qtptd) == 8 && offsetof(gm_t1s75i08qtptd, f0) == 0 && offsetof(gm_t1s75i08qtptd, f1) == 32, "layout of `?Ident`");
_Static_assert(sizeof(gm_t419mo45g7j0a) == 32 && _Alignof(gm_t419mo45g7j0a) == 8 && offsetof(gm_t419mo45g7j0a, f0) == 0 && offsetof(gm_t419mo45g7j0a, f1) == 24, "layout of `(effect_: EffectRef, op: u32)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t8cc1mtbrhlpt) == 40 && _Alignof(gm_t8cc1mtbrhlpt) == 8 && offsetof(gm_t8cc1mtbrhlpt, f0) == 0 && offsetof(gm_t8cc1mtbrhlpt, f1) == 32, "layout of `?SoaInfo`");
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_t8ta482bl6be0) == 48 && _Alignof(gm_t8ta482bl6be0) == 8 && offsetof(gm_t8ta482bl6be0, f0) == 0 && offsetof(gm_t8ta482bl6be0, f1) == 8, "layout of `(i: usize, x: FieldDef)`");
_Static_assert(sizeof(gm_tandvbt63uk7n) == 24 && _Alignof(gm_tandvbt63uk7n) == 8 && offsetof(gm_tandvbt63uk7n, f0) == 0 && offsetof(gm_tandvbt63uk7n, f1) == 8, "layout of `(op: SoaOp, args: []Expr)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
_Static_assert(sizeof(gm_tcf2f71ra2nps) == 16 && _Alignof(gm_tcf2f71ra2nps) == 4 && offsetof(gm_tcf2f71ra2nps, f0) == 0 && offsetof(gm_tcf2f71ra2nps, f1) == 8, "layout of `(ArrayKind, ArrayKind)`");
typedef struct { gm_t1s75i08qtptd f0; void * f1; } gm_tchr52g0ema53;
typedef struct { gm_b16_8 f0; gm_tkn42u7na3t81 f1; gm_tbci5ospk5nnn f2; } gm_tckdobunaulee;
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_tdlpv0q69lov1) == 32 && _Alignof(gm_tdlpv0q69lov1) == 8 && offsetof(gm_tdlpv0q69lov1, f0) == 0 && offsetof(gm_tdlpv0q69lov1, f1) == 24, "layout of `?ValTy`");
_Static_assert(sizeof(gm_teo9d4mncjfdu) == 24 && _Alignof(gm_teo9d4mncjfdu) == 8 && offsetof(gm_teo9d4mncjfdu, f0) == 0 && offsetof(gm_teo9d4mncjfdu, f1) == 16, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_tet9guhev5r4u) == 56 && _Alignof(gm_tet9guhev5r4u) == 8 && offsetof(gm_tet9guhev5r4u, f0) == 0 && offsetof(gm_tet9guhev5r4u, f1) == 32, "layout of `IntrinsicExpr`");
typedef struct { void * f0; gm_t1s75i08qtptd f1; void * f2; } gm_tioi3jh9v22g5;
_Static_assert(sizeof(gm_tioie2ju3q4ul) == 40 && _Alignof(gm_tioie2ju3q4ul) == 8 && offsetof(gm_tioie2ju3q4ul, f0) == 0 && offsetof(gm_tioie2ju3q4ul, f1) == 8, "layout of `(base: ^Expr, name: Ident)`");
_Static_assert(sizeof(gm_tjvm1fbs3qbum) == 32 && _Alignof(gm_tjvm1fbs3qbum) == 8 && offsetof(gm_tjvm1fbs3qbum, f0) == 0 && offsetof(gm_tjvm1fbs3qbum, f1) == 8, "layout of `(i: usize, x: SourceField)`");
_Static_assert(sizeof(gm_tm7a6rksvs6n5) == 48 && _Alignof(gm_tm7a6rksvs6n5) == 8 && offsetof(gm_tm7a6rksvs6n5, f0) == 0 && offsetof(gm_tm7a6rksvs6n5, f1) == 24 && offsetof(gm_tm7a6rksvs6n5, f2) == 32, "layout of `TuplePart`");
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; } gm_tngtuiuskn938;
_Static_assert(sizeof(gm_tpautr4br7h3l) == 48 && _Alignof(gm_tpautr4br7h3l) == 8 && offsetof(gm_tpautr4br7h3l, f0) == 0 && offsetof(gm_tpautr4br7h3l, f1) == 16 && offsetof(gm_tpautr4br7h3l, f2) == 24 && offsetof(gm_tpautr4br7h3l, f3) == 40, "layout of `StructDef`");
_Static_assert(sizeof(gm_tpouv9r7lrri2) == 32 && _Alignof(gm_tpouv9r7lrri2) == 8 && offsetof(gm_tpouv9r7lrri2, f0) == 0 && offsetof(gm_tpouv9r7lrri2, f1) == 24, "layout of `SpreadPart`");
_Static_assert(sizeof(gm_trcmmd12cefb1) == 32 && _Alignof(gm_trcmmd12cefb1) == 8 && offsetof(gm_trcmmd12cefb1, f0) == 0 && offsetof(gm_trcmmd12cefb1, f1) == 8, "layout of `Stmt`");
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t419mo45g7j0a v1; void * v2; } f1; } gm_tsth9qu1dv0g3;
_Static_assert(sizeof(gm_tt8o5kebou62r) == 32 && _Alignof(gm_tt8o5kebou62r) == 8 && offsetof(gm_tt8o5kebou62r, f0) == 0 && offsetof(gm_tt8o5kebou62r, f1) == 16, "layout of `(op: Builtin, args: []Expr)`");
_Static_assert(sizeof(gm_ttu09lij36f8c) == 40 && _Alignof(gm_ttu09lij36f8c) == 8 && offsetof(gm_ttu09lij36f8c, f0) == 0 && offsetof(gm_ttu09lij36f8c, f1) == 32, "layout of `?Split`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; void * v1; gm_tibupt4dmcqai v2; uint8_t v3; gm_t08olq97rscn3 v4; gm_t08olq97rscn3 v5; void * v6; gm_b16_8 v7; void * v8; gm_t8ghgl2jj0qha v9; gm_trl9d7m1k5lbd v10; gm_tioi3jh9v22g5 v11; void * v12; void * v13; gm_t3mf58gv9kf3n v14; void * v15; gm_tlljg61a2ctgq v16; gm_t3mf58gv9kf3n v17; gm_tao63pk3r18v5 v18; gm_tioie2ju3q4ul v19; gm_tupk62cr6lbdu v20; void * v21; gm_tgtub0v94d5u5 v22; gm_tgtub0v94d5u5 v23; gm_tgtub0v94d5u5 v24; gm_t3mf58gv9kf3n v25; gm_t3mf58gv9kf3n v26; void * v27; void * v28; void * v29; void * v30; void * v31; gm_tpe1hv3fekn05 v32; void * v33; void * v34; gm_tibupt4dmcqai v35; void * v36; void * v37; void * v38; void * v39; void * v40; gm_t79rbdv4rafe5 v41; void * v42; gm_tchr52g0ema53 v43; gm_t1s75i08qtptd v44; gm_t3mf58gv9kf3n v45; gm_tet9guhev5r4u v46; } f1; } gm_t2gtbh39ptekd;
typedef struct { uint64_t f0; gm_tckdobunaulee f1; } gm_t3l8idb2m6c1r;
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_tchr52g0ema53) == 48 && _Alignof(gm_tchr52g0ema53) == 8 && offsetof(gm_tchr52g0ema53, f0) == 0 && offsetof(gm_tchr52g0ema53, f1) == 40, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_tckdobunaulee) == 72 && _Alignof(gm_tckdobunaulee) == 8 && offsetof(gm_tckdobunaulee, f0) == 0 && offsetof(gm_tckdobunaulee, f1) == 16 && offsetof(gm_tckdobunaulee, f2) == 24, "layout of `VariantDef`");
_Static_assert(sizeof(gm_tioi3jh9v22g5) == 56 && _Alignof(gm_tioi3jh9v22g5) == 8 && offsetof(gm_tioi3jh9v22g5, f0) == 0 && offsetof(gm_tioi3jh9v22g5, f1) == 8 && offsetof(gm_tioi3jh9v22g5, f2) == 48, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_tngtuiuskn938) == 96 && _Alignof(gm_tngtuiuskn938) == 8 && offsetof(gm_tngtuiuskn938, f0) == 0 && offsetof(gm_tngtuiuskn938, f1) == 48, "layout of `Rat`");
typedef struct { gm_tsth9qu1dv0g3 f0; gm_b16_8 f1; } gm_to76jli3bqddq;
_Static_assert(sizeof(gm_tsth9qu1dv0g3) == 40 && _Alignof(gm_tsth9qu1dv0g3) == 8 && offsetof(gm_tsth9qu1dv0g3, f0) == 0 && offsetof(gm_tsth9qu1dv0g3, f1) == 8, "layout of `Callee`");
_Static_assert(sizeof(gm_t2gtbh39ptekd) == 64 && _Alignof(gm_t2gtbh39ptekd) == 8 && offsetof(gm_t2gtbh39ptekd, f0) == 0 && offsetof(gm_t2gtbh39ptekd, f1) == 8, "layout of `ExprKind`");
_Static_assert(sizeof(gm_t3l8idb2m6c1r) == 80 && _Alignof(gm_t3l8idb2m6c1r) == 8 && offsetof(gm_t3l8idb2m6c1r, f0) == 0 && offsetof(gm_t3l8idb2m6c1r, f1) == 8, "layout of `(i: usize, x: VariantDef)`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_t2gtbh39ptekd f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tc3oht4ksth9d;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; double v1; void * v2; void * v3; uint8_t v4; gm_b16_8 v5; gm_b16_8 v6; uint32_t v7; gm_tkn42u7na3t81 v8; gm_tkn42u7na3t81 v9; gm_t88rcucg1b7uk v10; gm_t3rcjibl8kkg4 v11; gm_t08olq97rscn3 v12; uint32_t v13; gm_tcc1rq4n1avb2 v14; gm_t3877pk7lv748 v15; void * v16; gm_tbl8il92l97ut v17; gm_tlcl4u558l6p5 v18; gm_tao63pk3r18v5 v19; void * v20; void * v21; void * v22; void * v23; uint32_t v24; uint32_t v25; uint32_t v26; gm_t8ghgl2jj0qha v27; gm_trl9d7m1k5lbd v28; gm_ta612fil812pm v29; gm_t8jsc4n9vp2ib v30; gm_tt8o5kebou62r v31; gm_to76jli3bqddq v32; gm_b16_8 v33; gm_b16_8 v34; void * v35; gm_ttggq7ng4kp3p v36; void * v37; gm_tsplfr33dsvji v38; gm_t3dcoc7077t1q v39; gm_t083kd7b1kkag v40; gm_tlcl4u558l6p5 v41; gm_tkaapcii86udk v42; gm_tlcl4u558l6p5 v43; void * v44; gm_trl2khmuphncd v45; gm_t710lsrq1m8gk v46; gm_teo9d4mncjfdu v47; gm_te9hrk0u35rev v48; gm_ttggq7ng4kp3p v49; void * v50; gm_t3mf58gv9kf3n v51; void * v52; void * v53; void * v54; gm_tandvbt63uk7n v55; gm_t710lsrq1m8gk v56; } f1; } gm_teti411eujvf4;
_Static_assert(sizeof(gm_to76jli3bqddq) == 56 && _Alignof(gm_to76jli3bqddq) == 8 && offsetof(gm_to76jli3bqddq, f0) == 0 && offsetof(gm_to76jli3bqddq, f1) == 40, "layout of `(callee: Callee, args: []Expr)`");
typedef struct { gm_t08olq97rscn3 f0; gm_tc3oht4ksth9d f1; } gm_t56mj5hnm2uc6;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_teti411eujvf4 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t69m56meqefs8;
_Static_assert(sizeof(gm_tc3oht4ksth9d) == 88 && _Alignof(gm_tc3oht4ksth9d) == 8 && offsetof(gm_tc3oht4ksth9d, f0) == 0 && offsetof(gm_tc3oht4ksth9d, f1) == 8 && offsetof(gm_tc3oht4ksth9d, f2) == 72, "layout of `Expr`");
_Static_assert(sizeof(gm_teti411eujvf4) == 64 && _Alignof(gm_teti411eujvf4) == 8 && offsetof(gm_teti411eujvf4, f0) == 0 && offsetof(gm_teti411eujvf4, f1) == 8, "layout of `ExprKind`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tc3oht4ksth9d v0; gm_t56mj5hnm2uc6 v1; gm_tc3oht4ksth9d v2; } f1; } gm_t22mtv3sd9prf;
_Static_assert(sizeof(gm_t56mj5hnm2uc6) == 120 && _Alignof(gm_t56mj5hnm2uc6) == 8 && offsetof(gm_t56mj5hnm2uc6, f0) == 0 && offsetof(gm_t56mj5hnm2uc6, f1) == 32, "layout of `(name: Ident, value: Expr)`");
_Static_assert(sizeof(gm_t69m56meqefs8) == 88 && _Alignof(gm_t69m56meqefs8) == 8 && offsetof(gm_t69m56meqefs8, f0) == 0 && offsetof(gm_t69m56meqefs8, f1) == 8 && offsetof(gm_t69m56meqefs8, f2) == 72, "layout of `Expr`");
_Static_assert(sizeof(gm_t22mtv3sd9prf) == 128 && _Alignof(gm_t22mtv3sd9prf) == 8 && offsetof(gm_t22mtv3sd9prf, f0) == 0 && offsetof(gm_t22mtv3sd9prf, f1) == 8, "layout of `ArgKind`");
typedef struct { gm_t22mtv3sd9prf f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t7te2tdpebbt4;
_Static_assert(sizeof(gm_t7te2tdpebbt4) == 144 && _Alignof(gm_t7te2tdpebbt4) == 8 && offsetof(gm_t7te2tdpebbt4, f0) == 0 && offsetof(gm_t7te2tdpebbt4, f1) == 128, "layout of `Arg`");
struct gm_fr_bind_temp_sa5rcbr {
    gm_frame h;
    void * ret;
};
struct gm_fr_clear_4qp22g2 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_construct_struct_ve6tb88 {
    gm_frame h;
    gm_trqfvmjtt4sij l9; /* stmts */
    gm_trqfvmjtt4sij l10; /* slots */
    gm_trqfvmjtt4sij l11; /* explicit */
    gm_trqfvmjtt4sij l14; /* written */
    gm_t7te2tdpebbt4 l18; /* arg */
    gm_tc3oht4ksth9d l19; /* e */
    gm_tc3oht4ksth9d l21; /* e */
    gm_t0mglksrkfa5e l26; /* f */
    gm_tc3oht4ksth9d l29; /* e */
    gm_t0mglksrkfa5e l46; /* f */
    gm_trqfvmjtt4sij l57; /* values */
    void * ret;
};
struct gm_fr_construct_tuple_6ee5ejf {
    gm_frame h;
    gm_trqfvmjtt4sij l6; /* slots */
    gm_t7te2tdpebbt4 l13; /* a */
    gm_trqfvmjtt4sij l14; /* stmts */
    gm_tc3oht4ksth9d l16; /* e */
    gm_t7te2tdpebbt4 l24; /* arg */
    gm_trqfvmjtt4sij l25; /* parts */
    gm_tc3oht4ksth9d l26; /* e */
    gm_tc3oht4ksth9d l28; /* e */
    gm_tc3oht4ksth9d l30; /* e */
    gm_tm7a6rksvs6n5 l37; /* part */
    gm_trqfvmjtt4sij l41; /* values */
    void * ret;
};
struct gm_fr_construct_variant_o6is81c {
    gm_frame h;
    gm_tckdobunaulee l10; /* v */
    gm_tc3oht4ksth9d l18; /* x */
    void * ret;
};
struct gm_fr_copy_pmkg67r {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_trqfvmjtt4sij l1; /* l */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_default_value_lo723gi {
    gm_frame h;
    gm_tbci5ospk5nnn l11; /* v */
    gm_tngtuiuskn938 l13; /* r */
    void * ret;
};
struct gm_fr_empty_slice_73v4ttb {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_8q3kspo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_b8btuen {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_d914i5u {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_dtfif5p {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_g3tddrb_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ose1ba9 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_s1lbe5s {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_vj3ckdj_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_error_89glhjr {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_error_9huj44a {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_error_expr_o6qt7e2 {
    gm_frame h;
    void * ret;
};
struct gm_fr_error_ki8i3k6 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_field_default_4ssmakm {
    gm_frame h;
    gm_tpautr4br7h3l l7; /* def */
    void * ret;
};
struct gm_fr_field_value_19vleud {
    gm_frame h;
    void * ret;
};
struct gm_fr_float_value_cr4h9gi {
    gm_frame h;
    gm_tngtuiuskn938 l1; /* r */
    void * ret;
};
struct gm_fr_from_int_ofjuoqh {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* i */
    gm_tngtuiuskn938 ret;
};
struct gm_fr_has_param_u9t0efs {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_error_8k27gd1 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_integer_8d4f9ek {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_plain_data_ej4mh2m {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_zero_5aept6u {
    gm_frame h;
    gm_tngtuiuskn938 l0; /* r */
    uint8_t ret;
};
struct gm_fr_is_zero_8tcct9o {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    uint8_t ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_list_5dhgg4i {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_5kthfl8 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_7gsrufb {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_go2qg0b {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_ir0h3tc_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_pmp06kq {
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
struct gm_fr_list_vp1djv1_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_mk_9bc97fd {
    gm_frame h;
    gm_teti411eujvf4 l1; /* kind */
    void * ret;
};
struct gm_fr_neg_0u9uqg6 {
    gm_frame h;
    gm_tngtuiuskn938 l0; /* r */
    gm_tngtuiuskn938 ret;
};
struct gm_fr_neg_agd5c5k {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* r */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_one_08qg1op {
    gm_frame h;
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_report_eardf07 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_show_8uijkbs {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_show_fsnl6fa {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_show_kmjeh8s {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* out */
    gm_b16_8 ret;
};
struct gm_fr_spread_source_fields_5csral7 {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* out */
    gm_t0mglksrkfa5e l13; /* f */
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_strip_wrappers_e5thpib {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_temp_ilbvj8q {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_template_9o92cvv {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_zero_s25l990 {
    gm_frame h;
    gm_tbci5ospk5nnn ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b);
inline bool gm_eqb2ul45tidi(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) void * gm_f_bind_temp_sa5rcbr(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_bits_struct_ty_l1kjtap(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void * gm_f_block_typed_e7svg7n(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, gm_b16_8 a1, void * a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void * gm_f_check_const_expr_2ot4q9v(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_tkn42u7na3t81 a3);
inline __attribute__((aligned(4))) void gm_f_clear_4qp22g2(void * a0);
__attribute__((aligned(4))) void * gm_f_coerce_sudc6c1(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void * gm_f_construct_struct_ve6tb88(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void * gm_f_construct_tuple_6ee5ejf(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_b16_8 a3, gm_b16_8 a4, gm_tl874t2atnlh3 a5);
__attribute__((aligned(4))) void * gm_f_construct_variant_o6is81c(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, const void * a2, const void * a3, gm_tl874t2atnlh3 a4);
inline __attribute__((aligned(4))) void gm_f_copy_pmkg67r(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) uint8_t gm_f_default_const_k60bpu4(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_t88rcucg1b7uk a3);
__attribute__((aligned(4))) void * gm_f_default_value_lo723gi(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_t88rcucg1b7uk a3);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_73v4ttb(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8q3kspo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_b8btuen(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d914i5u(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_dtfif5p(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g3tddrb_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ose1ba9(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_s1lbe5s(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vj3ckdj_2(void);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_89glhjr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_9huj44a(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void * gm_f_error_expr_o6qt7e2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_error_ki8i3k6(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void * gm_f_expr_vmp704u(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tkn42u7na3t81 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_7hs67uj(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void * gm_f_field_default_4ssmakm(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint64_t a2, gm_t88rcucg1b7uk a3);
__attribute__((aligned(4))) void * gm_f_field_default_expr_tqfuiff(void * a0, uint32_t a1, uint64_t a2);
__attribute__((aligned(4))) void * gm_f_field_value_19vleud(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void * gm_f_float_value_cr4h9gi(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t88rcucg1b7uk a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void gm_f_format_0o67o8r(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_12rbmhd(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_1e409ou(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_3tnlp8q(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_6fdilsr(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_6nmb3uf(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_843cvhd(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_9i81nmj(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_9tg469g(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_a07tfib(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_adtncrl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_bbsjt91(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_bm4rhr0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_e1r9br2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_j4le8o0(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_ktbu9b9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_l002mid(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_lc07prd(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_s1oodgm(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_t8pf89n(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_from_int_ofjuoqh(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_from_u64_ud13q5v(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0);
inline __attribute__((aligned(4))) uint8_t gm_f_has_param_u9t0efs(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_hidden_field_u7uao2s(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_index_of_o9r57ha(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_intern_ia19mhl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_error_8k27gd1(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_integer_8d4f9ek(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_plain_data_ej4mh2m(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) uint8_t gm_f_is_plain_n60iihm(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_zero_5aept6u(const void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_zero_8tcct9o(const void * a0);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_lift_temps_7c6gk6l(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
inline __attribute__((aligned(4))) void gm_f_list_5dhgg4i(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_5kthfl8(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_7gsrufb(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_go2qg0b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_ir0h3tc_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_pmp06kq(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_s83jn2q(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_vp1djv1_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void * gm_f_mk_9bc97fd(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2);
inline __attribute__((aligned(4))) void gm_f_neg_0u9uqg6(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_neg_agd5c5k(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) uint32_t gm_f_new_local_003h9es(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_t88rcucg1b7uk a2, uint8_t a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_ve8bb01_2(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_one_08qg1op(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_9a0n4ld(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_d6c76ud_2(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_d943vgp_2(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_lnebar3(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_r2k6rjf(void * a0, void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_ucgmlo2(void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_vd1290p(void * a0, uint8_t a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_t2kal61(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_8uijkbs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_fsnl6fa(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void * gm_f_soa_construct_8nun9tl(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, const void * a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void gm_f_soa_info_pq4q43l(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_spread_parts_e62q2l2(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) void gm_f_spread_source_fields_5csral7(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, void * a2);
__attribute__((aligned(4))) gm_csguankko4lmhd gm_f_strip_wrappers_e5thpib(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_struct_fields_chqu3it(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint32_t gm_f_temp_ilbvj8q(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_tl874t2atnlh3 a2);
inline __attribute__((aligned(4))) uint32_t gm_f_template_9o92cvv(void * a0, uint32_t a1);
__attribute__((aligned(4))) double gm_f_to_float_o1iop8t(void *env, gm_node *cx, gm_node *inst, const void * a0, uint16_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_uninit_message_u2c3peu(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_union_variants_a7khaqp(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_zero_s25l990(void *gm_out, void *env, gm_node *cx, gm_node *inst);
extern uint8_t gm_k3tttsr3hp1[40] __attribute__((aligned(8)));
extern const uint8_t gm_sfdbfrv46lihd[2];
extern const uint8_t gm_shv1k5m05egbe[6];
extern const uint8_t gm_sna4vk4o059d0[10];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src7nt72u848oha[];
extern const char gm_srclv3pj5es0f8o[];
extern const char gm_srcod98slk3vnra[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_srrqh3pev0igg[9];
extern const uint8_t gm_sto8li44rv2ou[8];
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
inline bool gm_eqb2ul45tidi(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    if (ta == (uint8_t)1) return gm_eq9m6mvtmgde(a + 4, b + 4);
    return true;
}
inline __attribute__((aligned(4))) void gm_f_clear_4qp22g2(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_clear_4qp22g2 F;
    void * gm_l0; /* l */
    GM_STACK_CHECK_SHALLOW(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        void * t3 = gm_l0;
        uint64_t t4 = GM_LD(uintptr_t, (((uint8_t *)t3) + 0) + 8);
        void * t5 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t3) + 0)));
        uint64_t t6 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t7 = (uintptr_t)((uint64_t)0ULL);
        if (t6 > t7 || t7 > (uintptr_t)t4) gm_trap_slice(t6, t7, t4, gm_srcrgc9bkquf6nr, 137, 15);
        gm_b16_8 t8;
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 8)));
        GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(t7 - t6));
        GM_ST(gm_b16_8, (((uint8_t *)t2) + 0), t8);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8q3kspo(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_8q3kspo F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_b8btuen(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_b8btuen F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_dtfif5p(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_dtfif5p F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g3tddrb_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_g3tddrb_2 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vj3ckdj_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_vj3ckdj_2 F;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_9huj44a(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2) {
    struct gm_fr_error_9huj44a F;
    void * gm_l0; /* cx */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* msg */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        gm_tl874t2atnlh3 t4 = gm_l1;
        gm_b16_8 t5 = gm_l2;
        gm_b0_1 t6 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t3, t4, t5);
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
inline __attribute__((aligned(4))) void * gm_f_error_expr_o6qt7e2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1) {
    struct gm_fr_error_expr_o6qt7e2 F;
    void * gm_l0; /* s */
    gm_tl874t2atnlh3 gm_l1; /* span */
    GM_STACK_CHECK(&F, 156u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = (void *)(((uint8_t *)t1) + 8);
    gm_t88rcucg1b7uk t3 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t4 = gm_f_empty_slice_g3tddrb_2();
    gm_teti411eujvf4 t5;
    memset(&t5, 0, sizeof t5);
    GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)35ULL));
    GM_ST(gm_b16_8, ((uint8_t *)&t5 + 8), t4);
    gm_tl874t2atnlh3 t6 = gm_l1;
    void * t7 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t3, &t5, t6);
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_plain_data_ej4mh2m(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_is_plain_data_ej4mh2m F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 65u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t t1 = 0;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
    void * t4 = (void *)(((uint8_t *)t3) + 8);
    gm_t88rcucg1b7uk t5 = gm_l1;
    uint8_t t6 = gm_f_is_error_8k27gd1(t4, GM_CONV(gm_csguankko4lmhd, t5));
    if (!t6) {
        void * t7 = gm_l0;
        void * t8 = GM_LD(void *, (((uint8_t *)t7) + 0));
        gm_t88rcucg1b7uk t9 = gm_l1;
        uint8_t t10 = gm_f_is_plain_n60iihm(t8, GM_CONV(gm_csguankko4lmhd, t9));
        t1 = t10;
    } else t1 = 1;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_zero_5aept6u(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_zero_5aept6u F;
    GM_STACK_CHECK_SHALLOW(&F, 3u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint8_t t1 = gm_f_is_zero_8tcct9o(&(*(gm_tbci5ospk5nnn *)(((uint8_t *)a0) + 0)));
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
inline __attribute__((aligned(4))) void gm_f_list_5kthfl8(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_5kthfl8 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_b8btuen();
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
inline __attribute__((aligned(4))) void gm_f_list_7gsrufb(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_7gsrufb F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_dtfif5p();
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
inline __attribute__((aligned(4))) void gm_f_list_ir0h3tc_2(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_ir0h3tc_2 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_vj3ckdj_2();
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
inline __attribute__((aligned(4))) void gm_f_list_pmp06kq(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_pmp06kq F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_8q3kspo();
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
inline __attribute__((aligned(4))) void gm_f_list_vp1djv1_2(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_vp1djv1_2 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_g3tddrb_2();
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
inline __attribute__((aligned(4))) void * gm_f_mk_9bc97fd(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_mk_9bc97fd F;
    gm_t88rcucg1b7uk gm_l0; /* ty */
    gm_tl874t2atnlh3 gm_l2; /* span */
    GM_STACK_CHECK(&F, 160u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    gm_l2 = a2;
    gm_t69m56meqefs8 t1;
    memset((uint8_t *)&t1 + 4, 0, 4);
    memset((uint8_t *)&t1 + 84, 0, 4);
    gm_t88rcucg1b7uk t2 = gm_l0;
    GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t1 + 0, t2);
    memcpy((uint8_t *)&t1 + 8, ((uint8_t *)&F.l1), 64);
    gm_tl874t2atnlh3 t3 = gm_l2;
    GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t1 + 72, t3);
    gm_tq7s98ihonkju t4 = gm_f_new_ve8bb01_2((void *)NULL, cx, NULL, &t1, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k3tttsr3hp1)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (((((uint8_t *)&t4))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t4) + 8, gm_srcod98slk3vnra, 558, 81);
    void * t5 = GM_LD(void *, (((uint8_t *)&t4) + 8));
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
inline __attribute__((aligned(4))) void gm_f_neg_0u9uqg6(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_neg_0u9uqg6 F;
    GM_STACK_CHECK(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    gm_tbci5ospk5nnn t1;
    gm_f_neg_agd5c5k((void *)&t1, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)(((uint8_t *)&F.l0) + 0)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tbci5ospk5nnn t2 = GM_LD(gm_tbci5ospk5nnn, (((uint8_t *)&F.l0) + 48));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_fsnl6fa(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_show_fsnl6fa F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_b16_8 t4 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t2, t3);
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
inline __attribute__((aligned(4))) uint32_t gm_f_temp_ilbvj8q(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_temp_ilbvj8q F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    gm_tl874t2atnlh3 gm_l2; /* span */
    GM_STACK_CHECK(&F, 76u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t1 = gm_l0;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_shv1k5m05egbe));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(5));
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_tl874t2atnlh3 t4 = gm_l2;
    uint32_t t5 = gm_f_new_local_003h9es((void *)NULL, cx, NULL, t1, t2, t3, ((uint8_t)1), t4);
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
inline __attribute__((aligned(4))) void * gm_f_bind_temp_sa5rcbr(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2) {
    struct gm_fr_bind_temp_sa5rcbr F;
    void * gm_l0; /* cx */
    void * gm_l1; /* v */
    void * gm_l2; /* stmts */
    uint32_t gm_l3; /* t */
    GM_STACK_CHECK(&F, 318u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t3 = gm_l1;
        uint8_t t4;
        uint8_t t16 = GM_LD(uint8_t, ((((uint8_t *)t3) + 8) + 0));
        switch (t16) { case ((uint8_t)0ULL): goto ma6; case ((uint8_t)1ULL): goto ma7; case ((uint8_t)2ULL): goto ma8; case ((uint8_t)3ULL): goto ma14; case ((uint8_t)4ULL): goto ma9; case ((uint8_t)5ULL): goto ma10; case ((uint8_t)6ULL): goto ma14; case ((uint8_t)7ULL): goto ma11; case ((uint8_t)8ULL): goto ma12; case ((uint8_t)9ULL): goto ma14; case ((uint8_t)10ULL): goto ma14; case ((uint8_t)11ULL): goto ma14; case ((uint8_t)12ULL): goto ma14; case ((uint8_t)13ULL): goto ma14; case ((uint8_t)14ULL): goto ma13; case ((uint8_t)15ULL): goto ma14; case ((uint8_t)16ULL): goto ma14; case ((uint8_t)17ULL): goto ma14; case ((uint8_t)18ULL): goto ma14; case ((uint8_t)19ULL): goto ma14; case ((uint8_t)20ULL): goto ma14; case ((uint8_t)21ULL): goto ma14; case ((uint8_t)22ULL): goto ma14; case ((uint8_t)23ULL): goto ma14; case ((uint8_t)24ULL): goto ma14; case ((uint8_t)25ULL): goto ma14; case ((uint8_t)26ULL): goto ma14; case ((uint8_t)27ULL): goto ma14; case ((uint8_t)28ULL): goto ma14; case ((uint8_t)29ULL): goto ma14; case ((uint8_t)30ULL): goto ma14; case ((uint8_t)31ULL): goto ma14; case ((uint8_t)32ULL): goto ma14; case ((uint8_t)33ULL): goto ma14; case ((uint8_t)34ULL): goto ma14; case ((uint8_t)35ULL): goto ma14; case ((uint8_t)36ULL): goto ma14; case ((uint8_t)37ULL): goto ma14; case ((uint8_t)38ULL): goto ma14; case ((uint8_t)39ULL): goto ma14; case ((uint8_t)40ULL): goto ma14; case ((uint8_t)41ULL): goto ma14; case ((uint8_t)42ULL): goto ma14; case ((uint8_t)43ULL): goto ma14; case ((uint8_t)44ULL): goto ma14; case ((uint8_t)45ULL): goto ma14; case ((uint8_t)46ULL): goto ma14; case ((uint8_t)47ULL): goto ma14; case ((uint8_t)48ULL): goto ma14; case ((uint8_t)49ULL): goto ma14; case ((uint8_t)50ULL): goto ma14; case ((uint8_t)51ULL): goto ma14; case ((uint8_t)52ULL): goto ma14; case ((uint8_t)53ULL): goto ma14; case ((uint8_t)54ULL): goto ma14; case ((uint8_t)55ULL): goto ma14; case ((uint8_t)56ULL): goto ma14; case ((uint8_t)57ULL): goto ma14; case ((uint8_t)58ULL): goto ma14; default: goto ma14; }
ma6:;
        uint8_t t18 = GM_LD(uint8_t, ((((uint8_t *)t3) + 8) + 0));
        if (t18 != ((uint8_t)0ULL)) goto mn17;
        t4 = ((uint8_t)1);
        goto me5;
mn17:;
ma7:;
        uint8_t t20 = GM_LD(uint8_t, ((((uint8_t *)t3) + 8) + 0));
        if (t20 != ((uint8_t)1ULL)) goto mn19;
        t4 = ((uint8_t)1);
        goto me5;
mn19:;
ma8:;
        uint8_t t22 = GM_LD(uint8_t, ((((uint8_t *)t3) + 8) + 0));
        if (t22 != ((uint8_t)2ULL)) goto mn21;
        t4 = ((uint8_t)1);
        goto me5;
mn21:;
ma9:;
        uint8_t t24 = GM_LD(uint8_t, ((((uint8_t *)t3) + 8) + 0));
        if (t24 != ((uint8_t)4ULL)) goto mn23;
        t4 = ((uint8_t)1);
        goto me5;
mn23:;
ma10:;
        uint8_t t26 = GM_LD(uint8_t, ((((uint8_t *)t3) + 8) + 0));
        if (t26 != ((uint8_t)5ULL)) goto mn25;
        t4 = ((uint8_t)1);
        goto me5;
mn25:;
ma11:;
        uint8_t t28 = GM_LD(uint8_t, ((((uint8_t *)t3) + 8) + 0));
        if (t28 != ((uint8_t)7ULL)) goto mn27;
        t4 = ((uint8_t)1);
        goto me5;
mn27:;
ma12:;
        uint8_t t30 = GM_LD(uint8_t, ((((uint8_t *)t3) + 8) + 0));
        if (t30 != ((uint8_t)8ULL)) goto mn29;
        t4 = ((uint8_t)1);
        goto me5;
mn29:;
ma13:;
        uint8_t t32 = GM_LD(uint8_t, ((((uint8_t *)t3) + 8) + 0));
        if (t32 != ((uint8_t)14ULL)) goto mn31;
        t4 = ((uint8_t)1);
        goto me5;
mn31:;
ma14:;
        t4 = ((uint8_t)0);
        goto me5;
mn33:;
ma15:;
        gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 379, 8);
me5:;
        if (!t4) goto ie2;
        {
            void * t35 = gm_l1;
            uint8_t t36 = 0;
            F.ret = t35;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t37 = gm_l0;
        void * t38 = gm_l1;
        void * t39 = gm_l2;
        gm_b0_1 t40 = gm_f_lift_temps_7c6gk6l((void *)NULL, cx, NULL, t37, t38, t39);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t41 = gm_l0;
        void * t42 = gm_l1;
        gm_t88rcucg1b7uk t43 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t42) + 0));
        void * t44 = gm_l1;
        gm_tl874t2atnlh3 t45 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t44) + 72));
        uint32_t t46 = gm_f_temp_ilbvj8q((void *)NULL, cx, NULL, t41, t43, t45);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l3 = t46;
    }
    {
        void * t47 = gm_l2;
        uint32_t t48 = gm_l3;
        void * t49 = gm_l1;
        gm_ttggq7ng4kp3p t50;
        GM_ST(uint32_t, (uint8_t *)&t50 + 4, 0);
        GM_ST(uint32_t, ((uint8_t *)&t50 + 0), t48);
        GM_ST(void *, ((uint8_t *)&t50 + 8), t49);
        gm_trcmmd12cefb1 t51;
        GM_ST(uint8_t, (uint8_t *)&t51 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t51 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t51 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t51 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t51 + 0), ((uint8_t)0ULL));
        GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t51 + 8), t50);
        gm_tftb1odc739t5 t52 = gm_f_push_d6c76ud_2(t47, &t51);
        if (((((uint8_t *)&t52))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t52) + 1, gm_src7nt72u848oha, 384, 5);
        gm_b0_1 t53 = GM_LD(gm_b0_1, (((uint8_t *)&t52) + 1));
    }
    {
        void * t54 = gm_l1;
        gm_t88rcucg1b7uk t55 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t54) + 0));
        uint32_t t56 = gm_l3;
        gm_teti411eujvf4 t57;
        memset(&t57, 0, sizeof t57);
        GM_ST(uint8_t, ((uint8_t *)&t57 + 0), ((uint8_t)15ULL));
        GM_ST(uint32_t, ((uint8_t *)&t57 + 8), t56);
        void * t58 = gm_l1;
        gm_tl874t2atnlh3 t59 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t58) + 72));
        void * t60 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t55, &t57, t59);
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
__attribute__((aligned(4))) void * gm_f_construct_struct_ve6tb88(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_construct_struct_ve6tb88 F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    gm_b16_8 gm_l2; /* args */
    gm_tl874t2atnlh3 gm_l3; /* span */
    uint32_t gm_l4; /* id */
    gm_tjpqvfd72o62u gm_l5; /* info */
    gm_t6j6smj7qoctu gm_l6; /* h */
    gm_b16_8 gm_l7; /* fields */
    uint64_t gm_l8; /* n */
    uint64_t gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    uint8_t gm_l15; /* bad_args */
    gm_b16_8 gm_l16; /* <tmp> */
    uint64_t gm_l17; /* <tmp> */
    gm_t08olq97rscn3 gm_l20; /* fname */
    gm_tf5srh4mp4b3i gm_l22; /* found */
    gm_b16_8 gm_l23; /* <tmp> */
    uint64_t gm_l24; /* <tmp> */
    uint64_t gm_l25; /* i */
    uint64_t gm_l27; /* i */
    void * gm_l28; /* v */
    void * gm_l30; /* v */
    gm_t88rcucg1b7uk gm_l31; /* vt */
    gm_b16_8 gm_l32; /* src */
    gm_b16_8 gm_l33; /* <tmp> */
    uint64_t gm_l34; /* <tmp> */
    uint64_t gm_l35; /* w */
    void * gm_l36; /* x */
    void * gm_l37; /* base */
    gm_b16_8 gm_l38; /* <tmp> */
    uint64_t gm_l39; /* <tmp> */
    uint64_t gm_l40; /* k */
    gm_tep2drsl9408b gm_l41; /* sf */
    gm_tf5srh4mp4b3i gm_l42; /* found */
    gm_b16_8 gm_l43; /* <tmp> */
    uint64_t gm_l44; /* <tmp> */
    uint64_t gm_l45; /* i */
    uint64_t gm_l47; /* i */
    void * gm_l48; /* proj */
    uint8_t gm_l49; /* out_of_order */
    uint64_t gm_l50; /* <tmp> */
    uint64_t gm_l51; /* <tmp> */
    uint64_t gm_l52; /* k */
    gm_b16_8 gm_l53; /* <tmp> */
    uint64_t gm_l54; /* <tmp> */
    uint64_t gm_l55; /* w */
    void * gm_l56; /* v */
    uint64_t gm_l58; /* <tmp> */
    uint64_t gm_l59; /* <tmp> */
    uint64_t gm_l60; /* i */
    void * gm_l61; /* v */
    gm_t88rcucg1b7uk gm_l62; /* ft */
    void * gm_l63; /* d */
    void * gm_l64; /* agg */
    GM_STACK_CHECK(&F, 5993u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t3 = gm_l0;
        void * t4 = GM_LD(void *, (((uint8_t *)t3) + 0));
        void * t5 = (void *)(((uint8_t *)t4) + 8);
        gm_t88rcucg1b7uk t6 = gm_l1;
        gm_t53at80pfdus3 t7;
        gm_f_kind_jcnt4hm((void *)&t7, t5, GM_CONV(gm_csguankko4lmhd, t6));
        uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        if (t8 != ((uint8_t)17ULL)) goto ie2;
        uint32_t t9 = GM_LD(uint32_t, ((((uint8_t *)&t7) + 8) + 0));
        gm_l4 = t9;
        {
            void * t12 = gm_l0;
            void * t13 = GM_LD(void *, (((uint8_t *)t12) + 0));
            gm_t88rcucg1b7uk t14 = gm_l1;
            gm_t8cc1mtbrhlpt t15;
            gm_f_soa_info_pq4q43l((void *)&t15, t13, GM_CONV(gm_csguankko4lmhd, t14));
            if (!((((uint8_t *)&t15))[32] != 0)) goto ie11;
            gm_l5 = (*(gm_tjpqvfd72o62u *)(((uint8_t *)&t15) + 0));
            {
                void * t17 = gm_l0;
                gm_t88rcucg1b7uk t18 = gm_l1;
                gm_b16_8 t19 = gm_l2;
                gm_tl874t2atnlh3 t20 = gm_l3;
                void * t21 = gm_f_soa_construct_8nun9tl((void *)NULL, cx, NULL, t17, t18, &(*(gm_tjpqvfd72o62u *)((uint8_t *)&gm_l5)), t19, t20);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t22 = 0;
                F.ret = t21;
                goto gm_fret;
            }
            goto ix11;
ie11:;
ix11:;
        }
        {
            void * t24 = gm_l0;
            gm_t88rcucg1b7uk t25 = gm_l1;
            gm_ttu09lij36f8c t26;
            gm_f_hidden_field_u7uao2s((void *)&t26, t24, GM_CONV(gm_csguankko4lmhd, t25));
            if (!((((uint8_t *)&t26))[32] != 0)) goto ie23;
            gm_l6 = (*(gm_t6j6smj7qoctu *)(((uint8_t *)&t26) + 0));
            {
                void * t28 = gm_l0;
                gm_tl874t2atnlh3 t29 = gm_l3;
                gm_tfeab279c16rr t30;
                void * t31 = gm_l0;
                gm_t88rcucg1b7uk t32 = gm_l1;
                gm_b16_8 t33 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t31, t32);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t30 + 0, t33);
                gm_b16_8 t34 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l6) + 16));
                GM_ST(gm_b16_8, (uint8_t *)&t30 + 16, t34);
                gm_b16_8 t35 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l6) + 0));
                GM_ST(gm_b16_8, (uint8_t *)&t30 + 32, t35);
                gm_tabb710d9mq2e t36;
                gm_f_format_0o67o8r((void *)&t36, (void *)NULL, cx, NULL, &t30);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t36))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t36) + 8, gm_src7nt72u848oha, 194, 24);
                gm_b16_8 t37 = GM_LD(gm_b16_8, (((uint8_t *)&t36) + 8));
                gm_b16_8 t38 = GM_LD(gm_b16_8, ((uint8_t *)&t37));
                gm_b0_1 t39 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t28, t29, t38);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t40 = gm_l0;
                void * t41 = GM_LD(void *, (((uint8_t *)t40) + 0));
                gm_tl874t2atnlh3 t42 = gm_l3;
                void * t43 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t41, t42);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t44 = 0;
                F.ret = t43;
                goto gm_fret;
            }
            goto ix23;
ie23:;
ix23:;
        }
        {
            uint32_t t46 = gm_l4;
            void * t47 = gm_l0;
            void * t48 = GM_LD(void *, (((uint8_t *)t47) + 0));
            gm_b16_8 t49 = GM_LD(gm_b16_8, ((((uint8_t *)t48) + 1728) + 0));
            uint8_t t50 = 0;
            for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t49) + 8)); i++) if (gm_eq467d5cm1d5(((uint8_t *)GM_LD(void *, ((uint8_t *)&t49))) + i * 4, ((uint8_t *)&t46))) { t50 = 1; break; }
            uint8_t t51 = (uint8_t)(t50);
            if (!t51) goto ie45;
            {
                void * t53 = gm_l0;
                gm_tl874t2atnlh3 t54 = gm_l3;
                void * t55 = gm_l0;
                gm_t88rcucg1b7uk t56 = gm_l1;
                gm_b16_8 t57 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t55, t56);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tnpifv20irnph t58;
                GM_ST(gm_b16_8, ((uint8_t *)&t58 + 0), t57);
                gm_tabb710d9mq2e t59;
                gm_f_format_s1oodgm((void *)&t59, (void *)NULL, cx, NULL, t58);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t59))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t59) + 8, gm_src7nt72u848oha, 200, 24);
                gm_b16_8 t60 = GM_LD(gm_b16_8, (((uint8_t *)&t59) + 8));
                gm_b16_8 t61 = GM_LD(gm_b16_8, ((uint8_t *)&t60));
                gm_b0_1 t62 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t53, t54, t61);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t63 = gm_l0;
                void * t64 = GM_LD(void *, (((uint8_t *)t63) + 0));
                gm_tl874t2atnlh3 t65 = gm_l3;
                void * t66 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t64, t65);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t67 = 0;
                F.ret = t66;
                goto gm_fret;
            }
            goto ix45;
ie45:;
ix45:;
        }
        {
            void * t68 = gm_l0;
            void * t69 = GM_LD(void *, (((uint8_t *)t68) + 0));
            gm_t88rcucg1b7uk t70 = gm_l1;
            gm_th2lqbo77pvsc t71;
            gm_f_struct_fields_chqu3it((void *)&t71, t69, GM_CONV(gm_csguankko4lmhd, t70));
            gm_b16_8 t72;
            if (((((uint8_t *)&t71))[16] != 0)) {
                gm_b16_8 t73 = GM_LD(gm_b16_8, (((uint8_t *)&t71) + 0));
                t72 = t73;
            } else {
                gm_b16_8 t74 = gm_f_empty_slice_ose1ba9();
                t72 = t74;
            }
            gm_l7 = t72;
        }
        {
            uint64_t t75 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
            gm_l8 = t75;
        }
        {
            gm_f_list_ir0h3tc_2((void *)&F.l9, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_f_list_pmp06kq((void *)&F.l10, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_f_list_s83jn2q((void *)&F.l11, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_l13 = ((uint64_t)0ULL);
            }
            {
                uint64_t t77 = gm_l8;
                gm_l12 = t77;
            }
            {
                for (;;) {
                    uint64_t t79 = gm_l13;
                    uint64_t t80 = gm_l12;
                    uint8_t t81 = (t79 < t80);
                    if (!t81) goto lb78;
                    {
                        uint64_t t84 = gm_l13;
                        {
                            void * t88 = (void *)((uint8_t *)&F.l10);
                            gm_tftb1odc739t5 t89 = gm_f_push_r2k6rjf(t88, ((void *)0));
                            if (((((uint8_t *)&t89))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t89) + 1, gm_src7nt72u848oha, 209, 9);
                            gm_b0_1 t90 = GM_LD(gm_b0_1, (((uint8_t *)&t89) + 1));
                        }
                        {
                            void * t91 = (void *)((uint8_t *)&F.l11);
                            gm_tftb1odc739t5 t92 = gm_f_push_vd1290p(t91, ((uint8_t)0));
                            if (((((uint8_t *)&t92))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t92) + 1, gm_src7nt72u848oha, 210, 9);
                            gm_b0_1 t93 = GM_LD(gm_b0_1, (((uint8_t *)&t92) + 1));
                        }
                        goto me85;
mn86:;
                        gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 208, 5);
me85:;
                    }
lc78:;
                    {
                        uint64_t t95 = gm_l13;
                        uint64_t r96; bool o97 = __builtin_add_overflow(t95, ((uint64_t)1ULL), &r96);
                        uint64_t t98 = r96;
                        gm_l13 = t98;
                    }
                }
lb78:;
            }
        }
        {
            gm_f_list_5dhgg4i((void *)&F.l14, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_l15 = ((uint8_t)0);
        }
        {
            {
                gm_b16_8 t100 = gm_l2;
                gm_l16 = t100;
            }
            {
                gm_l17 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t102 = gm_l17;
                    uint64_t t103 = GM_LD(uintptr_t, ((uint8_t *)&gm_l16) + 8);
                    uint8_t t104 = (t102 < t103);
                    if (!t104) goto lb101;
                    {
                        uint64_t t107 = gm_l17;
                        uint64_t t108 = (uintptr_t)t107;
                        uint64_t t109 = GM_LD(uintptr_t, ((uint8_t *)&gm_l16) + 8);
                        gm_t7te2tdpebbt4 t112 = GM_LD(gm_t7te2tdpebbt4, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l16)) + t108 * 144));
                        F.l18 = t112;
                        {
                            uint8_t t119 = GM_LD(uint8_t, ((((uint8_t *)&F.l18) + 0) + 0));
                            switch (t119) { case ((uint8_t)0ULL): goto ma115; case ((uint8_t)1ULL): goto ma116; case ((uint8_t)2ULL): goto ma117; default: goto ma118; }
ma115:;
                            uint8_t t121 = GM_LD(uint8_t, ((((uint8_t *)&F.l18) + 0) + 0));
                            if (t121 != ((uint8_t)0ULL)) goto mn120;
                            gm_tc3oht4ksth9d t122 = GM_LD(gm_tc3oht4ksth9d, ((((uint8_t *)&F.l18) + 0) + 8));
                            F.l19 = t122;
                            {
                                uint8_t t125 = gm_l15;
                                uint8_t t126 = (uint8_t)!t125;
                                if (!t126) goto ie124;
                                {
                                    void * t128 = gm_l0;
                                    gm_tl874t2atnlh3 t129 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l19) + 72));
                                    gm_t6j6smj7qoctu t130;
                                    void * t131 = gm_l0;
                                    gm_t88rcucg1b7uk t132 = gm_l1;
                                    gm_b16_8 t133 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t131, t132);
                                    if (gm_unw.kind) {
                                        goto gm_ll105;
                                    }
                                    GM_ST(gm_b16_8, (uint8_t *)&t130 + 0, t133);
                                    void * t134 = gm_l0;
                                    gm_t88rcucg1b7uk t135 = gm_l1;
                                    gm_b16_8 t136 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t134, t135);
                                    if (gm_unw.kind) {
                                        goto gm_ll105;
                                    }
                                    GM_ST(gm_b16_8, (uint8_t *)&t130 + 16, t136);
                                    gm_tabb710d9mq2e t137;
                                    gm_f_format_1e409ou((void *)&t137, (void *)NULL, cx, NULL, &t130);
                                    if (gm_unw.kind) {
                                        goto gm_ll105;
                                    }
                                    if (((((uint8_t *)&t137))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t137) + 8, gm_src7nt72u848oha, 218, 38);
                                    gm_b16_8 t138 = GM_LD(gm_b16_8, (((uint8_t *)&t137) + 8));
                                    gm_b16_8 t139 = GM_LD(gm_b16_8, ((uint8_t *)&t138));
                                    gm_b0_1 t140 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t128, t129, t139);
                                    if (gm_unw.kind) {
                                        goto gm_ll105;
                                    }
                                }
                                goto ix124;
ie124:;
ix124:;
                            }
                            {
                                gm_l15 = ((uint8_t)1);
                            }
                            goto me114;
mn120:;
ma116:;
                            uint8_t t142 = GM_LD(uint8_t, ((((uint8_t *)&F.l18) + 0) + 0));
                            if (t142 != ((uint8_t)1ULL)) goto mn141;
                            gm_t08olq97rscn3 t143 = GM_LD(gm_t08olq97rscn3, (((((uint8_t *)&F.l18) + 0) + 8) + 0));
                            gm_l20 = t143;
                            gm_tc3oht4ksth9d t144 = GM_LD(gm_tc3oht4ksth9d, (((((uint8_t *)&F.l18) + 0) + 8) + 32));
                            F.l21 = t144;
                            {
                                gm_tf5srh4mp4b3i t146;
                                memset(&t146, 0, sizeof t146);
                                gm_l22 = t146;
                            }
                            {
                                {
                                    gm_b16_8 t148 = gm_l7;
                                    gm_b16_8 t149 = GM_LD(gm_b16_8, ((uint8_t *)&t148));
                                    gm_l23 = t149;
                                }
                                {
                                    gm_l24 = ((uint64_t)0ULL);
                                }
                                {
                                    for (;;) {
                                        uint64_t t151 = gm_l24;
                                        uint64_t t152 = GM_LD(uintptr_t, ((uint8_t *)&gm_l23) + 8);
                                        uint8_t t153 = (t151 < t152);
                                        if (!t153) goto lb150;
                                        {
                                            gm_t8ta482bl6be0 t156;
                                            uint64_t t157 = gm_l24;
                                            GM_ST(uint64_t, (uint8_t *)&t156 + 0, t157);
                                            uint64_t t158 = gm_l24;
                                            uint64_t t159 = (uintptr_t)t158;
                                            uint64_t t160 = GM_LD(uintptr_t, ((uint8_t *)&gm_l23) + 8);
                                            memcpy((uint8_t *)&t156 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l23)) + t159 * 40), 40);
                                            uint64_t t163 = GM_LD(uint64_t, (((uint8_t *)&t156) + 0));
                                            gm_l25 = t163;
                                            F.l26 = (*(gm_t0mglksrkfa5e *)(((uint8_t *)&t156) + 8));
                                            {
                                                gm_b16_8 t166 = GM_LD(gm_b16_8, (((uint8_t *)&F.l26) + 0));
                                                gm_b16_8 t167 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l20) + 0));
                                                uint8_t t168 = (gm_eq0iqiubfm7e(((uint8_t *)&t166), ((uint8_t *)&t167)));
                                                if (!t168) goto ie165;
                                                {
                                                    uint64_t t170 = gm_l25;
                                                    gm_tf5srh4mp4b3i t171;
                                                    GM_ST(uint8_t, (uint8_t *)&t171 + 9, 0);
                                                    GM_ST(uint16_t, (uint8_t *)&t171 + 10, 0);
                                                    GM_ST(uint32_t, (uint8_t *)&t171 + 12, 0);
                                                    GM_ST(uint64_t, ((uint8_t *)&t171), t170);
                                                    ((uint8_t *)&t171)[8] = 1;
                                                    gm_l22 = t171;
                                                }
                                                {
                                                    goto lb150;
                                                }
                                                goto ix165;
ie165:;
ix165:;
                                            }
                                            goto me161;
mn162:;
                                            gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 224, 17);
me161:;
                                        }
lc150:;
                                        {
                                            uint64_t t173 = gm_l24;
                                            uint64_t r174; bool o175 = __builtin_add_overflow(t173, ((uint64_t)1ULL), &r174);
                                            uint64_t t176 = r174;
                                            gm_l24 = t176;
                                        }
                                    }
lb150:;
                                }
                            }
                            {
                                gm_tf5srh4mp4b3i t177 = gm_l22;
                                uint64_t t178;
                                if (((((uint8_t *)&t177))[8] != 0)) {
                                    uint64_t t179 = GM_LD(uint64_t, (((uint8_t *)&t177) + 0));
                                    t178 = t179;
                                } else {
                                    {
                                        void * t181 = gm_l0;
                                        gm_tl874t2atnlh3 t182 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l20) + 16));
                                        gm_t6j6smj7qoctu t183;
                                        void * t184 = gm_l0;
                                        gm_t88rcucg1b7uk t185 = gm_l1;
                                        gm_b16_8 t186 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t184, t185);
                                        if (gm_unw.kind) {
                                            goto gm_ll105;
                                        }
                                        GM_ST(gm_b16_8, (uint8_t *)&t183 + 0, t186);
                                        gm_b16_8 t187 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l20) + 0));
                                        GM_ST(gm_b16_8, (uint8_t *)&t183 + 16, t187);
                                        gm_tabb710d9mq2e t188;
                                        gm_f_format_lc07prd((void *)&t188, (void *)NULL, cx, NULL, &t183);
                                        if (gm_unw.kind) {
                                            goto gm_ll105;
                                        }
                                        if (((((uint8_t *)&t188))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t188) + 8, gm_src7nt72u848oha, 231, 42);
                                        gm_b16_8 t189 = GM_LD(gm_b16_8, (((uint8_t *)&t188) + 8));
                                        gm_b16_8 t190 = GM_LD(gm_b16_8, ((uint8_t *)&t189));
                                        gm_b0_1 t191 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t181, t182, t190);
                                        if (gm_unw.kind) {
                                            goto gm_ll105;
                                        }
                                    }
                                    {
                                        {
                                            void * t193 = gm_l0;
                                            void * t194 = (void *)((uint8_t *)&F.l21);
                                            gm_tkn42u7na3t81 t195;
                                            memset(&t195, 0, sizeof t195);
                                            void * t196 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t193, t194, t195);
                                            if (gm_unw.kind) {
                                                goto gm_ll105;
                                            }
                                        }
                                    }
                                    goto lc101;
                                }
                                gm_l27 = t178;
                            }
                            {
                                uint64_t t198 = gm_l27;
                                uint64_t t199 = (uintptr_t)t198;
                                uint64_t t200 = GM_LD(uintptr_t, (((uint8_t *)&F.l11) + 0) + 8);
                                if (t199 >= (uintptr_t)(t200)) gm_trap_index(t199, t200, gm_src7nt72u848oha, 235, 20);
                                uint8_t t201 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l11) + 0)) + t199 * 1));
                                if (!t201) goto ie197;
                                {
                                    void * t203 = gm_l0;
                                    gm_tl874t2atnlh3 t204 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l20) + 16));
                                    gm_b16_8 t205 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l20) + 0));
                                    gm_tnpifv20irnph t206;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t206 + 0), t205);
                                    gm_tabb710d9mq2e t207;
                                    gm_f_format_bbsjt91((void *)&t207, (void *)NULL, cx, NULL, t206);
                                    if (gm_unw.kind) {
                                        goto gm_ll105;
                                    }
                                    if (((((uint8_t *)&t207))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t207) + 8, gm_src7nt72u848oha, 235, 61);
                                    gm_b16_8 t208 = GM_LD(gm_b16_8, (((uint8_t *)&t207) + 8));
                                    gm_b16_8 t209 = GM_LD(gm_b16_8, ((uint8_t *)&t208));
                                    gm_b0_1 t210 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t203, t204, t209);
                                    if (gm_unw.kind) {
                                        goto gm_ll105;
                                    }
                                }
                                goto ix197;
ie197:;
ix197:;
                            }
                            {
                                uint64_t t211 = gm_l27;
                                uint64_t t212 = (uintptr_t)t211;
                                uint64_t t213 = GM_LD(uintptr_t, (((uint8_t *)&F.l11) + 0) + 8);
                                if (t212 >= (uintptr_t)(t213)) gm_trap_index(t212, t213, gm_src7nt72u848oha, 236, 17);
                                GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l11) + 0)) + t212 * 1), ((uint8_t)1));
                            }
                            {
                                void * t214 = gm_l0;
                                void * t215 = (void *)((uint8_t *)&F.l21);
                                uint64_t t216 = gm_l27;
                                uint64_t t217 = (uintptr_t)t216;
                                uint64_t t218 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                                if (t217 >= (uintptr_t)(t218)) gm_trap_index(t217, t218, gm_src7nt72u848oha, 237, 41);
                                gm_t88rcucg1b7uk t219 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t217 * 40) + 16));
                                void * t220 = gm_f_field_value_19vleud((void *)NULL, cx, NULL, t214, t215, t219);
                                if (gm_unw.kind) {
                                    goto gm_ll105;
                                }
                                gm_l28 = t220;
                            }
                            {
                                void * t221 = (void *)((uint8_t *)&F.l14);
                                uint64_t t222 = gm_l27;
                                gm_tftb1odc739t5 t223 = gm_f_push_ucgmlo2(t221, t222);
                                if (((((uint8_t *)&t223))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t223) + 1, gm_src7nt72u848oha, 238, 17);
                                gm_b0_1 t224 = GM_LD(gm_b0_1, (((uint8_t *)&t223) + 1));
                            }
                            {
                                uint64_t t225 = gm_l27;
                                uint64_t t226 = (uintptr_t)t225;
                                uint64_t t227 = GM_LD(uintptr_t, (((uint8_t *)&F.l10) + 0) + 8);
                                if (t226 >= (uintptr_t)(t227)) gm_trap_index(t226, t227, gm_src7nt72u848oha, 239, 17);
                                void * t228 = gm_l28;
                                GM_ST(void *, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l10) + 0)) + t226 * 8), t228);
                            }
                            goto me114;
mn141:;
ma117:;
                            uint8_t t230 = GM_LD(uint8_t, ((((uint8_t *)&F.l18) + 0) + 0));
                            if (t230 != ((uint8_t)2ULL)) goto mn229;
                            gm_tc3oht4ksth9d t231 = GM_LD(gm_tc3oht4ksth9d, ((((uint8_t *)&F.l18) + 0) + 8));
                            F.l29 = t231;
                            {
                                void * t233 = gm_l0;
                                void * t234 = (void *)((uint8_t *)&F.l29);
                                gm_tkn42u7na3t81 t235;
                                memset(&t235, 0, sizeof t235);
                                void * t236 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t233, t234, t235);
                                if (gm_unw.kind) {
                                    goto gm_ll105;
                                }
                                gm_l30 = t236;
                            }
                            {
                                void * t237 = gm_l0;
                                void * t238 = (void *)(((uint8_t *)t237) + 96);
                                void * t239 = gm_l0;
                                void * t240 = GM_LD(void *, (((uint8_t *)t239) + 0));
                                void * t241 = gm_l30;
                                gm_t88rcucg1b7uk t242 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t241) + 0));
                                gm_t88rcucg1b7uk t243 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t238, t240, t242);
                                if (gm_unw.kind) {
                                    goto gm_ll105;
                                }
                                gm_l31 = t243;
                            }
                            {
                                void * t244 = gm_l0;
                                gm_t88rcucg1b7uk t245 = gm_l31;
                                void * t246 = (void *)((uint8_t *)&F.l29);
                                gm_th2lqbo77pvsc t247;
                                gm_f_spread_source_fields_5csral7((void *)&t247, (void *)NULL, cx, NULL, t244, t245, t246);
                                if (gm_unw.kind) {
                                    goto gm_ll105;
                                }
                                gm_b16_8 t248;
                                if (((((uint8_t *)&t247))[16] != 0)) {
                                    gm_b16_8 t249 = GM_LD(gm_b16_8, (((uint8_t *)&t247) + 0));
                                    t248 = t249;
                                } else {
                                    goto lc101;
                                }
                                gm_l32 = t248;
                            }
                            {
                                {
                                    gm_b16_8 t251 = GM_LD(gm_b16_8, (((uint8_t *)&F.l14) + 0));
                                    gm_l33 = t251;
                                }
                                {
                                    gm_l34 = ((uint64_t)0ULL);
                                }
                                {
                                    for (;;) {
                                        uint64_t t253 = gm_l34;
                                        uint64_t t254 = GM_LD(uintptr_t, ((uint8_t *)&gm_l33) + 8);
                                        uint8_t t255 = (t253 < t254);
                                        if (!t255) goto lb252;
                                        {
                                            uint64_t t258 = gm_l34;
                                            uint64_t t259 = (uintptr_t)t258;
                                            uint64_t t260 = GM_LD(uintptr_t, ((uint8_t *)&gm_l33) + 8);
                                            uint64_t t261 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l33)) + t259 * 8));
                                            uint64_t t264 = GM_LD(uint64_t, ((uint8_t *)&t261));
                                            gm_l35 = t264;
                                            {
                                                uint64_t t267 = gm_l35;
                                                uint64_t t268 = (uintptr_t)t267;
                                                uint64_t t269 = GM_LD(uintptr_t, (((uint8_t *)&F.l10) + 0) + 8);
                                                if (t268 >= (uintptr_t)(t269)) gm_trap_index(t268, t269, gm_src7nt72u848oha, 247, 29);
                                                void * t270 = GM_LD(void *, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l10) + 0)) + t268 * 8));
                                                if (!(GM_LD(void *, ((uint8_t *)&t270)) != 0)) goto ie266;
                                                void * t271 = GM_LD(void *, ((uint8_t *)&t270));
                                                gm_l36 = t271;
                                                {
                                                    uint64_t t273 = gm_l35;
                                                    uint64_t t274 = (uintptr_t)t273;
                                                    uint64_t t275 = GM_LD(uintptr_t, (((uint8_t *)&F.l10) + 0) + 8);
                                                    if (t274 >= (uintptr_t)(t275)) gm_trap_index(t274, t275, gm_src7nt72u848oha, 247, 46);
                                                    uint8_t *t276 = ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l10) + 0)) + t274 * 8);
                                                    void * t277 = gm_l0;
                                                    void * t278 = gm_l36;
                                                    void * t279 = (void *)((uint8_t *)&F.l9);
                                                    void * t280 = gm_f_bind_temp_sa5rcbr((void *)NULL, cx, NULL, t277, t278, t279);
                                                    if (gm_unw.kind) {
                                                        goto gm_ll256;
                                                    }
                                                    GM_ST(void *, t276, t280);
                                                }
                                                goto ix266;
ie266:;
ix266:;
                                            }
                                            goto me262;
mn263:;
                                            gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 246, 17);
me262:;
                                        }
lc252:;
                                        {
                                            uint64_t t282 = gm_l34;
                                            uint64_t r283; bool o284 = __builtin_add_overflow(t282, ((uint64_t)1ULL), &r283);
                                            uint64_t t285 = r283;
                                            gm_l34 = t285;
                                        }
                                    }
gm_ll256:;
                                    if (gm_unw.frame == &F.h && gm_unw.label == 3) {
                                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb252; }
                                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc252; }
                                    }
                                    goto gm_ll105;
lb252:;
                                }
                            }
                            {
                                void * t286 = (void *)((uint8_t *)&F.l14);
                                gm_f_clear_4qp22g2(t286);
                            }
                            {
                                void * t287 = gm_l0;
                                void * t288 = gm_l30;
                                void * t289 = (void *)((uint8_t *)&F.l9);
                                void * t290 = gm_f_bind_temp_sa5rcbr((void *)NULL, cx, NULL, t287, t288, t289);
                                if (gm_unw.kind) {
                                    goto gm_ll105;
                                }
                                gm_l37 = t290;
                            }
                            {
                                {
                                    gm_b16_8 t292 = gm_l32;
                                    gm_b16_8 t293 = GM_LD(gm_b16_8, ((uint8_t *)&t292));
                                    gm_l38 = t293;
                                }
                                {
                                    gm_l39 = ((uint64_t)0ULL);
                                }
                                {
                                    for (;;) {
                                        uint64_t t295 = gm_l39;
                                        uint64_t t296 = GM_LD(uintptr_t, ((uint8_t *)&gm_l38) + 8);
                                        uint8_t t297 = (t295 < t296);
                                        if (!t297) goto lb294;
                                        {
                                            gm_tjvm1fbs3qbum t300;
                                            uint64_t t301 = gm_l39;
                                            GM_ST(uint64_t, (uint8_t *)&t300 + 0, t301);
                                            uint64_t t302 = gm_l39;
                                            uint64_t t303 = (uintptr_t)t302;
                                            uint64_t t304 = GM_LD(uintptr_t, ((uint8_t *)&gm_l38) + 8);
                                            memcpy((uint8_t *)&t300 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l38)) + t303 * 24), 24);
                                            uint64_t t307 = GM_LD(uint64_t, (((uint8_t *)&t300) + 0));
                                            gm_l40 = t307;
                                            gm_l41 = (*(gm_tep2drsl9408b *)(((uint8_t *)&t300) + 8));
                                            {
                                                gm_tf5srh4mp4b3i t309;
                                                memset(&t309, 0, sizeof t309);
                                                gm_l42 = t309;
                                            }
                                            {
                                                {
                                                    gm_b16_8 t311 = gm_l7;
                                                    gm_b16_8 t312 = GM_LD(gm_b16_8, ((uint8_t *)&t311));
                                                    gm_l43 = t312;
                                                }
                                                {
                                                    gm_l44 = ((uint64_t)0ULL);
                                                }
                                                {
                                                    for (;;) {
                                                        uint64_t t314 = gm_l44;
                                                        uint64_t t315 = GM_LD(uintptr_t, ((uint8_t *)&gm_l43) + 8);
                                                        uint8_t t316 = (t314 < t315);
                                                        if (!t316) goto lb313;
                                                        {
                                                            gm_t8ta482bl6be0 t319;
                                                            uint64_t t320 = gm_l44;
                                                            GM_ST(uint64_t, (uint8_t *)&t319 + 0, t320);
                                                            uint64_t t321 = gm_l44;
                                                            uint64_t t322 = (uintptr_t)t321;
                                                            uint64_t t323 = GM_LD(uintptr_t, ((uint8_t *)&gm_l43) + 8);
                                                            memcpy((uint8_t *)&t319 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l43)) + t322 * 40), 40);
                                                            uint64_t t326 = GM_LD(uint64_t, (((uint8_t *)&t319) + 0));
                                                            gm_l45 = t326;
                                                            F.l46 = (*(gm_t0mglksrkfa5e *)(((uint8_t *)&t319) + 8));
                                                            {
                                                                gm_b16_8 t329 = GM_LD(gm_b16_8, (((uint8_t *)&F.l46) + 0));
                                                                gm_b16_8 t330 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l41) + 0));
                                                                uint8_t t331 = (gm_eq0iqiubfm7e(((uint8_t *)&t329), ((uint8_t *)&t330)));
                                                                if (!t331) goto ie328;
                                                                {
                                                                    uint64_t t333 = gm_l45;
                                                                    gm_tf5srh4mp4b3i t334;
                                                                    GM_ST(uint8_t, (uint8_t *)&t334 + 9, 0);
                                                                    GM_ST(uint16_t, (uint8_t *)&t334 + 10, 0);
                                                                    GM_ST(uint32_t, (uint8_t *)&t334 + 12, 0);
                                                                    GM_ST(uint64_t, ((uint8_t *)&t334), t333);
                                                                    ((uint8_t *)&t334)[8] = 1;
                                                                    gm_l42 = t334;
                                                                }
                                                                {
                                                                    goto lb313;
                                                                }
                                                                goto ix328;
ie328:;
ix328:;
                                                            }
                                                            goto me324;
mn325:;
                                                            gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 253, 21);
me324:;
                                                        }
lc313:;
                                                        {
                                                            uint64_t t336 = gm_l44;
                                                            uint64_t r337; bool o338 = __builtin_add_overflow(t336, ((uint64_t)1ULL), &r337);
                                                            uint64_t t339 = r337;
                                                            gm_l44 = t339;
                                                        }
                                                    }
lb313:;
                                                }
                                            }
                                            {
                                                gm_tf5srh4mp4b3i t340 = gm_l42;
                                                uint64_t t341;
                                                if (((((uint8_t *)&t340))[8] != 0)) {
                                                    uint64_t t342 = GM_LD(uint64_t, (((uint8_t *)&t340) + 0));
                                                    t341 = t342;
                                                } else {
                                                    {
                                                        void * t344 = gm_l0;
                                                        gm_tl874t2atnlh3 t345 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l29) + 72));
                                                        gm_t6j6smj7qoctu t346;
                                                        void * t347 = gm_l0;
                                                        gm_t88rcucg1b7uk t348 = gm_l1;
                                                        gm_b16_8 t349 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t347, t348);
                                                        if (gm_unw.kind) {
                                                            goto gm_ll298;
                                                        }
                                                        GM_ST(gm_b16_8, (uint8_t *)&t346 + 0, t349);
                                                        gm_b16_8 t350 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l41) + 0));
                                                        GM_ST(gm_b16_8, (uint8_t *)&t346 + 16, t350);
                                                        gm_tabb710d9mq2e t351;
                                                        gm_f_format_6fdilsr((void *)&t351, (void *)NULL, cx, NULL, &t346);
                                                        if (gm_unw.kind) {
                                                            goto gm_ll298;
                                                        }
                                                        if (((((uint8_t *)&t351))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t351) + 8, gm_src7nt72u848oha, 260, 42);
                                                        gm_b16_8 t352 = GM_LD(gm_b16_8, (((uint8_t *)&t351) + 8));
                                                        gm_b16_8 t353 = GM_LD(gm_b16_8, ((uint8_t *)&t352));
                                                        gm_b0_1 t354 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t344, t345, t353);
                                                        if (gm_unw.kind) {
                                                            goto gm_ll298;
                                                        }
                                                    }
                                                    goto lc294;
                                                }
                                                gm_l47 = t341;
                                            }
                                            {
                                                uint64_t t356 = gm_l47;
                                                uint64_t t357 = (uintptr_t)t356;
                                                uint64_t t358 = GM_LD(uintptr_t, (((uint8_t *)&F.l11) + 0) + 8);
                                                if (t357 >= (uintptr_t)(t358)) gm_trap_index(t357, t358, gm_src7nt72u848oha, 263, 24);
                                                uint8_t t359 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l11) + 0)) + t357 * 1));
                                                if (!t359) goto ie355;
                                                {
                                                    void * t361 = gm_l0;
                                                    gm_tl874t2atnlh3 t362 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l29) + 72));
                                                    gm_b16_8 t363 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l41) + 0));
                                                    gm_tnpifv20irnph t364;
                                                    GM_ST(gm_b16_8, ((uint8_t *)&t364 + 0), t363);
                                                    gm_tabb710d9mq2e t365;
                                                    gm_f_format_adtncrl((void *)&t365, (void *)NULL, cx, NULL, t364);
                                                    if (gm_unw.kind) {
                                                        goto gm_ll298;
                                                    }
                                                    if (((((uint8_t *)&t365))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t365) + 8, gm_src7nt72u848oha, 266, 42);
                                                    gm_b16_8 t366 = GM_LD(gm_b16_8, (((uint8_t *)&t365) + 8));
                                                    gm_b16_8 t367 = GM_LD(gm_b16_8, ((uint8_t *)&t366));
                                                    gm_b0_1 t368 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t361, t362, t367);
                                                    if (gm_unw.kind) {
                                                        goto gm_ll298;
                                                    }
                                                }
                                                {
                                                    goto lc294;
                                                }
                                                goto ix355;
ie355:;
ix355:;
                                            }
                                            {
                                                uint64_t t370 = gm_l47;
                                                uint64_t t371 = (uintptr_t)t370;
                                                uint64_t t372 = GM_LD(uintptr_t, (((uint8_t *)&F.l10) + 0) + 8);
                                                if (t371 >= (uintptr_t)(t372)) gm_trap_index(t371, t372, gm_src7nt72u848oha, 269, 24);
                                                void * t373 = GM_LD(void *, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l10) + 0)) + t371 * 8));
                                                uint8_t t374;
                                                if ((GM_LD(void *, ((uint8_t *)&t373)) != 0)) goto mn376;
                                                t374 = ((uint8_t)0);
                                                goto me375;
mn376:;
                                                t374 = ((uint8_t)1);
                                                goto me375;
mn377:;
                                                gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 269, 24);
me375:;
                                                if (!t374) goto ie369;
                                                {
                                                    void * t379 = gm_l0;
                                                    gm_tl874t2atnlh3 t380 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l29) + 72));
                                                    gm_b16_8 t381 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l41) + 0));
                                                    gm_tnpifv20irnph t382;
                                                    GM_ST(gm_b16_8, ((uint8_t *)&t382 + 0), t381);
                                                    gm_tabb710d9mq2e t383;
                                                    gm_f_format_9tg469g((void *)&t383, (void *)NULL, cx, NULL, t382);
                                                    if (gm_unw.kind) {
                                                        goto gm_ll298;
                                                    }
                                                    if (((((uint8_t *)&t383))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t383) + 8, gm_src7nt72u848oha, 269, 70);
                                                    gm_b16_8 t384 = GM_LD(gm_b16_8, (((uint8_t *)&t383) + 8));
                                                    gm_b16_8 t385 = GM_LD(gm_b16_8, ((uint8_t *)&t384));
                                                    gm_b0_1 t386 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t379, t380, t385);
                                                    if (gm_unw.kind) {
                                                        goto gm_ll298;
                                                    }
                                                }
                                                goto ix369;
ie369:;
ix369:;
                                            }
                                            {
                                                gm_t88rcucg1b7uk t387 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l41) + 16));
                                                void * t388 = gm_l37;
                                                uint64_t t389 = gm_l40;
                                                uint32_t t390 = (uint32_t)t389;
                                                gm_tbl8il92l97ut t391;
                                                GM_ST(uint32_t, (uint8_t *)&t391 + 12, 0);
                                                GM_ST(void *, ((uint8_t *)&t391 + 0), t388);
                                                GM_ST(uint32_t, ((uint8_t *)&t391 + 8), t390);
                                                gm_teti411eujvf4 t392;
                                                memset(&t392, 0, sizeof t392);
                                                GM_ST(uint8_t, ((uint8_t *)&t392 + 0), ((uint8_t)19ULL));
                                                GM_ST(gm_tbl8il92l97ut, ((uint8_t *)&t392 + 8), t391);
                                                gm_tl874t2atnlh3 t393 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l29) + 72));
                                                void * t394 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t387, &t392, t393);
                                                if (gm_unw.kind) {
                                                    goto gm_ll298;
                                                }
                                                gm_l48 = t394;
                                            }
                                            {
                                                uint64_t t395 = gm_l47;
                                                uint64_t t396 = (uintptr_t)t395;
                                                uint64_t t397 = GM_LD(uintptr_t, (((uint8_t *)&F.l10) + 0) + 8);
                                                if (t396 >= (uintptr_t)(t397)) gm_trap_index(t396, t397, gm_src7nt72u848oha, 271, 21);
                                                uint8_t *t398 = ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l10) + 0)) + t396 * 8);
                                                void * t399 = gm_l0;
                                                void * t400 = gm_l48;
                                                uint64_t t401 = gm_l47;
                                                uint64_t t402 = (uintptr_t)t401;
                                                uint64_t t403 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                                                if (t402 >= (uintptr_t)(t403)) gm_trap_index(t402, t403, gm_src7nt72u848oha, 271, 54);
                                                gm_t88rcucg1b7uk t404 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t402 * 40) + 16));
                                                void * t405 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t399, t400, t404);
                                                if (gm_unw.kind) {
                                                    goto gm_ll298;
                                                }
                                                GM_ST(void *, t398, t405);
                                            }
                                            goto me305;
mn306:;
                                            gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 251, 17);
me305:;
                                        }
lc294:;
                                        {
                                            uint64_t t407 = gm_l39;
                                            uint64_t r408; bool o409 = __builtin_add_overflow(t407, ((uint64_t)1ULL), &r408);
                                            uint64_t t410 = r408;
                                            gm_l39 = t410;
                                        }
                                    }
gm_ll298:;
                                    if (gm_unw.frame == &F.h && gm_unw.label == 4) {
                                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb294; }
                                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc294; }
                                    }
                                    goto gm_ll105;
lb294:;
                                }
                            }
                            goto me114;
mn229:;
ma118:;
                            gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 215, 9);
me114:;
                        }
                        goto me110;
mn111:;
                        gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 214, 5);
me110:;
                    }
lc101:;
                    {
                        uint64_t t412 = gm_l17;
                        uint64_t r413; bool o414 = __builtin_add_overflow(t412, ((uint64_t)1ULL), &r413);
                        uint64_t t415 = r413;
                        gm_l17 = t415;
                    }
                }
gm_ll105:;
                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb101; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc101; }
                }
                goto gm_lad;
lb101:;
            }
        }
        {
            gm_l49 = ((uint8_t)0);
        }
        {
            {
                gm_l51 = ((uint64_t)1ULL);
            }
            {
                uint64_t t417 = GM_LD(uintptr_t, (((uint8_t *)&F.l14) + 0) + 8);
                uint64_t t418 = (t417 > ((uint64_t)1ULL) ? t417 : ((uint64_t)1ULL));
                gm_l50 = t418;
            }
            {
                for (;;) {
                    uint64_t t420 = gm_l51;
                    uint64_t t421 = gm_l50;
                    uint8_t t422 = (t420 < t421);
                    if (!t422) goto lb419;
                    {
                        uint64_t t425 = gm_l51;
                        uint64_t t428 = GM_LD(uint64_t, ((uint8_t *)&t425));
                        gm_l52 = t428;
                        {
                            uint64_t t431 = gm_l52;
                            uint64_t r432; bool o433 = __builtin_sub_overflow(t431, ((uint64_t)1ULL), &r432);
                            if (o433) gm_trap("integer overflow in `-`", gm_src7nt72u848oha, 280, 26);
                            uint64_t t434 = r432;
                            uint64_t t435 = (uintptr_t)t434;
                            uint64_t t436 = GM_LD(uintptr_t, (((uint8_t *)&F.l14) + 0) + 8);
                            if (t435 >= (uintptr_t)(t436)) gm_trap_index(t435, t436, gm_src7nt72u848oha, 280, 12);
                            uint64_t t437 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l14) + 0)) + t435 * 8));
                            uint64_t t438 = gm_l52;
                            uint64_t t439 = (uintptr_t)t438;
                            uint64_t t440 = GM_LD(uintptr_t, (((uint8_t *)&F.l14) + 0) + 8);
                            if (t439 >= (uintptr_t)(t440)) gm_trap_index(t439, t440, gm_src7nt72u848oha, 280, 35);
                            uint64_t t441 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l14) + 0)) + t439 * 8));
                            uint8_t t442 = (t437 > t441);
                            if (!t442) goto ie430;
                            {
                                gm_l49 = ((uint8_t)1);
                            }
                            goto ix430;
ie430:;
ix430:;
                        }
                        goto me426;
mn427:;
                        gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 279, 5);
me426:;
                    }
lc419:;
                    {
                        uint64_t t445 = gm_l51;
                        uint64_t r446; bool o447 = __builtin_add_overflow(t445, ((uint64_t)1ULL), &r446);
                        uint64_t t448 = r446;
                        gm_l51 = t448;
                    }
                }
lb419:;
            }
        }
        {
            uint8_t t450 = gm_l49;
            if (!t450) goto ie449;
            {
                {
                    gm_b16_8 t453 = GM_LD(gm_b16_8, (((uint8_t *)&F.l14) + 0));
                    gm_l53 = t453;
                }
                {
                    gm_l54 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t455 = gm_l54;
                        uint64_t t456 = GM_LD(uintptr_t, ((uint8_t *)&gm_l53) + 8);
                        uint8_t t457 = (t455 < t456);
                        if (!t457) goto lb454;
                        {
                            uint64_t t460 = gm_l54;
                            uint64_t t461 = (uintptr_t)t460;
                            uint64_t t462 = GM_LD(uintptr_t, ((uint8_t *)&gm_l53) + 8);
                            uint64_t t463 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l53)) + t461 * 8));
                            uint64_t t466 = GM_LD(uint64_t, ((uint8_t *)&t463));
                            gm_l55 = t466;
                            {
                                uint64_t t469 = gm_l55;
                                uint64_t t470 = (uintptr_t)t469;
                                uint64_t t471 = GM_LD(uintptr_t, (((uint8_t *)&F.l10) + 0) + 8);
                                if (t470 >= (uintptr_t)(t471)) gm_trap_index(t470, t471, gm_src7nt72u848oha, 284, 21);
                                void * t472 = GM_LD(void *, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l10) + 0)) + t470 * 8));
                                if (!(GM_LD(void *, ((uint8_t *)&t472)) != 0)) goto ie468;
                                void * t473 = GM_LD(void *, ((uint8_t *)&t472));
                                gm_l56 = t473;
                                {
                                    uint64_t t475 = gm_l55;
                                    uint64_t t476 = (uintptr_t)t475;
                                    uint64_t t477 = GM_LD(uintptr_t, (((uint8_t *)&F.l10) + 0) + 8);
                                    if (t476 >= (uintptr_t)(t477)) gm_trap_index(t476, t477, gm_src7nt72u848oha, 284, 38);
                                    uint8_t *t478 = ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l10) + 0)) + t476 * 8);
                                    void * t479 = gm_l0;
                                    void * t480 = gm_l56;
                                    void * t481 = (void *)((uint8_t *)&F.l9);
                                    void * t482 = gm_f_bind_temp_sa5rcbr((void *)NULL, cx, NULL, t479, t480, t481);
                                    if (gm_unw.kind) {
                                        goto gm_ll458;
                                    }
                                    GM_ST(void *, t478, t482);
                                }
                                goto ix468;
ie468:;
ix468:;
                            }
                            goto me464;
mn465:;
                            gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 283, 9);
me464:;
                        }
lc454:;
                        {
                            uint64_t t484 = gm_l54;
                            uint64_t r485; bool o486 = __builtin_add_overflow(t484, ((uint64_t)1ULL), &r485);
                            uint64_t t487 = r485;
                            gm_l54 = t487;
                        }
                    }
gm_ll458:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 7) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb454; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc454; }
                    }
                    goto gm_lad;
lb454:;
                }
            }
            goto ix449;
ie449:;
ix449:;
        }
        {
            gm_f_list_vp1djv1_2((void *)&F.l57, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_l59 = ((uint64_t)0ULL);
            }
            {
                uint64_t t489 = gm_l8;
                gm_l58 = t489;
            }
            {
                for (;;) {
                    uint64_t t491 = gm_l59;
                    uint64_t t492 = gm_l58;
                    uint8_t t493 = (t491 < t492);
                    if (!t493) goto lb490;
                    {
                        uint64_t t496 = gm_l59;
                        uint64_t t499 = GM_LD(uint64_t, ((uint8_t *)&t496));
                        gm_l60 = t499;
                        {
                            uint64_t t502 = gm_l60;
                            uint64_t t503 = (uintptr_t)t502;
                            uint64_t t504 = GM_LD(uintptr_t, (((uint8_t *)&F.l10) + 0) + 8);
                            if (t503 >= (uintptr_t)(t504)) gm_trap_index(t503, t504, gm_src7nt72u848oha, 289, 17);
                            void * t505 = GM_LD(void *, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l10) + 0)) + t503 * 8));
                            if (!(GM_LD(void *, ((uint8_t *)&t505)) != 0)) goto ie501;
                            void * t506 = GM_LD(void *, ((uint8_t *)&t505));
                            gm_l61 = t506;
                            {
                                void * t508 = (void *)((uint8_t *)&F.l57);
                                void * t509 = gm_l61;
                                gm_tftb1odc739t5 t510 = gm_f_push_d943vgp_2(t508, &(*(gm_t69m56meqefs8 *)((uint8_t *)t509)));
                                if (((((uint8_t *)&t510))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t510) + 1, gm_src7nt72u848oha, 290, 13);
                                gm_b0_1 t511 = GM_LD(gm_b0_1, (((uint8_t *)&t510) + 1));
                            }
                            {
                                goto lc490;
                            }
                            goto ix501;
ie501:;
ix501:;
                        }
                        {
                            uint64_t t513 = gm_l60;
                            uint64_t t514 = (uintptr_t)t513;
                            uint64_t t515 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                            if (t514 >= (uintptr_t)(t515)) gm_trap_index(t514, t515, gm_src7nt72u848oha, 294, 12);
                            uint8_t t516 = (((GM_LD(uintptr_t, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t514 * 40) + 0) + 8) == 1) && memcmp(GM_LD(void *, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t514 * 40) + 0)), gm_sfdbfrv46lihd, 1) == 0));
                            if (!t516) goto ie512;
                            void * t517 = gm_l0;
                            void * t518 = GM_LD(void *, (((uint8_t *)t517) + 0));
                            gm_t88rcucg1b7uk t519 = gm_l1;
                            gm_tkn42u7na3t81 t520 = gm_f_bits_struct_ty_l1kjtap(t518, GM_CONV(gm_csguankko4lmhd, t519));
                            uint8_t t521;
                            if (((((uint8_t *)&t520))[4] != 0)) goto mn523;
                            t521 = ((uint8_t)0);
                            goto me522;
mn523:;
                            t521 = ((uint8_t)1);
                            goto me522;
mn524:;
                            gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 294, 38);
me522:;
                            if (!t521) goto ie512;
                            {
                                uint64_t t526 = gm_l60;
                                uint64_t t527 = (uintptr_t)t526;
                                uint64_t t528 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                                if (t527 >= (uintptr_t)(t528)) gm_trap_index(t527, t528, gm_src7nt72u848oha, 295, 19);
                                gm_t88rcucg1b7uk t529 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t527 * 40) + 16));
                                gm_l62 = t529;
                            }
                            {
                                void * t530 = (void *)((uint8_t *)&F.l57);
                                gm_t69m56meqefs8 t531;
                                void * t533 = gm_l0;
                                void * t534 = GM_LD(void *, (((uint8_t *)t533) + 0));
                                void * t535 = (void *)(((uint8_t *)t534) + 8);
                                gm_t88rcucg1b7uk t536 = gm_l62;
                                gm_t53at80pfdus3 t537;
                                gm_f_kind_jcnt4hm((void *)&t537, t535, GM_CONV(gm_csguankko4lmhd, t536));
                                uint8_t t538 = ((GM_LD(uint8_t, ((uint8_t *)&t537) + 0) == ((uint8_t)4ULL)));
                                if (!t538) goto ie532;
                                gm_t88rcucg1b7uk t540 = gm_l62;
                                gm_teti411eujvf4 t541;
                                memset(&t541, 0, sizeof t541);
                                GM_ST(uint8_t, ((uint8_t *)&t541 + 0), ((uint8_t)4ULL));
                                GM_ST(uint8_t, ((uint8_t *)&t541 + 8), ((uint8_t)0));
                                gm_tl874t2atnlh3 t542 = gm_l3;
                                void * t543 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t540, &t541, t542);
                                if (gm_unw.kind) {
                                    goto gm_ll494;
                                }
                                gm_t69m56meqefs8 t544 = GM_LD(gm_t69m56meqefs8, ((uint8_t *)t543));
                                gm_t69m56meqefs8 t545 = t544;
                                t531 = t545;
                                goto ix532;
ie532:;
                                gm_t88rcucg1b7uk t547 = gm_l62;
                                gm_tbci5ospk5nnn t548;
                                gm_f_zero_s25l990((void *)&t548, (void *)NULL, cx, NULL);
                                if (gm_unw.kind) {
                                    goto gm_ll494;
                                }
                                gm_teti411eujvf4 t549;
                                GM_ST(uint8_t, (uint8_t *)&t549 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t549 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t549 + 4, 0);
                                GM_ST(uint64_t, (uint8_t *)&t549 + 56, 0);
                                GM_ST(uint8_t, ((uint8_t *)&t549 + 0), ((uint8_t)0ULL));
                                GM_ST(gm_tbci5ospk5nnn, ((uint8_t *)&t549 + 8), t548);
                                gm_tl874t2atnlh3 t550 = gm_l3;
                                void * t551 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t547, &t549, t550);
                                if (gm_unw.kind) {
                                    goto gm_ll494;
                                }
                                gm_t69m56meqefs8 t552 = GM_LD(gm_t69m56meqefs8, ((uint8_t *)t551));
                                gm_t69m56meqefs8 t553 = t552;
                                t531 = t553;
ix532:;
                                gm_tftb1odc739t5 t554 = gm_f_push_d943vgp_2(t530, &t531);
                                if (((((uint8_t *)&t554))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t554) + 1, gm_src7nt72u848oha, 296, 13);
                                gm_b0_1 t555 = GM_LD(gm_b0_1, (((uint8_t *)&t554) + 1));
                            }
                            {
                                goto lc490;
                            }
                            goto ix512;
ie512:;
ix512:;
                        }
                        {
                            void * t557 = gm_l0;
                            uint32_t t558 = gm_l4;
                            uint64_t t559 = gm_l60;
                            uint64_t t560 = gm_l60;
                            uint64_t t561 = (uintptr_t)t560;
                            uint64_t t562 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                            if (t561 >= (uintptr_t)(t562)) gm_trap_index(t561, t562, gm_src7nt72u848oha, 299, 41);
                            gm_t88rcucg1b7uk t563 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t561 * 40) + 16));
                            void * t564 = gm_f_field_default_4ssmakm((void *)NULL, cx, NULL, t557, t558, t559, t563);
                            if (gm_unw.kind) {
                                goto gm_ll494;
                            }
                            if (!(GM_LD(void *, ((uint8_t *)&t564)) != 0)) goto ie556;
                            void * t565 = GM_LD(void *, ((uint8_t *)&t564));
                            gm_l63 = t565;
                            {
                                void * t567 = (void *)((uint8_t *)&F.l57);
                                void * t568 = gm_l63;
                                gm_tftb1odc739t5 t569 = gm_f_push_d943vgp_2(t567, &(*(gm_t69m56meqefs8 *)((uint8_t *)t568)));
                                if (((((uint8_t *)&t569))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t569) + 1, gm_src7nt72u848oha, 300, 13);
                                gm_b0_1 t570 = GM_LD(gm_b0_1, (((uint8_t *)&t569) + 1));
                            }
                            goto ix556;
ie556:;
                            {
                                uint8_t t573 = gm_l15;
                                uint8_t t574 = (uint8_t)!t573;
                                if (!t574) goto ie572;
                                {
                                    void * t576 = gm_l0;
                                    gm_tl874t2atnlh3 t577 = gm_l3;
                                    uint64_t t578 = gm_l60;
                                    uint64_t t579 = (uintptr_t)t578;
                                    uint64_t t580 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                                    if (t579 >= (uintptr_t)(t580)) gm_trap_index(t579, t580, gm_src7nt72u848oha, 302, 99);
                                    gm_b16_8 t581 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t579 * 40) + 0));
                                    gm_tnpifv20irnph t582;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t582 + 0), t581);
                                    gm_tabb710d9mq2e t583;
                                    gm_f_format_843cvhd((void *)&t583, (void *)NULL, cx, NULL, t582);
                                    if (gm_unw.kind) {
                                        goto gm_ll494;
                                    }
                                    if (((((uint8_t *)&t583))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t583) + 8, gm_src7nt72u848oha, 302, 46);
                                    gm_b16_8 t584 = GM_LD(gm_b16_8, (((uint8_t *)&t583) + 8));
                                    gm_b16_8 t585 = GM_LD(gm_b16_8, ((uint8_t *)&t584));
                                    gm_b0_1 t586 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t576, t577, t585);
                                    if (gm_unw.kind) {
                                        goto gm_ll494;
                                    }
                                }
                                goto ix572;
ie572:;
ix572:;
                            }
                            {
                                void * t587 = (void *)((uint8_t *)&F.l57);
                                void * t588 = gm_l0;
                                void * t589 = GM_LD(void *, (((uint8_t *)t588) + 0));
                                gm_tl874t2atnlh3 t590 = gm_l3;
                                void * t591 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t589, t590);
                                if (gm_unw.kind) {
                                    goto gm_ll494;
                                }
                                gm_tftb1odc739t5 t592 = gm_f_push_d943vgp_2(t587, &(*(gm_t69m56meqefs8 *)((uint8_t *)t591)));
                                if (((((uint8_t *)&t592))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t592) + 1, gm_src7nt72u848oha, 303, 13);
                                gm_b0_1 t593 = GM_LD(gm_b0_1, (((uint8_t *)&t592) + 1));
                            }
ix556:;
                        }
                        goto me497;
mn498:;
                        gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 288, 5);
me497:;
                    }
lc490:;
                    {
                        uint64_t t595 = gm_l59;
                        uint64_t r596; bool o597 = __builtin_add_overflow(t595, ((uint64_t)1ULL), &r596);
                        uint64_t t598 = r596;
                        gm_l59 = t598;
                    }
                }
gm_ll494:;
                if (gm_unw.frame == &F.h && gm_unw.label == 8) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb490; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc490; }
                }
                goto gm_lad;
lb490:;
            }
        }
        {
            gm_t88rcucg1b7uk t599 = gm_l1;
            gm_b16_8 t600 = GM_LD(gm_b16_8, (((uint8_t *)&F.l57) + 0));
            gm_teti411eujvf4 t601;
            memset(&t601, 0, sizeof t601);
            GM_ST(uint8_t, ((uint8_t *)&t601 + 0), ((uint8_t)35ULL));
            GM_ST(gm_b16_8, ((uint8_t *)&t601 + 8), t600);
            gm_tl874t2atnlh3 t602 = gm_l3;
            void * t603 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t599, &t601, t602);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l64 = t603;
        }
        {
            uint64_t t605 = GM_LD(uintptr_t, (((uint8_t *)&F.l9) + 0) + 8);
            uint8_t t606 = ((t605 == ((uint64_t)0ULL)));
            if (!t606) goto ie604;
            {
                void * t608 = gm_l64;
                uint8_t t609 = 0;
                F.ret = t608;
                goto gm_fret;
            }
            goto ix604;
ie604:;
ix604:;
        }
        {
            gm_t88rcucg1b7uk t610 = gm_l1;
            gm_b16_8 t611 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 0));
            void * t612 = gm_l64;
            gm_tl874t2atnlh3 t613 = gm_l3;
            void * t614 = gm_f_block_typed_e7svg7n((void *)NULL, cx, NULL, t610, t611, t612, t613);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t615 = 0;
            F.ret = t614;
            goto gm_fret;
        }
        goto ix2;
ie2:;
        {
            void * t617 = gm_l0;
            void * t618 = GM_LD(void *, (((uint8_t *)t617) + 0));
            gm_tl874t2atnlh3 t619 = gm_l3;
            void * t620 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t618, t619);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t621 = 0;
            F.ret = t620;
            goto gm_fret;
        }
ix2:;
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
__attribute__((aligned(4))) void * gm_f_construct_tuple_6ee5ejf(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_b16_8 a3, gm_b16_8 a4, gm_tl874t2atnlh3 a5) {
    struct gm_fr_construct_tuple_6ee5ejf F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    gm_b16_8 gm_l2; /* elems */
    gm_b16_8 gm_l3; /* names */
    gm_b16_8 gm_l4; /* args */
    gm_tl874t2atnlh3 gm_l5; /* span */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* next */
    uint8_t gm_l10; /* spreading */
    gm_b16_8 gm_l11; /* <tmp> */
    uint64_t gm_l12; /* <tmp> */
    uint8_t gm_l15; /* spread_failed */
    void * gm_l17; /* v */
    gm_t88rcucg1b7uk gm_l18; /* vt */
    gm_tnnjeerolos0a gm_l19; /* kind */
    gm_th2lqbo77pvsc gm_l20; /* what */
    gm_b16_8 gm_l21; /* w */
    gm_b16_8 gm_l22; /* <tmp> */
    uint64_t gm_l23; /* <tmp> */
    gm_t08olq97rscn3 gm_l27; /* n */
    uint64_t gm_l29; /* i */
    gm_b16_8 gm_l31; /* ps */
    gm_b16_8 gm_l32; /* <tmp> */
    uint64_t gm_l33; /* <tmp> */
    gm_tpouv9r7lrri2 gm_l34; /* p */
    gm_b16_8 gm_l35; /* <tmp> */
    uint64_t gm_l36; /* <tmp> */
    uint64_t gm_l38; /* i */
    gm_b16_8 gm_l39; /* n */
    void * gm_l40; /* v */
    uint64_t gm_l42; /* <tmp> */
    uint64_t gm_l43; /* <tmp> */
    uint64_t gm_l44; /* i */
    void * gm_l45; /* v */
    void * gm_l46; /* agg */
    GM_STACK_CHECK(&F, 4429u);
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
        gm_f_list_pmp06kq((void *)&F.l6, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t3 = gm_l2;
            gm_l7 = t3;
        }
        {
            gm_l8 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t5 = gm_l8;
                uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                uint8_t t7 = (t5 < t6);
                if (!t7) goto lb4;
                {
                    uint64_t t10 = gm_l8;
                    uint64_t t11 = (uintptr_t)t10;
                    uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                    {
                        void * t16 = (void *)((uint8_t *)&F.l6);
                        gm_tftb1odc739t5 t17 = gm_f_push_r2k6rjf(t16, ((void *)0));
                        if (((((uint8_t *)&t17))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t17) + 1, gm_src7nt72u848oha, 84, 22);
                        gm_b0_1 t18 = GM_LD(gm_b0_1, (((uint8_t *)&t17) + 1));
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 84, 5);
me13:;
                }
lc4:;
                {
                    uint64_t t20 = gm_l8;
                    uint64_t r21; bool o22 = __builtin_add_overflow(t20, ((uint64_t)1ULL), &r21);
                    uint64_t t23 = r21;
                    gm_l8 = t23;
                }
            }
lb4:;
        }
    }
    {
        gm_l9 = ((uint64_t)0ULL);
    }
    {
        gm_l10 = ((uint8_t)0);
    }
    {
        {
            gm_b16_8 t25 = gm_l4;
            gm_l11 = t25;
        }
        {
            gm_l12 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t27 = gm_l12;
                uint64_t t28 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                uint8_t t29 = (t27 < t28);
                if (!t29) goto lb26;
                {
                    uint64_t t32 = gm_l12;
                    uint64_t t33 = (uintptr_t)t32;
                    uint64_t t34 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                    gm_t7te2tdpebbt4 t37 = GM_LD(gm_t7te2tdpebbt4, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l11)) + t33 * 144));
                    F.l13 = t37;
                    {
                        uint8_t t40;
                        uint8_t t43 = GM_LD(uint8_t, ((((uint8_t *)&F.l13) + 0) + 0));
                        if (t43 != ((uint8_t)2ULL)) goto mn42;
                        t40 = ((uint8_t)1);
                        goto me41;
mn42:;
                        t40 = ((uint8_t)0);
                        goto me41;
mn44:;
                        gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 89, 12);
me41:;
                        if (!t40) goto ie39;
                        {
                            gm_l10 = ((uint8_t)1);
                        }
                        goto ix39;
ie39:;
ix39:;
                    }
                    goto me35;
mn36:;
                    gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 88, 5);
me35:;
                }
lc26:;
                {
                    uint64_t t47 = gm_l12;
                    uint64_t r48; bool o49 = __builtin_add_overflow(t47, ((uint64_t)1ULL), &r48);
                    uint64_t t50 = r48;
                    gm_l12 = t50;
                }
            }
lb26:;
        }
    }
    {
        gm_f_list_ir0h3tc_2((void *)&F.l14, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_l15 = ((uint8_t)0);
    }
    {
        uint64_t t52 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
        uint8_t t53 = ((t52 == ((uint64_t)1ULL)));
        if (!t53) goto ie51;
        uint64_t t54 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        uint8_t t55 = (!(t54 == ((uint64_t)1ULL)));
        if (!t55) goto ie51;
        uint64_t t56 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t57 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
        if (t56 >= (uintptr_t)(t57)) gm_trap_index(t56, t57, gm_src7nt72u848oha, 96, 67);
        uint8_t t58 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t56 * 144) + 0) + 0));
        if (t58 != ((uint8_t)0ULL)) goto ie51;
        gm_tc3oht4ksth9d t59 = GM_LD(gm_tc3oht4ksth9d, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t56 * 144) + 0) + 8));
        F.l16 = t59;
        {
            void * t61 = gm_l0;
            void * t62 = (void *)((uint8_t *)&F.l16);
            gm_tkn42u7na3t81 t63;
            memset(&t63, 0, sizeof t63);
            void * t64 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t61, t62, t63);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l17 = t64;
        }
        {
            void * t65 = gm_l0;
            void * t66 = (void *)(((uint8_t *)t65) + 96);
            void * t67 = gm_l0;
            void * t68 = GM_LD(void *, (((uint8_t *)t67) + 0));
            void * t69 = gm_l17;
            gm_t88rcucg1b7uk t70 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t69) + 0));
            gm_t88rcucg1b7uk t71 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t66, t68, t70);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l18 = t71;
        }
        {
            void * t72 = gm_l0;
            void * t73 = GM_LD(void *, (((uint8_t *)t72) + 0));
            void * t74 = (void *)(((uint8_t *)t73) + 8);
            gm_t88rcucg1b7uk t75 = gm_l18;
            gm_t53at80pfdus3 t76;
            gm_f_kind_jcnt4hm((void *)&t76, t74, GM_CONV(gm_csguankko4lmhd, t75));
            gm_th2lqbo77pvsc t77;
            uint8_t t83 = GM_LD(uint8_t, (((uint8_t *)&t76) + 0));
            switch (t83) { case ((uint8_t)0ULL): goto ma81; case ((uint8_t)1ULL): goto ma81; case ((uint8_t)2ULL): goto ma81; case ((uint8_t)3ULL): goto ma81; case ((uint8_t)4ULL): goto ma81; case ((uint8_t)5ULL): goto ma81; case ((uint8_t)6ULL): goto ma81; case ((uint8_t)7ULL): goto ma81; case ((uint8_t)8ULL): goto ma81; case ((uint8_t)9ULL): goto ma81; case ((uint8_t)10ULL): goto ma81; case ((uint8_t)11ULL): goto ma80; case ((uint8_t)12ULL): goto ma79; case ((uint8_t)13ULL): goto ma81; case ((uint8_t)14ULL): goto ma81; case ((uint8_t)15ULL): goto ma81; case ((uint8_t)16ULL): goto ma81; case ((uint8_t)17ULL): goto ma81; case ((uint8_t)18ULL): goto ma81; case ((uint8_t)19ULL): goto ma81; case ((uint8_t)20ULL): goto ma81; case ((uint8_t)21ULL): goto ma81; case ((uint8_t)22ULL): goto ma81; case ((uint8_t)23ULL): goto ma81; case ((uint8_t)24ULL): goto ma81; case ((uint8_t)25ULL): goto ma81; case ((uint8_t)26ULL): goto ma81; default: goto ma81; }
ma79:;
            uint8_t t85 = GM_LD(uint8_t, (((uint8_t *)&t76) + 0));
            if (t85 != ((uint8_t)12ULL)) goto mn84;
            gm_b16_8 t86;
            GM_ST(void *, (uint8_t *)&t86, (void *)(gm_sto8li44rv2ou));
            GM_ST(uintptr_t, (uint8_t *)&t86 + 8, (uintptr_t)(7));
            gm_th2lqbo77pvsc t87;
            GM_ST(uint8_t, (uint8_t *)&t87 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t87 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t87 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t87), t86);
            ((uint8_t *)&t87)[16] = 1;
            t77 = t87;
            goto me78;
mn84:;
ma80:;
            uint8_t t89 = GM_LD(uint8_t, (((uint8_t *)&t76) + 0));
            if (t89 != ((uint8_t)11ULL)) goto mn88;
            gm_tnnjeerolos0a t90 = GM_LD(gm_tnnjeerolos0a, ((((uint8_t *)&t76) + 8) + 12));
            gm_l19 = t90;
            gm_th2lqbo77pvsc t91;
            gm_tnnjeerolos0a t93 = gm_l19;
            gm_tnnjeerolos0a t94;
            GM_ST(uint8_t, (uint8_t *)&t94 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t94 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t94 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t94 + 0), ((uint8_t)0ULL));
            gm_tnnjeerolos0a t95;
            GM_ST(uint8_t, (uint8_t *)&t95 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t95 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t95 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t95 + 0), ((uint8_t)2ULL));
            gm_tcf2f71ra2nps t96;
            GM_ST(gm_tnnjeerolos0a, ((uint8_t *)&t96 + 0), t94);
            GM_ST(gm_tnnjeerolos0a, ((uint8_t *)&t96 + 8), t95);
            uint8_t t97 = 0;
            if (gm_eqb2ul45tidi(((uint8_t *)&t96) + 0, ((uint8_t *)&t93))) t97 = 1;
            if (gm_eqb2ul45tidi(((uint8_t *)&t96) + 8, ((uint8_t *)&t93))) t97 = 1;
            uint8_t t98 = (uint8_t)(t97);
            if (!t98) goto ie92;
            gm_b16_8 t100;
            GM_ST(void *, (uint8_t *)&t100, (void *)(gm_srrqh3pev0igg));
            GM_ST(uintptr_t, (uint8_t *)&t100 + 8, (uintptr_t)(8));
            gm_b16_8 t101 = t100;
            gm_th2lqbo77pvsc t102;
            GM_ST(uint8_t, (uint8_t *)&t102 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t102 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t102 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t102), t101);
            ((uint8_t *)&t102)[16] = 1;
            t91 = t102;
            goto ix92;
ie92:;
            gm_th2lqbo77pvsc t104;
            memset(&t104, 0, sizeof t104);
            gm_th2lqbo77pvsc t105 = t104;
            t91 = t105;
ix92:;
            t77 = t91;
            goto me78;
mn88:;
ma81:;
            gm_th2lqbo77pvsc t107;
            memset(&t107, 0, sizeof t107);
            t77 = t107;
            goto me78;
mn106:;
ma82:;
            gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 99, 25);
me78:;
            gm_l20 = t77;
        }
        {
            if (!((((uint8_t *)&gm_l20))[16] != 0)) goto ie108;
            gm_b16_8 t109 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l20));
            gm_l21 = t109;
            {
                void * t111 = gm_l0;
                gm_tl874t2atnlh3 t112 = gm_l5;
                gm_tfeab279c16rr t113;
                void * t114 = gm_l0;
                gm_t88rcucg1b7uk t115 = gm_l1;
                gm_b16_8 t116 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t114, t115);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t113 + 0, t116);
                gm_b16_8 t117 = gm_l21;
                GM_ST(gm_b16_8, (uint8_t *)&t113 + 16, t117);
                void * t118 = gm_l0;
                gm_t88rcucg1b7uk t119 = gm_l1;
                gm_b16_8 t120 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t118, t119);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t113 + 32, t120);
                gm_tabb710d9mq2e t121;
                gm_f_format_6nmb3uf((void *)&t121, (void *)NULL, cx, NULL, &t113);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t121))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t121) + 8, gm_src7nt72u848oha, 105, 28);
                gm_b16_8 t122 = GM_LD(gm_b16_8, (((uint8_t *)&t121) + 8));
                gm_b16_8 t123 = GM_LD(gm_b16_8, ((uint8_t *)&t122));
                gm_b0_1 t124 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t111, t112, t123);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t125 = gm_l0;
                void * t126 = GM_LD(void *, (((uint8_t *)t125) + 0));
                gm_tl874t2atnlh3 t127 = gm_l5;
                void * t128 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t126, t127);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t129 = 0;
                F.ret = t128;
                goto gm_fret;
            }
            goto ix108;
ie108:;
ix108:;
        }
        {
            uint64_t t131 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            uint8_t t132 = (t131 > ((uint64_t)0ULL));
            if (!t132) goto ie130;
            {
                uint64_t t134 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t135 = GM_LD(uintptr_t, (((uint8_t *)&F.l6) + 0) + 8);
                if (t134 >= (uintptr_t)(t135)) gm_trap_index(t134, t135, gm_src7nt72u848oha, 109, 13);
                uint8_t *t136 = ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l6) + 0)) + t134 * 8);
                void * t137 = gm_l0;
                void * t138 = gm_l17;
                uint64_t t139 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t140 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                if (t139 >= (uintptr_t)(t140)) gm_trap_index(t139, t140, gm_src7nt72u848oha, 109, 43);
                gm_t88rcucg1b7uk t141 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t139 * 4));
                void * t142 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t137, t138, t141);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(void *, t136, t142);
            }
            {
                gm_l9 = ((uint64_t)1ULL);
            }
            goto ix130;
ie130:;
ix130:;
        }
        goto ix51;
ie51:;
ix51:;
    }
    {
        {
            gm_b16_8 t144 = gm_l4;
            uint64_t t145 = GM_LD(uintptr_t, ((uint8_t *)&t144) + 8);
            void * t146 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t144)));
            uint64_t t147 = gm_l9;
            uint64_t t148 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            uint64_t t149 = (t147 < t148 ? t147 : t148);
            uint64_t t150 = (uintptr_t)t149;
            if (t150 > (uintptr_t)t145 || (uintptr_t)t145 > (uintptr_t)t145) gm_trap_slice(t150, (uintptr_t)t145, t145, gm_src7nt72u848oha, 113, 16);
            gm_b16_8 t151;
            GM_ST(void *, (uint8_t *)&t151, (void *)(((uint8_t *)t146 + t150 * 144)));
            GM_ST(uintptr_t, (uint8_t *)&t151 + 8, (uintptr_t)((uintptr_t)t145 - t150));
            gm_l22 = t151;
        }
        {
            gm_l23 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t153 = gm_l23;
                uint64_t t154 = GM_LD(uintptr_t, ((uint8_t *)&gm_l22) + 8);
                uint8_t t155 = (t153 < t154);
                if (!t155) goto lb152;
                {
                    uint64_t t158 = gm_l23;
                    uint64_t t159 = (uintptr_t)t158;
                    uint64_t t160 = GM_LD(uintptr_t, ((uint8_t *)&gm_l22) + 8);
                    gm_t7te2tdpebbt4 t163 = GM_LD(gm_t7te2tdpebbt4, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l22)) + t159 * 144));
                    F.l24 = t163;
                    {
                        gm_f_list_5kthfl8((void *)&F.l25, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_ll156;
                        }
                    }
                    {
                        uint8_t t170 = GM_LD(uint8_t, ((((uint8_t *)&F.l24) + 0) + 0));
                        switch (t170) { case ((uint8_t)0ULL): goto ma166; case ((uint8_t)1ULL): goto ma167; case ((uint8_t)2ULL): goto ma168; default: goto ma169; }
ma166:;
                        uint8_t t172 = GM_LD(uint8_t, ((((uint8_t *)&F.l24) + 0) + 0));
                        if (t172 != ((uint8_t)0ULL)) goto mn171;
                        gm_tc3oht4ksth9d t173 = GM_LD(gm_tc3oht4ksth9d, ((((uint8_t *)&F.l24) + 0) + 8));
                        F.l26 = t173;
                        {
                            uint64_t t176 = gm_l9;
                            uint64_t t177 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                            uint8_t t178 = (t176 >= t177);
                            if (!t178) goto ie175;
                            {
                                void * t180 = gm_l0;
                                gm_tl874t2atnlh3 t181 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l26) + 72));
                                void * t182 = gm_l0;
                                gm_t88rcucg1b7uk t183 = gm_l1;
                                gm_b16_8 t184 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t182, t183);
                                if (gm_unw.kind) {
                                    goto gm_ll156;
                                }
                                gm_tnpifv20irnph t185;
                                GM_ST(gm_b16_8, ((uint8_t *)&t185 + 0), t184);
                                gm_tabb710d9mq2e t186;
                                gm_f_format_bm4rhr0((void *)&t186, (void *)NULL, cx, NULL, t185);
                                if (gm_unw.kind) {
                                    goto gm_ll156;
                                }
                                if (((((uint8_t *)&t186))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t186) + 8, gm_src7nt72u848oha, 118, 38);
                                gm_b16_8 t187 = GM_LD(gm_b16_8, (((uint8_t *)&t186) + 8));
                                gm_b16_8 t188 = GM_LD(gm_b16_8, ((uint8_t *)&t187));
                                gm_b0_1 t189 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t180, t181, t188);
                                if (gm_unw.kind) {
                                    goto gm_ll156;
                                }
                            }
                            {
                                goto lc152;
                            }
                            goto ix175;
ie175:;
ix175:;
                        }
                        {
                            void * t190 = (void *)((uint8_t *)&F.l25);
                            gm_tm7a6rksvs6n5 t191;
                            memset((uint8_t *)&t191 + 44, 0, 4);
                            gm_th2lqbo77pvsc t192;
                            memset(&t192, 0, sizeof t192);
                            GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&t191 + 0, t192);
                            void * t193 = gm_l0;
                            void * t194 = (void *)((uint8_t *)&F.l26);
                            uint64_t t195 = gm_l9;
                            uint64_t t196 = (uintptr_t)t195;
                            uint64_t t197 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                            if (t196 >= (uintptr_t)(t197)) gm_trap_index(t196, t197, gm_src7nt72u848oha, 121, 76);
                            gm_t88rcucg1b7uk t198 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t196 * 4));
                            void * t199 = gm_f_field_value_19vleud((void *)NULL, cx, NULL, t193, t194, t198);
                            if (gm_unw.kind) {
                                goto gm_ll156;
                            }
                            GM_ST(void *, (uint8_t *)&t191 + 24, t199);
                            gm_tl874t2atnlh3 t200 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l26) + 72));
                            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t191 + 32, t200);
                            gm_tftb1odc739t5 t201 = gm_f_push_9a0n4ld(t190, &t191);
                            if (((((uint8_t *)&t201))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t201) + 1, gm_src7nt72u848oha, 121, 17);
                            gm_b0_1 t202 = GM_LD(gm_b0_1, (((uint8_t *)&t201) + 1));
                        }
                        goto me165;
mn171:;
ma167:;
                        uint8_t t204 = GM_LD(uint8_t, ((((uint8_t *)&F.l24) + 0) + 0));
                        if (t204 != ((uint8_t)1ULL)) goto mn203;
                        gm_t08olq97rscn3 t205 = GM_LD(gm_t08olq97rscn3, (((((uint8_t *)&F.l24) + 0) + 8) + 0));
                        gm_l27 = t205;
                        gm_tc3oht4ksth9d t206 = GM_LD(gm_tc3oht4ksth9d, (((((uint8_t *)&F.l24) + 0) + 8) + 32));
                        F.l28 = t206;
                        {
                            gm_b16_8 t208 = gm_l3;
                            gm_b16_8 t209 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l27) + 0));
                            gm_tf5srh4mp4b3i t210 = gm_f_index_of_o9r57ha(t208, t209);
                            uint64_t t211;
                            if (((((uint8_t *)&t210))[8] != 0)) {
                                uint64_t t212 = GM_LD(uint64_t, (((uint8_t *)&t210) + 0));
                                t211 = t212;
                            } else {
                                {
                                    void * t214 = gm_l0;
                                    gm_tl874t2atnlh3 t215 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l27) + 16));
                                    gm_t6j6smj7qoctu t216;
                                    void * t217 = gm_l0;
                                    gm_t88rcucg1b7uk t218 = gm_l1;
                                    gm_b16_8 t219 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t217, t218);
                                    if (gm_unw.kind) {
                                        goto gm_ll156;
                                    }
                                    GM_ST(gm_b16_8, (uint8_t *)&t216 + 0, t219);
                                    gm_b16_8 t220 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l27) + 0));
                                    GM_ST(gm_b16_8, (uint8_t *)&t216 + 16, t220);
                                    gm_tabb710d9mq2e t221;
                                    gm_f_format_3tnlp8q((void *)&t221, (void *)NULL, cx, NULL, &t216);
                                    if (gm_unw.kind) {
                                        goto gm_ll156;
                                    }
                                    if (((((uint8_t *)&t221))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t221) + 8, gm_src7nt72u848oha, 125, 38);
                                    gm_b16_8 t222 = GM_LD(gm_b16_8, (((uint8_t *)&t221) + 8));
                                    gm_b16_8 t223 = GM_LD(gm_b16_8, ((uint8_t *)&t222));
                                    gm_b0_1 t224 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t214, t215, t223);
                                    if (gm_unw.kind) {
                                        goto gm_ll156;
                                    }
                                }
                                goto lc152;
                            }
                            gm_l29 = t211;
                        }
                        {
                            void * t225 = (void *)((uint8_t *)&F.l25);
                            gm_tm7a6rksvs6n5 t226;
                            memset((uint8_t *)&t226 + 44, 0, 4);
                            gm_b16_8 t227 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l27) + 0));
                            gm_th2lqbo77pvsc t228;
                            GM_ST(uint8_t, (uint8_t *)&t228 + 17, 0);
                            GM_ST(uint16_t, (uint8_t *)&t228 + 18, 0);
                            GM_ST(uint32_t, (uint8_t *)&t228 + 20, 0);
                            GM_ST(gm_b16_8, ((uint8_t *)&t228), t227);
                            ((uint8_t *)&t228)[16] = 1;
                            GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&t226 + 0, t228);
                            void * t229 = gm_l0;
                            void * t230 = (void *)((uint8_t *)&F.l28);
                            uint64_t t231 = gm_l29;
                            uint64_t t232 = (uintptr_t)t231;
                            uint64_t t233 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                            if (t232 >= (uintptr_t)(t233)) gm_trap_index(t232, t233, gm_src7nt72u848oha, 128, 78);
                            gm_t88rcucg1b7uk t234 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t232 * 4));
                            void * t235 = gm_f_field_value_19vleud((void *)NULL, cx, NULL, t229, t230, t234);
                            if (gm_unw.kind) {
                                goto gm_ll156;
                            }
                            GM_ST(void *, (uint8_t *)&t226 + 24, t235);
                            gm_tl874t2atnlh3 t236 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l27) + 16));
                            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t226 + 32, t236);
                            gm_tftb1odc739t5 t237 = gm_f_push_9a0n4ld(t225, &t226);
                            if (((((uint8_t *)&t237))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t237) + 1, gm_src7nt72u848oha, 128, 17);
                            gm_b0_1 t238 = GM_LD(gm_b0_1, (((uint8_t *)&t237) + 1));
                        }
                        goto me165;
mn203:;
ma168:;
                        uint8_t t240 = GM_LD(uint8_t, ((((uint8_t *)&F.l24) + 0) + 0));
                        if (t240 != ((uint8_t)2ULL)) goto mn239;
                        gm_tc3oht4ksth9d t241 = GM_LD(gm_tc3oht4ksth9d, ((((uint8_t *)&F.l24) + 0) + 8));
                        F.l30 = t241;
                        {
                            void * t243 = gm_l0;
                            void * t244 = (void *)((uint8_t *)&F.l30);
                            void * t245 = (void *)((uint8_t *)&F.l14);
                            gm_th2lqbo77pvsc t246;
                            gm_f_spread_parts_e62q2l2((void *)&t246, (void *)NULL, cx, NULL, t243, t244, t245);
                            if (gm_unw.kind) {
                                goto gm_ll156;
                            }
                            gm_b16_8 t247;
                            if (((((uint8_t *)&t246))[16] != 0)) {
                                gm_b16_8 t248 = GM_LD(gm_b16_8, (((uint8_t *)&t246) + 0));
                                t247 = t248;
                            } else {
                                {
                                    gm_l15 = ((uint8_t)1);
                                }
                                goto lc152;
                            }
                            gm_l31 = t247;
                        }
                        {
                            {
                                gm_b16_8 t251 = gm_l31;
                                gm_l32 = t251;
                            }
                            {
                                gm_l33 = ((uint64_t)0ULL);
                            }
                            {
                                for (;;) {
                                    uint64_t t253 = gm_l33;
                                    uint64_t t254 = GM_LD(uintptr_t, ((uint8_t *)&gm_l32) + 8);
                                    uint8_t t255 = (t253 < t254);
                                    if (!t255) goto lb252;
                                    {
                                        uint64_t t258 = gm_l33;
                                        uint64_t t259 = (uintptr_t)t258;
                                        uint64_t t260 = GM_LD(uintptr_t, ((uint8_t *)&gm_l32) + 8);
                                        gm_tpouv9r7lrri2 t263 = GM_LD(gm_tpouv9r7lrri2, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l32)) + t259 * 32));
                                        gm_l34 = t263;
                                        {
                                            void * t265 = (void *)((uint8_t *)&F.l25);
                                            gm_tm7a6rksvs6n5 t266;
                                            memset((uint8_t *)&t266 + 44, 0, 4);
                                            memcpy((uint8_t *)&t266 + 0, (((uint8_t *)&gm_l34) + 0), 24);
                                            void * t267 = GM_LD(void *, (((uint8_t *)&gm_l34) + 24));
                                            GM_ST(void *, (uint8_t *)&t266 + 24, t267);
                                            gm_tl874t2atnlh3 t268 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l30) + 72));
                                            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t266 + 32, t268);
                                            gm_tftb1odc739t5 t269 = gm_f_push_9a0n4ld(t265, &t266);
                                            if (((((uint8_t *)&t269))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t269) + 1, gm_src7nt72u848oha, 135, 31);
                                            gm_b0_1 t270 = GM_LD(gm_b0_1, (((uint8_t *)&t269) + 1));
                                        }
                                        goto me261;
mn262:;
                                        gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 135, 17);
me261:;
                                    }
lc252:;
                                    {
                                        uint64_t t272 = gm_l33;
                                        uint64_t r273; bool o274 = __builtin_add_overflow(t272, ((uint64_t)1ULL), &r273);
                                        uint64_t t275 = r273;
                                        gm_l33 = t275;
                                    }
                                }
lb252:;
                            }
                        }
                        goto me165;
mn239:;
ma169:;
                        gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 115, 9);
me165:;
                    }
                    {
                        {
                            gm_b16_8 t277 = GM_LD(gm_b16_8, (((uint8_t *)&F.l25) + 0));
                            gm_l35 = t277;
                        }
                        {
                            gm_l36 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t279 = gm_l36;
                                uint64_t t280 = GM_LD(uintptr_t, ((uint8_t *)&gm_l35) + 8);
                                uint8_t t281 = (t279 < t280);
                                if (!t281) goto lb278;
                                {
                                    uint64_t t284 = gm_l36;
                                    uint64_t t285 = (uintptr_t)t284;
                                    uint64_t t286 = GM_LD(uintptr_t, ((uint8_t *)&gm_l35) + 8);
                                    gm_tm7a6rksvs6n5 t289 = GM_LD(gm_tm7a6rksvs6n5, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l35)) + t285 * 48));
                                    F.l37 = t289;
                                    {
                                        gm_l38 = ((uint64_t)0ULL);
                                    }
                                    {
                                        if (!(((((uint8_t *)&F.l37) + 0))[16] != 0)) goto ie291;
                                        gm_b16_8 t292 = GM_LD(gm_b16_8, (((uint8_t *)&F.l37) + 0));
                                        gm_l39 = t292;
                                        {
                                            gm_b16_8 t294 = gm_l3;
                                            gm_b16_8 t295 = gm_l39;
                                            gm_tf5srh4mp4b3i t296 = gm_f_index_of_o9r57ha(t294, t295);
                                            uint64_t t297;
                                            if (((((uint8_t *)&t296))[8] != 0)) {
                                                uint64_t t298 = GM_LD(uint64_t, (((uint8_t *)&t296) + 0));
                                                t297 = t298;
                                            } else {
                                                {
                                                    void * t300 = gm_l0;
                                                    gm_tl874t2atnlh3 t301 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l37) + 32));
                                                    gm_t6j6smj7qoctu t302;
                                                    void * t303 = gm_l0;
                                                    gm_t88rcucg1b7uk t304 = gm_l1;
                                                    gm_b16_8 t305 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t303, t304);
                                                    if (gm_unw.kind) {
                                                        goto gm_ll282;
                                                    }
                                                    GM_ST(gm_b16_8, (uint8_t *)&t302 + 0, t305);
                                                    gm_b16_8 t306 = gm_l39;
                                                    GM_ST(gm_b16_8, (uint8_t *)&t302 + 16, t306);
                                                    gm_tabb710d9mq2e t307;
                                                    gm_f_format_3tnlp8q((void *)&t307, (void *)NULL, cx, NULL, &t302);
                                                    if (gm_unw.kind) {
                                                        goto gm_ll282;
                                                    }
                                                    if (((((uint8_t *)&t307))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t307) + 8, gm_src7nt72u848oha, 142, 39);
                                                    gm_b16_8 t308 = GM_LD(gm_b16_8, (((uint8_t *)&t307) + 8));
                                                    gm_b16_8 t309 = GM_LD(gm_b16_8, ((uint8_t *)&t308));
                                                    gm_b0_1 t310 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t300, t301, t309);
                                                    if (gm_unw.kind) {
                                                        goto gm_ll282;
                                                    }
                                                }
                                                goto lc278;
                                            }
                                            gm_l38 = t297;
                                        }
                                        goto ix291;
ie291:;
                                        {
                                            uint64_t t313 = gm_l9;
                                            uint64_t t314 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                                            uint8_t t315 = (t313 >= t314);
                                            if (!t315) goto ie312;
                                            {
                                                void * t317 = gm_l0;
                                                gm_tl874t2atnlh3 t318 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l37) + 32));
                                                void * t319 = gm_l0;
                                                gm_t88rcucg1b7uk t320 = gm_l1;
                                                gm_b16_8 t321 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t319, t320);
                                                if (gm_unw.kind) {
                                                    goto gm_ll282;
                                                }
                                                gm_tnpifv20irnph t322;
                                                GM_ST(gm_b16_8, ((uint8_t *)&t322 + 0), t321);
                                                gm_tabb710d9mq2e t323;
                                                gm_f_format_bm4rhr0((void *)&t323, (void *)NULL, cx, NULL, t322);
                                                if (gm_unw.kind) {
                                                    goto gm_ll282;
                                                }
                                                if (((((uint8_t *)&t323))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t323) + 8, gm_src7nt72u848oha, 147, 39);
                                                gm_b16_8 t324 = GM_LD(gm_b16_8, (((uint8_t *)&t323) + 8));
                                                gm_b16_8 t325 = GM_LD(gm_b16_8, ((uint8_t *)&t324));
                                                gm_b0_1 t326 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t317, t318, t325);
                                                if (gm_unw.kind) {
                                                    goto gm_ll282;
                                                }
                                            }
                                            {
                                                goto lb278;
                                            }
                                            goto ix312;
ie312:;
ix312:;
                                        }
                                        {
                                            uint64_t t327 = gm_l9;
                                            gm_l38 = t327;
                                        }
                                        {
                                            uint64_t t328 = gm_l9;
                                            uint64_t r329; bool o330 = __builtin_add_overflow(t328, ((uint64_t)1ULL), &r329);
                                            if (o330) gm_trap("integer overflow in `+`", gm_src7nt72u848oha, 151, 17);
                                            uint64_t t331 = r329;
                                            gm_l9 = t331;
                                        }
ix291:;
                                    }
                                    {
                                        uint64_t t333 = gm_l38;
                                        uint64_t t334 = (uintptr_t)t333;
                                        uint64_t t335 = GM_LD(uintptr_t, (((uint8_t *)&F.l6) + 0) + 8);
                                        if (t334 >= (uintptr_t)(t335)) gm_trap_index(t334, t335, gm_src7nt72u848oha, 153, 16);
                                        void * t336 = GM_LD(void *, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l6) + 0)) + t334 * 8));
                                        uint8_t t337;
                                        if ((GM_LD(void *, ((uint8_t *)&t336)) != 0)) goto mn339;
                                        t337 = ((uint8_t)0);
                                        goto me338;
mn339:;
                                        t337 = ((uint8_t)1);
                                        goto me338;
mn340:;
                                        gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 153, 16);
me338:;
                                        if (!t337) goto ie332;
                                        {
                                            void * t342 = gm_l0;
                                            gm_tl874t2atnlh3 t343 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l37) + 32));
                                            gm_tf1bjg81u0glv t344;
                                            uint64_t t345 = gm_l38;
                                            GM_ST(uint64_t, (uint8_t *)&t344 + 0, t345);
                                            void * t346 = gm_l0;
                                            gm_t88rcucg1b7uk t347 = gm_l1;
                                            gm_b16_8 t348 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t346, t347);
                                            if (gm_unw.kind) {
                                                goto gm_ll282;
                                            }
                                            GM_ST(gm_b16_8, (uint8_t *)&t344 + 8, t348);
                                            gm_tabb710d9mq2e t349;
                                            gm_f_format_t8pf89n((void *)&t349, (void *)NULL, cx, NULL, &t344);
                                            if (gm_unw.kind) {
                                                goto gm_ll282;
                                            }
                                            if (((((uint8_t *)&t349))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t349) + 8, gm_src7nt72u848oha, 153, 63);
                                            gm_b16_8 t350 = GM_LD(gm_b16_8, (((uint8_t *)&t349) + 8));
                                            gm_b16_8 t351 = GM_LD(gm_b16_8, ((uint8_t *)&t350));
                                            gm_b0_1 t352 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t342, t343, t351);
                                            if (gm_unw.kind) {
                                                goto gm_ll282;
                                            }
                                        }
                                        goto ix332;
ie332:;
ix332:;
                                    }
                                    {
                                        void * t353 = gm_l0;
                                        void * t354 = GM_LD(void *, (((uint8_t *)&F.l37) + 24));
                                        uint64_t t355 = gm_l38;
                                        uint64_t t356 = (uintptr_t)t355;
                                        uint64_t t357 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                                        if (t356 >= (uintptr_t)(t357)) gm_trap_index(t356, t357, gm_src7nt72u848oha, 154, 40);
                                        gm_t88rcucg1b7uk t358 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t356 * 4));
                                        void * t359 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t353, t354, t358);
                                        if (gm_unw.kind) {
                                            goto gm_ll282;
                                        }
                                        gm_l40 = t359;
                                    }
                                    {
                                        uint64_t t360 = gm_l38;
                                        uint64_t t361 = (uintptr_t)t360;
                                        uint64_t t362 = GM_LD(uintptr_t, (((uint8_t *)&F.l6) + 0) + 8);
                                        if (t361 >= (uintptr_t)(t362)) gm_trap_index(t361, t362, gm_src7nt72u848oha, 155, 13);
                                        uint8_t *t363 = ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l6) + 0)) + t361 * 8);
                                        void * t364;
                                        uint8_t t366 = gm_l10;
                                        if (!t366) goto ie365;
                                        void * t368 = gm_l0;
                                        void * t369 = gm_l40;
                                        void * t370 = (void *)((uint8_t *)&F.l14);
                                        void * t371 = gm_f_bind_temp_sa5rcbr((void *)NULL, cx, NULL, t368, t369, t370);
                                        if (gm_unw.kind) {
                                            goto gm_ll282;
                                        }
                                        void * t372 = t371;
                                        t364 = t372;
                                        goto ix365;
ie365:;
                                        void * t374 = gm_l40;
                                        void * t375 = t374;
                                        t364 = t375;
ix365:;
                                        GM_ST(void *, t363, t364);
                                    }
                                    goto me287;
mn288:;
                                    gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 138, 9);
me287:;
                                }
lc278:;
                                {
                                    uint64_t t377 = gm_l36;
                                    uint64_t r378; bool o379 = __builtin_add_overflow(t377, ((uint64_t)1ULL), &r378);
                                    uint64_t t380 = r378;
                                    gm_l36 = t380;
                                }
                            }
gm_ll282:;
                            if (gm_unw.frame == &F.h && gm_unw.label == 4) {
                                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb278; }
                                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc278; }
                            }
                            goto gm_ll156;
lb278:;
                        }
                    }
                    goto me161;
mn162:;
                    gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 113, 5);
me161:;
                }
lc152:;
                {
                    uint64_t t382 = gm_l23;
                    uint64_t r383; bool o384 = __builtin_add_overflow(t382, ((uint64_t)1ULL), &r383);
                    uint64_t t385 = r383;
                    gm_l23 = t385;
                }
            }
gm_ll156:;
            if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb152; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc152; }
            }
            goto gm_lad;
lb152:;
        }
    }
    {
        gm_f_list_vp1djv1_2((void *)&F.l41, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_l43 = ((uint64_t)0ULL);
        }
        {
            uint64_t t387 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            gm_l42 = t387;
        }
        {
            for (;;) {
                uint64_t t389 = gm_l43;
                uint64_t t390 = gm_l42;
                uint8_t t391 = (t389 < t390);
                if (!t391) goto lb388;
                {
                    uint64_t t394 = gm_l43;
                    uint64_t t397 = GM_LD(uint64_t, ((uint8_t *)&t394));
                    gm_l44 = t397;
                    {
                        uint64_t t400 = gm_l44;
                        uint64_t t401 = (uintptr_t)t400;
                        uint64_t t402 = GM_LD(uintptr_t, (((uint8_t *)&F.l6) + 0) + 8);
                        if (t401 >= (uintptr_t)(t402)) gm_trap_index(t401, t402, gm_src7nt72u848oha, 160, 17);
                        void * t403 = GM_LD(void *, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l6) + 0)) + t401 * 8));
                        if (!(GM_LD(void *, ((uint8_t *)&t403)) != 0)) goto ie399;
                        void * t404 = GM_LD(void *, ((uint8_t *)&t403));
                        gm_l45 = t404;
                        {
                            void * t406 = (void *)((uint8_t *)&F.l41);
                            void * t407 = gm_l45;
                            gm_tftb1odc739t5 t408 = gm_f_push_d943vgp_2(t406, &(*(gm_t69m56meqefs8 *)((uint8_t *)t407)));
                            if (((((uint8_t *)&t408))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t408) + 1, gm_src7nt72u848oha, 161, 13);
                            gm_b0_1 t409 = GM_LD(gm_b0_1, (((uint8_t *)&t408) + 1));
                        }
                        goto ix399;
ie399:;
                        {
                            uint8_t t412 = gm_l15;
                            uint8_t t413 = (uint8_t)!t412;
                            if (!t413) goto ie411;
                            {
                                void * t415 = gm_l0;
                                gm_tl874t2atnlh3 t416 = gm_l5;
                                gm_tf1bjg81u0glv t417;
                                uint64_t t418 = gm_l44;
                                GM_ST(uint64_t, (uint8_t *)&t417 + 0, t418);
                                void * t419 = gm_l0;
                                gm_t88rcucg1b7uk t420 = gm_l1;
                                gm_b16_8 t421 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t419, t420);
                                if (gm_unw.kind) {
                                    goto gm_ll392;
                                }
                                GM_ST(gm_b16_8, (uint8_t *)&t417 + 8, t421);
                                gm_tabb710d9mq2e t422;
                                gm_f_format_j4le8o0((void *)&t422, (void *)NULL, cx, NULL, &t417);
                                if (gm_unw.kind) {
                                    goto gm_ll392;
                                }
                                if (((((uint8_t *)&t422))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t422) + 8, gm_src7nt72u848oha, 163, 51);
                                gm_b16_8 t423 = GM_LD(gm_b16_8, (((uint8_t *)&t422) + 8));
                                gm_b16_8 t424 = GM_LD(gm_b16_8, ((uint8_t *)&t423));
                                gm_b0_1 t425 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t415, t416, t424);
                                if (gm_unw.kind) {
                                    goto gm_ll392;
                                }
                            }
                            goto ix411;
ie411:;
ix411:;
                        }
                        {
                            void * t426 = (void *)((uint8_t *)&F.l41);
                            void * t427 = gm_l0;
                            void * t428 = GM_LD(void *, (((uint8_t *)t427) + 0));
                            gm_tl874t2atnlh3 t429 = gm_l5;
                            void * t430 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t428, t429);
                            if (gm_unw.kind) {
                                goto gm_ll392;
                            }
                            gm_tftb1odc739t5 t431 = gm_f_push_d943vgp_2(t426, &(*(gm_t69m56meqefs8 *)((uint8_t *)t430)));
                            if (((((uint8_t *)&t431))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t431) + 1, gm_src7nt72u848oha, 164, 13);
                            gm_b0_1 t432 = GM_LD(gm_b0_1, (((uint8_t *)&t431) + 1));
                        }
ix399:;
                    }
                    goto me395;
mn396:;
                    gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 159, 5);
me395:;
                }
lc388:;
                {
                    uint64_t t434 = gm_l43;
                    uint64_t r435; bool o436 = __builtin_add_overflow(t434, ((uint64_t)1ULL), &r435);
                    uint64_t t437 = r435;
                    gm_l43 = t437;
                }
            }
gm_ll392:;
            if (gm_unw.frame == &F.h && gm_unw.label == 5) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb388; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc388; }
            }
            goto gm_lad;
lb388:;
        }
    }
    {
        gm_t88rcucg1b7uk t438 = gm_l1;
        gm_b16_8 t439 = GM_LD(gm_b16_8, (((uint8_t *)&F.l41) + 0));
        gm_teti411eujvf4 t440;
        memset(&t440, 0, sizeof t440);
        GM_ST(uint8_t, ((uint8_t *)&t440 + 0), ((uint8_t)35ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t440 + 8), t439);
        gm_tl874t2atnlh3 t441 = gm_l5;
        void * t442 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t438, &t440, t441);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l46 = t442;
    }
    {
        uint64_t t444 = GM_LD(uintptr_t, (((uint8_t *)&F.l14) + 0) + 8);
        uint8_t t445 = ((t444 == ((uint64_t)0ULL)));
        if (!t445) goto ie443;
        {
            void * t447 = gm_l46;
            uint8_t t448 = 0;
            F.ret = t447;
            goto gm_fret;
        }
        goto ix443;
ie443:;
ix443:;
    }
    {
        gm_t88rcucg1b7uk t449 = gm_l1;
        gm_b16_8 t450 = GM_LD(gm_b16_8, (((uint8_t *)&F.l14) + 0));
        void * t451 = gm_l46;
        gm_tl874t2atnlh3 t452 = gm_l5;
        void * t453 = gm_f_block_typed_e7svg7n((void *)NULL, cx, NULL, t449, t450, t451, t452);
        if (gm_unw.kind) {
            goto gm_lad;
        }
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
__attribute__((aligned(4))) void * gm_f_construct_variant_o6is81c(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, const void * a2, const void * a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_construct_variant_o6is81c F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* u */
    gm_t08olq97rscn3 gm_l2; /* name */
    gm_th2lqbo77pvsc gm_l3; /* args */
    gm_tl874t2atnlh3 gm_l4; /* span */
    gm_tep2drsl9408b gm_l5; /* vs */
    gm_tf5srh4mp4b3i gm_l6; /* idx */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* i */
    uint64_t gm_l11; /* i */
    gm_tkn42u7na3t81 gm_l12; /* payload_ty */
    void * gm_l13; /* payload */
    gm_t88rcucg1b7uk gm_l14; /* pt */
    gm_b16_8 gm_l15; /* a */
    gm_b16_8 gm_l16; /* elems */
    gm_b16_8 gm_l17; /* names */
    GM_STACK_CHECK(&F, 2243u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    memcpy(&gm_l3, a3, sizeof gm_l3);
    gm_l4 = a4;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        gm_t88rcucg1b7uk t4 = gm_l1;
        gm_tdlpv0q69lov1 t5;
        gm_f_union_variants_a7khaqp((void *)&t5, t3, GM_CONV(gm_csguankko4lmhd, t4));
        gm_tep2drsl9408b t6;
        if (((((uint8_t *)&t5))[24] != 0)) {
            t6 = (*(gm_tep2drsl9408b *)(((uint8_t *)&t5) + 0));
        } else {
            {
                void * t8 = gm_l0;
                gm_tl874t2atnlh3 t9 = gm_l4;
                gm_t6j6smj7qoctu t10;
                gm_b16_8 t11 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 0));
                GM_ST(gm_b16_8, (uint8_t *)&t10 + 0, t11);
                void * t12 = gm_l0;
                gm_t88rcucg1b7uk t13 = gm_l1;
                gm_b16_8 t14 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t12, t13);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t10 + 16, t14);
                gm_tabb710d9mq2e t15;
                gm_f_format_12rbmhd((void *)&t15, (void *)NULL, cx, NULL, &t10);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t15))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t15) + 8, gm_src7nt72u848oha, 20, 24);
                gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&t15) + 8));
                gm_b16_8 t17 = GM_LD(gm_b16_8, ((uint8_t *)&t16));
                gm_b0_1 t18 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t8, t9, t17);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            void * t19 = gm_l0;
            void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
            gm_tl874t2atnlh3 t21 = gm_l4;
            void * t22 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t20, t21);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t23 = 0;
            F.ret = t22;
            goto gm_fret;
        }
        gm_l5 = t6;
    }
    {
        gm_tf5srh4mp4b3i t24;
        memset(&t24, 0, sizeof t24);
        gm_l6 = t24;
    }
    {
        {
            gm_b16_8 t26 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 0));
            gm_b16_8 t27 = GM_LD(gm_b16_8, ((uint8_t *)&t26));
            gm_l7 = t27;
        }
        {
            gm_l8 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t29 = gm_l8;
                uint64_t t30 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                uint8_t t31 = (t29 < t30);
                if (!t31) goto lb28;
                {
                    gm_t3l8idb2m6c1r t34;
                    uint64_t t35 = gm_l8;
                    GM_ST(uint64_t, (uint8_t *)&t34 + 0, t35);
                    uint64_t t36 = gm_l8;
                    uint64_t t37 = (uintptr_t)t36;
                    uint64_t t38 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                    memcpy((uint8_t *)&t34 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t37 * 72), 72);
                    uint64_t t41 = GM_LD(uint64_t, (((uint8_t *)&t34) + 0));
                    gm_l9 = t41;
                    F.l10 = (*(gm_tckdobunaulee *)(((uint8_t *)&t34) + 8));
                    {
                        gm_b16_8 t44 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 0));
                        gm_b16_8 t45 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 0));
                        uint8_t t46 = (gm_eq0iqiubfm7e(((uint8_t *)&t44), ((uint8_t *)&t45)));
                        if (!t46) goto ie43;
                        {
                            uint64_t t48 = gm_l9;
                            gm_tf5srh4mp4b3i t49;
                            GM_ST(uint8_t, (uint8_t *)&t49 + 9, 0);
                            GM_ST(uint16_t, (uint8_t *)&t49 + 10, 0);
                            GM_ST(uint32_t, (uint8_t *)&t49 + 12, 0);
                            GM_ST(uint64_t, ((uint8_t *)&t49), t48);
                            ((uint8_t *)&t49)[8] = 1;
                            gm_l6 = t49;
                        }
                        {
                            goto lb28;
                        }
                        goto ix43;
ie43:;
ix43:;
                    }
                    goto me39;
mn40:;
                    gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 24, 5);
me39:;
                }
lc28:;
                {
                    uint64_t t51 = gm_l8;
                    uint64_t r52; bool o53 = __builtin_add_overflow(t51, ((uint64_t)1ULL), &r52);
                    uint64_t t54 = r52;
                    gm_l8 = t54;
                }
            }
lb28:;
        }
    }
    {
        gm_tf5srh4mp4b3i t55 = gm_l6;
        uint64_t t56;
        if (((((uint8_t *)&t55))[8] != 0)) {
            uint64_t t57 = GM_LD(uint64_t, (((uint8_t *)&t55) + 0));
            t56 = t57;
        } else {
            {
                void * t59 = gm_l0;
                gm_tl874t2atnlh3 t60 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a2) + 16));
                gm_t6j6smj7qoctu t61;
                void * t62 = gm_l0;
                gm_t88rcucg1b7uk t63 = gm_l1;
                gm_b16_8 t64 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t62, t63);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t61 + 0, t64);
                gm_b16_8 t65 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 0));
                GM_ST(gm_b16_8, (uint8_t *)&t61 + 16, t65);
                gm_tabb710d9mq2e t66;
                gm_f_format_l002mid((void *)&t66, (void *)NULL, cx, NULL, &t61);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t66))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t66) + 8, gm_src7nt72u848oha, 31, 29);
                gm_b16_8 t67 = GM_LD(gm_b16_8, (((uint8_t *)&t66) + 8));
                gm_b16_8 t68 = GM_LD(gm_b16_8, ((uint8_t *)&t67));
                gm_b0_1 t69 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t59, t60, t68);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            void * t70 = gm_l0;
            void * t71 = GM_LD(void *, (((uint8_t *)t70) + 0));
            gm_tl874t2atnlh3 t72 = gm_l4;
            void * t73 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t71, t72);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t74 = 0;
            F.ret = t73;
            goto gm_fret;
        }
        gm_l11 = t56;
    }
    {
        uint64_t t75 = gm_l11;
        uint64_t t76 = (uintptr_t)t75;
        uint64_t t77 = GM_LD(uintptr_t, (((uint8_t *)&gm_l5) + 0) + 8);
        if (t76 >= (uintptr_t)(t77)) gm_trap_index(t76, t77, gm_src7nt72u848oha, 34, 19);
        gm_tkn42u7na3t81 t78 = GM_LD(gm_tkn42u7na3t81, (((uint8_t *)GM_LD(void *, (((uint8_t *)&gm_l5) + 0)) + t76 * 72) + 16));
        gm_l12 = t78;
    }
    {
        gm_l13 = ((void *)0);
    }
    {
        if (!((((uint8_t *)&gm_l12))[4] != 0)) goto ie79;
        gm_t88rcucg1b7uk t80 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l12));
        gm_l14 = t80;
        {
            gm_th2lqbo77pvsc t82 = gm_l3;
            gm_b16_8 t83;
            if (((((uint8_t *)&t82))[16] != 0)) {
                gm_b16_8 t84 = GM_LD(gm_b16_8, (((uint8_t *)&t82) + 0));
                t83 = t84;
            } else {
                {
                    void * t86 = gm_l0;
                    gm_tl874t2atnlh3 t87 = gm_l4;
                    gm_t6j6smj7qoctu t88;
                    gm_b16_8 t89 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 0));
                    GM_ST(gm_b16_8, (uint8_t *)&t88 + 0, t89);
                    gm_b16_8 t90 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 0));
                    GM_ST(gm_b16_8, (uint8_t *)&t88 + 16, t90);
                    gm_tabb710d9mq2e t91;
                    gm_f_format_9i81nmj((void *)&t91, (void *)NULL, cx, NULL, &t88);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t91))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t91) + 8, gm_src7nt72u848oha, 38, 28);
                    gm_b16_8 t92 = GM_LD(gm_b16_8, (((uint8_t *)&t91) + 8));
                    gm_b16_8 t93 = GM_LD(gm_b16_8, ((uint8_t *)&t92));
                    gm_b0_1 t94 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t86, t87, t93);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                void * t95 = gm_l0;
                void * t96 = GM_LD(void *, (((uint8_t *)t95) + 0));
                gm_tl874t2atnlh3 t97 = gm_l4;
                void * t98 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t96, t97);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t99 = 0;
                F.ret = t98;
                goto gm_fret;
            }
            gm_l15 = t83;
        }
        {
            void * t101 = gm_l0;
            void * t102 = GM_LD(void *, (((uint8_t *)t101) + 0));
            void * t103 = (void *)(((uint8_t *)t102) + 8);
            gm_t88rcucg1b7uk t104 = gm_l14;
            gm_t53at80pfdus3 t105;
            gm_f_kind_jcnt4hm((void *)&t105, t103, GM_CONV(gm_csguankko4lmhd, t104));
            uint8_t t106 = GM_LD(uint8_t, (((uint8_t *)&t105) + 0));
            if (t106 != ((uint8_t)12ULL)) goto ie100;
            gm_b16_8 t107 = GM_LD(gm_b16_8, ((((uint8_t *)&t105) + 8) + 0));
            gm_l16 = t107;
            gm_b16_8 t108 = GM_LD(gm_b16_8, ((((uint8_t *)&t105) + 8) + 16));
            gm_l17 = t108;
            {
                uint8_t t111 = 0;
                uint8_t t112 = 0;
                uint64_t t113 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                uint8_t t114 = (!(t113 == ((uint64_t)1ULL)));
                if (!t114) {
                    uint64_t t115 = GM_LD(uintptr_t, ((uint8_t *)&gm_l16) + 8);
                    uint8_t t116 = (!(t115 == ((uint64_t)1ULL)));
                    t112 = t116;
                } else t112 = 1;
                if (!t112) {
                    uint64_t t117 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t118 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                    if (t117 >= (uintptr_t)(t118)) gm_trap_index(t117, t118, gm_src7nt72u848oha, 43, 56);
                    uint8_t t119;
                    uint8_t t122 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l15)) + t117 * 144) + 0) + 0));
                    if (t122 != ((uint8_t)1ULL)) goto mn121;
                    t119 = ((uint8_t)1);
                    goto me120;
mn121:;
                    t119 = ((uint8_t)0);
                    goto me120;
mn123:;
                    gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 43, 56);
me120:;
                    t111 = t119;
                } else t111 = 1;
                if (!t111) goto ie110;
                {
                    void * t125 = gm_l0;
                    gm_t88rcucg1b7uk t126 = gm_l14;
                    gm_b16_8 t127 = gm_l16;
                    gm_b16_8 t128 = gm_l17;
                    gm_b16_8 t129 = gm_l15;
                    gm_tl874t2atnlh3 t130 = gm_l4;
                    void * t131 = gm_f_construct_tuple_6ee5ejf((void *)NULL, cx, NULL, t125, t126, t127, t128, t129, t130);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l13 = t131;
                }
                {
                    gm_t88rcucg1b7uk t132 = gm_l1;
                    uint64_t t133 = gm_l11;
                    uint32_t t134 = (uint32_t)t133;
                    void * t135 = gm_l13;
                    gm_ttggq7ng4kp3p t136;
                    GM_ST(uint32_t, (uint8_t *)&t136 + 4, 0);
                    GM_ST(uint32_t, ((uint8_t *)&t136 + 0), t134);
                    GM_ST(void *, ((uint8_t *)&t136 + 8), t135);
                    gm_teti411eujvf4 t137;
                    memset(&t137, 0, sizeof t137);
                    GM_ST(uint8_t, ((uint8_t *)&t137 + 0), ((uint8_t)38ULL));
                    GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t137 + 8), t136);
                    gm_tl874t2atnlh3 t138 = gm_l4;
                    void * t139 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t132, &t137, t138);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t140 = 0;
                    F.ret = t139;
                    goto gm_fret;
                }
                goto ix110;
ie110:;
ix110:;
            }
            goto ix100;
ie100:;
ix100:;
        }
        {
            uint8_t t142 = 0;
            uint64_t t143 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
            uint8_t t144 = (!(t143 == ((uint64_t)1ULL)));
            if (!t144) {
                uint64_t t145 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t146 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                if (t145 >= (uintptr_t)(t146)) gm_trap_index(t145, t146, gm_src7nt72u848oha, 48, 30);
                uint8_t t147;
                uint8_t t150 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l15)) + t145 * 144) + 0) + 0));
                if (t150 != ((uint8_t)0ULL)) goto mn149;
                t147 = ((uint8_t)0);
                goto me148;
mn149:;
                t147 = ((uint8_t)1);
                goto me148;
mn151:;
                gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 48, 30);
me148:;
                t142 = t147;
            } else t142 = 1;
            if (!t142) goto ie141;
            {
                void * t153 = gm_l0;
                gm_tl874t2atnlh3 t154 = gm_l4;
                void * t155 = gm_l0;
                gm_t88rcucg1b7uk t156 = gm_l14;
                gm_b16_8 t157 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t155, t156);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tnpifv20irnph t158;
                GM_ST(gm_b16_8, ((uint8_t *)&t158 + 0), t157);
                gm_tabb710d9mq2e t159;
                gm_f_format_e1r9br2((void *)&t159, (void *)NULL, cx, NULL, t158);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t159))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t159) + 8, gm_src7nt72u848oha, 49, 28);
                gm_b16_8 t160 = GM_LD(gm_b16_8, (((uint8_t *)&t159) + 8));
                gm_b16_8 t161 = GM_LD(gm_b16_8, ((uint8_t *)&t160));
                gm_b0_1 t162 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t153, t154, t161);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t163 = gm_l0;
                void * t164 = GM_LD(void *, (((uint8_t *)t163) + 0));
                gm_tl874t2atnlh3 t165 = gm_l4;
                void * t166 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t164, t165);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t167 = 0;
                F.ret = t166;
                goto gm_fret;
            }
            goto ix141;
ie141:;
ix141:;
        }
        {
            uint64_t t169 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t170 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
            if (t169 >= (uintptr_t)(t170)) gm_trap_index(t169, t170, gm_src7nt72u848oha, 52, 27);
            uint8_t t171 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l15)) + t169 * 144) + 0) + 0));
            if (t171 != ((uint8_t)0ULL)) goto ie168;
            gm_tc3oht4ksth9d t172 = GM_LD(gm_tc3oht4ksth9d, ((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l15)) + t169 * 144) + 0) + 8));
            F.l18 = t172;
            {
                void * t174 = gm_l0;
                void * t175 = (void *)((uint8_t *)&F.l18);
                gm_t88rcucg1b7uk t176 = gm_l14;
                void * t177 = gm_f_field_value_19vleud((void *)NULL, cx, NULL, t174, t175, t176);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l13 = t177;
            }
            goto ix168;
ie168:;
            {
                void * t179 = gm_l0;
                void * t180 = GM_LD(void *, (((uint8_t *)t179) + 0));
                gm_tl874t2atnlh3 t181 = gm_l4;
                void * t182 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t180, t181);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t183 = 0;
                F.ret = t182;
                goto gm_fret;
            }
ix168:;
        }
        goto ix79;
ie79:;
        uint8_t t185;
        if (((((uint8_t *)&gm_l3))[16] != 0)) goto mn187;
        t185 = ((uint8_t)0);
        goto me186;
mn187:;
        t185 = ((uint8_t)1);
        goto me186;
mn188:;
        gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 54, 15);
me186:;
        if (!t185) goto ie184;
        {
            void * t190 = gm_l0;
            gm_tl874t2atnlh3 t191 = gm_l4;
            gm_b16_8 t192 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 0));
            gm_tnpifv20irnph t193;
            GM_ST(gm_b16_8, ((uint8_t *)&t193 + 0), t192);
            gm_tabb710d9mq2e t194;
            gm_f_format_ktbu9b9((void *)&t194, (void *)NULL, cx, NULL, t193);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t194))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t194) + 8, gm_src7nt72u848oha, 55, 24);
            gm_b16_8 t195 = GM_LD(gm_b16_8, (((uint8_t *)&t194) + 8));
            gm_b16_8 t196 = GM_LD(gm_b16_8, ((uint8_t *)&t195));
            gm_b0_1 t197 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t190, t191, t196);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t198 = gm_l0;
            void * t199 = GM_LD(void *, (((uint8_t *)t198) + 0));
            gm_tl874t2atnlh3 t200 = gm_l4;
            void * t201 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t199, t200);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t202 = 0;
            F.ret = t201;
            goto gm_fret;
        }
        goto ix184;
ie184:;
ix184:;
ix79:;
    }
    {
        gm_t88rcucg1b7uk t203 = gm_l1;
        uint64_t t204 = gm_l11;
        uint32_t t205 = (uint32_t)t204;
        void * t206 = gm_l13;
        gm_ttggq7ng4kp3p t207;
        GM_ST(uint32_t, (uint8_t *)&t207 + 4, 0);
        GM_ST(uint32_t, ((uint8_t *)&t207 + 0), t205);
        GM_ST(void *, ((uint8_t *)&t207 + 8), t206);
        gm_teti411eujvf4 t208;
        memset(&t208, 0, sizeof t208);
        GM_ST(uint8_t, ((uint8_t *)&t208 + 0), ((uint8_t)38ULL));
        GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t208 + 8), t207);
        gm_tl874t2atnlh3 t209 = gm_l4;
        void * t210 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t203, &t208, t209);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t211 = 0;
        F.ret = t210;
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
__attribute__((aligned(4))) void * gm_f_default_value_lo723gi(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_t88rcucg1b7uk a3) {
    struct gm_fr_default_value_lo723gi F;
    void * gm_l0; /* cx */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* d */
    gm_t88rcucg1b7uk gm_l3; /* ty */
    void * gm_l4; /* tys */
    void * gm_l5; /* e */
    void * gm_l6; /* inner */
    uint8_t gm_l7; /* negative */
    void * gm_l8; /* x */
    uint8_t gm_l9; /* numeric */
    void * gm_l10; /* lit */
    void * gm_l12; /* lit */
    uint8_t gm_l14; /* b */
    GM_STACK_CHECK(&F, 1732u);
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
        gm_l4 = t4;
    }
    {
        void * t5 = gm_l2;
        gm_l5 = t5;
    }
    {
        for (;;) {
            void * t7 = gm_l5;
            uint8_t t8 = GM_LD(uint8_t, ((((uint8_t *)t7) + 8) + 0));
            if (t8 != ((uint8_t)9ULL)) goto lb6;
            void * t9 = GM_LD(void *, ((((uint8_t *)t7) + 8) + 8));
            gm_l6 = t9;
            {
                void * t12 = gm_l6;
                gm_l5 = t12;
            }
lc6:;
        }
lb6:;
    }
    {
        gm_l7 = ((uint8_t)0);
    }
    {
        void * t14 = gm_l5;
        uint8_t t15 = GM_LD(uint8_t, ((((uint8_t *)t14) + 8) + 0));
        if (t15 != ((uint8_t)12ULL)) goto ie13;
        uint8_t t16 = GM_LD(uint8_t, ((((((uint8_t *)t14) + 8) + 8) + 0) + 0));
        if (t16 != ((uint8_t)0ULL)) goto ie13;
        void * t17 = GM_LD(void *, (((((uint8_t *)t14) + 8) + 8) + 8));
        gm_l8 = t17;
        void * t18 = gm_l8;
        uint8_t t19;
        uint8_t t25 = GM_LD(uint8_t, ((((uint8_t *)t18) + 8) + 0));
        switch (t25) { case ((uint8_t)0ULL): goto ma21; case ((uint8_t)1ULL): goto ma22; case ((uint8_t)2ULL): goto ma23; case ((uint8_t)3ULL): goto ma23; case ((uint8_t)4ULL): goto ma23; case ((uint8_t)5ULL): goto ma23; case ((uint8_t)6ULL): goto ma23; case ((uint8_t)7ULL): goto ma23; case ((uint8_t)8ULL): goto ma23; case ((uint8_t)9ULL): goto ma23; case ((uint8_t)10ULL): goto ma23; case ((uint8_t)11ULL): goto ma23; case ((uint8_t)12ULL): goto ma23; case ((uint8_t)13ULL): goto ma23; case ((uint8_t)14ULL): goto ma23; case ((uint8_t)15ULL): goto ma23; case ((uint8_t)16ULL): goto ma23; case ((uint8_t)17ULL): goto ma23; case ((uint8_t)18ULL): goto ma23; case ((uint8_t)19ULL): goto ma23; case ((uint8_t)20ULL): goto ma23; case ((uint8_t)21ULL): goto ma23; case ((uint8_t)22ULL): goto ma23; case ((uint8_t)23ULL): goto ma23; case ((uint8_t)24ULL): goto ma23; case ((uint8_t)25ULL): goto ma23; case ((uint8_t)26ULL): goto ma23; case ((uint8_t)27ULL): goto ma23; case ((uint8_t)28ULL): goto ma23; case ((uint8_t)29ULL): goto ma23; case ((uint8_t)30ULL): goto ma23; case ((uint8_t)31ULL): goto ma23; case ((uint8_t)32ULL): goto ma23; case ((uint8_t)33ULL): goto ma23; case ((uint8_t)34ULL): goto ma23; case ((uint8_t)35ULL): goto ma23; case ((uint8_t)36ULL): goto ma23; case ((uint8_t)37ULL): goto ma23; case ((uint8_t)38ULL): goto ma23; case ((uint8_t)39ULL): goto ma23; case ((uint8_t)40ULL): goto ma23; case ((uint8_t)41ULL): goto ma23; case ((uint8_t)42ULL): goto ma23; case ((uint8_t)43ULL): goto ma23; case ((uint8_t)44ULL): goto ma23; case ((uint8_t)45ULL): goto ma23; case ((uint8_t)46ULL): goto ma23; case ((uint8_t)47ULL): goto ma23; case ((uint8_t)48ULL): goto ma23; case ((uint8_t)49ULL): goto ma23; case ((uint8_t)50ULL): goto ma23; default: goto ma23; }
ma21:;
        uint8_t t27 = GM_LD(uint8_t, ((((uint8_t *)t18) + 8) + 0));
        if (t27 != ((uint8_t)0ULL)) goto mn26;
        t19 = ((uint8_t)1);
        goto me20;
mn26:;
ma22:;
        uint8_t t29 = GM_LD(uint8_t, ((((uint8_t *)t18) + 8) + 0));
        if (t29 != ((uint8_t)1ULL)) goto mn28;
        t19 = ((uint8_t)1);
        goto me20;
mn28:;
ma23:;
        t19 = ((uint8_t)0);
        goto me20;
mn30:;
ma24:;
        gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 338, 38);
me20:;
        if (!t19) goto ie13;
        {
            gm_l7 = ((uint8_t)1);
        }
        {
            void * t32 = gm_l8;
            gm_l5 = t32;
        }
        goto ix13;
ie13:;
ix13:;
    }
    {
        uint8_t t33 = 0;
        void * t34 = gm_l4;
        gm_t88rcucg1b7uk t35 = gm_l3;
        uint8_t t36 = gm_f_is_integer_8d4f9ek(t34, GM_CONV(gm_csguankko4lmhd, t35));
        if (!t36) {
            void * t37 = gm_l4;
            gm_t88rcucg1b7uk t38 = gm_l3;
            gm_t53at80pfdus3 t39;
            gm_f_kind_jcnt4hm((void *)&t39, t37, GM_CONV(gm_csguankko4lmhd, t38));
            uint8_t t40;
            uint8_t t43 = GM_LD(uint8_t, (((uint8_t *)&t39) + 0));
            if (t43 != ((uint8_t)3ULL)) goto mn42;
            t40 = ((uint8_t)1);
            goto me41;
mn42:;
            t40 = ((uint8_t)0);
            goto me41;
mn44:;
            gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 342, 38);
me41:;
            t33 = t40;
        } else t33 = 1;
        gm_l9 = t33;
    }
    {
        void * t45 = gm_l5;
        uint8_t t53 = GM_LD(uint8_t, ((((uint8_t *)t45) + 8) + 0));
        switch (t53) { case ((uint8_t)0ULL): goto ma47; case ((uint8_t)1ULL): goto ma48; case ((uint8_t)2ULL): goto ma51; case ((uint8_t)3ULL): goto ma49; case ((uint8_t)4ULL): goto ma50; case ((uint8_t)5ULL): goto ma51; case ((uint8_t)6ULL): goto ma51; case ((uint8_t)7ULL): goto ma51; case ((uint8_t)8ULL): goto ma51; case ((uint8_t)9ULL): goto ma51; case ((uint8_t)10ULL): goto ma51; case ((uint8_t)11ULL): goto ma51; case ((uint8_t)12ULL): goto ma51; case ((uint8_t)13ULL): goto ma51; case ((uint8_t)14ULL): goto ma51; case ((uint8_t)15ULL): goto ma51; case ((uint8_t)16ULL): goto ma51; case ((uint8_t)17ULL): goto ma51; case ((uint8_t)18ULL): goto ma51; case ((uint8_t)19ULL): goto ma51; case ((uint8_t)20ULL): goto ma51; case ((uint8_t)21ULL): goto ma51; case ((uint8_t)22ULL): goto ma51; case ((uint8_t)23ULL): goto ma51; case ((uint8_t)24ULL): goto ma51; case ((uint8_t)25ULL): goto ma51; case ((uint8_t)26ULL): goto ma51; case ((uint8_t)27ULL): goto ma51; case ((uint8_t)28ULL): goto ma51; case ((uint8_t)29ULL): goto ma51; case ((uint8_t)30ULL): goto ma51; case ((uint8_t)31ULL): goto ma51; case ((uint8_t)32ULL): goto ma51; case ((uint8_t)33ULL): goto ma51; case ((uint8_t)34ULL): goto ma51; case ((uint8_t)35ULL): goto ma51; case ((uint8_t)36ULL): goto ma51; case ((uint8_t)37ULL): goto ma51; case ((uint8_t)38ULL): goto ma51; case ((uint8_t)39ULL): goto ma51; case ((uint8_t)40ULL): goto ma51; case ((uint8_t)41ULL): goto ma51; case ((uint8_t)42ULL): goto ma51; case ((uint8_t)43ULL): goto ma51; case ((uint8_t)44ULL): goto ma51; case ((uint8_t)45ULL): goto ma51; case ((uint8_t)46ULL): goto ma51; case ((uint8_t)47ULL): goto ma51; case ((uint8_t)48ULL): goto ma51; case ((uint8_t)49ULL): goto ma51; case ((uint8_t)50ULL): goto ma51; default: goto ma51; }
ma47:;
        uint8_t t55 = GM_LD(uint8_t, ((((uint8_t *)t45) + 8) + 0));
        if (t55 != ((uint8_t)0ULL)) goto mn54;
        void * t56 = GM_LD(void *, ((((uint8_t *)t45) + 8) + 8));
        gm_l10 = t56;
        uint8_t t58 = gm_l9;
        if (!t58) goto ie57;
        void * t59 = gm_l10;
        uint8_t t60;
        if ((((((uint8_t *)t59) + 48))[8] != 0)) goto mn62;
        t60 = ((uint8_t)1);
        goto me61;
mn62:;
        t60 = ((uint8_t)0);
        goto me61;
mn63:;
        gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 344, 37);
me61:;
        if (!t60) goto ie57;
        {
            void * t65 = gm_l10;
            gm_tbci5ospk5nnn t66 = GM_LD(gm_tbci5ospk5nnn, (((uint8_t *)t65) + 0));
            F.l11 = t66;
        }
        {
            uint8_t t68 = gm_l7;
            if (!t68) goto ie67;
            {
                gm_f_neg_agd5c5k((void *)&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l11)), (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l11)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix67;
ie67:;
ix67:;
        }
        {
            void * t71 = gm_l4;
            gm_t88rcucg1b7uk t72 = gm_l3;
            uint8_t t73 = gm_f_is_integer_8d4f9ek(t71, GM_CONV(gm_csguankko4lmhd, t72));
            if (!t73) goto ie70;
            {
                gm_t88rcucg1b7uk t75 = gm_l3;
                gm_tbci5ospk5nnn t76 = F.l11;
                gm_teti411eujvf4 t77;
                GM_ST(uint8_t, (uint8_t *)&t77 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t77 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t77 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t77 + 56, 0);
                GM_ST(uint8_t, ((uint8_t *)&t77 + 0), ((uint8_t)0ULL));
                GM_ST(gm_tbci5ospk5nnn, ((uint8_t *)&t77 + 8), t76);
                void * t78 = gm_l2;
                gm_tl874t2atnlh3 t79 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t78) + 72));
                void * t80 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t75, &t77, t79);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t81 = 0;
                F.ret = t80;
                goto gm_fret;
            }
            goto ix70;
ie70:;
ix70:;
        }
        {
            void * t82 = gm_l0;
            gm_tngtuiuskn938 t83;
            gm_f_from_int_ofjuoqh((void *)&t83, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l11)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t88rcucg1b7uk t84 = gm_l3;
            void * t85 = gm_l2;
            gm_tl874t2atnlh3 t86 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t85) + 72));
            void * t87 = gm_f_float_value_cr4h9gi((void *)NULL, cx, NULL, t82, &t83, t84, t86);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t88 = 0;
            F.ret = t87;
            goto gm_fret;
        }
        goto ix57;
ie57:;
ix57:;
        goto me46;
mn54:;
ma48:;
        uint8_t t90 = GM_LD(uint8_t, ((((uint8_t *)t45) + 8) + 0));
        if (t90 != ((uint8_t)1ULL)) goto mn89;
        void * t91 = GM_LD(void *, ((((uint8_t *)t45) + 8) + 8));
        gm_l12 = t91;
        void * t93 = gm_l4;
        gm_t88rcucg1b7uk t94 = gm_l3;
        gm_t53at80pfdus3 t95;
        gm_f_kind_jcnt4hm((void *)&t95, t93, GM_CONV(gm_csguankko4lmhd, t94));
        uint8_t t96;
        uint8_t t99 = GM_LD(uint8_t, (((uint8_t *)&t95) + 0));
        if (t99 != ((uint8_t)3ULL)) goto mn98;
        t96 = ((uint8_t)1);
        goto me97;
mn98:;
        t96 = ((uint8_t)0);
        goto me97;
mn100:;
        gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 350, 27);
me97:;
        if (!t96) goto ie92;
        void * t101 = gm_l12;
        uint8_t t102;
        if ((((((uint8_t *)t101) + 96))[1] != 0)) goto mn104;
        t102 = ((uint8_t)1);
        goto me103;
mn104:;
        t102 = ((uint8_t)0);
        goto me103;
mn105:;
        gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 350, 57);
me103:;
        if (!t102) goto ie92;
        void * t106 = gm_l12;
        uint8_t t107 = GM_LD(uint8_t, (((uint8_t *)t106) + 98));
        uint8_t t108 = (uint8_t)!t107;
        if (!t108) goto ie92;
        {
            void * t110 = gm_l12;
            gm_tngtuiuskn938 t111 = GM_LD(gm_tngtuiuskn938, (((uint8_t *)t110) + 0));
            F.l13 = t111;
        }
        {
            uint8_t t113 = gm_l7;
            if (!t113) goto ie112;
            {
                uint8_t t116 = gm_f_is_zero_5aept6u(&(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l13)));
                if (!t116) goto ie115;
                {
                    void * t119 = gm_l0;
                    void * t120 = GM_LD(void *, (((uint8_t *)t119) + 0));
                    uint64_t t121 = gm_l1;
                    void * t122 = gm_l2;
                    gm_t88rcucg1b7uk t123 = gm_l3;
                    uint8_t t124 = gm_f_default_const_k60bpu4((void *)NULL, cx, NULL, t120, t121, t122, t123);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t125 = (uint8_t)!t124;
                    if (!t125) goto ie118;
                    {
                        void * t127 = gm_l0;
                        void * t128 = GM_LD(void *, (((uint8_t *)t127) + 0));
                        void * t129 = gm_l2;
                        gm_tl874t2atnlh3 t130 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t129) + 72));
                        void * t131 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t128, t130);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t132 = 0;
                        F.ret = t131;
                        goto gm_fret;
                    }
                    goto ix118;
ie118:;
ix118:;
                }
                {
                    gm_t88rcucg1b7uk t133 = gm_l3;
                    gm_t08olq97rscn3 t134;
                    memset((uint8_t *)&t134 + 28, 0, 4);
                    gm_b16_8 t135;
                    GM_ST(void *, (uint8_t *)&t135, (void *)(gm_sna4vk4o059d0));
                    GM_ST(uintptr_t, (uint8_t *)&t135 + 8, (uintptr_t)(9));
                    GM_ST(gm_b16_8, (uint8_t *)&t134 + 0, t135);
                    void * t136 = gm_l2;
                    gm_tl874t2atnlh3 t137 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t136) + 72));
                    GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t134 + 16, t137);
                    gm_teti411eujvf4 t138;
                    GM_ST(uint8_t, (uint8_t *)&t138 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t138 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t138 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t138 + 40, 0);
                    GM_ST(uint64_t, (uint8_t *)&t138 + 48, 0);
                    GM_ST(uint64_t, (uint8_t *)&t138 + 56, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t138 + 0), ((uint8_t)14ULL));
                    GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t138 + 8), t134);
                    void * t139 = gm_l2;
                    gm_tl874t2atnlh3 t140 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t139) + 72));
                    void * t141 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t133, &t138, t140);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t142 = 0;
                    F.ret = t141;
                    goto gm_fret;
                }
                goto ix115;
ie115:;
ix115:;
            }
            {
                gm_f_neg_0u9uqg6((void *)&(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l13)), (void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l13)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix112;
ie112:;
ix112:;
        }
        {
            void * t143 = gm_l0;
            gm_t88rcucg1b7uk t144 = gm_l3;
            void * t145 = gm_l2;
            gm_tl874t2atnlh3 t146 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t145) + 72));
            void * t147 = gm_f_float_value_cr4h9gi((void *)NULL, cx, NULL, t143, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l13)), t144, t146);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t148 = 0;
            F.ret = t147;
            goto gm_fret;
        }
        goto ix92;
ie92:;
ix92:;
        goto me46;
mn89:;
ma49:;
        uint8_t t150 = GM_LD(uint8_t, ((((uint8_t *)t45) + 8) + 0));
        if (t150 != ((uint8_t)3ULL)) goto mn149;
        uint8_t t151 = GM_LD(uint8_t, ((((uint8_t *)t45) + 8) + 8));
        gm_l14 = t151;
        void * t153 = gm_l4;
        gm_t88rcucg1b7uk t154 = gm_l3;
        gm_t53at80pfdus3 t155;
        gm_f_kind_jcnt4hm((void *)&t155, t153, GM_CONV(gm_csguankko4lmhd, t154));
        uint8_t t156 = ((GM_LD(uint8_t, ((uint8_t *)&t155) + 0) == ((uint8_t)4ULL)));
        if (!t156) goto ie152;
        {
            gm_t88rcucg1b7uk t158 = gm_l3;
            uint8_t t159 = gm_l14;
            gm_teti411eujvf4 t160;
            memset(&t160, 0, sizeof t160);
            GM_ST(uint8_t, ((uint8_t *)&t160 + 0), ((uint8_t)4ULL));
            GM_ST(uint8_t, ((uint8_t *)&t160 + 8), t159);
            void * t161 = gm_l2;
            gm_tl874t2atnlh3 t162 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t161) + 72));
            void * t163 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t158, &t160, t162);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t164 = 0;
            F.ret = t163;
            goto gm_fret;
        }
        goto ix152;
ie152:;
ix152:;
        goto me46;
mn149:;
ma50:;
        uint8_t t166 = GM_LD(uint8_t, ((((uint8_t *)t45) + 8) + 0));
        if (t166 != ((uint8_t)4ULL)) goto mn165;
        void * t168 = gm_l4;
        gm_t88rcucg1b7uk t169 = gm_l3;
        gm_t53at80pfdus3 t170;
        gm_f_kind_jcnt4hm((void *)&t170, t168, GM_CONV(gm_csguankko4lmhd, t169));
        uint8_t t171;
        uint8_t t174 = GM_LD(uint8_t, (((uint8_t *)&t170) + 0));
        if (t174 != ((uint8_t)13ULL)) goto mn173;
        t171 = ((uint8_t)1);
        goto me172;
mn173:;
        t171 = ((uint8_t)0);
        goto me172;
mn175:;
        gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 362, 21);
me172:;
        if (!t171) goto ie167;
        {
            gm_t88rcucg1b7uk t177 = gm_l3;
            gm_teti411eujvf4 t178;
            memset(&t178, 0, sizeof t178);
            GM_ST(uint8_t, ((uint8_t *)&t178 + 0), ((uint8_t)7ULL));
            void * t179 = gm_l2;
            gm_tl874t2atnlh3 t180 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t179) + 72));
            void * t181 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t177, &t178, t180);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t182 = 0;
            F.ret = t181;
            goto gm_fret;
        }
        goto ix167;
ie167:;
ix167:;
        goto me46;
mn165:;
ma51:;
        goto me46;
mn183:;
ma52:;
        gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 343, 5);
me46:;
    }
    {
        void * t186 = gm_l0;
        void * t187 = GM_LD(void *, (((uint8_t *)t186) + 0));
        uint64_t t188 = gm_l1;
        void * t189 = gm_l2;
        gm_t88rcucg1b7uk t190 = gm_l3;
        uint8_t t191 = gm_f_default_const_k60bpu4((void *)NULL, cx, NULL, t187, t188, t189, t190);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t192 = (uint8_t)!t191;
        if (!t192) goto ie185;
        {
            void * t194 = gm_l0;
            void * t195 = GM_LD(void *, (((uint8_t *)t194) + 0));
            void * t196 = gm_l2;
            gm_tl874t2atnlh3 t197 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t196) + 72));
            void * t198 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t195, t197);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t199 = 0;
            F.ret = t198;
            goto gm_fret;
        }
        goto ix185;
ie185:;
ix185:;
    }
    {
        gm_t88rcucg1b7uk t200 = gm_l3;
        gm_t08olq97rscn3 t201;
        memset((uint8_t *)&t201 + 28, 0, 4);
        gm_b16_8 t202;
        GM_ST(void *, (uint8_t *)&t202, (void *)(gm_sna4vk4o059d0));
        GM_ST(uintptr_t, (uint8_t *)&t202 + 8, (uintptr_t)(9));
        GM_ST(gm_b16_8, (uint8_t *)&t201 + 0, t202);
        void * t203 = gm_l2;
        gm_tl874t2atnlh3 t204 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t203) + 72));
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t201 + 16, t204);
        gm_teti411eujvf4 t205;
        GM_ST(uint8_t, (uint8_t *)&t205 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t205 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t205 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t205 + 40, 0);
        GM_ST(uint64_t, (uint8_t *)&t205 + 48, 0);
        GM_ST(uint64_t, (uint8_t *)&t205 + 56, 0);
        GM_ST(uint8_t, ((uint8_t *)&t205 + 0), ((uint8_t)14ULL));
        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t205 + 8), t201);
        void * t206 = gm_l2;
        gm_tl874t2atnlh3 t207 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t206) + 72));
        void * t208 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t200, &t205, t207);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t209 = 0;
        F.ret = t208;
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
__attribute__((aligned(4))) void * gm_f_field_default_4ssmakm(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint64_t a2, gm_t88rcucg1b7uk a3) {
    struct gm_fr_field_default_4ssmakm F;
    void * gm_l0; /* cx */
    uint32_t gm_l1; /* id */
    uint64_t gm_l2; /* i */
    gm_t88rcucg1b7uk gm_l3; /* ty */
    gm_b16_8 gm_l4; /* defaults */
    void * gm_l5; /* d */
    uint32_t gm_l6; /* tid */
    void * gm_l8; /* v */
    GM_STACK_CHECK(&F, 708u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        uint32_t t2 = gm_l1;
        uint64_t t3 = (uint64_t)t2;
        uint64_t t4 = (uintptr_t)t3;
        void * t5 = gm_l0;
        void * t6 = GM_LD(void *, (((uint8_t *)t5) + 0));
        uint64_t t7 = GM_LD(uintptr_t, ((((uint8_t *)t6) + 1088) + 0) + 8);
        if (t4 >= (uintptr_t)(t7)) gm_trap_index(t4, t7, gm_src7nt72u848oha, 315, 17);
        gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t6) + 1088) + 0)) + t4 * 184) + 80));
        gm_l4 = t8;
    }
    {
        uint8_t t10 = 0;
        uint64_t t11 = gm_l2;
        uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
        uint8_t t13 = (t11 >= t12);
        if (!t13) {
            uint64_t t14 = gm_l2;
            uint64_t t15 = (uintptr_t)t14;
            uint64_t t16 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            if (t15 >= (uintptr_t)(t16)) gm_trap_index(t15, t16, gm_src7nt72u848oha, 316, 33);
            uint8_t t17 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t15 * 1));
            uint8_t t18 = (uint8_t)!t17;
            t10 = t18;
        } else t10 = 1;
        if (!t10) goto ie9;
        {
            uint8_t t20 = 0;
            F.ret = ((void *)0);
            goto gm_fret;
        }
        goto ix9;
ie9:;
ix9:;
    }
    {
        void * t21 = gm_l0;
        void * t22 = GM_LD(void *, (((uint8_t *)t21) + 0));
        uint32_t t23 = gm_l1;
        uint64_t t24 = gm_l2;
        void * t25 = gm_f_field_default_expr_tqfuiff(t22, t23, t24);
        void * t26;
        if ((GM_LD(void *, ((uint8_t *)&t25)) != 0)) {
            void * t27 = GM_LD(void *, (((uint8_t *)&t25) + 0));
            t26 = t27;
        } else {
            uint8_t t28 = 0;
            F.ret = ((void *)0);
            goto gm_fret;
        }
        gm_l5 = t26;
    }
    {
        void * t29 = gm_l0;
        void * t30 = GM_LD(void *, (((uint8_t *)t29) + 0));
        void * t31 = (void *)(((uint8_t *)t30) + 8);
        uint32_t t32 = gm_l1;
        uint32_t t33 = gm_f_template_9o92cvv(t31, t32);
        gm_l6 = t33;
    }
    {
        uint32_t t35 = gm_l6;
        uint32_t t36 = gm_l1;
        uint8_t t37 = (!(t35 == t36));
        if (!t37) goto ie34;
        {
            uint32_t t40 = gm_l6;
            uint64_t t41 = (uint64_t)t40;
            uint64_t t42 = (uintptr_t)t41;
            void * t43 = gm_l0;
            void * t44 = GM_LD(void *, (((uint8_t *)t43) + 0));
            uint64_t t45 = GM_LD(uintptr_t, ((((uint8_t *)t44) + 1088) + 0) + 8);
            if (t42 >= (uintptr_t)(t45)) gm_trap_index(t42, t45, gm_src7nt72u848oha, 322, 28);
            uint8_t t46 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t44) + 1088) + 0)) + t42 * 184) + 24) + 0));
            if (t46 != ((uint8_t)0ULL)) goto ie39;
            gm_tpautr4br7h3l t47 = GM_LD(gm_tpautr4br7h3l, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t44) + 1088) + 0)) + t42 * 184) + 24) + 8));
            F.l7 = t47;
            {
                uint64_t t50 = gm_l2;
                uint64_t t51 = GM_LD(uintptr_t, (((uint8_t *)&F.l7) + 0) + 8);
                uint8_t t52 = (t50 < t51);
                if (!t52) goto ie49;
                void * t53 = gm_l0;
                void * t54 = GM_LD(void *, (((uint8_t *)t53) + 0));
                void * t55 = (void *)(((uint8_t *)t54) + 8);
                uint64_t t56 = gm_l2;
                uint64_t t57 = (uintptr_t)t56;
                uint64_t t58 = GM_LD(uintptr_t, (((uint8_t *)&F.l7) + 0) + 8);
                if (t57 >= (uintptr_t)(t58)) gm_trap_index(t57, t58, gm_src7nt72u848oha, 323, 58);
                gm_t88rcucg1b7uk t59 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l7) + 0)) + t57 * 40) + 16));
                uint8_t t60 = gm_f_has_param_u9t0efs(t55, GM_CONV(gm_csguankko4lmhd, t59));
                if (!t60) goto ie49;
                {
                    void * t62 = gm_l0;
                    void * t63 = GM_LD(void *, (((uint8_t *)t62) + 0));
                    uint32_t t64 = gm_l1;
                    uint64_t t65 = (uint64_t)t64;
                    uint64_t t66 = (uintptr_t)t65;
                    void * t67 = gm_l0;
                    void * t68 = GM_LD(void *, (((uint8_t *)t67) + 0));
                    uint64_t t69 = GM_LD(uintptr_t, ((((uint8_t *)t68) + 1088) + 0) + 8);
                    if (t66 >= (uintptr_t)(t69)) gm_trap_index(t66, t69, gm_src7nt72u848oha, 324, 45);
                    uint64_t t70 = GM_LD(uint64_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t68) + 1088) + 0)) + t66 * 184) + 16));
                    void * t71 = gm_l5;
                    uint64_t t72 = gm_l2;
                    uint64_t t73 = (uintptr_t)t72;
                    uint64_t t74 = GM_LD(uintptr_t, (((uint8_t *)&F.l7) + 0) + 8);
                    if (t73 >= (uintptr_t)(t74)) gm_trap_index(t73, t74, gm_src7nt72u848oha, 324, 83);
                    gm_t88rcucg1b7uk t75 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l7) + 0)) + t73 * 40) + 16));
                    gm_tkn42u7na3t81 t76;
                    GM_ST(uint8_t, (uint8_t *)&t76 + 5, 0);
                    GM_ST(uint16_t, (uint8_t *)&t76 + 6, 0);
                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t76), t75);
                    ((uint8_t *)&t76)[4] = 1;
                    void * t77 = gm_f_check_const_expr_2ot4q9v((void *)NULL, cx, NULL, t63, t70, t71, t76);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l8 = t77;
                }
                {
                    void * t78 = gm_l8;
                    void * t79;
                    if ((GM_LD(void *, ((uint8_t *)&t78)) != 0)) {
                        void * t80 = GM_LD(void *, (((uint8_t *)&t78) + 0));
                        t79 = t80;
                    } else {
                        void * t81 = gm_l0;
                        void * t82 = GM_LD(void *, (((uint8_t *)t81) + 0));
                        void * t83 = gm_l5;
                        gm_tl874t2atnlh3 t84 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t83) + 72));
                        void * t85 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t82, t84);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        t79 = t85;
                    }
                    uint8_t t86 = 0;
                    F.ret = t79;
                    goto gm_fret;
                }
                goto ix49;
ie49:;
ix49:;
            }
            goto ix39;
ie39:;
ix39:;
        }
        goto ix34;
ie34:;
ix34:;
    }
    {
        void * t87 = gm_l0;
        uint32_t t88 = gm_l1;
        uint64_t t89 = (uint64_t)t88;
        uint64_t t90 = (uintptr_t)t89;
        void * t91 = gm_l0;
        void * t92 = GM_LD(void *, (((uint8_t *)t91) + 0));
        uint64_t t93 = GM_LD(uintptr_t, ((((uint8_t *)t92) + 1088) + 0) + 8);
        if (t90 >= (uintptr_t)(t93)) gm_trap_index(t90, t93, gm_src7nt72u848oha, 329, 29);
        uint64_t t94 = GM_LD(uint64_t, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t92) + 1088) + 0)) + t90 * 184) + 16));
        void * t95 = gm_l5;
        gm_t88rcucg1b7uk t96 = gm_l3;
        void * t97 = gm_f_default_value_lo723gi((void *)NULL, cx, NULL, t87, t94, t95, t96);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t98 = 0;
        F.ret = t97;
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
inline __attribute__((aligned(4))) void * gm_f_field_value_19vleud(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_field_value_19vleud F;
    void * gm_l0; /* cx */
    void * gm_l1; /* e */
    gm_t88rcucg1b7uk gm_l2; /* ty */
    void * gm_l3; /* v */
    GM_STACK_CHECK(&F, 293u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t3 = gm_l1;
        uint8_t t4 = ((GM_LD(uint8_t, (((uint8_t *)t3) + 8) + 0) == ((uint8_t)5ULL)));
        if (!t4) goto ie2;
        {
            void * t7 = gm_l0;
            gm_t88rcucg1b7uk t8 = gm_l2;
            uint8_t t9 = gm_f_is_plain_data_ej4mh2m((void *)NULL, cx, NULL, t7, t8);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t10 = (uint8_t)!t9;
            if (!t10) goto ie6;
            {
                void * t12 = gm_l0;
                void * t13 = gm_l1;
                gm_tl874t2atnlh3 t14 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t13) + 72));
                void * t15 = gm_l0;
                void * t16 = GM_LD(void *, (((uint8_t *)t15) + 0));
                gm_t88rcucg1b7uk t17 = gm_l2;
                gm_b16_8 t18 = gm_f_uninit_message_u2c3peu((void *)NULL, cx, NULL, t16, t17);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_b0_1 t19 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t12, t14, t18);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix6;
ie6:;
ix6:;
        }
        {
            gm_t88rcucg1b7uk t20 = gm_l2;
            gm_teti411eujvf4 t21;
            memset(&t21, 0, sizeof t21);
            GM_ST(uint8_t, ((uint8_t *)&t21 + 0), ((uint8_t)8ULL));
            void * t22 = gm_l1;
            gm_tl874t2atnlh3 t23 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t22) + 72));
            void * t24 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t20, &t21, t23);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t25 = 0;
            F.ret = t24;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t26 = gm_l0;
        void * t27 = gm_l1;
        gm_t88rcucg1b7uk t28 = gm_l2;
        gm_tkn42u7na3t81 t29;
        GM_ST(uint8_t, (uint8_t *)&t29 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t29 + 6, 0);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t29), t28);
        ((uint8_t *)&t29)[4] = 1;
        void * t30 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t26, t27, t29);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l3 = t30;
    }
    {
        void * t31 = gm_l0;
        void * t32 = gm_l3;
        gm_t88rcucg1b7uk t33 = gm_l2;
        void * t34 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t31, t32, t33);
        if (gm_unw.kind) {
            goto gm_lad;
        }
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
inline __attribute__((aligned(4))) void * gm_f_float_value_cr4h9gi(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t88rcucg1b7uk a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_float_value_cr4h9gi F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l2; /* ty */
    gm_tl874t2atnlh3 gm_l3; /* span */
    uint16_t gm_l4; /* bits */
    double gm_l5; /* x */
    GM_STACK_CHECK(&F, 281u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t3 = gm_l0;
        void * t4 = GM_LD(void *, (((uint8_t *)t3) + 0));
        void * t5 = (void *)(((uint8_t *)t4) + 8);
        gm_t88rcucg1b7uk t6 = gm_l2;
        gm_t53at80pfdus3 t7;
        gm_f_kind_jcnt4hm((void *)&t7, t5, GM_CONV(gm_csguankko4lmhd, t6));
        uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        if (t8 != ((uint8_t)3ULL)) goto ie2;
        uint16_t t9 = GM_LD(uint16_t, (((uint8_t *)&t7) + 8));
        gm_l4 = t9;
        {
            uint16_t t11 = gm_l4;
            double t12 = gm_f_to_float_o1iop8t((void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l1)), t11);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l5 = t12;
        }
        {
            gm_t88rcucg1b7uk t13 = gm_l2;
            double t14 = gm_l5;
            gm_teti411eujvf4 t15;
            memset(&t15, 0, sizeof t15);
            GM_ST(uint8_t, ((uint8_t *)&t15 + 0), ((uint8_t)1ULL));
            GM_ST(double, ((uint8_t *)&t15 + 8), t14);
            gm_tl874t2atnlh3 t16 = gm_l3;
            void * t17 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t13, &t15, t16);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t18 = 0;
            F.ret = t17;
            goto gm_fret;
        }
        goto ix2;
ie2:;
        {
            void * t20 = gm_l0;
            void * t21 = GM_LD(void *, (((uint8_t *)t20) + 0));
            gm_tl874t2atnlh3 t22 = gm_l3;
            void * t23 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t21, t22);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t24 = 0;
            F.ret = t23;
            goto gm_fret;
        }
ix2:;
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
__attribute__((aligned(4))) void gm_f_spread_source_fields_5csral7(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, void * a2) {
    struct gm_fr_spread_source_fields_5csral7 F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* vt */
    void * gm_l2; /* e */
    gm_b16_8 gm_l4; /* elems */
    gm_b16_8 gm_l5; /* names */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* i */
    gm_b16_8 gm_l9; /* n */
    gm_b16_8 gm_l10; /* fs */
    gm_b16_8 gm_l11; /* <tmp> */
    uint64_t gm_l12; /* <tmp> */
    GM_STACK_CHECK(&F, 947u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        gm_f_list_7gsrufb((void *)&F.l3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t3 = gm_l0;
        void * t4 = GM_LD(void *, (((uint8_t *)t3) + 0));
        void * t5 = (void *)(((uint8_t *)t4) + 8);
        gm_t88rcucg1b7uk t6 = gm_l1;
        gm_t53at80pfdus3 t7;
        gm_f_kind_jcnt4hm((void *)&t7, t5, GM_CONV(gm_csguankko4lmhd, t6));
        uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        if (t8 != ((uint8_t)12ULL)) goto ie2;
        gm_b16_8 t9 = GM_LD(gm_b16_8, ((((uint8_t *)&t7) + 8) + 0));
        gm_l4 = t9;
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((((uint8_t *)&t7) + 8) + 16));
        gm_l5 = t10;
        uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
        uint8_t t12 = (t11 > ((uint64_t)0ULL));
        if (!t12) goto ie2;
        {
            {
                gm_b16_8 t15 = gm_l5;
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
                            void * t33 = (void *)((uint8_t *)&F.l3);
                            gm_tep2drsl9408b t34;
                            memset((uint8_t *)&t34 + 20, 0, 4);
                            gm_b16_8 t35 = gm_l9;
                            GM_ST(gm_b16_8, (uint8_t *)&t34 + 0, t35);
                            uint64_t t36 = gm_l8;
                            uint64_t t37 = (uintptr_t)t36;
                            uint64_t t38 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                            if (t37 >= (uintptr_t)(t38)) gm_trap_index(t37, t38, gm_src7nt72u848oha, 65, 74);
                            gm_t88rcucg1b7uk t39 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t37 * 4));
                            GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t34 + 16, t39);
                            gm_tftb1odc739t5 t40 = gm_f_push_lnebar3(t33, &t34);
                            if (((((uint8_t *)&t40))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t40) + 1, gm_src7nt72u848oha, 65, 40);
                            gm_b0_1 t41 = GM_LD(gm_b0_1, (((uint8_t *)&t40) + 1));
                        }
                        goto me28;
mn29:;
                        gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 65, 9);
me28:;
                    }
lc16:;
                    {
                        uint64_t t43 = gm_l7;
                        uint64_t r44; bool o45 = __builtin_add_overflow(t43, ((uint64_t)1ULL), &r44);
                        uint64_t t46 = r44;
                        gm_l7 = t46;
                    }
                }
lb16:;
            }
        }
        {
            gm_b16_8 t47 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
            ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 0, t47);
            goto gm_fdone;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t48 = gm_l0;
        void * t49 = GM_LD(void *, (((uint8_t *)t48) + 0));
        gm_t88rcucg1b7uk t50 = gm_l1;
        gm_th2lqbo77pvsc t51;
        gm_f_struct_fields_chqu3it((void *)&t51, t49, GM_CONV(gm_csguankko4lmhd, t50));
        gm_b16_8 t52;
        if (((((uint8_t *)&t51))[16] != 0)) {
            gm_b16_8 t53 = GM_LD(gm_b16_8, (((uint8_t *)&t51) + 0));
            t52 = t53;
        } else {
            {
                void * t56 = gm_l0;
                void * t57 = GM_LD(void *, (((uint8_t *)t56) + 0));
                void * t58 = (void *)(((uint8_t *)t57) + 8);
                gm_t88rcucg1b7uk t59 = gm_l1;
                uint8_t t60 = gm_f_is_error_8k27gd1(t58, GM_CONV(gm_csguankko4lmhd, t59));
                uint8_t t61 = (uint8_t)!t60;
                if (!t61) goto ie55;
                {
                    void * t63 = gm_l0;
                    void * t64 = gm_l2;
                    gm_tl874t2atnlh3 t65 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t64) + 72));
                    void * t66 = gm_l0;
                    gm_t88rcucg1b7uk t67 = gm_l1;
                    gm_b16_8 t68 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t66, t67);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tnpifv20irnph t69;
                    GM_ST(gm_b16_8, ((uint8_t *)&t69 + 0), t68);
                    gm_tabb710d9mq2e t70;
                    gm_f_format_a07tfib((void *)&t70, (void *)NULL, cx, NULL, t69);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (((((uint8_t *)&t70))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t70) + 8, gm_src7nt72u848oha, 69, 57);
                    gm_b16_8 t71 = GM_LD(gm_b16_8, (((uint8_t *)&t70) + 8));
                    gm_b16_8 t72 = GM_LD(gm_b16_8, ((uint8_t *)&t71));
                    gm_b0_1 t73 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t63, t65, t72);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                goto ix55;
ie55:;
ix55:;
            }
            gm_th2lqbo77pvsc t74;
            memset(&t74, 0, sizeof t74);
            uint8_t t75 = 0;
            F.ret = t74;
            goto gm_fret;
        }
        gm_l10 = t52;
    }
    {
        {
            gm_b16_8 t77 = gm_l10;
            gm_l11 = t77;
        }
        {
            gm_l12 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t79 = gm_l12;
                uint64_t t80 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                uint8_t t81 = (t79 < t80);
                if (!t81) goto lb78;
                {
                    uint64_t t84 = gm_l12;
                    uint64_t t85 = (uintptr_t)t84;
                    uint64_t t86 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                    gm_t0mglksrkfa5e t89 = GM_LD(gm_t0mglksrkfa5e, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l11)) + t85 * 40));
                    F.l13 = t89;
                    {
                        void * t91 = (void *)((uint8_t *)&F.l3);
                        gm_tep2drsl9408b t92;
                        memset((uint8_t *)&t92 + 20, 0, 4);
                        gm_b16_8 t93 = GM_LD(gm_b16_8, (((uint8_t *)&F.l13) + 0));
                        GM_ST(gm_b16_8, (uint8_t *)&t92 + 0, t93);
                        gm_t88rcucg1b7uk t94 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l13) + 16));
                        GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t92 + 16, t94);
                        gm_tftb1odc739t5 t95 = gm_f_push_lnebar3(t91, &t92);
                        if (((((uint8_t *)&t95))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t95) + 1, gm_src7nt72u848oha, 72, 19);
                        gm_b0_1 t96 = GM_LD(gm_b0_1, (((uint8_t *)&t95) + 1));
                    }
                    goto me87;
mn88:;
                    gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 72, 5);
me87:;
                }
lc78:;
                {
                    uint64_t t98 = gm_l12;
                    uint64_t r99; bool o100 = __builtin_add_overflow(t98, ((uint64_t)1ULL), &r99);
                    uint64_t t101 = r99;
                    gm_l12 = t101;
                }
            }
lb78:;
        }
    }
    {
        gm_b16_8 t102 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
        ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 0, t102);
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
inline __attribute__((aligned(4))) gm_csguankko4lmhd gm_f_strip_wrappers_e5thpib(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_strip_wrappers_e5thpib F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* t0 */
    gm_t88rcucg1b7uk gm_l2; /* t */
    gm_t88rcucg1b7uk gm_l3; /* i */
    gm_t88rcucg1b7uk gm_l4; /* ok */
    GM_STACK_CHECK_SHALLOW(&F, 144u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    {
        gm_t88rcucg1b7uk t2 = gm_l1;
        gm_l2 = t2;
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb3;
            {
                void * t6 = gm_l0;
                void * t7 = GM_LD(void *, (((uint8_t *)t6) + 0));
                void * t8 = (void *)(((uint8_t *)t7) + 8);
                gm_t88rcucg1b7uk t9 = gm_l2;
                gm_t53at80pfdus3 t10;
                gm_f_kind_jcnt4hm((void *)&t10, t8, GM_CONV(gm_csguankko4lmhd, t9));
                uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)&t10) + 0));
                switch (t16) { case ((uint8_t)0ULL): goto ma14; case ((uint8_t)1ULL): goto ma14; case ((uint8_t)2ULL): goto ma14; case ((uint8_t)3ULL): goto ma14; case ((uint8_t)4ULL): goto ma14; case ((uint8_t)5ULL): goto ma14; case ((uint8_t)6ULL): goto ma14; case ((uint8_t)7ULL): goto ma14; case ((uint8_t)8ULL): goto ma14; case ((uint8_t)9ULL): goto ma14; case ((uint8_t)10ULL): goto ma14; case ((uint8_t)11ULL): goto ma14; case ((uint8_t)12ULL): goto ma14; case ((uint8_t)13ULL): goto ma12; case ((uint8_t)14ULL): goto ma13; case ((uint8_t)15ULL): goto ma14; case ((uint8_t)16ULL): goto ma14; case ((uint8_t)17ULL): goto ma14; case ((uint8_t)18ULL): goto ma14; case ((uint8_t)19ULL): goto ma14; case ((uint8_t)20ULL): goto ma14; case ((uint8_t)21ULL): goto ma14; case ((uint8_t)22ULL): goto ma14; case ((uint8_t)23ULL): goto ma14; case ((uint8_t)24ULL): goto ma14; case ((uint8_t)25ULL): goto ma14; case ((uint8_t)26ULL): goto ma14; default: goto ma14; }
ma12:;
                uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&t10) + 0));
                if (t18 != ((uint8_t)13ULL)) goto mn17;
                gm_t88rcucg1b7uk t19 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t10) + 8));
                gm_l3 = t19;
                {
                    gm_t88rcucg1b7uk t21 = gm_l3;
                    gm_l2 = t21;
                }
                goto me11;
mn17:;
ma13:;
                uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)&t10) + 0));
                if (t23 != ((uint8_t)14ULL)) goto mn22;
                gm_t88rcucg1b7uk t24 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t10) + 8) + 4));
                gm_l4 = t24;
                {
                    gm_t88rcucg1b7uk t26 = gm_l4;
                    gm_l2 = t26;
                }
                goto me11;
mn22:;
ma14:;
                gm_t88rcucg1b7uk t28 = gm_l2;
                uint8_t t29 = 0;
                F.ret = t28;
                goto gm_fret;
                goto me11;
mn27:;
ma15:;
                gm_trap("no match arm matched the scrutinee", gm_src7nt72u848oha, 9, 9);
me11:;
            }
lc3:;
        }
lb3:;
    }
gm_fret:
    return GM_CONV(gm_csguankko4lmhd, F.ret);
gm_fprop:
    return GM_CONV(gm_csguankko4lmhd, F.ret);
}

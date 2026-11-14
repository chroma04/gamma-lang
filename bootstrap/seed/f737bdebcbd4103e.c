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
typedef struct gm_csguankko4lmhd { uint32_t f0; } gm_csguankko4lmhd;
typedef struct gm_csn5gfg3c8ooaj { _Bool f0; void * f1; } gm_csn5gfg3c8ooaj;
typedef struct { int32_t f0; } gm_t19enj90vg8bd;
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
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
typedef struct { void * f0; gm_b16_8 f1; uint8_t f2; uint8_t pe[7]; } gm_tlljg61a2ctgq;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint64_t f2; gm_b16_8 f3; } gm_tmr5oep1578tb;
_Static_assert(sizeof(gm_tn68el8d14vku) == 16 && _Alignof(gm_tn68el8d14vku) == 8 && offsetof(gm_tn68el8d14vku, f0) == 0 && offsetof(gm_tn68el8d14vku, f1) == 8, "layout of `(i: usize, x: u32)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t9805qtonbsag v0; } f1; } gm_tnrltbc61i6v1;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t f1; uint8_t pe[3]; } gm_toceffb697p0u;
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
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_thognv9odov03 v0; gm_tiilmfd16v3fc v1; } f1; } gm_tbf8u1b86cgmi;
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
_Static_assert(sizeof(gm_tcrdb1inj22os) == 72 && _Alignof(gm_tcrdb1inj22os) == 8 && offsetof(gm_tcrdb1inj22os, f0) == 0 && offsetof(gm_tcrdb1inj22os, f1) == 16 && offsetof(gm_tcrdb1inj22os, f2) == 24 && offsetof(gm_tcrdb1inj22os, f3) == 40 && offsetof(gm_tcrdb1inj22os, f4) == 56, "layout of `(string, usize, string, string, string)`");
typedef struct { gm_trqfvmjtt4sij f0; gm_trqfvmjtt4sij f1; } gm_td7pkamr64l0c;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
typedef struct { gm_tlljg61a2ctgq f0; uint8_t f1; uint8_t pe[7]; } gm_tdh3gh5n00hec;
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
_Static_assert(sizeof(gm_tlljg61a2ctgq) == 32 && _Alignof(gm_tlljg61a2ctgq) == 8 && offsetof(gm_tlljg61a2ctgq, f0) == 0 && offsetof(gm_tlljg61a2ctgq, f1) == 8 && offsetof(gm_tlljg61a2ctgq, f2) == 24, "layout of `(callee: ^Expr, args: []Arg, trailing_comma: bool)`");
_Static_assert(sizeof(gm_tmr5oep1578tb) == 56 && _Alignof(gm_tmr5oep1578tb) == 8 && offsetof(gm_tmr5oep1578tb, f0) == 0 && offsetof(gm_tmr5oep1578tb, f1) == 16 && offsetof(gm_tmr5oep1578tb, f2) == 32 && offsetof(gm_tmr5oep1578tb, f3) == 40, "layout of `(string, []u8, usize, []u8)`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tnrltbc61i6v1) == 12 && _Alignof(gm_tnrltbc61i6v1) == 4 && offsetof(gm_tnrltbc61i6v1, f0) == 0 && offsetof(gm_tnrltbc61i6v1, f1) == 4, "layout of `Observed`");
_Static_assert(sizeof(gm_toceffb697p0u) == 16 && _Alignof(gm_toceffb697p0u) == 4 && offsetof(gm_toceffb697p0u, f0) == 0 && offsetof(gm_toceffb697p0u, f1) == 12, "layout of `?Span`");
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
_Static_assert(sizeof(gm_tbf8u1b86cgmi) == 48 && _Alignof(gm_tbf8u1b86cgmi) == 8 && offsetof(gm_tbf8u1b86cgmi, f0) == 0 && offsetof(gm_tbf8u1b86cgmi, f1) == 8, "layout of `ScopeKind`");
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
_Static_assert(sizeof(gm_tehgl6lkqgvt0) == 104 && _Alignof(gm_tehgl6lkqgvt0) == 8 && offsetof(gm_tehgl6lkqgvt0, f0) == 0 && offsetof(gm_tehgl6lkqgvt0, f1) == 48 && offsetof(gm_tehgl6lkqgvt0, f2) == 56 && offsetof(gm_tehgl6lkqgvt0, f3) == 96, "layout of `Scope`");
typedef struct { void * f0; uint32_t f1; uint8_t p2[4]; gm_b16_8 f2; gm_t9805qtonbsag f3; gm_t5lf9i3n1r3co f4; gm_tdh3gh5n00hec f5; gm_tjefu848ssvm9 f6; gm_th2lqbo77pvsc f7; void * f8; gm_trqfvmjtt4sij f9; uint64_t f10; uint32_t f11; uint8_t p12[4]; gm_trqfvmjtt4sij f12; gm_b16_8 f13; uint8_t f14; uint8_t f15; uint8_t f16; uint8_t p17[5]; uint64_t f17; gm_b16_8 f18; gm_b16_8 f19; uint32_t f20; uint8_t f21; uint8_t p22[3]; gm_trqfvmjtt4sij f22; } gm_tfo5f2rllfhc5;
_Static_assert(sizeof(gm_tjefu848ssvm9) == 48 && _Alignof(gm_tjefu848ssvm9) == 8 && offsetof(gm_tjefu848ssvm9, f0) == 0 && offsetof(gm_tjefu848ssvm9, f1) == 40, "layout of `?DestCall`");
_Static_assert(sizeof(gm_tsth9qu1dv0g3) == 40 && _Alignof(gm_tsth9qu1dv0g3) == 8 && offsetof(gm_tsth9qu1dv0g3, f0) == 0 && offsetof(gm_tsth9qu1dv0g3, f1) == 8, "layout of `Callee`");
_Static_assert(sizeof(gm_t1g28mkc78m0c) == 112 && _Alignof(gm_t1g28mkc78m0c) == 8 && offsetof(gm_t1g28mkc78m0c, f0) == 0 && offsetof(gm_t1g28mkc78m0c, f1) == 104, "layout of `?Scope`");
_Static_assert(sizeof(gm_tfo5f2rllfhc5) == 392 && _Alignof(gm_tfo5f2rllfhc5) == 8 && offsetof(gm_tfo5f2rllfhc5, f0) == 0 && offsetof(gm_tfo5f2rllfhc5, f1) == 8 && offsetof(gm_tfo5f2rllfhc5, f2) == 16 && offsetof(gm_tfo5f2rllfhc5, f3) == 32 && offsetof(gm_tfo5f2rllfhc5, f4) == 40 && offsetof(gm_tfo5f2rllfhc5, f5) == 64 && offsetof(gm_tfo5f2rllfhc5, f6) == 104 && offsetof(gm_tfo5f2rllfhc5, f7) == 152 && offsetof(gm_tfo5f2rllfhc5, f8) == 176 && offsetof(gm_tfo5f2rllfhc5, f9) == 184 && offsetof(gm_tfo5f2rllfhc5, f10) == 224 && offsetof(gm_tfo5f2rllfhc5, f11) == 232 && offsetof(gm_tfo5f2rllfhc5, f12) == 240 && offsetof(gm_tfo5f2rllfhc5, f13) == 280 && offsetof(gm_tfo5f2rllfhc5, f14) == 296 && offsetof(gm_tfo5f2rllfhc5, f15) == 297 && offsetof(gm_tfo5f2rllfhc5, f16) == 298 && offsetof(gm_tfo5f2rllfhc5, f17) == 304 && offsetof(gm_tfo5f2rllfhc5, f18) == 312 && offsetof(gm_tfo5f2rllfhc5, f19) == 328 && offsetof(gm_tfo5f2rllfhc5, f20) == 344 && offsetof(gm_tfo5f2rllfhc5, f21) == 348 && offsetof(gm_tfo5f2rllfhc5, f22) == 352, "layout of `FnGen`");
struct gm_fr_add_member_name_j3i0eje {
    gm_frame h;
    gm_b0_1 ret;
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
    gm_tehgl6lkqgvt0 l48; /* scope */
    gm_tehgl6lkqgvt0 l50; /* popped */
    gm_tqgtd5gtgj65s ret;
};
struct gm_fr_fixed_address_5usq50j {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_fn_2tr382j {
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
struct gm_fr_fn_aa55llv {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_fn_count_g9jh9qr {
    gm_frame h;
    uint64_t ret;
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
    gm_tfo5f2rllfhc5 l26; /* fg */
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
struct gm_fr_with_scope_o670qbc {
    gm_frame h;
    gm_tehgl6lkqgvt0 ret;
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
inline __attribute__((aligned(4))) uint8_t gm_f_fn_2tr382j(void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_5evrfa7(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_7u87bne(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_fn_aa55llv(void * a0);
inline __attribute__((aligned(4))) uint64_t gm_f_fn_count_g9jh9qr(void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_jjarlg6(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_kssb0q2(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_fn_name_6n34573(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_fn_tag_okd90ep(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_t9805qtonbsag a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_v0t9sik(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_for_each_child_fm49s73(void * a0, gm_closure a1);
__attribute__((aligned(4))) void gm_f_foreign_decl_rld7m78(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_format_07f8le4(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_32n178h(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_4hn970h(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_4un19t8(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_5bcbbmm(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tb31k5l794t7p a0);
__attribute__((aligned(4))) void gm_f_format_5erjuiq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_7t9p78f(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
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
__attribute__((aligned(4))) gm_b0_1 gm_f_line_l6u87p9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
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
__attribute__((aligned(4))) uint8_t gm_f_starts_with_6vvt01e(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_stmt_1t29bfr(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_stmt_block_5vkbn0n(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_stmt_list_d9ovq7d(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_toceffb697p0u gm_f_stmt_span_d0pel0p(void * a0);
inline __attribute__((aligned(4))) void gm_f_stmts_1199hll(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
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
__attribute__((aligned(4))) void gm_f_with_scope_o670qbc(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_3c9chj4(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_ajj3a37(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_cdouvhd(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_f1cbljc(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_i2vvij8(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_ih4qokb(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_ihqa4kk(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_ildv1bj(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_j67vjku(void * a0, gm_tnpifv20irnph a1);
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
extern const uint8_t gm_som0q53jqvjh3[5];
extern const uint8_t gm_sovhd8e8hvflr[52];
extern const uint8_t gm_sp1g56u3ibrtp[23];
extern const uint8_t gm_spdft68ac8308[95];
extern const uint8_t gm_spdpobhso0dbc[10];
extern const uint8_t gm_sqtc9fffut4s2[13];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src0gtuh94b1fce[];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src3d0v42kavhv7[];
extern const char gm_srcbsc23is03ttm[];
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
    GM_STACK_CHECK(&F, 60u);
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
        gm_tftb1odc739t5 t6 = t5;
        if (((((uint8_t *)&t6))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t6) + 1, gm_srckfibkpt681aa, 450, 5);
        gm_b0_1 t7 = GM_LD(gm_b0_1, (((uint8_t *)&t6) + 1));
    }
    F.ret = GM_ZERO(gm_b0_1);
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
    GM_STACK_CHECK(&F, 97u);
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
    gm_t5v8n6qrn4rh4 t5 = t4;
    uint8_t t6 = ((GM_LD(uint8_t, ((uint8_t *)&t5) + 0) == ((uint8_t)6ULL)));
    if (t6) {
        void * t7 = gm_l0;
        gm_t88rcucg1b7uk t8 = gm_l1;
        uint64_t t9 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t7, t8);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t10 = (t9 > ((uint64_t)16ULL));
        t1 = t10;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_84b0b46(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_84b0b46 F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 8)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_93nt26c(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_93nt26c F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 48)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_gm6v230(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_gm6v230 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_k21v01c(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_k21v01c F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lv6i4jh(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_lv6i4jh F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 104)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_meisvg6(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_meisvg6 F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 8)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ppl3r45(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_ppl3r45 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rcd2jln(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_rcd2jln F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 4)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_s1lbe5s(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_s1lbe5s F;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_end_lines_99v0bhd(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_end_lines_99v0bhd F;
    void * gm_l0; /* fg */
    GM_STACK_CHECK(&F, 77u);
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
            gm_tftb1odc739t5 t12 = t11;
            if (((((uint8_t *)&t12))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t12) + 1, gm_srcmok80ho8hh01, 430, 31);
            gm_b0_1 t13 = GM_LD(gm_b0_1, (((uint8_t *)&t12) + 1));
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
inline __attribute__((aligned(4))) uint8_t gm_f_fn_2tr382j(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fn_2tr382j F;
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
inline __attribute__((aligned(4))) uint8_t gm_f_fn_aa55llv(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fn_aa55llv F;
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
    GM_STACK_CHECK(&F, 192u);
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
    gm_tabb710d9mq2e t7 = t6;
    if (((((uint8_t *)&t7))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t7) + 8, gm_srcht195tfrfuqo, 167, 60);
    gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)&t7) + 8));
    gm_b16_8 t9 = t8;
    gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
    F.ret = t10;
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
        if (o6) gm_trap("integer overflow in `+`", gm_srcmok80ho8hh01, 452, 5);
        uint32_t t7 = r5;
        GM_ST(uint32_t, (((uint8_t *)t2) + 232), t7);
    }
    {
        void * t8 = gm_l0;
        void * t9 = gm_l0;
        uint32_t t10 = GM_LD(uint32_t, (((uint8_t *)t9) + 344));
        uint32_t r11; bool o12 = __builtin_add_overflow(t10, ((uint32_t)1ULL), &r11);
        if (o12) gm_trap("integer overflow in `+`", gm_srcmok80ho8hh01, 453, 5);
        uint32_t t13 = r11;
        GM_ST(uint32_t, (((uint8_t *)t8) + 344), t13);
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
    GM_STACK_CHECK(&F, 131u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)((uint8_t *)&gm_l1);
        gm_tf5srh4mp4b3i t4 = gm_f_find_efqiqfc(t2, t3);
        gm_tf5srh4mp4b3i t5 = t4;
        uint64_t t6;
        if (((((uint8_t *)&t5))[8] != 0)) {
            uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)&t5) + 0));
            t6 = t7;
        } else {
            gm_tqs5p4n2tq19v t8;
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
        uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)t12) + 0)) + t11 * 16) + 12));
        gm_tqs5p4n2tq19v t15;
        GM_ST(uint8_t, ((uint8_t *)&t15), t14);
        ((uint8_t *)&t15)[1] = 1;
        uint8_t t16 = 0;
        F.ret = t15;
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
    GM_STACK_CHECK_SHALLOW(&F, 109u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        uint32_t t4 = gm_l1;
        gm_tf5srh4mp4b3i t5 = gm_f_param_index_rp1nn6k(t3, t4);
        gm_tf5srh4mp4b3i t6 = t5;
        if (!((((uint8_t *)&t6))[8] != 0)) goto ie2;
        uint64_t t7 = GM_LD(uint64_t, ((uint8_t *)&t6));
        gm_l2 = t7;
        {
            uint64_t t9 = gm_l2;
            uint64_t t10 = (uintptr_t)t9;
            void * t11 = gm_l0;
            uint64_t t12 = GM_LD(uintptr_t, (((uint8_t *)t11) + 312) + 8);
            if (t10 >= (uintptr_t)(t12)) gm_trap_index(t10, t12, gm_srcmok80ho8hh01, 531, 40);
            uint8_t t13 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t11) + 312)) + t10 * 1));
            uint8_t t14 = 0;
            F.ret = t13;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint8_t t15 = 0;
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
    GM_STACK_CHECK_SHALLOW(&F, 155u);
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
    gm_t53at80pfdus3 t6 = t5;
    uint8_t t7 = ((GM_LD(uint8_t, ((uint8_t *)&t6) + 0) == ((uint8_t)5ULL)));
    F.ret = t7;
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
    GM_STACK_CHECK_SHALLOW(&F, 182u);
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
        gm_t53at80pfdus3 t6 = t5;
        uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t7 != ((uint8_t)12ULL)) goto ie2;
        gm_b16_8 t8 = GM_LD(gm_b16_8, ((((uint8_t *)&t6) + 8) + 0));
        gm_l2 = t8;
        {
            uint64_t t10 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            uint8_t t11 = ((t10 == ((uint64_t)0ULL)));
            uint8_t t12 = 0;
            F.ret = t11;
            goto gm_fret;
        }
        goto ix2;
ie2:;
        {
            uint8_t t14 = 0;
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
    GM_STACK_CHECK_SHALLOW(&F, 104u);
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
    gm_t53at80pfdus3 t6 = GM_LD(gm_t53at80pfdus3, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t4) + 0) + 0)) + t3 * 56));
    F.ret = t6;
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
    GM_STACK_CHECK(&F, 76u);
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
        gm_tftb1odc739t5 t7 = t6;
        if (((((uint8_t *)&t7))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t7) + 1, gm_srcmok80ho8hh01, 507, 5);
        gm_b0_1 t8 = GM_LD(gm_b0_1, (((uint8_t *)&t7) + 1));
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
    GM_STACK_CHECK(&F, 163u);
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
        gm_tabb710d9mq2e t11 = t10;
        if (((((uint8_t *)&t11))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t11) + 8, gm_src0gtuh94b1fce, 1160, 12);
        gm_b16_8 t12 = GM_LD(gm_b16_8, (((uint8_t *)&t11) + 8));
        gm_b16_8 t13 = t12;
        gm_b16_8 t14 = GM_LD(gm_b16_8, ((uint8_t *)&t13));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_offsets_eh6vfle(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_offsets_eh6vfle F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 144u);
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
    gm_tbccvikg6b96e t5 = t4;
    gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)&t5) + 16));
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
    GM_STACK_CHECK(&F, 182u);
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
        gm_th2lqbo77pvsc t6 = t5;
        if (!((((uint8_t *)&t6))[16] != 0)) goto ie2;
        gm_b16_8 t7 = GM_LD(gm_b16_8, ((uint8_t *)&t6));
        gm_l2 = t7;
        {
            gm_b16_8 t9 = gm_l2;
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
        gm_b16_8 t13 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t11, t12);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t14 = 0;
        F.ret = t13;
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
        gm_tehgl6lkqgvt0 t29 = F.l2;
        gm_t1g28mkc78m0c t30;
        GM_ST(uint8_t, (uint8_t *)&t30 + 105, 0);
        GM_ST(uint16_t, (uint8_t *)&t30 + 106, 0);
        GM_ST(uint32_t, (uint8_t *)&t30 + 108, 0);
        GM_ST(gm_tehgl6lkqgvt0, ((uint8_t *)&t30), t29);
        ((uint8_t *)&t30)[104] = 1;
        uint8_t t31 = 0;
        F.ret = t30;
        goto gm_fret;
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
        GM_ST(gm_trqfvmjtt4sij, (((uint8_t *)t4) + 352), t5);
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
        memcpy((uint8_t *)&F.l1 + 40, (((uint8_t *)t3) + 352), 40);
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
        uint8_t *t7 = (((uint8_t *)t6) + 352);
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
    GM_STACK_CHECK(&F, 120u);
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
    gm_tbccvikg6b96e t5 = t4;
    uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)&t5) + 0));
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
    GM_STACK_CHECK(&F, 212u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_t6j6smj7qoctu t2;
    gm_b16_8 t3 = gm_f_empty_slice_rcd2jln();
    gm_b16_8 t4 = t3;
    gm_b16_8 t5 = GM_LD(gm_b16_8, ((uint8_t *)&t4));
    GM_ST(gm_b16_8, (uint8_t *)&t2 + 0, t5);
    gm_b16_8 t6 = gm_f_empty_slice_5nn7ph0();
    gm_b16_8 t7 = t6;
    gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&t7));
    GM_ST(gm_b16_8, (uint8_t *)&t2 + 16, t8);
    gm_t53at80pfdus3 t9;
    GM_ST(uint8_t, (uint8_t *)&t9 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t9 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t9 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t9 + 40, 0);
    GM_ST(uint64_t, (uint8_t *)&t9 + 48, 0);
    GM_ST(uint8_t, ((uint8_t *)&t9 + 0), ((uint8_t)12ULL));
    GM_ST(gm_t6j6smj7qoctu, ((uint8_t *)&t9 + 8), t2);
    gm_t88rcucg1b7uk t10 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t1, &t9);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t10;
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
    GM_STACK_CHECK(&F, 168u);
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
        gm_tabb710d9mq2e t5 = t4;
        if (((((uint8_t *)&t5))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t5) + 8, gm_srcd5arorrf8jkg, 35, 27);
        gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)&t5) + 8));
        gm_b16_8 t7 = t6;
        gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&t7));
        gm_tl874t2atnlh3 t9 = gm_l0;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t08olq97rscn3 *)gm_out) + 0, t8);
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&(*(gm_t08olq97rscn3 *)gm_out) + 16, t9);
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
    GM_STACK_CHECK(&F, 582u);
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
        gm_t5o0tpv5jka2l t5 = t4;
        if (((((uint8_t *)&t5))[0] != 0)) {
            gm_t08olq97rscn3 t6 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t5) + 8));
            gm_t5o0tpv5jka2l t7;
            GM_ST(uint8_t, (uint8_t *)&t7 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t7 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t7 + 4, 0);
            ((uint8_t *)&t7)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t7 + 8), t6);
            F.ret = t7;
            goto gm_fret;
        }
        gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)&t5) + 8));
        gm_l3 = t8;
    }
    {
        uint8_t t10 = 0;
        void * t11 = gm_l1;
        uint8_t t12 = gm_f_fixed_address_5usq50j(t11);
        if (!t12) {
            void * t13 = gm_l2;
            uint8_t t14 = gm_f_may_write_702phjq(t13);
            uint8_t t15 = (uint8_t)!t14;
            t10 = t15;
        } else t10 = 1;
        if (!t10) goto ie9;
        {
            gm_b16_8 t17 = gm_l3;
            gm_t5o0tpv5jka2l t18;
            GM_ST(uint8_t, (uint8_t *)&t18 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t18 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t18 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t18 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t18 + 32, 0);
            ((uint8_t *)&t18)[0] = 0;
            GM_ST(gm_b16_8, ((uint8_t *)&t18 + 8), t17);
            gm_t5o0tpv5jka2l t19 = t18;
            uint8_t t20 = ((((uint8_t *)&t19))[0] != 0);
            F.ret = t18;
            goto gm_fret;
        }
        goto ix9;
ie9:;
ix9:;
    }
    {
        void * t21 = gm_l0;
        void * t22 = gm_l0;
        uint64_t t23 = GM_LD(uint64_t, (((uint8_t *)t22) + 304));
        uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)8ULL), &r24);
        if (o25) gm_trap("integer overflow in `+`", gm_srcmok80ho8hh01, 490, 5);
        uint64_t t26 = r24;
        GM_ST(uint64_t, (((uint8_t *)t21) + 304), t26);
    }
    {
        void * t27 = gm_l0;
        gm_b16_8 t28 = gm_f_fresh_tmp_6tadl69((void *)NULL, cx, NULL, t27);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l4 = t28;
    }
    {
        void * t29 = gm_l0;
        gm_t6j6smj7qoctu t30;
        gm_b16_8 t31 = gm_l4;
        GM_ST(gm_b16_8, (uint8_t *)&t30 + 0, t31);
        gm_b16_8 t32 = gm_l3;
        GM_ST(gm_b16_8, (uint8_t *)&t30 + 16, t32);
        gm_b0_1 t33 = gm_f_linef_5n8up5i((void *)NULL, cx, NULL, t29, &t30);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t34 = gm_l4;
        gm_t5o0tpv5jka2l t35;
        GM_ST(uint8_t, (uint8_t *)&t35 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t35 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t35 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t35 + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&t35 + 32, 0);
        ((uint8_t *)&t35)[0] = 0;
        GM_ST(gm_b16_8, ((uint8_t *)&t35 + 8), t34);
        gm_t5o0tpv5jka2l t36 = t35;
        uint8_t t37 = ((((uint8_t *)&t36))[0] != 0);
        F.ret = t35;
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
extern __attribute__((aligned(4))) uint8_t gm_f_big_place_raoupin(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) void gm_f_block_31qi85g(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_block_31qi85g F;
    void * gm_l0; /* fg */
    void * gm_l1; /* b */
    gm_th2lqbo77pvsc gm_l3; /* result */
    uint64_t gm_l5; /* k */
    gm_tgtub0v94d5u5 gm_l6; /* d */
    gm_b16_8 gm_l7; /* r */
    GM_STACK_CHECK(&F, 993u);
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
        gm_tftb1odc739t5 t6 = t5;
        if (((((uint8_t *)&t6))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t6) + 1, gm_srcmok80ho8hh01, 972, 5);
        gm_b0_1 t7 = GM_LD(gm_b0_1, (((uint8_t *)&t6) + 1));
    }
    {
        gm_th2lqbo77pvsc t8;
        memset(&t8, 0, sizeof t8);
        gm_l3 = t8;
    }
    {
        void * t9 = gm_l0;
        void * t10 = gm_l1;
        void * t11 = (void *)((uint8_t *)&gm_l3);
        gm_tqgtd5gtgj65s t12;
        gm_f_block_inner_83vev92((void *)&t12, (void *)NULL, cx, NULL, t9, t10, t11);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tqgtd5gtgj65s t13 = t12;
        if (((((uint8_t *)&t13))[0] != 0)) {
            gm_t08olq97rscn3 t14 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t13) + 8));
            gm_t5o0tpv5jka2l t15;
            GM_ST(uint8_t, (uint8_t *)&t15 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t15 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t15 + 4, 0);
            ((uint8_t *)&t15)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t15 + 8), t14);
            F.ret = t15;
            goto gm_fret;
        }
        gm_b0_1 t16 = GM_LD(gm_b0_1, (((uint8_t *)&t13) + 8));
    }
    {
        void * t17 = gm_l0;
        void * t18 = (void *)(((uint8_t *)t17) + 240);
        gm_t1g28mkc78m0c t19;
        gm_f_pop_g2usrvd((void *)&t19, t18);
        gm_t1g28mkc78m0c t20 = t19;
        if (!((((uint8_t *)&t20))[104] != 0)) gm_trap("unwrap of null", gm_srcmok80ho8hh01, 975, 15);
        gm_tehgl6lkqgvt0 t21 = GM_LD(gm_tehgl6lkqgvt0, ((uint8_t *)&t20));
        F.l4 = t21;
    }
    {
        uint64_t t22 = GM_LD(uintptr_t, ((((uint8_t *)&F.l4) + 56) + 0) + 8);
        gm_l5 = t22;
    }
    {
        for (;;) {
            uint64_t t24 = gm_l5;
            uint8_t t25 = (t24 > ((uint64_t)0ULL));
            if (!t25) goto lb23;
            {
                uint64_t t28 = gm_l5;
                uint64_t r29; bool o30 = __builtin_sub_overflow(t28, ((uint64_t)1ULL), &r29);
                if (o30) gm_trap("integer overflow in `-`", gm_srcmok80ho8hh01, 979, 9);
                uint64_t t31 = r29;
                gm_l5 = t31;
            }
            {
                uint64_t t32 = gm_l5;
                uint64_t t33 = (uintptr_t)t32;
                uint64_t t34 = GM_LD(uintptr_t, ((((uint8_t *)&F.l4) + 56) + 0) + 8);
                if (t33 >= (uintptr_t)(t34)) gm_trap_index(t33, t34, gm_srcmok80ho8hh01, 980, 14);
                gm_tgtub0v94d5u5 t35 = GM_LD(gm_tgtub0v94d5u5, ((uint8_t *)GM_LD(void *, ((((uint8_t *)&F.l4) + 56) + 0)) + t33 * 16));
                gm_l6 = t35;
            }
            {
                uint8_t t37 = GM_LD(uint8_t, (((uint8_t *)&gm_l6) + 0));
                uint8_t t38 = (uint8_t)!t37;
                if (!t38) goto ie36;
                {
                    void * t40 = gm_l0;
                    gm_th2lqbo77pvsc t41;
                    memset(&t41, 0, sizeof t41);
                    void * t42 = GM_LD(void *, (((uint8_t *)&gm_l6) + 8));
                    gm_b0_1 t43 = gm_f_defer_body_avm7tau((void *)NULL, cx, NULL, t40, &t41, t42, ((uint8_t)0));
                    if (gm_unw.kind) {
                        goto gm_ll26;
                    }
                }
                goto ix36;
ie36:;
ix36:;
            }
lc23:;
        }
gm_ll26:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb23; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc23; }
        }
        goto gm_lad;
lb23:;
    }
    {
        void * t44 = gm_l0;
        gm_b0_1 t45 = gm_f_end_scope_45ar5qv((void *)NULL, cx, NULL, t44, &(*(gm_tehgl6lkqgvt0 *)((uint8_t *)&F.l4)), ((uint8_t)1));
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        if (!((((uint8_t *)&gm_l3))[16] != 0)) goto ie46;
        gm_b16_8 t47 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l3));
        gm_l7 = t47;
        {
            gm_b16_8 t49 = gm_l7;
            gm_t5o0tpv5jka2l t50;
            GM_ST(uint8_t, (uint8_t *)&t50 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t50 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t50 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t50 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t50 + 32, 0);
            ((uint8_t *)&t50)[0] = 0;
            GM_ST(gm_b16_8, ((uint8_t *)&t50 + 8), t49);
            gm_t5o0tpv5jka2l t51 = t50;
            uint8_t t52 = ((((uint8_t *)&t51))[0] != 0);
            F.ret = t50;
            goto gm_fret;
        }
        goto ix46;
ie46:;
ix46:;
    }
    {
        void * t53 = gm_l0;
        gm_b16_8 t54 = gm_f_unit_8ekedsl((void *)NULL, cx, NULL, t53);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5o0tpv5jka2l t55;
        GM_ST(uint8_t, (uint8_t *)&t55 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t55 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t55 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t55 + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&t55 + 32, 0);
        ((uint8_t *)&t55)[0] = 0;
        GM_ST(gm_b16_8, ((uint8_t *)&t55 + 8), t54);
        gm_t5o0tpv5jka2l t56 = t55;
        uint8_t t57 = ((((uint8_t *)&t56))[0] != 0);
        F.ret = t55;
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
inline __attribute__((aligned(4))) void gm_f_block_inner_83vev92(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2) {
    struct gm_fr_block_inner_83vev92 F;
    void * gm_l0; /* fg */
    void * gm_l1; /* b */
    void * gm_l2; /* result */
    void * gm_l3; /* t */
    gm_b16_8 gm_l4; /* v */
    GM_STACK_CHECK(&F, 745u);
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
            gm_tqgtd5gtgj65s t6 = t5;
            if (((((uint8_t *)&t6))[0] != 0)) {
                gm_t08olq97rscn3 t7 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t6) + 8));
                gm_tqgtd5gtgj65s t8;
                GM_ST(uint8_t, (uint8_t *)&t8 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t8 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t8 + 4, 0);
                ((uint8_t *)&t8)[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t8 + 8), t7);
                F.ret = t8;
                goto gm_fret;
            }
            gm_b0_1 t9 = GM_LD(gm_b0_1, (((uint8_t *)&t6) + 8));
        }
        {
            void * t11 = gm_l1;
            void * t12 = GM_LD(void *, (((uint8_t *)t11) + 16));
            void * t13 = t12;
            if (!(GM_LD(void *, ((uint8_t *)&t13)) != 0)) goto ie10;
            void * t14 = GM_LD(void *, ((uint8_t *)&t13));
            gm_l3 = t14;
            {
                void * t16 = gm_l0;
                void * t17 = gm_l3;
                gm_tl874t2atnlh3 t18 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t17) + 72));
                gm_b0_1 t19 = gm_f_src_line_d24sajl((void *)NULL, cx, NULL, t16, t18);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t20 = gm_l0;
                void * t21 = gm_l3;
                gm_t5o0tpv5jka2l t22;
                gm_f_expr_porbc9t((void *)&t22, (void *)NULL, cx, NULL, t20, t21);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t5o0tpv5jka2l t23 = t22;
                if (((((uint8_t *)&t23))[0] != 0)) {
                    gm_t08olq97rscn3 t24 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t23) + 8));
                    gm_tqgtd5gtgj65s t25;
                    GM_ST(uint8_t, (uint8_t *)&t25 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t25 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t25 + 4, 0);
                    ((uint8_t *)&t25)[0] = 1;
                    GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t25 + 8), t24);
                    F.ret = t25;
                    goto gm_fret;
                }
                gm_b16_8 t26 = GM_LD(gm_b16_8, (((uint8_t *)&t23) + 8));
                gm_l4 = t26;
            }
            {
                void * t28 = gm_l0;
                void * t29 = GM_LD(void *, (((uint8_t *)t28) + 0));
                void * t30 = (void *)(((uint8_t *)t29) + 8);
                void * t31 = gm_l3;
                gm_t88rcucg1b7uk t32 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t31) + 0));
                uint8_t t33 = gm_f_is_never_in5ftma(t30, GM_CONV(gm_csguankko4lmhd, t32));
                if (!t33) goto ie27;
                {
                    void * t35 = gm_l2;
                    uint8_t *t36 = ((uint8_t *)t35);
                    void * t37 = gm_l0;
                    void * t38 = gm_l3;
                    gm_t88rcucg1b7uk t39 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t38) + 0));
                    gm_b16_8 t40 = gm_f_zero_tlk13aa((void *)NULL, cx, NULL, t37, t39);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_th2lqbo77pvsc t41;
                    GM_ST(uint8_t, (uint8_t *)&t41 + 17, 0);
                    GM_ST(uint16_t, (uint8_t *)&t41 + 18, 0);
                    GM_ST(uint32_t, (uint8_t *)&t41 + 20, 0);
                    GM_ST(gm_b16_8, ((uint8_t *)&t41), t40);
                    ((uint8_t *)&t41)[16] = 1;
                    GM_ST(gm_th2lqbo77pvsc, t36, t41);
                }
                goto ix27;
ie27:;
                {
                    void * t43 = gm_l2;
                    uint8_t *t44 = ((uint8_t *)t43);
                    void * t45 = gm_l0;
                    void * t46 = gm_l3;
                    gm_t88rcucg1b7uk t47 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t46) + 0));
                    gm_b16_8 t48 = gm_l4;
                    gm_b16_8 t49 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t45, t47, t48);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_th2lqbo77pvsc t50;
                    GM_ST(uint8_t, (uint8_t *)&t50 + 17, 0);
                    GM_ST(uint16_t, (uint8_t *)&t50 + 18, 0);
                    GM_ST(uint32_t, (uint8_t *)&t50 + 20, 0);
                    GM_ST(gm_b16_8, ((uint8_t *)&t50), t49);
                    ((uint8_t *)&t50)[16] = 1;
                    GM_ST(gm_th2lqbo77pvsc, t44, t50);
                }
ix27:;
            }
            goto ix10;
ie10:;
ix10:;
        }
    }
    gm_b0_1 t51;
    gm_tqgtd5gtgj65s t52;
    memset(&t52, 0, sizeof t52);
    ((uint8_t *)&t52)[0] = 0;
    GM_ST(gm_b0_1, ((uint8_t *)&t52 + 8), t51);
    gm_tqgtd5gtgj65s t53 = t52;
    F.ret = t53;
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
    void * gm_l2; /* b */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    gm_trcmmd12cefb1 gm_l5; /* s */
    void * gm_l6; /* t */
    GM_STACK_CHECK(&F, 467u);
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
        uint8_t t26 = GM_LD(uint8_t, ((((uint8_t *)t19) + 8) + 0));
        switch (t26) { case ((uint8_t)0ULL): goto ma24; case ((uint8_t)1ULL): goto ma24; case ((uint8_t)2ULL): goto ma24; case ((uint8_t)3ULL): goto ma24; case ((uint8_t)4ULL): goto ma24; case ((uint8_t)5ULL): goto ma24; case ((uint8_t)6ULL): goto ma24; case ((uint8_t)7ULL): goto ma24; case ((uint8_t)8ULL): goto ma24; case ((uint8_t)9ULL): goto ma24; case ((uint8_t)10ULL): goto ma24; case ((uint8_t)11ULL): goto ma24; case ((uint8_t)12ULL): goto ma24; case ((uint8_t)13ULL): goto ma24; case ((uint8_t)14ULL): goto ma24; case ((uint8_t)15ULL): goto ma24; case ((uint8_t)16ULL): goto ma24; case ((uint8_t)17ULL): goto ma24; case ((uint8_t)18ULL): goto ma24; case ((uint8_t)19ULL): goto ma24; case ((uint8_t)20ULL): goto ma24; case ((uint8_t)21ULL): goto ma24; case ((uint8_t)22ULL): goto ma24; case ((uint8_t)23ULL): goto ma24; case ((uint8_t)24ULL): goto ma24; case ((uint8_t)25ULL): goto ma24; case ((uint8_t)26ULL): goto ma24; case ((uint8_t)27ULL): goto ma24; case ((uint8_t)28ULL): goto ma24; case ((uint8_t)29ULL): goto ma24; case ((uint8_t)30ULL): goto ma24; case ((uint8_t)31ULL): goto ma24; case ((uint8_t)32ULL): goto ma24; case ((uint8_t)33ULL): goto ma24; case ((uint8_t)34ULL): goto ma21; case ((uint8_t)35ULL): goto ma22; case ((uint8_t)36ULL): goto ma21; case ((uint8_t)37ULL): goto ma24; case ((uint8_t)38ULL): goto ma24; case ((uint8_t)39ULL): goto ma24; case ((uint8_t)40ULL): goto ma24; case ((uint8_t)41ULL): goto ma24; case ((uint8_t)42ULL): goto ma24; case ((uint8_t)43ULL): goto ma24; case ((uint8_t)44ULL): goto ma24; case ((uint8_t)45ULL): goto ma24; case ((uint8_t)46ULL): goto ma23; case ((uint8_t)47ULL): goto ma24; case ((uint8_t)48ULL): goto ma24; case ((uint8_t)49ULL): goto ma24; case ((uint8_t)50ULL): goto ma24; case ((uint8_t)51ULL): goto ma24; case ((uint8_t)52ULL): goto ma24; case ((uint8_t)53ULL): goto ma24; case ((uint8_t)54ULL): goto ma24; case ((uint8_t)55ULL): goto ma24; case ((uint8_t)56ULL): goto ma24; case ((uint8_t)57ULL): goto ma24; case ((uint8_t)58ULL): goto ma24; default: goto ma24; }
ma21:;
        uint8_t t30 = GM_LD(uint8_t, ((((uint8_t *)t19) + 8) + 0));
        if (t30 != ((uint8_t)34ULL)) goto pa29;
        goto po28;
pa29:;
        uint8_t t32 = GM_LD(uint8_t, ((((uint8_t *)t19) + 8) + 0));
        if (t32 != ((uint8_t)36ULL)) goto pa31;
        goto po28;
pa31:;
        goto mn27;
po28:;
        uint8_t t33 = 0;
        F.ret = ((uint8_t)1);
        goto gm_fret;
        goto me20;
mn27:;
ma22:;
        uint8_t t35 = GM_LD(uint8_t, ((((uint8_t *)t19) + 8) + 0));
        if (t35 != ((uint8_t)35ULL)) goto mn34;
        void * t36 = gm_l0;
        void * t37 = GM_LD(void *, (((uint8_t *)t36) + 0));
        void * t38 = (void *)(((uint8_t *)t37) + 8);
        void * t39 = gm_l1;
        gm_t88rcucg1b7uk t40 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t39) + 0));
        gm_t57euh612ligr t41;
        gm_f_bits_layout_23rect9((void *)&t41, (void *)NULL, cx, NULL, t38, t40);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t57euh612ligr t42 = t41;
        uint8_t t43;
        if (((((uint8_t *)&t42))[24] != 0)) goto mn45;
        t43 = ((uint8_t)1);
        goto me44;
mn45:;
        t43 = ((uint8_t)0);
        goto me44;
mn46:;
        gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 797, 33);
me44:;
        uint8_t t47 = 0;
        F.ret = t43;
        goto gm_fret;
        goto me20;
mn34:;
ma23:;
        uint8_t t49 = GM_LD(uint8_t, ((((uint8_t *)t19) + 8) + 0));
        if (t49 != ((uint8_t)46ULL)) goto mn48;
        void * t50 = GM_LD(void *, ((((uint8_t *)t19) + 8) + 8));
        gm_l2 = t50;
        {
            {
                void * t53 = gm_l2;
                gm_b16_8 t54 = GM_LD(gm_b16_8, (((uint8_t *)t53) + 0));
                gm_l3 = t54;
            }
            {
                gm_l4 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t56 = gm_l4;
                    uint64_t t57 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    uint8_t t58 = (t56 < t57);
                    if (!t58) goto lb55;
                    {
                        uint64_t t61 = gm_l4;
                        uint64_t t62 = (uintptr_t)t61;
                        uint64_t t63 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                        gm_trcmmd12cefb1 t66 = GM_LD(gm_trcmmd12cefb1, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t62 * 32));
                        gm_l5 = t66;
                        {
                            uint8_t t69;
                            uint8_t t72 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                            if (t72 != ((uint8_t)5ULL)) goto mn71;
                            t69 = ((uint8_t)1);
                            goto me70;
mn71:;
                            t69 = ((uint8_t)0);
                            goto me70;
mn73:;
                            gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 800, 20);
me70:;
                            if (!t69) goto ie68;
                            {
                                uint8_t t75 = 0;
                                F.ret = ((uint8_t)0);
                                goto gm_fret;
                            }
                            goto ix68;
ie68:;
ix68:;
                        }
                        goto me64;
mn65:;
                        gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 799, 13);
me64:;
                    }
lc55:;
                    {
                        uint64_t t77 = gm_l4;
                        uint64_t r78; bool o79 = __builtin_add_overflow(t77, ((uint64_t)1ULL), &r78);
                        uint64_t t80 = r78;
                        gm_l4 = t80;
                    }
                }
lb55:;
            }
        }
        {
            void * t81 = gm_l2;
            void * t82 = GM_LD(void *, (((uint8_t *)t81) + 16));
            void * t83 = t82;
            void * t84;
            if ((GM_LD(void *, ((uint8_t *)&t83)) != 0)) {
                void * t85 = GM_LD(void *, (((uint8_t *)&t83) + 0));
                t84 = t85;
            } else {
                uint8_t t86 = 0;
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            gm_l6 = t84;
        }
        {
            void * t87 = gm_l0;
            void * t88 = gm_l6;
            uint8_t t89 = gm_f_builds_in_place_ae1b90u((void *)NULL, cx, NULL, t87, t88);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t90 = 0;
            F.ret = t89;
            goto gm_fret;
        }
        goto me20;
mn48:;
ma24:;
        uint8_t t92 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
        goto me20;
mn91:;
ma25:;
        gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 795, 5);
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
    GM_STACK_CHECK(&F, 3119u);
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
                    gm_tn68el8d14vku t17 = t16;
                    uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)&t17) + 0));
                    gm_l7 = t20;
                    uint32_t t21 = GM_LD(uint32_t, (((uint8_t *)&t17) + 8));
                    gm_l8 = t21;
                    {
                        uint32_t t23 = gm_l8;
                        uint64_t t24 = (uint64_t)t23;
                        uint64_t t25 = (uintptr_t)t24;
                        void * t26 = gm_l2;
                        uint64_t t27 = GM_LD(uintptr_t, (((uint8_t *)t26) + 48) + 8);
                        if (t25 >= (uintptr_t)(t27)) gm_trap_index(t25, t27, gm_srcmok80ho8hh01, 354, 14);
                        gm_t88rcucg1b7uk t28 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, (((uint8_t *)t26) + 48)) + t25 * 40) + 16));
                        gm_l9 = t28;
                    }
                    {
                        void * t29 = (void *)((uint8_t *)&F.l3);
                        gm_t3877pk7lv748 t30;
                        void * t31 = gm_l0;
                        void * t32 = (void *)(((uint8_t *)t31) + 8);
                        gm_t88rcucg1b7uk t33 = gm_l9;
                        gm_b16_8 t34 = gm_f_plain_cty_q9lhlci((void *)NULL, cx, NULL, t32, t33);
                        if (gm_unw.kind) {
                            goto gm_ll9;
                        }
                        GM_ST(gm_b16_8, (uint8_t *)&t30 + 0, t34);
                        uint64_t t35 = gm_l7;
                        GM_ST(uint64_t, (uint8_t *)&t30 + 16, t35);
                        gm_tabb710d9mq2e t36;
                        gm_f_format_eq87866((void *)&t36, (void *)NULL, cx, NULL, &t30);
                        if (gm_unw.kind) {
                            goto gm_ll9;
                        }
                        gm_tabb710d9mq2e t37 = t36;
                        if (((((uint8_t *)&t37))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t37) + 8, gm_srcmok80ho8hh01, 355, 21);
                        gm_b16_8 t38 = GM_LD(gm_b16_8, (((uint8_t *)&t37) + 8));
                        gm_b16_8 t39 = t38;
                        gm_b16_8 t40 = GM_LD(gm_b16_8, ((uint8_t *)&t39));
                        gm_tftb1odc739t5 t41 = gm_f_push_3ef746b(t29, t40);
                        gm_tftb1odc739t5 t42 = t41;
                        if (((((uint8_t *)&t42))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t42) + 1, gm_srcmok80ho8hh01, 355, 9);
                        gm_b0_1 t43 = GM_LD(gm_b0_1, (((uint8_t *)&t42) + 1));
                    }
                    {
                        void * t45 = gm_l0;
                        void * t46 = (void *)(((uint8_t *)t45) + 8);
                        gm_t88rcucg1b7uk t47 = gm_l9;
                        uint8_t t48 = gm_f_by_ref_fi0o860((void *)NULL, cx, NULL, t46, t47);
                        if (gm_unw.kind) {
                            goto gm_ll9;
                        }
                        if (!t48) goto ie44;
                        {
                            void * t50 = (void *)((uint8_t *)&F.l4);
                            uint64_t t51 = gm_l7;
                            gm_t4ce0jd3tdro4 t52;
                            GM_ST(uint64_t, ((uint8_t *)&t52 + 0), t51);
                            gm_tabb710d9mq2e t53;
                            gm_f_format_ddrb17v((void *)&t53, (void *)NULL, cx, NULL, t52);
                            if (gm_unw.kind) {
                                goto gm_ll9;
                            }
                            gm_tabb710d9mq2e t54 = t53;
                            if (((((uint8_t *)&t54))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t54) + 8, gm_srcmok80ho8hh01, 356, 39);
                            gm_b16_8 t55 = GM_LD(gm_b16_8, (((uint8_t *)&t54) + 8));
                            gm_b16_8 t56 = t55;
                            gm_b16_8 t57 = GM_LD(gm_b16_8, ((uint8_t *)&t56));
                            gm_tftb1odc739t5 t58 = gm_f_push_3ef746b(t50, t57);
                            gm_tftb1odc739t5 t59 = t58;
                            if (((((uint8_t *)&t59))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t59) + 1, gm_srcmok80ho8hh01, 356, 29);
                            gm_b0_1 t60 = GM_LD(gm_b0_1, (((uint8_t *)&t59) + 1));
                        }
                        goto ix44;
ie44:;
                        {
                            void * t62 = (void *)((uint8_t *)&F.l4);
                            uint64_t t63 = gm_l7;
                            gm_t4ce0jd3tdro4 t64;
                            GM_ST(uint64_t, ((uint8_t *)&t64 + 0), t63);
                            gm_tabb710d9mq2e t65;
                            gm_f_format_l05am3t((void *)&t65, (void *)NULL, cx, NULL, t64);
                            if (gm_unw.kind) {
                                goto gm_ll9;
                            }
                            gm_tabb710d9mq2e t66 = t65;
                            if (((((uint8_t *)&t66))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t66) + 8, gm_srcmok80ho8hh01, 356, 83);
                            gm_b16_8 t67 = GM_LD(gm_b16_8, (((uint8_t *)&t66) + 8));
                            gm_b16_8 t68 = t67;
                            gm_b16_8 t69 = GM_LD(gm_b16_8, ((uint8_t *)&t68));
                            gm_tftb1odc739t5 t70 = gm_f_push_3ef746b(t62, t69);
                            gm_tftb1odc739t5 t71 = t70;
                            if (((((uint8_t *)&t71))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t71) + 1, gm_srcmok80ho8hh01, 356, 73);
                            gm_b0_1 t72 = GM_LD(gm_b0_1, (((uint8_t *)&t71) + 1));
                        }
ix44:;
                    }
                    goto me18;
mn19:;
                    gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 353, 5);
me18:;
                }
lc5:;
                {
                    uint64_t t74 = gm_l6;
                    uint64_t r75; bool o76 = __builtin_add_overflow(t74, ((uint64_t)1ULL), &r75);
                    uint64_t t77 = r75;
                    gm_l6 = t77;
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
        uint64_t t79 = GM_LD(uintptr_t, (((uint8_t *)&F.l3) + 0) + 8);
        uint8_t t80 = ((t79 == ((uint64_t)0ULL)));
        if (!t80) goto ie78;
        {
            void * t82 = (void *)((uint8_t *)&F.l3);
            gm_b16_8 t83;
            GM_ST(void *, (uint8_t *)&t83, (void *)(gm_sts5j6f5pkov0));
            GM_ST(uintptr_t, (uint8_t *)&t83 + 8, (uintptr_t)(4));
            gm_tftb1odc739t5 t84 = gm_f_push_3ef746b(t82, t83);
            gm_tftb1odc739t5 t85 = t84;
            if (((((uint8_t *)&t85))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t85) + 1, gm_srcmok80ho8hh01, 358, 32);
            gm_b0_1 t86 = GM_LD(gm_b0_1, (((uint8_t *)&t85) + 1));
        }
        goto ix78;
ie78:;
ix78:;
    }
    {
        gm_t6j6smj7qoctu t87;
        void * t88 = gm_l0;
        uint32_t t89 = gm_l1;
        gm_b16_8 t90 = gm_f_fn_name_6n34573((void *)NULL, cx, NULL, t88, t89);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t87 + 0, t90);
        gm_b16_8 t91 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 0));
        gm_b16_8 t92 = t91;
        gm_b16_8 t93 = GM_LD(gm_b16_8, ((uint8_t *)&t92));
        gm_b16_8 t94;
        GM_ST(void *, (uint8_t *)&t94, (void *)(gm_sg5gdnrith2i6));
        GM_ST(uintptr_t, (uint8_t *)&t94 + 8, (uintptr_t)(2));
        gm_tabb710d9mq2e t95;
        gm_f_join_sfknljb((void *)&t95, (void *)NULL, cx, NULL, t93, t94);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t96 = t95;
        if (((((uint8_t *)&t96))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t96) + 8, gm_srcmok80ho8hh01, 359, 48);
        gm_b16_8 t97 = GM_LD(gm_b16_8, (((uint8_t *)&t96) + 8));
        GM_ST(gm_b16_8, (uint8_t *)&t87 + 16, t97);
        gm_tabb710d9mq2e t98;
        gm_f_format_99gc605((void *)&t98, (void *)NULL, cx, NULL, &t87);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t99 = t98;
        if (((((uint8_t *)&t99))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t99) + 8, gm_srcmok80ho8hh01, 359, 13);
        gm_b16_8 t100 = GM_LD(gm_b16_8, (((uint8_t *)&t99) + 8));
        gm_l10 = t100;
    }
    {
        gm_b16_8 t101;
        GM_ST(void *, (uint8_t *)&t101, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t101 + 8, (uintptr_t)(0));
        gm_l11 = t101;
    }
    {
        gm_b16_8 t102;
        GM_ST(void *, (uint8_t *)&t102, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t102 + 8, (uintptr_t)(0));
        gm_l12 = t102;
    }
    {
        void * t104 = gm_l0;
        void * t105 = (void *)(((uint8_t *)t104) + 8);
        void * t106 = gm_l2;
        gm_t88rcucg1b7uk t107 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t106) + 64));
        uint8_t t108 = gm_f_is_unit_17uennd(t105, GM_CONV(gm_csguankko4lmhd, t107));
        if (!t108) goto ie103;
        {
            gm_b16_8 t110;
            GM_ST(void *, (uint8_t *)&t110, (void *)(gm_sts5j6f5pkov0));
            GM_ST(uintptr_t, (uint8_t *)&t110 + 8, (uintptr_t)(4));
            gm_l11 = t110;
        }
        {
            gm_b16_8 t111 = gm_l10;
            gm_tnpifv20irnph t112;
            GM_ST(gm_b16_8, ((uint8_t *)&t112 + 0), t111);
            gm_tabb710d9mq2e t113;
            gm_f_format_lsmcccn((void *)&t113, (void *)NULL, cx, NULL, t112);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t114 = t113;
            if (((((uint8_t *)&t114))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t114) + 8, gm_srcmok80ho8hh01, 364, 16);
            gm_b16_8 t115 = GM_LD(gm_b16_8, (((uint8_t *)&t114) + 8));
            gm_b16_8 t116 = t115;
            gm_b16_8 t117 = GM_LD(gm_b16_8, ((uint8_t *)&t116));
            gm_l12 = t117;
        }
        goto ix103;
ie103:;
        void * t119 = gm_l0;
        void * t120 = (void *)(((uint8_t *)t119) + 8);
        void * t121 = gm_l2;
        gm_t88rcucg1b7uk t122 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t121) + 64));
        uint8_t t123 = gm_f_by_ref_fi0o860((void *)NULL, cx, NULL, t120, t122);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t123) goto ie118;
        {
            void * t125 = gm_l0;
            void * t126 = (void *)(((uint8_t *)t125) + 8);
            void * t127 = gm_l2;
            gm_t88rcucg1b7uk t128 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t127) + 64));
            gm_b16_8 t129 = gm_f_plain_cty_q9lhlci((void *)NULL, cx, NULL, t126, t128);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l11 = t129;
        }
        {
            void * t130 = gm_l0;
            void * t131 = (void *)(((uint8_t *)t130) + 8);
            void * t132 = gm_l2;
            gm_t88rcucg1b7uk t133 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t132) + 64));
            gm_b16_8 t134 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t131, t133);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l13 = t134;
        }
        {
            gm_f_list_epvducg((void *)&F.l14, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t135 = (void *)((uint8_t *)&F.l14);
            gm_b16_8 t136;
            GM_ST(void *, (uint8_t *)&t136, (void *)(gm_s2254b9hkee3b));
            GM_ST(uintptr_t, (uint8_t *)&t136 + 8, (uintptr_t)(5));
            gm_tftb1odc739t5 t137 = gm_f_push_3ef746b(t135, t136);
            gm_tftb1odc739t5 t138 = t137;
            if (((((uint8_t *)&t138))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t138) + 1, gm_srcmok80ho8hh01, 369, 9);
            gm_b0_1 t139 = GM_LD(gm_b0_1, (((uint8_t *)&t138) + 1));
        }
        {
            void * t140 = (void *)((uint8_t *)&F.l14);
            gm_b16_8 t141 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 0));
            gm_b16_8 t142 = t141;
            gm_b16_8 t143 = GM_LD(gm_b16_8, ((uint8_t *)&t142));
            gm_tftb1odc739t5 t144 = gm_f_extend_vogsd72(t140, t143);
            gm_tftb1odc739t5 t145 = t144;
            if (((((uint8_t *)&t145))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t145) + 1, gm_srcmok80ho8hh01, 370, 9);
            gm_b0_1 t146 = GM_LD(gm_b0_1, (((uint8_t *)&t145) + 1));
        }
        {
            gm_t6j6smj7qoctu t147;
            void * t148 = gm_l0;
            uint32_t t149 = gm_l1;
            gm_b16_8 t150 = gm_f_fn_name_6n34573((void *)NULL, cx, NULL, t148, t149);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t147 + 0, t150);
            gm_b16_8 t151 = GM_LD(gm_b16_8, (((uint8_t *)&F.l14) + 0));
            gm_b16_8 t152 = t151;
            gm_b16_8 t153 = GM_LD(gm_b16_8, ((uint8_t *)&t152));
            gm_b16_8 t154;
            GM_ST(void *, (uint8_t *)&t154, (void *)(gm_sg5gdnrith2i6));
            GM_ST(uintptr_t, (uint8_t *)&t154 + 8, (uintptr_t)(2));
            gm_tabb710d9mq2e t155;
            gm_f_join_sfknljb((void *)&t155, (void *)NULL, cx, NULL, t153, t154);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t156 = t155;
            if (((((uint8_t *)&t156))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t156) + 8, gm_srcmok80ho8hh01, 371, 56);
            gm_b16_8 t157 = GM_LD(gm_b16_8, (((uint8_t *)&t156) + 8));
            GM_ST(gm_b16_8, (uint8_t *)&t147 + 16, t157);
            gm_tabb710d9mq2e t158;
            gm_f_format_99gc605((void *)&t158, (void *)NULL, cx, NULL, &t147);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t159 = t158;
            if (((((uint8_t *)&t159))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t159) + 8, gm_srcmok80ho8hh01, 371, 21);
            gm_b16_8 t160 = GM_LD(gm_b16_8, (((uint8_t *)&t159) + 8));
            gm_l15 = t160;
        }
        {
            gm_b16_8 t161;
            GM_ST(void *, (uint8_t *)&t161, (void *)(gm_ss03naco6s5l7));
            GM_ST(uintptr_t, (uint8_t *)&t161 + 8, (uintptr_t)(4));
            gm_l16 = t161;
        }
        {
            void * t163 = gm_l0;
            void * t164 = (void *)(((uint8_t *)t163) + 8);
            void * t165 = gm_l2;
            gm_t88rcucg1b7uk t166 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t165) + 64));
            gm_th2lqbo77pvsc t167;
            gm_f_mirror_lcjegsf((void *)&t167, (void *)NULL, cx, NULL, t164, t166);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_th2lqbo77pvsc t168 = t167;
            if (!((((uint8_t *)&t168))[16] != 0)) goto ie162;
            gm_b16_8 t169 = GM_LD(gm_b16_8, ((uint8_t *)&t168));
            gm_l17 = t169;
            {
                gm_b16_8 t171 = gm_l17;
                gm_tnpifv20irnph t172;
                GM_ST(gm_b16_8, ((uint8_t *)&t172 + 0), t171);
                gm_tabb710d9mq2e t173;
                gm_f_format_cp0hre9((void *)&t173, (void *)NULL, cx, NULL, t172);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t174 = t173;
                if (((((uint8_t *)&t174))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t174) + 8, gm_srcmok80ho8hh01, 373, 44);
                gm_b16_8 t175 = GM_LD(gm_b16_8, (((uint8_t *)&t174) + 8));
                gm_b16_8 t176 = t175;
                gm_b16_8 t177 = GM_LD(gm_b16_8, ((uint8_t *)&t176));
                gm_l16 = t177;
            }
            goto ix162;
ie162:;
ix162:;
        }
        {
            gm_tfeab279c16rr t178;
            gm_b16_8 t179 = gm_l13;
            GM_ST(gm_b16_8, (uint8_t *)&t178 + 0, t179);
            gm_b16_8 t180 = gm_l15;
            GM_ST(gm_b16_8, (uint8_t *)&t178 + 16, t180);
            gm_b16_8 t181 = gm_l16;
            GM_ST(gm_b16_8, (uint8_t *)&t178 + 32, t181);
            gm_tabb710d9mq2e t182;
            gm_f_format_u9etquj((void *)&t182, (void *)NULL, cx, NULL, &t178);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t183 = t182;
            if (((((uint8_t *)&t183))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t183) + 8, gm_srcmok80ho8hh01, 374, 16);
            gm_b16_8 t184 = GM_LD(gm_b16_8, (((uint8_t *)&t183) + 8));
            gm_b16_8 t185 = t184;
            gm_b16_8 t186 = GM_LD(gm_b16_8, ((uint8_t *)&t185));
            gm_l12 = t186;
        }
        goto ix118;
ie118:;
        {
            void * t188 = gm_l0;
            void * t189 = (void *)(((uint8_t *)t188) + 8);
            void * t190 = gm_l2;
            gm_t88rcucg1b7uk t191 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t190) + 64));
            gm_b16_8 t192 = gm_f_plain_cty_q9lhlci((void *)NULL, cx, NULL, t189, t191);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l11 = t192;
        }
        {
            gm_b16_8 t193 = gm_l10;
            gm_l18 = t193;
        }
        {
            void * t195 = gm_l0;
            void * t196 = (void *)(((uint8_t *)t195) + 8);
            void * t197 = gm_l2;
            gm_t88rcucg1b7uk t198 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t197) + 64));
            gm_th2lqbo77pvsc t199;
            gm_f_mirror_lcjegsf((void *)&t199, (void *)NULL, cx, NULL, t196, t198);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_th2lqbo77pvsc t200 = t199;
            if (!((((uint8_t *)&t200))[16] != 0)) goto ie194;
            gm_b16_8 t201 = GM_LD(gm_b16_8, ((uint8_t *)&t200));
            gm_l19 = t201;
            {
                void * t204 = gm_l0;
                void * t205 = (void *)(((uint8_t *)t204) + 8);
                void * t206 = gm_l2;
                gm_t88rcucg1b7uk t207 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t206) + 64));
                uint8_t t208 = gm_f_by_ref_fi0o860((void *)NULL, cx, NULL, t205, t207);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (!t208) goto ie203;
                {
                    gm_t6j6smj7qoctu t210;
                    gm_b16_8 t211 = gm_l19;
                    GM_ST(gm_b16_8, (uint8_t *)&t210 + 0, t211);
                    gm_b16_8 t212 = gm_l10;
                    GM_ST(gm_b16_8, (uint8_t *)&t210 + 16, t212);
                    gm_tabb710d9mq2e t213;
                    gm_f_format_4hn970h((void *)&t213, (void *)NULL, cx, NULL, &t210);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tabb710d9mq2e t214 = t213;
                    if (((((uint8_t *)&t214))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t214) + 8, gm_srcmok80ho8hh01, 379, 43);
                    gm_b16_8 t215 = GM_LD(gm_b16_8, (((uint8_t *)&t214) + 8));
                    gm_l18 = t215;
                }
                goto ix203;
ie203:;
ix203:;
            }
            goto ix194;
ie194:;
ix194:;
        }
        {
            gm_b16_8 t216 = gm_l18;
            gm_tnpifv20irnph t217;
            GM_ST(gm_b16_8, ((uint8_t *)&t217 + 0), t216);
            gm_tabb710d9mq2e t218;
            gm_f_format_ko1kpil((void *)&t218, (void *)NULL, cx, NULL, t217);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t219 = t218;
            if (((((uint8_t *)&t219))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t219) + 8, gm_srcmok80ho8hh01, 381, 16);
            gm_b16_8 t220 = GM_LD(gm_b16_8, (((uint8_t *)&t219) + 8));
            gm_b16_8 t221 = t220;
            gm_b16_8 t222 = GM_LD(gm_b16_8, ((uint8_t *)&t221));
            gm_l12 = t222;
        }
ix118:;
ix103:;
    }
    {
        gm_t0uuhaljsf410 t223;
        void * t224 = gm_l2;
        uint8_t t225 = GM_LD(uint8_t, (((uint8_t *)t224) + 144));
        gm_b16_8 t226 = gm_f_conv_attr_m0b89pc(t225);
        GM_ST(gm_b16_8, (uint8_t *)&t223 + 0, t226);
        gm_b16_8 t227 = gm_l11;
        GM_ST(gm_b16_8, (uint8_t *)&t223 + 16, t227);
        void * t228 = gm_l0;
        uint32_t t229 = gm_l1;
        gm_b16_8 t230 = gm_f_fn_name_6n34573((void *)NULL, cx, NULL, t228, t229);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t223 + 32, t230);
        gm_b16_8 t231 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
        gm_b16_8 t232 = t231;
        gm_b16_8 t233 = GM_LD(gm_b16_8, ((uint8_t *)&t232));
        gm_b16_8 t234;
        GM_ST(void *, (uint8_t *)&t234, (void *)(gm_sg5gdnrith2i6));
        GM_ST(uintptr_t, (uint8_t *)&t234 + 8, (uintptr_t)(2));
        gm_tabb710d9mq2e t235;
        gm_f_join_sfknljb((void *)&t235, (void *)NULL, cx, NULL, t233, t234);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t236 = t235;
        if (((((uint8_t *)&t236))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t236) + 8, gm_srcmok80ho8hh01, 383, 116);
        gm_b16_8 t237 = GM_LD(gm_b16_8, (((uint8_t *)&t236) + 8));
        GM_ST(gm_b16_8, (uint8_t *)&t223 + 48, t237);
        gm_tabb710d9mq2e t238;
        gm_f_format_kadoe0b((void *)&t238, (void *)NULL, cx, NULL, &t223);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t239 = t238;
        if (((((uint8_t *)&t239))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t239) + 8, gm_srcmok80ho8hh01, 383, 12);
        gm_b16_8 t240 = GM_LD(gm_b16_8, (((uint8_t *)&t239) + 8));
        gm_l20 = t240;
    }
    {
        void * t241 = gm_l0;
        void * t242 = (void *)(((uint8_t *)t241) + 1888);
        gm_b16_8 t243 = gm_l20;
        gm_tnpifv20irnph t244;
        GM_ST(gm_b16_8, ((uint8_t *)&t244 + 0), t243);
        gm_tftb1odc739t5 t245 = gm_f_write_i2vvij8(t242, t244);
        gm_tftb1odc739t5 t246 = t245;
        if (((((uint8_t *)&t246))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t246) + 1, gm_srcmok80ho8hh01, 384, 5);
        gm_b0_1 t247 = GM_LD(gm_b0_1, (((uint8_t *)&t246) + 1));
    }
    {
        void * t248 = gm_l0;
        void * t249 = (void *)(((uint8_t *)t248) + 1928);
        gm_t6j6smj7qoctu t250;
        gm_b16_8 t251 = gm_l20;
        GM_ST(gm_b16_8, (uint8_t *)&t250 + 0, t251);
        gm_b16_8 t252 = gm_l12;
        GM_ST(gm_b16_8, (uint8_t *)&t250 + 16, t252);
        gm_tftb1odc739t5 t253 = gm_f_write_ildv1bj(t249, &t250);
        gm_tftb1odc739t5 t254 = t253;
        if (((((uint8_t *)&t254))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t254) + 1, gm_srcmok80ho8hh01, 385, 5);
        gm_b0_1 t255 = GM_LD(gm_b0_1, (((uint8_t *)&t254) + 1));
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
    GM_STACK_CHECK(&F, 350u);
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
                gm_tftb1odc739t5 t14 = t13;
                if (((((uint8_t *)&t14))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t14) + 1, gm_srcmok80ho8hh01, 290, 41);
                gm_b0_1 t15 = GM_LD(gm_b0_1, (((uint8_t *)&t14) + 1));
            }
lc4:;
        }
lb4:;
    }
    {
        uint64_t t16 = gm_l2;
        uint64_t t17 = (uintptr_t)t16;
        void * t18 = gm_l0;
        uint64_t t19 = GM_LD(uintptr_t, ((((uint8_t *)t18) + 3040) + 0) + 8);
        if (t17 >= (uintptr_t)(t19)) gm_trap_index(t17, t19, gm_srcmok80ho8hh01, 291, 14);
        uint8_t t20 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t18) + 3040) + 0)) + t17 * 1));
        gm_l3 = t20;
    }
    {
        uint8_t t22 = gm_l3;
        uint8_t t23 = ((t22 == ((uint8_t)1ULL)));
        if (!t23) goto ie21;
        {
            uint8_t t25 = GM_LD(uint8_t, gm_c71idar1l1k);
            uint32_t t26 = (uint32_t)t25;
            uint8_t t27 = 0;
            F.ret = t26;
            goto gm_fret;
        }
        goto ix21;
ie21:;
ix21:;
    }
    {
        uint8_t t29 = gm_l3;
        uint8_t t30 = (t29 > ((uint8_t)1ULL));
        if (!t30) goto ie28;
        {
            uint8_t t32 = gm_l3;
            uint32_t t33 = (uint32_t)t32;
            uint32_t r34; bool o35 = __builtin_sub_overflow(t33, ((uint32_t)2ULL), &r34);
            if (o35) gm_trap("integer overflow in `-`", gm_srcmok80ho8hh01, 294, 27);
            uint32_t t36 = r34;
            uint8_t t37 = 0;
            F.ret = t36;
            goto gm_fret;
        }
        goto ix28;
ie28:;
ix28:;
    }
    {
        uint64_t t38 = gm_l2;
        uint64_t t39 = (uintptr_t)t38;
        void * t40 = gm_l0;
        uint64_t t41 = GM_LD(uintptr_t, ((((uint8_t *)t40) + 3040) + 0) + 8);
        if (t39 >= (uintptr_t)(t41)) gm_trap_index(t39, t41, gm_srcmok80ho8hh01, 295, 5);
        GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t40) + 3040) + 0)) + t39 * 1), ((uint8_t)1ULL));
    }
    {
        uint8_t t42 = GM_LD(uint8_t, gm_c71idar1l1k);
        uint32_t t43 = (uint32_t)t42;
        gm_l4 = t43;
    }
    {
        void * t44 = gm_l0;
        void * t45 = GM_LD(void *, (((uint8_t *)t44) + 0));
        uint32_t t46 = gm_l1;
        void * t47 = gm_f_func_in17r5n((void *)NULL, cx, NULL, t45, t46);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l5 = t47;
    }
    {
        void * t49 = gm_l5;
        void * t50 = GM_LD(void *, (((uint8_t *)t49) + 152));
        void * t51 = t50;
        if (!(GM_LD(void *, ((uint8_t *)&t51)) != 0)) goto ie48;
        void * t52 = GM_LD(void *, ((uint8_t *)&t51));
        gm_l6 = t52;
        void * t53 = gm_l5;
        uint8_t t54;
        uint8_t t57 = GM_LD(uint8_t, ((((uint8_t *)t53) + 96) + 0));
        if (t57 != ((uint8_t)3ULL)) goto mn56;
        t54 = ((uint8_t)0);
        goto me55;
mn56:;
        t54 = ((uint8_t)1);
        goto me55;
mn58:;
        gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 298, 26);
me55:;
        if (!t54) goto ie48;
        {
            void * t60 = gm_l0;
            void * t61 = gm_l6;
            uint32_t t62 = gm_f_expr_call_depth_dliqc7u((void *)NULL, cx, NULL, t60, t61);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l4 = t62;
        }
        goto ix48;
ie48:;
ix48:;
    }
    {
        uint64_t t63 = gm_l2;
        uint64_t t64 = (uintptr_t)t63;
        void * t65 = gm_l0;
        uint64_t t66 = GM_LD(uintptr_t, ((((uint8_t *)t65) + 3040) + 0) + 8);
        if (t64 >= (uintptr_t)(t66)) gm_trap_index(t64, t66, gm_srcmok80ho8hh01, 299, 5);
        uint8_t *t67 = ((uint8_t *)GM_LD(void *, ((((uint8_t *)t65) + 3040) + 0)) + t64 * 1);
        uint8_t t68;
        uint32_t t70 = gm_l4;
        uint32_t t71 = GM_LD(uint32_t, gm_cdmki9acqi9);
        uint8_t t72 = (t70 > t71);
        if (!t72) goto ie69;
        uint8_t t74 = GM_LD(uint8_t, gm_c71idar1l1k);
        uint8_t t75 = t74;
        t68 = t75;
        goto ix69;
ie69:;
        uint32_t t77 = gm_l4;
        uint8_t t78 = (uint8_t)t77;
        uint8_t r79; bool o80 = __builtin_add_overflow(t78, ((uint8_t)2ULL), &r79);
        if (o80) gm_trap("integer overflow in `+`", gm_srcmok80ho8hh01, 299, 79);
        uint8_t t81 = r79;
        uint8_t t82 = t81;
        t68 = t82;
ix69:;
        GM_ST(uint8_t, t67, t68);
    }
    {
        uint32_t t83 = gm_l4;
        uint8_t t84 = 0;
        F.ret = t83;
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
        gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 324, 29);
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
        gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 323, 5);
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
    GM_STACK_CHECK(&F, 449u);
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
                    uint64_t t18 = t17;
                    uint64_t t21 = GM_LD(uint64_t, ((uint8_t *)&t18));
                    gm_l6 = t21;
                    {
                        void * t23 = gm_l0;
                        void * t24 = GM_LD(void *, (((uint8_t *)t23) + 0));
                        uint64_t t25 = gm_l6;
                        uint32_t t26 = (uint32_t)t25;
                        void * t27 = gm_f_func_in17r5n((void *)NULL, cx, NULL, t24, t26);
                        if (gm_unw.kind) {
                            goto gm_ll15;
                        }
                        gm_l7 = t27;
                    }
                    {
                        void * t29 = gm_l7;
                        void * t30 = GM_LD(void *, (((uint8_t *)t29) + 152));
                        void * t31 = t30;
                        if (!(GM_LD(void *, ((uint8_t *)&t31)) != 0)) goto ie28;
                        void * t32 = GM_LD(void *, ((uint8_t *)&t31));
                        gm_l8 = t32;
                        {
                            void * t34 = gm_l0;
                            void * t35 = GM_LD(void *, (((uint8_t *)t34) + 0));
                            uint64_t t36 = gm_l6;
                            uint32_t t37 = (uint32_t)t36;
                            void * t38 = gm_l8;
                            void * t39 = (void *)((uint8_t *)&F.l2);
                            gm_b0_1 t40 = gm_f_collect_upvars_q0085da((void *)NULL, cx, NULL, t35, t37, t38, t39);
                            if (gm_unw.kind) {
                                goto gm_ll15;
                            }
                        }
                        goto ix28;
ie28:;
ix28:;
                    }
                    goto me19;
mn20:;
                    gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 1185, 5);
me19:;
                }
lc11:;
                {
                    uint64_t t42 = gm_l5;
                    uint64_t r43; bool o44 = __builtin_add_overflow(t42, ((uint64_t)1ULL), &r43);
                    uint64_t t45 = r43;
                    gm_l5 = t45;
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
        void * t46 = gm_l0;
        gm_t4c7ipjj9s3fp t47 = F.l2;
        gm_tuutj662vpgtg t48;
        GM_ST(uint8_t, (uint8_t *)&t48 + 57, 0);
        GM_ST(uint16_t, (uint8_t *)&t48 + 58, 0);
        GM_ST(uint32_t, (uint8_t *)&t48 + 60, 0);
        GM_ST(gm_t4c7ipjj9s3fp, ((uint8_t *)&t48), t47);
        ((uint8_t *)&t48)[56] = 1;
        GM_ST(gm_tuutj662vpgtg, (((uint8_t *)t46) + 2896), t48);
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
    GM_STACK_CHECK(&F, 170u);
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
                        uint32_t t18 = t17;
                        {
                            void * t23 = F.l0;
                            uint32_t t24 = gm_l6;
                            void * t25 = gm_f_func_in17r5n((void *)NULL, cx, NULL, t23, t24);
                            if (gm_unw.kind) {
                                goto gm_ll15;
                            }
                            if (!(((((uint8_t *)t25) + 88))[4] != 0)) goto ie22;
                            uint32_t t26 = GM_LD(uint32_t, (((uint8_t *)t25) + 88));
                            gm_l9 = t26;
                            {
                                uint32_t t28 = gm_l9;
                                gm_l6 = t28;
                            }
                            goto ix22;
ie22:;
ix22:;
                        }
                        goto me19;
mn20:;
                        gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 1205, 9);
me19:;
                    }
lc11:;
                    {
                        uint32_t t30 = gm_l8;
                        uint32_t r31; bool o32 = __builtin_add_overflow(t30, ((uint32_t)1ULL), &r31);
                        uint32_t t33 = r31;
                        gm_l8 = t33;
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
            void * t34 = F.l3;
            uint32_t t35 = gm_l6;
            uint32_t t36 = gm_l5;
            gm_tcc1rq4n1avb2 t37;
            GM_ST(uint32_t, ((uint8_t *)&t37 + 0), t35);
            GM_ST(uint32_t, ((uint8_t *)&t37 + 4), t36);
            gm_tftb1odc739t5 t38 = gm_f_put_3g1tdpr(t34, t37, ((uint8_t)1));
            gm_tftb1odc739t5 t39 = t38;
            if (((((uint8_t *)&t39))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t39) + 1, gm_srcmok80ho8hh01, 1208, 9);
            gm_b0_1 t40 = GM_LD(gm_b0_1, (((uint8_t *)&t39) + 1));
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t41 = gm_l2;
        gm_closure t42 = ((gm_closure){(void *)gm_f_fn_jjarlg6, (void *)&F.h, cx});
        gm_f_for_each_child_fm49s73(t41, t42);
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
    GM_STACK_CHECK(&F, 125u);
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
            gm_tftb1odc739t5 t13 = t12;
            if (((((uint8_t *)&t13))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t13) + 1, gm_srcmok80ho8hh01, 417, 29);
            gm_b0_1 t14 = GM_LD(gm_b0_1, (((uint8_t *)&t13) + 1));
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t15 = gm_l0;
        gm_closure t16 = ((gm_closure){(void *)gm_f_fn_kssb0q2, (void *)&F.h, cx});
        gm_f_for_each_child_fm49s73(t15, t16);
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
        if (o20) gm_trap("integer overflow in `+`", gm_srcmok80ho8hh01, 698, 5);
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
        if (o42) gm_trap("integer overflow in `-`", gm_srcmok80ho8hh01, 704, 5);
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
    GM_STACK_CHECK(&F, 1067u);
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
            gm_tabb710d9mq2e t16 = t15;
            if (((((uint8_t *)&t16))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t16) + 8, gm_srcmok80ho8hh01, 649, 16);
            gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)&t16) + 8));
            gm_b16_8 t18 = t17;
            gm_b16_8 t19 = GM_LD(gm_b16_8, ((uint8_t *)&t18));
            gm_l3 = t19;
        }
        {
            void * t20 = gm_l0;
            gm_b16_8 t21 = gm_l3;
            gm_tnpifv20irnph t22;
            GM_ST(gm_b16_8, ((uint8_t *)&t22 + 0), t21);
            gm_b0_1 t23 = gm_f_linef_njoe3l6((void *)NULL, cx, NULL, t20, t22);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix9;
ie9:;
ix9:;
    }
    {
        uint8_t t29 = GM_LD(uint8_t, ((((uint8_t *)a1) + 0) + 0));
        switch (t29) { case ((uint8_t)0ULL): goto ma25; case ((uint8_t)1ULL): goto ma26; case ((uint8_t)2ULL): goto ma27; default: goto ma28; }
ma25:;
        uint8_t t31 = GM_LD(uint8_t, ((((uint8_t *)a1) + 0) + 0));
        if (t31 != ((uint8_t)0ULL)) goto mn30;
        {
            uint64_t t33 = GM_LD(uint64_t, (((uint8_t *)a1) + 96));
            gm_l4 = t33;
        }
        {
            for (;;) {
                uint64_t t35 = gm_l4;
                uint8_t t36 = (t35 >= ((uint64_t)1ULL));
                if (!t36) goto lb34;
                {
                    void * t39 = gm_l0;
                    uint32_t t40 = GM_LD(uint32_t, (((uint8_t *)a1) + 48));
                    uint64_t t41 = gm_l4;
                    gm_tb31k5l794t7p t42;
                    GM_ST(uint32_t, (uint8_t *)&t42 + 4, 0);
                    GM_ST(uint32_t, ((uint8_t *)&t42 + 0), t40);
                    GM_ST(uint64_t, ((uint8_t *)&t42 + 8), t41);
                    gm_tabb710d9mq2e t43;
                    gm_f_format_5bcbbmm((void *)&t43, (void *)NULL, cx, NULL, t42);
                    if (gm_unw.kind) {
                        goto gm_ll37;
                    }
                    gm_tabb710d9mq2e t44 = t43;
                    if (((((uint8_t *)&t44))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t44) + 8, gm_srcmok80ho8hh01, 656, 26);
                    gm_b16_8 t45 = GM_LD(gm_b16_8, (((uint8_t *)&t44) + 8));
                    gm_b16_8 t46 = t45;
                    gm_b16_8 t47 = GM_LD(gm_b16_8, ((uint8_t *)&t46));
                    gm_b0_1 t48 = gm_f_label_710oev4((void *)NULL, cx, NULL, t39, t47);
                    if (gm_unw.kind) {
                        goto gm_ll37;
                    }
                }
                {
                    uint64_t t49 = gm_l4;
                    uint64_t r50; bool o51 = __builtin_sub_overflow(t49, ((uint64_t)1ULL), &r50);
                    if (o51) gm_trap("integer overflow in `-`", gm_srcmok80ho8hh01, 657, 37);
                    uint64_t t52 = r50;
                    uint64_t t53 = (uintptr_t)t52;
                    uint64_t t54 = GM_LD(uintptr_t, ((((uint8_t *)a1) + 56) + 0) + 8);
                    if (t53 >= (uintptr_t)(t54)) gm_trap_index(t53, t54, gm_srcmok80ho8hh01, 657, 22);
                    gm_tgtub0v94d5u5 t55 = GM_LD(gm_tgtub0v94d5u5, ((uint8_t *)GM_LD(void *, ((((uint8_t *)a1) + 56) + 0)) + t53 * 16));
                    gm_l5 = t55;
                }
                {
                    gm_th2lqbo77pvsc t56;
                    memset(&t56, 0, sizeof t56);
                    gm_l6 = t56;
                }
                {
                    uint8_t t58 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                    if (!t58) goto ie57;
                    {
                        gm_b16_8 t60;
                        GM_ST(void *, (uint8_t *)&t60, (void *)(gm_sm7odqe0d6lgu));
                        GM_ST(uintptr_t, (uint8_t *)&t60 + 8, (uintptr_t)(22));
                        gm_th2lqbo77pvsc t61;
                        GM_ST(uint8_t, (uint8_t *)&t61 + 17, 0);
                        GM_ST(uint16_t, (uint8_t *)&t61 + 18, 0);
                        GM_ST(uint32_t, (uint8_t *)&t61 + 20, 0);
                        GM_ST(gm_b16_8, ((uint8_t *)&t61), t60);
                        ((uint8_t *)&t61)[16] = 1;
                        gm_l6 = t61;
                    }
                    goto ix57;
ie57:;
ix57:;
                }
                {
                    void * t62 = gm_l0;
                    void * t63 = GM_LD(void *, (((uint8_t *)&gm_l5) + 8));
                    gm_b0_1 t64 = gm_f_defer_body_avm7tau((void *)NULL, cx, NULL, t62, &(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l6)), t63, ((uint8_t)1));
                    if (gm_unw.kind) {
                        goto gm_ll37;
                    }
                }
                {
                    uint64_t t65 = gm_l4;
                    uint64_t r66; bool o67 = __builtin_sub_overflow(t65, ((uint64_t)1ULL), &r66);
                    if (o67) gm_trap("integer overflow in `-`", gm_srcmok80ho8hh01, 661, 17);
                    uint64_t t68 = r66;
                    gm_l4 = t68;
                }
lc34:;
            }
gm_ll37:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb34; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc34; }
            }
            goto gm_lad;
lb34:;
        }
        goto me24;
mn30:;
ma26:;
        uint8_t t70 = GM_LD(uint8_t, ((((uint8_t *)a1) + 0) + 0));
        if (t70 != ((uint8_t)1ULL)) goto mn69;
        gm_thognv9odov03 t71 = GM_LD(gm_thognv9odov03, ((((uint8_t *)a1) + 0) + 8));
        F.l7 = t71;
        {
            void * t73 = gm_l0;
            uint32_t t74 = GM_LD(uint32_t, (((uint8_t *)a1) + 48));
            gm_t88rcucg1b7uk t75;
            GM_ST(uint32_t, ((uint8_t *)&t75 + 0), t74);
            gm_tabb710d9mq2e t76;
            gm_f_format_dd6d4bt((void *)&t76, (void *)NULL, cx, NULL, t75);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t77 = t76;
            if (((((uint8_t *)&t77))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t77) + 8, gm_srcmok80ho8hh01, 665, 22);
            gm_b16_8 t78 = GM_LD(gm_b16_8, (((uint8_t *)&t77) + 8));
            gm_b16_8 t79 = t78;
            gm_b16_8 t80 = GM_LD(gm_b16_8, ((uint8_t *)&t79));
            gm_b0_1 t81 = gm_f_label_710oev4((void *)NULL, cx, NULL, t73, t80);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t82 = gm_l0;
            uint32_t t83 = GM_LD(uint32_t, (((uint8_t *)&F.l7) + 0));
            gm_t88rcucg1b7uk t84;
            GM_ST(uint32_t, ((uint8_t *)&t84 + 0), t83);
            gm_b0_1 t85 = gm_f_linef_aka1a8l((void *)NULL, cx, NULL, t82, t84);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t86 = gm_l0;
            gm_b16_8 t87 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 8));
            gm_tnpifv20irnph t88;
            GM_ST(gm_b16_8, ((uint8_t *)&t88 + 0), t87);
            gm_b0_1 t89 = gm_f_linef_2n883h6((void *)NULL, cx, NULL, t86, t88);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t90 = gm_l0;
            gm_b16_8 t91 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 24));
            gm_tnpifv20irnph t92;
            GM_ST(gm_b16_8, ((uint8_t *)&t92 + 0), t91);
            gm_b0_1 t93 = gm_f_linef_mkfjg2u((void *)NULL, cx, NULL, t90, t92);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t94 = gm_l0;
            gm_b16_8 t95;
            GM_ST(void *, (uint8_t *)&t95, (void *)(gm_s2cuflhqkkuaq));
            GM_ST(uintptr_t, (uint8_t *)&t95 + 8, (uintptr_t)(1));
            gm_b0_1 t96 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t94, t95);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto me24;
mn69:;
ma27:;
        uint8_t t98 = GM_LD(uint8_t, ((((uint8_t *)a1) + 0) + 0));
        if (t98 != ((uint8_t)2ULL)) goto mn97;
        gm_tiilmfd16v3fc t99 = GM_LD(gm_tiilmfd16v3fc, ((((uint8_t *)a1) + 0) + 8));
        gm_l8 = t99;
        {
            void * t101 = gm_l0;
            uint32_t t102 = GM_LD(uint32_t, (((uint8_t *)a1) + 48));
            gm_t88rcucg1b7uk t103;
            GM_ST(uint32_t, ((uint8_t *)&t103 + 0), t102);
            gm_tabb710d9mq2e t104;
            gm_f_format_dd6d4bt((void *)&t104, (void *)NULL, cx, NULL, t103);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t105 = t104;
            if (((((uint8_t *)&t105))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t105) + 8, gm_srcmok80ho8hh01, 672, 22);
            gm_b16_8 t106 = GM_LD(gm_b16_8, (((uint8_t *)&t105) + 8));
            gm_b16_8 t107 = t106;
            gm_b16_8 t108 = GM_LD(gm_b16_8, ((uint8_t *)&t107));
            gm_b0_1 t109 = gm_f_label_710oev4((void *)NULL, cx, NULL, t101, t108);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t110 = gm_l0;
            gm_tiilmfd16v3fc t111;
            memset((uint8_t *)&t111 + 4, 0, 4);
            uint32_t t112 = GM_LD(uint32_t, (((uint8_t *)&gm_l8) + 0));
            GM_ST(uint32_t, (uint8_t *)&t111 + 0, t112);
            gm_b16_8 t113 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l8) + 8));
            GM_ST(gm_b16_8, (uint8_t *)&t111 + 8, t113);
            gm_b0_1 t114 = gm_f_linef_1p9d72n((void *)NULL, cx, NULL, t110, &t111);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto me24;
mn97:;
ma28:;
        gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 652, 5);
me24:;
    }
    {
        void * t115 = gm_l0;
        gm_b0_1 t116 = gm_f_ladder_36amdbn((void *)NULL, cx, NULL, t115);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint8_t t118 = gm_l2;
        if (!t118) goto ie117;
        {
            void * t120 = gm_l0;
            gm_b16_8 t121 = gm_l3;
            gm_b0_1 t122 = gm_f_label_710oev4((void *)NULL, cx, NULL, t120, t121);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix117;
ie117:;
ix117:;
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
    gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 714, 45);
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
        gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 307, 29);
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
        gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 312, 31);
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
        gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 306, 5);
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
    void * gm_l47; /* b */
    void * gm_l49; /* t */
    GM_STACK_CHECK(&F, 5491u);
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
                gm_t5o0tpv5jka2l t12 = t11;
                if (((((uint8_t *)&t12))[0] != 0)) {
                    gm_t08olq97rscn3 t13 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t12) + 8));
                    gm_tqgtd5gtgj65s t14;
                    GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
                    ((uint8_t *)&t14)[0] = 1;
                    GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t14 + 8), t13);
                    F.ret = t14;
                    goto gm_fret;
                }
                gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&t12) + 8));
                gm_l4 = t15;
            }
            {
                void * t17 = gm_l0;
                void * t18 = GM_LD(void *, (((uint8_t *)t17) + 0));
                void * t19 = (void *)(((uint8_t *)t18) + 8);
                void * t20 = gm_l1;
                gm_t88rcucg1b7uk t21 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t20) + 0));
                uint8_t t22 = gm_f_is_never_in5ftma(t19, GM_CONV(gm_csguankko4lmhd, t21));
                uint8_t t23 = (uint8_t)!t22;
                if (!t23) goto ie16;
                {
                    void * t25 = gm_l0;
                    gm_t6j6smj7qoctu t26;
                    gm_b16_8 t27 = gm_l2;
                    GM_ST(gm_b16_8, (uint8_t *)&t26 + 0, t27);
                    gm_b16_8 t28 = gm_l4;
                    GM_ST(gm_b16_8, (uint8_t *)&t26 + 16, t28);
                    gm_b0_1 t29 = gm_f_linef_uu8i6ue((void *)NULL, cx, NULL, t25, &t26);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                goto ix16;
ie16:;
ix16:;
            }
            {
                gm_b0_1 t30;
                gm_tqgtd5gtgj65s t31;
                memset(&t31, 0, sizeof t31);
                ((uint8_t *)&t31)[0] = 0;
                GM_ST(gm_b0_1, ((uint8_t *)&t31 + 8), t30);
                gm_tqgtd5gtgj65s t32 = t31;
                uint8_t t33 = ((((uint8_t *)&t32))[0] != 0);
                F.ret = t31;
                goto gm_fret;
            }
            goto ix3;
ie3:;
ix3:;
        }
        {
            void * t34 = gm_l1;
            uint8_t t41 = GM_LD(uint8_t, ((((uint8_t *)t34) + 8) + 0));
            switch (t41) { case ((uint8_t)0ULL): goto ma39; case ((uint8_t)1ULL): goto ma39; case ((uint8_t)2ULL): goto ma39; case ((uint8_t)3ULL): goto ma39; case ((uint8_t)4ULL): goto ma39; case ((uint8_t)5ULL): goto ma39; case ((uint8_t)6ULL): goto ma39; case ((uint8_t)7ULL): goto ma39; case ((uint8_t)8ULL): goto ma39; case ((uint8_t)9ULL): goto ma39; case ((uint8_t)10ULL): goto ma39; case ((uint8_t)11ULL): goto ma39; case ((uint8_t)12ULL): goto ma39; case ((uint8_t)13ULL): goto ma39; case ((uint8_t)14ULL): goto ma39; case ((uint8_t)15ULL): goto ma39; case ((uint8_t)16ULL): goto ma39; case ((uint8_t)17ULL): goto ma39; case ((uint8_t)18ULL): goto ma39; case ((uint8_t)19ULL): goto ma39; case ((uint8_t)20ULL): goto ma39; case ((uint8_t)21ULL): goto ma39; case ((uint8_t)22ULL): goto ma39; case ((uint8_t)23ULL): goto ma39; case ((uint8_t)24ULL): goto ma39; case ((uint8_t)25ULL): goto ma39; case ((uint8_t)26ULL): goto ma39; case ((uint8_t)27ULL): goto ma39; case ((uint8_t)28ULL): goto ma39; case ((uint8_t)29ULL): goto ma39; case ((uint8_t)30ULL): goto ma39; case ((uint8_t)31ULL): goto ma39; case ((uint8_t)32ULL): goto ma39; case ((uint8_t)33ULL): goto ma39; case ((uint8_t)34ULL): goto ma36; case ((uint8_t)35ULL): goto ma37; case ((uint8_t)36ULL): goto ma37; case ((uint8_t)37ULL): goto ma39; case ((uint8_t)38ULL): goto ma39; case ((uint8_t)39ULL): goto ma39; case ((uint8_t)40ULL): goto ma39; case ((uint8_t)41ULL): goto ma39; case ((uint8_t)42ULL): goto ma39; case ((uint8_t)43ULL): goto ma39; case ((uint8_t)44ULL): goto ma39; case ((uint8_t)45ULL): goto ma39; case ((uint8_t)46ULL): goto ma38; case ((uint8_t)47ULL): goto ma39; case ((uint8_t)48ULL): goto ma39; case ((uint8_t)49ULL): goto ma39; case ((uint8_t)50ULL): goto ma39; case ((uint8_t)51ULL): goto ma39; case ((uint8_t)52ULL): goto ma39; case ((uint8_t)53ULL): goto ma39; case ((uint8_t)54ULL): goto ma39; case ((uint8_t)55ULL): goto ma39; case ((uint8_t)56ULL): goto ma39; case ((uint8_t)57ULL): goto ma39; case ((uint8_t)58ULL): goto ma39; default: goto ma39; }
ma36:;
            uint8_t t43 = GM_LD(uint8_t, ((((uint8_t *)t34) + 8) + 0));
            if (t43 != ((uint8_t)34ULL)) goto mn42;
            {
                void * t45 = gm_l0;
                gm_ts0eitcfeteg8 t46;
                memset((uint8_t *)&t46 + 36, 0, 4);
                void * t47 = gm_l1;
                GM_ST(void *, (uint8_t *)&t46 + 0, t47);
                gm_b16_8 t48 = gm_l2;
                GM_ST(gm_b16_8, (uint8_t *)&t46 + 8, t48);
                gm_tnrltbc61i6v1 t49 = gm_l3;
                GM_ST(gm_tnrltbc61i6v1, (uint8_t *)&t46 + 24, t49);
                gm_tjefu848ssvm9 t50;
                GM_ST(uint8_t, (uint8_t *)&t50 + 41, 0);
                GM_ST(uint16_t, (uint8_t *)&t50 + 42, 0);
                GM_ST(uint32_t, (uint8_t *)&t50 + 44, 0);
                GM_ST(gm_ts0eitcfeteg8, ((uint8_t *)&t50), t46);
                ((uint8_t *)&t50)[40] = 1;
                GM_ST(gm_tjefu848ssvm9, (((uint8_t *)t45) + 104), t50);
            }
            {
                void * t51 = gm_l0;
                void * t52 = gm_l1;
                gm_f_expr_porbc9t((void *)&F.l5, (void *)NULL, cx, NULL, t51, t52);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t53 = gm_l0;
                gm_tjefu848ssvm9 t54;
                memset(&t54, 0, sizeof t54);
                GM_ST(gm_tjefu848ssvm9, (((uint8_t *)t53) + 104), t54);
            }
            {
                gm_t5o0tpv5jka2l t55 = F.l5;
                gm_t5o0tpv5jka2l t56 = t55;
                if (((((uint8_t *)&t56))[0] != 0)) {
                    gm_t08olq97rscn3 t57 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t56) + 8));
                    gm_tqgtd5gtgj65s t58;
                    GM_ST(uint8_t, (uint8_t *)&t58 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t58 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t58 + 4, 0);
                    ((uint8_t *)&t58)[0] = 1;
                    GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t58 + 8), t57);
                    F.ret = t58;
                    goto gm_fret;
                }
                gm_b16_8 t59 = GM_LD(gm_b16_8, (((uint8_t *)&t56) + 8));
                gm_l6 = t59;
            }
            {
                gm_b16_8 t61 = gm_l6;
                gm_b16_8 t62 = gm_l2;
                gm_b16_8 t63 = t61;
                gm_b16_8 t64 = t62;
                uint8_t t65 = (gm_eq0iqiubfm7e(((uint8_t *)&t63), ((uint8_t *)&t64)));
                uint8_t t66 = (uint8_t)!t65;
                if (!t66) goto ie60;
                {
                    void * t68 = gm_l0;
                    gm_t6j6smj7qoctu t69;
                    gm_b16_8 t70 = gm_l2;
                    GM_ST(gm_b16_8, (uint8_t *)&t69 + 0, t70);
                    gm_b16_8 t71 = gm_l6;
                    GM_ST(gm_b16_8, (uint8_t *)&t69 + 16, t71);
                    gm_b0_1 t72 = gm_f_linef_uu8i6ue((void *)NULL, cx, NULL, t68, &t69);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                goto ix60;
ie60:;
ix60:;
            }
            goto me35;
mn42:;
ma37:;
            uint8_t t76 = GM_LD(uint8_t, ((((uint8_t *)t34) + 8) + 0));
            if (t76 != ((uint8_t)35ULL)) goto pa75;
            gm_b16_8 t77 = GM_LD(gm_b16_8, ((((uint8_t *)t34) + 8) + 8));
            gm_l7 = t77;
            goto po74;
pa75:;
            uint8_t t79 = GM_LD(uint8_t, ((((uint8_t *)t34) + 8) + 0));
            if (t79 != ((uint8_t)36ULL)) goto pa78;
            gm_b16_8 t80 = GM_LD(gm_b16_8, ((((uint8_t *)t34) + 8) + 8));
            gm_l7 = t80;
            goto po74;
pa78:;
            goto mn73;
po74:;
            {
                gm_l9 = ((uint8_t)0);
            }
            {
                {
                    gm_l11 = ((uint64_t)0ULL);
                }
                {
                    uint64_t t83 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                    gm_l10 = t83;
                }
                {
                    for (;;) {
                        uint64_t t85 = gm_l11;
                        uint64_t t86 = gm_l10;
                        uint8_t t87 = (t85 < t86);
                        if (!t87) goto lb84;
                        {
                            uint64_t t90 = gm_l11;
                            uint64_t t91 = t90;
                            uint64_t t94 = GM_LD(uint64_t, ((uint8_t *)&t91));
                            gm_l12 = t94;
                            {
                                void * t97 = gm_l0;
                                uint64_t t98 = gm_l12;
                                uint64_t t99 = (uintptr_t)t98;
                                uint64_t t100 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                                if (t99 >= (uintptr_t)(t100)) gm_trap_index(t99, t100, gm_srcmok80ho8hh01, 844, 39);
                                void * t101 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t99 * 88);
                                gm_tnrltbc61i6v1 t102 = gm_l3;
                                uint8_t t103 = gm_f_nests_in_place_jc41ehn((void *)NULL, cx, NULL, t97, t101, t102);
                                if (gm_unw.kind) {
                                    goto gm_ll88;
                                }
                                if (!t103) goto ie96;
                                {
                                    gm_l9 = ((uint8_t)1);
                                }
                                goto ix96;
ie96:;
ix96:;
                            }
                            goto me92;
mn93:;
                            gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 843, 13);
me92:;
                        }
lc84:;
                        {
                            uint64_t t106 = gm_l11;
                            uint64_t r107; bool o108 = __builtin_add_overflow(t106, ((uint64_t)1ULL), &r107);
                            uint64_t t109 = r107;
                            gm_l11 = t109;
                        }
                    }
gm_ll88:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb84; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc84; }
                    }
                    goto gm_lad;
lb84:;
                }
            }
            {
                uint8_t t111 = gm_l9;
                if (!t111) goto ie110;
                uint8_t t112;
                uint8_t t115 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                if (t115 != ((uint8_t)2ULL)) goto mn114;
                t112 = ((uint8_t)1);
                goto me113;
mn114:;
                t112 = ((uint8_t)0);
                goto me113;
mn116:;
                gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 846, 27);
me113:;
                if (!t112) goto ie110;
                {
                    void * t118 = gm_l0;
                    void * t119 = gm_l1;
                    gm_t88rcucg1b7uk t120 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t119) + 0));
                    gm_b16_8 t121 = gm_f_tmp_uninit_jr39s5u((void *)NULL, cx, NULL, t118, t120);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l13 = t121;
                }
                {
                    void * t122 = gm_l0;
                    void * t123 = gm_l1;
                    gm_b16_8 t124 = gm_l13;
                    gm_tnrltbc61i6v1 t125;
                    GM_ST(uint8_t, (uint8_t *)&t125 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t125 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t125 + 4, 0);
                    GM_ST(uint32_t, (uint8_t *)&t125 + 8, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t125 + 0), ((uint8_t)0ULL));
                    gm_tqgtd5gtgj65s t126;
                    gm_f_expr_into_dn9kavo((void *)&t126, (void *)NULL, cx, NULL, t122, t123, t124, t125);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tqgtd5gtgj65s t127 = t126;
                    if (((((uint8_t *)&t127))[0] != 0)) {
                        gm_t08olq97rscn3 t128 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t127) + 8));
                        gm_tqgtd5gtgj65s t129;
                        GM_ST(uint8_t, (uint8_t *)&t129 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t129 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t129 + 4, 0);
                        ((uint8_t *)&t129)[0] = 1;
                        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t129 + 8), t128);
                        F.ret = t129;
                        goto gm_fret;
                    }
                    gm_b0_1 t130 = GM_LD(gm_b0_1, (((uint8_t *)&t127) + 8));
                }
                {
                    void * t131 = gm_l0;
                    void * t132 = GM_LD(void *, (((uint8_t *)t131) + 0));
                    void * t133 = (void *)(((uint8_t *)t132) + 8);
                    void * t134 = gm_l1;
                    gm_t88rcucg1b7uk t135 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t134) + 0));
                    gm_b16_8 t136 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t133, t135);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l14 = t136;
                }
                {
                    void * t137 = gm_l0;
                    gm_tfeab279c16rr t138;
                    gm_b16_8 t139 = gm_l14;
                    GM_ST(gm_b16_8, (uint8_t *)&t138 + 0, t139);
                    gm_b16_8 t140 = gm_l2;
                    GM_ST(gm_b16_8, (uint8_t *)&t138 + 16, t140);
                    gm_b16_8 t141 = gm_l13;
                    GM_ST(gm_b16_8, (uint8_t *)&t138 + 32, t141);
                    gm_b0_1 t142 = gm_f_linef_21h063v((void *)NULL, cx, NULL, t137, &t138);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    gm_b0_1 t143;
                    gm_tqgtd5gtgj65s t144;
                    memset(&t144, 0, sizeof t144);
                    ((uint8_t *)&t144)[0] = 0;
                    GM_ST(gm_b0_1, ((uint8_t *)&t144 + 8), t143);
                    gm_tqgtd5gtgj65s t145 = t144;
                    uint8_t t146 = ((((uint8_t *)&t145))[0] != 0);
                    F.ret = t144;
                    goto gm_fret;
                }
                goto ix110;
ie110:;
ix110:;
            }
            {
                gm_l15 = ((uint8_t)0);
            }
            {
                uint8_t t152 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                switch (t152) { case ((uint8_t)0ULL): goto ma148; case ((uint8_t)1ULL): goto ma149; case ((uint8_t)2ULL): goto ma150; default: goto ma151; }
ma148:;
                uint8_t t154 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                if (t154 != ((uint8_t)0ULL)) goto mn153;
                {
                    gm_l15 = ((uint8_t)1);
                }
                goto me147;
mn153:;
ma149:;
                uint8_t t157 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                if (t157 != ((uint8_t)1ULL)) goto mn156;
                {
                    void * t159 = gm_l0;
                    void * t160 = gm_l1;
                    uint8_t t161 = gm_f_frame_pure_cbdra2q((void *)NULL, cx, NULL, t159, t160);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l15 = t161;
                }
                goto me147;
mn156:;
ma150:;
                uint8_t t163 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                if (t163 != ((uint8_t)2ULL)) goto mn162;
                {
                    gm_l15 = ((uint8_t)0);
                }
                goto me147;
mn162:;
ma151:;
                gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 855, 13);
me147:;
            }
            {
                gm_f_list_kl0t9m0((void *)&F.l16, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t166 = gm_l1;
                uint8_t t167;
                uint8_t t170 = GM_LD(uint8_t, ((((uint8_t *)t166) + 8) + 0));
                if (t170 != ((uint8_t)35ULL)) goto mn169;
                t167 = ((uint8_t)1);
                goto me168;
mn169:;
                t167 = ((uint8_t)0);
                goto me168;
mn171:;
                gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 861, 16);
me168:;
                if (!t167) goto ie165;
                {
                    void * t173 = (void *)((uint8_t *)&F.l16);
                    void * t174 = gm_l0;
                    void * t175 = GM_LD(void *, (((uint8_t *)t174) + 0));
                    void * t176 = (void *)(((uint8_t *)t175) + 8);
                    void * t177 = gm_l1;
                    gm_t88rcucg1b7uk t178 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t177) + 0));
                    gm_b16_8 t179 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t176, t178);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tftb1odc739t5 t180 = gm_f_extend_ll5819n(t173, t179);
                    gm_tftb1odc739t5 t181 = t180;
                    if (((((uint8_t *)&t181))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t181) + 1, gm_srcmok80ho8hh01, 862, 17);
                    gm_b0_1 t182 = GM_LD(gm_b0_1, (((uint8_t *)&t181) + 1));
                }
                goto ix165;
ie165:;
                {
                    {
                        gm_l18 = ((uint64_t)0ULL);
                    }
                    {
                        uint64_t t185 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                        gm_l17 = t185;
                    }
                    {
                        for (;;) {
                            uint64_t t187 = gm_l18;
                            uint64_t t188 = gm_l17;
                            uint8_t t189 = (t187 < t188);
                            if (!t189) goto lb186;
                            {
                                uint64_t t192 = gm_l18;
                                uint64_t t193 = t192;
                                uint64_t t196 = GM_LD(uint64_t, ((uint8_t *)&t193));
                                gm_l19 = t196;
                                {
                                    void * t198 = (void *)((uint8_t *)&F.l16);
                                    uint64_t t199 = gm_l19;
                                    uint64_t t200 = (uint64_t)t199;
                                    void * t201 = gm_l0;
                                    void * t202 = GM_LD(void *, (((uint8_t *)t201) + 0));
                                    void * t203 = (void *)(((uint8_t *)t202) + 8);
                                    uint64_t t204 = gm_l19;
                                    uint64_t t205 = (uintptr_t)t204;
                                    uint64_t t206 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                                    if (t205 >= (uintptr_t)(t206)) gm_trap_index(t205, t206, gm_srcmok80ho8hh01, 864, 70);
                                    gm_t88rcucg1b7uk t207 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t205 * 88) + 0));
                                    uint64_t t208 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t203, t207);
                                    if (gm_unw.kind) {
                                        goto gm_ll190;
                                    }
                                    uint64_t r209; bool o210 = __builtin_mul_overflow(t200, t208, &r209);
                                    if (o210) gm_trap("integer overflow in `*`", gm_srcmok80ho8hh01, 864, 48);
                                    uint64_t t211 = r209;
                                    gm_tftb1odc739t5 t212 = gm_f_push_7o6oubd(t198, t211);
                                    gm_tftb1odc739t5 t213 = t212;
                                    if (((((uint8_t *)&t213))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t213) + 1, gm_srcmok80ho8hh01, 864, 41);
                                    gm_b0_1 t214 = GM_LD(gm_b0_1, (((uint8_t *)&t213) + 1));
                                }
                                goto me194;
mn195:;
                                gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 864, 17);
me194:;
                            }
lc186:;
                            {
                                uint64_t t216 = gm_l18;
                                uint64_t r217; bool o218 = __builtin_add_overflow(t216, ((uint64_t)1ULL), &r217);
                                uint64_t t219 = r217;
                                gm_l18 = t219;
                            }
                        }
gm_ll190:;
                        if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb186; }
                            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc186; }
                        }
                        goto gm_lad;
lb186:;
                    }
                }
ix165:;
            }
            {
                gm_b16_8 t220 = GM_LD(gm_b16_8, (((uint8_t *)&F.l16) + 0));
                gm_l20 = t220;
            }
            {
                gm_l21 = ((uint64_t)0ULL);
            }
            {
                {
                    gm_l23 = ((uint64_t)0ULL);
                }
                {
                    uint64_t t222 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                    gm_l22 = t222;
                }
                {
                    for (;;) {
                        uint64_t t224 = gm_l23;
                        uint64_t t225 = gm_l22;
                        uint8_t t226 = (t224 < t225);
                        if (!t226) goto lb223;
                        {
                            uint64_t t229 = gm_l23;
                            uint64_t t230 = t229;
                            uint64_t t233 = GM_LD(uint64_t, ((uint8_t *)&t230));
                            gm_l24 = t233;
                            {
                                uint64_t t235 = gm_l24;
                                uint64_t t236 = (uintptr_t)t235;
                                uint64_t t237 = GM_LD(uintptr_t, ((uint8_t *)&gm_l20) + 8);
                                if (t236 >= (uintptr_t)(t237)) gm_trap_index(t236, t237, gm_srcmok80ho8hh01, 870, 24);
                                uint64_t t238 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l20)) + t236 * 8));
                                gm_l25 = t238;
                            }
                            {
                                void * t239 = gm_l0;
                                void * t240 = GM_LD(void *, (((uint8_t *)t239) + 0));
                                void * t241 = (void *)(((uint8_t *)t240) + 8);
                                uint64_t t242 = gm_l24;
                                uint64_t t243 = (uintptr_t)t242;
                                uint64_t t244 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                                if (t243 >= (uintptr_t)(t244)) gm_trap_index(t243, t244, gm_srcmok80ho8hh01, 871, 36);
                                gm_t88rcucg1b7uk t245 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t243 * 88) + 0));
                                uint64_t t246 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t241, t245);
                                if (gm_unw.kind) {
                                    goto gm_ll227;
                                }
                                gm_l26 = t246;
                            }
                            {
                                uint64_t t248 = gm_l26;
                                uint8_t t249 = (t248 > ((uint64_t)0ULL));
                                if (!t249) goto ie247;
                                {
                                    uint64_t t252 = gm_l25;
                                    uint64_t t253 = gm_l21;
                                    uint8_t t254 = (t252 > t253);
                                    if (!t254) goto ie251;
                                    {
                                        void * t256 = gm_l0;
                                        gm_tgbgti82mf879 t257;
                                        gm_b16_8 t258 = gm_l2;
                                        GM_ST(gm_b16_8, (uint8_t *)&t257 + 0, t258);
                                        uint64_t t259 = gm_l21;
                                        GM_ST(uint64_t, (uint8_t *)&t257 + 16, t259);
                                        uint64_t t260 = gm_l25;
                                        uint64_t t261 = gm_l21;
                                        uint64_t r262; bool o263 = __builtin_sub_overflow(t260, t261, &r262);
                                        if (o263) gm_trap("integer overflow in `-`", gm_srcmok80ho8hh01, 873, 101);
                                        uint64_t t264 = r262;
                                        GM_ST(uint64_t, (uint8_t *)&t257 + 24, t264);
                                        gm_b0_1 t265 = gm_f_linef_pduqjh2((void *)NULL, cx, NULL, t256, &t257);
                                        if (gm_unw.kind) {
                                            goto gm_ll227;
                                        }
                                    }
                                    goto ix251;
ie251:;
ix251:;
                                }
                                {
                                    uint64_t t266 = gm_l25;
                                    uint64_t t267 = gm_l26;
                                    uint64_t r268; bool o269 = __builtin_add_overflow(t266, t267, &r268);
                                    if (o269) gm_trap("integer overflow in `+`", gm_srcmok80ho8hh01, 874, 30);
                                    uint64_t t270 = r268;
                                    gm_l21 = t270;
                                }
                                goto ix247;
ie247:;
ix247:;
                            }
                            goto me231;
mn232:;
                            gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 869, 13);
me231:;
                        }
lc223:;
                        {
                            uint64_t t272 = gm_l23;
                            uint64_t r273; bool o274 = __builtin_add_overflow(t272, ((uint64_t)1ULL), &r273);
                            uint64_t t275 = r273;
                            gm_l23 = t275;
                        }
                    }
gm_ll227:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb223; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc223; }
                    }
                    goto gm_lad;
lb223:;
                }
            }
            {
                void * t276 = gm_l0;
                void * t277 = GM_LD(void *, (((uint8_t *)t276) + 0));
                void * t278 = (void *)(((uint8_t *)t277) + 8);
                void * t279 = gm_l1;
                gm_t88rcucg1b7uk t280 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t279) + 0));
                uint64_t t281 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t278, t280);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l27 = t281;
            }
            {
                uint64_t t283 = gm_l27;
                uint64_t t284 = gm_l21;
                uint8_t t285 = (t283 > t284);
                if (!t285) goto ie282;
                {
                    void * t287 = gm_l0;
                    gm_tgbgti82mf879 t288;
                    gm_b16_8 t289 = gm_l2;
                    GM_ST(gm_b16_8, (uint8_t *)&t288 + 0, t289);
                    uint64_t t290 = gm_l21;
                    GM_ST(uint64_t, (uint8_t *)&t288 + 16, t290);
                    uint64_t t291 = gm_l27;
                    uint64_t t292 = gm_l21;
                    uint64_t r293; bool o294 = __builtin_sub_overflow(t291, t292, &r293);
                    if (o294) gm_trap("integer overflow in `-`", gm_srcmok80ho8hh01, 878, 95);
                    uint64_t t295 = r293;
                    GM_ST(uint64_t, (uint8_t *)&t288 + 24, t295);
                    gm_b0_1 t296 = gm_f_linef_pduqjh2((void *)NULL, cx, NULL, t287, &t288);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                goto ix282;
ie282:;
ix282:;
            }
            {
                uint8_t t298 = gm_l15;
                if (!t298) goto ie297;
                {
                    {
                        gm_l29 = ((uint64_t)0ULL);
                    }
                    {
                        uint64_t t301 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                        gm_l28 = t301;
                    }
                    {
                        for (;;) {
                            uint64_t t303 = gm_l29;
                            uint64_t t304 = gm_l28;
                            uint8_t t305 = (t303 < t304);
                            if (!t305) goto lb302;
                            {
                                uint64_t t308 = gm_l29;
                                uint64_t t309 = t308;
                                uint64_t t312 = GM_LD(uint64_t, ((uint8_t *)&t309));
                                gm_l30 = t312;
                                {
                                    uint64_t t314 = gm_l30;
                                    uint64_t t315 = (uintptr_t)t314;
                                    uint64_t t316 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                                    if (t315 >= (uintptr_t)(t316)) gm_trap_index(t315, t316, gm_srcmok80ho8hh01, 881, 27);
                                    void * t317 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t315 * 88);
                                    gm_l31 = t317;
                                }
                                {
                                    uint64_t t318 = gm_l30;
                                    uint64_t t319 = (uintptr_t)t318;
                                    uint64_t t320 = GM_LD(uintptr_t, ((uint8_t *)&gm_l20) + 8);
                                    if (t319 >= (uintptr_t)(t320)) gm_trap_index(t319, t320, gm_srcmok80ho8hh01, 882, 28);
                                    uint64_t t321 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l20)) + t319 * 8));
                                    gm_l32 = t321;
                                }
                                {
                                    void * t322 = gm_l0;
                                    void * t323 = GM_LD(void *, (((uint8_t *)t322) + 0));
                                    void * t324 = (void *)(((uint8_t *)t323) + 8);
                                    void * t325 = gm_l31;
                                    gm_t88rcucg1b7uk t326 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t325) + 0));
                                    gm_b16_8 t327 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t324, t326);
                                    if (gm_unw.kind) {
                                        goto gm_ll306;
                                    }
                                    gm_l33 = t327;
                                }
                                {
                                    void * t329 = gm_l0;
                                    void * t330 = gm_l31;
                                    gm_tnrltbc61i6v1 t331 = gm_l3;
                                    uint8_t t332 = gm_f_nests_in_place_jc41ehn((void *)NULL, cx, NULL, t329, t330, t331);
                                    if (gm_unw.kind) {
                                        goto gm_ll306;
                                    }
                                    if (!t332) goto ie328;
                                    {
                                        gm_tftksr4op114e t334;
                                        gm_b16_8 t335 = gm_l33;
                                        GM_ST(gm_b16_8, (uint8_t *)&t334 + 0, t335);
                                        gm_b16_8 t336 = gm_l2;
                                        GM_ST(gm_b16_8, (uint8_t *)&t334 + 16, t336);
                                        uint64_t t337 = gm_l32;
                                        GM_ST(uint64_t, (uint8_t *)&t334 + 32, t337);
                                        gm_tabb710d9mq2e t338;
                                        gm_f_format_e1d93qv((void *)&t338, (void *)NULL, cx, NULL, &t334);
                                        if (gm_unw.kind) {
                                            goto gm_ll306;
                                        }
                                        gm_tabb710d9mq2e t339 = t338;
                                        if (((((uint8_t *)&t339))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t339) + 8, gm_srcmok80ho8hh01, 885, 32);
                                        gm_b16_8 t340 = GM_LD(gm_b16_8, (((uint8_t *)&t339) + 8));
                                        gm_l34 = t340;
                                    }
                                    {
                                        void * t341 = gm_l0;
                                        void * t342 = gm_l31;
                                        gm_b16_8 t343 = gm_l34;
                                        gm_b16_8 t344 = t343;
                                        gm_b16_8 t345 = GM_LD(gm_b16_8, ((uint8_t *)&t344));
                                        gm_tnrltbc61i6v1 t346 = gm_l3;
                                        gm_tqgtd5gtgj65s t347;
                                        gm_f_expr_into_dn9kavo((void *)&t347, (void *)NULL, cx, NULL, t341, t342, t345, t346);
                                        if (gm_unw.kind) {
                                            goto gm_ll306;
                                        }
                                        gm_tqgtd5gtgj65s t348 = t347;
                                        if (((((uint8_t *)&t348))[0] != 0)) {
                                            gm_t08olq97rscn3 t349 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t348) + 8));
                                            gm_tqgtd5gtgj65s t350;
                                            GM_ST(uint8_t, (uint8_t *)&t350 + 1, 0);
                                            GM_ST(uint16_t, (uint8_t *)&t350 + 2, 0);
                                            GM_ST(uint32_t, (uint8_t *)&t350 + 4, 0);
                                            ((uint8_t *)&t350)[0] = 1;
                                            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t350 + 8), t349);
                                            F.ret = t350;
                                            goto gm_fret;
                                        }
                                        gm_b0_1 t351 = GM_LD(gm_b0_1, (((uint8_t *)&t348) + 8));
                                    }
                                    goto ix328;
ie328:;
                                    void * t353 = gm_l0;
                                    void * t354 = GM_LD(void *, (((uint8_t *)t353) + 0));
                                    void * t355 = (void *)(((uint8_t *)t354) + 8);
                                    void * t356 = gm_l31;
                                    gm_t88rcucg1b7uk t357 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t356) + 0));
                                    uint8_t t358 = gm_f_by_ref_fi0o860((void *)NULL, cx, NULL, t355, t357);
                                    if (gm_unw.kind) {
                                        goto gm_ll306;
                                    }
                                    if (!t358) goto ie352;
                                    void * t359 = gm_l0;
                                    void * t360 = gm_l31;
                                    uint8_t t361 = gm_f_reads_place_nji55rm((void *)NULL, cx, NULL, t359, t360);
                                    if (gm_unw.kind) {
                                        goto gm_ll306;
                                    }
                                    if (!t361) goto ie352;
                                    {
                                        void * t363 = gm_l0;
                                        void * t364 = gm_l31;
                                        gm_t5o0tpv5jka2l t365;
                                        gm_f_place_addr_7ji15ot((void *)&t365, (void *)NULL, cx, NULL, t363, t364);
                                        if (gm_unw.kind) {
                                            goto gm_ll306;
                                        }
                                        gm_t5o0tpv5jka2l t366 = t365;
                                        if (((((uint8_t *)&t366))[0] != 0)) {
                                            gm_t08olq97rscn3 t367 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t366) + 8));
                                            gm_tqgtd5gtgj65s t368;
                                            GM_ST(uint8_t, (uint8_t *)&t368 + 1, 0);
                                            GM_ST(uint16_t, (uint8_t *)&t368 + 2, 0);
                                            GM_ST(uint32_t, (uint8_t *)&t368 + 4, 0);
                                            ((uint8_t *)&t368)[0] = 1;
                                            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t368 + 8), t367);
                                            F.ret = t368;
                                            goto gm_fret;
                                        }
                                        gm_b16_8 t369 = GM_LD(gm_b16_8, (((uint8_t *)&t366) + 8));
                                        gm_l35 = t369;
                                    }
                                    {
                                        void * t370 = gm_l0;
                                        void * t371 = GM_LD(void *, (((uint8_t *)t370) + 0));
                                        void * t372 = (void *)(((uint8_t *)t371) + 8);
                                        void * t373 = gm_l31;
                                        gm_t88rcucg1b7uk t374 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t373) + 0));
                                        uint64_t t375 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t372, t374);
                                        if (gm_unw.kind) {
                                            goto gm_ll306;
                                        }
                                        gm_l36 = t375;
                                    }
                                    {
                                        void * t376 = gm_l0;
                                        gm_thddbqb6snabm t377;
                                        gm_b16_8 t378 = gm_l2;
                                        GM_ST(gm_b16_8, (uint8_t *)&t377 + 0, t378);
                                        uint64_t t379 = gm_l32;
                                        GM_ST(uint64_t, (uint8_t *)&t377 + 16, t379);
                                        gm_b16_8 t380 = gm_l35;
                                        GM_ST(gm_b16_8, (uint8_t *)&t377 + 24, t380);
                                        uint64_t t381 = gm_l36;
                                        GM_ST(uint64_t, (uint8_t *)&t377 + 40, t381);
                                        gm_b0_1 t382 = gm_f_linef_thnd8mb((void *)NULL, cx, NULL, t376, &t377);
                                        if (gm_unw.kind) {
                                            goto gm_ll306;
                                        }
                                    }
                                    goto ix352;
ie352:;
                                    {
                                        void * t384 = gm_l0;
                                        void * t385 = gm_l31;
                                        gm_t5o0tpv5jka2l t386;
                                        gm_f_expr_porbc9t((void *)&t386, (void *)NULL, cx, NULL, t384, t385);
                                        if (gm_unw.kind) {
                                            goto gm_ll306;
                                        }
                                        gm_t5o0tpv5jka2l t387 = t386;
                                        if (((((uint8_t *)&t387))[0] != 0)) {
                                            gm_t08olq97rscn3 t388 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t387) + 8));
                                            gm_tqgtd5gtgj65s t389;
                                            GM_ST(uint8_t, (uint8_t *)&t389 + 1, 0);
                                            GM_ST(uint16_t, (uint8_t *)&t389 + 2, 0);
                                            GM_ST(uint32_t, (uint8_t *)&t389 + 4, 0);
                                            ((uint8_t *)&t389)[0] = 1;
                                            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t389 + 8), t388);
                                            F.ret = t389;
                                            goto gm_fret;
                                        }
                                        gm_b16_8 t390 = GM_LD(gm_b16_8, (((uint8_t *)&t387) + 8));
                                        gm_l37 = t390;
                                    }
                                    {
                                        void * t391 = gm_l0;
                                        gm_tmr5oep1578tb t392;
                                        gm_b16_8 t393 = gm_l33;
                                        GM_ST(gm_b16_8, (uint8_t *)&t392 + 0, t393);
                                        gm_b16_8 t394 = gm_l2;
                                        GM_ST(gm_b16_8, (uint8_t *)&t392 + 16, t394);
                                        uint64_t t395 = gm_l32;
                                        GM_ST(uint64_t, (uint8_t *)&t392 + 32, t395);
                                        gm_b16_8 t396 = gm_l37;
                                        GM_ST(gm_b16_8, (uint8_t *)&t392 + 40, t396);
                                        gm_b0_1 t397 = gm_f_linef_8i4929c((void *)NULL, cx, NULL, t391, &t392);
                                        if (gm_unw.kind) {
                                            goto gm_ll306;
                                        }
                                    }
ix352:;
ix328:;
                                }
                                goto me310;
mn311:;
                                gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 880, 17);
me310:;
                            }
lc302:;
                            {
                                uint64_t t399 = gm_l29;
                                uint64_t r400; bool o401 = __builtin_add_overflow(t399, ((uint64_t)1ULL), &r400);
                                uint64_t t402 = r400;
                                gm_l29 = t402;
                            }
                        }
gm_ll306:;
                        if (gm_unw.frame == &F.h && gm_unw.label == 3) {
                            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb302; }
                            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc302; }
                        }
                        goto gm_lad;
lb302:;
                    }
                }
                goto ix297;
ie297:;
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
                        uint64_t t405 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                        gm_l39 = t405;
                    }
                    {
                        for (;;) {
                            uint64_t t407 = gm_l40;
                            uint64_t t408 = gm_l39;
                            uint8_t t409 = (t407 < t408);
                            if (!t409) goto lb406;
                            {
                                uint64_t t412 = gm_l40;
                                uint64_t t413 = t412;
                                uint64_t t416 = GM_LD(uint64_t, ((uint8_t *)&t413));
                                gm_l41 = t416;
                                {
                                    void * t418 = gm_l0;
                                    uint64_t t419 = gm_l41;
                                    uint64_t t420 = (uintptr_t)t419;
                                    uint64_t t421 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                                    if (t420 >= (uintptr_t)(t421)) gm_trap_index(t420, t421, gm_srcmok80ho8hh01, 900, 39);
                                    void * t422 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t420 * 88);
                                    gm_t5o0tpv5jka2l t423;
                                    gm_f_expr_porbc9t((void *)&t423, (void *)NULL, cx, NULL, t418, t422);
                                    if (gm_unw.kind) {
                                        goto gm_ll410;
                                    }
                                    gm_t5o0tpv5jka2l t424 = t423;
                                    if (((((uint8_t *)&t424))[0] != 0)) {
                                        gm_t08olq97rscn3 t425 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t424) + 8));
                                        gm_tqgtd5gtgj65s t426;
                                        GM_ST(uint8_t, (uint8_t *)&t426 + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t426 + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t426 + 4, 0);
                                        ((uint8_t *)&t426)[0] = 1;
                                        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t426 + 8), t425);
                                        F.ret = t426;
                                        goto gm_fret;
                                    }
                                    gm_b16_8 t427 = GM_LD(gm_b16_8, (((uint8_t *)&t424) + 8));
                                    gm_l42 = t427;
                                }
                                {
                                    void * t428 = (void *)((uint8_t *)&F.l38);
                                    gm_b16_8 t429 = gm_l42;
                                    gm_tftb1odc739t5 t430 = gm_f_push_3ef746b(t428, t429);
                                    gm_tftb1odc739t5 t431 = t430;
                                    if (((((uint8_t *)&t431))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t431) + 1, gm_srcmok80ho8hh01, 901, 21);
                                    gm_b0_1 t432 = GM_LD(gm_b0_1, (((uint8_t *)&t431) + 1));
                                }
                                goto me414;
mn415:;
                                gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 899, 17);
me414:;
                            }
lc406:;
                            {
                                uint64_t t434 = gm_l40;
                                uint64_t r435; bool o436 = __builtin_add_overflow(t434, ((uint64_t)1ULL), &r435);
                                uint64_t t437 = r435;
                                gm_l40 = t437;
                            }
                        }
gm_ll410:;
                        if (gm_unw.frame == &F.h && gm_unw.label == 4) {
                            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb406; }
                            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc406; }
                        }
                        goto gm_lad;
lb406:;
                    }
                }
                {
                    {
                        gm_l44 = ((uint64_t)0ULL);
                    }
                    {
                        uint64_t t439 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                        gm_l43 = t439;
                    }
                    {
                        for (;;) {
                            uint64_t t441 = gm_l44;
                            uint64_t t442 = gm_l43;
                            uint8_t t443 = (t441 < t442);
                            if (!t443) goto lb440;
                            {
                                uint64_t t446 = gm_l44;
                                uint64_t t447 = t446;
                                uint64_t t450 = GM_LD(uint64_t, ((uint8_t *)&t447));
                                gm_l45 = t450;
                                {
                                    void * t452 = gm_l0;
                                    void * t453 = GM_LD(void *, (((uint8_t *)t452) + 0));
                                    void * t454 = (void *)(((uint8_t *)t453) + 8);
                                    uint64_t t455 = gm_l45;
                                    uint64_t t456 = (uintptr_t)t455;
                                    uint64_t t457 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                                    if (t456 >= (uintptr_t)(t457)) gm_trap_index(t456, t457, gm_srcmok80ho8hh01, 904, 38);
                                    gm_t88rcucg1b7uk t458 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t456 * 88) + 0));
                                    gm_b16_8 t459 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t454, t458);
                                    if (gm_unw.kind) {
                                        goto gm_ll444;
                                    }
                                    gm_l46 = t459;
                                }
                                {
                                    void * t460 = gm_l0;
                                    gm_tmr5oep1578tb t461;
                                    gm_b16_8 t462 = gm_l46;
                                    GM_ST(gm_b16_8, (uint8_t *)&t461 + 0, t462);
                                    gm_b16_8 t463 = gm_l2;
                                    GM_ST(gm_b16_8, (uint8_t *)&t461 + 16, t463);
                                    uint64_t t464 = gm_l45;
                                    uint64_t t465 = (uintptr_t)t464;
                                    uint64_t t466 = GM_LD(uintptr_t, ((uint8_t *)&gm_l20) + 8);
                                    if (t465 >= (uintptr_t)(t466)) gm_trap_index(t465, t466, gm_srcmok80ho8hh01, 905, 78);
                                    uint64_t t467 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l20)) + t465 * 8));
                                    GM_ST(uint64_t, (uint8_t *)&t461 + 32, t467);
                                    uint64_t t468 = gm_l45;
                                    uint64_t t469 = (uintptr_t)t468;
                                    uint64_t t470 = GM_LD(uintptr_t, (((uint8_t *)&F.l38) + 0) + 8);
                                    if (t469 >= (uintptr_t)(t470)) gm_trap_index(t469, t470, gm_srcmok80ho8hh01, 905, 87);
                                    gm_b16_8 t471 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l38) + 0)) + t469 * 16));
                                    GM_ST(gm_b16_8, (uint8_t *)&t461 + 40, t471);
                                    gm_b0_1 t472 = gm_f_linef_8i4929c((void *)NULL, cx, NULL, t460, &t461);
                                    if (gm_unw.kind) {
                                        goto gm_ll444;
                                    }
                                }
                                goto me448;
mn449:;
                                gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 903, 17);
me448:;
                            }
lc440:;
                            {
                                uint64_t t474 = gm_l44;
                                uint64_t r475; bool o476 = __builtin_add_overflow(t474, ((uint64_t)1ULL), &r475);
                                uint64_t t477 = r475;
                                gm_l44 = t477;
                            }
                        }
gm_ll444:;
                        if (gm_unw.frame == &F.h && gm_unw.label == 5) {
                            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb440; }
                            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc440; }
                        }
                        goto gm_lad;
lb440:;
                    }
                }
ix297:;
            }
            goto me35;
mn73:;
ma38:;
            uint8_t t479 = GM_LD(uint8_t, ((((uint8_t *)t34) + 8) + 0));
            if (t479 != ((uint8_t)46ULL)) goto mn478;
            void * t480 = GM_LD(void *, ((((uint8_t *)t34) + 8) + 8));
            gm_l47 = t480;
            {
                void * t482 = gm_l0;
                gm_f_block_scope_e3cgusu((void *)&F.l48, (void *)NULL, cx, NULL, t482);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t483 = gm_l0;
                void * t484 = (void *)(((uint8_t *)t483) + 240);
                gm_tftb1odc739t5 t485 = gm_f_push_pcqb56b(t484, &(*(gm_tehgl6lkqgvt0 *)((uint8_t *)&F.l48)));
                gm_tftb1odc739t5 t486 = t485;
                if (((((uint8_t *)&t486))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t486) + 1, gm_srcmok80ho8hh01, 911, 13);
                gm_b0_1 t487 = GM_LD(gm_b0_1, (((uint8_t *)&t486) + 1));
            }
            {
                void * t488 = gm_l0;
                void * t489 = gm_l47;
                gm_tqgtd5gtgj65s t490;
                gm_f_stmts_1199hll((void *)&t490, (void *)NULL, cx, NULL, t488, t489);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tqgtd5gtgj65s t491 = t490;
                if (((((uint8_t *)&t491))[0] != 0)) {
                    gm_t08olq97rscn3 t492 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t491) + 8));
                    gm_tqgtd5gtgj65s t493;
                    GM_ST(uint8_t, (uint8_t *)&t493 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t493 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t493 + 4, 0);
                    ((uint8_t *)&t493)[0] = 1;
                    GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t493 + 8), t492);
                    F.ret = t493;
                    goto gm_fret;
                }
                gm_b0_1 t494 = GM_LD(gm_b0_1, (((uint8_t *)&t491) + 8));
            }
            {
                void * t495 = gm_l47;
                void * t496 = GM_LD(void *, (((uint8_t *)t495) + 16));
                void * t497 = t496;
                if (!(GM_LD(void *, ((uint8_t *)&t497)) != 0)) gm_trap("unwrap of null", gm_srcmok80ho8hh01, 913, 18);
                void * t498 = GM_LD(void *, ((uint8_t *)&t497));
                gm_l49 = t498;
            }
            {
                void * t499 = gm_l0;
                void * t500 = gm_l49;
                gm_tl874t2atnlh3 t501 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t500) + 72));
                gm_b0_1 t502 = gm_f_src_line_d24sajl((void *)NULL, cx, NULL, t499, t501);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t503 = gm_l0;
                void * t504 = gm_l49;
                gm_b16_8 t505 = gm_l2;
                gm_tnrltbc61i6v1 t506 = gm_l3;
                gm_tqgtd5gtgj65s t507;
                gm_f_expr_into_dn9kavo((void *)&t507, (void *)NULL, cx, NULL, t503, t504, t505, t506);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tqgtd5gtgj65s t508 = t507;
                if (((((uint8_t *)&t508))[0] != 0)) {
                    gm_t08olq97rscn3 t509 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t508) + 8));
                    gm_tqgtd5gtgj65s t510;
                    GM_ST(uint8_t, (uint8_t *)&t510 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t510 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t510 + 4, 0);
                    ((uint8_t *)&t510)[0] = 1;
                    GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t510 + 8), t509);
                    F.ret = t510;
                    goto gm_fret;
                }
                gm_b0_1 t511 = GM_LD(gm_b0_1, (((uint8_t *)&t508) + 8));
            }
            {
                void * t512 = gm_l0;
                void * t513 = (void *)(((uint8_t *)t512) + 240);
                gm_t1g28mkc78m0c t514;
                gm_f_pop_g2usrvd((void *)&t514, t513);
                gm_t1g28mkc78m0c t515 = t514;
                if (!((((uint8_t *)&t515))[104] != 0)) gm_trap("unwrap of null", gm_srcmok80ho8hh01, 916, 23);
                gm_tehgl6lkqgvt0 t516 = GM_LD(gm_tehgl6lkqgvt0, ((uint8_t *)&t515));
                F.l50 = t516;
            }
            {
                void * t517 = gm_l0;
                gm_b0_1 t518 = gm_f_end_scope_45ar5qv((void *)NULL, cx, NULL, t517, &(*(gm_tehgl6lkqgvt0 *)((uint8_t *)&F.l50)), ((uint8_t)1));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto me35;
mn478:;
ma39:;
            gm_trap("reached `@unreachable()`", gm_srcmok80ho8hh01, 919, 14);
            goto me35;
mn519:;
ma40:;
            gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 833, 5);
me35:;
        }
    }
    gm_b0_1 t520;
    gm_tqgtd5gtgj65s t521;
    memset(&t521, 0, sizeof t521);
    ((uint8_t *)&t521)[0] = 0;
    GM_ST(gm_b0_1, ((uint8_t *)&t521 + 8), t520);
    gm_tqgtd5gtgj65s t522 = t521;
    F.ret = t522;
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
        gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 1216, 5);
me3:;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) uint8_t gm_f_fn_2tr382j(void * a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_5evrfa7(void *env, gm_node *cx, gm_node *inst, void * a0);
extern __attribute__((aligned(4))) uint8_t gm_f_fn_aa55llv(void * a0);
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
    GM_STACK_CHECK(&F, 487u);
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
            gm_tabb710d9mq2e t9 = t8;
            if (((((uint8_t *)&t9))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t9) + 8, gm_srcmok80ho8hh01, 560, 29);
            gm_b16_8 t10 = GM_LD(gm_b16_8, (((uint8_t *)&t9) + 8));
            gm_b16_8 t11 = t10;
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
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
        uint32_t t15 = gm_l1;
        gm_tommf4j9upomc t16;
        gm_f_frame_up_g8486eu((void *)&t16, (void *)NULL, cx, NULL, t14, t15);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tommf4j9upomc t17 = t16;
        gm_l3 = GM_LD(gm_b16_8, (uint8_t *)&t17 + 0);
        gm_l4 = GM_LD(uint32_t, (uint8_t *)&t17 + 16);
    }
    {
        gm_tfeab279c16rr t18;
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        uint32_t t21 = gm_l4;
        gm_b16_8 t22 = gm_f_frame_name_stddmgg((void *)NULL, cx, NULL, t20, t21);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t18 + 0, t22);
        gm_b16_8 t23 = gm_l3;
        GM_ST(gm_b16_8, (uint8_t *)&t18 + 16, t23);
        gm_b16_8 t24 = gm_l2;
        GM_ST(gm_b16_8, (uint8_t *)&t18 + 32, t24);
        gm_tabb710d9mq2e t25;
        gm_f_format_lufljqc((void *)&t25, (void *)NULL, cx, NULL, &t18);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t26 = t25;
        if (((((uint8_t *)&t26))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t26) + 8, gm_srcmok80ho8hh01, 562, 12);
        gm_b16_8 t27 = GM_LD(gm_b16_8, (((uint8_t *)&t26) + 8));
        gm_b16_8 t28 = t27;
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
    GM_STACK_CHECK(&F, 915u);
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
                gm_tftb1odc739t5 t15 = t14;
                if (((((uint8_t *)&t15))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t15) + 1, gm_srcmok80ho8hh01, 1161, 42);
                gm_b0_1 t16 = GM_LD(gm_b0_1, (((uint8_t *)&t15) + 1));
            }
lc4:;
        }
lb4:;
    }
    {
        uint64_t t18 = gm_l2;
        uint64_t t19 = (uintptr_t)t18;
        void * t20 = gm_l0;
        uint64_t t21 = GM_LD(uintptr_t, ((((uint8_t *)t20) + 2960) + 0) + 8);
        if (t19 >= (uintptr_t)(t21)) gm_trap_index(t19, t21, gm_srcmok80ho8hh01, 1162, 17);
        if (!((((uint8_t *)GM_LD(void *, ((((uint8_t *)t20) + 2960) + 0)) + t19 * 24))[16] != 0)) goto ie17;
        gm_b16_8 t22 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t20) + 2960) + 0)) + t19 * 24));
        gm_l3 = t22;
        {
            gm_b16_8 t24 = gm_l3;
            uint8_t t25 = 0;
            F.ret = t24;
            goto gm_fret;
        }
        goto ix17;
ie17:;
ix17:;
    }
    {
        void * t26 = gm_l0;
        void * t27 = GM_LD(void *, (((uint8_t *)t26) + 0));
        uint32_t t28 = gm_l1;
        void * t29 = gm_f_func_in17r5n((void *)NULL, cx, NULL, t27, t28);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l4 = t29;
    }
    {
        gm_f_list_s83jn2q((void *)&F.l5, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint8_t t30 = 0;
        uint8_t t31 = 0;
        void * t32 = gm_l0;
        uint8_t t33 = GM_LD(uint8_t, ((((uint8_t *)t32) + 1136) + 1));
        if (!t33) {
            void * t34 = gm_l0;
            uint8_t t35 = GM_LD(uint8_t, ((((uint8_t *)t34) + 1136) + 2));
            t31 = t35;
        } else t31 = 1;
        if (!t31) {
            void * t36 = gm_l0;
            uint32_t t37 = gm_l1;
            uint8_t t38 = gm_f_outlines_2kvbvit((void *)NULL, cx, NULL, t36, t37);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            t30 = t38;
        } else t30 = 1;
        gm_l6 = t30;
    }
    {
        void * t40 = gm_l4;
        void * t41 = GM_LD(void *, (((uint8_t *)t40) + 152));
        void * t42 = t41;
        if (!(GM_LD(void *, ((uint8_t *)&t42)) != 0)) goto ie39;
        void * t43 = GM_LD(void *, ((uint8_t *)&t42));
        gm_l7 = t43;
        {
            uint8_t t45 = 0;
            uint8_t t46 = gm_l6;
            if (!t46) {
                void * t47 = gm_l7;
                uint8_t t48 = gm_f_has_fiber_with_53c3jik(t47);
                t45 = t48;
            } else t45 = 1;
            gm_l6 = t45;
        }
        goto ix39;
ie39:;
ix39:;
    }
    {
        uint8_t t50 = gm_l6;
        if (!t50) goto ie49;
        {
            {
                void * t53 = gm_l4;
                gm_b16_8 t54 = GM_LD(gm_b16_8, (((uint8_t *)t53) + 48));
                gm_l8 = t54;
            }
            {
                gm_l9 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t56 = gm_l9;
                    uint64_t t57 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                    uint8_t t58 = (t56 < t57);
                    if (!t58) goto lb55;
                    {
                        uint64_t t61 = gm_l9;
                        uint64_t t62 = (uintptr_t)t61;
                        uint64_t t63 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                        {
                            void * t67 = (void *)((uint8_t *)&F.l5);
                            gm_tftb1odc739t5 t68 = gm_f_push_vd1290p(t67, ((uint8_t)1));
                            gm_tftb1odc739t5 t69 = t68;
                            if (((((uint8_t *)&t69))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t69) + 1, gm_srcmok80ho8hh01, 1168, 31);
                            gm_b0_1 t70 = GM_LD(gm_b0_1, (((uint8_t *)&t69) + 1));
                        }
                        goto me64;
mn65:;
                        gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 1168, 9);
me64:;
                    }
lc55:;
                    {
                        uint64_t t72 = gm_l9;
                        uint64_t r73; bool o74 = __builtin_add_overflow(t72, ((uint64_t)1ULL), &r73);
                        uint64_t t75 = r73;
                        gm_l9 = t75;
                    }
                }
lb55:;
            }
        }
        goto ix49;
ie49:;
        {
            void * t77 = gm_l0;
            gm_f_captured_locals_glsvloj((void *)&F.l10, (void *)NULL, cx, NULL, t77);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t78 = gm_l0;
            uint8_t t79 = GM_LD(uint8_t, ((((((uint8_t *)t78) + 8) + 8) + 0) + 56));
            gm_l11 = t79;
        }
        {
            {
                void * t81 = gm_l4;
                gm_b16_8 t82 = GM_LD(gm_b16_8, (((uint8_t *)t81) + 48));
                gm_l12 = t82;
            }
            {
                gm_l13 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t84 = gm_l13;
                    uint64_t t85 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                    uint8_t t86 = (t84 < t85);
                    if (!t86) goto lb83;
                    {
                        gm_t5im2v50m3hk6 t89;
                        uint64_t t90 = gm_l13;
                        GM_ST(uint64_t, (uint8_t *)&t89 + 0, t90);
                        uint64_t t91 = gm_l13;
                        uint64_t t92 = (uintptr_t)t91;
                        uint64_t t93 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                        memcpy((uint8_t *)&t89 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l12)) + t92 * 40), 40);
                        gm_t5im2v50m3hk6 t94 = t89;
                        uint64_t t97 = GM_LD(uint64_t, (((uint8_t *)&t94) + 0));
                        gm_l14 = t97;
                        gm_tsoie1ppgdvcg t98 = GM_LD(gm_tsoie1ppgdvcg, (((uint8_t *)&t94) + 8));
                        F.l15 = t98;
                        {
                            void * t100 = (void *)((uint8_t *)&F.l5);
                            uint8_t t101 = 0;
                            uint8_t t102 = 0;
                            uint8_t t103 = gm_l11;
                            uint8_t t104 = (uint8_t)!t103;
                            if (t104) {
                                void * t105 = gm_l0;
                                void * t106 = (void *)(((uint8_t *)t105) + 8);
                                gm_t88rcucg1b7uk t107 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l15) + 16));
                                uint64_t t108 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t106, t107);
                                if (gm_unw.kind) {
                                    goto gm_ll87;
                                }
                                uint64_t t109 = GM_LD(uint64_t, gm_cbemal9c6bf);
                                uint8_t t110 = (t108 > t109);
                                t102 = t110;
                            } else t102 = 0;
                            if (!t102) {
                                void * t111 = (void *)((uint8_t *)&F.l10);
                                uint32_t t112 = gm_l1;
                                uint64_t t113 = gm_l14;
                                uint32_t t114 = (uint32_t)t113;
                                gm_tcc1rq4n1avb2 t115;
                                GM_ST(uint32_t, ((uint8_t *)&t115 + 0), t112);
                                GM_ST(uint32_t, ((uint8_t *)&t115 + 4), t114);
                                gm_tqs5p4n2tq19v t116 = gm_f_get_ag7vhtq(t111, t115);
                                gm_tqs5p4n2tq19v t117 = t116;
                                uint8_t t118;
                                if (((((uint8_t *)&t117))[1] != 0)) goto mn120;
                                t118 = ((uint8_t)0);
                                goto me119;
mn120:;
                                t118 = ((uint8_t)1);
                                goto me119;
mn121:;
                                gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 1173, 70);
me119:;
                                t101 = t118;
                            } else t101 = 1;
                            gm_tftb1odc739t5 t122 = gm_f_push_vd1290p(t100, t101);
                            gm_tftb1odc739t5 t123 = t122;
                            if (((((uint8_t *)&t123))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t123) + 1, gm_srcmok80ho8hh01, 1173, 13);
                            gm_b0_1 t124 = GM_LD(gm_b0_1, (((uint8_t *)&t123) + 1));
                        }
                        goto me95;
mn96:;
                        gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 1172, 9);
me95:;
                    }
lc83:;
                    {
                        uint64_t t126 = gm_l13;
                        uint64_t r127; bool o128 = __builtin_add_overflow(t126, ((uint64_t)1ULL), &r127);
                        uint64_t t129 = r127;
                        gm_l13 = t129;
                    }
                }
gm_ll87:;
                if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb83; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc83; }
                }
                goto gm_lad;
lb83:;
            }
        }
ix49:;
    }
    {
        uint64_t t130 = gm_l2;
        uint64_t t131 = (uintptr_t)t130;
        void * t132 = gm_l0;
        uint64_t t133 = GM_LD(uintptr_t, ((((uint8_t *)t132) + 2960) + 0) + 8);
        if (t131 >= (uintptr_t)(t133)) gm_trap_index(t131, t133, gm_srcmok80ho8hh01, 1176, 5);
        gm_b16_8 t134 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 0));
        gm_th2lqbo77pvsc t135;
        GM_ST(uint8_t, (uint8_t *)&t135 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t135 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t135 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t135), t134);
        ((uint8_t *)&t135)[16] = 1;
        GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t132) + 2960) + 0)) + t131 * 24), t135);
    }
    {
        gm_b16_8 t136 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 0));
        uint8_t t137 = 0;
        F.ret = t136;
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
    GM_STACK_CHECK(&F, 858u);
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
        gm_t53at80pfdus3 t55 = t54;
        uint8_t t56;
        uint8_t t59 = GM_LD(uint8_t, (((uint8_t *)&t55) + 0));
        if (t59 != ((uint8_t)11ULL)) goto mn58;
        t56 = ((uint8_t)1);
        goto me57;
mn58:;
        t56 = ((uint8_t)0);
        goto me57;
mn60:;
        gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 953, 42);
me57:;
        if (t56) {
            void * t61 = gm_l0;
            void * t62 = gm_l3;
            uint8_t t63 = gm_f_frame_pure_cbdra2q((void *)NULL, cx, NULL, t61, t62);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            t48 = t63;
        } else t48 = 0;
        if (t48) {
            void * t64 = gm_l0;
            void * t65 = gm_l4;
            uint8_t t66 = gm_f_frame_pure_cbdra2q((void *)NULL, cx, NULL, t64, t65);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            t47 = t66;
        } else t47 = 0;
        uint8_t t67 = 0;
        F.ret = t47;
        goto gm_fret;
        goto me3;
mn43:;
ma8:;
        uint8_t t69 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t69 != ((uint8_t)29ULL)) goto mn68;
        void * t70 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l5 = t70;
        uint8_t t71 = 0;
        void * t72 = gm_l0;
        void * t73 = GM_LD(void *, (((uint8_t *)t72) + 0));
        void * t74 = (void *)(((uint8_t *)t73) + 8);
        void * t75 = gm_l1;
        gm_t88rcucg1b7uk t76 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t75) + 0));
        gm_t5v8n6qrn4rh4 t77;
        gm_f_repr_53quqki((void *)&t77, (void *)NULL, cx, NULL, t74, t76);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5v8n6qrn4rh4 t78 = t77;
        uint8_t t79 = ((GM_LD(uint8_t, ((uint8_t *)&t78) + 0) == ((uint8_t)6ULL)));
        uint8_t t80 = (uint8_t)!t79;
        if (t80) {
            void * t81 = gm_l0;
            void * t82 = gm_l5;
            uint8_t t83 = gm_f_frame_pure_cbdra2q((void *)NULL, cx, NULL, t81, t82);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            t71 = t83;
        } else t71 = 0;
        uint8_t t84 = 0;
        F.ret = t71;
        goto gm_fret;
        goto me3;
mn68:;
ma9:;
        uint8_t t86 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t86 != ((uint8_t)30ULL)) goto mn85;
        void * t87 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l6 = t87;
        void * t88 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 16));
        gm_l7 = t88;
        {
            uint8_t t90 = 0;
            uint8_t t91 = 0;
            uint8_t t92 = 0;
            void * t93 = gm_l0;
            void * t94 = GM_LD(void *, (((uint8_t *)t93) + 0));
            void * t95 = (void *)(((uint8_t *)t94) + 8);
            void * t96 = gm_l1;
            gm_t88rcucg1b7uk t97 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t96) + 0));
            gm_t5v8n6qrn4rh4 t98;
            gm_f_repr_53quqki((void *)&t98, (void *)NULL, cx, NULL, t95, t97);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t5v8n6qrn4rh4 t99 = t98;
            uint8_t t100 = ((GM_LD(uint8_t, ((uint8_t *)&t99) + 0) == ((uint8_t)6ULL)));
            uint8_t t101 = (uint8_t)!t100;
            if (t101) {
                void * t102 = gm_l0;
                void * t103 = GM_LD(void *, (((uint8_t *)t102) + 0));
                void * t104 = (void *)(((uint8_t *)t103) + 8);
                void * t105 = gm_l6;
                gm_t88rcucg1b7uk t106 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t105) + 0));
                gm_t5v8n6qrn4rh4 t107;
                gm_f_repr_53quqki((void *)&t107, (void *)NULL, cx, NULL, t104, t106);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t5v8n6qrn4rh4 t108 = t107;
                uint8_t t109 = ((GM_LD(uint8_t, ((uint8_t *)&t108) + 0) == ((uint8_t)6ULL)));
                uint8_t t110 = (uint8_t)!t109;
                t92 = t110;
            } else t92 = 0;
            if (t92) {
                void * t111 = gm_l0;
                void * t112 = gm_l6;
                uint8_t t113 = gm_f_frame_pure_cbdra2q((void *)NULL, cx, NULL, t111, t112);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                t91 = t113;
            } else t91 = 0;
            if (t91) {
                void * t114 = gm_l0;
                void * t115 = gm_l7;
                uint8_t t116 = gm_f_frame_pure_cbdra2q((void *)NULL, cx, NULL, t114, t115);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                t90 = t116;
            } else t90 = 0;
            uint8_t t117 = 0;
            F.ret = t90;
            goto gm_fret;
        }
        goto me3;
mn85:;
ma10:;
        uint8_t t121 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t121 != ((uint8_t)35ULL)) goto pa120;
        gm_b16_8 t122 = GM_LD(gm_b16_8, ((((uint8_t *)t2) + 8) + 8));
        gm_l8 = t122;
        goto po119;
pa120:;
        uint8_t t124 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t124 != ((uint8_t)36ULL)) goto pa123;
        gm_b16_8 t125 = GM_LD(gm_b16_8, ((((uint8_t *)t2) + 8) + 8));
        gm_l8 = t125;
        goto po119;
pa123:;
        goto mn118;
po119:;
        {
            {
                gm_l11 = ((uint64_t)0ULL);
            }
            {
                uint64_t t128 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                gm_l10 = t128;
            }
            {
                for (;;) {
                    uint64_t t130 = gm_l11;
                    uint64_t t131 = gm_l10;
                    uint8_t t132 = (t130 < t131);
                    if (!t132) goto lb129;
                    {
                        uint64_t t135 = gm_l11;
                        uint64_t t136 = t135;
                        uint64_t t139 = GM_LD(uint64_t, ((uint8_t *)&t136));
                        gm_l12 = t139;
                        {
                            void * t142 = gm_l0;
                            uint64_t t143 = gm_l12;
                            uint64_t t144 = (uintptr_t)t143;
                            uint64_t t145 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                            if (t144 >= (uintptr_t)(t145)) gm_trap_index(t144, t145, gm_srcmok80ho8hh01, 960, 39);
                            void * t146 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l8)) + t144 * 88);
                            uint8_t t147 = gm_f_frame_pure_cbdra2q((void *)NULL, cx, NULL, t142, t146);
                            if (gm_unw.kind) {
                                goto gm_ll133;
                            }
                            uint8_t t148 = (uint8_t)!t147;
                            if (!t148) goto ie141;
                            {
                                uint8_t t150 = 0;
                                F.ret = ((uint8_t)0);
                                goto gm_fret;
                            }
                            goto ix141;
ie141:;
ix141:;
                        }
                        goto me137;
mn138:;
                        gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 959, 13);
me137:;
                    }
lc129:;
                    {
                        uint64_t t152 = gm_l11;
                        uint64_t r153; bool o154 = __builtin_add_overflow(t152, ((uint64_t)1ULL), &r153);
                        uint64_t t155 = r153;
                        gm_l11 = t155;
                    }
                }
gm_ll133:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb129; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc129; }
                }
                goto gm_lad;
lb129:;
            }
        }
        {
            uint8_t t156 = 0;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
        goto me3;
mn118:;
ma11:;
        uint8_t t158 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
        goto me3;
mn157:;
ma12:;
        gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 949, 5);
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
    GM_STACK_CHECK(&F, 293u);
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
                    uint32_t t14 = t13;
                    {
                        gm_b16_8 t18 = gm_l2;
                        gm_tnpifv20irnph t19;
                        GM_ST(gm_b16_8, ((uint8_t *)&t19 + 0), t18);
                        gm_tabb710d9mq2e t20;
                        gm_f_format_lb0inss((void *)&t20, (void *)NULL, cx, NULL, t19);
                        if (gm_unw.kind) {
                            goto gm_ll11;
                        }
                        gm_tabb710d9mq2e t21 = t20;
                        if (((((uint8_t *)&t21))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t21) + 8, gm_srcmok80ho8hh01, 552, 16);
                        gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&t21) + 8));
                        gm_b16_8 t23 = t22;
                        gm_b16_8 t24 = GM_LD(gm_b16_8, ((uint8_t *)&t23));
                        gm_l2 = t24;
                    }
                    {
                        void * t26 = gm_l0;
                        void * t27 = GM_LD(void *, (((uint8_t *)t26) + 0));
                        void * t28 = GM_LD(void *, (((uint8_t *)t27) + 0));
                        uint32_t t29 = gm_l3;
                        void * t30 = gm_f_func_in17r5n((void *)NULL, cx, NULL, t28, t29);
                        if (gm_unw.kind) {
                            goto gm_ll11;
                        }
                        if (!(((((uint8_t *)t30) + 88))[4] != 0)) goto ie25;
                        uint32_t t31 = GM_LD(uint32_t, (((uint8_t *)t30) + 88));
                        gm_l6 = t31;
                        {
                            uint32_t t33 = gm_l6;
                            gm_l3 = t33;
                        }
                        goto ix25;
ie25:;
ix25:;
                    }
                    goto me15;
mn16:;
                    gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 551, 5);
me15:;
                }
lc7:;
                {
                    uint32_t t35 = gm_l5;
                    uint32_t r36; bool o37 = __builtin_add_overflow(t35, ((uint32_t)1ULL), &r36);
                    uint32_t t38 = r36;
                    gm_l5 = t38;
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
        gm_b16_8 t39 = gm_l2;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tommf4j9upomc *)gm_out) + 0, t39);
        uint32_t t40 = gm_l3;
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tommf4j9upomc *)gm_out) + 16, t40);
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
    GM_STACK_CHECK(&F, 322u);
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
                if (o19) gm_trap("integer overflow in `+`", gm_srcmok80ho8hh01, 461, 62);
                uint64_t t20 = r18;
                gm_t4ce0jd3tdro4 t21;
                GM_ST(uint64_t, ((uint8_t *)&t21 + 0), t20);
                gm_tabb710d9mq2e t22;
                gm_f_format_rtv6jsp((void *)&t22, (void *)NULL, cx, NULL, t21);
                if (gm_unw.kind) {
                    goto gm_ll13;
                }
                gm_tabb710d9mq2e t23 = t22;
                if (((((uint8_t *)&t23))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t23) + 8, gm_srcmok80ho8hh01, 461, 44);
                gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&t23) + 8));
                gm_b16_8 t25 = t24;
                gm_b16_8 t26 = GM_LD(gm_b16_8, ((uint8_t *)&t25));
                gm_tftb1odc739t5 t27 = gm_f_push_3ef746b(t15, t26);
                gm_tftb1odc739t5 t28 = t27;
                if (((((uint8_t *)&t28))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t28) + 1, gm_srcmok80ho8hh01, 461, 33);
                gm_b0_1 t29 = GM_LD(gm_b0_1, (((uint8_t *)&t28) + 1));
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
        uint64_t t30 = gm_l1;
        uint64_t r31; bool o32 = __builtin_sub_overflow(t30, ((uint64_t)1ULL), &r31);
        if (o32) gm_trap("integer overflow in `-`", gm_srcmok80ho8hh01, 462, 24);
        uint64_t t33 = r31;
        uint64_t t34 = (uintptr_t)t33;
        void * t35 = gm_l2;
        uint64_t t36 = GM_LD(uintptr_t, (((uint8_t *)t35) + 0) + 8);
        if (t34 >= (uintptr_t)(t36)) gm_trap_index(t34, t36, gm_srcmok80ho8hh01, 462, 12);
        gm_b16_8 t37 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)t35) + 0)) + t34 * 16));
        uint8_t t38 = 0;
        F.ret = t37;
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
    gm_b16_8 gm_l46; /* kill */
    gm_b16_8 gm_l47; /* r */
    gm_b16_8 gm_l48; /* r */
    gm_b16_8 gm_l49; /* ct */
    gm_b16_8 gm_l50; /* tail */
    gm_b16_8 gm_l51; /* text */
    gm_b16_8 gm_l52; /* header */
    uint64_t gm_l53; /* nl */
    GM_STACK_CHECK(&F, 6721u);
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
                gm_tftb1odc739t5 t47 = t46;
                if (((((uint8_t *)&t47))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t47) + 1, gm_srcmok80ho8hh01, 115, 18);
                gm_b0_1 t48 = GM_LD(gm_b0_1, (((uint8_t *)&t47) + 1));
            }
            goto ix41;
ie41:;
ix41:;
        }
        {
            uint8_t t50 = gm_l5;
            uint8_t t51 = (uint8_t)!t50;
            if (!t51) goto ie49;
            {
                void * t53 = (void *)((uint8_t *)&F.l10);
                gm_b16_8 t54;
                GM_ST(void *, (uint8_t *)&t54, (void *)(gm_siji3oo7ud53t));
                GM_ST(uintptr_t, (uint8_t *)&t54 + 8, (uintptr_t)(9));
                gm_tftb1odc739t5 t55 = gm_f_push_3ef746b(t53, t54);
                gm_tftb1odc739t5 t56 = t55;
                if (((((uint8_t *)&t56))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t56) + 1, gm_srcmok80ho8hh01, 117, 9);
                gm_b0_1 t57 = GM_LD(gm_b0_1, (((uint8_t *)&t56) + 1));
            }
            {
                void * t58 = (void *)((uint8_t *)&F.l10);
                gm_b16_8 t59;
                GM_ST(void *, (uint8_t *)&t59, (void *)(gm_s2kvk8bnjk52t));
                GM_ST(uintptr_t, (uint8_t *)&t59 + 8, (uintptr_t)(11));
                gm_tftb1odc739t5 t60 = gm_f_push_3ef746b(t58, t59);
                gm_tftb1odc739t5 t61 = t60;
                if (((((uint8_t *)&t61))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t61) + 1, gm_srcmok80ho8hh01, 118, 9);
                gm_b0_1 t62 = GM_LD(gm_b0_1, (((uint8_t *)&t61) + 1));
            }
            {
                void * t63 = (void *)((uint8_t *)&F.l10);
                gm_b16_8 t64;
                GM_ST(void *, (uint8_t *)&t64, (void *)(gm_sdi44r3kug6n6));
                GM_ST(uintptr_t, (uint8_t *)&t64 + 8, (uintptr_t)(13));
                gm_tftb1odc739t5 t65 = gm_f_push_3ef746b(t63, t64);
                gm_tftb1odc739t5 t66 = t65;
                if (((((uint8_t *)&t66))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t66) + 1, gm_srcmok80ho8hh01, 119, 9);
                gm_b0_1 t67 = GM_LD(gm_b0_1, (((uint8_t *)&t66) + 1));
            }
            goto ix49;
ie49:;
ix49:;
        }
        {
            {
                void * t69 = gm_l2;
                gm_b16_8 t70 = GM_LD(gm_b16_8, (((uint8_t *)t69) + 32));
                gm_l11 = t70;
            }
            {
                gm_l12 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t72 = gm_l12;
                    uint64_t t73 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                    uint8_t t74 = (t72 < t73);
                    if (!t74) goto lb71;
                    {
                        uint64_t t77 = gm_l12;
                        uint64_t t78 = gm_l12;
                        uint64_t t79 = (uintptr_t)t78;
                        uint64_t t80 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                        uint32_t t81 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l11)) + t79 * 4));
                        gm_tn68el8d14vku t82;
                        GM_ST(uint32_t, (uint8_t *)&t82 + 12, 0);
                        GM_ST(uint64_t, ((uint8_t *)&t82 + 0), t77);
                        GM_ST(uint32_t, ((uint8_t *)&t82 + 8), t81);
                        gm_tn68el8d14vku t83 = t82;
                        uint64_t t86 = GM_LD(uint64_t, (((uint8_t *)&t83) + 0));
                        gm_l13 = t86;
                        uint32_t t87 = GM_LD(uint32_t, (((uint8_t *)&t83) + 8));
                        gm_l14 = t87;
                        {
                            uint32_t t89 = gm_l14;
                            uint64_t t90 = (uint64_t)t89;
                            uint64_t t91 = (uintptr_t)t90;
                            void * t92 = gm_l2;
                            uint64_t t93 = GM_LD(uintptr_t, (((uint8_t *)t92) + 48) + 8);
                            if (t91 >= (uintptr_t)(t93)) gm_trap_index(t91, t93, gm_srcmok80ho8hh01, 122, 14);
                            gm_t88rcucg1b7uk t94 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, (((uint8_t *)t92) + 48)) + t91 * 40) + 16));
                            gm_l15 = t94;
                        }
                        {
                            gm_b16_8 t95;
                            GM_ST(void *, (uint8_t *)&t95, (void *)(gm_sr4qbrk6tlgaa));
                            GM_ST(uintptr_t, (uint8_t *)&t95 + 8, (uintptr_t)(0));
                            gm_l16 = t95;
                        }
                        {
                            uint8_t t97 = gm_l5;
                            if (!t97) goto ie96;
                            void * t98 = gm_l0;
                            void * t99 = (void *)(((uint8_t *)t98) + 8);
                            gm_t88rcucg1b7uk t100 = gm_l15;
                            uint8_t t101 = gm_f_by_ref_fi0o860((void *)NULL, cx, NULL, t99, t100);
                            if (gm_unw.kind) {
                                goto gm_ll75;
                            }
                            uint8_t t102 = (uint8_t)!t101;
                            if (!t102) goto ie96;
                            {
                                void * t104 = gm_l0;
                                void * t105 = (void *)(((uint8_t *)t104) + 8);
                                gm_t88rcucg1b7uk t106 = gm_l15;
                                gm_b16_8 t107 = gm_f_plain_cty_q9lhlci((void *)NULL, cx, NULL, t105, t106);
                                if (gm_unw.kind) {
                                    goto gm_ll75;
                                }
                                gm_l16 = t107;
                            }
                            goto ix96;
ie96:;
                            {
                                void * t109 = gm_l0;
                                void * t110 = (void *)(((uint8_t *)t109) + 8);
                                gm_t88rcucg1b7uk t111 = gm_l15;
                                gm_b16_8 t112 = gm_f_param_cty_e8tvhrm((void *)NULL, cx, NULL, t110, t111);
                                if (gm_unw.kind) {
                                    goto gm_ll75;
                                }
                                gm_l16 = t112;
                            }
ix96:;
                        }
                        {
                            void * t113 = (void *)((uint8_t *)&F.l10);
                            gm_t3877pk7lv748 t114;
                            gm_b16_8 t115 = gm_l16;
                            GM_ST(gm_b16_8, (uint8_t *)&t114 + 0, t115);
                            uint64_t t116 = gm_l13;
                            GM_ST(uint64_t, (uint8_t *)&t114 + 16, t116);
                            gm_tabb710d9mq2e t117;
                            gm_f_format_eq87866((void *)&t117, (void *)NULL, cx, NULL, &t114);
                            if (gm_unw.kind) {
                                goto gm_ll75;
                            }
                            gm_tabb710d9mq2e t118 = t117;
                            if (((((uint8_t *)&t118))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t118) + 8, gm_srcmok80ho8hh01, 125, 21);
                            gm_b16_8 t119 = GM_LD(gm_b16_8, (((uint8_t *)&t118) + 8));
                            gm_b16_8 t120 = t119;
                            gm_b16_8 t121 = GM_LD(gm_b16_8, ((uint8_t *)&t120));
                            gm_tftb1odc739t5 t122 = gm_f_push_3ef746b(t113, t121);
                            gm_tftb1odc739t5 t123 = t122;
                            if (((((uint8_t *)&t123))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t123) + 1, gm_srcmok80ho8hh01, 125, 9);
                            gm_b0_1 t124 = GM_LD(gm_b0_1, (((uint8_t *)&t123) + 1));
                        }
                        goto me84;
mn85:;
                        gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 121, 5);
me84:;
                    }
lc71:;
                    {
                        uint64_t t126 = gm_l12;
                        uint64_t r127; bool o128 = __builtin_add_overflow(t126, ((uint64_t)1ULL), &r127);
                        uint64_t t129 = r127;
                        gm_l12 = t129;
                    }
                }
gm_ll75:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb71; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc71; }
                }
                goto gm_lad;
lb71:;
            }
        }
        {
            uint64_t t131 = GM_LD(uintptr_t, (((uint8_t *)&F.l10) + 0) + 8);
            uint8_t t132 = ((t131 == ((uint64_t)0ULL)));
            if (!t132) goto ie130;
            {
                void * t134 = (void *)((uint8_t *)&F.l10);
                gm_b16_8 t135;
                GM_ST(void *, (uint8_t *)&t135, (void *)(gm_sts5j6f5pkov0));
                GM_ST(uintptr_t, (uint8_t *)&t135 + 8, (uintptr_t)(4));
                gm_tftb1odc739t5 t136 = gm_f_push_3ef746b(t134, t135);
                gm_tftb1odc739t5 t137 = t136;
                if (((((uint8_t *)&t137))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t137) + 1, gm_srcmok80ho8hh01, 127, 32);
                gm_b0_1 t138 = GM_LD(gm_b0_1, (((uint8_t *)&t137) + 1));
            }
            goto ix130;
ie130:;
ix130:;
        }
        {
            uint8_t t140;
            if (((((uint8_t *)&gm_l4))[4] != 0)) goto mn142;
            t140 = ((uint8_t)1);
            goto me141;
mn142:;
            t140 = ((uint8_t)0);
            goto me141;
mn143:;
            gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 128, 8);
me141:;
            if (!t140) goto ie139;
            void * t144 = gm_l0;
            uint32_t t145 = gm_l1;
            uint8_t t146 = gm_f_needs_wrapper_ktlbb7i((void *)NULL, cx, NULL, t144, t145);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!t146) goto ie139;
            {
                void * t148 = gm_l0;
                uint32_t t149 = gm_l1;
                void * t150 = gm_l2;
                gm_b0_1 t151 = gm_f_c_abi_wrapper_33vbmt4((void *)NULL, cx, NULL, t148, t149, t150);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix139;
ie139:;
ix139:;
        }
        {
            void * t152 = gm_l0;
            uint32_t t153 = gm_l1;
            gm_b16_8 t154 = gm_f_borrowed_params_b39va3r((void *)NULL, cx, NULL, t152, t153);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l17 = t154;
        }
        {
            void * t155 = gm_l0;
            uint32_t t156 = gm_l1;
            gm_b16_8 t157 = gm_f_frame_locals_mt4nb6o((void *)NULL, cx, NULL, t155, t156);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l18 = t157;
        }
        {
            uint8_t t158 = 0;
            void * t159 = gm_l0;
            uint8_t t160 = GM_LD(uint8_t, ((((uint8_t *)t159) + 1136) + 2));
            uint8_t t161 = (uint8_t)!t160;
            if (t161) {
                void * t162 = gm_l0;
                uint32_t t163 = gm_l1;
                uint8_t t164 = gm_f_long_body_qgvuu1c((void *)NULL, cx, NULL, t162, t163);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                t158 = t164;
            } else t158 = 0;
            gm_l19 = t158;
        }
        {
            gm_b16_8 t165;
            GM_ST(void *, (uint8_t *)&t165, (void *)(gm_sr4qbrk6tlgaa));
            GM_ST(uintptr_t, (uint8_t *)&t165 + 8, (uintptr_t)(0));
            gm_l20 = t165;
        }
        {
            if (!((((uint8_t *)&gm_l4))[4] != 0)) goto ie166;
            uint32_t t167 = GM_LD(uint32_t, ((uint8_t *)&gm_l4));
            gm_l21 = t167;
            {
                void * t169 = gm_l0;
                uint32_t t170 = gm_l1;
                uint32_t t171 = gm_l21;
                gm_b16_8 t172 = gm_f_spec_name_jptsbbf((void *)NULL, cx, NULL, t169, t170, t171);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l20 = t172;
            }
            goto ix166;
ie166:;
            {
                void * t174 = gm_l0;
                uint32_t t175 = gm_l1;
                gm_b16_8 t176 = gm_f_fn_name_6n34573((void *)NULL, cx, NULL, t174, t175);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l20 = t176;
            }
ix166:;
        }
        {
            gm_tud7oovfvnf43 t177;
            void * t178 = gm_l2;
            gm_t1h8kp9hsu3ua t179 = GM_LD(gm_t1h8kp9hsu3ua, (((uint8_t *)t178) + 145));
            gm_b16_8 t180 = gm_f_hint_attrs_mktdhi1((void *)NULL, cx, NULL, t179);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t177 + 0, t180);
            void * t181 = gm_l2;
            uint8_t t182 = GM_LD(uint8_t, (((uint8_t *)t181) + 144));
            gm_b16_8 t183 = gm_f_conv_attr_m0b89pc(t182);
            GM_ST(gm_b16_8, (uint8_t *)&t177 + 16, t183);
            gm_b16_8 t184 = gm_l9;
            GM_ST(gm_b16_8, (uint8_t *)&t177 + 32, t184);
            gm_b16_8 t185 = gm_l20;
            GM_ST(gm_b16_8, (uint8_t *)&t177 + 48, t185);
            gm_b16_8 t186 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 0));
            gm_b16_8 t187 = t186;
            gm_b16_8 t188 = GM_LD(gm_b16_8, ((uint8_t *)&t187));
            gm_b16_8 t189;
            GM_ST(void *, (uint8_t *)&t189, (void *)(gm_sg5gdnrith2i6));
            GM_ST(uintptr_t, (uint8_t *)&t189 + 8, (uintptr_t)(2));
            gm_tabb710d9mq2e t190;
            gm_f_join_sfknljb((void *)&t190, (void *)NULL, cx, NULL, t188, t189);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t191 = t190;
            if (((((uint8_t *)&t191))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t191) + 8, gm_srcmok80ho8hh01, 134, 132);
            gm_b16_8 t192 = GM_LD(gm_b16_8, (((uint8_t *)&t191) + 8));
            GM_ST(gm_b16_8, (uint8_t *)&t177 + 64, t192);
            gm_tabb710d9mq2e t193;
            gm_f_format_822rutp((void *)&t193, (void *)NULL, cx, NULL, &t177);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t194 = t193;
            if (((((uint8_t *)&t194))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t194) + 8, gm_srcmok80ho8hh01, 134, 12);
            gm_b16_8 t195 = GM_LD(gm_b16_8, (((uint8_t *)&t194) + 8));
            gm_l22 = t195;
        }
        {
            void * t196 = gm_l0;
            void * t197 = (void *)(((uint8_t *)t196) + 1888);
            gm_b16_8 t198 = gm_l22;
            gm_tnpifv20irnph t199;
            GM_ST(gm_b16_8, ((uint8_t *)&t199 + 0), t198);
            gm_tftb1odc739t5 t200 = gm_f_write_i2vvij8(t197, t199);
            gm_tftb1odc739t5 t201 = t200;
            if (((((uint8_t *)&t201))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t201) + 1, gm_srcmok80ho8hh01, 135, 5);
            gm_b0_1 t202 = GM_LD(gm_b0_1, (((uint8_t *)&t201) + 1));
        }
        {
            void * t203 = gm_l0;
            void * t204 = GM_LD(void *, (((uint8_t *)t203) + 0));
            void * t205 = GM_LD(void *, (((uint8_t *)t204) + 0));
            void * t206 = gm_l2;
            gm_b16_8 t207 = GM_LD(gm_b16_8, (((uint8_t *)t206) + 48));
            void * t208 = gm_l2;
            void * t209 = GM_LD(void *, (((uint8_t *)t208) + 152));
            uint64_t t210 = gm_f_frame_cost_oei64tv((void *)NULL, cx, NULL, t205, t207, t209);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l23 = t210;
        }
        {
            uint64_t t212 = gm_l23;
            uint64_t t213 = GM_LD(uint64_t, gm_c1id0n0l1pf);
            uint8_t t214 = (t212 > t213);
            if (!t214) goto ie211;
            {
                void * t216 = gm_l2;
                gm_b16_8 t217 = GM_LD(gm_b16_8, (((uint8_t *)t216) + 0));
                gm_b16_8 t218 = gm_f_c_string_n7h27b3((void *)NULL, cx, NULL, t217);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l24 = t218;
            }
            {
                void * t219 = gm_l0;
                void * t220 = (void *)(((uint8_t *)t219) + 1928);
                gm_tftksr4op114e t221;
                gm_b16_8 t222 = gm_l22;
                GM_ST(gm_b16_8, (uint8_t *)&t221 + 0, t222);
                gm_b16_8 t223 = gm_l24;
                GM_ST(gm_b16_8, (uint8_t *)&t221 + 16, t223);
                uint64_t t224 = gm_l23;
                GM_ST(uint64_t, (uint8_t *)&t221 + 32, t224);
                gm_tftb1odc739t5 t225 = gm_f_write_ajj3a37(t220, &t221);
                gm_tftb1odc739t5 t226 = t225;
                if (((((uint8_t *)&t226))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t226) + 1, gm_srcmok80ho8hh01, 141, 9);
                gm_b0_1 t227 = GM_LD(gm_b0_1, (((uint8_t *)&t226) + 1));
            }
            {
                gm_b0_1 t228;
                gm_tqgtd5gtgj65s t229;
                memset(&t229, 0, sizeof t229);
                ((uint8_t *)&t229)[0] = 0;
                GM_ST(gm_b0_1, ((uint8_t *)&t229 + 8), t228);
                gm_tqgtd5gtgj65s t230 = t229;
                uint8_t t231 = ((((uint8_t *)&t230))[0] != 0);
                F.ret = t229;
                goto gm_fret;
            }
            goto ix211;
ie211:;
ix211:;
        }
        {
            void * t232 = gm_l0;
            uint32_t t233 = gm_l1;
            gm_t9805qtonbsag t234 = gm_l4;
            gm_b16_8 t235 = gm_f_fn_tag_okd90ep((void *)NULL, cx, NULL, t232, t233, t234);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l25 = t235;
        }
        {
            memset((uint8_t *)&F.l26 + 12, 0, 4);
            memset((uint8_t *)&F.l26 + 236, 0, 4);
            memset((uint8_t *)&F.l26 + 299, 0, 5);
            memset((uint8_t *)&F.l26 + 349, 0, 3);
            void * t236 = gm_l0;
            GM_ST(void *, (uint8_t *)&F.l26 + 0, t236);
            uint32_t t237 = gm_l1;
            GM_ST(uint32_t, (uint8_t *)&F.l26 + 8, t237);
            gm_b16_8 t238 = gm_l25;
            GM_ST(gm_b16_8, (uint8_t *)&F.l26 + 16, t238);
            gm_t9805qtonbsag t239 = gm_l4;
            GM_ST(gm_t9805qtonbsag, (uint8_t *)&F.l26 + 32, t239);
            gm_t5lf9i3n1r3co t240;
            memset(&t240, 0, sizeof t240);
            GM_ST(gm_t5lf9i3n1r3co, (uint8_t *)&F.l26 + 40, t240);
            gm_tdh3gh5n00hec t241;
            memset(&t241, 0, sizeof t241);
            GM_ST(gm_tdh3gh5n00hec, (uint8_t *)&F.l26 + 64, t241);
            gm_tjefu848ssvm9 t242;
            memset(&t242, 0, sizeof t242);
            GM_ST(gm_tjefu848ssvm9, (uint8_t *)&F.l26 + 104, t242);
            gm_th2lqbo77pvsc t243;
            memset(&t243, 0, sizeof t243);
            GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&F.l26 + 152, t243);
            void * t244 = gm_l2;
            GM_ST(void *, (uint8_t *)&F.l26 + 176, t244);
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
            gm_b16_8 t245 = GM_LD(gm_b16_8, gm_cgsmfcq937e_4);
            GM_ST(gm_b16_8, (uint8_t *)&F.l26 + 280, t245);
            GM_ST(uint8_t, (uint8_t *)&F.l26 + 296, ((uint8_t)0));
            GM_ST(uint8_t, (uint8_t *)&F.l26 + 297, ((uint8_t)0));
            GM_ST(uint8_t, (uint8_t *)&F.l26 + 298, ((uint8_t)0));
            GM_ST(uint64_t, (uint8_t *)&F.l26 + 304, ((uint64_t)0ULL));
            gm_b16_8 t246 = gm_l17;
            GM_ST(gm_b16_8, (uint8_t *)&F.l26 + 312, t246);
            gm_b16_8 t247 = gm_l18;
            GM_ST(gm_b16_8, (uint8_t *)&F.l26 + 328, t247);
            GM_ST(uint32_t, (uint8_t *)&F.l26 + 344, ((uint32_t)0ULL));
            uint8_t t248 = gm_l19;
            GM_ST(uint8_t, (uint8_t *)&F.l26 + 348, t248);
            gm_f_list_ns53od0((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l26 + 352)), (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t249 = (void *)(((uint8_t *)&F.l26) + 184);
            gm_b16_8 t250 = gm_l22;
            gm_tnpifv20irnph t251;
            GM_ST(gm_b16_8, ((uint8_t *)&t251 + 0), t250);
            gm_tftb1odc739t5 t252 = gm_f_write_n0vcum5(t249, t251);
            gm_tftb1odc739t5 t253 = t252;
            if (((((uint8_t *)&t253))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t253) + 1, gm_srcmok80ho8hh01, 158, 5);
            gm_b0_1 t254 = GM_LD(gm_b0_1, (((uint8_t *)&t253) + 1));
        }
        {
            void * t255 = (void *)((uint8_t *)&F.l26);
            void * t256 = gm_l2;
            gm_tl874t2atnlh3 t257 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t256) + 16));
            gm_b0_1 t258 = gm_f_src_line_d24sajl((void *)NULL, cx, NULL, t255, t257);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t260 = gm_l2;
            uint8_t t261 = GM_LD(uint8_t, ((((uint8_t *)t260) + 145) + 0));
            if (!t261) goto ie259;
            {
                void * t263 = (void *)((uint8_t *)&F.l26);
                gm_b16_8 t264;
                GM_ST(void *, (uint8_t *)&t264, (void *)(gm_s2gtin895e08a));
                GM_ST(uintptr_t, (uint8_t *)&t264 + 8, (uintptr_t)(17));
                gm_b0_1 t265 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t263, t264);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix259;
ie259:;
ix259:;
        }
        {
            uint8_t t267 = gm_l5;
            if (!t267) goto ie266;
            {
                void * t269 = (void *)((uint8_t *)&F.l26);
                gm_b16_8 t270;
                GM_ST(void *, (uint8_t *)&t270, (void *)(gm_svdapq3j66qso));
                GM_ST(uintptr_t, (uint8_t *)&t270 + 8, (uintptr_t)(59));
                gm_b0_1 t271 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t269, t270);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix266;
ie266:;
ix266:;
        }
        {
            void * t272 = (void *)((uint8_t *)&F.l26);
            void * t273 = gm_l0;
            uint32_t t274 = gm_l1;
            gm_b16_8 t275 = gm_f_frame_name_stddmgg((void *)NULL, cx, NULL, t273, t274);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tnpifv20irnph t276;
            GM_ST(gm_b16_8, ((uint8_t *)&t276 + 0), t275);
            gm_b0_1 t277 = gm_f_linef_4plvds6((void *)NULL, cx, NULL, t272, t276);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                void * t279 = gm_l2;
                gm_b16_8 t280 = GM_LD(gm_b16_8, (((uint8_t *)t279) + 48));
                gm_l27 = t280;
            }
            {
                gm_l28 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t282 = gm_l28;
                    uint64_t t283 = GM_LD(uintptr_t, ((uint8_t *)&gm_l27) + 8);
                    uint8_t t284 = (t282 < t283);
                    if (!t284) goto lb281;
                    {
                        gm_t5im2v50m3hk6 t287;
                        uint64_t t288 = gm_l28;
                        GM_ST(uint64_t, (uint8_t *)&t287 + 0, t288);
                        uint64_t t289 = gm_l28;
                        uint64_t t290 = (uintptr_t)t289;
                        uint64_t t291 = GM_LD(uintptr_t, ((uint8_t *)&gm_l27) + 8);
                        memcpy((uint8_t *)&t287 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l27)) + t290 * 40), 40);
                        gm_t5im2v50m3hk6 t292 = t287;
                        uint64_t t295 = GM_LD(uint64_t, (((uint8_t *)&t292) + 0));
                        gm_l29 = t295;
                        gm_tsoie1ppgdvcg t296 = GM_LD(gm_tsoie1ppgdvcg, (((uint8_t *)&t292) + 8));
                        F.l30 = t296;
                        {
                            uint64_t t299 = gm_l29;
                            uint64_t t300 = (uintptr_t)t299;
                            uint64_t t301 = GM_LD(uintptr_t, (((uint8_t *)&F.l26) + 328) + 8);
                            if (t300 >= (uintptr_t)(t301)) gm_trap_index(t300, t301, gm_srcmok80ho8hh01, 164, 16);
                            uint8_t t302 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l26) + 328)) + t300 * 1));
                            uint8_t t303 = (uint8_t)!t302;
                            if (!t303) goto ie298;
                            {
                                uint64_t t305 = GM_LD(uint64_t, (((uint8_t *)&F.l26) + 304));
                                void * t306 = GM_LD(void *, (((uint8_t *)&F.l26) + 0));
                                void * t307 = (void *)(((uint8_t *)t306) + 8);
                                gm_t88rcucg1b7uk t308 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l30) + 16));
                                uint64_t t309 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t307, t308);
                                if (gm_unw.kind) {
                                    goto gm_ll285;
                                }
                                uint64_t r310; bool o311 = __builtin_add_overflow(t305, t309, &r310);
                                if (o311) gm_trap("integer overflow in `+`", gm_srcmok80ho8hh01, 165, 13);
                                uint64_t t312 = r310;
                                GM_ST(uint64_t, (((uint8_t *)&F.l26) + 304), t312);
                            }
                            {
                                void * t313 = GM_LD(void *, (((uint8_t *)&F.l26) + 0));
                                void * t314 = (void *)(((uint8_t *)t313) + 8);
                                gm_t88rcucg1b7uk t315 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l30) + 16));
                                gm_b16_8 t316 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t314, t315);
                                if (gm_unw.kind) {
                                    goto gm_ll285;
                                }
                                gm_l31 = t316;
                            }
                            {
                                void * t317 = (void *)((uint8_t *)&F.l26);
                                gm_tq3m5qgvkpje8 t318;
                                gm_b16_8 t319 = gm_l31;
                                GM_ST(gm_b16_8, (uint8_t *)&t318 + 0, t319);
                                uint64_t t320 = gm_l29;
                                GM_ST(uint64_t, (uint8_t *)&t318 + 16, t320);
                                gm_b16_8 t321 = GM_LD(gm_b16_8, (((uint8_t *)&F.l30) + 0));
                                GM_ST(gm_b16_8, (uint8_t *)&t318 + 24, t321);
                                gm_b0_1 t322 = gm_f_linef_md5jtn8((void *)NULL, cx, NULL, t317, &t318);
                                if (gm_unw.kind) {
                                    goto gm_ll285;
                                }
                            }
                            goto ix298;
ie298:;
ix298:;
                        }
                        goto me293;
mn294:;
                        gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 163, 5);
me293:;
                    }
lc281:;
                    {
                        uint64_t t324 = gm_l28;
                        uint64_t r325; bool o326 = __builtin_add_overflow(t324, ((uint64_t)1ULL), &r325);
                        uint64_t t327 = r325;
                        gm_l28 = t327;
                    }
                }
gm_ll285:;
                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb281; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc281; }
                }
                goto gm_lad;
lb281:;
            }
        }
        {
            void * t329 = GM_LD(void *, (((uint8_t *)&F.l26) + 0));
            uint8_t t330 = GM_LD(uint8_t, ((((uint8_t *)t329) + 1136) + 0));
            if (!t330) goto ie328;
            void * t331 = GM_LD(void *, (((uint8_t *)&F.l26) + 0));
            void * t332 = gm_l2;
            uint8_t t333 = gm_f_probe_free_4hilimu((void *)NULL, cx, NULL, t331, t332);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t334 = (uint8_t)!t333;
            if (!t334) goto ie328;
            {
                void * t337 = GM_LD(void *, (((uint8_t *)&F.l26) + 0));
                uint32_t t338 = gm_l1;
                uint32_t t339 = gm_f_call_depth_4m6ap7n((void *)NULL, cx, NULL, t337, t338);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint32_t t340 = GM_LD(uint32_t, gm_cdmki9acqi9);
                uint8_t t341 = (t339 <= t340);
                if (!t341) goto ie336;
                {
                    void * t343 = (void *)((uint8_t *)&F.l26);
                    gm_b16_8 t344;
                    GM_ST(void *, (uint8_t *)&t344, (void *)(gm_s8hh4pp2rpnlj));
                    GM_ST(uintptr_t, (uint8_t *)&t344 + 8, (uintptr_t)(42));
                    gm_b0_1 t345 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t343, t344);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                goto ix336;
ie336:;
                {
                    void * t347 = (void *)((uint8_t *)&F.l26);
                    gm_b16_8 t348;
                    GM_ST(void *, (uint8_t *)&t348, (void *)(gm_sjs17a3d2ohel));
                    GM_ST(uintptr_t, (uint8_t *)&t348 + 8, (uintptr_t)(34));
                    gm_b0_1 t349 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t347, t348);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
ix336:;
            }
            goto ix328;
ie328:;
ix328:;
        }
        {
            gm_b16_8 t350;
            GM_ST(void *, (uint8_t *)&t350, (void *)(gm_sr4qbrk6tlgaa));
            GM_ST(uintptr_t, (uint8_t *)&t350 + 8, (uintptr_t)(0));
            gm_l32 = t350;
        }
        {
            void * t352 = GM_LD(void *, (((uint8_t *)&F.l26) + 0));
            uint8_t t353 = GM_LD(uint8_t, ((((uint8_t *)t352) + 1136) + 1));
            if (!t353) goto ie351;
            {
                gm_b16_8 t355;
                GM_ST(void *, (uint8_t *)&t355, (void *)(gm_s04qus0luoga5));
                GM_ST(uintptr_t, (uint8_t *)&t355 + 8, (uintptr_t)(4));
                gm_l33 = t355;
            }
            {
                void * t357 = GM_LD(void *, (((uint8_t *)&F.l26) + 0));
                void * t358 = GM_LD(void *, (((uint8_t *)t357) + 0));
                uint32_t t359 = gm_l1;
                gm_th2lqbo77pvsc t360;
                gm_f_backtrace_name_mars429((void *)&t360, (void *)NULL, cx, NULL, t358, t359);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_th2lqbo77pvsc t361 = t360;
                if (!((((uint8_t *)&t361))[16] != 0)) goto ie356;
                gm_b16_8 t362 = GM_LD(gm_b16_8, ((uint8_t *)&t361));
                gm_l34 = t362;
                {
                    gm_b16_8 t364 = gm_l34;
                    gm_b16_8 t365 = gm_f_c_string_n7h27b3((void *)NULL, cx, NULL, t364);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l33 = t365;
                }
                goto ix356;
ie356:;
ix356:;
            }
            {
                void * t366 = (void *)((uint8_t *)&F.l26);
                gm_b16_8 t367 = gm_l33;
                gm_tnpifv20irnph t368;
                GM_ST(gm_b16_8, ((uint8_t *)&t368 + 0), t367);
                gm_b0_1 t369 = gm_f_linef_cuactnf((void *)NULL, cx, NULL, t366, t368);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                gm_b16_8 t370;
                GM_ST(void *, (uint8_t *)&t370, (void *)(gm_sn2gg3b98t635));
                GM_ST(uintptr_t, (uint8_t *)&t370 + 8, (uintptr_t)(20));
                gm_l32 = t370;
            }
            goto ix351;
ie351:;
ix351:;
        }
        {
            void * t371 = (void *)((uint8_t *)&F.l26);
            gm_b16_8 t372;
            GM_ST(void *, (uint8_t *)&t372, (void *)(gm_s2micd2hub1nf));
            GM_ST(uintptr_t, (uint8_t *)&t372 + 8, (uintptr_t)(25));
            gm_b0_1 t373 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t371, t372);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            int32_t t374;
            void * t376 = gm_l2;
            uint8_t t377 = ((GM_LD(uint8_t, (((uint8_t *)t376) + 96) + 0) == ((uint8_t)2ULL)));
            if (!t377) goto ie375;
            int32_t t379 = ((int32_t)1LL);
            t374 = t379;
            goto ix375;
ie375:;
            int32_t t381 = ((int32_t)0LL);
            t374 = t381;
ix375:;
            gm_l35 = t374;
        }
        {
            void * t382 = (void *)((uint8_t *)&F.l26);
            int32_t t383 = gm_l35;
            gm_t19enj90vg8bd t384;
            GM_ST(int32_t, ((uint8_t *)&t384 + 0), t383);
            gm_b0_1 t385 = gm_f_linef_cs9kbvs((void *)NULL, cx, NULL, t382, t384);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t386 = (void *)((uint8_t *)&F.l26);
            gm_b16_8 t387;
            GM_ST(void *, (uint8_t *)&t387, (void *)(gm_sdikuksp636di));
            GM_ST(uintptr_t, (uint8_t *)&t387 + 8, (uintptr_t)(11));
            gm_b0_1 t388 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t386, t387);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                void * t390 = gm_l2;
                gm_b16_8 t391 = GM_LD(gm_b16_8, (((uint8_t *)t390) + 32));
                gm_l36 = t391;
            }
            {
                gm_l37 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t393 = gm_l37;
                    uint64_t t394 = GM_LD(uintptr_t, ((uint8_t *)&gm_l36) + 8);
                    uint8_t t395 = (t393 < t394);
                    if (!t395) goto lb392;
                    {
                        uint64_t t398 = gm_l37;
                        uint64_t t399 = gm_l37;
                        uint64_t t400 = (uintptr_t)t399;
                        uint64_t t401 = GM_LD(uintptr_t, ((uint8_t *)&gm_l36) + 8);
                        uint32_t t402 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l36)) + t400 * 4));
                        gm_tn68el8d14vku t403;
                        GM_ST(uint32_t, (uint8_t *)&t403 + 12, 0);
                        GM_ST(uint64_t, ((uint8_t *)&t403 + 0), t398);
                        GM_ST(uint32_t, ((uint8_t *)&t403 + 8), t402);
                        gm_tn68el8d14vku t404 = t403;
                        uint64_t t407 = GM_LD(uint64_t, (((uint8_t *)&t404) + 0));
                        gm_l38 = t407;
                        uint32_t t408 = GM_LD(uint32_t, (((uint8_t *)&t404) + 8));
                        gm_l39 = t408;
                        {
                            uint32_t t410 = gm_l39;
                            uint64_t t411 = (uint64_t)t410;
                            uint64_t t412 = (uintptr_t)t411;
                            void * t413 = gm_l2;
                            uint64_t t414 = GM_LD(uintptr_t, (((uint8_t *)t413) + 48) + 8);
                            if (t412 >= (uintptr_t)(t414)) gm_trap_index(t412, t414, gm_srcmok80ho8hh01, 191, 14);
                            gm_t88rcucg1b7uk t415 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, (((uint8_t *)t413) + 48)) + t412 * 40) + 16));
                            gm_l40 = t415;
                        }
                        {
                            uint64_t t417 = gm_l38;
                            uint64_t t418 = (uintptr_t)t417;
                            uint64_t t419 = GM_LD(uintptr_t, (((uint8_t *)&F.l26) + 312) + 8);
                            if (t418 >= (uintptr_t)(t419)) gm_trap_index(t418, t419, gm_srcmok80ho8hh01, 192, 12);
                            uint8_t t420 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l26) + 312)) + t418 * 1));
                            if (!t420) goto ie416;
                            {
                                goto lc392;
                            }
                            goto ix416;
ie416:;
ix416:;
                        }
                        {
                            void * t423 = GM_LD(void *, (((uint8_t *)&F.l26) + 0));
                            void * t424 = (void *)(((uint8_t *)t423) + 8);
                            gm_t88rcucg1b7uk t425 = gm_l40;
                            uint8_t t426 = gm_f_by_ref_fi0o860((void *)NULL, cx, NULL, t424, t425);
                            if (gm_unw.kind) {
                                goto gm_ll396;
                            }
                            if (!t426) goto ie422;
                            {
                                void * t428 = (void *)((uint8_t *)&F.l26);
                                uint32_t t429 = gm_l39;
                                gm_b16_8 t430 = gm_f_local_lv_m2jtf0n((void *)NULL, cx, NULL, t428, t429);
                                if (gm_unw.kind) {
                                    goto gm_ll396;
                                }
                                gm_l41 = t430;
                            }
                            {
                                void * t431 = (void *)((uint8_t *)&F.l26);
                                gm_tq3m5qgvkpje8 t432;
                                gm_b16_8 t433 = gm_l41;
                                GM_ST(gm_b16_8, (uint8_t *)&t432 + 0, t433);
                                uint64_t t434 = gm_l38;
                                GM_ST(uint64_t, (uint8_t *)&t432 + 16, t434);
                                gm_b16_8 t435 = gm_l41;
                                GM_ST(gm_b16_8, (uint8_t *)&t432 + 24, t435);
                                gm_b0_1 t436 = gm_f_linef_nvqsj6t((void *)NULL, cx, NULL, t431, &t432);
                                if (gm_unw.kind) {
                                    goto gm_ll396;
                                }
                            }
                            {
                                goto lc392;
                            }
                            goto ix422;
ie422:;
ix422:;
                        }
                        {
                            uint64_t t437 = gm_l38;
                            gm_t4ce0jd3tdro4 t438;
                            GM_ST(uint64_t, ((uint8_t *)&t438 + 0), t437);
                            gm_tabb710d9mq2e t439;
                            gm_f_format_l05am3t((void *)&t439, (void *)NULL, cx, NULL, t438);
                            if (gm_unw.kind) {
                                goto gm_ll396;
                            }
                            gm_tabb710d9mq2e t440 = t439;
                            if (((((uint8_t *)&t440))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t440) + 8, gm_srcmok80ho8hh01, 198, 15);
                            gm_b16_8 t441 = GM_LD(gm_b16_8, (((uint8_t *)&t440) + 8));
                            gm_l42 = t441;
                        }
                        {
                            gm_b16_8 t442 = gm_l42;
                            gm_b16_8 t443 = t442;
                            gm_b16_8 t444 = GM_LD(gm_b16_8, ((uint8_t *)&t443));
                            gm_l43 = t444;
                        }
                        {
                            uint8_t t446 = gm_l5;
                            if (!t446) goto ie445;
                            {
                                void * t448 = (void *)((uint8_t *)&F.l26);
                                gm_t88rcucg1b7uk t449 = gm_l40;
                                gm_b16_8 t450 = gm_l42;
                                gm_b16_8 t451 = t450;
                                gm_b16_8 t452 = GM_LD(gm_b16_8, ((uint8_t *)&t451));
                                gm_b16_8 t453 = gm_f_gamma_value_rgbfhcc((void *)NULL, cx, NULL, t448, t449, t452);
                                if (gm_unw.kind) {
                                    goto gm_ll396;
                                }
                                gm_l43 = t453;
                            }
                            goto ix445;
ie445:;
ix445:;
                        }
                        {
                            void * t454 = (void *)((uint8_t *)&F.l26);
                            uint32_t t455 = gm_l39;
                            gm_b16_8 t456 = gm_f_local_lv_m2jtf0n((void *)NULL, cx, NULL, t454, t455);
                            if (gm_unw.kind) {
                                goto gm_ll396;
                            }
                            gm_l44 = t456;
                        }
                        {
                            void * t457 = (void *)((uint8_t *)&F.l26);
                            gm_t6j6smj7qoctu t458;
                            gm_b16_8 t459 = gm_l44;
                            GM_ST(gm_b16_8, (uint8_t *)&t458 + 0, t459);
                            gm_b16_8 t460 = gm_l43;
                            GM_ST(gm_b16_8, (uint8_t *)&t458 + 16, t460);
                            gm_b0_1 t461 = gm_f_linef_uu8i6ue((void *)NULL, cx, NULL, t457, &t458);
                            if (gm_unw.kind) {
                                goto gm_ll396;
                            }
                        }
                        goto me405;
mn406:;
                        gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 190, 5);
me405:;
                    }
lc392:;
                    {
                        uint64_t t463 = gm_l37;
                        uint64_t r464; bool o465 = __builtin_add_overflow(t463, ((uint64_t)1ULL), &r464);
                        uint64_t t466 = r464;
                        gm_l37 = t466;
                    }
                }
gm_ll396:;
                if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb392; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc392; }
                }
                goto gm_lad;
lb392:;
            }
        }
        {
            uint8_t t468 = gm_l6;
            if (!t468) goto ie467;
            void * t469 = (void *)((uint8_t *)&F.l26);
            void * t470 = gm_l3;
            uint8_t t471 = gm_f_builds_in_place_ae1b90u((void *)NULL, cx, NULL, t469, t470);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!t471) goto ie467;
            {
                void * t473 = GM_LD(void *, (((uint8_t *)&F.l26) + 0));
                void * t474 = (void *)(((uint8_t *)t473) + 8);
                void * t475 = gm_l2;
                gm_t88rcucg1b7uk t476 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t475) + 64));
                gm_b16_8 t477 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t474, t476);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l45 = t477;
            }
            {
                void * t478 = (void *)((uint8_t *)&F.l26);
                void * t479 = gm_l3;
                gm_b16_8 t480 = gm_l45;
                gm_tnpifv20irnph t481;
                GM_ST(gm_b16_8, ((uint8_t *)&t481 + 0), t480);
                gm_tabb710d9mq2e t482;
                gm_f_format_9c0uitb((void *)&t482, (void *)NULL, cx, NULL, t481);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t483 = t482;
                if (((((uint8_t *)&t483))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t483) + 8, gm_srcmok80ho8hh01, 207, 32);
                gm_b16_8 t484 = GM_LD(gm_b16_8, (((uint8_t *)&t483) + 8));
                gm_b16_8 t485 = t484;
                gm_b16_8 t486 = GM_LD(gm_b16_8, ((uint8_t *)&t485));
                gm_tnrltbc61i6v1 t487;
                GM_ST(uint8_t, (uint8_t *)&t487 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t487 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t487 + 4, 0);
                GM_ST(uint32_t, (uint8_t *)&t487 + 8, 0);
                GM_ST(uint8_t, ((uint8_t *)&t487 + 0), ((uint8_t)1ULL));
                gm_tqgtd5gtgj65s t488;
                gm_f_expr_into_dn9kavo((void *)&t488, (void *)NULL, cx, NULL, t478, t479, t486, t487);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tqgtd5gtgj65s t489 = t488;
                if (((((uint8_t *)&t489))[0] != 0)) {
                    gm_t08olq97rscn3 t490 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t489) + 8));
                    gm_tqgtd5gtgj65s t491;
                    GM_ST(uint8_t, (uint8_t *)&t491 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t491 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t491 + 4, 0);
                    ((uint8_t *)&t491)[0] = 1;
                    GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t491 + 8), t490);
                    F.ret = t491;
                    goto gm_fret;
                }
                gm_b0_1 t492 = GM_LD(gm_b0_1, (((uint8_t *)&t489) + 8));
            }
            {
                void * t493 = (void *)((uint8_t *)&F.l26);
                gm_b16_8 t494;
                GM_ST(void *, (uint8_t *)&t494, (void *)(gm_svj967tkdfgka));
                GM_ST(uintptr_t, (uint8_t *)&t494 + 8, (uintptr_t)(14));
                gm_b0_1 t495 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t493, t494);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix467;
ie467:;
            void * t497 = GM_LD(void *, (((uint8_t *)&F.l26) + 0));
            void * t498 = (void *)(((uint8_t *)t497) + 8);
            void * t499 = gm_l3;
            gm_t88rcucg1b7uk t500 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t499) + 0));
            uint8_t t501 = gm_f_is_never_in5ftma(t498, GM_CONV(gm_csguankko4lmhd, t500));
            uint8_t t502 = (uint8_t)!t501;
            if (!t502) goto ie496;
            {
                void * t504 = (void *)((uint8_t *)&F.l26);
                void * t505 = gm_l3;
                gm_b16_8 t506;
                GM_ST(void *, (uint8_t *)&t506, (void *)(gm_svg1jvtfj7fgp));
                GM_ST(uintptr_t, (uint8_t *)&t506 + 8, (uintptr_t)(5));
                gm_tnrltbc61i6v1 t507;
                GM_ST(uint8_t, (uint8_t *)&t507 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t507 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t507 + 4, 0);
                GM_ST(uint32_t, (uint8_t *)&t507 + 8, 0);
                GM_ST(uint8_t, ((uint8_t *)&t507 + 0), ((uint8_t)0ULL));
                gm_tqgtd5gtgj65s t508;
                gm_f_expr_into_dn9kavo((void *)&t508, (void *)NULL, cx, NULL, t504, t505, t506, t507);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tqgtd5gtgj65s t509 = t508;
                if (((((uint8_t *)&t509))[0] != 0)) {
                    gm_t08olq97rscn3 t510 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t509) + 8));
                    gm_tqgtd5gtgj65s t511;
                    GM_ST(uint8_t, (uint8_t *)&t511 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t511 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t511 + 4, 0);
                    ((uint8_t *)&t511)[0] = 1;
                    GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t511 + 8), t510);
                    F.ret = t511;
                    goto gm_fret;
                }
                gm_b0_1 t512 = GM_LD(gm_b0_1, (((uint8_t *)&t509) + 8));
            }
            goto ix496;
ie496:;
            {
                {
                    void * t515 = (void *)((uint8_t *)&F.l26);
                    void * t516 = gm_l3;
                    gm_t5o0tpv5jka2l t517;
                    gm_f_expr_porbc9t((void *)&t517, (void *)NULL, cx, NULL, t515, t516);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t5o0tpv5jka2l t518 = t517;
                    if (((((uint8_t *)&t518))[0] != 0)) {
                        gm_t08olq97rscn3 t519 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t518) + 8));
                        gm_tqgtd5gtgj65s t520;
                        GM_ST(uint8_t, (uint8_t *)&t520 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t520 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t520 + 4, 0);
                        ((uint8_t *)&t520)[0] = 1;
                        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t520 + 8), t519);
                        F.ret = t520;
                        goto gm_fret;
                    }
                    gm_b16_8 t521 = GM_LD(gm_b16_8, (((uint8_t *)&t518) + 8));
                }
            }
ix496:;
ix467:;
        }
        {
            uint8_t t523 = GM_LD(uint8_t, (((uint8_t *)&F.l26) + 297));
            if (!t523) goto ie522;
            {
                void * t525 = (void *)((uint8_t *)&F.l26);
                gm_b16_8 t526;
                GM_ST(void *, (uint8_t *)&t526, (void *)(gm_s0r850bpi2rnm));
                GM_ST(uintptr_t, (uint8_t *)&t526 + 8, (uintptr_t)(13));
                gm_b0_1 t527 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t525, t526);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t528 = (void *)((uint8_t *)&F.l26);
                gm_b16_8 t529;
                GM_ST(void *, (uint8_t *)&t529, (void *)(gm_s2hi6cc9n75l5));
                GM_ST(uintptr_t, (uint8_t *)&t529 + 8, (uintptr_t)(6));
                gm_b0_1 t530 = gm_f_label_710oev4((void *)NULL, cx, NULL, t528, t529);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t531 = (void *)((uint8_t *)&F.l26);
                gm_b16_8 t532;
                GM_ST(void *, (uint8_t *)&t532, (void *)(gm_spdft68ac8308));
                GM_ST(uintptr_t, (uint8_t *)&t532 + 8, (uintptr_t)(94));
                gm_b0_1 t533 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t531, t532);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t534 = (void *)((uint8_t *)&F.l26);
                gm_b16_8 t535;
                GM_ST(void *, (uint8_t *)&t535, (void *)(gm_sgdqi8a9n3f12));
                GM_ST(uintptr_t, (uint8_t *)&t535 + 8, (uintptr_t)(14));
                gm_b0_1 t536 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t534, t535);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix522;
ie522:;
ix522:;
        }
        {
            gm_b16_8 t537;
            GM_ST(void *, (uint8_t *)&t537, (void *)(gm_sr4qbrk6tlgaa));
            GM_ST(uintptr_t, (uint8_t *)&t537 + 8, (uintptr_t)(0));
            gm_l46 = t537;
        }
        {
            void * t539 = GM_LD(void *, (((uint8_t *)&F.l26) + 0));
            uint8_t t540 = GM_LD(uint8_t, ((((uint8_t *)t539) + 1136) + 1));
            if (!t540) goto ie538;
            {
                gm_b16_8 t542 = gm_l32;
                gm_tnpifv20irnph t543;
                GM_ST(gm_b16_8, ((uint8_t *)&t543 + 0), t542);
                gm_tabb710d9mq2e t544;
                gm_f_format_d6fj9tl((void *)&t544, (void *)NULL, cx, NULL, t543);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t545 = t544;
                if (((((uint8_t *)&t545))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t545) + 8, gm_srcmok80ho8hh01, 222, 33);
                gm_b16_8 t546 = GM_LD(gm_b16_8, (((uint8_t *)&t545) + 8));
                gm_b16_8 t547 = t546;
                gm_b16_8 t548 = GM_LD(gm_b16_8, ((uint8_t *)&t547));
                gm_l46 = t548;
            }
            goto ix538;
ie538:;
ix538:;
        }
        {
            uint8_t t550 = gm_l6;
            if (!t550) goto ie549;
            {
                void * t552 = (void *)(((uint8_t *)&F.l26) + 184);
                gm_t6j6smj7qoctu t553;
                gm_b16_8 t554 = gm_l46;
                GM_ST(gm_b16_8, (uint8_t *)&t553 + 0, t554);
                gm_b16_8 t555 = gm_l46;
                GM_ST(gm_b16_8, (uint8_t *)&t553 + 16, t555);
                gm_tftb1odc739t5 t556 = gm_f_write_va1cfg2(t552, &t553);
                gm_tftb1odc739t5 t557 = t556;
                if (((((uint8_t *)&t557))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t557) + 1, gm_srcmok80ho8hh01, 225, 9);
                gm_b0_1 t558 = GM_LD(gm_b0_1, (((uint8_t *)&t557) + 1));
            }
            goto ix549;
ie549:;
            uint8_t t560 = gm_l7;
            if (!t560) goto ie559;
            {
                void * t562 = (void *)(((uint8_t *)&F.l26) + 184);
                gm_t6j6smj7qoctu t563;
                gm_b16_8 t564 = gm_l46;
                GM_ST(gm_b16_8, (uint8_t *)&t563 + 0, t564);
                gm_b16_8 t565 = gm_l46;
                GM_ST(gm_b16_8, (uint8_t *)&t563 + 16, t565);
                gm_tftb1odc739t5 t566 = gm_f_write_m7rqe8h(t562, &t563);
                gm_tftb1odc739t5 t567 = t566;
                if (((((uint8_t *)&t567))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t567) + 1, gm_srcmok80ho8hh01, 227, 9);
                gm_b0_1 t568 = GM_LD(gm_b0_1, (((uint8_t *)&t567) + 1));
            }
            goto ix559;
ie559:;
            uint64_t t570 = GM_LD(uintptr_t, ((uint8_t *)&gm_l46) + 8);
            uint8_t t571 = ((t570 == ((uint64_t)0ULL)));
            if (!t571) goto ie569;
            {
                gm_b16_8 t573;
                GM_ST(void *, (uint8_t *)&t573, (void *)(gm_svg1jvtfj7fgp));
                GM_ST(uintptr_t, (uint8_t *)&t573 + 8, (uintptr_t)(5));
                gm_l47 = t573;
            }
            {
                uint8_t t575 = gm_l8;
                uint8_t t576 = (uint8_t)!t575;
                if (!t576) goto ie574;
                {
                    void * t578 = (void *)((uint8_t *)&F.l26);
                    void * t579 = gm_l2;
                    gm_t88rcucg1b7uk t580 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t579) + 64));
                    gm_b16_8 t581;
                    GM_ST(void *, (uint8_t *)&t581, (void *)(gm_svg1jvtfj7fgp));
                    GM_ST(uintptr_t, (uint8_t *)&t581 + 8, (uintptr_t)(5));
                    gm_b16_8 t582 = gm_f_plain_value_e380k1u((void *)NULL, cx, NULL, t578, t580, t581);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l47 = t582;
                }
                goto ix574;
ie574:;
ix574:;
            }
            {
                void * t583 = (void *)(((uint8_t *)&F.l26) + 184);
                gm_t6j6smj7qoctu t584;
                gm_b16_8 t585 = gm_l47;
                GM_ST(gm_b16_8, (uint8_t *)&t584 + 0, t585);
                gm_b16_8 t586 = gm_l47;
                GM_ST(gm_b16_8, (uint8_t *)&t584 + 16, t586);
                gm_tftb1odc739t5 t587 = gm_f_write_3c9chj4(t583, &t584);
                gm_tftb1odc739t5 t588 = t587;
                if (((((uint8_t *)&t588))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t588) + 1, gm_srcmok80ho8hh01, 231, 9);
                gm_b0_1 t589 = GM_LD(gm_b0_1, (((uint8_t *)&t588) + 1));
            }
            goto ix569;
ie569:;
            {
                gm_b16_8 t591;
                GM_ST(void *, (uint8_t *)&t591, (void *)(gm_ss03naco6s5l7));
                GM_ST(uintptr_t, (uint8_t *)&t591 + 8, (uintptr_t)(4));
                gm_l48 = t591;
            }
            {
                uint8_t t593 = gm_l8;
                uint8_t t594 = (uint8_t)!t593;
                if (!t594) goto ie592;
                {
                    void * t596 = (void *)((uint8_t *)&F.l26);
                    void * t597 = gm_l2;
                    gm_t88rcucg1b7uk t598 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t597) + 64));
                    gm_b16_8 t599;
                    GM_ST(void *, (uint8_t *)&t599, (void *)(gm_ss03naco6s5l7));
                    GM_ST(uintptr_t, (uint8_t *)&t599 + 8, (uintptr_t)(4));
                    gm_b16_8 t600 = gm_f_plain_value_e380k1u((void *)NULL, cx, NULL, t596, t598, t599);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l48 = t600;
                }
                goto ix592;
ie592:;
ix592:;
            }
            {
                void * t601 = GM_LD(void *, (((uint8_t *)&F.l26) + 0));
                void * t602 = (void *)(((uint8_t *)t601) + 8);
                void * t603 = gm_l2;
                gm_t88rcucg1b7uk t604 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t603) + 64));
                gm_b16_8 t605 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t602, t604);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l49 = t605;
            }
            {
                gm_tfeab279c16rr t606;
                gm_b16_8 t607 = gm_l49;
                GM_ST(gm_b16_8, (uint8_t *)&t606 + 0, t607);
                gm_b16_8 t608 = gm_l46;
                GM_ST(gm_b16_8, (uint8_t *)&t606 + 16, t608);
                gm_b16_8 t609 = gm_l48;
                GM_ST(gm_b16_8, (uint8_t *)&t606 + 32, t609);
                gm_tabb710d9mq2e t610;
                gm_f_format_hi89jlk((void *)&t610, (void *)NULL, cx, NULL, &t606);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t611 = t610;
                if (((((uint8_t *)&t611))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t611) + 8, gm_srcmok80ho8hh01, 236, 17);
                gm_b16_8 t612 = GM_LD(gm_b16_8, (((uint8_t *)&t611) + 8));
                gm_l50 = t612;
            }
            {
                void * t613 = (void *)(((uint8_t *)&F.l26) + 184);
                gm_t6j6smj7qoctu t614;
                gm_b16_8 t615 = gm_l50;
                GM_ST(gm_b16_8, (uint8_t *)&t614 + 0, t615);
                gm_b16_8 t616 = gm_l50;
                GM_ST(gm_b16_8, (uint8_t *)&t614 + 16, t616);
                gm_tftb1odc739t5 t617 = gm_f_write_ih4qokb(t613, &t614);
                gm_tftb1odc739t5 t618 = t617;
                if (((((uint8_t *)&t618))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t618) + 1, gm_srcmok80ho8hh01, 237, 9);
                gm_b0_1 t619 = GM_LD(gm_b0_1, (((uint8_t *)&t618) + 1));
            }
ix569:;
ix559:;
ix549:;
        }
        {
            uint8_t t621 = gm_l6;
            uint8_t t622 = (uint8_t)!t621;
            if (!t622) goto ie620;
            {
                uint64_t t624 = GM_LD(uint64_t, (((uint8_t *)&F.l26) + 304));
                void * t625 = GM_LD(void *, (((uint8_t *)&F.l26) + 0));
                void * t626 = (void *)(((uint8_t *)t625) + 8);
                void * t627 = gm_l2;
                gm_t88rcucg1b7uk t628 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t627) + 64));
                uint64_t t629 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t626, t628);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint64_t r630; bool o631 = __builtin_mul_overflow(((uint64_t)2ULL), t629, &r630);
                if (o631) gm_trap("integer overflow in `*`", gm_srcmok80ho8hh01, 240, 39);
                uint64_t t632 = r630;
                uint64_t r633; bool o634 = __builtin_add_overflow(t624, t632, &r633);
                if (o634) gm_trap("integer overflow in `+`", gm_srcmok80ho8hh01, 240, 22);
                uint64_t t635 = r633;
                GM_ST(uint64_t, (((uint8_t *)&F.l26) + 304), t635);
            }
            goto ix620;
ie620:;
ix620:;
        }
        {
            void * t636 = (void *)((uint8_t *)&F.l26);
            gm_b0_1 t637 = gm_f_end_lines_99v0bhd((void *)NULL, cx, NULL, t636);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_b16_8 t638 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l26) + 184) + 0));
            gm_b16_8 t639 = t638;
            gm_b16_8 t640 = GM_LD(gm_b16_8, ((uint8_t *)&t639));
            gm_b16_8 t641;
            GM_ST(void *, (uint8_t *)&t641, (void *)(gm_sv394jlms97cd));
            GM_ST(uintptr_t, (uint8_t *)&t641 + 8, (uintptr_t)(13));
            uint64_t t642 = GM_LD(uint64_t, (((uint8_t *)&F.l26) + 304));
            gm_t4ce0jd3tdro4 t643;
            GM_ST(uint64_t, ((uint8_t *)&t643 + 0), t642);
            gm_tabb710d9mq2e t644;
            gm_f_format_7t9p78f((void *)&t644, (void *)NULL, cx, NULL, t643);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t645 = t644;
            if (((((uint8_t *)&t645))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t645) + 8, gm_srcmok80ho8hh01, 242, 54);
            gm_b16_8 t646 = GM_LD(gm_b16_8, (((uint8_t *)&t645) + 8));
            gm_b16_8 t647 = t646;
            gm_b16_8 t648 = GM_LD(gm_b16_8, ((uint8_t *)&t647));
            gm_b16_8 t649 = gm_f_replacen1_7ksfmmn((void *)NULL, cx, NULL, t640, t641, t648);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l51 = t649;
        }
        {
            void * t651 = GM_LD(void *, (((uint8_t *)&F.l26) + 0));
            uint8_t t652 = GM_LD(uint8_t, ((((uint8_t *)t651) + 1136) + 1));
            uint8_t t653 = (uint8_t)!t652;
            if (!t653) goto ie650;
            uint32_t t654 = GM_LD(uint32_t, (((uint8_t *)&F.l26) + 232));
            uint32_t t655 = GM_LD(uint32_t, gm_csshki16t34);
            uint8_t t656 = (t654 <= t655);
            if (!t656) goto ie650;
            void * t657 = gm_l2;
            uint8_t t658;
            if (((((((uint8_t *)t657) + 145) + 1))[1] != 0)) goto mn660;
            t658 = ((uint8_t)1);
            goto me659;
mn660:;
            t658 = ((uint8_t)0);
            goto me659;
mn661:;
            gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 243, 65);
me659:;
            if (!t658) goto ie650;
            {
                gm_b16_8 t663 = gm_l51;
                gm_b16_8 t664;
                GM_ST(void *, (uint8_t *)&t664, (void *)(gm_skdv3lmatcupg));
                GM_ST(uintptr_t, (uint8_t *)&t664 + 8, (uintptr_t)(20));
                gm_b16_8 t665;
                GM_ST(void *, (uint8_t *)&t665, (void *)(gm_sapbk38ibps8q));
                GM_ST(uintptr_t, (uint8_t *)&t665 + 8, (uintptr_t)(27));
                gm_b16_8 t666 = gm_f_replacen1_7ksfmmn((void *)NULL, cx, NULL, t663, t664, t665);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l51 = t666;
            }
            goto ix650;
ie650:;
ix650:;
        }
        {
            uint32_t t668 = GM_LD(uint32_t, (((uint8_t *)&F.l26) + 344));
            uint32_t t669 = GM_LD(uint32_t, gm_cvpsnk4jni3);
            uint8_t t670 = (t668 > t669);
            if (!t670) goto ie667;
            {
                gm_b16_8 t672 = gm_l51;
                gm_b16_8 t673;
                GM_ST(void *, (uint8_t *)&t673, (void *)(gm_skdv3lmatcupg));
                GM_ST(uintptr_t, (uint8_t *)&t673 + 8, (uintptr_t)(20));
                gm_b16_8 t674;
                GM_ST(void *, (uint8_t *)&t674, (void *)(gm_soah6d9unb7uo));
                GM_ST(uintptr_t, (uint8_t *)&t674 + 8, (uintptr_t)(31));
                gm_b16_8 t675 = gm_f_replacen1_7ksfmmn((void *)NULL, cx, NULL, t672, t673, t674);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l51 = t675;
            }
            goto ix667;
ie667:;
ix667:;
        }
        {
            gm_b16_8 t676 = gm_l51;
            gm_l52 = t676;
        }
        {
            gm_b16_8 t678 = gm_l51;
            gm_b16_8 t679;
            GM_ST(void *, (uint8_t *)&t679, (void *)(gm_so514oj5nbhcu));
            GM_ST(uintptr_t, (uint8_t *)&t679 + 8, (uintptr_t)(1));
            gm_tf5srh4mp4b3i t680 = gm_f_find_nab29co(t678, t679);
            gm_tf5srh4mp4b3i t681 = t680;
            if (!((((uint8_t *)&t681))[8] != 0)) goto ie677;
            uint64_t t682 = GM_LD(uint64_t, ((uint8_t *)&t681));
            gm_l53 = t682;
            {
                uint64_t t684 = GM_LD(uintptr_t, ((uint8_t *)&gm_l51) + 8);
                void * t685 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l51)));
                uint64_t t686 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t687 = gm_l53;
                uint64_t t688 = (uintptr_t)t687;
                if (t686 > t688 || t688 > (uintptr_t)t684) gm_trap_slice(t686, t688, t684, gm_srcmok80ho8hh01, 252, 46);
                gm_b16_8 t689;
                GM_ST(void *, (uint8_t *)&t689, (void *)(((uint8_t *)t685 + t686 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t689 + 8, (uintptr_t)(t688 - t686));
                gm_l52 = t689;
            }
            goto ix677;
ie677:;
ix677:;
        }
        {
            uint32_t t691 = GM_LD(uint32_t, (((uint8_t *)&F.l26) + 344));
            uint32_t t692 = GM_LD(uint32_t, gm_ctq6t8frtor);
            uint8_t t693 = (t691 > t692);
            if (!t693) goto ie690;
            gm_b16_8 t694 = gm_l52;
            gm_b16_8 t695;
            GM_ST(void *, (uint8_t *)&t695, (void *)(gm_soa9mnl4ahjud));
            GM_ST(uintptr_t, (uint8_t *)&t695 + 8, (uintptr_t)(7));
            uint8_t t696 = gm_f_starts_with_6vvt01e(t694, t695);
            if (!t696) goto ie690;
            gm_b16_8 t697 = gm_l52;
            gm_b16_8 t698;
            GM_ST(void *, (uint8_t *)&t698, (void *)(gm_s65rai75evpi7));
            GM_ST(uintptr_t, (uint8_t *)&t698 + 8, (uintptr_t)(13));
            uint8_t t699 = gm_f_contains_gkki4kq(t697, t698);
            uint8_t t700 = (uint8_t)!t699;
            if (!t700) goto ie690;
            {
                gm_b16_8 t702 = gm_l51;
                gm_b16_8 t703;
                GM_ST(void *, (uint8_t *)&t703, (void *)(gm_soa9mnl4ahjud));
                GM_ST(uintptr_t, (uint8_t *)&t703 + 8, (uintptr_t)(7));
                gm_b16_8 t704;
                GM_ST(void *, (uint8_t *)&t704, (void *)(gm_smap40unho18s));
                GM_ST(uintptr_t, (uint8_t *)&t704 + 8, (uintptr_t)(18));
                gm_b16_8 t705 = gm_f_replacen1_7ksfmmn((void *)NULL, cx, NULL, t702, t703, t704);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l51 = t705;
            }
            goto ix690;
ie690:;
ix690:;
        }
        {
            void * t706 = gm_l0;
            void * t707 = (void *)(((uint8_t *)t706) + 1928);
            gm_b16_8 t708 = gm_l51;
            gm_tftb1odc739t5 t709 = gm_f_extend_0cs85gs(t707, t708);
            gm_tftb1odc739t5 t710 = t709;
            if (((((uint8_t *)&t710))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t710) + 1, gm_srcmok80ho8hh01, 256, 5);
            gm_b0_1 t711 = GM_LD(gm_b0_1, (((uint8_t *)&t710) + 1));
        }
    }
    gm_b0_1 t712;
    gm_tqgtd5gtgj65s t713;
    memset(&t713, 0, sizeof t713);
    ((uint8_t *)&t713)[0] = 0;
    GM_ST(gm_b0_1, ((uint8_t *)&t713 + 8), t712);
    gm_tqgtd5gtgj65s t714 = t713;
    F.ret = t714;
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
    GM_STACK_CHECK(&F, 2332u);
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
        void * t18 = t17;
        void * t19;
        if ((GM_LD(void *, ((uint8_t *)&t18)) != 0)) {
            void * t20 = GM_LD(void *, (((uint8_t *)&t18) + 0));
            t19 = t20;
        } else {
            void * t21 = gm_l3;
            gm_tl874t2atnlh3 t22 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t21) + 16));
            void * t23 = gm_l3;
            gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)t23) + 0));
            gm_tnpifv20irnph t25;
            GM_ST(gm_b16_8, ((uint8_t *)&t25 + 0), t24);
            gm_tabb710d9mq2e t26;
            gm_f_format_pu04c4a((void *)&t26, (void *)NULL, cx, NULL, t25);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t27 = t26;
            if (((((uint8_t *)&t27))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t27) + 8, gm_srcmok80ho8hh01, 54, 57);
            gm_b16_8 t28 = GM_LD(gm_b16_8, (((uint8_t *)&t27) + 8));
            gm_b16_8 t29 = t28;
            gm_b16_8 t30 = GM_LD(gm_b16_8, ((uint8_t *)&t29));
            gm_t08olq97rscn3 t31;
            gm_f_unsupported_531q51r((void *)&t31, (void *)NULL, cx, NULL, t22, t30);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tqgtd5gtgj65s t32;
            GM_ST(uint8_t, (uint8_t *)&t32 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t32 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t32 + 4, 0);
            ((uint8_t *)&t32)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t32 + 8), t31);
            gm_tqgtd5gtgj65s t33 = t32;
            uint8_t t34 = ((((uint8_t *)&t33))[0] != 0);
            F.ret = t32;
            goto gm_fret;
        }
        gm_l4 = t19;
    }
    {
        void * t36 = gm_l0;
        uint32_t t37 = gm_l1;
        uint8_t t38 = gm_f_frame_done_014nc4i((void *)NULL, cx, NULL, t36, t37);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t38) goto ie35;
        {
            void * t40 = gm_l0;
            uint32_t t41 = gm_l1;
            void * t42 = gm_l3;
            void * t43 = gm_l4;
            gm_t9805qtonbsag t44 = gm_l2;
            gm_f_gen_body_j6ku8qk((void *)&(*(gm_tqgtd5gtgj65s *)gm_out), (void *)NULL, cx, NULL, t40, t41, t42, t43, t44);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto ix35;
ie35:;
ix35:;
    }
    {
        void * t45 = gm_l0;
        uint32_t t46 = gm_l1;
        gm_b16_8 t47 = gm_f_frame_locals_mt4nb6o((void *)NULL, cx, NULL, t45, t46);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l5 = t47;
    }
    {
        gm_f_list_uvfu1k1((void *)&F.l6, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t48 = (void *)((uint8_t *)&F.l6);
        void * t49 = gm_l0;
        uint32_t t50 = gm_l1;
        gm_b16_8 t51 = gm_f_frame_name_stddmgg((void *)NULL, cx, NULL, t49, t50);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t52;
        GM_ST(gm_b16_8, ((uint8_t *)&t52 + 0), t51);
        gm_tftb1odc739t5 t53 = gm_f_write_j67vjku(t48, t52);
        gm_tftb1odc739t5 t54 = t53;
        if (((((uint8_t *)&t54))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t54) + 1, gm_srcmok80ho8hh01, 59, 5);
        gm_b0_1 t55 = GM_LD(gm_b0_1, (((uint8_t *)&t54) + 1));
    }
    {
        gm_b16_8 t56 = gm_f_empty_slice_ppl3r45();
        gm_b16_8 t57 = t56;
        gm_b16_8 t58 = GM_LD(gm_b16_8, ((uint8_t *)&t57));
        gm_l7 = t58;
    }
    {
        void * t60 = gm_l0;
        uint8_t t61 = GM_LD(uint8_t, ((((uint8_t *)t60) + 1136) + 2));
        if (!t61) goto ie59;
        {
            gm_f_list_epvducg((void *)&F.l8, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                void * t64 = gm_l3;
                gm_b16_8 t65 = GM_LD(gm_b16_8, (((uint8_t *)t64) + 48));
                gm_l9 = t65;
            }
            {
                gm_l10 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t67 = gm_l10;
                    uint64_t t68 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                    uint8_t t69 = (t67 < t68);
                    if (!t69) goto lb66;
                    {
                        uint64_t t72 = gm_l10;
                        uint64_t t73 = (uintptr_t)t72;
                        uint64_t t74 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                        gm_tsoie1ppgdvcg t77 = GM_LD(gm_tsoie1ppgdvcg, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t73 * 40));
                        F.l11 = t77;
                        {
                            void * t79 = (void *)((uint8_t *)&F.l8);
                            gm_b16_8 t80 = GM_LD(gm_b16_8, (((uint8_t *)&F.l11) + 0));
                            gm_tftb1odc739t5 t81 = gm_f_push_3ef746b(t79, t80);
                            gm_tftb1odc739t5 t82 = t81;
                            if (((((uint8_t *)&t82))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t82) + 1, gm_srcmok80ho8hh01, 64, 31);
                            gm_b0_1 t83 = GM_LD(gm_b0_1, (((uint8_t *)&t82) + 1));
                        }
                        goto me75;
mn76:;
                        gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 64, 9);
me75:;
                    }
lc66:;
                    {
                        uint64_t t85 = gm_l10;
                        uint64_t r86; bool o87 = __builtin_add_overflow(t85, ((uint64_t)1ULL), &r86);
                        uint64_t t88 = r86;
                        gm_l10 = t88;
                    }
                }
lb66:;
            }
        }
        {
            gm_f_hash_map_cuh4fs9((void *)&F.l12, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_b16_8 t89 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 0));
            gm_b16_8 t90 = t89;
            gm_b16_8 t91 = GM_LD(gm_b16_8, ((uint8_t *)&t90));
            void * t92 = (void *)((uint8_t *)&F.l12);
            gm_b16_8 t93 = gm_f_unique_members_mrojmjf((void *)NULL, cx, NULL, t91, t92);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t94 = t93;
            gm_b16_8 t95 = GM_LD(gm_b16_8, ((uint8_t *)&t94));
            gm_l7 = t95;
        }
        goto ix59;
ie59:;
ix59:;
    }
    {
        {
            void * t97 = gm_l3;
            gm_b16_8 t98 = GM_LD(gm_b16_8, (((uint8_t *)t97) + 48));
            gm_l13 = t98;
        }
        {
            gm_l14 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t100 = gm_l14;
                uint64_t t101 = GM_LD(uintptr_t, ((uint8_t *)&gm_l13) + 8);
                uint8_t t102 = (t100 < t101);
                if (!t102) goto lb99;
                {
                    gm_t5im2v50m3hk6 t105;
                    uint64_t t106 = gm_l14;
                    GM_ST(uint64_t, (uint8_t *)&t105 + 0, t106);
                    uint64_t t107 = gm_l14;
                    uint64_t t108 = (uintptr_t)t107;
                    uint64_t t109 = GM_LD(uintptr_t, ((uint8_t *)&gm_l13) + 8);
                    memcpy((uint8_t *)&t105 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l13)) + t108 * 40), 40);
                    gm_t5im2v50m3hk6 t110 = t105;
                    uint64_t t113 = GM_LD(uint64_t, (((uint8_t *)&t110) + 0));
                    gm_l15 = t113;
                    gm_tsoie1ppgdvcg t114 = GM_LD(gm_tsoie1ppgdvcg, (((uint8_t *)&t110) + 8));
                    F.l16 = t114;
                    {
                        uint64_t t117 = gm_l15;
                        uint64_t t118 = (uintptr_t)t117;
                        uint64_t t119 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                        if (t118 >= (uintptr_t)(t119)) gm_trap_index(t118, t119, gm_srcmok80ho8hh01, 69, 16);
                        uint8_t t120 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t118 * 1));
                        uint8_t t121 = (uint8_t)!t120;
                        if (!t121) goto ie116;
                        {
                            goto lc99;
                        }
                        goto ix116;
ie116:;
ix116:;
                    }
                    {
                        void * t123 = gm_l0;
                        void * t124 = (void *)(((uint8_t *)t123) + 8);
                        gm_t88rcucg1b7uk t125 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l16) + 16));
                        gm_b16_8 t126 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t124, t125);
                        if (gm_unw.kind) {
                            goto gm_ll103;
                        }
                        gm_l17 = t126;
                    }
                    {
                        uint64_t t128 = gm_l15;
                        uint64_t t129 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                        uint8_t t130 = (t128 < t129);
                        if (!t130) goto ie127;
                        uint64_t t131 = gm_l15;
                        uint64_t t132 = (uintptr_t)t131;
                        uint64_t t133 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                        if (t132 >= (uintptr_t)(t133)) gm_trap_index(t132, t133, gm_srcmok80ho8hh01, 71, 30);
                        uint8_t t134;
                        if (((((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t132 * 24))[16] != 0)) goto mn136;
                        t134 = ((uint8_t)0);
                        goto me135;
mn136:;
                        t134 = ((uint8_t)1);
                        goto me135;
mn137:;
                        gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 71, 30);
me135:;
                        if (!t134) goto ie127;
                        {
                            uint64_t t139 = gm_l15;
                            uint64_t t140 = (uintptr_t)t139;
                            uint64_t t141 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                            if (t140 >= (uintptr_t)(t141)) gm_trap_index(t140, t141, gm_srcmok80ho8hh01, 72, 18);
                            gm_th2lqbo77pvsc t142 = GM_LD(gm_th2lqbo77pvsc, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t140 * 24));
                            gm_th2lqbo77pvsc t143 = t142;
                            if (!((((uint8_t *)&t143))[16] != 0)) gm_trap("unwrap of null", gm_srcmok80ho8hh01, 72, 18);
                            gm_b16_8 t144 = GM_LD(gm_b16_8, ((uint8_t *)&t143));
                            gm_l18 = t144;
                        }
                        {
                            gm_b16_8 t145 = gm_l17;
                            gm_l19 = t145;
                        }
                        {
                            gm_b16_8 t146;
                            GM_ST(void *, (uint8_t *)&t146, (void *)(gm_sr4qbrk6tlgaa));
                            GM_ST(uintptr_t, (uint8_t *)&t146 + 8, (uintptr_t)(0));
                            gm_l20 = t146;
                        }
                        {
                            void * t148 = gm_l0;
                            void * t149 = (void *)(((uint8_t *)t148) + 8);
                            gm_t88rcucg1b7uk t150 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l16) + 16));
                            gm_ttu09lij36f8c t151;
                            gm_f_debug_view_gp38euq((void *)&t151, (void *)NULL, cx, NULL, t149, t150);
                            if (gm_unw.kind) {
                                goto gm_ll103;
                            }
                            gm_ttu09lij36f8c t152 = t151;
                            if (!((((uint8_t *)&t152))[32] != 0)) goto ie147;
                            gm_t6j6smj7qoctu t153 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)&t152));
                            gm_l21 = t153;
                            {
                                gm_b16_8 t155 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l21) + 0));
                                gm_l19 = t155;
                            }
                            {
                                gm_b16_8 t156 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l21) + 16));
                                gm_l20 = t156;
                            }
                            goto ix147;
ie147:;
ix147:;
                        }
                        {
                            void * t157 = gm_l0;
                            void * t158 = (void *)(((uint8_t *)t157) + 8);
                            gm_b16_8 t159 = gm_l18;
                            gm_b0_1 t160 = gm_f_add_member_name_j3i0eje((void *)NULL, cx, NULL, t158, t159);
                            if (gm_unw.kind) {
                                goto gm_ll103;
                            }
                        }
                        {
                            void * t161 = (void *)((uint8_t *)&F.l6);
                            gm_tcrdb1inj22os t162;
                            gm_b16_8 t163 = gm_l17;
                            GM_ST(gm_b16_8, (uint8_t *)&t162 + 0, t163);
                            uint64_t t164 = gm_l15;
                            GM_ST(uint64_t, (uint8_t *)&t162 + 16, t164);
                            gm_b16_8 t165 = gm_l19;
                            GM_ST(gm_b16_8, (uint8_t *)&t162 + 24, t165);
                            gm_b16_8 t166 = gm_l18;
                            GM_ST(gm_b16_8, (uint8_t *)&t162 + 40, t166);
                            gm_b16_8 t167 = gm_l20;
                            GM_ST(gm_b16_8, (uint8_t *)&t162 + 56, t167);
                            gm_tftb1odc739t5 t168 = gm_f_write_s858s21(t161, &t162);
                            gm_tftb1odc739t5 t169 = t168;
                            if (((((uint8_t *)&t169))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t169) + 1, gm_srcmok80ho8hh01, 80, 13);
                            gm_b0_1 t170 = GM_LD(gm_b0_1, (((uint8_t *)&t169) + 1));
                        }
                        goto ix127;
ie127:;
                        {
                            void * t172 = (void *)((uint8_t *)&F.l6);
                            gm_tq3m5qgvkpje8 t173;
                            gm_b16_8 t174 = gm_l17;
                            GM_ST(gm_b16_8, (uint8_t *)&t173 + 0, t174);
                            uint64_t t175 = gm_l15;
                            GM_ST(uint64_t, (uint8_t *)&t173 + 16, t175);
                            gm_b16_8 t176 = GM_LD(gm_b16_8, (((uint8_t *)&F.l16) + 0));
                            GM_ST(gm_b16_8, (uint8_t *)&t173 + 24, t176);
                            gm_tftb1odc739t5 t177 = gm_f_write_s0f8vaq(t172, &t173);
                            gm_tftb1odc739t5 t178 = t177;
                            if (((((uint8_t *)&t178))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t178) + 1, gm_srcmok80ho8hh01, 82, 13);
                            gm_b0_1 t179 = GM_LD(gm_b0_1, (((uint8_t *)&t178) + 1));
                        }
ix127:;
                    }
                    goto me111;
mn112:;
                    gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 68, 5);
me111:;
                }
lc99:;
                {
                    uint64_t t181 = gm_l14;
                    uint64_t r182; bool o183 = __builtin_add_overflow(t181, ((uint64_t)1ULL), &r182);
                    uint64_t t184 = r182;
                    gm_l14 = t184;
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
        void * t185 = (void *)((uint8_t *)&F.l6);
        void * t186 = gm_l0;
        void * t187 = (void *)(((uint8_t *)t186) + 8);
        void * t188 = gm_l3;
        gm_t88rcucg1b7uk t189 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t188) + 64));
        gm_b16_8 t190 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t187, t189);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t191;
        GM_ST(gm_b16_8, ((uint8_t *)&t191 + 0), t190);
        gm_tftb1odc739t5 t192 = gm_f_write_shh24f1(t185, t191);
        gm_tftb1odc739t5 t193 = t192;
        if (((((uint8_t *)&t193))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t193) + 1, gm_srcmok80ho8hh01, 85, 5);
        gm_b0_1 t194 = GM_LD(gm_b0_1, (((uint8_t *)&t193) + 1));
    }
    {
        gm_f_list_9ljroit((void *)&F.l22, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t195 = gm_l4;
        void * t196 = (void *)((uint8_t *)&F.l22);
        gm_b0_1 t197 = gm_f_collect_withs_fa29mk1((void *)NULL, cx, NULL, t195, t196);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t199 = GM_LD(gm_b16_8, (((uint8_t *)&F.l22) + 0));
            gm_l23 = t199;
        }
        {
            gm_l24 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t201 = gm_l24;
                uint64_t t202 = GM_LD(uintptr_t, ((uint8_t *)&gm_l23) + 8);
                uint8_t t203 = (t201 < t202);
                if (!t203) goto lb200;
                {
                    uint64_t t206 = gm_l24;
                    uint64_t t207 = (uintptr_t)t206;
                    uint64_t t208 = GM_LD(uintptr_t, ((uint8_t *)&gm_l23) + 8);
                    gm_tplpbqf3duvn3 t211 = GM_LD(gm_tplpbqf3duvn3, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l23)) + t207 * 8));
                    gm_l25 = t211;
                    {
                        void * t213 = (void *)((uint8_t *)&F.l6);
                        gm_tommf4j9upomc t214;
                        memset((uint8_t *)&t214 + 20, 0, 4);
                        void * t215 = gm_l0;
                        void * t216 = (void *)(((uint8_t *)t215) + 8);
                        gm_t88rcucg1b7uk t217 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l25) + 4));
                        gm_b16_8 t218 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t216, t217);
                        if (gm_unw.kind) {
                            goto gm_ll204;
                        }
                        GM_ST(gm_b16_8, (uint8_t *)&t214 + 0, t218);
                        uint32_t t219 = GM_LD(uint32_t, (((uint8_t *)&gm_l25) + 0));
                        GM_ST(uint32_t, (uint8_t *)&t214 + 16, t219);
                        gm_tftb1odc739t5 t220 = gm_f_write_ihqa4kk(t213, &t214);
                        gm_tftb1odc739t5 t221 = t220;
                        if (((((uint8_t *)&t221))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t221) + 1, gm_srcmok80ho8hh01, 89, 9);
                        gm_b0_1 t222 = GM_LD(gm_b0_1, (((uint8_t *)&t221) + 1));
                    }
                    goto me209;
mn210:;
                    gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 88, 5);
me209:;
                }
lc200:;
                {
                    uint64_t t224 = gm_l24;
                    uint64_t r225; bool o226 = __builtin_add_overflow(t224, ((uint64_t)1ULL), &r225);
                    uint64_t t227 = r225;
                    gm_l24 = t227;
                }
            }
gm_ll204:;
            if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb200; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc200; }
            }
            goto gm_lad;
lb200:;
        }
    }
    {
        void * t228 = (void *)((uint8_t *)&F.l6);
        gm_b16_8 t229;
        GM_ST(void *, (uint8_t *)&t229, (void *)(gm_sg398rgmck67b));
        GM_ST(uintptr_t, (uint8_t *)&t229 + 8, (uintptr_t)(3));
        gm_tftb1odc739t5 t230 = gm_f_extend_0cs85gs(t228, t229);
        gm_tftb1odc739t5 t231 = t230;
        if (((((uint8_t *)&t231))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t231) + 1, gm_srcmok80ho8hh01, 91, 5);
        gm_b0_1 t232 = GM_LD(gm_b0_1, (((uint8_t *)&t231) + 1));
    }
    {
        void * t233 = gm_l0;
        void * t234 = (void *)(((uint8_t *)t233) + 1848);
        gm_b16_8 t235 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
        gm_b16_8 t236 = t235;
        gm_b16_8 t237 = GM_LD(gm_b16_8, ((uint8_t *)&t236));
        gm_tftb1odc739t5 t238 = gm_f_extend_0cs85gs(t234, t237);
        gm_tftb1odc739t5 t239 = t238;
        if (((((uint8_t *)&t239))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t239) + 1, gm_srcmok80ho8hh01, 92, 5);
        gm_b0_1 t240 = GM_LD(gm_b0_1, (((uint8_t *)&t239) + 1));
    }
    {
        void * t241 = gm_l0;
        uint32_t t242 = gm_l1;
        void * t243 = gm_l3;
        void * t244 = gm_l4;
        gm_t9805qtonbsag t245 = gm_l2;
        gm_f_gen_body_j6ku8qk((void *)&(*(gm_tqgtd5gtgj65s *)gm_out), (void *)NULL, cx, NULL, t241, t242, t243, t244, t245);
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
    GM_STACK_CHECK(&F, 111u);
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
            void * t9 = t8;
            uint8_t t10;
            if ((GM_LD(void *, ((uint8_t *)&t9)) != 0)) goto mn12;
            t10 = ((uint8_t)0);
            goto me11;
mn12:;
            t10 = ((uint8_t)1);
            goto me11;
mn13:;
            gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 1196, 12);
me11:;
            if (!t10) goto ie7;
            {
                uint8_t t15 = 0;
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
        void * t16 = gm_l0;
        gm_closure t17 = ((gm_closure){GM_PTR_TAG(gm_f_fn_aa55llv), NULL, cx});
        uint8_t t18 = gm_f_any_child_ffc8h1k(t16, t17);
        if (gm_unw.kind) {
            goto gm_lad;
        }
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_hint_attrs_mktdhi1(void *env, gm_node *cx, gm_node *inst, gm_t1h8kp9hsu3ua a0) {
    struct gm_fr_hint_attrs_mktdhi1 F;
    gm_t1h8kp9hsu3ua gm_l0; /* h */
    uint8_t gm_l2; /* x */
    GM_STACK_CHECK(&F, 202u);
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
                gm_tftb1odc739t5 t11 = t10;
                if (((((uint8_t *)&t11))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t11) + 1, gm_srcmok80ho8hh01, 392, 16);
                gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t11) + 1));
            }
            goto ix5;
ie5:;
            {
                void * t14 = (void *)((uint8_t *)&F.l1);
                gm_b16_8 t15;
                GM_ST(void *, (uint8_t *)&t15, (void *)(gm_s39jha51ulivf));
                GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)(26));
                gm_tftb1odc739t5 t16 = gm_f_extend_0cs85gs(t14, t15);
                gm_tftb1odc739t5 t17 = t16;
                if (((((uint8_t *)&t17))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t17) + 1, gm_srcmok80ho8hh01, 392, 77);
                gm_b0_1 t18 = GM_LD(gm_b0_1, (((uint8_t *)&t17) + 1));
            }
ix5:;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 3));
        if (!t20) goto ie19;
        {
            void * t22 = (void *)((uint8_t *)&F.l1);
            gm_b16_8 t23;
            GM_ST(void *, (uint8_t *)&t23, (void *)(gm_s3cu59itn73k7));
            GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(22));
            gm_tftb1odc739t5 t24 = gm_f_extend_0cs85gs(t22, t23);
            gm_tftb1odc739t5 t25 = t24;
            if (((((uint8_t *)&t25))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t25) + 1, gm_srcmok80ho8hh01, 394, 17);
            gm_b0_1 t26 = GM_LD(gm_b0_1, (((uint8_t *)&t25) + 1));
        }
        goto ix19;
ie19:;
ix19:;
    }
    {
        uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
        if (!t28) goto ie27;
        {
            void * t30 = (void *)((uint8_t *)&F.l1);
            gm_b16_8 t31;
            GM_ST(void *, (uint8_t *)&t31, (void *)(gm_slnuijmj5p1vk));
            GM_ST(uintptr_t, (uint8_t *)&t31 + 8, (uintptr_t)(18));
            gm_tftb1odc739t5 t32 = gm_f_extend_0cs85gs(t30, t31);
            gm_tftb1odc739t5 t33 = t32;
            if (((((uint8_t *)&t33))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t33) + 1, gm_srcmok80ho8hh01, 395, 22);
            gm_b0_1 t34 = GM_LD(gm_b0_1, (((uint8_t *)&t33) + 1));
        }
        goto ix27;
ie27:;
ix27:;
    }
    {
        gm_b16_8 t35 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
        gm_b16_8 t36 = t35;
        gm_b16_8 t37 = GM_LD(gm_b16_8, ((uint8_t *)&t36));
        uint8_t t38 = 0;
        F.ret = t37;
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
    GM_STACK_CHECK(&F, 635u);
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
                if (o11) gm_trap("integer overflow in `-`", gm_srcmok80ho8hh01, 612, 9);
                uint64_t t12 = r10;
                gm_l1 = t12;
            }
            {
                uint64_t t13 = gm_l1;
                uint64_t t14 = (uintptr_t)t13;
                void * t15 = gm_l0;
                uint64_t t16 = GM_LD(uintptr_t, ((((uint8_t *)t15) + 240) + 0) + 8);
                if (t14 >= (uintptr_t)(t16)) gm_trap_index(t14, t16, gm_srcmok80ho8hh01, 613, 15);
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
                    gm_tabb710d9mq2e t46 = t45;
                    if (((((uint8_t *)&t46))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t46) + 8, gm_srcmok80ho8hh01, 619, 25);
                    gm_b16_8 t47 = GM_LD(gm_b16_8, (((uint8_t *)&t46) + 8));
                    gm_b16_8 t48 = t47;
                    gm_b16_8 t49 = GM_LD(gm_b16_8, ((uint8_t *)&t48));
                    gm_tf5srh4mp4b3i t50 = gm_l3;
                    GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tj64ui6kruni6 *)gm_out) + 0, t49);
                    GM_ST(gm_tf5srh4mp4b3i, (uint8_t *)&(*(gm_tj64ui6kruni6 *)gm_out) + 16, t50);
                    goto gm_fdone;
                }
                goto me21;
mn22:;
                uint8_t t54 = GM_LD(uint8_t, ((((uint8_t *)t20) + 0) + 0));
                if (t54 != ((uint8_t)1ULL)) goto pa53;
                goto po52;
pa53:;
                uint8_t t56 = GM_LD(uint8_t, ((((uint8_t *)t20) + 0) + 0));
                if (t56 != ((uint8_t)2ULL)) goto pa55;
                goto po52;
pa55:;
                goto mn51;
po52:;
                {
                    void * t58 = gm_l2;
                    GM_ST(uint64_t, (((uint8_t *)t58) + 96), ((uint64_t)1ULL));
                }
                {
                    void * t59 = gm_l2;
                    uint32_t t60 = GM_LD(uint32_t, (((uint8_t *)t59) + 48));
                    gm_t88rcucg1b7uk t61;
                    GM_ST(uint32_t, ((uint8_t *)&t61 + 0), t60);
                    gm_tabb710d9mq2e t62;
                    gm_f_format_dd6d4bt((void *)&t62, (void *)NULL, cx, NULL, t61);
                    if (gm_unw.kind) {
                        goto gm_ll7;
                    }
                    gm_tabb710d9mq2e t63 = t62;
                    if (((((uint8_t *)&t63))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t63) + 8, gm_srcmok80ho8hh01, 623, 25);
                    gm_b16_8 t64 = GM_LD(gm_b16_8, (((uint8_t *)&t63) + 8));
                    gm_b16_8 t65 = t64;
                    gm_b16_8 t66 = GM_LD(gm_b16_8, ((uint8_t *)&t65));
                    gm_tf5srh4mp4b3i t67 = gm_l3;
                    GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tj64ui6kruni6 *)gm_out) + 0, t66);
                    GM_ST(gm_tf5srh4mp4b3i, (uint8_t *)&(*(gm_tj64ui6kruni6 *)gm_out) + 16, t67);
                    goto gm_fdone;
                }
                goto me21;
mn51:;
                gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 615, 9);
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
        gm_tf5srh4mp4b3i t68;
        memset(&t68, 0, sizeof t68);
        gm_l4 = t68;
    }
    {
        void * t70 = gm_l0;
        uint8_t t71 = GM_LD(uint8_t, (((uint8_t *)t70) + 298));
        if (!t71) goto ie69;
        {
            gm_b16_8 t73;
            GM_ST(void *, (uint8_t *)&t73, (void *)(gm_sjibpf3glbc54));
            GM_ST(uintptr_t, (uint8_t *)&t73 + 8, (uintptr_t)(8));
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tj64ui6kruni6 *)gm_out) + 0, t73);
            gm_tf5srh4mp4b3i t74 = gm_l4;
            GM_ST(gm_tf5srh4mp4b3i, (uint8_t *)&(*(gm_tj64ui6kruni6 *)gm_out) + 16, t74);
            goto gm_fdone;
        }
        goto ix69;
ie69:;
ix69:;
    }
    {
        void * t75 = gm_l0;
        GM_ST(uint8_t, (((uint8_t *)t75) + 297), ((uint8_t)1));
    }
    {
        gm_b16_8 t76;
        GM_ST(void *, (uint8_t *)&t76, (void *)(gm_s2hi6cc9n75l5));
        GM_ST(uintptr_t, (uint8_t *)&t76 + 8, (uintptr_t)(6));
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tj64ui6kruni6 *)gm_out) + 0, t76);
        gm_tf5srh4mp4b3i t77 = gm_l4;
        GM_ST(gm_tf5srh4mp4b3i, (uint8_t *)&(*(gm_tj64ui6kruni6 *)gm_out) + 16, t77);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_line_l6u87p9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_line_l6u87p9 F;
    void * gm_l0; /* fg */
    gm_b16_8 gm_l1; /* s */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 197u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        {
            gm_l3 = ((uint64_t)0ULL);
        }
        {
            void * t3 = gm_l0;
            uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t3) + 224));
            gm_l2 = t4;
        }
        {
            for (;;) {
                uint64_t t6 = gm_l3;
                uint64_t t7 = gm_l2;
                uint8_t t8 = (t6 < t7);
                if (!t8) goto lb5;
                {
                    uint64_t t11 = gm_l3;
                    uint64_t t12 = t11;
                    {
                        void * t16 = gm_l0;
                        void * t17 = (void *)(((uint8_t *)t16) + 184);
                        gm_b16_8 t18;
                        GM_ST(void *, (uint8_t *)&t18, (void *)(gm_som0q53jqvjh3));
                        GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(4));
                        gm_tftb1odc739t5 t19 = gm_f_extend_0cs85gs(t17, t18);
                        gm_tftb1odc739t5 t20 = t19;
                        if (((((uint8_t *)&t20))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t20) + 1, gm_srcmok80ho8hh01, 435, 29);
                        gm_b0_1 t21 = GM_LD(gm_b0_1, (((uint8_t *)&t20) + 1));
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 435, 5);
me13:;
                }
lc5:;
                {
                    uint64_t t23 = gm_l3;
                    uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
                    uint64_t t26 = r24;
                    gm_l3 = t26;
                }
            }
lb5:;
        }
    }
    {
        void * t27 = gm_l0;
        void * t28 = (void *)(((uint8_t *)t27) + 184);
        gm_b16_8 t29 = gm_l1;
        gm_tftb1odc739t5 t30 = gm_f_extend_0cs85gs(t28, t29);
        gm_tftb1odc739t5 t31 = t30;
        if (((((uint8_t *)&t31))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t31) + 1, gm_srcmok80ho8hh01, 436, 5);
        gm_b0_1 t32 = GM_LD(gm_b0_1, (((uint8_t *)&t31) + 1));
    }
    {
        void * t33 = gm_l0;
        void * t34 = (void *)(((uint8_t *)t33) + 184);
        gm_tftb1odc739t5 t35 = gm_f_push_t83rg2m(t34, ((uint8_t)10ULL));
        gm_tftb1odc739t5 t36 = t35;
        if (((((uint8_t *)&t36))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t36) + 1, gm_srcmok80ho8hh01, 437, 5);
        gm_b0_1 t37 = GM_LD(gm_b0_1, (((uint8_t *)&t36) + 1));
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_local_addr_g9lkr6l(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_local_addr_g9lkr6l F;
    void * gm_l0; /* fg */
    uint32_t gm_l1; /* l */
    uint64_t gm_l2; /* i */
    GM_STACK_CHECK(&F, 391u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        uint32_t t4 = gm_l1;
        gm_tf5srh4mp4b3i t5 = gm_f_param_index_rp1nn6k(t3, t4);
        gm_tf5srh4mp4b3i t6 = t5;
        if (!((((uint8_t *)&t6))[8] != 0)) goto ie2;
        uint64_t t7 = GM_LD(uint64_t, ((uint8_t *)&t6));
        gm_l2 = t7;
        {
            uint64_t t10 = gm_l2;
            uint64_t t11 = (uintptr_t)t10;
            void * t12 = gm_l0;
            uint64_t t13 = GM_LD(uintptr_t, (((uint8_t *)t12) + 312) + 8);
            if (t11 >= (uintptr_t)(t13)) gm_trap_index(t11, t13, gm_srcmok80ho8hh01, 524, 12);
            uint8_t t14 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t12) + 312)) + t11 * 1));
            if (!t14) goto ie9;
            {
                uint64_t t16 = gm_l2;
                gm_t4ce0jd3tdro4 t17;
                GM_ST(uint64_t, ((uint8_t *)&t17 + 0), t16);
                gm_tabb710d9mq2e t18;
                gm_f_format_9pumrco((void *)&t18, (void *)NULL, cx, NULL, t17);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t19 = t18;
                if (((((uint8_t *)&t19))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t19) + 8, gm_srcmok80ho8hh01, 524, 36);
                gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&t19) + 8));
                gm_b16_8 t21 = t20;
                gm_b16_8 t22 = GM_LD(gm_b16_8, ((uint8_t *)&t21));
                uint8_t t23 = 0;
                F.ret = t22;
                goto gm_fret;
            }
            goto ix9;
ie9:;
ix9:;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t24 = gm_l0;
        uint32_t t25 = gm_l1;
        gm_b16_8 t26 = gm_f_local_lv_m2jtf0n((void *)NULL, cx, NULL, t24, t25);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t27;
        GM_ST(gm_b16_8, ((uint8_t *)&t27 + 0), t26);
        gm_tabb710d9mq2e t28;
        gm_f_format_4un19t8((void *)&t28, (void *)NULL, cx, NULL, t27);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t29 = t28;
        if (((((uint8_t *)&t29))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t29) + 8, gm_srcmok80ho8hh01, 526, 12);
        gm_b16_8 t30 = GM_LD(gm_b16_8, (((uint8_t *)&t29) + 8));
        gm_b16_8 t31 = t30;
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
    GM_STACK_CHECK(&F, 1535u);
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
        gm_t0am46iohlpol t11 = t10;
        uint8_t t12 = 0;
        gm_t9qkh0rsooc8d t13 = t7;
        if (gm_eqgnrg8h3j6k(((uint8_t *)&t11) + 0, ((uint8_t *)&t13))) t12 = 1;
        if (gm_eqgnrg8h3j6k(((uint8_t *)&t11) + 1, ((uint8_t *)&t13))) t12 = 1;
        uint8_t t14 = (uint8_t)(t12);
        if (!t14) goto ie6;
        {
            gm_tf5srh4mp4b3i t16;
            memset(&t16, 0, sizeof t16);
            gm_l5 = t16;
        }
        {
            void * t17 = gm_l0;
            uint64_t t18 = GM_LD(uintptr_t, ((((uint8_t *)t17) + 240) + 0) + 8);
            gm_l6 = t18;
        }
        {
            for (;;) {
                uint64_t t20 = gm_l6;
                uint8_t t21 = (t20 > ((uint64_t)0ULL));
                if (!t21) goto lb19;
                {
                    uint64_t t24 = gm_l6;
                    uint64_t r25; bool o26 = __builtin_sub_overflow(t24, ((uint64_t)1ULL), &r25);
                    if (o26) gm_trap("integer overflow in `-`", gm_srcmok80ho8hh01, 744, 13);
                    uint64_t t27 = r25;
                    gm_l6 = t27;
                }
                {
                    uint64_t t28 = gm_l6;
                    uint64_t t29 = (uintptr_t)t28;
                    void * t30 = gm_l0;
                    uint64_t t31 = GM_LD(uintptr_t, ((((uint8_t *)t30) + 240) + 0) + 8);
                    if (t29 >= (uintptr_t)(t31)) gm_trap_index(t29, t31, gm_srcmok80ho8hh01, 745, 18);
                    gm_tehgl6lkqgvt0 t32 = GM_LD(gm_tehgl6lkqgvt0, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t30) + 240) + 0)) + t29 * 104));
                    F.l7 = t32;
                }
                {
                    gm_l8 = ((uint8_t)0);
                }
                {
                    uint8_t t38 = GM_LD(uint8_t, ((((uint8_t *)&F.l7) + 0) + 0));
                    switch (t38) { case ((uint8_t)0ULL): goto ma36; case ((uint8_t)1ULL): goto ma34; case ((uint8_t)2ULL): goto ma35; default: goto ma37; }
ma34:;
                    uint8_t t40 = GM_LD(uint8_t, ((((uint8_t *)&F.l7) + 0) + 0));
                    if (t40 != ((uint8_t)1ULL)) goto mn39;
                    gm_thognv9odov03 t41 = GM_LD(gm_thognv9odov03, ((((uint8_t *)&F.l7) + 0) + 8));
                    F.l9 = t41;
                    {
                        uint32_t t43 = GM_LD(uint32_t, (((uint8_t *)&F.l9) + 0));
                        gm_t88rcucg1b7uk t44;
                        GM_ST(uint32_t, ((uint8_t *)&t44 + 0), t43);
                        gm_tabb710d9mq2e t45;
                        gm_f_format_32n178h((void *)&t45, (void *)NULL, cx, NULL, t44);
                        if (gm_unw.kind) {
                            goto gm_ll22;
                        }
                        gm_tabb710d9mq2e t46 = t45;
                        if (((((uint8_t *)&t46))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t46) + 8, gm_srcmok80ho8hh01, 748, 37);
                        gm_b16_8 t47 = GM_LD(gm_b16_8, (((uint8_t *)&t46) + 8));
                        gm_b16_8 t48 = t47;
                        gm_b16_8 t49 = GM_LD(gm_b16_8, ((uint8_t *)&t48));
                        gm_b16_8 t50 = gm_l2;
                        gm_b16_8 t51 = t49;
                        gm_b16_8 t52 = t50;
                        uint8_t t53 = (gm_eq0iqiubfm7e(((uint8_t *)&t51), ((uint8_t *)&t52)));
                        gm_l8 = t53;
                    }
                    goto me33;
mn39:;
ma35:;
                    uint8_t t55 = GM_LD(uint8_t, ((((uint8_t *)&F.l7) + 0) + 0));
                    if (t55 != ((uint8_t)2ULL)) goto mn54;
                    gm_tiilmfd16v3fc t56 = GM_LD(gm_tiilmfd16v3fc, ((((uint8_t *)&F.l7) + 0) + 8));
                    gm_l10 = t56;
                    {
                        uint8_t t58 = 0;
                        uint8_t t59 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l1) + 0) == ((uint8_t)1ULL)));
                        if (t59) {
                            uint32_t t60 = GM_LD(uint32_t, (((uint8_t *)&gm_l10) + 0));
                            gm_t88rcucg1b7uk t61;
                            GM_ST(uint32_t, ((uint8_t *)&t61 + 0), t60);
                            gm_tabb710d9mq2e t62;
                            gm_f_format_32n178h((void *)&t62, (void *)NULL, cx, NULL, t61);
                            if (gm_unw.kind) {
                                goto gm_ll22;
                            }
                            gm_tabb710d9mq2e t63 = t62;
                            if (((((uint8_t *)&t63))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t63) + 8, gm_srcmok80ho8hh01, 749, 56);
                            gm_b16_8 t64 = GM_LD(gm_b16_8, (((uint8_t *)&t63) + 8));
                            gm_b16_8 t65 = t64;
                            gm_b16_8 t66 = GM_LD(gm_b16_8, ((uint8_t *)&t65));
                            gm_b16_8 t67 = gm_l2;
                            gm_b16_8 t68 = t66;
                            gm_b16_8 t69 = t67;
                            uint8_t t70 = (gm_eq0iqiubfm7e(((uint8_t *)&t68), ((uint8_t *)&t69)));
                            t58 = t70;
                        } else t58 = 0;
                        gm_l8 = t58;
                    }
                    goto me33;
mn54:;
ma36:;
                    uint8_t t72 = GM_LD(uint8_t, ((((uint8_t *)&F.l7) + 0) + 0));
                    if (t72 != ((uint8_t)0ULL)) goto mn71;
                    goto me33;
mn71:;
ma37:;
                    gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 747, 13);
me33:;
                }
                {
                    uint8_t t75 = gm_l8;
                    if (!t75) goto ie74;
                    {
                        uint64_t t77 = gm_l6;
                        gm_tf5srh4mp4b3i t78;
                        GM_ST(uint8_t, (uint8_t *)&t78 + 9, 0);
                        GM_ST(uint16_t, (uint8_t *)&t78 + 10, 0);
                        GM_ST(uint32_t, (uint8_t *)&t78 + 12, 0);
                        GM_ST(uint64_t, ((uint8_t *)&t78), t77);
                        ((uint8_t *)&t78)[8] = 1;
                        gm_l5 = t78;
                    }
                    {
                        goto lb19;
                    }
                    goto ix74;
ie74:;
ix74:;
                }
lc19:;
            }
gm_ll22:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb19; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc19; }
            }
            goto gm_lad;
lb19:;
        }
        {
            uint8_t t80;
            if (((((uint8_t *)&gm_l5))[8] != 0)) goto mn82;
            t80 = ((uint8_t)1);
            goto me81;
mn82:;
            t80 = ((uint8_t)0);
            goto me81;
mn83:;
            gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 757, 12);
me81:;
            if (!t80) goto ie79;
            {
                uint8_t t85 = 0;
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            goto ix79;
ie79:;
ix79:;
        }
        {
            gm_tf5srh4mp4b3i t86 = gm_l5;
            gm_l4 = t86;
        }
        goto ix6;
ie6:;
        {
            uint8_t t88 = 0;
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
        if (!((((uint8_t *)&gm_l4))[8] != 0)) goto ie89;
        uint64_t t90 = GM_LD(uint64_t, ((uint8_t *)&gm_l4));
        gm_l12 = t90;
        {
            uint64_t t92 = gm_l12;
            uint64_t r93; bool o94 = __builtin_add_overflow(t92, ((uint64_t)1ULL), &r93);
            if (o94) gm_trap("integer overflow in `+`", gm_srcmok80ho8hh01, 763, 30);
            uint64_t t95 = r93;
            gm_l11 = t95;
        }
        goto ix89;
ie89:;
ix89:;
    }
    {
        uint8_t t96 = 0;
        uint8_t t97 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l1) + 0) == ((uint8_t)0ULL)));
        if (t97) {
            uint8_t t98 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8) == 1) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l3)), gm_sicgmpca63jln, 1) == 0));
            uint8_t t99 = (uint8_t)!t98;
            t96 = t99;
        } else t96 = 0;
        gm_l13 = t96;
    }
    {
        {
            uint64_t t101 = gm_l11;
            gm_l15 = t101;
        }
        {
            void * t102 = gm_l0;
            uint64_t t103 = GM_LD(uintptr_t, ((((uint8_t *)t102) + 240) + 0) + 8);
            gm_l14 = t103;
        }
        {
            for (;;) {
                uint64_t t105 = gm_l15;
                uint64_t t106 = gm_l14;
                uint8_t t107 = (t105 < t106);
                if (!t107) goto lb104;
                {
                    uint64_t t110 = gm_l15;
                    uint64_t t111 = t110;
                    uint64_t t114 = GM_LD(uint64_t, ((uint8_t *)&t111));
                    gm_l16 = t114;
                    {
                        uint64_t t116 = gm_l16;
                        uint64_t t117 = (uintptr_t)t116;
                        void * t118 = gm_l0;
                        uint64_t t119 = GM_LD(uintptr_t, ((((uint8_t *)t118) + 240) + 0) + 8);
                        if (t117 >= (uintptr_t)(t119)) gm_trap_index(t117, t119, gm_srcmok80ho8hh01, 769, 14);
                        gm_tehgl6lkqgvt0 t120 = GM_LD(gm_tehgl6lkqgvt0, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t118) + 240) + 0)) + t117 * 104));
                        F.l17 = t120;
                    }
                    {
                        uint8_t t122 = ((GM_LD(uint8_t, (((uint8_t *)&F.l17) + 0) + 0) == ((uint8_t)0ULL)));
                        if (!t122) goto ie121;
                        {
                            {
                                gm_b16_8 t125 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l17) + 56) + 0));
                                gm_l18 = t125;
                            }
                            {
                                gm_l19 = ((uint64_t)0ULL);
                            }
                            {
                                for (;;) {
                                    uint64_t t127 = gm_l19;
                                    uint64_t t128 = GM_LD(uintptr_t, ((uint8_t *)&gm_l18) + 8);
                                    uint8_t t129 = (t127 < t128);
                                    if (!t129) goto lb126;
                                    {
                                        uint64_t t132 = gm_l19;
                                        uint64_t t133 = (uintptr_t)t132;
                                        uint64_t t134 = GM_LD(uintptr_t, ((uint8_t *)&gm_l18) + 8);
                                        gm_tgtub0v94d5u5 t137 = GM_LD(gm_tgtub0v94d5u5, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l18)) + t133 * 16));
                                        gm_l20 = t137;
                                        {
                                            uint8_t t140 = 0;
                                            uint8_t t141 = GM_LD(uint8_t, (((uint8_t *)&gm_l20) + 0));
                                            uint8_t t142 = (uint8_t)!t141;
                                            if (!t142) {
                                                uint8_t t143 = gm_l13;
                                                t140 = t143;
                                            } else t140 = 1;
                                            if (!t140) goto ie139;
                                            {
                                                uint8_t t145 = 0;
                                                F.ret = ((uint8_t)0);
                                                goto gm_fret;
                                            }
                                            goto ix139;
ie139:;
ix139:;
                                        }
                                        goto me135;
mn136:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 771, 13);
me135:;
                                    }
lc126:;
                                    {
                                        uint64_t t147 = gm_l19;
                                        uint64_t r148; bool o149 = __builtin_add_overflow(t147, ((uint64_t)1ULL), &r148);
                                        uint64_t t150 = r148;
                                        gm_l19 = t150;
                                    }
                                }
lb126:;
                            }
                        }
                        goto ix121;
ie121:;
ix121:;
                    }
                    goto me112;
mn113:;
                    gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 768, 5);
me112:;
                }
lc104:;
                {
                    uint64_t t152 = gm_l15;
                    uint64_t r153; bool o154 = __builtin_add_overflow(t152, ((uint64_t)1ULL), &r153);
                    uint64_t t155 = r153;
                    gm_l15 = t155;
                }
            }
lb104:;
        }
    }
    {
        gm_b16_8 t156;
        GM_ST(void *, (uint8_t *)&t156, (void *)(gm_s0m07bui94rjq));
        GM_ST(uintptr_t, (uint8_t *)&t156 + 8, (uintptr_t)(7));
        gm_l21 = t156;
    }
    {
        if (!((((uint8_t *)&gm_l4))[8] != 0)) goto ie157;
        uint64_t t158 = GM_LD(uint64_t, ((uint8_t *)&gm_l4));
        gm_l22 = t158;
        {
            uint64_t t160 = gm_l22;
            uint64_t t161 = (uintptr_t)t160;
            void * t162 = gm_l0;
            uint64_t t163 = GM_LD(uintptr_t, ((((uint8_t *)t162) + 240) + 0) + 8);
            if (t161 >= (uintptr_t)(t163)) gm_trap_index(t161, t163, gm_srcmok80ho8hh01, 778, 14);
            gm_tehgl6lkqgvt0 t164 = GM_LD(gm_tehgl6lkqgvt0, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t162) + 240) + 0)) + t161 * 104));
            F.l23 = t164;
        }
        {
            uint8_t t170 = GM_LD(uint8_t, ((((uint8_t *)&F.l23) + 0) + 0));
            switch (t170) { case ((uint8_t)0ULL): goto ma168; case ((uint8_t)1ULL): goto ma166; case ((uint8_t)2ULL): goto ma167; default: goto ma169; }
ma166:;
            uint8_t t172 = GM_LD(uint8_t, ((((uint8_t *)&F.l23) + 0) + 0));
            if (t172 != ((uint8_t)1ULL)) goto mn171;
            gm_thognv9odov03 t173 = GM_LD(gm_thognv9odov03, ((((uint8_t *)&F.l23) + 0) + 8));
            F.l24 = t173;
            uint8_t t175 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l1) + 0) == ((uint8_t)1ULL)));
            if (!t175) goto ie174;
            {
                gm_b16_8 t177 = GM_LD(gm_b16_8, (((uint8_t *)&F.l24) + 8));
                gm_l21 = t177;
            }
            goto ix174;
ie174:;
            {
                gm_b16_8 t179 = GM_LD(gm_b16_8, (((uint8_t *)&F.l24) + 24));
                gm_l21 = t179;
            }
ix174:;
            goto me165;
mn171:;
ma167:;
            uint8_t t181 = GM_LD(uint8_t, ((((uint8_t *)&F.l23) + 0) + 0));
            if (t181 != ((uint8_t)2ULL)) goto mn180;
            gm_tiilmfd16v3fc t182 = GM_LD(gm_tiilmfd16v3fc, ((((uint8_t *)&F.l23) + 0) + 8));
            gm_l25 = t182;
            {
                gm_b16_8 t184 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l25) + 8));
                gm_l21 = t184;
            }
            goto me165;
mn180:;
ma168:;
            uint8_t t186 = GM_LD(uint8_t, ((((uint8_t *)&F.l23) + 0) + 0));
            if (t186 != ((uint8_t)0ULL)) goto mn185;
            gm_trap("reached `@unreachable()`", gm_srcmok80ho8hh01, 782, 23);
            goto me165;
mn185:;
ma169:;
            gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 779, 9);
me165:;
        }
        goto ix157;
ie157:;
ix157:;
    }
    {
        void * t187 = gm_l0;
        gm_b16_8 t188 = gm_l21;
        gm_tf5srh4mp4b3i t189 = gm_l4;
        gm_b0_1 t190 = gm_f_jump_pvnkvme((void *)NULL, cx, NULL, t187, t188, t189);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint8_t t191 = 0;
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
    GM_STACK_CHECK(&F, 549u);
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
        uint64_t t10 = GM_LD(uintptr_t, (((uint8_t *)t9) + 328) + 8);
        if (t8 >= (uintptr_t)(t10)) gm_trap_index(t8, t10, gm_srcmok80ho8hh01, 539, 8);
        uint8_t t11 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t9) + 328)) + t8 * 1));
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
                    gm_tabb710d9mq2e t26 = t25;
                    if (((((uint8_t *)&t26))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t26) + 8, gm_srcmok80ho8hh01, 540, 65);
                    gm_b16_8 t27 = GM_LD(gm_b16_8, (((uint8_t *)&t26) + 8));
                    gm_b16_8 t28 = t27;
                    gm_b16_8 t29 = GM_LD(gm_b16_8, ((uint8_t *)&t28));
                    gm_tftb1odc739t5 t30 = gm_f_push_3ef746b(t21, t29);
                    gm_tftb1odc739t5 t31 = t30;
                    if (((((uint8_t *)&t31))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t31) + 1, gm_srcmok80ho8hh01, 540, 46);
                    gm_b0_1 t32 = GM_LD(gm_b0_1, (((uint8_t *)&t31) + 1));
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
            uint64_t t33 = gm_l3;
            uint64_t t34 = (uintptr_t)t33;
            void * t35 = gm_l2;
            uint64_t t36 = GM_LD(uintptr_t, ((((uint8_t *)t35) + 3160) + 0) + 8);
            if (t34 >= (uintptr_t)(t36)) gm_trap_index(t34, t36, gm_srcmok80ho8hh01, 541, 16);
            gm_b16_8 t37 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t35) + 3160) + 0)) + t34 * 16));
            uint8_t t38 = 0;
            F.ret = t37;
            goto gm_fret;
        }
        goto ix6;
ie6:;
ix6:;
    }
    {
        for (;;) {
            void * t40 = gm_l2;
            uint64_t t41 = GM_LD(uintptr_t, ((((uint8_t *)t40) + 3200) + 0) + 8);
            uint64_t t42 = gm_l3;
            uint8_t t43 = (t41 <= t42);
            if (!t43) goto lb39;
            {
                void * t46 = gm_l2;
                void * t47 = (void *)(((uint8_t *)t46) + 3200);
                void * t48 = gm_l2;
                uint64_t t49 = GM_LD(uintptr_t, ((((uint8_t *)t48) + 3200) + 0) + 8);
                gm_t4ce0jd3tdro4 t50;
                GM_ST(uint64_t, ((uint8_t *)&t50 + 0), t49);
                gm_tabb710d9mq2e t51;
                gm_f_format_ghqt9ar((void *)&t51, (void *)NULL, cx, NULL, t50);
                if (gm_unw.kind) {
                    goto gm_ll44;
                }
                gm_tabb710d9mq2e t52 = t51;
                if (((((uint8_t *)&t52))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t52) + 8, gm_srcmok80ho8hh01, 543, 61);
                gm_b16_8 t53 = GM_LD(gm_b16_8, (((uint8_t *)&t52) + 8));
                gm_b16_8 t54 = t53;
                gm_b16_8 t55 = GM_LD(gm_b16_8, ((uint8_t *)&t54));
                gm_tftb1odc739t5 t56 = gm_f_push_3ef746b(t47, t55);
                gm_tftb1odc739t5 t57 = t56;
                if (((((uint8_t *)&t57))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t57) + 1, gm_srcmok80ho8hh01, 543, 42);
                gm_b0_1 t58 = GM_LD(gm_b0_1, (((uint8_t *)&t57) + 1));
            }
lc39:;
        }
gm_ll44:;
        if (gm_unw.frame == &F.h && gm_unw.label == 1) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb39; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc39; }
        }
        goto gm_lad;
lb39:;
    }
    {
        uint64_t t59 = gm_l3;
        uint64_t t60 = (uintptr_t)t59;
        void * t61 = gm_l2;
        uint64_t t62 = GM_LD(uintptr_t, ((((uint8_t *)t61) + 3200) + 0) + 8);
        if (t60 >= (uintptr_t)(t62)) gm_trap_index(t60, t62, gm_srcmok80ho8hh01, 544, 12);
        gm_b16_8 t63 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t61) + 3200) + 0)) + t60 * 16));
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
        gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 1229, 28);
me35:;
        goto me3;
mn32:;
ma6:;
        goto me3;
mn67:;
ma7:;
        gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 1227, 5);
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
        gm_closure t74 = ((gm_closure){GM_PTR_TAG(gm_f_fn_2tr382j), NULL, cx});
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
        gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 928, 5);
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
                    gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 811, 5);
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
    GM_STACK_CHECK_SHALLOW(&F, 276u);
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
                    gm_tn68el8d14vku t18 = t17;
                    uint64_t t21 = GM_LD(uint64_t, (((uint8_t *)&t18) + 0));
                    gm_l4 = t21;
                    uint32_t t22 = GM_LD(uint32_t, (((uint8_t *)&t18) + 8));
                    gm_l5 = t22;
                    {
                        uint32_t t25 = gm_l5;
                        uint32_t t26 = gm_l1;
                        uint8_t t27 = ((t25 == t26));
                        if (!t27) goto ie24;
                        {
                            uint64_t t29 = gm_l4;
                            gm_tf5srh4mp4b3i t30;
                            GM_ST(uint8_t, (uint8_t *)&t30 + 9, 0);
                            GM_ST(uint16_t, (uint8_t *)&t30 + 10, 0);
                            GM_ST(uint32_t, (uint8_t *)&t30 + 12, 0);
                            GM_ST(uint64_t, ((uint8_t *)&t30), t29);
                            ((uint8_t *)&t30)[8] = 1;
                            uint8_t t31 = 0;
                            F.ret = t30;
                            goto gm_fret;
                        }
                        goto ix24;
ie24:;
ix24:;
                    }
                    goto me19;
mn20:;
                    gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 514, 5);
me19:;
                }
lc6:;
                {
                    uint64_t t33 = gm_l3;
                    uint64_t r34; bool o35 = __builtin_add_overflow(t33, ((uint64_t)1ULL), &r34);
                    uint64_t t36 = r34;
                    gm_l3 = t36;
                }
            }
lb6:;
        }
    }
    {
        gm_tf5srh4mp4b3i t37;
        memset(&t37, 0, sizeof t37);
        uint8_t t38 = 0;
        F.ret = t37;
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
    GM_STACK_CHECK(&F, 107u);
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
            void * t14 = t13;
            void * t15;
            if ((GM_LD(void *, ((uint8_t *)&t14)) != 0)) {
                void * t16 = GM_LD(void *, (((uint8_t *)&t14) + 0));
                t15 = t16;
            } else {
                uint8_t t17 = 0;
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            gm_l3 = t15;
        }
        {
            void * t18 = gm_l0;
            void * t19 = gm_l3;
            uint8_t t20 = gm_f_calls_probe_2ia3rhf((void *)NULL, cx, NULL, t18, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t21 = 0;
            F.ret = t20;
            goto gm_fret;
        }
        goto ix7;
ie7:;
ix7:;
    }
    {
        uint8_t t22 = 0;
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
    GM_STACK_CHECK(&F, 154u);
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
        gm_t57euh612ligr t30 = t29;
        uint8_t t31;
        if (((((uint8_t *)&t30))[24] != 0)) goto mn33;
        t31 = ((uint8_t)1);
        goto me32;
mn33:;
        t31 = ((uint8_t)0);
        goto me32;
mn34:;
        gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 939, 35);
me32:;
        uint8_t t35 = 0;
        F.ret = t31;
        goto gm_fret;
        goto me3;
mn21:;
ma6:;
        uint8_t t37 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t37 != ((uint8_t)20ULL)) goto mn36;
        uint8_t t38 = 0;
        F.ret = ((uint8_t)1);
        goto gm_fret;
        goto me3;
mn36:;
ma7:;
        uint8_t t40 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
        goto me3;
mn39:;
ma8:;
        gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 937, 5);
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
    GM_STACK_CHECK(&F, 466u);
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
        gm_tf5srh4mp4b3i t5 = t4;
        uint64_t t6;
        if (((((uint8_t *)&t5))[8] != 0)) {
            uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)&t5) + 0));
            t6 = t7;
        } else {
            gm_b16_8 t8 = gm_l0;
            uint8_t t9 = 0;
            F.ret = t8;
            goto gm_fret;
        }
        gm_l3 = t6;
    }
    {
        gm_tfeab279c16rr t10;
        uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        void * t12 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
        uint64_t t13 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t14 = gm_l3;
        uint64_t t15 = (uintptr_t)t14;
        if (t13 > t15 || t15 > (uintptr_t)t11) gm_trap_slice(t13, t15, t11, gm_srcmok80ho8hh01, 47, 33);
        gm_b16_8 t16;
        GM_ST(void *, (uint8_t *)&t16, (void *)(((uint8_t *)t12 + t13 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t16 + 8, (uintptr_t)(t15 - t13));
        GM_ST(gm_b16_8, (uint8_t *)&t10 + 0, t16);
        gm_b16_8 t17 = gm_l2;
        GM_ST(gm_b16_8, (uint8_t *)&t10 + 16, t17);
        uint64_t t18 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        void * t19 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
        uint64_t t20 = gm_l3;
        uint64_t t21 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint64_t r22; bool o23 = __builtin_add_overflow(t20, t21, &r22);
        if (o23) gm_trap("integer overflow in `+`", gm_srcmok80ho8hh01, 47, 54);
        uint64_t t24 = r22;
        uint64_t t25 = (uintptr_t)t24;
        if (t25 > (uintptr_t)t18 || (uintptr_t)t18 > (uintptr_t)t18) gm_trap_slice(t25, (uintptr_t)t18, t18, gm_srcmok80ho8hh01, 47, 49);
        gm_b16_8 t26;
        GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t19 + t25 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)((uintptr_t)t18 - t25));
        GM_ST(gm_b16_8, (uint8_t *)&t10 + 32, t26);
        gm_tabb710d9mq2e t27;
        gm_f_format_dsta2ql((void *)&t27, (void *)NULL, cx, NULL, &t10);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t28 = t27;
        if (((((uint8_t *)&t28))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t28) + 8, gm_srcmok80ho8hh01, 47, 12);
        gm_b16_8 t29 = GM_LD(gm_b16_8, (((uint8_t *)&t28) + 8));
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
extern __attribute__((aligned(4))) gm_b16_8 gm_f_size_attrs_abk1l3m(uint32_t a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_src_line_d24sajl(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1) {
    struct gm_fr_src_line_d24sajl F;
    void * gm_l0; /* fg */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* d */
    GM_STACK_CHECK(&F, 180u);
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
        gm_th2lqbo77pvsc t7 = t6;
        if (!((((uint8_t *)&t7))[16] != 0)) goto ie2;
        gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&t7));
        gm_l2 = t8;
        {
            void * t10 = gm_l0;
            void * t11 = (void *)(((uint8_t *)t10) + 184);
            gm_b16_8 t12 = gm_l2;
            gm_tnpifv20irnph t13;
            GM_ST(gm_b16_8, ((uint8_t *)&t13 + 0), t12);
            gm_tftb1odc739t5 t14 = gm_f_write_cdouvhd(t11, t13);
            gm_tftb1odc739t5 t15 = t14;
            if (((((uint8_t *)&t15))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t15) + 1, gm_srcmok80ho8hh01, 425, 41);
            gm_b0_1 t16 = GM_LD(gm_b0_1, (((uint8_t *)&t15) + 1));
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
    GM_STACK_CHECK(&F, 8213u);
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
                void * t19 = t18;
                void * t20;
                if ((GM_LD(void *, ((uint8_t *)&t19)) != 0)) {
                    void * t21 = GM_LD(void *, (((uint8_t *)&t19) + 0));
                    t20 = t21;
                } else {
                    gm_b0_1 t22;
                    gm_tqgtd5gtgj65s t23;
                    memset(&t23, 0, sizeof t23);
                    ((uint8_t *)&t23)[0] = 0;
                    GM_ST(gm_b0_1, ((uint8_t *)&t23 + 8), t22);
                    gm_tqgtd5gtgj65s t24 = t23;
                    uint8_t t25 = ((((uint8_t *)&t24))[0] != 0);
                    F.ret = t23;
                    goto gm_fret;
                }
                gm_l4 = t20;
            }
            {
                void * t27 = gm_l4;
                uint8_t t28 = ((GM_LD(uint8_t, (((uint8_t *)t27) + 8) + 0) == ((uint8_t)8ULL)));
                if (!t28) goto ie26;
                {
                    void * t31 = gm_l0;
                    void * t32 = GM_LD(void *, (((uint8_t *)t31) + 0));
                    uint8_t t33 = GM_LD(uint8_t, ((((uint8_t *)t32) + 1136) + 1));
                    if (!t33) goto ie30;
                    {
                        void * t35 = gm_l0;
                        uint32_t t36 = gm_l2;
                        gm_b16_8 t37 = gm_f_local_lv_m2jtf0n((void *)NULL, cx, NULL, t35, t36);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l5 = t37;
                    }
                    {
                        void * t38 = gm_l0;
                        gm_t6j6smj7qoctu t39;
                        gm_b16_8 t40 = gm_l5;
                        GM_ST(gm_b16_8, (uint8_t *)&t39 + 0, t40);
                        gm_b16_8 t41 = gm_l5;
                        GM_ST(gm_b16_8, (uint8_t *)&t39 + 16, t41);
                        gm_b0_1 t42 = gm_f_linef_caq6p77((void *)NULL, cx, NULL, t38, &t39);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    goto ix30;
ie30:;
ix30:;
                }
                {
                    gm_b0_1 t43;
                    gm_tqgtd5gtgj65s t44;
                    memset(&t44, 0, sizeof t44);
                    ((uint8_t *)&t44)[0] = 0;
                    GM_ST(gm_b0_1, ((uint8_t *)&t44 + 8), t43);
                    gm_tqgtd5gtgj65s t45 = t44;
                    uint8_t t46 = ((((uint8_t *)&t45))[0] != 0);
                    F.ret = t44;
                    goto gm_fret;
                }
                goto ix26;
ie26:;
ix26:;
            }
            {
                void * t48 = gm_l0;
                void * t49 = gm_l4;
                uint8_t t50 = gm_f_big_place_raoupin((void *)NULL, cx, NULL, t48, t49);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (!t50) goto ie47;
                {
                    void * t52 = gm_l0;
                    void * t53 = gm_l4;
                    gm_t5o0tpv5jka2l t54;
                    gm_f_place_addr_7ji15ot((void *)&t54, (void *)NULL, cx, NULL, t52, t53);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t5o0tpv5jka2l t55 = t54;
                    if (((((uint8_t *)&t55))[0] != 0)) {
                        gm_t08olq97rscn3 t56 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t55) + 8));
                        gm_tqgtd5gtgj65s t57;
                        GM_ST(uint8_t, (uint8_t *)&t57 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t57 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t57 + 4, 0);
                        ((uint8_t *)&t57)[0] = 1;
                        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t57 + 8), t56);
                        F.ret = t57;
                        goto gm_fret;
                    }
                    gm_b16_8 t58 = GM_LD(gm_b16_8, (((uint8_t *)&t55) + 8));
                    gm_l6 = t58;
                }
                {
                    void * t59 = gm_l0;
                    uint32_t t60 = gm_l2;
                    gm_b16_8 t61 = gm_f_local_lv_m2jtf0n((void *)NULL, cx, NULL, t59, t60);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l7 = t61;
                }
                {
                    void * t62 = gm_l0;
                    gm_tfeab279c16rr t63;
                    gm_b16_8 t64 = gm_l7;
                    GM_ST(gm_b16_8, (uint8_t *)&t63 + 0, t64);
                    gm_b16_8 t65 = gm_l6;
                    GM_ST(gm_b16_8, (uint8_t *)&t63 + 16, t65);
                    gm_b16_8 t66 = gm_l7;
                    GM_ST(gm_b16_8, (uint8_t *)&t63 + 32, t66);
                    gm_b0_1 t67 = gm_f_linef_heeeedc((void *)NULL, cx, NULL, t62, &t63);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    gm_b0_1 t68;
                    gm_tqgtd5gtgj65s t69;
                    memset(&t69, 0, sizeof t69);
                    ((uint8_t *)&t69)[0] = 0;
                    GM_ST(gm_b0_1, ((uint8_t *)&t69 + 8), t68);
                    gm_tqgtd5gtgj65s t70 = t69;
                    uint8_t t71 = ((((uint8_t *)&t70))[0] != 0);
                    F.ret = t69;
                    goto gm_fret;
                }
                goto ix47;
ie47:;
ix47:;
            }
            {
                void * t72 = gm_l0;
                uint32_t t73 = gm_l2;
                gm_b16_8 t74 = gm_f_local_lv_m2jtf0n((void *)NULL, cx, NULL, t72, t73);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l8 = t74;
            }
            {
                void * t75 = gm_l0;
                void * t76 = gm_l4;
                gm_b16_8 t77 = gm_l8;
                gm_tnrltbc61i6v1 t78;
                GM_ST(uint8_t, (uint8_t *)&t78 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t78 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t78 + 4, 0);
                GM_ST(uint32_t, (uint8_t *)&t78 + 8, 0);
                GM_ST(uint8_t, ((uint8_t *)&t78 + 0), ((uint8_t)0ULL));
                gm_tqgtd5gtgj65s t79;
                gm_f_expr_into_dn9kavo((void *)&t79, (void *)NULL, cx, NULL, t75, t76, t77, t78);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tqgtd5gtgj65s t80 = t79;
                if (((((uint8_t *)&t80))[0] != 0)) {
                    gm_t08olq97rscn3 t81 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t80) + 8));
                    gm_tqgtd5gtgj65s t82;
                    GM_ST(uint8_t, (uint8_t *)&t82 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t82 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t82 + 4, 0);
                    ((uint8_t *)&t82)[0] = 1;
                    GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t82 + 8), t81);
                    F.ret = t82;
                    goto gm_fret;
                }
                gm_b0_1 t83 = GM_LD(gm_b0_1, (((uint8_t *)&t80) + 8));
            }
            goto me4;
mn13:;
ma6:;
            uint8_t t85 = GM_LD(uint8_t, (((uint8_t *)t3) + 0));
            if (t85 != ((uint8_t)1ULL)) goto mn84;
            gm_b16_8 t86 = GM_LD(gm_b16_8, ((((uint8_t *)t3) + 8) + 0));
            gm_l9 = t86;
            void * t87 = GM_LD(void *, ((((uint8_t *)t3) + 8) + 16));
            gm_l10 = t87;
            {
                void * t89 = gm_l0;
                void * t90 = gm_l10;
                gm_t5o0tpv5jka2l t91;
                gm_f_expr_porbc9t((void *)&t91, (void *)NULL, cx, NULL, t89, t90);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t5o0tpv5jka2l t92 = t91;
                if (((((uint8_t *)&t92))[0] != 0)) {
                    gm_t08olq97rscn3 t93 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t92) + 8));
                    gm_tqgtd5gtgj65s t94;
                    GM_ST(uint8_t, (uint8_t *)&t94 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t94 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t94 + 4, 0);
                    ((uint8_t *)&t94)[0] = 1;
                    GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t94 + 8), t93);
                    F.ret = t94;
                    goto gm_fret;
                }
                gm_b16_8 t95 = GM_LD(gm_b16_8, (((uint8_t *)&t92) + 8));
                gm_l11 = t95;
            }
            {
                void * t96 = gm_l0;
                void * t97 = gm_l10;
                gm_t88rcucg1b7uk t98 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t97) + 0));
                gm_b16_8 t99 = gm_l11;
                gm_b16_8 t100 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t96, t98, t99);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l12 = t100;
            }
            {
                void * t101 = gm_l0;
                void * t102 = GM_LD(void *, (((uint8_t *)t101) + 0));
                void * t103 = (void *)(((uint8_t *)t102) + 8);
                void * t104 = gm_l10;
                gm_t88rcucg1b7uk t105 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t104) + 0));
                gm_b16_8 t106 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t103, t105);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l13 = t106;
            }
            {
                {
                    gm_b16_8 t108 = gm_l9;
                    gm_b16_8 t109 = t108;
                    gm_b16_8 t110 = GM_LD(gm_b16_8, ((uint8_t *)&t109));
                    gm_l14 = t110;
                }
                {
                    gm_l15 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t112 = gm_l15;
                        uint64_t t113 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                        uint8_t t114 = (t112 < t113);
                        if (!t114) goto lb111;
                        {
                            uint64_t t117 = gm_l15;
                            uint64_t t118 = gm_l15;
                            uint64_t t119 = (uintptr_t)t118;
                            uint64_t t120 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                            gm_t9805qtonbsag t121 = GM_LD(gm_t9805qtonbsag, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l14)) + t119 * 8));
                            gm_tp0pp2cmr6tvd t122;
                            GM_ST(uint64_t, ((uint8_t *)&t122 + 0), t117);
                            GM_ST(gm_t9805qtonbsag, ((uint8_t *)&t122 + 8), t121);
                            gm_tp0pp2cmr6tvd t123 = t122;
                            uint64_t t126 = GM_LD(uint64_t, (((uint8_t *)&t123) + 0));
                            gm_l16 = t126;
                            gm_t9805qtonbsag t127 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)&t123) + 8));
                            gm_l17 = t127;
                            {
                                if (!((((uint8_t *)&gm_l17))[4] != 0)) goto ie129;
                                uint32_t t130 = GM_LD(uint32_t, ((uint8_t *)&gm_l17));
                                gm_l18 = t130;
                                {
                                    uint32_t t132 = gm_l18;
                                    uint64_t t133 = (uint64_t)t132;
                                    uint64_t t134 = (uintptr_t)t133;
                                    void * t135 = gm_l0;
                                    void * t136 = GM_LD(void *, (((uint8_t *)t135) + 176));
                                    uint64_t t137 = GM_LD(uintptr_t, (((uint8_t *)t136) + 48) + 8);
                                    if (t134 >= (uintptr_t)(t137)) gm_trap_index(t134, t137, gm_srcmok80ho8hh01, 1041, 27);
                                    gm_t88rcucg1b7uk t138 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, (((uint8_t *)t136) + 48)) + t134 * 40) + 16));
                                    gm_l19 = t138;
                                }
                                {
                                    void * t139 = gm_l0;
                                    void * t140 = GM_LD(void *, (((uint8_t *)t139) + 0));
                                    void * t141 = (void *)(((uint8_t *)t140) + 8);
                                    gm_t88rcucg1b7uk t142 = gm_l19;
                                    gm_b16_8 t143 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t141, t142);
                                    if (gm_unw.kind) {
                                        goto gm_ll115;
                                    }
                                    gm_l20 = t143;
                                }
                                {
                                    void * t144 = gm_l0;
                                    uint32_t t145 = gm_l18;
                                    gm_b16_8 t146 = gm_f_local_lv_m2jtf0n((void *)NULL, cx, NULL, t144, t145);
                                    if (gm_unw.kind) {
                                        goto gm_ll115;
                                    }
                                    gm_l21 = t146;
                                }
                                {
                                    void * t147 = gm_l0;
                                    gm_tttl9nsdo1iu6 t148;
                                    gm_b16_8 t149 = gm_l21;
                                    GM_ST(gm_b16_8, (uint8_t *)&t148 + 0, t149);
                                    gm_b16_8 t150 = gm_l20;
                                    GM_ST(gm_b16_8, (uint8_t *)&t148 + 16, t150);
                                    gm_b16_8 t151 = gm_l12;
                                    GM_ST(gm_b16_8, (uint8_t *)&t148 + 32, t151);
                                    uint64_t t152 = gm_l16;
                                    uint64_t t153 = (uintptr_t)t152;
                                    uint64_t t154 = GM_LD(uintptr_t, ((uint8_t *)&gm_l13) + 8);
                                    if (t153 >= (uintptr_t)(t154)) gm_trap_index(t153, t154, gm_srcmok80ho8hh01, 1044, 80);
                                    uint64_t t155 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l13)) + t153 * 8));
                                    GM_ST(uint64_t, (uint8_t *)&t148 + 48, t155);
                                    gm_b0_1 t156 = gm_f_linef_h4kb1fp((void *)NULL, cx, NULL, t147, &t148);
                                    if (gm_unw.kind) {
                                        goto gm_ll115;
                                    }
                                }
                                goto ix129;
ie129:;
ix129:;
                            }
                            goto me124;
mn125:;
                            gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 1039, 13);
me124:;
                        }
lc111:;
                        {
                            uint64_t t158 = gm_l15;
                            uint64_t r159; bool o160 = __builtin_add_overflow(t158, ((uint64_t)1ULL), &r159);
                            uint64_t t161 = r159;
                            gm_l15 = t161;
                        }
                    }
gm_ll115:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb111; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc111; }
                    }
                    goto gm_lad;
lb111:;
                }
            }
            goto me4;
mn84:;
ma7:;
            uint8_t t163 = GM_LD(uint8_t, (((uint8_t *)t3) + 0));
            if (t163 != ((uint8_t)2ULL)) goto mn162;
            void * t164 = GM_LD(void *, ((((uint8_t *)t3) + 8) + 0));
            gm_l22 = t164;
            void * t165 = GM_LD(void *, ((((uint8_t *)t3) + 8) + 8));
            gm_l23 = t165;
            {
                void * t168 = gm_l22;
                uint8_t t169 = GM_LD(uint8_t, ((((uint8_t *)t168) + 8) + 0));
                if (t169 != ((uint8_t)19ULL)) goto ie167;
                void * t170 = GM_LD(void *, (((((uint8_t *)t168) + 8) + 8) + 0));
                gm_l24 = t170;
                uint32_t t171 = GM_LD(uint32_t, (((((uint8_t *)t168) + 8) + 8) + 8));
                gm_l25 = t171;
                {
                    void * t174 = gm_l0;
                    void * t175 = GM_LD(void *, (((uint8_t *)t174) + 0));
                    void * t176 = (void *)(((uint8_t *)t175) + 8);
                    void * t177 = gm_l24;
                    gm_t88rcucg1b7uk t178 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t177) + 0));
                    gm_t57euh612ligr t179;
                    gm_f_bits_layout_23rect9((void *)&t179, (void *)NULL, cx, NULL, t176, t178);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t57euh612ligr t180 = t179;
                    if (!((((uint8_t *)&t180))[24] != 0)) goto ie173;
                    gm_tcmkompbl09il t181 = GM_LD(gm_tcmkompbl09il, ((uint8_t *)&t180));
                    gm_l26 = t181;
                    {
                        uint32_t t183 = gm_l25;
                        uint64_t t184 = (uint64_t)t183;
                        uint64_t t185 = (uintptr_t)t184;
                        uint64_t t186 = GM_LD(uintptr_t, (((uint8_t *)&gm_l26) + 8) + 8);
                        if (t185 >= (uintptr_t)(t186)) gm_trap_index(t185, t186, gm_srcmok80ho8hh01, 1052, 29);
                        uint32_t t187 = GM_LD(uint32_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)&gm_l26) + 8)) + t185 * 12) + 0));
                        gm_l27 = t187;
                    }
                    {
                        uint32_t t188 = gm_l25;
                        uint64_t t189 = (uint64_t)t188;
                        uint64_t t190 = (uintptr_t)t189;
                        uint64_t t191 = GM_LD(uintptr_t, (((uint8_t *)&gm_l26) + 8) + 8);
                        if (t190 >= (uintptr_t)(t191)) gm_trap_index(t190, t191, gm_srcmok80ho8hh01, 1053, 26);
                        uint32_t t192 = GM_LD(uint32_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)&gm_l26) + 8)) + t190 * 12) + 4));
                        gm_l28 = t192;
                    }
                    {
                        void * t193 = gm_l24;
                        gm_l29 = t193;
                    }
                    {
                        uint32_t t194 = gm_l27;
                        gm_l30 = t194;
                    }
                    {
                        for (;;) {
                            if (!((uint8_t)1)) goto lb195;
                            {
                                void * t199 = gm_l29;
                                uint8_t t200 = GM_LD(uint8_t, ((((uint8_t *)t199) + 8) + 0));
                                if (t200 != ((uint8_t)19ULL)) goto ie198;
                                void * t201 = GM_LD(void *, (((((uint8_t *)t199) + 8) + 8) + 0));
                                gm_l31 = t201;
                                uint32_t t202 = GM_LD(uint32_t, (((((uint8_t *)t199) + 8) + 8) + 8));
                                gm_l32 = t202;
                                {
                                    void * t204 = gm_l0;
                                    void * t205 = GM_LD(void *, (((uint8_t *)t204) + 0));
                                    void * t206 = (void *)(((uint8_t *)t205) + 8);
                                    void * t207 = gm_l31;
                                    gm_t88rcucg1b7uk t208 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t207) + 0));
                                    gm_t57euh612ligr t209;
                                    gm_f_bits_layout_23rect9((void *)&t209, (void *)NULL, cx, NULL, t206, t208);
                                    if (gm_unw.kind) {
                                        goto gm_ll196;
                                    }
                                    gm_t57euh612ligr t210 = t209;
                                    gm_tcmkompbl09il t211;
                                    if (((((uint8_t *)&t210))[24] != 0)) {
                                        gm_tcmkompbl09il t212 = GM_LD(gm_tcmkompbl09il, (((uint8_t *)&t210) + 0));
                                        t211 = t212;
                                    } else {
                                        goto lb195;
                                    }
                                    gm_l33 = t211;
                                }
                                {
                                    uint32_t t213 = gm_l30;
                                    uint32_t t214 = gm_l32;
                                    uint64_t t215 = (uint64_t)t214;
                                    uint64_t t216 = (uintptr_t)t215;
                                    uint64_t t217 = GM_LD(uintptr_t, (((uint8_t *)&gm_l33) + 8) + 8);
                                    if (t216 >= (uintptr_t)(t217)) gm_trap_index(t216, t217, gm_srcmok80ho8hh01, 1060, 32);
                                    uint32_t t218 = GM_LD(uint32_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)&gm_l33) + 8)) + t216 * 12) + 0));
                                    uint32_t r219; bool o220 = __builtin_add_overflow(t213, t218, &r219);
                                    if (o220) gm_trap("integer overflow in `+`", gm_srcmok80ho8hh01, 1060, 25);
                                    uint32_t t221 = r219;
                                    gm_l30 = t221;
                                }
                                {
                                    void * t222 = gm_l31;
                                    gm_l29 = t222;
                                }
                                goto ix198;
ie198:;
                                {
                                    goto lb195;
                                }
ix198:;
                            }
lc195:;
                        }
gm_ll196:;
                        if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb195; }
                            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc195; }
                        }
                        goto gm_lad;
lb195:;
                    }
                    {
                        void * t224 = gm_l0;
                        void * t225 = GM_LD(void *, (((uint8_t *)t224) + 0));
                        void * t226 = (void *)(((uint8_t *)t225) + 8);
                        void * t227 = gm_l29;
                        gm_t88rcucg1b7uk t228 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t227) + 0));
                        gm_t57euh612ligr t229;
                        gm_f_bits_layout_23rect9((void *)&t229, (void *)NULL, cx, NULL, t226, t228);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_t57euh612ligr t230 = t229;
                        if (!((((uint8_t *)&t230))[24] != 0)) gm_trap("unwrap of null", gm_srcmok80ho8hh01, 1063, 27);
                        gm_tcmkompbl09il t231 = GM_LD(gm_tcmkompbl09il, ((uint8_t *)&t230));
                        gm_l34 = t231;
                    }
                    {
                        void * t232 = gm_l0;
                        void * t233 = gm_l29;
                        void * t234 = gm_l23;
                        gm_t5o0tpv5jka2l t235;
                        gm_f_assign_addr_rlofoa5((void *)&t235, (void *)NULL, cx, NULL, t232, t233, t234);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_t5o0tpv5jka2l t236 = t235;
                        if (((((uint8_t *)&t236))[0] != 0)) {
                            gm_t08olq97rscn3 t237 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t236) + 8));
                            gm_tqgtd5gtgj65s t238;
                            GM_ST(uint8_t, (uint8_t *)&t238 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t238 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t238 + 4, 0);
                            ((uint8_t *)&t238)[0] = 1;
                            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t238 + 8), t237);
                            F.ret = t238;
                            goto gm_fret;
                        }
                        gm_b16_8 t239 = GM_LD(gm_b16_8, (((uint8_t *)&t236) + 8));
                        gm_l35 = t239;
                    }
                    {
                        void * t240 = gm_l0;
                        void * t241 = gm_l23;
                        gm_t5o0tpv5jka2l t242;
                        gm_f_expr_porbc9t((void *)&t242, (void *)NULL, cx, NULL, t240, t241);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_t5o0tpv5jka2l t243 = t242;
                        if (((((uint8_t *)&t243))[0] != 0)) {
                            gm_t08olq97rscn3 t244 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t243) + 8));
                            gm_tqgtd5gtgj65s t245;
                            GM_ST(uint8_t, (uint8_t *)&t245 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t245 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t245 + 4, 0);
                            ((uint8_t *)&t245)[0] = 1;
                            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t245 + 8), t244);
                            F.ret = t245;
                            goto gm_fret;
                        }
                        gm_b16_8 t246 = GM_LD(gm_b16_8, (((uint8_t *)&t243) + 8));
                        gm_l36 = t246;
                    }
                    {
                        void * t247 = gm_l0;
                        void * t248 = gm_l23;
                        gm_t88rcucg1b7uk t249 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t248) + 0));
                        gm_b16_8 t250 = gm_l36;
                        gm_b16_8 t251 = gm_f_bits_raw_e0qg9mb((void *)NULL, cx, NULL, t247, t249, t250);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l37 = t251;
                    }
                    {
                        void * t252 = gm_l0;
                        void * t253 = GM_LD(void *, (((uint8_t *)t252) + 0));
                        void * t254 = (void *)(((uint8_t *)t253) + 8);
                        gm_t88rcucg1b7uk t255 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l34) + 0));
                        gm_b16_8 t256 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t254, t255);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l38 = t256;
                    }
                    {
                        uint32_t t257 = gm_l28;
                        gm_b16_8 t258 = gm_f_mask_epjl3to((void *)NULL, cx, NULL, t257);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l39 = t258;
                    }
                    {
                        void * t259 = gm_l0;
                        gm_tt4j8kuecfafr t260;
                        memset((uint8_t *)&t260 + 100, 0, 4);
                        memset((uint8_t *)&t260 + 140, 0, 4);
                        gm_b16_8 t261 = gm_l38;
                        GM_ST(gm_b16_8, (uint8_t *)&t260 + 0, t261);
                        gm_b16_8 t262 = gm_l35;
                        GM_ST(gm_b16_8, (uint8_t *)&t260 + 16, t262);
                        gm_b16_8 t263 = gm_l38;
                        GM_ST(gm_b16_8, (uint8_t *)&t260 + 32, t263);
                        gm_b16_8 t264 = gm_l38;
                        GM_ST(gm_b16_8, (uint8_t *)&t260 + 48, t264);
                        gm_b16_8 t265 = gm_l35;
                        GM_ST(gm_b16_8, (uint8_t *)&t260 + 64, t265);
                        gm_b16_8 t266 = gm_l39;
                        GM_ST(gm_b16_8, (uint8_t *)&t260 + 80, t266);
                        uint32_t t267 = gm_l30;
                        GM_ST(uint32_t, (uint8_t *)&t260 + 96, t267);
                        gm_b16_8 t268 = gm_l37;
                        GM_ST(gm_b16_8, (uint8_t *)&t260 + 104, t268);
                        gm_b16_8 t269 = gm_l39;
                        GM_ST(gm_b16_8, (uint8_t *)&t260 + 120, t269);
                        uint32_t t270 = gm_l30;
                        GM_ST(uint32_t, (uint8_t *)&t260 + 136, t270);
                        gm_b0_1 t271 = gm_f_linef_rgkpmgb((void *)NULL, cx, NULL, t259, &t260);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    {
                        gm_b0_1 t272;
                        gm_tqgtd5gtgj65s t273;
                        memset(&t273, 0, sizeof t273);
                        ((uint8_t *)&t273)[0] = 0;
                        GM_ST(gm_b0_1, ((uint8_t *)&t273 + 8), t272);
                        gm_tqgtd5gtgj65s t274 = t273;
                        uint8_t t275 = ((((uint8_t *)&t274))[0] != 0);
                        F.ret = t273;
                        goto gm_fret;
                    }
                    goto ix173;
ie173:;
ix173:;
                }
                goto ix167;
ie167:;
ix167:;
            }
            {
                void * t277 = gm_l0;
                void * t278 = gm_l23;
                uint8_t t279 = gm_f_big_place_raoupin((void *)NULL, cx, NULL, t277, t278);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (!t279) goto ie276;
                {
                    void * t281 = gm_l0;
                    void * t282 = gm_l22;
                    void * t283 = gm_l23;
                    gm_t5o0tpv5jka2l t284;
                    gm_f_assign_addr_rlofoa5((void *)&t284, (void *)NULL, cx, NULL, t281, t282, t283);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t5o0tpv5jka2l t285 = t284;
                    if (((((uint8_t *)&t285))[0] != 0)) {
                        gm_t08olq97rscn3 t286 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t285) + 8));
                        gm_tqgtd5gtgj65s t287;
                        GM_ST(uint8_t, (uint8_t *)&t287 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t287 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t287 + 4, 0);
                        ((uint8_t *)&t287)[0] = 1;
                        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t287 + 8), t286);
                        F.ret = t287;
                        goto gm_fret;
                    }
                    gm_b16_8 t288 = GM_LD(gm_b16_8, (((uint8_t *)&t285) + 8));
                    gm_l40 = t288;
                }
                {
                    void * t289 = gm_l0;
                    void * t290 = gm_l23;
                    gm_t5o0tpv5jka2l t291;
                    gm_f_place_addr_7ji15ot((void *)&t291, (void *)NULL, cx, NULL, t289, t290);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t5o0tpv5jka2l t292 = t291;
                    if (((((uint8_t *)&t292))[0] != 0)) {
                        gm_t08olq97rscn3 t293 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t292) + 8));
                        gm_tqgtd5gtgj65s t294;
                        GM_ST(uint8_t, (uint8_t *)&t294 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t294 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t294 + 4, 0);
                        ((uint8_t *)&t294)[0] = 1;
                        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t294 + 8), t293);
                        F.ret = t294;
                        goto gm_fret;
                    }
                    gm_b16_8 t295 = GM_LD(gm_b16_8, (((uint8_t *)&t292) + 8));
                    gm_l41 = t295;
                }
                {
                    void * t296 = gm_l0;
                    gm_tftksr4op114e t297;
                    gm_b16_8 t298 = gm_l40;
                    GM_ST(gm_b16_8, (uint8_t *)&t297 + 0, t298);
                    gm_b16_8 t299 = gm_l41;
                    GM_ST(gm_b16_8, (uint8_t *)&t297 + 16, t299);
                    void * t300 = gm_l0;
                    void * t301 = GM_LD(void *, (((uint8_t *)t300) + 0));
                    void * t302 = (void *)(((uint8_t *)t301) + 8);
                    void * t303 = gm_l23;
                    gm_t88rcucg1b7uk t304 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t303) + 0));
                    uint64_t t305 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t302, t304);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(uint64_t, (uint8_t *)&t297 + 32, t305);
                    gm_b0_1 t306 = gm_f_linef_848m8fq((void *)NULL, cx, NULL, t296, &t297);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    gm_b0_1 t307;
                    gm_tqgtd5gtgj65s t308;
                    memset(&t308, 0, sizeof t308);
                    ((uint8_t *)&t308)[0] = 0;
                    GM_ST(gm_b0_1, ((uint8_t *)&t308 + 8), t307);
                    gm_tqgtd5gtgj65s t309 = t308;
                    uint8_t t310 = ((((uint8_t *)&t309))[0] != 0);
                    F.ret = t308;
                    goto gm_fret;
                }
                goto ix276;
ie276:;
ix276:;
            }
            {
                void * t312 = gm_l0;
                void * t313 = gm_l23;
                uint8_t t314 = gm_f_builds_in_place_ae1b90u((void *)NULL, cx, NULL, t312, t313);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (!t314) goto ie311;
                {
                    void * t316 = gm_l0;
                    void * t317 = gm_l22;
                    void * t318 = gm_l23;
                    gm_t5o0tpv5jka2l t319;
                    gm_f_assign_addr_rlofoa5((void *)&t319, (void *)NULL, cx, NULL, t316, t317, t318);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t5o0tpv5jka2l t320 = t319;
                    if (((((uint8_t *)&t320))[0] != 0)) {
                        gm_t08olq97rscn3 t321 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t320) + 8));
                        gm_tqgtd5gtgj65s t322;
                        GM_ST(uint8_t, (uint8_t *)&t322 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t322 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t322 + 4, 0);
                        ((uint8_t *)&t322)[0] = 1;
                        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t322 + 8), t321);
                        F.ret = t322;
                        goto gm_fret;
                    }
                    gm_b16_8 t323 = GM_LD(gm_b16_8, (((uint8_t *)&t320) + 8));
                    gm_l42 = t323;
                }
                {
                    void * t324 = gm_l0;
                    void * t325 = GM_LD(void *, (((uint8_t *)t324) + 0));
                    void * t326 = (void *)(((uint8_t *)t325) + 8);
                    void * t327 = gm_l22;
                    gm_t88rcucg1b7uk t328 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t327) + 0));
                    gm_b16_8 t329 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t326, t328);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l43 = t329;
                }
                {
                    void * t330 = gm_l0;
                    void * t331 = GM_LD(void *, (((uint8_t *)t330) + 0));
                    void * t332 = GM_LD(void *, (((uint8_t *)t331) + 0));
                    void * t333 = GM_LD(void *, (((uint8_t *)t332) + 0));
                    void * t334 = gm_l22;
                    gm_t9805qtonbsag t335 = gm_f_root_local_40frg7f(t333, t334);
                    gm_l44 = t335;
                }
                {
                    void * t336 = gm_l0;
                    void * t337 = gm_l23;
                    gm_t6j6smj7qoctu t338;
                    gm_b16_8 t339 = gm_l43;
                    GM_ST(gm_b16_8, (uint8_t *)&t338 + 0, t339);
                    gm_b16_8 t340 = gm_l42;
                    GM_ST(gm_b16_8, (uint8_t *)&t338 + 16, t340);
                    gm_tabb710d9mq2e t341;
                    gm_f_format_8lfevbr((void *)&t341, (void *)NULL, cx, NULL, &t338);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tabb710d9mq2e t342 = t341;
                    if (((((uint8_t *)&t342))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t342) + 8, gm_srcmok80ho8hh01, 1083, 41);
                    gm_b16_8 t343 = GM_LD(gm_b16_8, (((uint8_t *)&t342) + 8));
                    gm_b16_8 t344 = t343;
                    gm_b16_8 t345 = GM_LD(gm_b16_8, ((uint8_t *)&t344));
                    gm_t9805qtonbsag t346 = gm_l44;
                    gm_tnrltbc61i6v1 t347;
                    GM_ST(uint8_t, (uint8_t *)&t347 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t347 + 2, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t347 + 0), ((uint8_t)2ULL));
                    GM_ST(gm_t9805qtonbsag, ((uint8_t *)&t347 + 4), t346);
                    gm_tqgtd5gtgj65s t348;
                    gm_f_expr_into_dn9kavo((void *)&t348, (void *)NULL, cx, NULL, t336, t337, t345, t347);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tqgtd5gtgj65s t349 = t348;
                    if (((((uint8_t *)&t349))[0] != 0)) {
                        gm_t08olq97rscn3 t350 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t349) + 8));
                        gm_tqgtd5gtgj65s t351;
                        GM_ST(uint8_t, (uint8_t *)&t351 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t351 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t351 + 4, 0);
                        ((uint8_t *)&t351)[0] = 1;
                        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t351 + 8), t350);
                        F.ret = t351;
                        goto gm_fret;
                    }
                    gm_b0_1 t352 = GM_LD(gm_b0_1, (((uint8_t *)&t349) + 8));
                }
                {
                    gm_b0_1 t353;
                    gm_tqgtd5gtgj65s t354;
                    memset(&t354, 0, sizeof t354);
                    ((uint8_t *)&t354)[0] = 0;
                    GM_ST(gm_b0_1, ((uint8_t *)&t354 + 8), t353);
                    gm_tqgtd5gtgj65s t355 = t354;
                    uint8_t t356 = ((((uint8_t *)&t355))[0] != 0);
                    F.ret = t354;
                    goto gm_fret;
                }
                goto ix311;
ie311:;
ix311:;
            }
            {
                void * t358 = gm_l22;
                uint8_t t359 = GM_LD(uint8_t, ((((uint8_t *)t358) + 8) + 0));
                if (t359 != ((uint8_t)15ULL)) goto ie357;
                uint32_t t360 = GM_LD(uint32_t, ((((uint8_t *)t358) + 8) + 8));
                gm_l45 = t360;
                {
                    void * t363 = gm_l23;
                    gm_t88rcucg1b7uk t364 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t363) + 0));
                    void * t365 = gm_l22;
                    gm_t88rcucg1b7uk t366 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t365) + 0));
                    gm_t88rcucg1b7uk t367 = t364;
                    gm_t88rcucg1b7uk t368 = t366;
                    uint8_t t369 = (gm_eq9m6mvtmgde(((uint8_t *)&t367), ((uint8_t *)&t368)));
                    if (!t369) goto ie362;
                    uint32_t t370 = gm_l45;
                    uint64_t t371 = (uint64_t)t370;
                    uint64_t t372 = (uintptr_t)t371;
                    void * t373 = gm_l0;
                    void * t374 = GM_LD(void *, (((uint8_t *)t373) + 176));
                    uint64_t t375 = GM_LD(uintptr_t, (((uint8_t *)t374) + 48) + 8);
                    if (t372 >= (uintptr_t)(t375)) gm_trap_index(t372, t375, gm_srcmok80ho8hh01, 1088, 45);
                    gm_t88rcucg1b7uk t376 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, (((uint8_t *)t374) + 48)) + t372 * 40) + 16));
                    void * t377 = gm_l22;
                    gm_t88rcucg1b7uk t378 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t377) + 0));
                    gm_t88rcucg1b7uk t379 = t376;
                    gm_t88rcucg1b7uk t380 = t378;
                    uint8_t t381 = (gm_eq9m6mvtmgde(((uint8_t *)&t379), ((uint8_t *)&t380)));
                    if (!t381) goto ie362;
                    void * t382 = gm_l0;
                    uint32_t t383 = gm_l45;
                    uint8_t t384 = gm_f_is_borrowed_qi4apdt(t382, t383);
                    uint8_t t385 = (uint8_t)!t384;
                    if (!t385) goto ie362;
                    {
                        void * t387 = gm_l0;
                        void * t388 = gm_l23;
                        gm_t5o0tpv5jka2l t389;
                        gm_f_expr_porbc9t((void *)&t389, (void *)NULL, cx, NULL, t387, t388);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_t5o0tpv5jka2l t390 = t389;
                        if (((((uint8_t *)&t390))[0] != 0)) {
                            gm_t08olq97rscn3 t391 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t390) + 8));
                            gm_tqgtd5gtgj65s t392;
                            GM_ST(uint8_t, (uint8_t *)&t392 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t392 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t392 + 4, 0);
                            ((uint8_t *)&t392)[0] = 1;
                            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t392 + 8), t391);
                            F.ret = t392;
                            goto gm_fret;
                        }
                        gm_b16_8 t393 = GM_LD(gm_b16_8, (((uint8_t *)&t390) + 8));
                        gm_l46 = t393;
                    }
                    {
                        void * t394 = gm_l0;
                        uint32_t t395 = gm_l45;
                        gm_b16_8 t396 = gm_f_local_lv_m2jtf0n((void *)NULL, cx, NULL, t394, t395);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l47 = t396;
                    }
                    {
                        void * t397 = gm_l0;
                        gm_t6j6smj7qoctu t398;
                        gm_b16_8 t399 = gm_l47;
                        GM_ST(gm_b16_8, (uint8_t *)&t398 + 0, t399);
                        gm_b16_8 t400 = gm_l46;
                        GM_ST(gm_b16_8, (uint8_t *)&t398 + 16, t400);
                        gm_b0_1 t401 = gm_f_linef_uu8i6ue((void *)NULL, cx, NULL, t397, &t398);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    {
                        gm_b0_1 t402;
                        gm_tqgtd5gtgj65s t403;
                        memset(&t403, 0, sizeof t403);
                        ((uint8_t *)&t403)[0] = 0;
                        GM_ST(gm_b0_1, ((uint8_t *)&t403 + 8), t402);
                        gm_tqgtd5gtgj65s t404 = t403;
                        uint8_t t405 = ((((uint8_t *)&t404))[0] != 0);
                        F.ret = t403;
                        goto gm_fret;
                    }
                    goto ix362;
ie362:;
ix362:;
                }
                goto ix357;
ie357:;
ix357:;
            }
            {
                void * t406 = gm_l0;
                void * t407 = gm_l22;
                void * t408 = gm_l23;
                gm_t5o0tpv5jka2l t409;
                gm_f_assign_addr_rlofoa5((void *)&t409, (void *)NULL, cx, NULL, t406, t407, t408);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t5o0tpv5jka2l t410 = t409;
                if (((((uint8_t *)&t410))[0] != 0)) {
                    gm_t08olq97rscn3 t411 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t410) + 8));
                    gm_tqgtd5gtgj65s t412;
                    GM_ST(uint8_t, (uint8_t *)&t412 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t412 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t412 + 4, 0);
                    ((uint8_t *)&t412)[0] = 1;
                    GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t412 + 8), t411);
                    F.ret = t412;
                    goto gm_fret;
                }
                gm_b16_8 t413 = GM_LD(gm_b16_8, (((uint8_t *)&t410) + 8));
                gm_l48 = t413;
            }
            {
                void * t414 = gm_l0;
                void * t415 = gm_l23;
                gm_t5o0tpv5jka2l t416;
                gm_f_expr_porbc9t((void *)&t416, (void *)NULL, cx, NULL, t414, t415);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t5o0tpv5jka2l t417 = t416;
                if (((((uint8_t *)&t417))[0] != 0)) {
                    gm_t08olq97rscn3 t418 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t417) + 8));
                    gm_tqgtd5gtgj65s t419;
                    GM_ST(uint8_t, (uint8_t *)&t419 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t419 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t419 + 4, 0);
                    ((uint8_t *)&t419)[0] = 1;
                    GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t419 + 8), t418);
                    F.ret = t419;
                    goto gm_fret;
                }
                gm_b16_8 t420 = GM_LD(gm_b16_8, (((uint8_t *)&t417) + 8));
                gm_l49 = t420;
            }
            {
                void * t421 = gm_l0;
                void * t422 = GM_LD(void *, (((uint8_t *)t421) + 0));
                void * t423 = (void *)(((uint8_t *)t422) + 8);
                void * t424 = gm_l22;
                gm_t88rcucg1b7uk t425 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t424) + 0));
                gm_b16_8 t426 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t423, t425);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l50 = t426;
            }
            {
                void * t427 = gm_l0;
                gm_tfeab279c16rr t428;
                gm_b16_8 t429 = gm_l50;
                GM_ST(gm_b16_8, (uint8_t *)&t428 + 0, t429);
                gm_b16_8 t430 = gm_l48;
                GM_ST(gm_b16_8, (uint8_t *)&t428 + 16, t430);
                gm_b16_8 t431 = gm_l49;
                GM_ST(gm_b16_8, (uint8_t *)&t428 + 32, t431);
                gm_b0_1 t432 = gm_f_linef_f1jrb6n((void *)NULL, cx, NULL, t427, &t428);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto me4;
mn162:;
ma8:;
            uint8_t t434 = GM_LD(uint8_t, (((uint8_t *)t3) + 0));
            if (t434 != ((uint8_t)3ULL)) goto mn433;
            gm_b16_8 t435 = GM_LD(gm_b16_8, ((((uint8_t *)t3) + 8) + 0));
            gm_l51 = t435;
            void * t436 = GM_LD(void *, ((((uint8_t *)t3) + 8) + 16));
            gm_l52 = t436;
            {
                gm_f_list_0c2v515((void *)&F.l53, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                {
                    gm_b16_8 t439 = gm_l51;
                    gm_l54 = t439;
                }
                {
                    gm_l55 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t441 = gm_l55;
                        uint64_t t442 = GM_LD(uintptr_t, ((uint8_t *)&gm_l54) + 8);
                        uint8_t t443 = (t441 < t442);
                        if (!t443) goto lb440;
                        {
                            uint64_t t446 = gm_l55;
                            uint64_t t447 = (uintptr_t)t446;
                            uint64_t t448 = GM_LD(uintptr_t, ((uint8_t *)&gm_l54) + 8);
                            void * t449 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l54)) + t447 * 8));
                            void * t450 = t449;
                            void * t453 = GM_LD(void *, ((uint8_t *)&t450));
                            gm_l56 = t453;
                            {
                                void * t456 = gm_l56;
                                void * t457 = t456;
                                if (!(GM_LD(void *, ((uint8_t *)&t457)) != 0)) goto ie455;
                                void * t458 = GM_LD(void *, ((uint8_t *)&t457));
                                gm_l57 = t458;
                                {
                                    void * t460 = gm_l0;
                                    void * t461 = gm_l57;
                                    void * t462 = gm_l52;
                                    gm_t5o0tpv5jka2l t463;
                                    gm_f_assign_addr_rlofoa5((void *)&t463, (void *)NULL, cx, NULL, t460, t461, t462);
                                    if (gm_unw.kind) {
                                        goto gm_ll444;
                                    }
                                    gm_t5o0tpv5jka2l t464 = t463;
                                    if (((((uint8_t *)&t464))[0] != 0)) {
                                        gm_t08olq97rscn3 t465 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t464) + 8));
                                        gm_tqgtd5gtgj65s t466;
                                        GM_ST(uint8_t, (uint8_t *)&t466 + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t466 + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t466 + 4, 0);
                                        ((uint8_t *)&t466)[0] = 1;
                                        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t466 + 8), t465);
                                        F.ret = t466;
                                        goto gm_fret;
                                    }
                                    gm_b16_8 t467 = GM_LD(gm_b16_8, (((uint8_t *)&t464) + 8));
                                    gm_l58 = t467;
                                }
                                {
                                    void * t468 = (void *)((uint8_t *)&F.l53);
                                    gm_b16_8 t469 = gm_l58;
                                    gm_th2lqbo77pvsc t470;
                                    GM_ST(uint8_t, (uint8_t *)&t470 + 17, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t470 + 18, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t470 + 20, 0);
                                    GM_ST(gm_b16_8, ((uint8_t *)&t470), t469);
                                    ((uint8_t *)&t470)[16] = 1;
                                    gm_tftb1odc739t5 t471 = gm_f_push_d0271rf(t468, &t470);
                                    gm_tftb1odc739t5 t472 = t471;
                                    if (((((uint8_t *)&t472))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t472) + 1, gm_srcmok80ho8hh01, 1105, 21);
                                    gm_b0_1 t473 = GM_LD(gm_b0_1, (((uint8_t *)&t472) + 1));
                                }
                                goto ix455;
ie455:;
                                {
                                    void * t475 = (void *)((uint8_t *)&F.l53);
                                    gm_th2lqbo77pvsc t476;
                                    memset(&t476, 0, sizeof t476);
                                    gm_tftb1odc739t5 t477 = gm_f_push_d0271rf(t475, &t476);
                                    gm_tftb1odc739t5 t478 = t477;
                                    if (((((uint8_t *)&t478))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t478) + 1, gm_srcmok80ho8hh01, 1107, 21);
                                    gm_b0_1 t479 = GM_LD(gm_b0_1, (((uint8_t *)&t478) + 1));
                                }
ix455:;
                            }
                            goto me451;
mn452:;
                            gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 1102, 13);
me451:;
                        }
lc440:;
                        {
                            uint64_t t481 = gm_l55;
                            uint64_t r482; bool o483 = __builtin_add_overflow(t481, ((uint64_t)1ULL), &r482);
                            uint64_t t484 = r482;
                            gm_l55 = t484;
                        }
                    }
gm_ll444:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb440; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc440; }
                    }
                    goto gm_lad;
lb440:;
                }
            }
            {
                void * t485 = gm_l0;
                void * t486 = gm_l52;
                gm_t5o0tpv5jka2l t487;
                gm_f_expr_porbc9t((void *)&t487, (void *)NULL, cx, NULL, t485, t486);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t5o0tpv5jka2l t488 = t487;
                if (((((uint8_t *)&t488))[0] != 0)) {
                    gm_t08olq97rscn3 t489 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t488) + 8));
                    gm_tqgtd5gtgj65s t490;
                    GM_ST(uint8_t, (uint8_t *)&t490 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t490 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t490 + 4, 0);
                    ((uint8_t *)&t490)[0] = 1;
                    GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t490 + 8), t489);
                    F.ret = t490;
                    goto gm_fret;
                }
                gm_b16_8 t491 = GM_LD(gm_b16_8, (((uint8_t *)&t488) + 8));
                gm_l59 = t491;
            }
            {
                void * t492 = gm_l0;
                void * t493 = gm_l52;
                gm_t88rcucg1b7uk t494 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t493) + 0));
                gm_b16_8 t495 = gm_l59;
                gm_b16_8 t496 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t492, t494, t495);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l60 = t496;
            }
            {
                void * t497 = gm_l0;
                void * t498 = GM_LD(void *, (((uint8_t *)t497) + 0));
                void * t499 = (void *)(((uint8_t *)t498) + 8);
                void * t500 = gm_l52;
                gm_t88rcucg1b7uk t501 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t500) + 0));
                gm_b16_8 t502 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t499, t501);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l61 = t502;
            }
            {
                {
                    gm_l63 = ((uint64_t)0ULL);
                }
                {
                    uint64_t t504 = GM_LD(uintptr_t, (((uint8_t *)&F.l53) + 0) + 8);
                    gm_l62 = t504;
                }
                {
                    for (;;) {
                        uint64_t t506 = gm_l63;
                        uint64_t t507 = gm_l62;
                        uint8_t t508 = (t506 < t507);
                        if (!t508) goto lb505;
                        {
                            uint64_t t511 = gm_l63;
                            uint64_t t512 = t511;
                            uint64_t t515 = GM_LD(uint64_t, ((uint8_t *)&t512));
                            gm_l64 = t515;
                            {
                                uint64_t t518 = gm_l64;
                                uint64_t t519 = (uintptr_t)t518;
                                uint64_t t520 = GM_LD(uintptr_t, (((uint8_t *)&F.l53) + 0) + 8);
                                if (t519 >= (uintptr_t)(t520)) gm_trap_index(t519, t520, gm_srcmok80ho8hh01, 1114, 25);
                                if (!((((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l53) + 0)) + t519 * 24))[16] != 0)) goto ie517;
                                gm_b16_8 t521 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l53) + 0)) + t519 * 24));
                                gm_l65 = t521;
                                {
                                    uint64_t t524 = gm_l64;
                                    uint64_t t525 = (uintptr_t)t524;
                                    uint64_t t526 = GM_LD(uintptr_t, ((uint8_t *)&gm_l51) + 8);
                                    if (t525 >= (uintptr_t)(t526)) gm_trap_index(t525, t526, gm_srcmok80ho8hh01, 1115, 30);
                                    void * t527 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l51)) + t525 * 8));
                                    void * t528 = t527;
                                    if (!(GM_LD(void *, ((uint8_t *)&t528)) != 0)) goto ie523;
                                    void * t529 = GM_LD(void *, ((uint8_t *)&t528));
                                    gm_l66 = t529;
                                    {
                                        void * t531 = gm_l0;
                                        void * t532 = GM_LD(void *, (((uint8_t *)t531) + 0));
                                        void * t533 = (void *)(((uint8_t *)t532) + 8);
                                        void * t534 = gm_l66;
                                        gm_t88rcucg1b7uk t535 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t534) + 0));
                                        gm_b16_8 t536 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t533, t535);
                                        if (gm_unw.kind) {
                                            goto gm_ll509;
                                        }
                                        gm_l67 = t536;
                                    }
                                    {
                                        void * t537 = gm_l0;
                                        gm_t0j6181meiu4s t538;
                                        gm_b16_8 t539 = gm_l67;
                                        GM_ST(gm_b16_8, (uint8_t *)&t538 + 0, t539);
                                        gm_b16_8 t540 = gm_l65;
                                        GM_ST(gm_b16_8, (uint8_t *)&t538 + 16, t540);
                                        gm_b16_8 t541 = gm_l67;
                                        GM_ST(gm_b16_8, (uint8_t *)&t538 + 32, t541);
                                        gm_b16_8 t542 = gm_l60;
                                        GM_ST(gm_b16_8, (uint8_t *)&t538 + 48, t542);
                                        uint64_t t543 = gm_l64;
                                        uint64_t t544 = (uintptr_t)t543;
                                        uint64_t t545 = GM_LD(uintptr_t, ((uint8_t *)&gm_l61) + 8);
                                        if (t544 >= (uintptr_t)(t545)) gm_trap_index(t544, t545, gm_srcmok80ho8hh01, 1117, 96);
                                        uint64_t t546 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l61)) + t544 * 8));
                                        GM_ST(uint64_t, (uint8_t *)&t538 + 64, t546);
                                        gm_b0_1 t547 = gm_f_linef_5n29svs((void *)NULL, cx, NULL, t537, &t538);
                                        if (gm_unw.kind) {
                                            goto gm_ll509;
                                        }
                                    }
                                    goto ix523;
ie523:;
ix523:;
                                }
                                goto ix517;
ie517:;
ix517:;
                            }
                            goto me513;
mn514:;
                            gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 1113, 13);
me513:;
                        }
lc505:;
                        {
                            uint64_t t549 = gm_l63;
                            uint64_t r550; bool o551 = __builtin_add_overflow(t549, ((uint64_t)1ULL), &r550);
                            uint64_t t552 = r550;
                            gm_l63 = t552;
                        }
                    }
gm_ll509:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 3) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb505; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc505; }
                    }
                    goto gm_lad;
lb505:;
                }
            }
            goto me4;
mn433:;
ma9:;
            uint8_t t554 = GM_LD(uint8_t, (((uint8_t *)t3) + 0));
            if (t554 != ((uint8_t)4ULL)) goto mn553;
            void * t555 = GM_LD(void *, (((uint8_t *)t3) + 8));
            gm_l68 = t555;
            {
                {
                    void * t558 = gm_l0;
                    void * t559 = gm_l68;
                    gm_t5o0tpv5jka2l t560;
                    gm_f_expr_porbc9t((void *)&t560, (void *)NULL, cx, NULL, t558, t559);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t5o0tpv5jka2l t561 = t560;
                    if (((((uint8_t *)&t561))[0] != 0)) {
                        gm_t08olq97rscn3 t562 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t561) + 8));
                        gm_tqgtd5gtgj65s t563;
                        GM_ST(uint8_t, (uint8_t *)&t563 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t563 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t563 + 4, 0);
                        ((uint8_t *)&t563)[0] = 1;
                        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t563 + 8), t562);
                        F.ret = t563;
                        goto gm_fret;
                    }
                    gm_b16_8 t564 = GM_LD(gm_b16_8, (((uint8_t *)&t561) + 8));
                }
            }
            goto me4;
mn553:;
ma10:;
            uint8_t t566 = GM_LD(uint8_t, (((uint8_t *)t3) + 0));
            if (t566 != ((uint8_t)5ULL)) goto mn565;
            uint8_t t567 = GM_LD(uint8_t, ((((uint8_t *)t3) + 8) + 0));
            gm_l69 = t567;
            void * t568 = GM_LD(void *, ((((uint8_t *)t3) + 8) + 8));
            gm_l70 = t568;
            {
                void * t570 = gm_l0;
                gm_trqfvmjtt4sij t571 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)t570) + 184));
                F.l71 = t571;
            }
            {
                void * t572 = gm_l0;
                uint8_t *t573 = (((uint8_t *)t572) + 184);
                gm_f_list_uvfu1k1((void *)&(*(gm_trqfvmjtt4sij *)t573), (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t574 = gm_l0;
                uint8_t t575 = GM_LD(uint8_t, (((uint8_t *)t574) + 348));
                gm_l72 = t575;
            }
            {
                void * t576 = gm_l0;
                GM_ST(uint8_t, (((uint8_t *)t576) + 348), ((uint8_t)0));
            }
            {
                void * t577 = gm_l0;
                gm_f_set_aside_scopes_ho12if8((void *)&F.l73, (void *)NULL, cx, NULL, t577);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t578 = gm_l0;
                void * t579 = gm_l70;
                gm_f_expr_porbc9t((void *)&F.l74, (void *)NULL, cx, NULL, t578, t579);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t580 = gm_l0;
                gm_f_restore_scopes_f62992p(t580, &(*(gm_td7pkamr64l0c *)((uint8_t *)&F.l73)));
            }
            {
                void * t581 = gm_l0;
                uint8_t t582 = gm_l72;
                GM_ST(uint8_t, (((uint8_t *)t581) + 348), t582);
            }
            {
                void * t583 = gm_l0;
                gm_trqfvmjtt4sij t584 = F.l71;
                GM_ST(gm_trqfvmjtt4sij, (((uint8_t *)t583) + 184), t584);
            }
            {
                {
                    gm_t5o0tpv5jka2l t586 = F.l74;
                    gm_t5o0tpv5jka2l t587 = t586;
                    if (((((uint8_t *)&t587))[0] != 0)) {
                        gm_t08olq97rscn3 t588 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t587) + 8));
                        gm_tqgtd5gtgj65s t589;
                        GM_ST(uint8_t, (uint8_t *)&t589 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t589 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t589 + 4, 0);
                        ((uint8_t *)&t589)[0] = 1;
                        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t589 + 8), t588);
                        F.ret = t589;
                        goto gm_fret;
                    }
                    gm_b16_8 t590 = GM_LD(gm_b16_8, (((uint8_t *)&t587) + 8));
                }
            }
            {
                void * t592 = gm_l0;
                uint64_t t593 = GM_LD(uintptr_t, ((((uint8_t *)t592) + 240) + 0) + 8);
                uint8_t t594 = (t593 > ((uint64_t)0ULL));
                if (!t594) goto ie591;
                {
                    void * t596 = gm_l0;
                    uint64_t t597 = GM_LD(uintptr_t, ((((uint8_t *)t596) + 240) + 0) + 8);
                    uint64_t r598; bool o599 = __builtin_sub_overflow(t597, ((uint64_t)1ULL), &r598);
                    if (o599) gm_trap("integer overflow in `-`", gm_srcmok80ho8hh01, 1138, 41);
                    uint64_t t600 = r598;
                    uint64_t t601 = (uintptr_t)t600;
                    void * t602 = gm_l0;
                    uint64_t t603 = GM_LD(uintptr_t, ((((uint8_t *)t602) + 240) + 0) + 8);
                    if (t601 >= (uintptr_t)(t603)) gm_trap_index(t601, t603, gm_srcmok80ho8hh01, 1138, 25);
                    void * t604 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t602) + 240) + 0)) + t601 * 104);
                    gm_l75 = t604;
                }
                {
                    void * t606 = gm_l75;
                    uint8_t t607 = ((GM_LD(uint8_t, (((uint8_t *)t606) + 0) + 0) == ((uint8_t)0ULL)));
                    if (!t607) goto ie605;
                    {
                        void * t609 = gm_l75;
                        void * t610 = (void *)(((uint8_t *)t609) + 56);
                        uint8_t t611 = gm_l69;
                        void * t612 = gm_l70;
                        gm_tgtub0v94d5u5 t613;
                        GM_ST(uint8_t, (uint8_t *)&t613 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t613 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t613 + 4, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t613 + 0), t611);
                        GM_ST(void *, ((uint8_t *)&t613 + 8), t612);
                        gm_tftb1odc739t5 t614 = gm_f_push_2lk6rs2(t610, GM_CONV(gm_csn5gfg3c8ooaj, t613));
                        gm_tftb1odc739t5 t615 = t614;
                        if (((((uint8_t *)&t615))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t615) + 1, gm_srcmok80ho8hh01, 1139, 41);
                        gm_b0_1 t616 = GM_LD(gm_b0_1, (((uint8_t *)&t615) + 1));
                    }
                    goto ix605;
ie605:;
ix605:;
                }
                goto ix591;
ie591:;
ix591:;
            }
            goto me4;
mn565:;
ma11:;
            gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 1014, 5);
me4:;
        }
    }
    gm_b0_1 t617;
    gm_tqgtd5gtgj65s t618;
    memset(&t618, 0, sizeof t618);
    ((uint8_t *)&t618)[0] = 0;
    GM_ST(gm_b0_1, ((uint8_t *)&t618 + 8), t617);
    gm_tqgtd5gtgj65s t619 = t618;
    F.ret = t619;
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
    GM_STACK_CHECK(&F, 228u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l1;
        gm_toceffb697p0u t4 = gm_f_stmt_span_d0pel0p(t3);
        gm_toceffb697p0u t5 = t4;
        if (!((((uint8_t *)&t5))[12] != 0)) goto ie2;
        gm_tl874t2atnlh3 t6 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)&t5));
        gm_l2 = t6;
        {
            void * t8 = gm_l0;
            gm_tl874t2atnlh3 t9 = gm_l2;
            gm_b0_1 t10 = gm_f_src_line_d24sajl((void *)NULL, cx, NULL, t8, t9);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t11 = gm_l0;
        gm_b16_8 t12;
        GM_ST(void *, (uint8_t *)&t12, (void *)(gm_sh40na9qbga3l));
        GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)(1));
        gm_b0_1 t13 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t11, t12);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t14 = gm_l0;
        void * t15 = gm_l0;
        uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)t15) + 224));
        uint64_t r17; bool o18 = __builtin_add_overflow(t16, ((uint64_t)1ULL), &r17);
        if (o18) gm_trap("integer overflow in `+`", gm_srcmok80ho8hh01, 1006, 5);
        uint64_t t19 = r17;
        GM_ST(uint64_t, (((uint8_t *)t14) + 224), t19);
    }
    {
        void * t20 = gm_l0;
        void * t21 = gm_l1;
        gm_f_stmt_1t29bfr((void *)&F.l3, (void *)NULL, cx, NULL, t20, t21);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t22 = gm_l0;
        void * t23 = gm_l0;
        uint64_t t24 = GM_LD(uint64_t, (((uint8_t *)t23) + 224));
        uint64_t r25; bool o26 = __builtin_sub_overflow(t24, ((uint64_t)1ULL), &r25);
        if (o26) gm_trap("integer overflow in `-`", gm_srcmok80ho8hh01, 1008, 5);
        uint64_t t27 = r25;
        GM_ST(uint64_t, (((uint8_t *)t22) + 224), t27);
    }
    {
        void * t28 = gm_l0;
        gm_b16_8 t29;
        GM_ST(void *, (uint8_t *)&t29, (void *)(gm_s2cuflhqkkuaq));
        GM_ST(uintptr_t, (uint8_t *)&t29 + 8, (uintptr_t)(1));
        gm_b0_1 t30 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t28, t29);
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
    GM_STACK_CHECK_SHALLOW(&F, 510u);
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
                        void * t54 = t53;
                        void * t57 = GM_LD(void *, ((uint8_t *)&t54));
                        gm_l9 = t57;
                        {
                            void * t60 = gm_l9;
                            void * t61 = t60;
                            if (!(GM_LD(void *, ((uint8_t *)&t61)) != 0)) goto ie59;
                            void * t62 = GM_LD(void *, ((uint8_t *)&t61));
                            gm_l10 = t62;
                            {
                                void * t64 = gm_l10;
                                gm_tl874t2atnlh3 t65 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t64) + 72));
                                gm_toceffb697p0u t66;
                                GM_ST(uint8_t, (uint8_t *)&t66 + 13, 0);
                                GM_ST(uint16_t, (uint8_t *)&t66 + 14, 0);
                                GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t66), t65);
                                ((uint8_t *)&t66)[12] = 1;
                                uint8_t t67 = 0;
                                F.ret = t66;
                                goto gm_fret;
                            }
                            goto ix59;
ie59:;
ix59:;
                        }
                        goto me55;
mn56:;
                        gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 406, 13);
me55:;
                    }
lc44:;
                    {
                        uint64_t t69 = gm_l8;
                        uint64_t r70; bool o71 = __builtin_add_overflow(t69, ((uint64_t)1ULL), &r70);
                        uint64_t t72 = r70;
                        gm_l8 = t72;
                    }
                }
lb44:;
            }
        }
        {
            void * t73 = gm_l6;
            gm_tl874t2atnlh3 t74 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t73) + 72));
            gm_toceffb697p0u t75;
            GM_ST(uint8_t, (uint8_t *)&t75 + 13, 0);
            GM_ST(uint16_t, (uint8_t *)&t75 + 14, 0);
            GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t75), t74);
            ((uint8_t *)&t75)[12] = 1;
            uint8_t t76 = 0;
            F.ret = t75;
            goto gm_fret;
        }
        goto me3;
mn37:;
ma8:;
        gm_toceffb697p0u t78;
        memset(&t78, 0, sizeof t78);
        uint8_t t79 = 0;
        F.ret = t78;
        goto gm_fret;
        goto me3;
mn77:;
ma9:;
        gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 401, 5);
me3:;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) void gm_f_stmts_1199hll(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
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
        if (o12) gm_trap("integer overflow in `+`", gm_srcmok80ho8hh01, 476, 5);
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
        if (o12) gm_trap("integer overflow in `+`", gm_srcmok80ho8hh01, 467, 5);
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
        if (o6) gm_trap("integer overflow in `+`", gm_srcmok80ho8hh01, 594, 5);
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
        if (o14) gm_trap("integer overflow in `-`", gm_srcmok80ho8hh01, 596, 5);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_zero_tlk13aa(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_zero_tlk13aa F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* cty */
    GM_STACK_CHECK(&F, 444u);
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
        gm_t5v8n6qrn4rh4 t13 = t12;
        uint8_t t14;
        uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)&t13) + 0));
        switch (t22) { case ((uint8_t)0ULL): goto ma16; case ((uint8_t)1ULL): goto ma17; case ((uint8_t)2ULL): goto ma18; case ((uint8_t)3ULL): goto ma19; case ((uint8_t)4ULL): goto ma20; case ((uint8_t)5ULL): goto ma20; case ((uint8_t)6ULL): goto ma20; default: goto ma20; }
ma16:;
        uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)&t13) + 0));
        if (t24 != ((uint8_t)0ULL)) goto mn23;
        t14 = ((uint8_t)1);
        goto me15;
mn23:;
ma17:;
        uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&t13) + 0));
        if (t26 != ((uint8_t)1ULL)) goto mn25;
        t14 = ((uint8_t)1);
        goto me15;
mn25:;
ma18:;
        uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)&t13) + 0));
        if (t28 != ((uint8_t)2ULL)) goto mn27;
        t14 = ((uint8_t)1);
        goto me15;
mn27:;
ma19:;
        uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)&t13) + 0));
        if (t30 != ((uint8_t)3ULL)) goto mn29;
        t14 = ((uint8_t)1);
        goto me15;
mn29:;
ma20:;
        t14 = ((uint8_t)0);
        goto me15;
mn31:;
ma21:;
        gm_trap("no match arm matched the scrutinee", gm_srcmok80ho8hh01, 500, 8);
me15:;
        if (!t14) goto ie7;
        {
            gm_b16_8 t33 = gm_l2;
            gm_tnpifv20irnph t34;
            GM_ST(gm_b16_8, ((uint8_t *)&t34 + 0), t33);
            gm_tabb710d9mq2e t35;
            gm_f_format_07f8le4((void *)&t35, (void *)NULL, cx, NULL, t34);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t36 = t35;
            if (((((uint8_t *)&t36))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t36) + 8, gm_srcmok80ho8hh01, 500, 76);
            gm_b16_8 t37 = GM_LD(gm_b16_8, (((uint8_t *)&t36) + 8));
            gm_b16_8 t38 = t37;
            gm_b16_8 t39 = GM_LD(gm_b16_8, ((uint8_t *)&t38));
            uint8_t t40 = 0;
            F.ret = t39;
            goto gm_fret;
        }
        goto ix7;
ie7:;
ix7:;
    }
    {
        gm_b16_8 t41 = gm_l2;
        gm_tnpifv20irnph t42;
        GM_ST(gm_b16_8, ((uint8_t *)&t42 + 0), t41);
        gm_tabb710d9mq2e t43;
        gm_f_format_loh175m((void *)&t43, (void *)NULL, cx, NULL, t42);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t44 = t43;
        if (((((uint8_t *)&t44))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t44) + 8, gm_srcmok80ho8hh01, 501, 12);
        gm_b16_8 t45 = GM_LD(gm_b16_8, (((uint8_t *)&t44) + 8));
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

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
typedef struct gm_csguankko4lmhd { uint32_t f0; } gm_csguankko4lmhd;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { void * f0; void * f1; void * f2; } gm_tbbqefuhv2r8h;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint32_t f2; uint32_t f3; } gm_tmm67r2c94d5j;
_Static_assert(sizeof(gm_cseahvg8v7p0vs) == 12 && _Alignof(gm_cseahvg8v7p0vs) == 4 && offsetof(gm_cseahvg8v7p0vs, f0) == 0 && offsetof(gm_cseahvg8v7p0vs, f1) == 4 && offsetof(gm_cseahvg8v7p0vs, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tao63pk3r18v5) == 32 && _Alignof(gm_tao63pk3r18v5) == 8 && offsetof(gm_tao63pk3r18v5, f0) == 0 && offsetof(gm_tao63pk3r18v5, f1) == 8 && offsetof(gm_tao63pk3r18v5, f2) == 16 && offsetof(gm_tao63pk3r18v5, f3) == 24, "layout of `SliceExpr`");
_Static_assert(sizeof(gm_tbbqefuhv2r8h) == 24 && _Alignof(gm_tbbqefuhv2r8h) == 8 && offsetof(gm_tbbqefuhv2r8h, f0) == 0 && offsetof(gm_tbbqefuhv2r8h, f1) == 8 && offsetof(gm_tbbqefuhv2r8h, f2) == 16, "layout of `Arm`");
typedef struct { uint64_t f0; uint64_t f1; gm_b16_8 f2; } gm_tbccvikg6b96e;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tbgd14esf0jvi;
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_b16_8 f2; void * f3; gm_t9805qtonbsag f4; uint8_t f5; uint8_t pe[7]; } gm_tdgfduqjheqdm;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_b16_8 f0; void * f1; void * f2; } gm_trl2khmuphncd;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_th2lqbo77pvsc f6; } gm_t4gut2c9fj1kt;
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tbccvikg6b96e) == 32 && _Alignof(gm_tbccvikg6b96e) == 8 && offsetof(gm_tbccvikg6b96e, f0) == 0 && offsetof(gm_tbccvikg6b96e, f1) == 8 && offsetof(gm_tbccvikg6b96e, f2) == 16, "layout of `Layout`");
_Static_assert(sizeof(gm_tbgd14esf0jvi) == 32 && _Alignof(gm_tbgd14esf0jvi) == 8 && offsetof(gm_tbgd14esf0jvi, f0) == 0 && offsetof(gm_tbgd14esf0jvi, f1) == 16, "layout of `Edit`");
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_tdgfduqjheqdm) == 64 && _Alignof(gm_tdgfduqjheqdm) == 8 && offsetof(gm_tdgfduqjheqdm, f0) == 0 && offsetof(gm_tdgfduqjheqdm, f1) == 8 && offsetof(gm_tdgfduqjheqdm, f2) == 24 && offsetof(gm_tdgfduqjheqdm, f3) == 40 && offsetof(gm_tdgfduqjheqdm, f4) == 48 && offsetof(gm_tdgfduqjheqdm, f5) == 56, "layout of `FnDef`");
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `Toolchain`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t p2[7]; gm_tf5srh4mp4b3i f2; gm_tkn42u7na3t81 f3; } gm_tpautr4br7h3l;
_Static_assert(sizeof(gm_trl2khmuphncd) == 32 && _Alignof(gm_trl2khmuphncd) == 8 && offsetof(gm_trl2khmuphncd, f0) == 0 && offsetof(gm_trl2khmuphncd, f1) == 16 && offsetof(gm_trl2khmuphncd, f2) == 24, "layout of `IfExpr`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_tpautr4br7h3l) == 48 && _Alignof(gm_tpautr4br7h3l) == 8 && offsetof(gm_tpautr4br7h3l, f0) == 0 && offsetof(gm_tpautr4br7h3l, f1) == 16 && offsetof(gm_tpautr4br7h3l, f2) == 24 && offsetof(gm_tpautr4br7h3l, f3) == 40, "layout of `StructDef`");
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
struct gm_fr_check_escapes_brb2os4 {
    gm_frame h;
    void * l0; /* s */
    gm_trqfvmjtt4sij l1; /* found */
    gm_tdgfduqjheqdm l4; /* def */
    gm_tdgfduqjheqdm l8; /* def */
    gm_trqfvmjtt4sij l10; /* seen */
    gm_b0_1 ret;
};
struct gm_fr_empty_slice_bvqfhnb {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_r81t436 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_error_89glhjr {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_escaping_k8g5adu {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_6vd7mkm {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_h9dh5ch {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_iu2kf5m {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_k866gq9 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_njk3fpr {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_np21hmd {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_list_7s4om10 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_h20ej0f {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_packed_place_5ck3439 {
    gm_frame h;
    gm_tpautr4br7h3l l4; /* def */
    uint8_t ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_report_eardf07 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_returned_bhf7ho1 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_returns_4q3h1f6 {
    gm_frame h;
    gm_closure l1; /* f */
    gm_b0_1 ret;
};
struct gm_fr_root_local_jogho27 {
    gm_frame h;
    gm_t9805qtonbsag ret;
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
struct gm_fr_unaligned_q31c519 {
    gm_frame h;
    void * l0; /* s */
    gm_closure l2; /* f */
    gm_b0_1 ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_escapes_brb2os4(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_bvqfhnb(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_r81t436(void);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_89glhjr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_escaping_k8g5adu(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, void * a3);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_6vd7mkm(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_h9dh5ch(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_iu2kf5m(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_k866gq9(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_njk3fpr(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_np21hmd(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_for_each_child_fm49s73(void * a0, gm_closure a1);
__attribute__((aligned(4))) void gm_f_format_2kgv7hq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_d38c0qs(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_qilugr5(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_index_of_41b084m(gm_b16_8 a0, uint32_t a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_index_of_dgscica(gm_b16_8 a0, gm_cseahvg8v7p0vs a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_layout_a5ovnrm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_list_7s4om10(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_h20ej0f(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) uint8_t gm_f_packed_place_5ck3439(void * a0, void * a1);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_0mk7udh(void * a0, gm_cseahvg8v7p0vs a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_nf6ksi3(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_returned_bhf7ho1(void * a0, gm_closure a1);
__attribute__((aligned(4))) void gm_f_returns_4q3h1f6(void * a0, gm_closure a1);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_root_local_jogho27(void * a0, void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_8uijkbs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_unaligned_q31c519(void * a0, void * a1, gm_closure a2);
extern uint8_t gm_cs1qbc2gaqs[16] __attribute__((aligned(8)));
extern const uint8_t gm_sefrps06g7jfu[11];
extern const uint8_t gm_seqndsmrihepc[10];
extern const uint8_t gm_sjg1ri23hm753[6];
extern const uint8_t gm_sniol5ip67k9b[15];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcrgc9bkquf6nr[];
extern const char gm_srcsloagb2nrd8g[];
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    return n == 0 || memcmp(pa, pb, n * 1) == 0;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_bvqfhnb(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_bvqfhnb F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_r81t436(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_r81t436 F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 12)));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_6vd7mkm(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_6vd7mkm F;
    void * gm_l0; /* e */
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_check_escapes_brb2os4 *)(((&F.h)->up)->up))->l0));
        void * t3 = (void *)((uint8_t *)&((struct gm_fr_check_escapes_brb2os4 *)(((&F.h)->up)->up))->l4);
        void * t4 = gm_l0;
        void * t5 = (void *)((uint8_t *)&((struct gm_fr_check_escapes_brb2os4 *)(((&F.h)->up)->up))->l1);
        gm_b0_1 t6 = gm_f_escaping_k8g5adu((void *)NULL, cx, NULL, t2, t3, t4, t5);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_h9dh5ch(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_h9dh5ch F;
    void * gm_l0; /* v */
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        gm_closure t3 = ((gm_closure){(void *)gm_f_fn_6vd7mkm, (void *)&F.h, cx});
        gm_f_returned_bhf7ho1(t2, t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_iu2kf5m(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_iu2kf5m F;
    void * gm_l0; /* c */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_unaligned_q31c519 *)((&F.h)->up))->l0));
        void * t3 = gm_l0;
        gm_closure t4 = GM_LD(gm_closure, ((uint8_t *)&((struct gm_fr_unaligned_q31c519 *)((&F.h)->up))->l2));
        gm_f_unaligned_q31c519(t2, t3, t4);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_k866gq9(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_k866gq9 F;
    void * gm_l0; /* e */
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_check_escapes_brb2os4 *)((&F.h)->up))->l0));
        void * t3 = (void *)((uint8_t *)&((struct gm_fr_check_escapes_brb2os4 *)((&F.h)->up))->l4);
        void * t4 = gm_l0;
        void * t5 = (void *)((uint8_t *)&((struct gm_fr_check_escapes_brb2os4 *)((&F.h)->up))->l1);
        gm_b0_1 t6 = gm_f_escaping_k8g5adu((void *)NULL, cx, NULL, t2, t3, t4, t5);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_njk3fpr(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_njk3fpr F;
    void * gm_l0; /* c */
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        gm_closure t3 = GM_LD(gm_closure, ((uint8_t *)&((struct gm_fr_returns_4q3h1f6 *)((&F.h)->up))->l1));
        gm_f_returns_4q3h1f6(t2, t3);
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
inline __attribute__((aligned(4))) void gm_f_list_h20ej0f(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_h20ej0f F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_bvqfhnb();
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_show_kmjeh8s F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    gm_tmm67r2c94d5j gm_l3; /* p */
    GM_STACK_CHECK(&F, 145u);
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
        gm_b16_8 t8 = t7;
        gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)&t8));
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
__attribute__((aligned(4))) gm_b0_1 gm_f_check_escapes_brb2os4(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_check_escapes_brb2os4 F;
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    void * gm_l5; /* body */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    void * gm_l9; /* body */
    gm_b16_8 gm_l11; /* <tmp> */
    uint64_t gm_l12; /* <tmp> */
    gm_tbgd14esf0jvi gm_l13; /* x */
    GM_STACK_CHECK(&F, 836u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    {
        gm_f_list_h20ej0f((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            void * t3 = F.l0;
            gm_b16_8 t4 = GM_LD(gm_b16_8, ((((uint8_t *)t3) + 384) + 0));
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
                    gm_tdgfduqjheqdm t16 = GM_LD(gm_tdgfduqjheqdm, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t12 * 64));
                    F.l4 = t16;
                    {
                        void * t18 = GM_LD(void *, (((uint8_t *)&F.l4) + 40));
                        void * t19 = t18;
                        void * t20;
                        if ((GM_LD(void *, ((uint8_t *)&t19)) != 0)) {
                            void * t21 = GM_LD(void *, (((uint8_t *)&t19) + 0));
                            t20 = t21;
                        } else {
                            goto lc5;
                        }
                        gm_l5 = t20;
                    }
                    {
                        void * t22 = gm_l5;
                        gm_closure t23 = ((gm_closure){(void *)gm_f_fn_k866gq9, (void *)&F.h, cx});
                        gm_f_returned_bhf7ho1(t22, t23);
                        if (gm_unw.kind) {
                            goto gm_ll9;
                        }
                    }
                    {
                        void * t24 = gm_l5;
                        gm_closure t25 = ((gm_closure){(void *)gm_f_fn_h9dh5ch, (void *)&F.h, cx});
                        gm_f_returns_4q3h1f6(t24, t25);
                        if (gm_unw.kind) {
                            goto gm_ll9;
                        }
                    }
                    goto me14;
mn15:;
                    gm_trap("no match arm matched the scrutinee", gm_srcsloagb2nrd8g, 14, 5);
me14:;
                }
lc5:;
                {
                    uint64_t t27 = gm_l3;
                    uint64_t r28; bool o29 = __builtin_add_overflow(t27, ((uint64_t)1ULL), &r28);
                    uint64_t t30 = r28;
                    gm_l3 = t30;
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
        {
            void * t32 = F.l0;
            gm_b16_8 t33 = GM_LD(gm_b16_8, ((((uint8_t *)t32) + 384) + 0));
            gm_l6 = t33;
        }
        {
            gm_l7 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t35 = gm_l7;
                uint64_t t36 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                uint8_t t37 = (t35 < t36);
                if (!t37) goto lb34;
                {
                    uint64_t t40 = gm_l7;
                    uint64_t t41 = (uintptr_t)t40;
                    uint64_t t42 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    gm_tdgfduqjheqdm t45 = GM_LD(gm_tdgfduqjheqdm, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t41 * 64));
                    F.l8 = t45;
                    {
                        void * t47 = GM_LD(void *, (((uint8_t *)&F.l8) + 40));
                        void * t48 = t47;
                        void * t49;
                        if ((GM_LD(void *, ((uint8_t *)&t48)) != 0)) {
                            void * t50 = GM_LD(void *, (((uint8_t *)&t48) + 0));
                            t49 = t50;
                        } else {
                            goto lc34;
                        }
                        gm_l9 = t49;
                    }
                    {
                        void * t51 = F.l0;
                        void * t52 = gm_l9;
                        gm_closure t53 = ((gm_closure){(void *)gm_f_fn_np21hmd, (void *)&F.h, cx});
                        gm_f_unaligned_q31c519(t51, t52, t53);
                        if (gm_unw.kind) {
                            goto gm_ll38;
                        }
                    }
                    goto me43;
mn44:;
                    gm_trap("no match arm matched the scrutinee", gm_srcsloagb2nrd8g, 21, 5);
me43:;
                }
lc34:;
                {
                    uint64_t t55 = gm_l7;
                    uint64_t r56; bool o57 = __builtin_add_overflow(t55, ((uint64_t)1ULL), &r56);
                    uint64_t t58 = r56;
                    gm_l7 = t58;
                }
            }
gm_ll38:;
            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb34; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc34; }
            }
            goto gm_lad;
lb34:;
        }
    }
    {
        gm_f_list_7s4om10((void *)&F.l10, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t60 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
            gm_l11 = t60;
        }
        {
            gm_l12 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t62 = gm_l12;
                uint64_t t63 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                uint8_t t64 = (t62 < t63);
                if (!t64) goto lb61;
                {
                    uint64_t t67 = gm_l12;
                    uint64_t t68 = (uintptr_t)t67;
                    uint64_t t69 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                    gm_tbgd14esf0jvi t72 = GM_LD(gm_tbgd14esf0jvi, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l11)) + t68 * 32));
                    gm_l13 = t72;
                    {
                        gm_b16_8 t75 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 0));
                        gm_b16_8 t76 = t75;
                        gm_b16_8 t77 = GM_LD(gm_b16_8, ((uint8_t *)&t76));
                        gm_tl874t2atnlh3 t78 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l13) + 0));
                        gm_tf5srh4mp4b3i t79 = gm_f_index_of_dgscica(t77, GM_CONV(gm_cseahvg8v7p0vs, t78));
                        gm_tf5srh4mp4b3i t80 = t79;
                        uint8_t t81;
                        if (((((uint8_t *)&t80))[8] != 0)) goto mn83;
                        t81 = ((uint8_t)0);
                        goto me82;
mn83:;
                        t81 = ((uint8_t)1);
                        goto me82;
mn84:;
                        gm_trap("no match arm matched the scrutinee", gm_srcsloagb2nrd8g, 31, 12);
me82:;
                        if (!t81) goto ie74;
                        {
                            goto lc61;
                        }
                        goto ix74;
ie74:;
ix74:;
                    }
                    {
                        void * t86 = (void *)((uint8_t *)&F.l10);
                        gm_tl874t2atnlh3 t87 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l13) + 0));
                        gm_tftb1odc739t5 t88 = gm_f_push_0mk7udh(t86, GM_CONV(gm_cseahvg8v7p0vs, t87));
                        gm_tftb1odc739t5 t89 = t88;
                        if (((((uint8_t *)&t89))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t89) + 1, gm_srcsloagb2nrd8g, 32, 9);
                        gm_b0_1 t90 = GM_LD(gm_b0_1, (((uint8_t *)&t89) + 1));
                    }
                    {
                        void * t91 = F.l0;
                        gm_tl874t2atnlh3 t92 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l13) + 0));
                        gm_b16_8 t93 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l13) + 16));
                        gm_b0_1 t94 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t91, t92, t93);
                        if (gm_unw.kind) {
                            goto gm_ll65;
                        }
                    }
                    goto me70;
mn71:;
                    gm_trap("no match arm matched the scrutinee", gm_srcsloagb2nrd8g, 30, 5);
me70:;
                }
lc61:;
                {
                    uint64_t t96 = gm_l12;
                    uint64_t r97; bool o98 = __builtin_add_overflow(t96, ((uint64_t)1ULL), &r97);
                    uint64_t t99 = r97;
                    gm_l12 = t99;
                }
            }
gm_ll65:;
            if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb61; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc61; }
            }
            goto gm_lad;
lb61:;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_escaping_k8g5adu(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, void * a3) {
    struct gm_fr_escaping_k8g5adu F;
    void * gm_l0; /* s */
    void * gm_l1; /* def */
    void * gm_l2; /* e */
    void * gm_l3; /* found */
    gm_b16_8 gm_l4; /* what */
    void * gm_l5; /* place */
    void * gm_l6; /* p */
    gm_tao63pk3r18v5 gm_l7; /* sl */
    uint32_t gm_l8; /* local */
    gm_b16_8 gm_l9; /* name */
    gm_b16_8 gm_l10; /* kind */
    GM_STACK_CHECK(&F, 1179u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        gm_b16_8 t2;
        GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(0));
        gm_l4 = t2;
    }
    {
        void * t3 = gm_l2;
        gm_l5 = t3;
    }
    {
        void * t4 = gm_l2;
        uint8_t t10 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        switch (t10) { case ((uint8_t)0ULL): goto ma8; case ((uint8_t)1ULL): goto ma8; case ((uint8_t)2ULL): goto ma8; case ((uint8_t)3ULL): goto ma8; case ((uint8_t)4ULL): goto ma8; case ((uint8_t)5ULL): goto ma8; case ((uint8_t)6ULL): goto ma8; case ((uint8_t)7ULL): goto ma8; case ((uint8_t)8ULL): goto ma8; case ((uint8_t)9ULL): goto ma8; case ((uint8_t)10ULL): goto ma8; case ((uint8_t)11ULL): goto ma8; case ((uint8_t)12ULL): goto ma8; case ((uint8_t)13ULL): goto ma8; case ((uint8_t)14ULL): goto ma8; case ((uint8_t)15ULL): goto ma8; case ((uint8_t)16ULL): goto ma8; case ((uint8_t)17ULL): goto ma8; case ((uint8_t)18ULL): goto ma8; case ((uint8_t)19ULL): goto ma8; case ((uint8_t)20ULL): goto ma8; case ((uint8_t)21ULL): goto ma7; case ((uint8_t)22ULL): goto ma8; case ((uint8_t)23ULL): goto ma8; case ((uint8_t)24ULL): goto ma6; case ((uint8_t)25ULL): goto ma8; case ((uint8_t)26ULL): goto ma8; case ((uint8_t)27ULL): goto ma8; case ((uint8_t)28ULL): goto ma8; case ((uint8_t)29ULL): goto ma8; case ((uint8_t)30ULL): goto ma8; case ((uint8_t)31ULL): goto ma8; case ((uint8_t)32ULL): goto ma8; case ((uint8_t)33ULL): goto ma8; case ((uint8_t)34ULL): goto ma8; case ((uint8_t)35ULL): goto ma8; case ((uint8_t)36ULL): goto ma8; case ((uint8_t)37ULL): goto ma8; case ((uint8_t)38ULL): goto ma8; case ((uint8_t)39ULL): goto ma8; case ((uint8_t)40ULL): goto ma8; case ((uint8_t)41ULL): goto ma8; case ((uint8_t)42ULL): goto ma8; case ((uint8_t)43ULL): goto ma8; case ((uint8_t)44ULL): goto ma8; case ((uint8_t)45ULL): goto ma8; case ((uint8_t)46ULL): goto ma8; case ((uint8_t)47ULL): goto ma8; case ((uint8_t)48ULL): goto ma8; case ((uint8_t)49ULL): goto ma8; case ((uint8_t)50ULL): goto ma8; case ((uint8_t)51ULL): goto ma8; case ((uint8_t)52ULL): goto ma8; case ((uint8_t)53ULL): goto ma8; case ((uint8_t)54ULL): goto ma8; case ((uint8_t)55ULL): goto ma8; case ((uint8_t)56ULL): goto ma8; case ((uint8_t)57ULL): goto ma8; case ((uint8_t)58ULL): goto ma8; default: goto ma8; }
ma6:;
        uint8_t t12 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t12 != ((uint8_t)24ULL)) goto mn11;
        void * t13 = GM_LD(void *, ((((uint8_t *)t4) + 8) + 8));
        gm_l6 = t13;
        {
            gm_b16_8 t15;
            GM_ST(void *, (uint8_t *)&t15, (void *)(gm_sniol5ip67k9b));
            GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)(14));
            gm_l4 = t15;
        }
        {
            void * t16 = gm_l6;
            gm_l5 = t16;
        }
        goto me5;
mn11:;
ma7:;
        uint8_t t18 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t18 != ((uint8_t)21ULL)) goto mn17;
        gm_tao63pk3r18v5 t19 = GM_LD(gm_tao63pk3r18v5, ((((uint8_t *)t4) + 8) + 8));
        gm_l7 = t19;
        {
            void * t22 = gm_l0;
            void * t23 = (void *)(((uint8_t *)t22) + 8);
            void * t24 = GM_LD(void *, (((uint8_t *)&gm_l7) + 0));
            gm_t88rcucg1b7uk t25 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t24) + 0));
            gm_t53at80pfdus3 t26;
            gm_f_kind_jcnt4hm((void *)&t26, t23, GM_CONV(gm_csguankko4lmhd, t25));
            gm_t53at80pfdus3 t27 = t26;
            uint8_t t28;
            uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)&t27) + 0));
            if (t31 != ((uint8_t)11ULL)) goto mn30;
            t28 = ((uint8_t)0);
            goto me29;
mn30:;
            t28 = ((uint8_t)1);
            goto me29;
mn32:;
            gm_trap("no match arm matched the scrutinee", gm_srcsloagb2nrd8g, 68, 16);
me29:;
            if (!t28) goto ie21;
            {
                gm_b0_1 t34;
                uint8_t t35 = 0;
                F.ret = t34;
                goto gm_fret;
            }
            goto ix21;
ie21:;
ix21:;
        }
        {
            gm_b16_8 t36;
            GM_ST(void *, (uint8_t *)&t36, (void *)(gm_sefrps06g7jfu));
            GM_ST(uintptr_t, (uint8_t *)&t36 + 8, (uintptr_t)(10));
            gm_l4 = t36;
        }
        {
            void * t37 = GM_LD(void *, (((uint8_t *)&gm_l7) + 0));
            gm_l5 = t37;
        }
        goto me5;
mn17:;
ma8:;
        gm_b0_1 t39;
        uint8_t t40 = 0;
        F.ret = t39;
        goto gm_fret;
        goto me5;
mn38:;
ma9:;
        gm_trap("no match arm matched the scrutinee", gm_srcsloagb2nrd8g, 62, 5);
me5:;
    }
    {
        void * t41 = gm_l0;
        void * t42 = gm_l5;
        gm_t9805qtonbsag t43 = gm_f_root_local_jogho27(t41, t42);
        gm_t9805qtonbsag t44 = t43;
        uint32_t t45;
        if (((((uint8_t *)&t44))[4] != 0)) {
            uint32_t t46 = GM_LD(uint32_t, (((uint8_t *)&t44) + 0));
            t45 = t46;
        } else {
            gm_b0_1 t47;
            uint8_t t48 = 0;
            F.ret = t47;
            goto gm_fret;
        }
        gm_l8 = t45;
    }
    {
        uint32_t t49 = gm_l8;
        uint64_t t50 = (uint64_t)t49;
        uint64_t t51 = (uintptr_t)t50;
        void * t52 = gm_l1;
        uint64_t t53 = GM_LD(uintptr_t, (((uint8_t *)t52) + 24) + 8);
        if (t51 >= (uintptr_t)(t53)) gm_trap_index(t51, t53, gm_srcsloagb2nrd8g, 75, 13);
        gm_b16_8 t54 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, (((uint8_t *)t52) + 24)) + t51 * 40) + 0));
        gm_l9 = t54;
    }
    {
        gm_b16_8 t56 = gm_l9;
        gm_b16_8 t57 = GM_LD(gm_b16_8, gm_cs1qbc2gaqs);
        gm_b16_8 t58 = t56;
        gm_b16_8 t59 = t57;
        uint8_t t60 = (gm_eq0iqiubfm7e(((uint8_t *)&t58), ((uint8_t *)&t59)));
        if (!t60) goto ie55;
        {
            void * t62 = gm_l3;
            gm_tbgd14esf0jvi t63;
            memset((uint8_t *)&t63 + 12, 0, 4);
            void * t64 = gm_l2;
            gm_tl874t2atnlh3 t65 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t64) + 72));
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t63 + 0, t65);
            gm_b16_8 t66 = gm_l4;
            gm_tnpifv20irnph t67;
            GM_ST(gm_b16_8, ((uint8_t *)&t67 + 0), t66);
            gm_tabb710d9mq2e t68;
            gm_f_format_2kgv7hq((void *)&t68, (void *)NULL, cx, NULL, t67);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t69 = t68;
            if (((((uint8_t *)&t69))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t69) + 8, gm_srcsloagb2nrd8g, 77, 50);
            gm_b16_8 t70 = GM_LD(gm_b16_8, (((uint8_t *)&t69) + 8));
            gm_b16_8 t71 = t70;
            gm_b16_8 t72 = GM_LD(gm_b16_8, ((uint8_t *)&t71));
            GM_ST(gm_b16_8, (uint8_t *)&t63 + 16, t72);
            gm_tftb1odc739t5 t73 = gm_f_push_nf6ksi3(t62, &t63);
            gm_tftb1odc739t5 t74 = t73;
            if (((((uint8_t *)&t74))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t74) + 1, gm_srcsloagb2nrd8g, 77, 9);
            gm_b0_1 t75 = GM_LD(gm_b0_1, (((uint8_t *)&t74) + 1));
        }
        {
            gm_b0_1 t76;
            uint8_t t77 = 0;
            F.ret = t76;
            goto gm_fret;
        }
        goto ix55;
ie55:;
ix55:;
    }
    {
        gm_b16_8 t78;
        void * t80 = gm_l1;
        gm_b16_8 t81 = GM_LD(gm_b16_8, (((uint8_t *)t80) + 8));
        gm_b16_8 t82 = t81;
        gm_b16_8 t83 = GM_LD(gm_b16_8, ((uint8_t *)&t82));
        uint32_t t84 = gm_l8;
        gm_tf5srh4mp4b3i t85 = gm_f_index_of_41b084m(t83, t84);
        gm_tf5srh4mp4b3i t86 = t85;
        uint8_t t87;
        if (((((uint8_t *)&t86))[8] != 0)) goto mn89;
        t87 = ((uint8_t)0);
        goto me88;
mn89:;
        t87 = ((uint8_t)1);
        goto me88;
mn90:;
        gm_trap("no match arm matched the scrutinee", gm_srcsloagb2nrd8g, 80, 16);
me88:;
        if (!t87) goto ie79;
        gm_b16_8 t92;
        GM_ST(void *, (uint8_t *)&t92, (void *)(gm_seqndsmrihepc));
        GM_ST(uintptr_t, (uint8_t *)&t92 + 8, (uintptr_t)(9));
        gm_b16_8 t93 = t92;
        t78 = t93;
        goto ix79;
ie79:;
        gm_b16_8 t95;
        GM_ST(void *, (uint8_t *)&t95, (void *)(gm_sjg1ri23hm753));
        GM_ST(uintptr_t, (uint8_t *)&t95 + 8, (uintptr_t)(5));
        gm_b16_8 t96 = t95;
        t78 = t96;
ix79:;
        gm_l10 = t78;
    }
    {
        void * t97 = gm_l3;
        gm_tbgd14esf0jvi t98;
        memset((uint8_t *)&t98 + 12, 0, 4);
        void * t99 = gm_l2;
        gm_tl874t2atnlh3 t100 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t99) + 72));
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t98 + 0, t100);
        gm_tfeab279c16rr t101;
        gm_b16_8 t102 = gm_l4;
        GM_ST(gm_b16_8, (uint8_t *)&t101 + 0, t102);
        gm_b16_8 t103 = gm_l10;
        GM_ST(gm_b16_8, (uint8_t *)&t101 + 16, t103);
        gm_b16_8 t104 = gm_l9;
        GM_ST(gm_b16_8, (uint8_t *)&t101 + 32, t104);
        gm_tabb710d9mq2e t105;
        gm_f_format_qilugr5((void *)&t105, (void *)NULL, cx, NULL, &t101);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t106 = t105;
        if (((((uint8_t *)&t106))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t106) + 8, gm_srcsloagb2nrd8g, 81, 46);
        gm_b16_8 t107 = GM_LD(gm_b16_8, (((uint8_t *)&t106) + 8));
        gm_b16_8 t108 = t107;
        gm_b16_8 t109 = GM_LD(gm_b16_8, ((uint8_t *)&t108));
        GM_ST(gm_b16_8, (uint8_t *)&t98 + 16, t109);
        gm_tftb1odc739t5 t110 = gm_f_push_nf6ksi3(t97, &t98);
        gm_tftb1odc739t5 t111 = t110;
        if (((((uint8_t *)&t111))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t111) + 1, gm_srcsloagb2nrd8g, 81, 5);
        gm_b0_1 t112 = GM_LD(gm_b0_1, (((uint8_t *)&t111) + 1));
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
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_6vd7mkm(void *env, gm_node *cx, gm_node *inst, void * a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_h9dh5ch(void *env, gm_node *cx, gm_node *inst, void * a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_iu2kf5m(void *env, gm_node *cx, gm_node *inst, void * a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_k866gq9(void *env, gm_node *cx, gm_node *inst, void * a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_njk3fpr(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_np21hmd(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_fn_np21hmd F;
    void * gm_l0; /* e */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 301u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_check_escapes_brb2os4 *)((&F.h)->up))->l0));
        gm_t88rcucg1b7uk t4 = gm_l1;
        gm_tbccvikg6b96e t5;
        gm_f_layout_a5ovnrm((void *)&t5, (void *)NULL, cx, NULL, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tbccvikg6b96e t6 = t5;
        uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)&t6) + 8));
        uint8_t t8 = (t7 > ((uint64_t)1ULL));
        if (!t8) goto ie2;
        {
            void * t10 = (void *)((uint8_t *)&((struct gm_fr_check_escapes_brb2os4 *)((&F.h)->up))->l1);
            gm_tbgd14esf0jvi t11;
            memset((uint8_t *)&t11 + 12, 0, 4);
            void * t12 = gm_l0;
            gm_tl874t2atnlh3 t13 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t12) + 72));
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t11 + 0, t13);
            void * t14 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_check_escapes_brb2os4 *)((&F.h)->up))->l0));
            gm_t88rcucg1b7uk t15 = gm_l1;
            gm_b16_8 t16 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t14, t15);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tnpifv20irnph t17;
            GM_ST(gm_b16_8, ((uint8_t *)&t17 + 0), t16);
            gm_tabb710d9mq2e t18;
            gm_f_format_d38c0qs((void *)&t18, (void *)NULL, cx, NULL, t17);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t19 = t18;
            if (((((uint8_t *)&t19))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t19) + 8, gm_srcsloagb2nrd8g, 25, 58);
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&t19) + 8));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, ((uint8_t *)&t21));
            GM_ST(gm_b16_8, (uint8_t *)&t11 + 16, t22);
            gm_tftb1odc739t5 t23 = gm_f_push_nf6ksi3(t10, &t11);
            gm_tftb1odc739t5 t24 = t23;
            if (((((uint8_t *)&t24))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t24) + 1, gm_srcsloagb2nrd8g, 25, 17);
            gm_b0_1 t25 = GM_LD(gm_b0_1, (((uint8_t *)&t24) + 1));
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
inline __attribute__((aligned(4))) uint8_t gm_f_packed_place_5ck3439(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_packed_place_5ck3439 F;
    void * gm_l0; /* s */
    void * gm_l1; /* e */
    void * gm_l2; /* base */
    uint32_t gm_l3; /* id */
    void * gm_l5; /* base */
    GM_STACK_CHECK(&F, 477u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l1;
        uint8_t t8 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        switch (t8) { case ((uint8_t)0ULL): goto ma6; case ((uint8_t)1ULL): goto ma6; case ((uint8_t)2ULL): goto ma6; case ((uint8_t)3ULL): goto ma6; case ((uint8_t)4ULL): goto ma6; case ((uint8_t)5ULL): goto ma6; case ((uint8_t)6ULL): goto ma6; case ((uint8_t)7ULL): goto ma6; case ((uint8_t)8ULL): goto ma6; case ((uint8_t)9ULL): goto ma6; case ((uint8_t)10ULL): goto ma6; case ((uint8_t)11ULL): goto ma6; case ((uint8_t)12ULL): goto ma6; case ((uint8_t)13ULL): goto ma6; case ((uint8_t)14ULL): goto ma6; case ((uint8_t)15ULL): goto ma6; case ((uint8_t)16ULL): goto ma6; case ((uint8_t)17ULL): goto ma6; case ((uint8_t)18ULL): goto ma6; case ((uint8_t)19ULL): goto ma4; case ((uint8_t)20ULL): goto ma5; case ((uint8_t)21ULL): goto ma6; case ((uint8_t)22ULL): goto ma6; case ((uint8_t)23ULL): goto ma6; case ((uint8_t)24ULL): goto ma6; case ((uint8_t)25ULL): goto ma6; case ((uint8_t)26ULL): goto ma6; case ((uint8_t)27ULL): goto ma6; case ((uint8_t)28ULL): goto ma6; case ((uint8_t)29ULL): goto ma6; case ((uint8_t)30ULL): goto ma6; case ((uint8_t)31ULL): goto ma6; case ((uint8_t)32ULL): goto ma6; case ((uint8_t)33ULL): goto ma6; case ((uint8_t)34ULL): goto ma6; case ((uint8_t)35ULL): goto ma6; case ((uint8_t)36ULL): goto ma6; case ((uint8_t)37ULL): goto ma6; case ((uint8_t)38ULL): goto ma6; case ((uint8_t)39ULL): goto ma6; case ((uint8_t)40ULL): goto ma6; case ((uint8_t)41ULL): goto ma6; case ((uint8_t)42ULL): goto ma6; case ((uint8_t)43ULL): goto ma6; case ((uint8_t)44ULL): goto ma6; case ((uint8_t)45ULL): goto ma6; case ((uint8_t)46ULL): goto ma6; case ((uint8_t)47ULL): goto ma6; case ((uint8_t)48ULL): goto ma6; case ((uint8_t)49ULL): goto ma6; case ((uint8_t)50ULL): goto ma6; case ((uint8_t)51ULL): goto ma6; case ((uint8_t)52ULL): goto ma6; case ((uint8_t)53ULL): goto ma6; case ((uint8_t)54ULL): goto ma6; case ((uint8_t)55ULL): goto ma6; case ((uint8_t)56ULL): goto ma6; case ((uint8_t)57ULL): goto ma6; case ((uint8_t)58ULL): goto ma6; default: goto ma6; }
ma4:;
        uint8_t t10 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t10 != ((uint8_t)19ULL)) goto mn9;
        void * t11 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l2 = t11;
        {
            void * t14 = gm_l0;
            void * t15 = (void *)(((uint8_t *)t14) + 8);
            void * t16 = gm_l2;
            gm_t88rcucg1b7uk t17 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t16) + 0));
            gm_t53at80pfdus3 t18;
            gm_f_kind_jcnt4hm((void *)&t18, t15, GM_CONV(gm_csguankko4lmhd, t17));
            gm_t53at80pfdus3 t19 = t18;
            uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)&t19) + 0));
            if (t20 != ((uint8_t)17ULL)) goto ie13;
            uint32_t t21 = GM_LD(uint32_t, ((((uint8_t *)&t19) + 8) + 0));
            gm_l3 = t21;
            uint32_t t22 = gm_l3;
            uint64_t t23 = (uint64_t)t22;
            uint64_t t24 = (uintptr_t)t23;
            void * t25 = gm_l0;
            uint64_t t26 = GM_LD(uintptr_t, ((((uint8_t *)t25) + 1088) + 0) + 8);
            if (t24 >= (uintptr_t)(t26)) gm_trap_index(t24, t26, gm_srcsloagb2nrd8g, 110, 71);
            uint8_t t27 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t25) + 1088) + 0)) + t24 * 184) + 24) + 0));
            if (t27 != ((uint8_t)0ULL)) goto ie13;
            gm_tpautr4br7h3l t28 = GM_LD(gm_tpautr4br7h3l, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t25) + 1088) + 0)) + t24 * 184) + 24) + 8));
            F.l4 = t28;
            uint8_t t29 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 16));
            if (!t29) goto ie13;
            {
                uint8_t t31 = 0;
                F.ret = ((uint8_t)1);
                goto gm_fret;
            }
            goto ix13;
ie13:;
ix13:;
        }
        {
            void * t32 = gm_l0;
            void * t33 = gm_l2;
            uint8_t t34 = gm_f_packed_place_5ck3439(t32, t33);
            uint8_t t35 = 0;
            F.ret = t34;
            goto gm_fret;
        }
        goto me3;
mn9:;
ma5:;
        uint8_t t37 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t37 != ((uint8_t)20ULL)) goto mn36;
        void * t38 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l5 = t38;
        uint8_t t39 = 0;
        void * t40 = gm_l0;
        void * t41 = (void *)(((uint8_t *)t40) + 8);
        void * t42 = gm_l5;
        gm_t88rcucg1b7uk t43 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t42) + 0));
        gm_t53at80pfdus3 t44;
        gm_f_kind_jcnt4hm((void *)&t44, t41, GM_CONV(gm_csguankko4lmhd, t43));
        gm_t53at80pfdus3 t45 = t44;
        uint8_t t46;
        uint8_t t49 = GM_LD(uint8_t, (((uint8_t *)&t45) + 0));
        if (t49 != ((uint8_t)11ULL)) goto mn48;
        t46 = ((uint8_t)1);
        goto me47;
mn48:;
        t46 = ((uint8_t)0);
        goto me47;
mn50:;
        gm_trap("no match arm matched the scrutinee", gm_srcsloagb2nrd8g, 113, 38);
me47:;
        if (t46) {
            void * t51 = gm_l0;
            void * t52 = gm_l5;
            uint8_t t53 = gm_f_packed_place_5ck3439(t51, t52);
            t39 = t53;
        } else t39 = 0;
        uint8_t t54 = 0;
        F.ret = t39;
        goto gm_fret;
        goto me3;
mn36:;
ma6:;
        uint8_t t56 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
        goto me3;
mn55:;
ma7:;
        gm_trap("no match arm matched the scrutinee", gm_srcsloagb2nrd8g, 108, 5);
me3:;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_returned_bhf7ho1(void * a0, gm_closure a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_returned_bhf7ho1 F;
    void * gm_l0; /* e */
    gm_closure gm_l1; /* f */
    void * gm_l2; /* x */
    void * gm_l3; /* b */
    void * gm_l4; /* t */
    gm_trl2khmuphncd gm_l5; /* x */
    void * gm_l6; /* el */
    gm_b16_8 gm_l7; /* arms */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    gm_tbbqefuhv2r8h gm_l10; /* a */
    GM_STACK_CHECK(&F, 614u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        uint8_t t10 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        switch (t10) { case ((uint8_t)0ULL): goto ma8; case ((uint8_t)1ULL): goto ma8; case ((uint8_t)2ULL): goto ma8; case ((uint8_t)3ULL): goto ma8; case ((uint8_t)4ULL): goto ma8; case ((uint8_t)5ULL): goto ma8; case ((uint8_t)6ULL): goto ma8; case ((uint8_t)7ULL): goto ma8; case ((uint8_t)8ULL): goto ma8; case ((uint8_t)9ULL): goto ma8; case ((uint8_t)10ULL): goto ma8; case ((uint8_t)11ULL): goto ma8; case ((uint8_t)12ULL): goto ma8; case ((uint8_t)13ULL): goto ma8; case ((uint8_t)14ULL): goto ma8; case ((uint8_t)15ULL): goto ma8; case ((uint8_t)16ULL): goto ma8; case ((uint8_t)17ULL): goto ma8; case ((uint8_t)18ULL): goto ma8; case ((uint8_t)19ULL): goto ma8; case ((uint8_t)20ULL): goto ma8; case ((uint8_t)21ULL): goto ma8; case ((uint8_t)22ULL): goto ma8; case ((uint8_t)23ULL): goto ma8; case ((uint8_t)24ULL): goto ma8; case ((uint8_t)25ULL): goto ma8; case ((uint8_t)26ULL): goto ma8; case ((uint8_t)27ULL): goto ma8; case ((uint8_t)28ULL): goto ma8; case ((uint8_t)29ULL): goto ma8; case ((uint8_t)30ULL): goto ma8; case ((uint8_t)31ULL): goto ma8; case ((uint8_t)32ULL): goto ma4; case ((uint8_t)33ULL): goto ma8; case ((uint8_t)34ULL): goto ma8; case ((uint8_t)35ULL): goto ma8; case ((uint8_t)36ULL): goto ma8; case ((uint8_t)37ULL): goto ma8; case ((uint8_t)38ULL): goto ma8; case ((uint8_t)39ULL): goto ma8; case ((uint8_t)40ULL): goto ma8; case ((uint8_t)41ULL): goto ma8; case ((uint8_t)42ULL): goto ma8; case ((uint8_t)43ULL): goto ma8; case ((uint8_t)44ULL): goto ma7; case ((uint8_t)45ULL): goto ma8; case ((uint8_t)46ULL): goto ma5; case ((uint8_t)47ULL): goto ma6; case ((uint8_t)48ULL): goto ma8; case ((uint8_t)49ULL): goto ma8; case ((uint8_t)50ULL): goto ma8; case ((uint8_t)51ULL): goto ma8; case ((uint8_t)52ULL): goto ma8; case ((uint8_t)53ULL): goto ma8; case ((uint8_t)54ULL): goto ma8; case ((uint8_t)55ULL): goto ma8; case ((uint8_t)56ULL): goto ma8; case ((uint8_t)57ULL): goto ma8; case ((uint8_t)58ULL): goto ma8; default: goto ma8; }
ma4:;
        uint8_t t12 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t12 != ((uint8_t)32ULL)) goto mn11;
        void * t13 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l2 = t13;
        void * t14 = gm_l2;
        gm_closure t15 = gm_l1;
        gm_f_returned_bhf7ho1(t14, t15);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me3;
mn11:;
ma5:;
        uint8_t t17 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t17 != ((uint8_t)46ULL)) goto mn16;
        void * t18 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l3 = t18;
        void * t20 = gm_l3;
        void * t21 = GM_LD(void *, (((uint8_t *)t20) + 16));
        void * t22 = t21;
        if (!(GM_LD(void *, ((uint8_t *)&t22)) != 0)) goto ie19;
        void * t23 = GM_LD(void *, ((uint8_t *)&t22));
        gm_l4 = t23;
        {
            void * t25 = gm_l4;
            gm_closure t26 = gm_l1;
            gm_f_returned_bhf7ho1(t25, t26);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix19;
ie19:;
ix19:;
        goto me3;
mn16:;
ma6:;
        uint8_t t28 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t28 != ((uint8_t)47ULL)) goto mn27;
        gm_trl2khmuphncd t29 = GM_LD(gm_trl2khmuphncd, ((((uint8_t *)t2) + 8) + 8));
        gm_l5 = t29;
        {
            void * t31 = GM_LD(void *, (((uint8_t *)&gm_l5) + 16));
            gm_closure t32 = gm_l1;
            gm_f_returned_bhf7ho1(t31, t32);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t34 = GM_LD(void *, (((uint8_t *)&gm_l5) + 24));
            void * t35 = t34;
            if (!(GM_LD(void *, ((uint8_t *)&t35)) != 0)) goto ie33;
            void * t36 = GM_LD(void *, ((uint8_t *)&t35));
            gm_l6 = t36;
            {
                void * t38 = gm_l6;
                gm_closure t39 = gm_l1;
                gm_f_returned_bhf7ho1(t38, t39);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix33;
ie33:;
ix33:;
        }
        goto me3;
mn27:;
ma7:;
        uint8_t t41 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t41 != ((uint8_t)44ULL)) goto mn40;
        gm_b16_8 t42 = GM_LD(gm_b16_8, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l7 = t42;
        {
            gm_b16_8 t44 = gm_l7;
            gm_l8 = t44;
        }
        {
            gm_l9 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t46 = gm_l9;
                uint64_t t47 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                uint8_t t48 = (t46 < t47);
                if (!t48) goto lb45;
                {
                    uint64_t t51 = gm_l9;
                    uint64_t t52 = (uintptr_t)t51;
                    uint64_t t53 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                    gm_tbbqefuhv2r8h t56 = GM_LD(gm_tbbqefuhv2r8h, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l8)) + t52 * 24));
                    gm_l10 = t56;
                    {
                        void * t58 = GM_LD(void *, (((uint8_t *)&gm_l10) + 16));
                        gm_closure t59 = gm_l1;
                        gm_f_returned_bhf7ho1(t58, t59);
                        if (gm_unw.kind) {
                            goto gm_ll49;
                        }
                    }
                    goto me54;
mn55:;
                    gm_trap("no match arm matched the scrutinee", gm_srcsloagb2nrd8g, 52, 28);
me54:;
                }
lc45:;
                {
                    uint64_t t61 = gm_l9;
                    uint64_t r62; bool o63 = __builtin_add_overflow(t61, ((uint64_t)1ULL), &r62);
                    uint64_t t64 = r62;
                    gm_l9 = t64;
                }
            }
gm_ll49:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb45; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc45; }
            }
            goto gm_lad;
lb45:;
        }
        goto me3;
mn40:;
ma8:;
        gm_closure t66 = gm_l1;
        void * t67 = gm_l0;
        gm_closure t68 = t66;
        gm_node *ccx69 = t68.cx;
        void * t70 = t68.code;
        gm_b0_1 t71;
        if (GM_PTR_TAGGED(t70)) {
            ((void (*)(void *))GM_PTR_UNTAG(t70))(t67);
        } else {
            t71 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t70)(t68.env, ccx69, NULL, t67);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me3;
mn65:;
ma9:;
        gm_trap("no match arm matched the scrutinee", gm_srcsloagb2nrd8g, 45, 5);
me3:;
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
inline __attribute__((aligned(4))) void gm_f_returns_4q3h1f6(void * a0, gm_closure a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_returns_4q3h1f6 F;
    void * gm_l0; /* e */
    uint32_t gm_l2; /* levels */
    void * gm_l3; /* value */
    GM_STACK_CHECK(&F, 142u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    F.l1 = a1;
    {
        void * t3 = gm_l0;
        uint8_t t4 = GM_LD(uint8_t, ((((uint8_t *)t3) + 8) + 0));
        if (t4 != ((uint8_t)51ULL)) goto ie2;
        uint32_t t5 = GM_LD(uint32_t, (((((uint8_t *)t3) + 8) + 8) + 0));
        gm_l2 = t5;
        void * t6 = GM_LD(void *, (((((uint8_t *)t3) + 8) + 8) + 8));
        gm_l3 = t6;
        uint32_t t7 = gm_l2;
        uint8_t t8 = ((t7 == ((uint32_t)0ULL)));
        if (!t8) goto ie2;
        {
            gm_closure t10 = F.l1;
            void * t11 = gm_l3;
            gm_closure t12 = t10;
            gm_node *ccx13 = t12.cx;
            void * t14 = t12.code;
            gm_b0_1 t15;
            if (GM_PTR_TAGGED(t14)) {
                ((void (*)(void *))GM_PTR_UNTAG(t14))(t11);
            } else {
                t15 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *))t14)(t12.env, ccx13, NULL, t11);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t16 = gm_l0;
        gm_closure t17 = ((gm_closure){(void *)gm_f_fn_njk3fpr, (void *)&F.h, cx});
        gm_f_for_each_child_fm49s73(t16, t17);
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
inline __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_root_local_jogho27(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_root_local_jogho27 F;
    void * gm_l0; /* s */
    void * gm_l1; /* e */
    uint32_t gm_l2; /* l */
    void * gm_l3; /* base */
    void * gm_l4; /* base */
    GM_STACK_CHECK(&F, 298u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l1;
        uint8_t t9 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        switch (t9) { case ((uint8_t)0ULL): goto ma7; case ((uint8_t)1ULL): goto ma7; case ((uint8_t)2ULL): goto ma7; case ((uint8_t)3ULL): goto ma7; case ((uint8_t)4ULL): goto ma7; case ((uint8_t)5ULL): goto ma7; case ((uint8_t)6ULL): goto ma7; case ((uint8_t)7ULL): goto ma7; case ((uint8_t)8ULL): goto ma7; case ((uint8_t)9ULL): goto ma7; case ((uint8_t)10ULL): goto ma7; case ((uint8_t)11ULL): goto ma7; case ((uint8_t)12ULL): goto ma7; case ((uint8_t)13ULL): goto ma7; case ((uint8_t)14ULL): goto ma7; case ((uint8_t)15ULL): goto ma4; case ((uint8_t)16ULL): goto ma7; case ((uint8_t)17ULL): goto ma7; case ((uint8_t)18ULL): goto ma7; case ((uint8_t)19ULL): goto ma5; case ((uint8_t)20ULL): goto ma6; case ((uint8_t)21ULL): goto ma7; case ((uint8_t)22ULL): goto ma7; case ((uint8_t)23ULL): goto ma7; case ((uint8_t)24ULL): goto ma7; case ((uint8_t)25ULL): goto ma7; case ((uint8_t)26ULL): goto ma7; case ((uint8_t)27ULL): goto ma7; case ((uint8_t)28ULL): goto ma7; case ((uint8_t)29ULL): goto ma7; case ((uint8_t)30ULL): goto ma7; case ((uint8_t)31ULL): goto ma7; case ((uint8_t)32ULL): goto ma7; case ((uint8_t)33ULL): goto ma7; case ((uint8_t)34ULL): goto ma7; case ((uint8_t)35ULL): goto ma7; case ((uint8_t)36ULL): goto ma7; case ((uint8_t)37ULL): goto ma7; case ((uint8_t)38ULL): goto ma7; case ((uint8_t)39ULL): goto ma7; case ((uint8_t)40ULL): goto ma7; case ((uint8_t)41ULL): goto ma7; case ((uint8_t)42ULL): goto ma7; case ((uint8_t)43ULL): goto ma7; case ((uint8_t)44ULL): goto ma7; case ((uint8_t)45ULL): goto ma7; case ((uint8_t)46ULL): goto ma7; case ((uint8_t)47ULL): goto ma7; case ((uint8_t)48ULL): goto ma7; case ((uint8_t)49ULL): goto ma7; case ((uint8_t)50ULL): goto ma7; case ((uint8_t)51ULL): goto ma7; case ((uint8_t)52ULL): goto ma7; case ((uint8_t)53ULL): goto ma7; case ((uint8_t)54ULL): goto ma7; case ((uint8_t)55ULL): goto ma7; case ((uint8_t)56ULL): goto ma7; case ((uint8_t)57ULL): goto ma7; case ((uint8_t)58ULL): goto ma7; default: goto ma7; }
ma4:;
        uint8_t t11 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t11 != ((uint8_t)15ULL)) goto mn10;
        uint32_t t12 = GM_LD(uint32_t, ((((uint8_t *)t2) + 8) + 8));
        gm_l2 = t12;
        uint32_t t13 = gm_l2;
        gm_t9805qtonbsag t14;
        GM_ST(uint8_t, (uint8_t *)&t14 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t14 + 6, 0);
        GM_ST(uint32_t, ((uint8_t *)&t14), t13);
        ((uint8_t *)&t14)[4] = 1;
        uint8_t t15 = 0;
        F.ret = t14;
        goto gm_fret;
        goto me3;
mn10:;
ma5:;
        uint8_t t17 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t17 != ((uint8_t)19ULL)) goto mn16;
        void * t18 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l3 = t18;
        void * t19 = gm_l0;
        void * t20 = gm_l3;
        gm_t9805qtonbsag t21 = gm_f_root_local_jogho27(t19, t20);
        uint8_t t22 = 0;
        F.ret = t21;
        goto gm_fret;
        goto me3;
mn16:;
ma6:;
        uint8_t t24 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t24 != ((uint8_t)20ULL)) goto mn23;
        void * t25 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l4 = t25;
        void * t27 = gm_l0;
        void * t28 = (void *)(((uint8_t *)t27) + 8);
        void * t29 = gm_l4;
        gm_t88rcucg1b7uk t30 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t29) + 0));
        gm_t53at80pfdus3 t31;
        gm_f_kind_jcnt4hm((void *)&t31, t28, GM_CONV(gm_csguankko4lmhd, t30));
        gm_t53at80pfdus3 t32 = t31;
        uint8_t t33;
        uint8_t t36 = GM_LD(uint8_t, (((uint8_t *)&t32) + 0));
        if (t36 != ((uint8_t)11ULL)) goto mn35;
        t33 = ((uint8_t)1);
        goto me34;
mn35:;
        t33 = ((uint8_t)0);
        goto me34;
mn37:;
        gm_trap("no match arm matched the scrutinee", gm_srcsloagb2nrd8g, 89, 34);
me34:;
        if (!t33) goto ie26;
        {
            void * t39 = gm_l0;
            void * t40 = gm_l4;
            gm_t9805qtonbsag t41 = gm_f_root_local_jogho27(t39, t40);
            uint8_t t42 = 0;
            F.ret = t41;
            goto gm_fret;
        }
        goto ix26;
ie26:;
ix26:;
        goto me3;
mn23:;
ma7:;
        goto me3;
mn43:;
ma8:;
        gm_trap("no match arm matched the scrutinee", gm_srcsloagb2nrd8g, 86, 5);
me3:;
    }
    {
        gm_t9805qtonbsag t45;
        memset(&t45, 0, sizeof t45);
        uint8_t t46 = 0;
        F.ret = t45;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_unaligned_q31c519(void * a0, void * a1, gm_closure a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_unaligned_q31c519 F;
    void * gm_l1; /* e */
    void * gm_l3; /* p */
    gm_tao63pk3r18v5 gm_l4; /* sl */
    gm_t88rcucg1b7uk gm_l5; /* elem */
    GM_STACK_CHECK(&F, 458u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = a1;
    F.l2 = a2;
    {
        void * t2 = gm_l1;
        uint8_t t8 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        switch (t8) { case ((uint8_t)0ULL): goto ma6; case ((uint8_t)1ULL): goto ma6; case ((uint8_t)2ULL): goto ma6; case ((uint8_t)3ULL): goto ma6; case ((uint8_t)4ULL): goto ma6; case ((uint8_t)5ULL): goto ma6; case ((uint8_t)6ULL): goto ma6; case ((uint8_t)7ULL): goto ma6; case ((uint8_t)8ULL): goto ma6; case ((uint8_t)9ULL): goto ma6; case ((uint8_t)10ULL): goto ma6; case ((uint8_t)11ULL): goto ma6; case ((uint8_t)12ULL): goto ma6; case ((uint8_t)13ULL): goto ma6; case ((uint8_t)14ULL): goto ma6; case ((uint8_t)15ULL): goto ma6; case ((uint8_t)16ULL): goto ma6; case ((uint8_t)17ULL): goto ma6; case ((uint8_t)18ULL): goto ma6; case ((uint8_t)19ULL): goto ma6; case ((uint8_t)20ULL): goto ma6; case ((uint8_t)21ULL): goto ma5; case ((uint8_t)22ULL): goto ma6; case ((uint8_t)23ULL): goto ma6; case ((uint8_t)24ULL): goto ma4; case ((uint8_t)25ULL): goto ma6; case ((uint8_t)26ULL): goto ma6; case ((uint8_t)27ULL): goto ma6; case ((uint8_t)28ULL): goto ma6; case ((uint8_t)29ULL): goto ma6; case ((uint8_t)30ULL): goto ma6; case ((uint8_t)31ULL): goto ma6; case ((uint8_t)32ULL): goto ma6; case ((uint8_t)33ULL): goto ma6; case ((uint8_t)34ULL): goto ma6; case ((uint8_t)35ULL): goto ma6; case ((uint8_t)36ULL): goto ma6; case ((uint8_t)37ULL): goto ma6; case ((uint8_t)38ULL): goto ma6; case ((uint8_t)39ULL): goto ma6; case ((uint8_t)40ULL): goto ma6; case ((uint8_t)41ULL): goto ma6; case ((uint8_t)42ULL): goto ma6; case ((uint8_t)43ULL): goto ma6; case ((uint8_t)44ULL): goto ma6; case ((uint8_t)45ULL): goto ma6; case ((uint8_t)46ULL): goto ma6; case ((uint8_t)47ULL): goto ma6; case ((uint8_t)48ULL): goto ma6; case ((uint8_t)49ULL): goto ma6; case ((uint8_t)50ULL): goto ma6; case ((uint8_t)51ULL): goto ma6; case ((uint8_t)52ULL): goto ma6; case ((uint8_t)53ULL): goto ma6; case ((uint8_t)54ULL): goto ma6; case ((uint8_t)55ULL): goto ma6; case ((uint8_t)56ULL): goto ma6; case ((uint8_t)57ULL): goto ma6; case ((uint8_t)58ULL): goto ma6; default: goto ma6; }
ma4:;
        uint8_t t10 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t10 != ((uint8_t)24ULL)) goto mn9;
        void * t11 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l3 = t11;
        void * t13 = F.l0;
        void * t14 = gm_l3;
        uint8_t t15 = gm_f_packed_place_5ck3439(t13, t14);
        if (!t15) goto ie12;
        {
            gm_closure t17 = F.l2;
            void * t18 = gm_l1;
            void * t19 = gm_l3;
            gm_t88rcucg1b7uk t20 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t19) + 0));
            gm_closure t21 = t17;
            gm_node *ccx22 = t21.cx;
            void * t23 = t21.code;
            gm_b0_1 t24;
            if (GM_PTR_TAGGED(t23)) {
                ((void (*)(void *, gm_csguankko4lmhd))GM_PTR_UNTAG(t23))(t18, GM_CONV(gm_csguankko4lmhd, t20));
            } else {
                t24 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *, gm_t88rcucg1b7uk))t23)(t21.env, ccx22, NULL, t18, t20);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix12;
ie12:;
ix12:;
        goto me3;
mn9:;
ma5:;
        uint8_t t26 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t26 != ((uint8_t)21ULL)) goto mn25;
        gm_tao63pk3r18v5 t27 = GM_LD(gm_tao63pk3r18v5, ((((uint8_t *)t2) + 8) + 8));
        gm_l4 = t27;
        void * t29 = F.l0;
        void * t30 = GM_LD(void *, (((uint8_t *)&gm_l4) + 0));
        uint8_t t31 = gm_f_packed_place_5ck3439(t29, t30);
        if (!t31) goto ie28;
        void * t32 = F.l0;
        void * t33 = (void *)(((uint8_t *)t32) + 8);
        void * t34 = GM_LD(void *, (((uint8_t *)&gm_l4) + 0));
        gm_t88rcucg1b7uk t35 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t34) + 0));
        gm_t53at80pfdus3 t36;
        gm_f_kind_jcnt4hm((void *)&t36, t33, GM_CONV(gm_csguankko4lmhd, t35));
        gm_t53at80pfdus3 t37 = t36;
        uint8_t t38 = GM_LD(uint8_t, (((uint8_t *)&t37) + 0));
        if (t38 != ((uint8_t)11ULL)) goto ie28;
        gm_t88rcucg1b7uk t39 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t37) + 8) + 8));
        gm_l5 = t39;
        {
            gm_closure t41 = F.l2;
            void * t42 = gm_l1;
            gm_t88rcucg1b7uk t43 = gm_l5;
            gm_closure t44 = t41;
            gm_node *ccx45 = t44.cx;
            void * t46 = t44.code;
            gm_b0_1 t47;
            if (GM_PTR_TAGGED(t46)) {
                ((void (*)(void *, gm_csguankko4lmhd))GM_PTR_UNTAG(t46))(t42, GM_CONV(gm_csguankko4lmhd, t43));
            } else {
                t47 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, void *, gm_t88rcucg1b7uk))t46)(t44.env, ccx45, NULL, t42, t43);
            }
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix28;
ie28:;
ix28:;
        goto me3;
mn25:;
ma6:;
        goto me3;
mn48:;
ma7:;
        gm_trap("no match arm matched the scrutinee", gm_srcsloagb2nrd8g, 98, 5);
me3:;
    }
    {
        void * t50 = gm_l1;
        gm_closure t51 = ((gm_closure){(void *)gm_f_fn_iu2kf5m, (void *)&F.h, cx});
        gm_f_for_each_child_fm49s73(t50, t51);
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

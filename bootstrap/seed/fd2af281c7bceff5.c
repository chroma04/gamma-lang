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
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint8_t f0; uint8_t p1[3]; union { int32_t v0; } f1; } gm_toe5fmr3vvf4d;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_b16_8 v1; } f1; } gm_t3ujhu6qph7de;
typedef struct { uint64_t f0; gm_t9qkh0rsooc8d f1; uint8_t p2[7]; int64_t f2; uint32_t f3; uint8_t pe[4]; } gm_t5j14m8ru132p;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t f1; } gm_taari1s0m51a7;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { uint64_t f0; gm_b16_8 f1; } gm_tf1bjg81u0glv;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; } f1; } gm_tl572d7579hns;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
_Static_assert(sizeof(gm_toe5fmr3vvf4d) == 8 && _Alignof(gm_toe5fmr3vvf4d) == 4 && offsetof(gm_toe5fmr3vvf4d, f0) == 0 && offsetof(gm_toe5fmr3vvf4d, f1) == 4, "layout of `IoError`");
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_toe5fmr3vvf4d v0; } f1; } gm_ttln0d0hd70en;
_Static_assert(sizeof(gm_t3ujhu6qph7de) == 24 && _Alignof(gm_t3ujhu6qph7de) == 8 && offsetof(gm_t3ujhu6qph7de, f0) == 0 && offsetof(gm_t3ujhu6qph7de, f1) == 8, "layout of `string!string`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_trqfvmjtt4sij v1; } f1; } gm_t470aqkia5212;
_Static_assert(sizeof(gm_t5j14m8ru132p) == 32 && _Alignof(gm_t5j14m8ru132p) == 8 && offsetof(gm_t5j14m8ru132p, f0) == 0 && offsetof(gm_t5j14m8ru132p, f1) == 8 && offsetof(gm_t5j14m8ru132p, f2) == 16 && offsetof(gm_t5j14m8ru132p, f3) == 24, "layout of `FileInfo`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_taari1s0m51a7) == 2 && _Alignof(gm_taari1s0m51a7) == 1 && offsetof(gm_taari1s0m51a7, f0) == 0 && offsetof(gm_taari1s0m51a7, f1) == 1, "layout of `?UnOp`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tf1bjg81u0glv) == 24 && _Alignof(gm_tf1bjg81u0glv) == 8 && offsetof(gm_tf1bjg81u0glv, f0) == 0 && offsetof(gm_tf1bjg81u0glv, f1) == 8, "layout of `(i: usize, x: string)`");
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `Toolchain`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
typedef struct { gm_taari1s0m51a7 f0; gm_tqs5p4n2tq19v f1; uint8_t p2[4]; gm_th2lqbo77pvsc f2; gm_th2lqbo77pvsc f3; gm_th2lqbo77pvsc f4; gm_th2lqbo77pvsc f5; gm_th2lqbo77pvsc f6; gm_th2lqbo77pvsc f7; } gm_ti0g3bmheuq9o;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; gm_t5j14m8ru132p v1; } f1; } gm_tih6h4avv5nrb;
typedef struct { gm_trqfvmjtt4sij f0; } gm_tin97tt4ud0s1;
_Static_assert(sizeof(gm_tl572d7579hns) == 24 && _Alignof(gm_tl572d7579hns) == 8 && offsetof(gm_tl572d7579hns, f0) == 0 && offsetof(gm_tl572d7579hns, f1) == 8, "layout of `string!()`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
typedef struct { gm_trqfvmjtt4sij f0; uint8_t f1; uint8_t pe[7]; } gm_tqetrgo0q24b5;
typedef struct { gm_trqfvmjtt4sij f0; gm_trqfvmjtt4sij f1; gm_trqfvmjtt4sij f2; } gm_tr8dvlu9ik4gj;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_th2lqbo77pvsc f2; gm_th2lqbo77pvsc f3; gm_b16_8 f4; gm_b16_8 f5; gm_th2lqbo77pvsc f6; gm_b16_8 f7; gm_b16_8 f8; } gm_ts1nru0t7g7aj;
typedef struct { gm_b16_8 f0; gm_th2lqbo77pvsc f1; gm_th2lqbo77pvsc f2; } gm_tt008mgut8glb;
_Static_assert(sizeof(gm_ttln0d0hd70en) == 12 && _Alignof(gm_ttln0d0hd70en) == 4 && offsetof(gm_ttln0d0hd70en, f0) == 0 && offsetof(gm_ttln0d0hd70en, f1) == 4, "layout of `IoError!()`");
typedef struct { gm_tin97tt4ud0s1 f0; gm_tin97tt4ud0s1 f1; } gm_t08dlk5fmn1l9;
_Static_assert(sizeof(gm_t470aqkia5212) == 48 && _Alignof(gm_t470aqkia5212) == 8 && offsetof(gm_t470aqkia5212, f0) == 0 && offsetof(gm_t470aqkia5212, f1) == 8, "layout of `string!List(string)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_tr8dvlu9ik4gj v1; } f1; } gm_t5detta1b4gnk;
_Static_assert(sizeof(gm_ti0g3bmheuq9o) == 152 && _Alignof(gm_ti0g3bmheuq9o) == 8 && offsetof(gm_ti0g3bmheuq9o, f0) == 0 && offsetof(gm_ti0g3bmheuq9o, f1) == 2 && offsetof(gm_ti0g3bmheuq9o, f2) == 8 && offsetof(gm_ti0g3bmheuq9o, f3) == 32 && offsetof(gm_ti0g3bmheuq9o, f4) == 56 && offsetof(gm_ti0g3bmheuq9o, f5) == 80 && offsetof(gm_ti0g3bmheuq9o, f6) == 104 && offsetof(gm_ti0g3bmheuq9o, f7) == 128, "layout of `BuildSettings`");
typedef struct { gm_ts1nru0t7g7aj f0; uint8_t f1; uint8_t pe[7]; } gm_tie69pa8pplgc;
_Static_assert(sizeof(gm_tih6h4avv5nrb) == 40 && _Alignof(gm_tih6h4avv5nrb) == 8 && offsetof(gm_tih6h4avv5nrb, f0) == 0 && offsetof(gm_tih6h4avv5nrb, f1) == 8, "layout of `IoError!FileInfo`");
_Static_assert(sizeof(gm_tin97tt4ud0s1) == 40 && _Alignof(gm_tin97tt4ud0s1) == 8 && offsetof(gm_tin97tt4ud0s1, f0) == 0, "layout of `SourceMap`");
typedef struct { gm_th2lqbo77pvsc f0; gm_th2lqbo77pvsc f1; gm_trqfvmjtt4sij f2; gm_ti0g3bmheuq9o f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; } gm_tm43kqe3e8b4a;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tin97tt4ud0s1 v0; gm_b16_8 v1; gm_b16_8 v2; gm_b16_8 v3; } f1; } gm_toq2f2q4opoa3;
_Static_assert(sizeof(gm_tqetrgo0q24b5) == 48 && _Alignof(gm_tqetrgo0q24b5) == 8 && offsetof(gm_tqetrgo0q24b5, f0) == 0 && offsetof(gm_tqetrgo0q24b5, f1) == 40, "layout of `?List(usize)`");
_Static_assert(sizeof(gm_tr8dvlu9ik4gj) == 120 && _Alignof(gm_tr8dvlu9ik4gj) == 8 && offsetof(gm_tr8dvlu9ik4gj, f0) == 0 && offsetof(gm_tr8dvlu9ik4gj, f1) == 40 && offsetof(gm_tr8dvlu9ik4gj, f2) == 80, "layout of `Extras`");
_Static_assert(sizeof(gm_ts1nru0t7g7aj) == 168 && _Alignof(gm_ts1nru0t7g7aj) == 8 && offsetof(gm_ts1nru0t7g7aj, f0) == 0 && offsetof(gm_ts1nru0t7g7aj, f1) == 16 && offsetof(gm_ts1nru0t7g7aj, f2) == 32 && offsetof(gm_ts1nru0t7g7aj, f3) == 56 && offsetof(gm_ts1nru0t7g7aj, f4) == 80 && offsetof(gm_ts1nru0t7g7aj, f5) == 96 && offsetof(gm_ts1nru0t7g7aj, f6) == 112 && offsetof(gm_ts1nru0t7g7aj, f7) == 136 && offsetof(gm_ts1nru0t7g7aj, f8) == 152, "layout of `LockedPackage`");
_Static_assert(sizeof(gm_tt008mgut8glb) == 64 && _Alignof(gm_tt008mgut8glb) == 8 && offsetof(gm_tt008mgut8glb, f0) == 0 && offsetof(gm_tt008mgut8glb, f1) == 16 && offsetof(gm_tt008mgut8glb, f2) == 40, "layout of `UrlSource`");
typedef struct { gm_tin97tt4ud0s1 f0; uint8_t f1; uint8_t pe[7]; } gm_tv37d3dpt2opu;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_tv37d3dpt2opu v1; } f1; } gm_t074vrl1hb4f6;
_Static_assert(sizeof(gm_t08dlk5fmn1l9) == 80 && _Alignof(gm_t08dlk5fmn1l9) == 8 && offsetof(gm_t08dlk5fmn1l9, f0) == 0 && offsetof(gm_t08dlk5fmn1l9, f1) == 40, "layout of `(res: Resolution, lock: Lock)`");
_Static_assert(sizeof(gm_t5detta1b4gnk) == 128 && _Alignof(gm_t5detta1b4gnk) == 8 && offsetof(gm_t5detta1b4gnk, f0) == 0 && offsetof(gm_t5detta1b4gnk, f1) == 8, "layout of `string!Extras`");
typedef struct { gm_b16_8 f0; gm_toq2f2q4opoa3 f1; gm_th2lqbo77pvsc f2; } gm_te5jk64pjl28k;
_Static_assert(sizeof(gm_tie69pa8pplgc) == 176 && _Alignof(gm_tie69pa8pplgc) == 8 && offsetof(gm_tie69pa8pplgc, f0) == 0 && offsetof(gm_tie69pa8pplgc, f1) == 168, "layout of `?LockedPackage`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_tm43kqe3e8b4a v1; } f1; } gm_tikairc198460;
typedef struct { gm_b16_8 f0; gm_tqs5p4n2tq19v f1; uint8_t p2[6]; gm_tl572d7579hns f2; gm_tnpifv20irnph f3; gm_tv37d3dpt2opu f4; gm_tin97tt4ud0s1 f5; gm_trqfvmjtt4sij f6; gm_trqfvmjtt4sij f7; gm_trqfvmjtt4sij f8; gm_trqfvmjtt4sij f9; gm_tin97tt4ud0s1 f10; gm_trqfvmjtt4sij f11; gm_tin97tt4ud0s1 f12; gm_tin97tt4ud0s1 f13; gm_tin97tt4ud0s1 f14; gm_tin97tt4ud0s1 f15; uint8_t f16; uint8_t p17[7]; gm_tin97tt4ud0s1 f17; gm_trqfvmjtt4sij f18; } gm_tlhfcpum9vunj;
typedef struct { gm_tv37d3dpt2opu f0; gm_tr8dvlu9ik4gj f1; } gm_tljis7hgku5bj;
_Static_assert(sizeof(gm_tm43kqe3e8b4a) == 320 && _Alignof(gm_tm43kqe3e8b4a) == 8 && offsetof(gm_tm43kqe3e8b4a, f0) == 0 && offsetof(gm_tm43kqe3e8b4a, f1) == 24 && offsetof(gm_tm43kqe3e8b4a, f2) == 48 && offsetof(gm_tm43kqe3e8b4a, f3) == 88 && offsetof(gm_tm43kqe3e8b4a, f4) == 240 && offsetof(gm_tm43kqe3e8b4a, f5) == 280, "layout of `Manifest`");
_Static_assert(sizeof(gm_toq2f2q4opoa3) == 48 && _Alignof(gm_toq2f2q4opoa3) == 8 && offsetof(gm_toq2f2q4opoa3, f0) == 0 && offsetof(gm_toq2f2q4opoa3, f1) == 8, "layout of `GitSpec`");
typedef struct { gm_t08dlk5fmn1l9 f0; uint8_t f1; uint8_t pe[7]; } gm_tuu655st500b0;
_Static_assert(sizeof(gm_tv37d3dpt2opu) == 48 && _Alignof(gm_tv37d3dpt2opu) == 8 && offsetof(gm_tv37d3dpt2opu, f0) == 0 && offsetof(gm_tv37d3dpt2opu, f1) == 40, "layout of `?Resolution`");
_Static_assert(sizeof(gm_t074vrl1hb4f6) == 56 && _Alignof(gm_t074vrl1hb4f6) == 8 && offsetof(gm_t074vrl1hb4f6, f0) == 0 && offsetof(gm_t074vrl1hb4f6, f1) == 8, "layout of `string!?Resolution`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_tuu655st500b0 v1; } f1; } gm_t8a9rhgj2oa25;
typedef struct { gm_tljis7hgku5bj f0; uint8_t f1; uint8_t pe[7]; } gm_tdsi8a7qahakk;
_Static_assert(sizeof(gm_te5jk64pjl28k) == 88 && _Alignof(gm_te5jk64pjl28k) == 8 && offsetof(gm_te5jk64pjl28k, f0) == 0 && offsetof(gm_te5jk64pjl28k, f1) == 16 && offsetof(gm_te5jk64pjl28k, f2) == 64, "layout of `GitSource`");
_Static_assert(sizeof(gm_tikairc198460) == 328 && _Alignof(gm_tikairc198460) == 8 && offsetof(gm_tikairc198460, f0) == 0 && offsetof(gm_tikairc198460, f1) == 8, "layout of `string!Manifest`");
_Static_assert(sizeof(gm_tlhfcpum9vunj) == 640 && _Alignof(gm_tlhfcpum9vunj) == 8 && offsetof(gm_tlhfcpum9vunj, f0) == 0 && offsetof(gm_tlhfcpum9vunj, f1) == 16 && offsetof(gm_tlhfcpum9vunj, f2) == 24 && offsetof(gm_tlhfcpum9vunj, f3) == 48 && offsetof(gm_tlhfcpum9vunj, f4) == 64 && offsetof(gm_tlhfcpum9vunj, f5) == 112 && offsetof(gm_tlhfcpum9vunj, f6) == 152 && offsetof(gm_tlhfcpum9vunj, f7) == 192 && offsetof(gm_tlhfcpum9vunj, f8) == 232 && offsetof(gm_tlhfcpum9vunj, f9) == 272 && offsetof(gm_tlhfcpum9vunj, f10) == 312 && offsetof(gm_tlhfcpum9vunj, f11) == 352 && offsetof(gm_tlhfcpum9vunj, f12) == 392 && offsetof(gm_tlhfcpum9vunj, f13) == 432 && offsetof(gm_tlhfcpum9vunj, f14) == 472 && offsetof(gm_tlhfcpum9vunj, f15) == 512 && offsetof(gm_tlhfcpum9vunj, f16) == 552 && offsetof(gm_tlhfcpum9vunj, f17) == 560 && offsetof(gm_tlhfcpum9vunj, f18) == 600, "layout of `Resolver`");
_Static_assert(sizeof(gm_tljis7hgku5bj) == 168 && _Alignof(gm_tljis7hgku5bj) == 8 && offsetof(gm_tljis7hgku5bj, f0) == 0 && offsetof(gm_tljis7hgku5bj, f1) == 48, "layout of `Prepared`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_te5jk64pjl28k v1; gm_tt008mgut8glb v2; } f1; } gm_tnlfbo6r4vlhg;
_Static_assert(sizeof(gm_tuu655st500b0) == 88 && _Alignof(gm_tuu655st500b0) == 8 && offsetof(gm_tuu655st500b0, f0) == 0 && offsetof(gm_tuu655st500b0, f1) == 80, "layout of `?(res: Resolution, lock: Lock)`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_tnlfbo6r4vlhg f2; } gm_t7tiv6vif86qg;
_Static_assert(sizeof(gm_t8a9rhgj2oa25) == 96 && _Alignof(gm_t8a9rhgj2oa25) == 8 && offsetof(gm_t8a9rhgj2oa25, f0) == 0 && offsetof(gm_t8a9rhgj2oa25, f1) == 8, "layout of `string!?(res: Resolution, lock: Lock)`");
_Static_assert(sizeof(gm_tdsi8a7qahakk) == 176 && _Alignof(gm_tdsi8a7qahakk) == 8 && offsetof(gm_tdsi8a7qahakk, f0) == 0 && offsetof(gm_tdsi8a7qahakk, f1) == 168, "layout of `?Prepared`");
_Static_assert(sizeof(gm_tnlfbo6r4vlhg) == 96 && _Alignof(gm_tnlfbo6r4vlhg) == 8 && offsetof(gm_tnlfbo6r4vlhg, f0) == 0 && offsetof(gm_tnlfbo6r4vlhg, f1) == 8, "layout of `Source`");
_Static_assert(sizeof(gm_t7tiv6vif86qg) == 120 && _Alignof(gm_t7tiv6vif86qg) == 8 && offsetof(gm_t7tiv6vif86qg, f0) == 0 && offsetof(gm_t7tiv6vif86qg, f1) == 16 && offsetof(gm_t7tiv6vif86qg, f2) == 24, "layout of `Dependency`");
struct gm_fr_canonicalize_epo7cln {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_dependency_dir_grk9phj {
    gm_frame h;
    gm_tv37d3dpt2opu l1; /* res */
    gm_trqfvmjtt4sij l4; /* seen */
    gm_trqfvmjtt4sij l5; /* queue */
    gm_trqfvmjtt4sij l6; /* direct */
    gm_trqfvmjtt4sij l9; /* deps */
    gm_trqfvmjtt4sij l13; /* names */
    gm_t3ujhu6qph7de ret;
};
struct gm_fr_direct_dependencies_p0chc0k {
    gm_frame h;
    gm_tv37d3dpt2opu l1; /* res */
    gm_tin97tt4ud0s1 l2; /* r */
    gm_trqfvmjtt4sij l3; /* deps */
    gm_trqfvmjtt4sij l4; /* out */
    gm_trqfvmjtt4sij l8; /* out */
    gm_tm43kqe3e8b4a l9; /* manifest */
    gm_t7tiv6vif86qg l12; /* dep */
    gm_t470aqkia5212 ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_n8lsnom {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_pkfv52g {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_exists_1uo1rk0 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_find_root_kjnm6hf {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_fn_36v2k5l {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_fn_o5cn7ts {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_io_message_p7jbrea {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_is_file_0k0up67 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_file_tcuv9rv {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_join_sb1m5es {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_list_0pjrgaq {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_epvducg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_pfs7ocg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_prepare_4l27ul2 {
    gm_frame h;
    gm_t074vrl1hb4f6 ret;
};
struct gm_fr_prepare_build_b1lg1n1 {
    gm_frame h;
    gm_tv37d3dpt2opu l4; /* res */
    gm_tr8dvlu9ik4gj l6; /* extras */
    gm_tdsi8a7qahakk ret;
};
struct gm_fr_report_changes_8drf1hi {
    gm_frame h;
    gm_tv37d3dpt2opu l0; /* old */
    gm_trqfvmjtt4sij l3; /* pkgs */
    gm_ts1nru0t7g7aj l6; /* p */
    gm_ts1nru0t7g7aj l9; /* p */
    gm_tie69pa8pplgc l10; /* prev */
    gm_tin97tt4ud0s1 l11; /* o */
    gm_ts1nru0t7g7aj l12; /* o */
    gm_tin97tt4ud0s1 l13; /* o */
    gm_ts1nru0t7g7aj l16; /* p */
    gm_b0_1 ret;
};
struct gm_fr_sort_by_2q4cr01 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_sort_by_oh2660r {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_sync_ht164ep {
    gm_frame h;
    gm_tv37d3dpt2opu l5; /* old */
    gm_tlhfcpum9vunj l6; /* r */
    gm_tuu655st500b0 l7; /* found */
    gm_t08dlk5fmn1l9 l9; /* answer */
    gm_tin97tt4ud0s1 l12; /* o */
    gm_t074vrl1hb4f6 ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq0ogca82g1f(const uint8_t *a, const uint8_t *b);
inline __attribute__((aligned(4))) void gm_f_canonicalize_epo7cln(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_canonicalize_n3e3qri(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_collect_0f0qu2r(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1, const void * a2);
__attribute__((aligned(4))) void gm_f_dependency_dir_grk9phj(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_deps_of_i4no6j9(void *gm_out, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_direct_dependencies_p0chc0k(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_display_version_94faa6b(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_n8lsnom(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_pkfv52g(void);
inline __attribute__((aligned(4))) uint8_t gm_f_exists_1uo1rk0(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_find_locked_83oh60j(void *gm_out, void * a0, gm_b16_8 a1, const void * a2);
__attribute__((aligned(4))) void gm_f_find_package_root_fs4fuqj(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_find_root_kjnm6hf(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_fn_36v2k5l(void * a0, void * a1);
__attribute__((aligned(4))) uint8_t gm_f_fn_o5cn7ts(void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_format_478hq5o(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_krr1d3v(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_rpma0gd(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_introsort_3u0hp9g(gm_b16_8 a0, gm_closure a1);
__attribute__((aligned(4))) void gm_f_introsort_atfr77p(gm_b16_8 a0, gm_closure a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_io_message_p7jbrea(void *env, gm_node *cx, gm_node *inst, gm_toe5fmr3vvf4d a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_file_0k0up67(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_file_tcuv9rv(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_join_qveh5b2(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_join_sb1m5es(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_list_0pjrgaq(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_pfs7ocg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b16_8 gm_f_os_error_text_319eh6h(void *env, gm_node *cx, gm_node *inst, gm_toe5fmr3vvf4d a0);
__attribute__((aligned(4))) void gm_f_prepare_4l27ul2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_tqs5p4n2tq19v a1);
__attribute__((aligned(4))) void gm_f_prepare_build_b1lg1n1(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_tqs5p4n2tq19v a1, const void * a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_1htqqaf(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_i535kr1(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_read_lock_8pmv1su(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_read_manifest_u9uajvn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_remove_all_h0k1qkh(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_render_lock_bj7rst0(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_report_changes_8drf1hi(void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, const void * a2);
__attribute__((aligned(4))) void gm_f_resolver_ms661bf(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_tqs5p4n2tq19v a1, const void * a2, const void * a3);
__attribute__((aligned(4))) void gm_f_run_resolver_tf2ua9j(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void gm_f_sort_by_2q4cr01(gm_b16_8 a0, gm_closure a1);
inline __attribute__((aligned(4))) void gm_f_sort_by_oh2660r(gm_b16_8 a0, gm_closure a1);
__attribute__((aligned(4))) void gm_f_sync_ht164ep(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_tqs5p4n2tq19v a1, const void * a2, uint8_t a3);
__attribute__((aligned(4))) void gm_f_write_atomic_v74ncu3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_ke5blrl(void * a0, gm_tnpifv20irnph a1);
inline bool gm_lt0iqiubfm7e(const uint8_t *a, const uint8_t *b);
GM_NOINLINE bool gm_pf454qfrpt6r(gm_node *cx, const void *d, uintptr_t n, const char *file, uint32_t line, uint32_t col);
GM_NOINLINE bool gm_pfu8dq5bikbh(gm_node *cx, const gm_type *d, const void *v, const char *file, uint32_t line, uint32_t col);
extern const uint8_t gm_s2b7lcpvalpdu[14];
extern const uint8_t gm_s3p0rbs9hc3lg[2];
extern const uint8_t gm_s4a1r52bl6d5j[87];
extern const uint8_t gm_s5lqhiu2bn6of[8];
extern const uint8_t gm_s7dobq9tho3k8[96];
extern const uint8_t gm_s9m6kd6gec368[14];
extern const uint8_t gm_sdbvaa960ee0f[5];
extern const uint8_t gm_se24c7hpp401n[11];
extern const uint8_t gm_sg5gdnrith2i6[3];
extern const uint8_t gm_sgib96mf5meqv[11];
extern const uint8_t gm_so514oj5nbhcu[2];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcc2qutubarto1[];
extern const char gm_srcdo1hnokt0mq8[];
extern const char gm_srcoj9bno4fi1q4[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_ss1enqh0qsqn8[14];
extern const gm_type gm_ty0iqiubfm7e;
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    return n == 0 || memcmp(pa, pb, n * 1) == 0;
}
inline bool gm_eq0ogca82g1f(const uint8_t *a, const uint8_t *b) {
    if (a[16] != b[16]) return false;
    return !a[16] || gm_eq0iqiubfm7e(a, b);
}
inline __attribute__((aligned(4))) void gm_f_canonicalize_epo7cln(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_canonicalize_epo7cln F;
    gm_b16_8 gm_l0; /* p */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t1 = gm_l0;
    gm_f_canonicalize_n3e3qri((void *)&(*(gm_th2lqbo77pvsc *)gm_out), (void *)NULL, cx, NULL, t1);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_n8lsnom(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_n8lsnom F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 168)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_pkfv52g(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_pkfv52g F;
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
inline __attribute__((aligned(4))) uint8_t gm_f_exists_1uo1rk0(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_exists_1uo1rk0 F;
    gm_b16_8 gm_l0; /* path */
    GM_STACK_CHECK(&F, 180u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        {
            gm_b16_8 t3 = gm_l0;
            gm_node *pn4 = gm_find(cx, 2126215853);
            if (!pn4) gm_trap("no handler installed for effect `Fs`", gm_srcdo1hnokt0mq8, 124, 9);
            gm_tih6h4avv5nrb t5;
            ((void (*)(void *, void *, gm_node *, gm_node *, gm_b16_8))pn4->clauses[5])((void *)&t5, pn4->env, pn4->hctx, pn4, t3);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tih6h4avv5nrb t6 = t5;
            gm_t5j14m8ru132p t7;
            if (!((((uint8_t *)&t6))[0] != 0)) {
                gm_t5j14m8ru132p t8 = GM_LD(gm_t5j14m8ru132p, (((uint8_t *)&t6) + 8));
                t7 = t8;
            } else {
                uint8_t t9 = 0;
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
        }
    }
    {
        uint8_t t10 = 0;
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
inline __attribute__((aligned(4))) void gm_f_find_root_kjnm6hf(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_find_root_kjnm6hf F;
    gm_b16_8 gm_l0; /* start */
    GM_STACK_CHECK(&F, 122u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t3 = gm_l0;
        gm_th2lqbo77pvsc t4;
        gm_f_canonicalize_epo7cln((void *)&t4, (void *)NULL, cx, NULL, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_th2lqbo77pvsc t5 = t4;
        uint8_t t6;
        if (((((uint8_t *)&t5))[16] != 0)) goto mn8;
        t6 = ((uint8_t)1);
        goto me7;
mn8:;
        t6 = ((uint8_t)0);
        goto me7;
mn9:;
        gm_trap("no match arm matched the scrutinee", gm_srcoj9bno4fi1q4, 11, 8);
me7:;
        if (!t6) goto ie2;
        {
            gm_th2lqbo77pvsc t11;
            memset(&t11, 0, sizeof t11);
            uint8_t t12 = 0;
            F.ret = t11;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_b16_8 t13 = gm_l0;
        gm_f_find_package_root_fs4fuqj((void *)&(*(gm_th2lqbo77pvsc *)gm_out), (void *)NULL, cx, NULL, t13);
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
inline __attribute__((aligned(4))) uint8_t gm_f_fn_36v2k5l(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fn_36v2k5l F;
    void * gm_l0; /* a */
    void * gm_l1; /* b */
    GM_STACK_CHECK_SHALLOW(&F, 263u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t t1 = 0;
    void * t2 = gm_l0;
    gm_b16_8 t3 = GM_LD(gm_b16_8, (((uint8_t *)t2) + 0));
    void * t4 = gm_l1;
    gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)t4) + 0));
    gm_b16_8 t6 = t3;
    gm_b16_8 t7 = t5;
    uint8_t t8 = (uint8_t)gm_lt0iqiubfm7e(((uint8_t *)&t6), ((uint8_t *)&t7));
    if (!t8) {
        uint8_t t9 = 0;
        void * t10 = gm_l0;
        gm_b16_8 t11 = GM_LD(gm_b16_8, (((uint8_t *)t10) + 0));
        void * t12 = gm_l1;
        gm_b16_8 t13 = GM_LD(gm_b16_8, (((uint8_t *)t12) + 0));
        gm_b16_8 t14 = t11;
        gm_b16_8 t15 = t13;
        uint8_t t16 = (gm_eq0iqiubfm7e(((uint8_t *)&t14), ((uint8_t *)&t15)));
        if (t16) {
            void * t17 = gm_l0;
            gm_b16_8 t18 = GM_LD(gm_b16_8, (((uint8_t *)t17) + 16));
            void * t19 = gm_l1;
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)t19) + 16));
            gm_b16_8 t21 = t18;
            gm_b16_8 t22 = t20;
            uint8_t t23 = (uint8_t)gm_lt0iqiubfm7e(((uint8_t *)&t21), ((uint8_t *)&t22));
            t9 = t23;
        } else t9 = 0;
        t1 = t9;
    } else t1 = 1;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_io_message_p7jbrea(void *env, gm_node *cx, gm_node *inst, gm_toe5fmr3vvf4d a0) {
    struct gm_fr_io_message_p7jbrea F;
    gm_toe5fmr3vvf4d gm_l0; /* e */
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_toe5fmr3vvf4d t1 = gm_l0;
    gm_b16_8 t2 = gm_f_os_error_text_319eh6h((void *)NULL, cx, NULL, t1);
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_file_0k0up67(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_is_file_0k0up67 F;
    gm_b16_8 gm_l0; /* p */
    GM_STACK_CHECK(&F, 35u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t1 = gm_l0;
    uint8_t t2 = gm_f_is_file_tcuv9rv((void *)NULL, cx, NULL, t1);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_join_sb1m5es(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_join_sb1m5es F;
    gm_b16_8 gm_l0; /* a */
    gm_b16_8 gm_l1; /* b */
    GM_STACK_CHECK(&F, 112u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_b16_8 t1 = gm_l0;
    gm_b16_8 t2 = gm_l1;
    gm_b16_8 t3 = gm_f_join_qveh5b2((void *)NULL, cx, NULL, t1, t2);
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
inline __attribute__((aligned(4))) void gm_f_list_0pjrgaq(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_0pjrgaq F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_pkfv52g();
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
inline __attribute__((aligned(4))) void gm_f_list_pfs7ocg(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_pfs7ocg F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_n8lsnom();
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
inline __attribute__((aligned(4))) void gm_f_sort_by_2q4cr01(gm_b16_8 a0, gm_closure a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sort_by_2q4cr01 F;
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
        gm_f_introsort_atfr77p(t2, t3);
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
inline __attribute__((aligned(4))) void gm_f_sort_by_oh2660r(gm_b16_8 a0, gm_closure a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sort_by_oh2660r F;
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
        gm_f_introsort_3u0hp9g(t2, t3);
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
inline bool gm_lt0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t na = GM_LD(uintptr_t, a + 8), nb = GM_LD(uintptr_t, b + 8);
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    uintptr_t m = na < nb ? na : nb;
    int c = m == 0 ? 0 : memcmp(pa, pb, m);
    return c != 0 ? c < 0 : na < nb;
}
__attribute__((aligned(4))) void gm_f_dependency_dir_grk9phj(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1, gm_b16_8 a2) {
    struct gm_fr_dependency_dir_grk9phj F;
    gm_b16_8 gm_l0; /* root_dir */
    gm_b16_8 gm_l2; /* name */
    gm_b16_8 gm_l3; /* root */
    uint64_t gm_l7; /* next */
    gm_b16_8 gm_l8; /* dir */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    gm_t6j6smj7qoctu gm_l12; /* d */
    gm_b16_8 gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    uint64_t gm_l16; /* i */
    gm_b16_8 gm_l17; /* n */
    GM_STACK_CHECK(&F, 1800u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    gm_l2 = a2;
    {
        gm_b16_8 t2 = gm_l0;
        gm_th2lqbo77pvsc t3;
        gm_f_canonicalize_epo7cln((void *)&t3, (void *)NULL, cx, NULL, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_th2lqbo77pvsc t4 = t3;
        gm_b16_8 t5;
        if (((((uint8_t *)&t4))[16] != 0)) {
            gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)&t4) + 0));
            t5 = t6;
        } else {
            gm_b16_8 t7 = gm_l0;
            t5 = t7;
        }
        gm_l3 = t5;
    }
    {
        gm_f_list_epvducg((void *)&F.l4, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t8 = (void *)((uint8_t *)&F.l4);
        gm_b16_8 t9 = gm_l3;
        gm_tftb1odc739t5 t10 = gm_f_push_3ef746b(t8, t9);
        gm_tftb1odc739t5 t11 = t10;
        if (((((uint8_t *)&t11))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t11) + 1, gm_srcoj9bno4fi1q4, 93, 5);
        gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t11) + 1));
    }
    {
        gm_f_list_epvducg((void *)&F.l5, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t13 = (void *)((uint8_t *)&F.l5);
        gm_b16_8 t14 = gm_l3;
        gm_tftb1odc739t5 t15 = gm_f_push_3ef746b(t13, t14);
        gm_tftb1odc739t5 t16 = t15;
        if (((((uint8_t *)&t16))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t16) + 1, gm_srcoj9bno4fi1q4, 95, 5);
        gm_b0_1 t17 = GM_LD(gm_b0_1, (((uint8_t *)&t16) + 1));
    }
    {
        gm_f_list_epvducg((void *)&F.l6, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_l7 = ((uint64_t)0ULL);
    }
    {
        for (;;) {
            uint64_t t19 = gm_l7;
            uint64_t t20 = GM_LD(uintptr_t, (((uint8_t *)&F.l5) + 0) + 8);
            uint8_t t21 = (t19 < t20);
            if (!t21) goto lb18;
            {
                uint64_t t24 = gm_l7;
                uint64_t t25 = (uintptr_t)t24;
                uint64_t t26 = GM_LD(uintptr_t, (((uint8_t *)&F.l5) + 0) + 8);
                if (t25 >= (uintptr_t)(t26)) gm_trap_index(t25, t26, gm_srcoj9bno4fi1q4, 99, 16);
                gm_b16_8 t27 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l5) + 0)) + t25 * 16));
                gm_l8 = t27;
            }
            {
                uint64_t t28 = gm_l7;
                uint64_t r29; bool o30 = __builtin_add_overflow(t28, ((uint64_t)1ULL), &r29);
                if (o30) gm_trap("integer overflow in `+`", gm_srcoj9bno4fi1q4, 100, 9);
                uint64_t t31 = r29;
                gm_l7 = t31;
            }
            {
                gm_b16_8 t32 = gm_l8;
                gm_t470aqkia5212 t33;
                gm_f_direct_dependencies_p0chc0k((void *)&t33, (void *)NULL, cx, NULL, t32, &(*(gm_tv37d3dpt2opu *)((uint8_t *)&F.l1)));
                if (gm_unw.kind) {
                    goto gm_ll22;
                }
                gm_t470aqkia5212 t34 = t33;
                if (((((uint8_t *)&t34))[0] != 0)) {
                    gm_b16_8 t35 = GM_LD(gm_b16_8, (((uint8_t *)&t34) + 8));
                    gm_t3ujhu6qph7de t36;
                    GM_ST(uint8_t, (uint8_t *)&t36 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t36 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t36 + 4, 0);
                    ((uint8_t *)&t36)[0] = 1;
                    GM_ST(gm_b16_8, ((uint8_t *)&t36 + 8), t35);
                    F.ret = t36;
                    goto gm_fret;
                }
                gm_trqfvmjtt4sij t37 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)&t34) + 8));
                F.l9 = t37;
            }
            {
                {
                    gm_b16_8 t39 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 0));
                    gm_l10 = t39;
                }
                {
                    gm_l11 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t41 = gm_l11;
                        uint64_t t42 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                        uint8_t t43 = (t41 < t42);
                        if (!t43) goto lb40;
                        {
                            uint64_t t46 = gm_l11;
                            uint64_t t47 = (uintptr_t)t46;
                            uint64_t t48 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                            gm_t6j6smj7qoctu t51 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l10)) + t47 * 32));
                            gm_l12 = t51;
                            {
                                gm_b16_8 t54 = gm_l8;
                                gm_b16_8 t55 = gm_l3;
                                gm_b16_8 t56 = t54;
                                gm_b16_8 t57 = t55;
                                uint8_t t58 = (gm_eq0iqiubfm7e(((uint8_t *)&t56), ((uint8_t *)&t57)));
                                if (!t58) goto ie53;
                                {
                                    void * t60 = (void *)((uint8_t *)&F.l6);
                                    gm_b16_8 t61 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l12) + 0));
                                    gm_tftb1odc739t5 t62 = gm_f_push_3ef746b(t60, t61);
                                    gm_tftb1odc739t5 t63 = t62;
                                    if (((((uint8_t *)&t63))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t63) + 1, gm_srcoj9bno4fi1q4, 103, 30);
                                    gm_b0_1 t64 = GM_LD(gm_b0_1, (((uint8_t *)&t63) + 1));
                                }
                                goto ix53;
ie53:;
ix53:;
                            }
                            {
                                gm_b16_8 t66 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l12) + 0));
                                gm_b16_8 t67 = gm_l2;
                                gm_b16_8 t68 = t66;
                                gm_b16_8 t69 = t67;
                                uint8_t t70 = (gm_eq0iqiubfm7e(((uint8_t *)&t68), ((uint8_t *)&t69)));
                                if (!t70) goto ie65;
                                {
                                    gm_b16_8 t72 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l12) + 16));
                                    gm_t3ujhu6qph7de t73;
                                    GM_ST(uint8_t, (uint8_t *)&t73 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t73 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t73 + 4, 0);
                                    ((uint8_t *)&t73)[0] = 0;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t73 + 8), t72);
                                    gm_t3ujhu6qph7de t74 = t73;
                                    uint8_t t75 = ((((uint8_t *)&t74))[0] != 0);
                                    F.ret = t73;
                                    goto gm_fret;
                                }
                                goto ix65;
ie65:;
ix65:;
                            }
                            {
                                gm_b16_8 t77 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l12) + 16));
                                gm_b16_8 t78 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 0));
                                gm_b16_8 t79 = t78;
                                uint8_t t80 = 0;
                                gm_b16_8 t81 = t77;
                                for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t79) + 8)); i++) if (gm_eq0iqiubfm7e(((uint8_t *)GM_LD(void *, ((uint8_t *)&t79))) + i * 16, ((uint8_t *)&t81))) { t80 = 1; break; }
                                uint8_t t82 = (uint8_t)(!t80);
                                if (!t82) goto ie76;
                                {
                                    void * t84 = (void *)((uint8_t *)&F.l4);
                                    gm_b16_8 t85 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l12) + 16));
                                    gm_tftb1odc739t5 t86 = gm_f_push_3ef746b(t84, t85);
                                    gm_tftb1odc739t5 t87 = t86;
                                    if (((((uint8_t *)&t87))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t87) + 1, gm_srcoj9bno4fi1q4, 106, 17);
                                    gm_b0_1 t88 = GM_LD(gm_b0_1, (((uint8_t *)&t87) + 1));
                                }
                                {
                                    void * t89 = (void *)((uint8_t *)&F.l5);
                                    gm_b16_8 t90 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l12) + 16));
                                    gm_tftb1odc739t5 t91 = gm_f_push_3ef746b(t89, t90);
                                    gm_tftb1odc739t5 t92 = t91;
                                    if (((((uint8_t *)&t92))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t92) + 1, gm_srcoj9bno4fi1q4, 107, 17);
                                    gm_b0_1 t93 = GM_LD(gm_b0_1, (((uint8_t *)&t92) + 1));
                                }
                                goto ix76;
ie76:;
ix76:;
                            }
                            goto me49;
mn50:;
                            gm_trap("no match arm matched the scrutinee", gm_srcoj9bno4fi1q4, 102, 9);
me49:;
                        }
lc40:;
                        {
                            uint64_t t95 = gm_l11;
                            uint64_t r96; bool o97 = __builtin_add_overflow(t95, ((uint64_t)1ULL), &r96);
                            uint64_t t98 = r96;
                            gm_l11 = t98;
                        }
                    }
lb40:;
                }
            }
lc18:;
        }
gm_ll22:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb18; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc18; }
        }
        goto gm_lad;
lb18:;
    }
    {
        uint64_t t100 = GM_LD(uintptr_t, (((uint8_t *)&F.l6) + 0) + 8);
        uint8_t t101 = ((t100 == ((uint64_t)0ULL)));
        if (!t101) goto ie99;
        {
            gm_t6j6smj7qoctu t103;
            gm_b16_8 t104 = gm_l3;
            GM_ST(gm_b16_8, (uint8_t *)&t103 + 0, t104);
            gm_b16_8 t105 = gm_l2;
            GM_ST(gm_b16_8, (uint8_t *)&t103 + 16, t105);
            gm_tabb710d9mq2e t106;
            gm_f_format_krr1d3v((void *)&t106, (void *)NULL, cx, NULL, &t103);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t107 = t106;
            if (((((uint8_t *)&t107))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t107) + 8, gm_srcoj9bno4fi1q4, 111, 40);
            gm_b16_8 t108 = GM_LD(gm_b16_8, (((uint8_t *)&t107) + 8));
            gm_b16_8 t109 = t108;
            gm_b16_8 t110 = GM_LD(gm_b16_8, ((uint8_t *)&t109));
            gm_t3ujhu6qph7de t111;
            GM_ST(uint8_t, (uint8_t *)&t111 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t111 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t111 + 4, 0);
            ((uint8_t *)&t111)[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&t111 + 8), t110);
            gm_t3ujhu6qph7de t112 = t111;
            uint8_t t113 = ((((uint8_t *)&t112))[0] != 0);
            F.ret = t111;
            goto gm_fret;
        }
        goto ix99;
ie99:;
ix99:;
    }
    {
        gm_f_list_uvfu1k1((void *)&F.l13, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t115 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
            gm_b16_8 t116 = t115;
            gm_b16_8 t117 = GM_LD(gm_b16_8, ((uint8_t *)&t116));
            gm_l14 = t117;
        }
        {
            gm_l15 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t119 = gm_l15;
                uint64_t t120 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                uint8_t t121 = (t119 < t120);
                if (!t121) goto lb118;
                {
                    gm_tf1bjg81u0glv t124;
                    uint64_t t125 = gm_l15;
                    GM_ST(uint64_t, (uint8_t *)&t124 + 0, t125);
                    uint64_t t126 = gm_l15;
                    uint64_t t127 = (uintptr_t)t126;
                    uint64_t t128 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                    gm_b16_8 t129 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l14)) + t127 * 16));
                    GM_ST(gm_b16_8, (uint8_t *)&t124 + 8, t129);
                    gm_tf1bjg81u0glv t130 = t124;
                    uint64_t t133 = GM_LD(uint64_t, (((uint8_t *)&t130) + 0));
                    gm_l16 = t133;
                    gm_b16_8 t134 = GM_LD(gm_b16_8, (((uint8_t *)&t130) + 8));
                    gm_l17 = t134;
                    {
                        uint64_t t137 = gm_l16;
                        uint8_t t138 = (t137 > ((uint64_t)0ULL));
                        if (!t138) goto ie136;
                        {
                            void * t140 = (void *)((uint8_t *)&F.l13);
                            gm_b16_8 t141;
                            GM_ST(void *, (uint8_t *)&t141, (void *)(gm_sg5gdnrith2i6));
                            GM_ST(uintptr_t, (uint8_t *)&t141 + 8, (uintptr_t)(2));
                            gm_tftb1odc739t5 t142 = gm_f_extend_0cs85gs(t140, t141);
                            gm_tftb1odc739t5 t143 = t142;
                            if (((((uint8_t *)&t143))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t143) + 1, gm_srcoj9bno4fi1q4, 114, 20);
                            gm_b0_1 t144 = GM_LD(gm_b0_1, (((uint8_t *)&t143) + 1));
                        }
                        goto ix136;
ie136:;
ix136:;
                    }
                    {
                        void * t145 = (void *)((uint8_t *)&F.l13);
                        gm_b16_8 t146 = gm_l17;
                        gm_tnpifv20irnph t147;
                        GM_ST(gm_b16_8, ((uint8_t *)&t147 + 0), t146);
                        gm_tftb1odc739t5 t148 = gm_f_write_ke5blrl(t145, t147);
                        gm_tftb1odc739t5 t149 = t148;
                        if (((((uint8_t *)&t149))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t149) + 1, gm_srcoj9bno4fi1q4, 115, 9);
                        gm_b0_1 t150 = GM_LD(gm_b0_1, (((uint8_t *)&t149) + 1));
                    }
                    goto me131;
mn132:;
                    gm_trap("no match arm matched the scrutinee", gm_srcoj9bno4fi1q4, 113, 5);
me131:;
                }
lc118:;
                {
                    uint64_t t152 = gm_l15;
                    uint64_t r153; bool o154 = __builtin_add_overflow(t152, ((uint64_t)1ULL), &r153);
                    uint64_t t155 = r153;
                    gm_l15 = t155;
                }
            }
lb118:;
        }
    }
    {
        gm_tfeab279c16rr t156;
        gm_b16_8 t157 = gm_l3;
        GM_ST(gm_b16_8, (uint8_t *)&t156 + 0, t157);
        gm_b16_8 t158 = gm_l2;
        GM_ST(gm_b16_8, (uint8_t *)&t156 + 16, t158);
        gm_b16_8 t159 = GM_LD(gm_b16_8, (((uint8_t *)&F.l13) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t156 + 32, t159);
        gm_tabb710d9mq2e t160;
        gm_f_format_478hq5o((void *)&t160, (void *)NULL, cx, NULL, &t156);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t161 = t160;
        if (((((uint8_t *)&t161))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t161) + 8, gm_srcoj9bno4fi1q4, 117, 13);
        gm_b16_8 t162 = GM_LD(gm_b16_8, (((uint8_t *)&t161) + 8));
        gm_b16_8 t163 = t162;
        gm_b16_8 t164 = GM_LD(gm_b16_8, ((uint8_t *)&t163));
        gm_t3ujhu6qph7de t165;
        GM_ST(uint8_t, (uint8_t *)&t165 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t165 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t165 + 4, 0);
        ((uint8_t *)&t165)[0] = 1;
        GM_ST(gm_b16_8, ((uint8_t *)&t165 + 8), t164);
        gm_t3ujhu6qph7de t166 = t165;
        uint8_t t167 = ((((uint8_t *)&t166))[0] != 0);
        F.ret = t165;
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
__attribute__((aligned(4))) void gm_f_direct_dependencies_p0chc0k(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1) {
    struct gm_fr_direct_dependencies_p0chc0k F;
    gm_b16_8 gm_l0; /* dir */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    gm_t6j6smj7qoctu gm_l7; /* d */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    gm_b16_8 gm_l13; /* p */
    gm_b16_8 gm_l14; /* full */
    GM_STACK_CHECK(&F, 2432u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        if (!((((uint8_t *)a1))[40] != 0)) goto ie2;
        gm_tin97tt4ud0s1 t3 = GM_LD(gm_tin97tt4ud0s1, ((uint8_t *)a1));
        F.l2 = t3;
        {
            void * t6 = (void *)((uint8_t *)&F.l2);
            gm_b16_8 t7 = gm_l0;
            gm_tqetrgo0q24b5 t8;
            gm_f_deps_of_i4no6j9((void *)&t8, t6, t7);
            gm_tqetrgo0q24b5 t9 = t8;
            if (!((((uint8_t *)&t9))[40] != 0)) goto ie5;
            gm_trqfvmjtt4sij t10 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&t9));
            F.l3 = t10;
            {
                gm_f_list_0pjrgaq((void *)&F.l4, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                {
                    gm_b16_8 t13 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
                    gm_l5 = t13;
                }
                {
                    gm_l6 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t15 = gm_l6;
                        uint64_t t16 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                        uint8_t t17 = (t15 < t16);
                        if (!t17) goto lb14;
                        {
                            uint64_t t20 = gm_l6;
                            uint64_t t21 = (uintptr_t)t20;
                            uint64_t t22 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                            gm_t6j6smj7qoctu t25 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t21 * 32));
                            gm_l7 = t25;
                            {
                                void * t27 = (void *)((uint8_t *)&F.l4);
                                gm_tftb1odc739t5 t28 = gm_f_push_i535kr1(t27, &(*(gm_t6j6smj7qoctu *)((uint8_t *)&gm_l7)));
                                gm_tftb1odc739t5 t29 = t28;
                                if (((((uint8_t *)&t29))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t29) + 1, gm_srcoj9bno4fi1q4, 126, 35);
                                gm_b0_1 t30 = GM_LD(gm_b0_1, (((uint8_t *)&t29) + 1));
                            }
                            goto me23;
mn24:;
                            gm_trap("no match arm matched the scrutinee", gm_srcoj9bno4fi1q4, 126, 13);
me23:;
                        }
lc14:;
                        {
                            uint64_t t32 = gm_l6;
                            uint64_t r33; bool o34 = __builtin_add_overflow(t32, ((uint64_t)1ULL), &r33);
                            uint64_t t35 = r33;
                            gm_l6 = t35;
                        }
                    }
lb14:;
                }
            }
            {
                gm_b16_8 t36 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 0));
                gm_closure t37 = ((gm_closure){GM_PTR_TAG(gm_f_fn_36v2k5l), NULL, cx});
                gm_f_sort_by_oh2660r(t36, t37);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                gm_trqfvmjtt4sij t38 = F.l4;
                gm_t470aqkia5212 t39;
                GM_ST(uint8_t, (uint8_t *)&t39 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t39 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t39 + 4, 0);
                ((uint8_t *)&t39)[0] = 0;
                GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t39 + 8), t38);
                gm_t470aqkia5212 t40 = t39;
                uint8_t t41 = ((((uint8_t *)&t40))[0] != 0);
                F.ret = t39;
                goto gm_fret;
            }
            goto ix5;
ie5:;
ix5:;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_f_list_0pjrgaq((void *)&F.l8, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t43 = gm_l0;
        gm_b16_8 t44;
        GM_ST(void *, (uint8_t *)&t44, (void *)(gm_se24c7hpp401n));
        GM_ST(uintptr_t, (uint8_t *)&t44 + 8, (uintptr_t)(10));
        gm_b16_8 t45 = gm_f_join_sb1m5es((void *)NULL, cx, NULL, t43, t44);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t46 = gm_f_is_file_0k0up67((void *)NULL, cx, NULL, t45);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t47 = (uint8_t)!t46;
        if (!t47) goto ie42;
        {
            gm_trqfvmjtt4sij t49 = F.l8;
            gm_t470aqkia5212 t50;
            GM_ST(uint8_t, (uint8_t *)&t50 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t50 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t50 + 4, 0);
            ((uint8_t *)&t50)[0] = 0;
            GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t50 + 8), t49);
            gm_t470aqkia5212 t51 = t50;
            uint8_t t52 = ((((uint8_t *)&t51))[0] != 0);
            F.ret = t50;
            goto gm_fret;
        }
        goto ix42;
ie42:;
ix42:;
    }
    {
        gm_b16_8 t53 = gm_l0;
        gm_tikairc198460 t54;
        gm_f_read_manifest_u9uajvn((void *)&t54, (void *)NULL, cx, NULL, t53);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tikairc198460 t55 = t54;
        if (((((uint8_t *)&t55))[0] != 0)) {
            gm_b16_8 t56 = GM_LD(gm_b16_8, (((uint8_t *)&t55) + 8));
            gm_t470aqkia5212 t57;
            GM_ST(uint8_t, (uint8_t *)&t57 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t57 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t57 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t57 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t57 + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&t57 + 40, 0);
            ((uint8_t *)&t57)[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&t57 + 8), t56);
            F.ret = t57;
            goto gm_fret;
        }
        gm_tm43kqe3e8b4a t58 = GM_LD(gm_tm43kqe3e8b4a, (((uint8_t *)&t55) + 8));
        F.l9 = t58;
    }
    {
        {
            gm_b16_8 t60 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l9) + 48) + 0));
            gm_l10 = t60;
        }
        {
            gm_l11 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t62 = gm_l11;
                uint64_t t63 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                uint8_t t64 = (t62 < t63);
                if (!t64) goto lb61;
                {
                    uint64_t t67 = gm_l11;
                    uint64_t t68 = (uintptr_t)t67;
                    uint64_t t69 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                    gm_t7tiv6vif86qg t72 = GM_LD(gm_t7tiv6vif86qg, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l10)) + t68 * 120));
                    F.l12 = t72;
                    {
                        uint8_t t75 = GM_LD(uint8_t, ((((uint8_t *)&F.l12) + 24) + 0));
                        if (t75 != ((uint8_t)0ULL)) goto ie74;
                        gm_b16_8 t76 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l12) + 24) + 8));
                        gm_l13 = t76;
                        {
                            gm_b16_8 t78 = gm_l0;
                            gm_b16_8 t79 = gm_l13;
                            gm_b16_8 t80 = gm_f_join_sb1m5es((void *)NULL, cx, NULL, t78, t79);
                            if (gm_unw.kind) {
                                goto gm_ll65;
                            }
                            gm_l14 = t80;
                        }
                        {
                            void * t81 = (void *)((uint8_t *)&F.l8);
                            gm_t6j6smj7qoctu t82;
                            gm_b16_8 t83 = GM_LD(gm_b16_8, (((uint8_t *)&F.l12) + 0));
                            GM_ST(gm_b16_8, (uint8_t *)&t82 + 0, t83);
                            gm_b16_8 t84 = gm_l14;
                            gm_th2lqbo77pvsc t85;
                            gm_f_canonicalize_epo7cln((void *)&t85, (void *)NULL, cx, NULL, t84);
                            if (gm_unw.kind) {
                                goto gm_ll65;
                            }
                            gm_th2lqbo77pvsc t86 = t85;
                            gm_b16_8 t87;
                            if (((((uint8_t *)&t86))[16] != 0)) {
                                gm_b16_8 t88 = GM_LD(gm_b16_8, (((uint8_t *)&t86) + 0));
                                t87 = t88;
                            } else {
                                gm_b16_8 t89 = gm_l14;
                                t87 = t89;
                            }
                            GM_ST(gm_b16_8, (uint8_t *)&t82 + 16, t87);
                            gm_tftb1odc739t5 t90 = gm_f_push_i535kr1(t81, &t82);
                            gm_tftb1odc739t5 t91 = t90;
                            if (((((uint8_t *)&t91))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t91) + 1, gm_srcoj9bno4fi1q4, 137, 13);
                            gm_b0_1 t92 = GM_LD(gm_b0_1, (((uint8_t *)&t91) + 1));
                        }
                        goto ix74;
ie74:;
ix74:;
                    }
                    goto me70;
mn71:;
                    gm_trap("no match arm matched the scrutinee", gm_srcoj9bno4fi1q4, 134, 5);
me70:;
                }
lc61:;
                {
                    uint64_t t94 = gm_l11;
                    uint64_t r95; bool o96 = __builtin_add_overflow(t94, ((uint64_t)1ULL), &r95);
                    uint64_t t97 = r95;
                    gm_l11 = t97;
                }
            }
gm_ll65:;
            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb61; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc61; }
            }
            goto gm_lad;
lb61:;
        }
    }
    {
        gm_trqfvmjtt4sij t98 = F.l8;
        gm_t470aqkia5212 t99;
        GM_ST(uint8_t, (uint8_t *)&t99 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t99 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t99 + 4, 0);
        ((uint8_t *)&t99)[0] = 0;
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t99 + 8), t98);
        gm_t470aqkia5212 t100 = t99;
        uint8_t t101 = ((((uint8_t *)&t100))[0] != 0);
        F.ret = t99;
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
extern __attribute__((aligned(4))) void gm_f_find_root_kjnm6hf(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) uint8_t gm_f_fn_36v2k5l(void * a0, void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_fn_o5cn7ts(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fn_o5cn7ts F;
    void * gm_l0; /* a */
    void * gm_l1; /* b */
    GM_STACK_CHECK_SHALLOW(&F, 263u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        gm_b16_8 t4 = GM_LD(gm_b16_8, (((uint8_t *)t3) + 0));
        void * t5 = gm_l1;
        gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)t5) + 0));
        gm_b16_8 t7 = t4;
        gm_b16_8 t8 = t6;
        uint8_t t9 = (!gm_eq0iqiubfm7e(((uint8_t *)&t7), ((uint8_t *)&t8)));
        if (!t9) goto ie2;
        {
            void * t11 = gm_l0;
            gm_b16_8 t12 = GM_LD(gm_b16_8, (((uint8_t *)t11) + 0));
            void * t13 = gm_l1;
            gm_b16_8 t14 = GM_LD(gm_b16_8, (((uint8_t *)t13) + 0));
            gm_b16_8 t15 = t12;
            gm_b16_8 t16 = t14;
            uint8_t t17 = (uint8_t)gm_lt0iqiubfm7e(((uint8_t *)&t15), ((uint8_t *)&t16));
            uint8_t t18 = 0;
            F.ret = t17;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t19 = gm_l0;
        gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)t19) + 16));
        void * t21 = gm_l1;
        gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)t21) + 16));
        gm_b16_8 t23 = t20;
        gm_b16_8 t24 = t22;
        uint8_t t25 = (uint8_t)gm_lt0iqiubfm7e(((uint8_t *)&t23), ((uint8_t *)&t24));
        uint8_t t26 = 0;
        F.ret = t25;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_prepare_4l27ul2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_tqs5p4n2tq19v a1) {
    struct gm_fr_prepare_4l27ul2 F;
    gm_b16_8 gm_l0; /* input */
    gm_tqs5p4n2tq19v gm_l1; /* opts */
    gm_b16_8 gm_l2; /* root */
    GM_STACK_CHECK(&F, 333u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_b16_8 t2 = gm_l0;
        gm_th2lqbo77pvsc t3;
        gm_f_find_root_kjnm6hf((void *)&t3, (void *)NULL, cx, NULL, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_th2lqbo77pvsc t4 = t3;
        gm_b16_8 t5;
        if (((((uint8_t *)&t4))[16] != 0)) {
            gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)&t4) + 0));
            t5 = t6;
        } else {
            gm_tv37d3dpt2opu t7;
            memset(&t7, 0, sizeof t7);
            gm_t074vrl1hb4f6 t8;
            GM_ST(uint8_t, (uint8_t *)&t8 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t8 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t8 + 4, 0);
            ((uint8_t *)&t8)[0] = 0;
            GM_ST(gm_tv37d3dpt2opu, ((uint8_t *)&t8 + 8), t7);
            gm_t074vrl1hb4f6 t9 = t8;
            uint8_t t10 = ((((uint8_t *)&t9))[0] != 0);
            F.ret = t8;
            goto gm_fret;
        }
        gm_l2 = t5;
    }
    {
        gm_b16_8 t11 = gm_l2;
        gm_tqs5p4n2tq19v t12 = gm_l1;
        gm_tl572d7579hns t13;
        GM_ST(uint8_t, (uint8_t *)&t13 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t13 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t13 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t13 + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&t13 + 16, 0);
        GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
        gm_f_sync_ht164ep((void *)&(*(gm_t074vrl1hb4f6 *)gm_out), (void *)NULL, cx, NULL, t11, t12, &t13, ((uint8_t)0));
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
inline __attribute__((aligned(4))) void gm_f_prepare_build_b1lg1n1(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_tqs5p4n2tq19v a1, const void * a2) {
    struct gm_fr_prepare_build_b1lg1n1 F;
    gm_b16_8 gm_l0; /* input */
    gm_tqs5p4n2tq19v gm_l1; /* opts */
    gm_th2lqbo77pvsc gm_l2; /* profile */
    gm_b16_8 gm_l3; /* e */
    gm_b16_8 gm_l5; /* e */
    GM_STACK_CHECK(&F, 1607u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_b16_8 t2 = gm_l0;
        gm_tqs5p4n2tq19v t3 = gm_l1;
        gm_t074vrl1hb4f6 t4;
        gm_f_prepare_4l27ul2((void *)&t4, (void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t074vrl1hb4f6 t5 = t4;
        gm_tv37d3dpt2opu t6;
        if (!((((uint8_t *)&t5))[0] != 0)) {
            gm_tv37d3dpt2opu t7 = GM_LD(gm_tv37d3dpt2opu, (((uint8_t *)&t5) + 8));
            t6 = t7;
        } else {
            gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)&t5) + 8));
            gm_l3 = t8;
            {
                gm_b16_8 t10 = gm_l3;
                gm_b16_8 t11 = t10;
                if (gm_pf454qfrpt6r(cx, gm_s5lqhiu2bn6of, 7, gm_srcoj9bno4fi1q4, 154, 9)) {
                    goto gm_lad;
                }
                if (gm_pfu8dq5bikbh(cx, &gm_ty0iqiubfm7e, ((uint8_t *)&t11), gm_srcoj9bno4fi1q4, 154, 9)) {
                    goto gm_lad;
                }
                if (gm_pf454qfrpt6r(cx, gm_so514oj5nbhcu, 1, gm_srcoj9bno4fi1q4, 154, 9)) {
                    goto gm_lad;
                }
            }
            gm_tdsi8a7qahakk t12;
            memset(&t12, 0, sizeof t12);
            uint8_t t13 = 0;
            F.ret = t12;
            goto gm_fret;
        }
        F.l4 = t6;
    }
    {
        gm_b16_8 t14 = gm_l0;
        gm_t5detta1b4gnk t15;
        gm_f_collect_0f0qu2r((void *)&t15, (void *)NULL, cx, NULL, t14, &(*(gm_tv37d3dpt2opu *)((uint8_t *)&F.l4)), &(*(gm_th2lqbo77pvsc *)((uint8_t *)a2)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5detta1b4gnk t16 = t15;
        gm_tr8dvlu9ik4gj t17;
        if (!((((uint8_t *)&t16))[0] != 0)) {
            gm_tr8dvlu9ik4gj t18 = GM_LD(gm_tr8dvlu9ik4gj, (((uint8_t *)&t16) + 8));
            t17 = t18;
        } else {
            gm_b16_8 t19 = GM_LD(gm_b16_8, (((uint8_t *)&t16) + 8));
            gm_l5 = t19;
            {
                gm_b16_8 t21 = gm_l5;
                gm_b16_8 t22 = t21;
                if (gm_pf454qfrpt6r(cx, gm_s5lqhiu2bn6of, 7, gm_srcoj9bno4fi1q4, 158, 9)) {
                    goto gm_lad;
                }
                if (gm_pfu8dq5bikbh(cx, &gm_ty0iqiubfm7e, ((uint8_t *)&t22), gm_srcoj9bno4fi1q4, 158, 9)) {
                    goto gm_lad;
                }
                if (gm_pf454qfrpt6r(cx, gm_so514oj5nbhcu, 1, gm_srcoj9bno4fi1q4, 158, 9)) {
                    goto gm_lad;
                }
            }
            gm_tdsi8a7qahakk t23;
            memset(&t23, 0, sizeof t23);
            uint8_t t24 = 0;
            F.ret = t23;
            goto gm_fret;
        }
        F.l6 = t17;
    }
    {
        gm_tljis7hgku5bj t25;
        memcpy((uint8_t *)&t25 + 0, ((uint8_t *)&F.l4), 48);
        memcpy((uint8_t *)&t25 + 48, ((uint8_t *)&F.l6), 120);
        gm_tdsi8a7qahakk t26;
        GM_ST(uint8_t, (uint8_t *)&t26 + 169, 0);
        GM_ST(uint16_t, (uint8_t *)&t26 + 170, 0);
        GM_ST(uint32_t, (uint8_t *)&t26 + 172, 0);
        GM_ST(gm_tljis7hgku5bj, ((uint8_t *)&t26), t25);
        ((uint8_t *)&t26)[168] = 1;
        uint8_t t27 = 0;
        F.ret = t26;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_report_changes_8drf1hi(void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, const void * a2) {
    struct gm_fr_report_changes_8drf1hi F;
    void * gm_l1; /* new */
    gm_tl572d7579hns gm_l2; /* refresh */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    gm_b16_8 gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    GM_STACK_CHECK(&F, 2204u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l1 = a1;
    {
        gm_f_list_pfs7ocg((void *)&F.l3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            void * t3 = gm_l1;
            gm_b16_8 t4 = GM_LD(gm_b16_8, ((((uint8_t *)t3) + 0) + 0));
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
                    gm_ts1nru0t7g7aj t16 = GM_LD(gm_ts1nru0t7g7aj, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t12 * 168));
                    F.l6 = t16;
                    {
                        void * t18 = (void *)((uint8_t *)&F.l3);
                        gm_tftb1odc739t5 t19 = gm_f_push_1htqqaf(t18, &(*(gm_ts1nru0t7g7aj *)((uint8_t *)&F.l6)));
                        gm_tftb1odc739t5 t20 = t19;
                        if (((((uint8_t *)&t20))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t20) + 1, gm_srcoj9bno4fi1q4, 58, 35);
                        gm_b0_1 t21 = GM_LD(gm_b0_1, (((uint8_t *)&t20) + 1));
                    }
                    goto me14;
mn15:;
                    gm_trap("no match arm matched the scrutinee", gm_srcoj9bno4fi1q4, 58, 5);
me14:;
                }
lc5:;
                {
                    uint64_t t23 = gm_l5;
                    uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
                    uint64_t t26 = r24;
                    gm_l5 = t26;
                }
            }
lb5:;
        }
    }
    {
        gm_b16_8 t27 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
        gm_closure t28 = ((gm_closure){GM_PTR_TAG(gm_f_fn_o5cn7ts), NULL, cx});
        gm_f_sort_by_2q4cr01(t27, t28);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t30 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_l7 = t30;
        }
        {
            gm_l8 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t32 = gm_l8;
                uint64_t t33 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                uint8_t t34 = (t32 < t33);
                if (!t34) goto lb31;
                {
                    uint64_t t37 = gm_l8;
                    uint64_t t38 = (uintptr_t)t37;
                    uint64_t t39 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                    gm_ts1nru0t7g7aj t42 = GM_LD(gm_ts1nru0t7g7aj, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t38 * 168));
                    F.l9 = t42;
                    {
                        gm_tie69pa8pplgc t44;
                        memset(&t44, 0, sizeof t44);
                        F.l10 = t44;
                    }
                    {
                        if (!((((uint8_t *)a0))[40] != 0)) goto ie45;
                        gm_tin97tt4ud0s1 t46 = GM_LD(gm_tin97tt4ud0s1, ((uint8_t *)a0));
                        F.l11 = t46;
                        {
                            void * t48 = (void *)((uint8_t *)&F.l11);
                            gm_b16_8 t49 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 16));
                            gm_f_find_locked_83oh60j((void *)&(*(gm_tie69pa8pplgc *)((uint8_t *)&F.l10)), t48, t49, &(*(gm_th2lqbo77pvsc *)(((uint8_t *)&F.l9) + 32)));
                        }
                        goto ix45;
ie45:;
ix45:;
                    }
                    {
                        if (!((((uint8_t *)&F.l10))[168] != 0)) goto ie50;
                        gm_ts1nru0t7g7aj t51 = GM_LD(gm_ts1nru0t7g7aj, ((uint8_t *)&F.l10));
                        F.l12 = t51;
                        {
                            uint8_t t54 = 0;
                            gm_b16_8 t55 = GM_LD(gm_b16_8, (((uint8_t *)&F.l12) + 96));
                            gm_b16_8 t56 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 96));
                            gm_b16_8 t57 = t55;
                            gm_b16_8 t58 = t56;
                            uint8_t t59 = (!gm_eq0iqiubfm7e(((uint8_t *)&t57), ((uint8_t *)&t58)));
                            if (!t59) {
                                gm_th2lqbo77pvsc t60 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)&F.l12) + 112));
                                gm_th2lqbo77pvsc t61 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)&F.l9) + 112));
                                gm_th2lqbo77pvsc t62 = t60;
                                gm_th2lqbo77pvsc t63 = t61;
                                uint8_t t64 = (!gm_eq0ogca82g1f(((uint8_t *)&t62), ((uint8_t *)&t63)));
                                t54 = t64;
                            } else t54 = 1;
                            if (!t54) goto ie53;
                            {
                                uint8_t t67 = ((GM_LD(uint8_t, ((uint8_t *)a2) + 0) == ((uint8_t)0ULL)));
                                if (!t67) goto ie66;
                                {
                                    gm_b16_8 t69 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 0));
                                    gm_b16_8 t70 = t69;
                                    void * t71 = (void *)((uint8_t *)&F.l9);
                                    gm_b16_8 t72 = gm_f_display_version_94faa6b((void *)NULL, cx, NULL, t71);
                                    if (gm_unw.kind) {
                                        goto gm_ll35;
                                    }
                                    gm_b16_8 t73 = t72;
                                    if (gm_pf454qfrpt6r(cx, gm_s2b7lcpvalpdu, 13, gm_srcoj9bno4fi1q4, 69, 21)) {
                                        goto gm_ll35;
                                    }
                                    if (gm_pfu8dq5bikbh(cx, &gm_ty0iqiubfm7e, ((uint8_t *)&t70), gm_srcoj9bno4fi1q4, 69, 21)) {
                                        goto gm_ll35;
                                    }
                                    if (gm_pf454qfrpt6r(cx, gm_s3p0rbs9hc3lg, 1, gm_srcoj9bno4fi1q4, 69, 21)) {
                                        goto gm_ll35;
                                    }
                                    if (gm_pfu8dq5bikbh(cx, &gm_ty0iqiubfm7e, ((uint8_t *)&t73), gm_srcoj9bno4fi1q4, 69, 21)) {
                                        goto gm_ll35;
                                    }
                                    if (gm_pf454qfrpt6r(cx, gm_so514oj5nbhcu, 1, gm_srcoj9bno4fi1q4, 69, 21)) {
                                        goto gm_ll35;
                                    }
                                }
                                goto ix66;
ie66:;
                                {
                                    gm_b16_8 t75 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 0));
                                    gm_b16_8 t76 = t75;
                                    void * t77 = (void *)((uint8_t *)&F.l12);
                                    gm_b16_8 t78 = gm_f_display_version_94faa6b((void *)NULL, cx, NULL, t77);
                                    if (gm_unw.kind) {
                                        goto gm_ll35;
                                    }
                                    gm_b16_8 t79 = t78;
                                    void * t80 = (void *)((uint8_t *)&F.l9);
                                    gm_b16_8 t81 = gm_f_display_version_94faa6b((void *)NULL, cx, NULL, t80);
                                    if (gm_unw.kind) {
                                        goto gm_ll35;
                                    }
                                    gm_b16_8 t82 = t81;
                                    if (gm_pf454qfrpt6r(cx, gm_ss1enqh0qsqn8, 13, gm_srcoj9bno4fi1q4, 71, 21)) {
                                        goto gm_ll35;
                                    }
                                    if (gm_pfu8dq5bikbh(cx, &gm_ty0iqiubfm7e, ((uint8_t *)&t76), gm_srcoj9bno4fi1q4, 71, 21)) {
                                        goto gm_ll35;
                                    }
                                    if (gm_pf454qfrpt6r(cx, gm_s3p0rbs9hc3lg, 1, gm_srcoj9bno4fi1q4, 71, 21)) {
                                        goto gm_ll35;
                                    }
                                    if (gm_pfu8dq5bikbh(cx, &gm_ty0iqiubfm7e, ((uint8_t *)&t79), gm_srcoj9bno4fi1q4, 71, 21)) {
                                        goto gm_ll35;
                                    }
                                    if (gm_pf454qfrpt6r(cx, gm_sdbvaa960ee0f, 4, gm_srcoj9bno4fi1q4, 71, 21)) {
                                        goto gm_ll35;
                                    }
                                    if (gm_pfu8dq5bikbh(cx, &gm_ty0iqiubfm7e, ((uint8_t *)&t82), gm_srcoj9bno4fi1q4, 71, 21)) {
                                        goto gm_ll35;
                                    }
                                    if (gm_pf454qfrpt6r(cx, gm_so514oj5nbhcu, 1, gm_srcoj9bno4fi1q4, 71, 21)) {
                                        goto gm_ll35;
                                    }
                                }
ix66:;
                            }
                            goto ix53;
ie53:;
ix53:;
                        }
                        goto ix50;
ie50:;
                        {
                            gm_b16_8 t84 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 0));
                            gm_b16_8 t85 = t84;
                            void * t86 = (void *)((uint8_t *)&F.l9);
                            gm_b16_8 t87 = gm_f_display_version_94faa6b((void *)NULL, cx, NULL, t86);
                            if (gm_unw.kind) {
                                goto gm_ll35;
                            }
                            gm_b16_8 t88 = t87;
                            if (gm_pf454qfrpt6r(cx, gm_s2b7lcpvalpdu, 13, gm_srcoj9bno4fi1q4, 75, 13)) {
                                goto gm_ll35;
                            }
                            if (gm_pfu8dq5bikbh(cx, &gm_ty0iqiubfm7e, ((uint8_t *)&t85), gm_srcoj9bno4fi1q4, 75, 13)) {
                                goto gm_ll35;
                            }
                            if (gm_pf454qfrpt6r(cx, gm_s3p0rbs9hc3lg, 1, gm_srcoj9bno4fi1q4, 75, 13)) {
                                goto gm_ll35;
                            }
                            if (gm_pfu8dq5bikbh(cx, &gm_ty0iqiubfm7e, ((uint8_t *)&t88), gm_srcoj9bno4fi1q4, 75, 13)) {
                                goto gm_ll35;
                            }
                            if (gm_pf454qfrpt6r(cx, gm_so514oj5nbhcu, 1, gm_srcoj9bno4fi1q4, 75, 13)) {
                                goto gm_ll35;
                            }
                        }
ix50:;
                    }
                    goto me40;
mn41:;
                    gm_trap("no match arm matched the scrutinee", gm_srcoj9bno4fi1q4, 63, 5);
me40:;
                }
lc31:;
                {
                    uint64_t t90 = gm_l8;
                    uint64_t r91; bool o92 = __builtin_add_overflow(t90, ((uint64_t)1ULL), &r91);
                    uint64_t t93 = r91;
                    gm_l8 = t93;
                }
            }
gm_ll35:;
            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb31; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc31; }
            }
            goto gm_lad;
lb31:;
        }
    }
    {
        if (!((((uint8_t *)a0))[40] != 0)) goto ie94;
        gm_tin97tt4ud0s1 t95 = GM_LD(gm_tin97tt4ud0s1, ((uint8_t *)a0));
        F.l13 = t95;
        {
            {
                gm_b16_8 t98 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l13) + 0) + 0));
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
                        gm_ts1nru0t7g7aj t110 = GM_LD(gm_ts1nru0t7g7aj, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l14)) + t106 * 168));
                        F.l16 = t110;
                        {
                            void * t113 = gm_l1;
                            gm_b16_8 t114 = GM_LD(gm_b16_8, (((uint8_t *)&F.l16) + 16));
                            gm_tie69pa8pplgc t115;
                            gm_f_find_locked_83oh60j((void *)&t115, t113, t114, &(*(gm_th2lqbo77pvsc *)(((uint8_t *)&F.l16) + 32)));
                            gm_tie69pa8pplgc t116 = t115;
                            uint8_t t117;
                            if (((((uint8_t *)&t116))[168] != 0)) goto mn119;
                            t117 = ((uint8_t)1);
                            goto me118;
mn119:;
                            t117 = ((uint8_t)0);
                            goto me118;
mn120:;
                            gm_trap("no match arm matched the scrutinee", gm_srcoj9bno4fi1q4, 80, 16);
me118:;
                            if (!t117) goto ie112;
                            {
                                gm_b16_8 t122 = GM_LD(gm_b16_8, (((uint8_t *)&F.l16) + 0));
                                gm_b16_8 t123 = t122;
                                void * t124 = (void *)((uint8_t *)&F.l16);
                                gm_b16_8 t125 = gm_f_display_version_94faa6b((void *)NULL, cx, NULL, t124);
                                if (gm_unw.kind) {
                                    goto gm_ll103;
                                }
                                gm_b16_8 t126 = t125;
                                if (gm_pf454qfrpt6r(cx, gm_s9m6kd6gec368, 13, gm_srcoj9bno4fi1q4, 81, 17)) {
                                    goto gm_ll103;
                                }
                                if (gm_pfu8dq5bikbh(cx, &gm_ty0iqiubfm7e, ((uint8_t *)&t123), gm_srcoj9bno4fi1q4, 81, 17)) {
                                    goto gm_ll103;
                                }
                                if (gm_pf454qfrpt6r(cx, gm_s3p0rbs9hc3lg, 1, gm_srcoj9bno4fi1q4, 81, 17)) {
                                    goto gm_ll103;
                                }
                                if (gm_pfu8dq5bikbh(cx, &gm_ty0iqiubfm7e, ((uint8_t *)&t126), gm_srcoj9bno4fi1q4, 81, 17)) {
                                    goto gm_ll103;
                                }
                                if (gm_pf454qfrpt6r(cx, gm_so514oj5nbhcu, 1, gm_srcoj9bno4fi1q4, 81, 17)) {
                                    goto gm_ll103;
                                }
                            }
                            goto ix112;
ie112:;
ix112:;
                        }
                        goto me108;
mn109:;
                        gm_trap("no match arm matched the scrutinee", gm_srcoj9bno4fi1q4, 79, 9);
me108:;
                    }
lc99:;
                    {
                        uint64_t t128 = gm_l15;
                        uint64_t r129; bool o130 = __builtin_add_overflow(t128, ((uint64_t)1ULL), &r129);
                        uint64_t t131 = r129;
                        gm_l15 = t131;
                    }
                }
gm_ll103:;
                if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb99; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc99; }
                }
                goto gm_lad;
lb99:;
            }
        }
        goto ix94;
ie94:;
ix94:;
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
__attribute__((aligned(4))) void gm_f_sync_ht164ep(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_tqs5p4n2tq19v a1, const void * a2, uint8_t a3) {
    struct gm_fr_sync_ht164ep F;
    gm_b16_8 gm_l0; /* root */
    gm_tqs5p4n2tq19v gm_l1; /* opts */
    gm_tl572d7579hns gm_l2; /* refresh */
    uint8_t gm_l3; /* explicit */
    gm_b16_8 gm_l4; /* lock_path */
    gm_toe5fmr3vvf4d gm_l8; /* e */
    gm_b16_8 gm_l10; /* text */
    uint8_t gm_l11; /* same */
    GM_STACK_CHECK(&F, 2299u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    memcpy(&gm_l2, a2, sizeof gm_l2);
    gm_l3 = a3;
    {
        gm_b16_8 t2 = gm_l0;
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sgib96mf5meqv));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(10));
        gm_b16_8 t4 = gm_f_join_sb1m5es((void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l4 = t4;
    }
    {
        gm_b16_8 t5 = gm_l4;
        gm_t074vrl1hb4f6 t6;
        gm_f_read_lock_8pmv1su((void *)&t6, (void *)NULL, cx, NULL, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t074vrl1hb4f6 t7 = t6;
        if (((((uint8_t *)&t7))[0] != 0)) {
            gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)&t7) + 8));
            gm_t074vrl1hb4f6 t9;
            memset(&t9, 0, sizeof t9);
            ((uint8_t *)&t9)[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&t9 + 8), t8);
            F.ret = t9;
            goto gm_fret;
        }
        gm_tv37d3dpt2opu t10 = GM_LD(gm_tv37d3dpt2opu, (((uint8_t *)&t7) + 8));
        F.l5 = t10;
    }
    {
        gm_b16_8 t11 = gm_l0;
        gm_tqs5p4n2tq19v t12 = gm_l1;
        gm_f_resolver_ms661bf((void *)&F.l6, (void *)NULL, cx, NULL, t11, t12, &(*(gm_tl572d7579hns *)((uint8_t *)&gm_l2)), &(*(gm_tv37d3dpt2opu *)((uint8_t *)&F.l5)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t13 = (void *)((uint8_t *)&F.l6);
        gm_t8a9rhgj2oa25 t14;
        gm_f_run_resolver_tf2ua9j((void *)&t14, (void *)NULL, cx, NULL, t13);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t8a9rhgj2oa25 t15 = t14;
        if (((((uint8_t *)&t15))[0] != 0)) {
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&t15) + 8));
            gm_t074vrl1hb4f6 t17;
            memset(&t17, 0, sizeof t17);
            ((uint8_t *)&t17)[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&t17 + 8), t16);
            F.ret = t17;
            goto gm_fret;
        }
        gm_tuu655st500b0 t18 = GM_LD(gm_tuu655st500b0, (((uint8_t *)&t15) + 8));
        F.l7 = t18;
    }
    {
        gm_tuu655st500b0 t19 = F.l7;
        gm_tuu655st500b0 t20 = t19;
        gm_t08dlk5fmn1l9 t21;
        if (((((uint8_t *)&t20))[80] != 0)) {
            gm_t08dlk5fmn1l9 t22 = GM_LD(gm_t08dlk5fmn1l9, (((uint8_t *)&t20) + 0));
            t21 = t22;
        } else {
            {
                uint8_t t25 = gm_l3;
                if (!t25) goto ie24;
                uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 1));
                uint8_t t27 = (uint8_t)!t26;
                if (!t27) goto ie24;
                gm_b16_8 t28 = gm_l4;
                uint8_t t29 = gm_f_exists_1uo1rk0((void *)NULL, cx, NULL, t28);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (!t29) goto ie24;
                {
                    gm_b16_8 t31 = gm_l4;
                    gm_ttln0d0hd70en t32 = gm_f_remove_all_h0k1qkh((void *)NULL, cx, NULL, t31);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_ttln0d0hd70en t33 = t32;
                    gm_b0_1 t34;
                    if (!((((uint8_t *)&t33))[0] != 0)) {
                        gm_b0_1 t35 = GM_LD(gm_b0_1, (((uint8_t *)&t33) + 4));
                        t34 = t35;
                    } else {
                        gm_toe5fmr3vvf4d t36 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&t33) + 4));
                        gm_l8 = t36;
                        gm_t6j6smj7qoctu t38;
                        gm_b16_8 t39 = gm_l4;
                        GM_ST(gm_b16_8, (uint8_t *)&t38 + 0, t39);
                        gm_toe5fmr3vvf4d t40 = gm_l8;
                        gm_b16_8 t41 = gm_f_io_message_p7jbrea((void *)NULL, cx, NULL, t40);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        GM_ST(gm_b16_8, (uint8_t *)&t38 + 16, t41);
                        gm_tabb710d9mq2e t42;
                        gm_f_format_rpma0gd((void *)&t42, (void *)NULL, cx, NULL, &t38);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tabb710d9mq2e t43 = t42;
                        if (((((uint8_t *)&t43))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t43) + 8, gm_srcoj9bno4fi1q4, 26, 58);
                        gm_b16_8 t44 = GM_LD(gm_b16_8, (((uint8_t *)&t43) + 8));
                        gm_b16_8 t45 = t44;
                        gm_b16_8 t46 = GM_LD(gm_b16_8, ((uint8_t *)&t45));
                        gm_t074vrl1hb4f6 t47;
                        memset(&t47, 0, sizeof t47);
                        ((uint8_t *)&t47)[0] = 1;
                        GM_ST(gm_b16_8, ((uint8_t *)&t47 + 8), t46);
                        gm_t074vrl1hb4f6 t48 = t47;
                        uint8_t t49 = ((((uint8_t *)&t48))[0] != 0);
                        F.ret = t47;
                        goto gm_fret;
                    }
                }
                goto ix24;
ie24:;
ix24:;
            }
            gm_tv37d3dpt2opu t50;
            memset(&t50, 0, sizeof t50);
            gm_t074vrl1hb4f6 t51;
            GM_ST(uint8_t, (uint8_t *)&t51 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t51 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t51 + 4, 0);
            ((uint8_t *)&t51)[0] = 0;
            GM_ST(gm_tv37d3dpt2opu, ((uint8_t *)&t51 + 8), t50);
            gm_t074vrl1hb4f6 t52 = t51;
            uint8_t t53 = ((((uint8_t *)&t52))[0] != 0);
            F.ret = t51;
            goto gm_fret;
        }
        F.l9 = t21;
    }
    {
        void * t54 = (void *)(((uint8_t *)&F.l9) + 40);
        gm_b16_8 t55 = gm_f_render_lock_bj7rst0((void *)NULL, cx, NULL, t54);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l10 = t55;
    }
    {
        gm_l11 = ((uint8_t)0);
    }
    {
        if (!((((uint8_t *)&F.l5))[40] != 0)) goto ie56;
        gm_tin97tt4ud0s1 t57 = GM_LD(gm_tin97tt4ud0s1, ((uint8_t *)&F.l5));
        F.l12 = t57;
        {
            void * t59 = (void *)((uint8_t *)&F.l12);
            gm_b16_8 t60 = gm_f_render_lock_bj7rst0((void *)NULL, cx, NULL, t59);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t61 = gm_l10;
            gm_b16_8 t62 = t60;
            gm_b16_8 t63 = t61;
            uint8_t t64 = (gm_eq0iqiubfm7e(((uint8_t *)&t62), ((uint8_t *)&t63)));
            gm_l11 = t64;
        }
        goto ix56;
ie56:;
ix56:;
    }
    {
        uint8_t t66 = gm_l11;
        uint8_t t67 = (uint8_t)!t66;
        if (!t67) goto ie65;
        {
            uint8_t t70 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 1));
            if (!t70) goto ie69;
            {
                gm_b16_8 t72;
                uint8_t t74;
                if (((((uint8_t *)&F.l5))[40] != 0)) goto mn76;
                t74 = ((uint8_t)1);
                goto me75;
mn76:;
                t74 = ((uint8_t)0);
                goto me75;
mn77:;
                gm_trap("no match arm matched the scrutinee", gm_srcoj9bno4fi1q4, 37, 24);
me75:;
                if (!t74) goto ie73;
                gm_b16_8 t79;
                GM_ST(void *, (uint8_t *)&t79, (void *)(gm_s4a1r52bl6d5j));
                GM_ST(uintptr_t, (uint8_t *)&t79 + 8, (uintptr_t)(86));
                gm_b16_8 t80 = t79;
                t72 = t80;
                goto ix73;
ie73:;
                gm_b16_8 t82;
                GM_ST(void *, (uint8_t *)&t82, (void *)(gm_s7dobq9tho3k8));
                GM_ST(uintptr_t, (uint8_t *)&t82 + 8, (uintptr_t)(95));
                gm_b16_8 t83 = t82;
                t72 = t83;
ix73:;
                gm_t074vrl1hb4f6 t84;
                memset(&t84, 0, sizeof t84);
                ((uint8_t *)&t84)[0] = 1;
                GM_ST(gm_b16_8, ((uint8_t *)&t84 + 8), t72);
                gm_t074vrl1hb4f6 t85 = t84;
                uint8_t t86 = ((((uint8_t *)&t85))[0] != 0);
                F.ret = t84;
                goto gm_fret;
            }
            goto ix69;
ie69:;
ix69:;
        }
        {
            void * t87 = (void *)(((uint8_t *)&F.l9) + 40);
            gm_b0_1 t88 = gm_f_report_changes_8drf1hi((void *)NULL, cx, NULL, &(*(gm_tv37d3dpt2opu *)((uint8_t *)&F.l5)), t87, &(*(gm_tl572d7579hns *)((uint8_t *)&gm_l2)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_b16_8 t89 = gm_l4;
            gm_b16_8 t90 = gm_l10;
            gm_tl572d7579hns t91;
            gm_f_write_atomic_v74ncu3((void *)&t91, (void *)NULL, cx, NULL, t89, t90);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tl572d7579hns t92 = t91;
            if (((((uint8_t *)&t92))[0] != 0)) {
                gm_b16_8 t93 = GM_LD(gm_b16_8, (((uint8_t *)&t92) + 8));
                gm_t074vrl1hb4f6 t94;
                memset(&t94, 0, sizeof t94);
                ((uint8_t *)&t94)[0] = 1;
                GM_ST(gm_b16_8, ((uint8_t *)&t94 + 8), t93);
                F.ret = t94;
                goto gm_fret;
            }
            gm_b0_1 t95 = GM_LD(gm_b0_1, (((uint8_t *)&t92) + 8));
        }
        goto ix65;
ie65:;
ix65:;
    }
    {
        gm_tin97tt4ud0s1 t96 = GM_LD(gm_tin97tt4ud0s1, (((uint8_t *)&F.l9) + 0));
        gm_tv37d3dpt2opu t97;
        GM_ST(uint8_t, (uint8_t *)&t97 + 41, 0);
        GM_ST(uint16_t, (uint8_t *)&t97 + 42, 0);
        GM_ST(uint32_t, (uint8_t *)&t97 + 44, 0);
        GM_ST(gm_tin97tt4ud0s1, ((uint8_t *)&t97), t96);
        ((uint8_t *)&t97)[40] = 1;
        gm_t074vrl1hb4f6 t98;
        GM_ST(uint8_t, (uint8_t *)&t98 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t98 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t98 + 4, 0);
        ((uint8_t *)&t98)[0] = 0;
        GM_ST(gm_tv37d3dpt2opu, ((uint8_t *)&t98 + 8), t97);
        gm_t074vrl1hb4f6 t99 = t98;
        uint8_t t100 = ((((uint8_t *)&t99))[0] != 0);
        F.ret = t98;
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

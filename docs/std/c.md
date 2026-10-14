# Module `std/c`

[All modules](index.md)

C's platform-dependent types (§19.1), for declaring foreign functions.

## Type aliases

### `c_schar`

```gamma
c_schar :: i8
```

C's `signed char`.

### `c_uchar`

```gamma
c_uchar :: u8
```

C's `unsigned char`.

### `c_short`

```gamma
c_short :: i16
```

C's `short`.

### `c_ushort`

```gamma
c_ushort :: u16
```

C's `unsigned short`.

### `c_int`

```gamma
c_int :: i32
```

C's `int`.

### `c_uint`

```gamma
c_uint :: u32
```

C's `unsigned int`.

### `c_longlong`

```gamma
c_longlong :: i64
```

C's `long long`.

### `c_ulonglong`

```gamma
c_ulonglong :: u64
```

C's `unsigned long long`.

### `c_size_t`

```gamma
c_size_t :: usize
```

C's `size_t`.

### `c_ssize_t`

```gamma
c_ssize_t :: isize
```

C's `ssize_t`.

### `c_ptrdiff_t`

```gamma
c_ptrdiff_t :: isize
```

C's `ptrdiff_t`.

### `c_intptr_t`

```gamma
c_intptr_t :: isize
```

C's `intptr_t`.

### `c_uintptr_t`

```gamma
c_uintptr_t :: usize
```

C's `uintptr_t`.

### `c_float`

```gamma
c_float :: f32
```

C's `float`.

### `c_double`

```gamma
c_double :: f64
```

C's `double`.

### `c_char`

```gamma
// when target.os is .Linux and (target.arch is .Aarch64 or target.arch is .Riscv64)
c_char :: u8
```

C's `char`, which is unsigned on this target.

### `c_char`

```gamma
// otherwise
c_char :: i8
```

C's `char`, which is signed on this target.

### `c_long`

```gamma
// when target.os is .Windows or target.ptr_bits is 32
c_long :: i32
```

C's `long`, 32 bits wide on this target.

### `c_ulong`

```gamma
// when target.os is .Windows or target.ptr_bits is 32
c_ulong :: u32
```

C's `unsigned long`, 32 bits wide on this target.

### `c_long`

```gamma
// otherwise
c_long :: i64
```

C's `long`, 64 bits wide on this target.

### `c_ulong`

```gamma
// otherwise
c_ulong :: u64
```

C's `unsigned long`, 64 bits wide on this target.

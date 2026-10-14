# Module `std/math`

[All modules](index.md)

Elementary functions on `f64`, written entirely in Gamma.

Nothing here calls foreign code or uses effects, so every function gives
bit-identical results in the interpreter, in compiled code and during
compile-time evaluation. The algorithms are the classic fdlibm ones (as
maintained in FreeBSD's msun): argument reduction into a small interval,
a minimax polynomial, and a careful reconstruction. All functions follow
C99 Annex F for special values: a domain error gives NaN, an overflow gives
an infinity, and the sign of zero is preserved where the result is a zero.
Except for `pow`, results are within 1 ulp of the exact value; `pow` is
within 2 ulp (in practice below 0.85 ulp).

`sin`, `cos` and `tan` reduce their argument modulo pi/2 exactly (with
Payne-Hanek style multi-precision arithmetic for large arguments), so they
are accurate for every finite argument.

## Constants

### `inf`

```gamma
inf: f64 : (0x7ff0_0000_0000_0000u64).bits(f64)
```

Positive infinity.

### `nan`

```gamma
nan: f64 : (0x7ff8_0000_0000_0000u64).bits(f64)
```

A quiet NaN.

### `pi`

```gamma
pi: f64 : 3.14159265358979323846264338327950288
```

The ratio of a circle's circumference to its diameter.

### `tau`

```gamma
tau: f64 : 6.28318530717958647692528676655900577
```

Twice `pi`, the number of radians in a full turn.

### `e`

```gamma
e: f64 : 2.71828182845904523536028747135266250
```

Euler's number, the base of the natural logarithm.

### `ln2`

```gamma
ln2: f64 : 0.693147180559945309417232121458176568
```

The natural logarithm of 2.

### `ln10`

```gamma
ln10: f64 : 2.30258509299404568401799145468436421
```

The natural logarithm of 10.

### `sqrt2`

```gamma
sqrt2: f64 : 1.41421356237309504880168872420969808
```

The square root of 2.

## Functions

### `exp`

```gamma
exp :: fn(x: f64) -> f64
```

e raised to the power `x`. It is +inf for `x` above about 709.78, +0 for
`x` below about -745.13, `exp(-inf)` is 0 and `exp(+inf)` is +inf.

### `exp2`

```gamma
exp2 :: fn(x: f64) -> f64
```

Two raised to the power `x`. It is +inf for `x >= 1024` and 0 for
`x < -1075`; exact for integral `x`.

### `expm1`

```gamma
expm1 :: fn(x: f64) -> f64
```

`exp(x) - 1`, accurate also when `x` is close to zero (where `exp(x) - 1`
would cancel). `expm1(-inf)` is -1.

### `sinh`

```gamma
sinh :: fn(x: f64) -> f64
```

The hyperbolic sine of `x`. It overflows to an infinity of the sign of `x`
for `|x|` above about 710.48.

### `cosh`

```gamma
cosh :: fn(x: f64) -> f64
```

The hyperbolic cosine of `x`. It overflows to +inf for `|x|` above about
710.48.

### `tanh`

```gamma
tanh :: fn(x: f64) -> f64
```

The hyperbolic tangent of `x`; `tanh(+-inf)` is +-1 and the sign of zero
is preserved.

### `asin`

```gamma
asin :: fn(x: f64) -> f64
```

The arc sine of `x`, in radians within [-pi/2, pi/2]. It is NaN for `|x| > 1`.

### `acos`

```gamma
acos :: fn(x: f64) -> f64
```

The arc cosine of `x`, in radians within [0, pi]. It is NaN for `|x| > 1`.

### `atan`

```gamma
atan :: fn(x: f64) -> f64
```

The arc tangent of `x`, in radians within [-pi/2, pi/2]. `atan(+-inf)` is
+-pi/2 and the sign of zero is preserved.

### `atan2`

```gamma
atan2 :: fn(y: f64, x: f64) -> f64
```

The angle of the point (`x`, `y`) from the positive x axis, in radians
within [-pi, pi]; the signs of both zeros are taken into account, so
`atan2(+0, -1)` is pi and `atan2(-0, -1)` is -pi. Note the order: `y` first.

### `log`

```gamma
log :: fn(x0: f64) -> f64
```

The natural logarithm of `x`. It is -inf for zero, NaN for negative `x`
(and for -inf), and +inf for +inf.

### `log2`

```gamma
log2 :: fn(x0: f64) -> f64
```

The base-2 logarithm of `x`; exact for powers of two. The special values
are those of `log`.

### `log10`

```gamma
log10 :: fn(x0: f64) -> f64
```

The base-10 logarithm of `x`. The special values are those of `log`.

### `log1p`

```gamma
log1p :: fn(x: f64) -> f64
```

`log(1 + x)`, accurate also when `x` is close to zero. It is -inf for
`x = -1` and NaN for `x < -1`.

### `abs`

```gamma
abs :: fn(x: f64) -> f64
```

The absolute value of `x`; the sign bit is cleared even for NaN and -0.

### `copysign`

```gamma
copysign :: fn(x: f64, y: f64) -> f64
```

A value with the magnitude of `x` and the sign of `y` (including the sign
of zeros and NaNs).

### `sqrt`

```gamma
sqrt :: fn(x: f64) -> f64
```

The square root of `x`, correctly rounded. It is NaN for negative `x`
(except -0, whose root is -0).

### `trunc`

```gamma
trunc :: fn(x: f64) -> f64
```

`x` rounded toward zero to an integral value. The result keeps the sign of
`x`, so `trunc(-0.5)` is -0. Infinities, NaNs and zeros are returned as is.

### `floor`

```gamma
floor :: fn(x: f64) -> f64
```

The largest integral value not greater than `x`.

### `ceil`

```gamma
ceil :: fn(x: f64) -> f64
```

The smallest integral value not less than `x`.

### `round`

```gamma
round :: fn(x: f64) -> f64
```

`x` rounded to the nearest integral value, halfway cases away from zero.

### `fmod`

```gamma
fmod :: fn(x: f64, y: f64) -> f64
```

The remainder of `x / y` with the sign of `x` and the magnitude below that
of `y`, computed exactly: `x - n * y` where `n` is `x / y` truncated toward
zero. It is NaN if `x` is infinite or `y` is zero, and `x` if `y` is
infinite.

### `rem`

```gamma
rem :: fn(x: f64, y: f64) -> f64
```

The remainder of `x / y` with the sign of `x`; the same as `fmod`, and the
float counterpart of the integer operator `%`.

### `pow`

```gamma
pow :: fn(x: f64, y: f64) -> f64
```

`x` raised to the power `y`, following C99 Annex F: `pow(x, 0)` and
`pow(1, y)` are 1 even for NaN, a negative `x` with a non-integral `y`
gives NaN, and overflow and underflow give an infinity and a zero with the
sign the exact result would have. The error is below 1 ulp in practice (0.82 ulp measured).

### `cbrt`

```gamma
cbrt :: fn(x: f64) -> f64
```

The real cube root of `x`, within 0.67 ulp. It is defined for negative `x`
(`cbrt(-8)` is -2) and preserves infinities, NaNs and the sign of zero.

### `hypot`

```gamma
hypot :: fn(x: f64, y: f64) -> f64
```

The length of the hypotenuse, `sqrt(x*x + y*y)`, without undue overflow or
underflow and within 1 ulp. It is +inf if either argument is infinite
(even if the other is NaN).

### `sin`

```gamma
sin :: fn(x: f64) -> f64
```

The sine of `x` (in radians). It is NaN for infinite `x`; the sign of zero
is preserved. Accurate for every finite argument, however large.

### `cos`

```gamma
cos :: fn(x: f64) -> f64
```

The cosine of `x` (in radians). It is NaN for infinite `x`. Accurate for
every finite argument, however large.

### `tan`

```gamma
tan :: fn(x: f64) -> f64
```

The tangent of `x` (in radians). It is NaN for infinite `x`; the sign of
zero is preserved. Accurate for every finite argument, however large.

# Module `std/complex`

[All modules](index.md)

Functions of a complex variable on `Complex(f64)`, written entirely in Gamma.

The type, its operators, `conj` and `abs` are in the core library; this
module adds the polar form and the transcendental functions. Everything is
built on `std/math`, so results are bit-identical on every back end and
during compile-time evaluation.

All functions return the principal value. `log` and `sqrt` have their branch
cut along the negative real axis, and the sign of a zero imaginary part
chooses the side, as in C99: `sqrt(-4 + 0i)` is `2i` and `sqrt(-4 - 0i)`
is `-2i`.

Special values follow C99 Annex G for `exp`, `log`, `sqrt` and `arg`, which
are the cases with a well-defined limit. `sin` and `cos` handle zero parts,
an infinite imaginary part and a NaN with a zero imaginary part; for other
infinite or NaN arguments they return NaN parts where Annex G prescribes
more specific values, and the sign of a NaN or of a returned zero that
Annex G leaves unspecified is not defined. `from_polar` and `pow` apply
the arithmetic of their definitions and do not special-case infinities.

## Functions

### `arg`

```gamma
arg :: fn(z: Complex(f64)) -> f64
```

The argument (phase angle) of `z` in radians, in `[-pi, pi]`. It is `pi` or
`-pi` on the negative real axis, according to the sign of the zero imaginary
part.

### `from_polar`

```gamma
from_polar :: fn(r, theta: f64) -> Complex(f64)
```

The number with magnitude `r` and argument `theta`: `r*cos(theta) +
r*sin(theta)*i`.

### `exp`

```gamma
exp :: fn(z: Complex(f64)) -> Complex(f64)
```

The exponential `e^z`. On the real axis it is exact (`exp(x + 0i)` has the
imaginary part of `z`), and a part whose true value is finite is not lost to
an intermediate overflow of `e^x`.

### `log`

```gamma
log :: fn(z: Complex(f64)) -> Complex(f64)
```

The natural logarithm: `log|z| + arg(z)*i`. The logarithm of zero has the
real part `-inf`. Near `|z| = 1` the real part is computed with `log1p`,
so it keeps its relative accuracy there.

### `sqrt`

```gamma
sqrt :: fn(z: Complex(f64)) -> Complex(f64)
```

The principal square root, with `re >= 0`. It uses the stable formulas that
avoid cancellation, and it does not overflow for large parts.

### `pow`

```gamma
pow :: fn(z, w: Complex(f64)) -> Complex(f64)
```

`z` raised to the power `w`, as `exp(w * log(z))`. `pow(z, 0)` is 1 for
every `z`. For `z = 0` the result is 0 when `re(w) > 0`, infinite on the
real axis when `w` is a negative real and NaN otherwise. Integer powers are
not exact; multiply for small integral exponents.

### `sin`

```gamma
sin :: fn(z: Complex(f64)) -> Complex(f64)
```

The sine: `sin(x)cosh(y) + cos(x)sinh(y)*i` for `z = x + yi`.

### `cos`

```gamma
cos :: fn(z: Complex(f64)) -> Complex(f64)
```

The cosine: `cos(x)cosh(y) - sin(x)sinh(y)*i` for `z = x + yi`.

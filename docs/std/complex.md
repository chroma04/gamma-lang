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

The hyperbolic functions `sinh`, `cosh` and `tanh`, the trigonometric
`tan`, and the inverse functions `asin`, `acos`, `atan`, `asinh`, `acosh`
and `atanh` follow C99 Annex G (G.6) completely: branch cuts, the signs of
zero parts, infinities and NaN. The branch cuts are listed in each
function's documentation. A NaN result has no defined sign or payload, and
where Annex G leaves the sign of an infinity or a zero optional the choice
made here is documented by the tests.

The inverse functions are the algorithms of Hull, Fairgrieve and Tang,
"Implementing the complex arcsine and arccosine functions using exception
handling" (ACM TOMS 23, 1997), in the form of FreeBSD's msun `catrig.c`
(Bruce D. Evans, BSD licence): they neither overflow for huge arguments
nor lose accuracy close to the branch cuts or the points +-1 and +-i.
`tanh` is Kahan's formula as in FreeBSD's `s_ctanh.c`, `sinh` and `cosh`
follow `s_csinh.c` and `s_ccosh.c`.

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

### `sinh`

```gamma
sinh :: fn(z: Complex(f64)) -> Complex(f64)
```

The hyperbolic sine: `sinh(x)cos(y) + cosh(x)sin(y)*i` for `z = x + yi`. It
is entire, so it has no branch cut. A part whose true value is finite is
not lost to an intermediate overflow of `e^x`. `sinh(x + 0i)` has the
imaginary part of `z`; `sinh(+-0 + yi)` is `+-0 + sin(y)*i`, and for an
infinite or NaN `y` it is `+-0 + NaN*i`; `sinh(+-inf + yi)` is
`+-inf*cos(y) + inf*sin(y)*i` for a finite `y` and `+-inf + NaN*i` for an
infinite or NaN `y`; `sinh(NaN + 0i)` is `NaN + 0i`.

### `cosh`

```gamma
cosh :: fn(z: Complex(f64)) -> Complex(f64)
```

The hyperbolic cosine: `cosh(x)cos(y) + sinh(x)sin(y)*i` for `z = x + yi`.
It is entire, so it has no branch cut; overflow is handled as in `sinh`.
`cosh(x + 0i)` is `cosh(x) + (x*0)i` (the imaginary part has the sign of
`x` times the sign of the zero); `cosh(+-0 + yi)` for an infinite or NaN
`y` is `NaN +- 0i`; `cosh(+-inf + yi)` is `inf*cos(y) +- inf*sin(y)*i` for a
finite `y` and `inf + NaN*i` for an infinite or NaN `y`; `cosh(NaN + 0i)` is
`NaN + 0i`.

### `tanh`

```gamma
tanh :: fn(z: Complex(f64)) -> Complex(f64)
```

The hyperbolic tangent, by Kahan's formula, which stays accurate when
`cosh(x)` or `tan(y)` is huge. It is entire except for poles at
`(k + 1/2)*pi*i`; it has no branch cut, and at the (inexact) double nearest
to a pole the result is large but finite. `tanh(+-inf + yi)` is
`+-1 + 0i` with the zero signed like `sin(2y)` (positive for an infinite or
NaN `y`); `tanh(+-0 + yi)` for an infinite or NaN `y` is `+-0 + NaN*i`;
`tanh(x + yi)` for finite nonzero `x` and infinite or NaN `y` is
`NaN + NaN*i`; `tanh(NaN + 0i)` is `NaN + 0i` and `tanh(NaN + yi)` for
other `y` is `NaN + NaN*i`.

### `tan`

```gamma
tan :: fn(z: Complex(f64)) -> Complex(f64)
```

The tangent, `-i*tanh(i*z)`. It is entire except for poles at
`pi/2 + k*pi` on the real axis; it has no branch cut. `tan(x + 0i)` has the
imaginary part of `z` and `tan(+-0 + yi)` has the real part `+-0`. For a
large `|y|` the value tends to `+-i`: `tan(x +- inf*i)` is `0 +- 1i` for a
finite or NaN `x`, the zero being signed like `sin(2x)` (positive for a
NaN `x`). `tan(+-0 + NaN*i)` is `+-0 + NaN*i` and `tan(NaN + 0i)` is
`NaN + 0i`.

### `asinh`

```gamma
asinh :: fn(z: Complex(f64)) -> Complex(f64)
```

The inverse hyperbolic sine, with real part in `(-inf, inf)` and imaginary
part in `[-pi/2, pi/2]`. The branch cuts are the parts of the imaginary axis
outside the open segment from `-i` to `i`; the sign of a zero real part
chooses the side: `asinh(+0 + 2i)` is `1.3169... + pi/2*i` and
`asinh(-0 + 2i)` is `-1.3169... + pi/2*i`. `asinh(-z) = -asinh(z)` and
`asinh(conj(z)) = conj(asinh(z))` hold, also for signed zeros. Special
values: `asinh(+-0 + 0i)` is the argument, `asinh(x + inf*i)` is
`+inf + pi/2*i` for positive finite `x`, `asinh(+inf + inf*i)` is
`+inf + pi/4*i`, `asinh(+inf + NaN*i)` is `+inf + NaN*i`,
`asinh(NaN + 0i)` is `NaN + 0i` and `asinh(NaN + inf*i)` is `inf + NaN*i`.
Huge arguments do not overflow.

### `asin`

```gamma
asin :: fn(z: Complex(f64)) -> Complex(f64)
```

The inverse sine, `-i*asinh(i*z)`, with real part in `[-pi/2, pi/2]`. The
branch cuts are the parts of the real axis outside `[-1, 1]`; the sign of a
zero imaginary part chooses the side: `asin(2 + 0i)` is
`pi/2 + 1.3169...i` and `asin(2 - 0i)` is `pi/2 - 1.3169...i`. For real
`x` with `|x| <= 1`, `asin(x + 0i)` is real (with the sign of the zero kept).
It is accurate close to the cuts, and huge arguments do not overflow.

### `acos`

```gamma
acos :: fn(z: Complex(f64)) -> Complex(f64)
```

The inverse cosine, with real part in `[0, pi]`. The branch cuts are the
parts of the real axis outside `[-1, 1]`, with the side chosen by the sign
of a zero imaginary part: `acos(2 + 0i)` is `0 - 1.3169...i` and
`acos(2 - 0i)` is `0 + 1.3169...i`. It is `pi/2 - asin(z)` but accurate also
near `z = 1`, where that subtraction would cancel. `acos(1 + 0i)` is
`0 - 0i`, `acos(0 + NaN*i)` is `pi/2 + NaN*i`, `acos(x + inf*i)` is
`pi/2 - inf*i`, `acos(+-inf + NaN*i)` is `NaN +- inf*i` and
`acos(NaN + inf*i)` is `NaN - inf*i`.

### `acosh`

```gamma
acosh :: fn(z: Complex(f64)) -> Complex(f64)
```

The inverse hyperbolic cosine, with real part in `[0, inf)` and imaginary
part in `[-pi, pi]`. The branch cut is the part of the real axis left of
`1`: `acosh(x + 0i)` for `x < -1` is `acosh(|x|) + pi*i`, with `-0i` it is
`acosh(|x|) - pi*i`, and for `-1 < x < 1` it is `0 +- acos(x)*i`. In
general it is `+-i*acos(z)` with the sign of `im(z)`. `acosh(+-0 + 0i)` is
`0 + pi/2*i`, `acosh(x + inf*i)` is `inf + pi/2*i`, `acosh(-inf + yi)` is
`inf + pi*i` for finite positive `y`, and `acosh(+-inf + NaN*i)` and
`acosh(NaN +- inf*i)` are `inf + NaN*i`.

### `atanh`

```gamma
atanh :: fn(z: Complex(f64)) -> Complex(f64)
```

The inverse hyperbolic tangent, with imaginary part in `[-pi/2, pi/2]`. The
branch cuts are the parts of the real axis outside `[-1, 1]`, with the side
chosen by the sign of a zero imaginary part: `atanh(2 + 0i)` has imaginary
part `+pi/2` and `atanh(2 - 0i)` has `-pi/2`. The points `+-1` are poles:
`atanh(+-1 + 0i)` is `+-inf + 0i`. It is accurate close to `+-1` and `+-i`,
and for huge arguments, where the result tends to `0 +- pi/2*i`.
`atanh(+-inf + NaN*i)` is `+-0 + NaN*i`, `atanh(NaN + inf*i)` is
`0 +- pi/2*i` and `atanh(+-0 + NaN*i)` is `+-0 + NaN*i`.

### `atan`

```gamma
atan :: fn(z: Complex(f64)) -> Complex(f64)
```

The inverse tangent, `-i*atanh(i*z)`, with real part in `[-pi/2, pi/2]`.
The branch cuts are the parts of the imaginary axis outside the open
segment from `-i` to `i`, with the side chosen by the sign of a zero real
part: `atan(0 + 2i)` has real part `+pi/2` and `atan(-0 + 2i)` has `-pi/2`.
The points `+-i` are poles. It is accurate close to them and for huge
arguments.

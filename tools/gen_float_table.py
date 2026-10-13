#!/usr/bin/env python3
"""Generates the power-of-ten table of the shortest-digits float printer (gm_shortest in
runtime/gm_rt.c, an implementation of the Schubfach algorithm).

Usage: tools/gen_float_table.py

For each decimal exponent k in [K_MIN, K_MAX] the table holds the 126-bit integer
g(k) = floor(10^-k * 2^r) + 1, with r chosen so that 2^125 <= g(k) < 2^126, split as
g = hi * 2^63 + lo. It also checks the integer approximations of the logarithms used by
the C and Rust code against exact arithmetic. The output is the initializer of gm_g10 in
runtime/gm_rt.c.
"""

from fractions import Fraction

K_MIN, K_MAX = -324, 292


def flog10pow2(q):
    return (q * 661971961083) >> 41


def flog10_three_quarters_pow2(q):
    return (q * 661971961083 - 274743187321) >> 41


def flog2pow10(e):
    return (e * 913124641741) >> 38


def check_logs():
    for q in range(-1080, 980):
        # floor(log10(2^q)) and floor(log10(3 * 2^(q-2))) by exact comparison.
        p = Fraction(2) ** q
        k = flog10pow2(q)
        assert Fraction(10) ** k <= p < Fraction(10) ** (k + 1), q
        t = Fraction(3, 4) * p
        k = flog10_three_quarters_pow2(q)
        assert Fraction(10) ** k <= t < Fraction(10) ** (k + 1), q
    for e in range(-300, 330):
        v = Fraction(10) ** e
        k = flog2pow10(e)
        assert Fraction(2) ** k <= v < Fraction(2) ** (k + 1), e


def g(k):
    v = Fraction(10) ** (-k)
    n, d = v.numerator, v.denominator
    lg = n.bit_length() - d.bit_length()
    if (n << max(0, -lg)) < (d << max(0, lg)):
        lg -= 1
    r = 125 - lg
    x = (n << r) // d if r >= 0 else n // (d << -r)
    x += 1
    assert 1 << 125 <= x < 1 << 126
    return x


def main():
    check_logs()
    for k in range(K_MIN, K_MAX + 1):
        x = g(k)
        print("    {0x%016xULL, 0x%016xULL}," % (x >> 63, x & ((1 << 63) - 1)))


main()

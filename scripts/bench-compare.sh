#!/usr/bin/env bash
# Runs `gamma bench <dir> --backend=c` with the compiler at $1 and, if given, the compiler at
# $2 (the base branch's), and writes a Markdown table of the time per run and the ratio of
# the first to the second to $GITHUB_STEP_SUMMARY (or standard output). It is informational:
# it never fails on timing, and a base compiler without `gamma bench` just leaves the ratio out.
# It fails when the benchmarks do not build or run with the compiler at $1.
#
# usage: scripts/bench-compare.sh <head gamma> <base gamma or ""> <bench directory>
set -u

head_gamma=$1
base_gamma=${2:-}
dir=${3:-bench}
out=${GITHUB_STEP_SUMMARY:-/dev/stdout}
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

# `bench "name" ... 1.23 µs/run (± 2%, …)` becomes `name<TAB>nanoseconds`.
to_ns() {
    awk '
        /^bench "/ && /\/run \(/ {
            line = $0
            name = line; sub(/^bench "/, "", name); sub(/" .*$/, "", name)
            rest = line; sub(/^.*\.\.\. /, "", rest)
            split(rest, w, " ")
            v = w[1] + 0; u = w[2]
            if (u ~ /^µs/) v *= 1e3; else if (u ~ /^ms/) v *= 1e6; else if (u ~ /^s\//) v *= 1e9
            printf "%s\t%.3f\n", name, v
        }'
}

if ! "$head_gamma" bench "$dir" --backend=c >"$tmp/head.txt" 2>"$tmp/head.err"; then
    {
        echo "### Benchmarks"
        echo
        echo "\`gamma bench $dir\` failed on this branch:"
        echo '```'
        cat "$tmp/head.err" "$tmp/head.txt"
        echo '```'
    } >>"$out"
    cat "$tmp/head.err" "$tmp/head.txt" >&2
    exit 1
fi
to_ns <"$tmp/head.txt" >"$tmp/head.tsv"

have_base=0
why_no_base="The base branch has no \`gamma bench\`, so there is nothing to compare with."
if [ -z "$base_gamma" ]; then
    why_no_base="The base branch could not be built, so there is nothing to compare with."
elif "$base_gamma" bench "$dir" --backend=c >"$tmp/base.txt" 2>"$tmp/base.err"; then
    to_ns <"$tmp/base.txt" >"$tmp/base.tsv"
    [ -s "$tmp/base.tsv" ] && have_base=1
elif ! grep -qi 'unknown command' "$tmp/base.err"; then
    # The command exists there but failed: say so rather than claim it is missing.
    why_no_base="\`gamma bench\` failed on the base branch: $(head -n 3 "$tmp/base.err" | tr '\n' ' ')"
fi

{
    echo "### Benchmarks (\`gamma bench $dir --backend=c\`)"
    echo
    if [ "$have_base" = 1 ]; then
        echo "| Benchmark | Base | This branch | Ratio |"
        echo "| --- | ---: | ---: | ---: |"
        awk -F'\t' '
            function fmt(ns) {
                if (ns < 1e3) return sprintf("%.1f ns", ns)
                if (ns < 1e6) return sprintf("%.2f µs", ns / 1e3)
                if (ns < 1e9) return sprintf("%.2f ms", ns / 1e6)
                return sprintf("%.2f s", ns / 1e9)
            }
            NR == FNR { base[$1] = $2; next }
            {
                if ($1 in base && base[$1] > 0)
                    printf "| %s | %s | %s | %.2fx |\n", $1, fmt(base[$1]), fmt($2), $2 / base[$1]
                else
                    printf "| %s | – | %s | – |\n", $1, fmt($2)
            }' "$tmp/base.tsv" "$tmp/head.tsv"
        echo
        echo "A ratio above 1 means this branch is slower. Wall-clock times on a shared runner are noisy; this table is for information and never fails the build."
    else
        echo "$why_no_base"
        echo
        echo "| Benchmark | This branch |"
        echo "| --- | ---: |"
        awk -F'\t' '{ printf "| %s | %.1f ns |\n", $1, $2 }' "$tmp/head.tsv"
    fi
} >>"$out"
exit 0

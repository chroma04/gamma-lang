#!/usr/bin/env bash
# Parses every Gamma source file in the repository with the tree-sitter grammar and
# reports the files that produce ERROR or MISSING nodes.
#
# Only files that the real compiler accepts (`gamma parse`) are counted: the conformance
# suite also holds deliberately invalid programs, which the grammar need not reject.
# All files under lib/ must parse; the script exits with status 1 if any of them does not.
#
# Usage: scripts/parse-corpus.sh [-v] [directory ...]
#   -v   also list the files that parse cleanly
#   Directories default to lib/ and tests/conformance/.
#
# Environment:
#   GAMMA         the gamma executable (default: target/{release,debug}/gamma, then PATH)
#   TREE_SITTER   the tree-sitter command (default: the npm-installed one, then PATH)

set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
repo="$(cd "$here/../.." && pwd)"

verbose=0
if [[ "${1:-}" == "-v" ]]; then
  verbose=1
  shift
fi

if [[ $# -gt 0 ]]; then
  dirs=("$@")
else
  dirs=("$repo/lib" "$repo/tests/conformance")
fi

gamma="${GAMMA:-}"
if [[ -z "$gamma" ]]; then
  for candidate in "$repo/target/release/gamma" "$repo/target/debug/gamma"; do
    if [[ -x "$candidate" ]]; then
      gamma="$candidate"
      break
    fi
  done
fi
if [[ -z "$gamma" ]]; then
  gamma="$(command -v gamma || true)"
fi
if [[ -z "$gamma" ]]; then
  echo "error: the gamma executable was not found; build it with 'cargo build' or set GAMMA" >&2
  exit 2
fi

ts="${TREE_SITTER:-}"
if [[ -z "$ts" ]]; then
  if [[ -x "$here/node_modules/.bin/tree-sitter" ]]; then
    ts="$here/node_modules/.bin/tree-sitter"
  else
    ts="$(command -v tree-sitter || true)"
  fi
fi
if [[ -z "$ts" ]]; then
  echo "error: tree-sitter was not found; run 'npm install' in $here or set TREE_SITTER" >&2
  exit 2
fi

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

accepted="$work/accepted.txt"
: > "$accepted"
rejected=0
while IFS= read -r file; do
  if "$gamma" parse "$file" > /dev/null 2>&1; then
    printf '%s\n' "$file" >> "$accepted"
  else
    rejected=$((rejected + 1))
  fi
done < <(find "${dirs[@]}" -name '*.gm' | LC_ALL=C sort)

total="$(wc -l < "$accepted" | tr -d ' ')"
if [[ "$total" -eq 0 ]]; then
  echo "no files to parse"
  exit 0
fi

# `tree-sitter parse --quiet` prints one line per file that fails to parse cleanly.
cd "$here"
failures="$work/failures.txt"
"$ts" parse --quiet --paths "$accepted" 2> /dev/null | awk -F'\t' '/Parse:/ { print $1 }' | sed 's/[[:space:]]*$//' > "$failures" || true

failed="$(wc -l < "$failures" | tr -d ' ')"
ok=$((total - failed))

if [[ "$verbose" -eq 1 ]]; then
  grep -vxFf "$failures" "$accepted" | sed "s|^$repo/||" | sed 's/^/ok:   /' || true
fi

lib_failed=0
while IFS= read -r file; do
  rel="${file#"$repo"/}"
  echo "FAIL: $rel"
  case "$rel" in lib/*) lib_failed=$((lib_failed + 1)) ;; esac
done < "$failures"

echo "parsed cleanly: $ok of $total files accepted by the compiler ($rejected others rejected by the compiler and skipped)"
awk -v ok="$ok" -v total="$total" 'BEGIN { printf "success rate: %.2f%%\n", 100 * ok / total }'

if [[ "$lib_failed" -gt 0 ]]; then
  exit 1
fi

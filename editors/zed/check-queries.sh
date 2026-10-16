#!/usr/bin/env bash
# Runs every query of the Zed extension against the Gamma sources in the repository
# with the tree-sitter CLI. A query that names a node type or a field the grammar does
# not have is an error. Run `npm ci` in ../tree-sitter-gamma first.
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)

cd "$root/editors/tree-sitter-gamma"
sources=("$root"/examples/*.gm)
while IFS= read -r f; do sources+=("$f"); done < <(find "$root/lib/std" -name '*.gm' | sort)

fail=0
count=0
for q in "$here"/languages/gamma/*.scm; do
  count=$((count + 1))
  if npx tree-sitter query "$q" "${sources[@]}" > /dev/null; then
    echo "ok   $(basename "$q")"
  else
    echo "FAIL $(basename "$q")" >&2
    fail=1
  fi
done
echo "$count queries checked against ${#sources[@]} files"
exit $fail

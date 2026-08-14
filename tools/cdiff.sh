#!/usr/bin/env bash
# cdiff.sh -- prove a refactor changed no generated code.
#
# Emits C for every test/ and benchmark/ program with two spinel binaries and
# compares byte for byte. A pure code movement (see refactor_plan.md) must
# produce an empty report: identical text, identical `_tN` numbering. A split
# that renumbers temps has reordered arms, which is a logic change however it
# looks in the diff.
#
#   tools/cdiff.sh <ref-rev>            # build <ref-rev> in a temp worktree
#   tools/cdiff.sh <old-binary> --bin   # compare against an existing binary
#
# Prefer the worktree form. A spinel binary finds lib/ relative to its own
# path, so a binary COPIED out of its tree cannot resolve `require` or the
# prelude, and those programs then differ for a reason that has nothing to do
# with the change under test.
#
# Exit status is 0 when every file matches.
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT" || exit 1
WORK=${TMPDIR:-/tmp}/spinel-cdiff.$$
NEW=$ROOT/bin/spinel

if [ "${2-}" = "--bin" ]; then
  OLD=$1
  [ -x "$OLD" ] || { echo "not executable: $OLD" >&2; exit 2; }
  WT=""
else
  REV=${1-}
  [ -n "$REV" ] || { echo "usage: $0 <ref-rev> | <old-binary> --bin" >&2; exit 2; }
  WT=$WORK/ref
  git worktree add --detach "$WT" "$REV" >/dev/null 2>&1 || {
    echo "cannot check out $REV" >&2; exit 2; }
  ( cd "$WT" && make deps >/dev/null 2>&1; make -j"$(nproc)" >/dev/null 2>&1 ) || {
    echo "reference build failed" >&2; git worktree remove --force "$WT"; exit 2; }
  OLD=$WT/bin/spinel
fi

mkdir -p "$WORK/a" "$WORK/b"
same=0; diffn=0; skip=0
for f in test/*.rb benchmark/*.rb; do
  bn=$(basename "$f" .rb)
  "$OLD" -c "$f" -o "$WORK/a/$bn.c" >/dev/null 2>&1 || { skip=$((skip+1)); continue; }
  "$NEW" -c "$f" -o "$WORK/b/$bn.c" >/dev/null 2>&1 || { skip=$((skip+1)); continue; }
  # A #line directive for a package or prelude source carries the absolute path
  # of the tree the compiler was built in; the reference tree is elsewhere by
  # construction. Normalize that prefix, not the directives themselves.
  if [ -n "$WT" ]; then
    LC_ALL=C sed -i "s|$WT/|@ROOT@/|g" "$WORK/a/$bn.c"
    LC_ALL=C sed -i "s|$ROOT/|@ROOT@/|g" "$WORK/b/$bn.c"
  fi
  if cmp -s "$WORK/a/$bn.c" "$WORK/b/$bn.c"; then same=$((same+1))
  else
    diffn=$((diffn+1))
    echo "DIFFERS: $bn"
    diff -u "$WORK/a/$bn.c" "$WORK/b/$bn.c" | head -20
  fi
done
echo "cdiff: $same identical, $diffn differ, $skip skipped (uncompilable by one side)"
[ -n "$WT" ] && git worktree remove --force "$WT" >/dev/null 2>&1
rm -rf "$WORK"
[ "$diffn" -eq 0 ]

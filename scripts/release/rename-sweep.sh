#!/usr/bin/env bash
# scripts/release/rename-sweep.sh -- 6.9 step 5: after this repository is
# renamed MilkDAWp2 -> MilkDAWp (and v1 to MilkDAWp-v1), point the text at the
# new names. Dry run by default; --apply edits the files.
#
#   bash scripts/release/rename-sweep.sh          # show what would change
#   bash scripts/release/rename-sweep.sh --apply  # change it, then review git diff
#
# Run it only AFTER both renames: before them, "Blue-Kachina/MilkDAWp" is still v1.
#
# Changes Blue-Kachina/MilkDAWp2 to Blue-Kachina/MilkDAWp everywhere (URLs, the
# update check's repository, docs, workflows).
# Leaves alone, on purpose:
#   - the dev identity "MilkDAWp2 Dev" / com.otitismedia.MilkDAWp2Dev (ADR-0007);
#   - the GHCR image ghcr.io/blue-kachina/milkdawp2-devcontainer: a package
#     doesn't follow a repo rename, and renaming it means rebuilding and
#     re-pointing CI for no user-visible gain;
#   - development_roadmap.md's history (it records what things were called).
set -euo pipefail

apply=false
[[ "${1:-}" == "--apply" ]] && apply=true
cd "$(dirname "${BASH_SOURCE[0]}")/../.."

files="$(git grep -l --untracked -e 'Blue-Kachina/MilkDAWp2' -- . ':!development_roadmap.md' ':!scripts/release/rename-sweep.sh' || true)"

echo "Blue-Kachina/MilkDAWp2 -> Blue-Kachina/MilkDAWp in:"
if [[ -n "$files" ]]; then
  for f in $files; do echo "   $f ($(grep -c 'Blue-Kachina/MilkDAWp2' "$f"))"; done
else
  echo "   (none)"
fi

if ! $apply; then
  echo
  echo "Dry run. Re-run with --apply after both repositories are renamed."
  exit 0
fi

for f in $files; do
  perl -pi -e 's{Blue-Kachina/MilkDAWp2}{Blue-Kachina/MilkDAWp}g' "$f"
done
echo
echo "Applied. Review with: git diff"

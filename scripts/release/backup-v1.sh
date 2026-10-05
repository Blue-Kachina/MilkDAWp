#!/usr/bin/env bash
# scripts/release/backup-v1.sh -- 6.9 step 1: back up the v1 repository
# before it is renamed and archived. Everything a mirror clone misses too:
# issues with comments, releases and their assets, the wiki, repo metadata.
#
#   bash scripts/release/backup-v1.sh [owner/repo] [out-dir]
#
# Needs git and the GitHub CLI, logged in (gh auth login). Safe to re-run
# into a new folder; it changes nothing on GitHub.
set -euo pipefail

repo="${1:-Blue-Kachina/MilkDAWp}"
out="${2:-milkdawp-v1-backup-$(date +%Y-%m-%d)}"
mkdir -p "$out"
out="$(cd "$out" && pwd)"
echo "Backing up $repo into $out"

# Every ref, then a single-file bundle of them (restore: git clone MilkDAWp.bundle).
git clone --mirror "https://github.com/$repo.git" "$out/repo.git"
git -C "$out/repo.git" bundle create "$out/repo.bundle" --all
git bundle verify "$out/repo.bundle" > /dev/null

# The wiki, if it has one.
git clone --mirror "https://github.com/$repo.wiki.git" "$out/wiki.git" 2> /dev/null \
  || echo "(no wiki)"

gh api "repos/$repo" > "$out/repo.json"
gh issue list --repo "$repo" --state all --limit 5000 \
  --json number,title,body,state,stateReason,labels,author,assignees,milestone,createdAt,updatedAt,closedAt,comments,url \
  > "$out/issues.json"
gh pr list --repo "$repo" --state all --limit 5000 \
  --json number,title,body,state,author,createdAt,closedAt,mergedAt,headRefName,baseRefName,url \
  > "$out/pull-requests.json"
gh label list --repo "$repo" --limit 500 --json name,color,description > "$out/labels.json"

# Releases: notes and every asset (the built v1 plugins live here, so old
# sessions can still be opened in v1).
gh release list --repo "$repo" --limit 500 --json tagName,name,isPrerelease,publishedAt > "$out/releases.json"
for tag in $(gh release list --repo "$repo" --limit 500 --json tagName --jq '.[].tagName'); do
  mkdir -p "$out/releases/$tag"
  gh release view "$tag" --repo "$repo" --json body --jq .body > "$out/releases/$tag/notes.md"
  gh release download "$tag" --repo "$repo" --dir "$out/releases/$tag" --skip-existing
done

(cd "$out" && find . -type f ! -name SHA256SUMS.txt ! -path './repo.git/*' ! -path './wiki.git/*' -print0 \
  | sort -z | xargs -0 sha256sum > SHA256SUMS.txt)
echo
echo "Done: $out"
echo "  issues: $(python3 -c "import json; print(len(json.load(open('$out/issues.json'))))" 2> /dev/null || echo '?')"
echo "  releases: $(ls "$out/releases" 2> /dev/null | wc -l | tr -d ' ')"
echo "Copy it somewhere off this machine as well."

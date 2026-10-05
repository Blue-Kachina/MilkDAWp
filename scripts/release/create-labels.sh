#!/usr/bin/env bash
# scripts/release/create-labels.sh -- the issue labels the issue forms and
# beta triage use (6.8). Run once (it updates labels that already exist):
#
#   bash scripts/release/create-labels.sh [owner/repo]
#
# Needs the GitHub CLI, logged in (gh auth login).
set -euo pipefail
repo="${1:-Blue-Kachina/MilkDAWp}"

label() { gh label create "$1" --repo "$repo" --color "$2" --description "$3" --force; }

label triage         "fbca04" "New: not looked at yet"
label bug            "d73a4a" "Something doesn't work"
label enhancement    "a2eeef" "An idea or request"
label preset-removal "5319e7" "A preset author asks for removal (honour it: THIRD_PARTY_NOTICES.md)"
label beta           "0e8a16" "Found in a -beta build"
label blocker        "b60205" "Must be fixed before 1.0"
label host-specific  "c5def5" "Only in one DAW (name it in the issue)"
label platform:windows "1d76db" "Windows only"
label platform:macos   "1d76db" "macOS only"
label platform:linux   "1d76db" "Linux only"
label needs-info     "d4c5f9" "Waiting for the reporter"
label wontfix        "ffffff" "Not planned"
echo "Labels ready in $repo."

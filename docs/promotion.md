# 1.0 release and repository promotion (6.9)

The order matters: each step assumes the ones before it. Steps marked
**(GitHub)** change public state and can't easily be undone.

## 1. Back up v1

```sh
bash scripts/release/backup-v1.sh            # Blue-Kachina/MilkDAWp -> ./milkdawp-v1-backup-<date>/
```

It saves a mirror clone and a `git bundle`, the wiki, issues and pull requests
with comments, labels, repo metadata, and every release with its assets (the
built v1 plugins, so old sessions can still be opened in v1). Copy the folder
off this machine too.

## 2. Check that v1 sessions open

v1 migration was dropped (§4.8, 2026-09-26): a v1 session opens with v2's
defaults. `PluginProcessorTests` checks this with a blob built exactly as v1
0.7.x saved its state ("opens a v1 session with v2 defaults"). By hand, on
**copies** of real v1 projects in your DAWs: each opens, the plugin loads with
defaults (preset folder empty until chosen, or the bundled pack on first
play), nothing crashes, and saving and reopening keeps v2's settings.

## 3. Release 1.0

When the beta is done ([beta.md](beta.md)): tag `v1.0.0` (no suffix) and let
the release workflow publish it ([releasing.md](releasing.md)). The version is
1.0.0 (decided 2026-10-05): the earlier MilkDAWp never reached 1.0, so 1.0.0
is still an upgrade for hosts that compare plugin versions.

(v1 has no issues to carry over (checked 2026-10-05), so there is no issue
transfer step. If any appear before step 4, transfer them first:
`gh issue transfer <n> Blue-Kachina/MilkDAWp2`. An archived repository is
read-only.)

## 4. Rename and archive v1 **(GitHub)**

```sh
gh repo rename MilkDAWp-v1 --repo Blue-Kachina/MilkDAWp
```

Then, in a clone of v1, replace the top of its README with a pointer and push:

> **MilkDAWp has moved.** This repository holds the earlier MilkDAWp (versions
> 0.x), kept for reference. MilkDAWp 1.0, its replacement, lives at
> https://github.com/Blue-Kachina/MilkDAWp (installers on its releases page).
> The 0.x releases stay here for opening old projects.

```sh
gh repo archive Blue-Kachina/MilkDAWp-v1 --yes
```

## 5. Rename this repository **(GitHub)**

```sh
gh repo rename MilkDAWp --repo Blue-Kachina/MilkDAWp2
git remote set-url origin https://github.com/Blue-Kachina/MilkDAWp.git   # in every clone
```

GitHub redirects `Blue-Kachina/MilkDAWp2` URLs (including the API the update
check uses) to the new name. Old links to `Blue-Kachina/MilkDAWp` now land
here; links to v1's release downloads break (they're in `MilkDAWp-v1` and in
the backup).

## 6. Sweep the old name

```sh
bash scripts/release/rename-sweep.sh          # what would change
bash scripts/release/rename-sweep.sh --apply  # then review git diff, commit, push
```

It points URLs, the update check and the docs at `Blue-Kachina/MilkDAWp`.
It deliberately keeps the dev identity
(`MilkDAWp2 Dev`, ADR-0007) and the GHCR image name
`milkdawp2-devcontainer` (a package doesn't follow a repo rename; renaming it
means rebuilding the image and re-pointing CI for nothing users see).

## 7. Announce

Announce 1.0 as MilkDAWp's first release: what it does (the
[user guide](user-guide/README.md)), the download link, and the issue forms for
feedback. User-facing text never mentions the earlier, unpromoted 0.x builds
(decided 2026-10-05: they'd only confuse).

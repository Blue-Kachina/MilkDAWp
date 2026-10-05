# 1.0 release and repository promotion (6.9)

Steps marked **(GitHub)** change public state and can't easily be undone.

## Done (2026-10-05)

- **Old version kept:** a `.rar` of the old VST3 plugin folder. Old sessions
  don't need to keep working in it.
- **Old sessions in 1.0:** they open with defaults. `PluginProcessorTests`
  checks this with a state blob built exactly as v0.7.x saved it.
- **Released:** `v1.0.0` tagged directly (no beta programme).
- **Old repository:** renamed `Blue-Kachina/MilkDAWp` to
  `Blue-Kachina/MilkDAWp_v0_7_5` and archived. (It had no issues to carry over.)

## 1. Rename this repository **(GitHub)**: done 2026-10-05

After the `v1.0.0` release run has finished. The `MilkDAWp` name is free now
that the old repository has moved.

```sh
gh repo rename MilkDAWp --repo Blue-Kachina/MilkDAWp2
git remote set-url origin https://github.com/Blue-Kachina/MilkDAWp.git   # in every clone
```

GitHub redirects `Blue-Kachina/MilkDAWp2` URLs, including the releases API the
update check uses and the links inside the installed 1.0, to the new name.

## 2. Sweep the old name: done 2026-10-05

```sh
bash scripts/release/rename-sweep.sh          # what would change
bash scripts/release/rename-sweep.sh --apply  # then review git diff, commit, push
```

It points URLs, the update check and the docs at `Blue-Kachina/MilkDAWp`.
It deliberately keeps the dev identity (`MilkDAWp2 Dev`, ADR-0007) and the
GHCR image name `milkdawp2-devcontainer` (a package doesn't follow a repo
rename; renaming it means rebuilding the image and re-pointing CI for nothing
users see).

## 3. Announce (optional)

Present 1.0 as MilkDAWp's first release: what it does (the
[user guide](user-guide/README.md)), the download link, and the issue forms for
feedback. User-facing text never mentions the earlier, unpromoted 0.x builds
(decided 2026-10-05: they'd only confuse).

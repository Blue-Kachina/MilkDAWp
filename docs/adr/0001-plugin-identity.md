# ADR-0001: Plugin identity and versioning

Status: Accepted

## Context

MilkDAWp v1 shipped with manufacturer code `OMda`, plugin code `Mlkw`, bundle
ID `com.otitismedia.MilkDAWp`, and product name `MilkDAWp`. v2 is a ground-up
rebuild (development_roadmap.md §2), but hosts identify a VST3/AU by these
codes, and existing users have sessions referencing them (D1).

## Decision

v2 **is** the next MilkDAWp, not a new product:

- Keep v1's manufacturer code (`OMda`), plugin code (`Mlkw`), bundle ID
  (`com.otitismedia.MilkDAWp`), and product name (`MilkDAWp`).
- The first release ships as MilkDAWp 1.0. Existing v1 sessions keep loading
  the plugin, but with v2 default settings: v1 state is not migrated
  (dropped 2026-09-26, roadmap Phase 1.14).
- The state schema is versioned from the first commit (`StateSchema v2`) so
  beta/dev builds also migrate forward cleanly.
- Dev and beta builds carry a visible pre-release version string.
- The v1 repository is archived with a pointer to this one once 1.0 ships
  (Phase 6.9).

## Consequences

- Never install a v1 and v2 build into the same plugin folder at once: same
  identity codes, so a host will pick one arbitrarily.
- Changing plugin identity codes, the bundle ID, or the state schema version
  requires a new ADR (see the working agreements in §11).

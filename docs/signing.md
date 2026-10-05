# Windows code signing through SignPath (6.10, D11)

The [SignPath Foundation](https://signpath.org/) signs open-source releases for
free, with SignPath Foundation named as the publisher on the certificate.
Signed installers stop Windows SmartScreen's "Windows protected your PC"
warning once the certificate has built up reputation.

The release workflow is ready: its three signing steps run only once the
repository variable `SIGNPATH_ORGANIZATION_ID` exists. Until then releases ship
unsigned, with SHA-256 checksums and build attestations.

## 1. Before applying

SignPath's terms ask for:

- an OSI-approved licence, with no proprietary components: MilkDAWp is
  AGPL-3.0-or-later; projectM LGPL-2.1; Lucide ISC; the presets are freely
  released data (THIRD_PARTY_NOTICES.md);
- a public repository whose releases are built automatically from it: GitHub
  Actions, `.github/workflows/release.yml`, with build provenance attestations;
- an existing release: apply after the first `-beta` is published.

**Ask first** (D11): JUCE is dual-licensed (AGPL-3.0 or commercial), and
SignPath's terms exclude projects that sell commercial licences of
themselves. MilkDAWp uses JUCE only under the AGPL and sells nothing, but
confirm that JUCE's own dual licence isn't a problem before applying.

## 2. The application

Apply at <https://signpath.org/apply>. Points worth making:

- Project: MilkDAWp, a music visualizer (VST3/AU plugin and standalone app),
  <https://github.com/Blue-Kachina/MilkDAWp2> (to become `Blue-Kachina/MilkDAWp`).
- Licence: AGPL-3.0-or-later; dependencies: projectM (LGPL-2.1, dynamically
  linked), JUCE (used under the AGPL-3.0), Lucide icons (ISC).
- Build: GitHub Actions on GitHub-hosted runners, from tags, with
  `actions/attest-build-provenance`; the Windows artifact is one Inno Setup
  installer per release.
- What to sign: `MilkDAWp-<version>-windows-x64-setup.exe`.

## 3. After acceptance

In SignPath (they set up the organization and project with you):

1. Artifact configuration: paste
   `packaging/windows/signpath-artifact-configuration.xml`.
2. Signing policy: release signing, with GitHub as the trusted build system
   (SignPath's "GitHub" connector checks that the artifact came from this
   repository's workflow).
3. Create an API token for a CI user that may submit to the policy.

In GitHub (Settings > Secrets and variables > Actions):

| Kind | Name | Value |
|---|---|---|
| Secret | `SIGNPATH_API_TOKEN` | the API token |
| Variable | `SIGNPATH_ORGANIZATION_ID` | SignPath organization id |
| Variable | `SIGNPATH_PROJECT_SLUG` | e.g. `MilkDAWp` |
| Variable | `SIGNPATH_SIGNING_POLICY_SLUG` | e.g. `release-signing` |
| Variable | `SIGNPATH_ARTIFACT_CONFIGURATION_SLUG` | e.g. `initial` |

The next release run uploads the installer, waits for SignPath to sign it,
checks the signature is valid, and ships the signed file (the smoke test then
installs the signed one).

Then remove "not code-signed yet" from `scripts/release/package.sh`'s README
text, the release notes in `release.yml`, `docs/user-guide/installing.md` and
`docs/user-guide/faq.md`, and record the outcome in D11.

## If SignPath says no

Record it in D11 and keep shipping unsigned with checksums and attestations
(the workflow needs no change: without the variable it never tries).

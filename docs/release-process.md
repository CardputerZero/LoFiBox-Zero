<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# LoFiBox Release Process

This document tracks the intended release discipline for official archive readiness.

## Release Gates

A release candidate should satisfy:

- clean CMake configure/build/test
- no build-time network
- valid desktop file
- valid AppStream metadata
- lintian review
- autopkgtest smoke
- dependency policy review
- copyright/resource review
- packaging install/uninstall sanity

## Versioning

Release tags should be traceable by `debian/watch`.
Source releases are created by the `Source Release` GitHub Actions workflow.
Run it from `main` with an upstream version such as `0.2.1`; it validates
release metadata, creates the annotated `vX.Y.Z` tag, creates the GitHub
Release, and can trigger the `lofibox-apt` publishing workflow.

The tag pattern is `vX.Y.Z`. This matches `debian/watch` and gives the APT
publisher an immutable source ref instead of a moving branch name.

When `publish_apt` is enabled, the source repository must define the
`LOFIBOX_APT_WORKFLOW_TOKEN` secret. The token needs permission to run Actions
workflows in `vicliu624/lofibox-apt`. The source workflow passes
`source_ref=vX.Y.Z` and `expected_upstream_version=X.Y.Z`; the APT repository is
responsible only for adding its preview suffix and publishing the signed
repository.

## Archive Review Preparation

Before sponsor review, prepare:

- source package
- copyright audit
- dependency audit
- build log from clean chroot
- autopkgtest result
- lintian result
- current engineering check report

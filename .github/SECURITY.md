# Security Policy

## Supported versions

The library is released from `main` and published to the Arduino and PlatformIO
registries. Security fixes target the latest released version. Please reproduce
on the latest release before reporting.

Only the latest released version is supported; earlier versions receive no fixes. This table is kept
current automatically by the release bump (`.bumpversion.cfg`).

| Version | Supported          |
| ------- | ------------------ |
| 0.0.1   | :white_check_mark: |
| < 0.0.1 | :x:                |

## Reporting a vulnerability

**Do not open a public issue for a security vulnerability.**

Report privately through GitHub's
[Security Advisories](https://github.com/dstroy0/ProtoCore/security/advisories/new),
or by email to **dquigg123@gmail.com**.

Please include:

- The affected version and build flags (`PC_ENABLE_*`) in use.
- A description of the issue and its impact (for example: memory disclosure,
  authentication bypass, protocol-parsing flaw, remote crash).
- A minimal reproduction: request bytes, a script, or a test case.
- Any suggested fix, if you have one.

You can expect an acknowledgment within a few days. Once the issue is confirmed
and a fix is prepared, we will coordinate disclosure and credit you in the
release notes if you wish.

## Scope and posture

This library is built for deterministic, zero-heap operation, and a number of
classes of vulnerability are designed out (no runtime allocation, fixed and
bounds-checked buffers, strict RFC parsing). For the full security model, the
specific files that enforce each property, and the known limitations, see
[docs/SECURITY.md](../docs/SECURITY.md) and
[docs/KNOWN_LIMITATIONS.md](../docs/KNOWN_LIMITATIONS.md).

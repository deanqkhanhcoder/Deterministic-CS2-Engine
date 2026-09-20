# Security Policy

## Supported Versions

Currently, only the V27 branch (`main`) is supported with security updates.

| Version | Supported          |
| ------- | ------------------ |
| >= 27.0 | :white_check_mark: |
| < 27.0  | :x:                |

## Reporting a Vulnerability

If you discover a security vulnerability (such as a ring-0 escape, OS-level arbitrary execution through SendInput batching, or memory corruption in the hook chain), please do **NOT** open a public issue.

Instead, please email the project maintainer securely. You should receive a response within 48 hours.

## Scope
Please only report vulnerabilities that affect the Windows OS integrity or cause unintended external side effects. Bugs regarding mathematical divergence or UI glitches should be filed as standard public issues.

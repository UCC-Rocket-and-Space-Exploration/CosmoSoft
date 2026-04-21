# Security Policy

## Supported Versions

| Version | Supported |
|---------|-----------|
| `main` branch | Yes |
| Tagged releases | Latest only |

## Reporting a Vulnerability

**Do not open a public GitHub Issue for security vulnerabilities.**

If you discover a security issue in CosmoSoft — for example, a vulnerability in the serial communication layer, the binary telemetry parser, or any other component — please report it privately:

1. Open a [GitHub Security Advisory](https://github.com/UCC-Rocket-and-Space-Exploration/CosmoSoft/security/advisories) in this repository, **or**
2. Email the maintainers directly (add contact here before making the repository public)

### What to include

- Description of the vulnerability and its potential impact
- Steps to reproduce or a minimal proof-of-concept
- Affected versions or commits
- Any suggested fix if you have one

### What to expect

- Acknowledgement within 5 business days
- A fix or mitigation plan within 30 days for confirmed issues
- Credit in the release notes (unless you prefer to remain anonymous)

## Scope

The following are in scope:

- `Framer` / `Parser` — malformed or malicious telemetry bytes causing crashes or undefined behaviour
- `SerialCommsPosix` — improper handling of serial device input leading to privilege escalation
- Any component that processes untrusted input (file import, serial stream)

The following are **out of scope** for this policy:

- Vulnerabilities in Qt, third-party libraries, or the operating system
- Issues that require physical access to the hardware running the ground station

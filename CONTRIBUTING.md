# Contributing to CosmoSoft

Thank you for your interest in contributing to CosmoSoft — an open-source desktop ground station for rocket telemetry.

---

## Getting Started

### Requirements

- CMake 3.21 or newer
- A C++20 compiler (GCC 11+, Clang 13+, MSVC 2022+)
- Qt 6.2 or newer (`Core`, `Gui`, `Widgets`, `Charts`, `Concurrent`,
  `WebEngineWidgets`, `WebChannel`)
- Zlib development files

### Build

```bash
cmake -S . -B build
cmake --build build
```

See [`README.md`](README.md) for full build instructions and platform notes.

---

## Development Workflow

### 1. Find or open an Issue

Before writing code, check [GitHub Issues](../../issues) to see if someone is already working on the same thing.

- If an Issue exists, comment to say you are working on it
- If no Issue exists for a non-trivial change, open one and wait for acknowledgement before starting

### 2. Branch

Fork the repository and create a branch from `main`:

```bash
git checkout -b feat/telemetry-framer-sync
```

Branch prefixes:

| Prefix | When to use |
|--------|-------------|
| `feat/` | New feature |
| `fix/` | Bug fix |
| `chore/` | Tooling, CI, docs, refactor |
| `test/` | Adding or updating tests |

### 3. Make your changes

Follow the coding standards in [`AGENTS.md`](AGENTS.md) and the rules in [`.cursor/rules/`](.cursor/rules/).

Key rules:
- C++20; no raw `new`/`delete`; use `std::unique_ptr`
- All public functions need a Doxygen `/** @brief */` comment
- No `TODO`/`FIXME` in committed code — open an Issue instead

### 4. Test

Run the test suite before opening a PR:

```bash
ctest --test-dir build --output-on-failure
```

Every new service function should have a corresponding Catch2 unit test in `tests/`.

### 5. Check formatting and static analysis

```bash
# Format check
clang-format --dry-run --Werror $(find src include -name "*.cpp" -o -name "*.h")

# Static analysis
cppcheck --enable=all --suppress=missingInclude src/
```

### 6. Open a Pull Request

PR titles must follow [Conventional Commits](https://www.conventionalcommits.org/en/v1.0.0/):

```
feat(telemetry): implement Framer sync-byte detection
fix(serial): apply termios baud rate in SerialCommsPosix::open
chore(ci): add GitHub Actions build workflow
```

PR description must include:

- **What** the change does
- **Why** it is needed (link to the Issue)
- **How** to test it
- Test commands run and their results

Keep PRs small and focused — one logical change per PR.

---

## AI-Assisted Contributions

AI coding tools are welcome. CosmoSoft's open-source approach is consistent with [Red Hat's guidance on AI-assisted development](https://www.redhat.com/en/blog/ai-assisted-development-supercharging-open-source-way).

### Rules

1. **You are responsible for every line you submit.** AI-generated code must be understood, reviewed, and testable by you before opening a PR. Pure agent PRs without human validation will be closed.

2. **Disclose AI assistance** with a git commit trailer:
   ```
   feat(telemetry): implement Framer sync-byte detection

   Assisted-by: Cursor
   ```
   Trivial AI use (autocomplete, docstring suggestions) does not require disclosure.

3. **Same review bar.** AI-generated code is reviewed to exactly the same standard as hand-written code — no exceptions.

4. **License compliance.** Do not submit AI output that replicates copyrighted or licence-restricted code. If you cannot trace a snippet to an Apache 2.0 / MIT / GPL-compatible source, rewrite it.

5. **Coordination first.** Check for duplicate Issues and open PRs before submitting AI-generated work (see the [Development Workflow](#development-workflow) above).

---

## Significant Changes — Enhancement Proposals

For changes that affect public APIs (`IComms`, `FlightSample`, `FlightDataModel`) or the binary telemetry protocol, open a GitHub Issue first and reach consensus with maintainers before writing code.

A clear Issue description covering the problem, proposed solution, and alternatives considered is sufficient — no formal document required for v1.

This process is inspired by the [Kubeflow Enhancement Proposal (KEP) process](https://github.com/kubeflow/community/tree/master/proposals).

---

## Issue Triage

Helping to label and triage open Issues is a great first contribution and a good way to learn the codebase. Look for Issues labelled `needs-triage` or `good first issue`.

---

## Security Vulnerabilities

Do **not** report security vulnerabilities as public GitHub Issues. See [`SECURITY.md`](SECURITY.md) for the responsible disclosure process.

---

## Code of Conduct

This project follows the [Contributor Covenant Code of Conduct](CODE_OF_CONDUCT.md). By participating you agree to uphold its standards.

---

## License

CosmoSoft is licensed under the [MIT License](LICENSE). By submitting a contribution you agree that your work will be made available under the same licence.

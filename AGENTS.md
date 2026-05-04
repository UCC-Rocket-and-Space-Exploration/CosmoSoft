# AGENTS.md — CosmoSoft

## Who This Is For

- **AI agents** — automate repository tasks with minimal context
- **Contributors** — humans using AI assistants or working directly
- **Maintainers** — ensure assistants follow project conventions and CI rules

---

## Agent Behaviour Policy

AI agents should:

- Make atomic, minimal, and reversible changes
- Verify the build produces zero warnings before proposing commits
- Prefer local analysis (`cmake --build build`, `clang-format`, `cppcheck`) before proposing changes
- Use this file and the `.cursor/rules/` directory as the source of truth for conventions

Agents must **NOT**:

- Modify `CMakeLists.txt`, CI configuration, or `assets/resources.qrc` unless explicitly requested
- Bypass `clang-format` or `cppcheck`
- Introduce new third-party libraries without updating CMake and documenting the reason in a comment
- Generate or commit files from the `build/` directory
- Add platform-specific code (`#ifdef _WIN32`, `#ifdef __APPLE__`) without a matching guard on the other platform and a note in `README.md`
- Use non-deterministic code (unseeded random, `time()`-based logic) in tests

---

## Repository Map

```
include/               # Public headers (domain models, GUI, services)
├── domain/            # FlightSample, FlightSession
├── gateway/comms/     # IComms, ISerialPortScanner, platform headers
├── gui/               # MainWindow, pages, widgets, FlightDataModel
└── services/          # BlockingQueue, RingBuffer, telemetry pipeline, import, persistence

src/                   # Implementations
├── gateway/comms/     # CommsFactory, SerialWorker, Posix/, Windows/
├── gui/               # MainWindow, pages/, widgets/
└── services/
    ├── flight/        # Placeholder (FlightModuleStub)
    ├── import/        # SampleFileLoader (CSV + .telem)
    ├── persistence/   # FlightLogManager, RingBuffer
    └── telemetry/     # Framer, Parser, ParserWorker

assets/                # Qt resources (.qrc, fonts, icons, images)
sample_data/           # Example CSV and .telem files for replay
scripts/               # Build helpers (fresh-cmake.sh)
docs/                  # Architecture docs, protocol spec (docs/protocol.md once agreed)
tests/                 # Catch2 unit tests (to be created)
```

---

## Build & Verify Commands

```bash
# Configure and build
cmake -S . -B build
cmake --build build

# Clean rebuild (uses helper script)
bash scripts/fresh-cmake.sh

# Format check (requires clang-format)
clang-format --dry-run --Werror $(find src include -name "*.cpp" -o -name "*.h")

# Static analysis (requires cppcheck)
cppcheck --enable=all --suppress=missingInclude src/

# Run tests (once Catch2 suite exists)
ctest --test-dir build --output-on-failure
```

---

## Core Development Principles

### 1. Stable Public Interfaces

Do **not** change the following public APIs without opening a GitHub Issue and getting maintainer consensus:

- `IComms` — serial communication interface
- `FlightSample` — core domain type passed through the entire pipeline
- `FlightDataModel` — Qt model consumed by all UI pages

Before changing any exported class:

- Check all usages in `src/` and `include/`
- Use new parameters only as additions; never reorder or remove existing parameters

### 2. Code Quality

- C++20; no raw `new`/`delete`; use `std::unique_ptr` / `std::shared_ptr`
- No bare `catch (...)` — catch specific exception types or `std::exception`
- Proper resource cleanup: serial file descriptors, Qt objects, thread handles
- No `system()`, `popen()`, or shell invocations from application code

```cpp
// ❌ BAD
IComms* c = new SerialCommsPosix(dev, baud);

// ✅ GOOD
auto c = std::make_unique<SerialCommsPosix>(dev, baud);
```

### 3. Testing Requirements

Every new service function must be covered by a Catch2 unit test in `tests/`.

Test structure:

```cpp
TEST_CASE("SampleFileLoader: valid Theseus CSV", "[import]") {
    FlightSession session;
    auto err = SampleFileLoader::loadTheseusCsv("sample_data/theseus_flight_data.csv", session);
    REQUIRE_FALSE(err.has_value());
    REQUIRE(session.samples.size() > 0);
}
```

No network calls or hardware access in unit tests. Hardware-in-the-loop tests go in a separate `tests/box/` directory.

### 4. Security Checklist

Before committing, verify:

- No `system()`, `exec()`, or shell-constructed commands from user input
- No hard-coded secrets, credentials, or absolute device paths
- Proper exception handling — no bare `catch (...)`
- All file handles and socket descriptors are closed in destructors or RAII wrappers
- No deprecated crypto (MD5, SHA-1) in any new serialisation or protocol code
- Serial input is length-checked before processing — never trust `size` fields from untrusted bytes

### 5. Documentation Standards

All public functions and classes must have a Doxygen comment:

```cpp
// ❌ Insufficient
bool open();

// ✅ Complete
/**
 * @brief Open the serial port with the configured device path and baud rate.
 * @return true if the port was opened successfully, false otherwise.
 */
bool open();
```

---

## AI-Generated Code Policy

### Disclosure

Disclose AI assistance via a git commit trailer:

```
feat(telemetry): implement Framer sync-byte detection

Assisted-by: Cursor
```

Trivial AI use (variable-name autocomplete, docstring generation) does not require disclosure.

### Accountability

- Pure agent PRs are not permitted — a human contributor must understand and be able to defend every changed line
- AI-generated code is held to the same review standard as hand-written code
- The submitting human is responsible for running the relevant tests and verifying correctness

### License Compliance

AI tools may inadvertently reproduce copyrighted or licence-restricted code.

- Do not submit AI output that you cannot trace back to an Apache 2.0 / MIT / GPL-compatible source
- If in doubt, rewrite the generated snippet rather than submitting it verbatim
- Red Hat's guidance: marking AI assistance with `Assisted-by:` preserves legal clarity for downstream consumers

---

## Coordination Before Coding

Before opening any PR:

1. Check whether a GitHub Issue already covers the work; coordinate on it first
2. Check for overlapping open PRs:
  ```bash
   gh pr list --repo <org>/cosmo-soft --state open --search "<keywords>"
  ```
3. Do not work on another contributor's issue without explicit approval in the issue thread
4. Do not open one-off PRs for trivial edits (single typo, isolated lint cleanup)

**Fail-closed rule:** if coordination evidence cannot be found, do not open a PR — explain what is missing instead.

---

## Component Ownership


| Component          | Path                        | Notes                                              |
| ------------------ | --------------------------- | -------------------------------------------------- |
| Telemetry pipeline | `src/services/telemetry/`   | Protocol-sensitive; changes need Issue discussion  |
| Serial gateway     | `src/gateway/comms/`        | Platform-specific; test on macOS + Linux           |
| Qt GUI             | `src/gui/`                  | No business logic; delegate to services            |
| Domain types       | `include/domain/`           | Stable API; changes propagate everywhere           |
| Import / replay    | `src/services/import/`      | Only CSV is fully implemented for v1               |
| Persistence        | `src/services/persistence/` | `FlightLogManager` not yet wired into `MainWindow` |



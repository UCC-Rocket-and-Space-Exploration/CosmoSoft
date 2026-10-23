# Telemetry performance and correctness fixes

The chart overview now has a strict point budget even when measurements alternate
between valid and missing values. It keeps per-bucket extrema and representative
gap boundaries. Rendering checks the original intervals for missing values before
connecting selected points. Each metric uses at most 32 connected line segments;
additional fragments and isolated measurements share a point-only series. Raw
samples and original-row hover inspection remain available. Comparison and theme
changes preserve the fragments; resetting the trail removes them.

Replay loading builds immutable range summaries in 64-sample blocks on the loading
worker. Replay selection and gap queries use those summaries instead of repeatedly
scanning the complete visible history on the GUI thread. Live charts use the same
bounded selection policy over their existing limited history.

XLSX loading converts worksheet rows directly to samples and retains only the
recognized data columns. It no longer constructs and copies a complete row table.
It rejects more than 16,384 columns or 1,048,576 worksheet rows while preserving the
active session on failure. ZIP/XML bytes and shared strings still reside in memory;
this change streams worksheet rows, not the entire ZIP archive.

Blank scalar acceleration cells remain missing rather than becoming zero. POSIX
serial reads drain available data and then report a peer hangup as a disconnect,
including when readability and hangup arrive together.

## Measurements

A local standalone C++20 probe compiled with `-O2` compared direct selection with
summary-based selection over one million synthetic samples, three enabled metrics,
a 1,000-point budget, and 20 changing end ranges:

| Operation | Mean duration |
| --- | ---: |
| Selection scanning raw samples | 20.70 ms |
| Selection using block summaries | 0.090 ms |
| Initial summary construction | 72 ms total |

Both selection paths averaged 399 selected points. These timings cover selection
only, excluding Qt series construction, painting, QtWebEngine and GPU work. They
do not establish an application frame rate or an XLSX peak-memory reduction.

## Verification

Regression tests cover missing acceleration versus measured zero, sparse XLSX
columns and worksheet limits, serial peer closure, strict chart budgets, extrema,
gap-query boundaries, cancellation, comparison, theme switching and fragment
cleanup. The GUI tests also check that decimated curves never connect across
omitted missing measurements.

The build completed without compiler warnings. All 96 core CTest entries passed;
the complete GUI suite passed separately outside the sandbox after QtWebEngine
startup was blocked inside it. Dark and light chart captures were inspected.
Cppcheck reported no errors, warnings, performance or portability findings, with
five existing style suggestions. Changed C++ ranges were checked with clang-format;
unrelated legacy formatting was preserved. Independent review found no blockers.

Linux and Windows execution remain unverified in this macOS session.

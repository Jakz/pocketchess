# ADR 0007 — An in-repo YAML subset reader instead of yaml-cpp

- Status: Accepted (2026-09-29). Replaces P8 (yaml-cpp) in `docs/ai/01-decisions.md`.

## Decision

Descriptors (engines now; themes, piece sets and content packs later) are read by
`src/core/util/Yaml.{h,cpp}`, a small reader for the YAML subset the descriptors use:

- block mappings and sequences by indentation (spaces only), `- key: value` items, nested sequences;
- flow collections (`{a: 1, b: [x, y]}`), also spanning several lines;
- plain, `'single'` and `"double"` quoted scalars (escapes, `\uXXXX`), comments, `~`/`null`,
  a leading `---`;
- ordered mappings, line numbers on every node, typed accessors that never throw
  (`doc["a"]["b"].intOr(0)`; missing keys give a shared null node).

Anything outside the subset is an error with a line number, never a silent misread: tabs in
indentation, anchors/aliases, tags, block scalars (`|`, `>`), duplicate keys, multiple documents,
bad indentation, unbalanced flow collections.

## Why

- The same reasons as ADR 0005 (own TrueType rasterizer, PNG writer, test framework): no
  exception-based third-party library in core, identical behaviour on MSVC and the Zig/ARM build,
  nothing to vendor or keep in sync with Enigmistica's copy.
- The files are ours and small; the subset covers the design notes' descriptor examples (§19, §35)
  and architecture doc §8.2. About 650 lines plus tests, versus yaml-cpp's ~40 files and a
  `YamlCpp.vcxproj` + CMake target.
- Errors carry `file: line N: ...`, which is what the "malformed descriptor → disable with a
  message" rule (design notes §32) needs.

## Consequences

- No `YamlCpp` project (build doc §3.1) and no `libs/yaml/`.
- If a later format needs block scalars or anchors (lesson texts, M7), extend the reader or revisit
  yaml-cpp in a new ADR; the `YamlNode` API is small enough to put an adapter behind.
- `data/simulator/kaleido3.yaml` keeps its line-based reader (desktop only); it can move to
  `util::Yaml` when it is next touched.

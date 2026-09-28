# 06 — Risks and open questions

## 1. Risks and mitigations

| # | Risk | Likelihood / impact | Mitigation | Resolved in |
| --- | --- | --- | --- | --- |
| R1 | Stockfish 19 NNUE is too slow to load, too large for RAM, or too energy-hungry on the Cortex-A7 (1 GB RAM device) | Medium / Medium | M0.6 checkpoint. Engines are plug-ins with power profiles (D8), so an Eco engine (Stockfish 11 first) can take play and hints while Stockfish 19 stays for Deep analysis. The M2 benchmark decides with data. | M0/M2 |
| R2 | The NNUE embedding (INCBIN) or SF's build flags do not work with Zig for 32-bit ARM | Medium / Medium | `NNUE_EMBEDDING_OFF` + `EvalFile` beside the binary. Compile flags are copied from upstream `ARCH=armv7-neon`. | M0 |
| R3 | InkView color-canvas format or update calls differ from the assumptions (depth, color partial updates, fast modes) | Medium / Medium | Diagnostics spike M0.5. The Presenter abstraction confines changes to one file. `Stretch(IMAGE_RGB)` fallback as in Enigmistica. | M0 |
| R4 | No thread-safe way to wake the InkView loop from the engine reader thread | Low–Medium / Medium | Test candidates in M0.5. Worst case: a 100 ms `SetHardTimer` poll **only while an engine job is active**. | M0 |
| R5 | C++20 runtime issues on firmware 6.10 (Zig links libc++ statically; glibc symbol versions) | Low / High | `check-elf.ps1` checks NEEDED libraries and max GLIBC version. The self-test app runs on the device early (M0/M1). | M0–M1 |
| R6 | Exec not permitted in `applications/pocketchess/engines/`, or WAL misbehaving, on the user partition (`/mnt/ext1`) | Low / Medium | Checked in the M0.5 diagnostics. Apps already execute from `applications/`. Fall back to the `DELETE` journal. | M0/M4 |
| R7 | Home/Back keys not delivered to apps | Medium / Low | Menu long-press = Back. On-screen back button on every screen. | M0 |
| R8 | Source-list drift between the vcxproj files and `sources.cmake` | High / Low | `check-sources.ps1` as a pre-build step plus `verify-all.ps1`. | M0 |
| R9 | Piece-set licensing | Medium / Medium | Only sets with explicit GPL-compatible licences, with `LICENSE` per set. Plus an own-made high-contrast set. | M6 |
| R10 | Kaleido 3 colors differ from the simulator | High / Low | Tune `kaleido3.yaml` from device photos. Colour is never the sole carrier of information. | M1/M6 |
| R11 | Scope creep (study features) | Medium / Medium | M7 stays backlog until the MVP (M6) passes on the device. | — |
| R12 | A lighter engine does not actually save energy (the same time budget uses the same CPU-seconds) | Medium / Low | Measure CPU-seconds per job and strength (architecture doc §8.3). Profiles save energy mainly through search budgets and idle timeouts. The Eco engine is bundled only if the data shows a gain. | M2/M6 |
| R13 | Storage/RAM creep as packs and engines grow | Low / Medium | Per-component budgets (architecture doc §13). `deploy.ps1` and Diagnostics report sizes against them. | M6 |
| R14 | Engine sources/networks too large for the GitHub repo, or a fresh clone missing them | Low / Low | Engine sources are local-only, restored by `fetch-engines.ps1` from pinned tags and SHA-256 (build doc §2). `build-engine.ps1` fails with a clear message when they are missing. Never commit generated DBs. | M0 |

## 2. Answered (2026-09-28)

- Own chess core (D10).
- Device: Verse Pro Color PB634K3, firmware 6.10, the same unit as Enigmistica (D11).
- Data folder `applications/pocketchess/` (D11).
- Flexible, pluggable engines and content, with an energy-saver option and power profiles (D8, D9).
- Name PocketChess (D12).
- Debug/Release build both platforms, with `Desktop` as the fast configuration (D13).
- Default puzzle pack Large 50k (D14).
- GPL-3.0-or-later in a private GitHub repository (D15).

## 3. Open questions

None at the moment. New questions found during implementation go here, with a proposed default,
and are raised with Jack before the milestone that depends on them.

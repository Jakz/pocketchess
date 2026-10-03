# ADR 0019 — pocketchess-setup: fetch what cannot be shipped, install on the reader

- Status: Accepted (2026-10-03). Follows docs/licensing.md ("Before shipping", items 1 and 6),
  ADR 0013 (user content under `user/`), ADR 0017.

## Context

Until now the reader got PocketChess through `projects/pocketbook/deploy.ps1`, which needs this
repository and Windows. The historical games pack (PGN Mentor) may not be redistributed, and the
large puzzle packs are too big to ship. The user asked for one cross-platform tool that fetches the
content that needs a licence decision, installs the app, and sets a home-screen icon, plus
instructions to undo the icon.

## Decision

1. **One command-line tool**, `pocketchess-setup`, in C++ with the standard library only
   (`src/tools/setup`). It has three commands:
   - `fetch --historical --puzzles 500k` downloads into `downloads/` and builds the packs in
     `packs/` by running the bundled PgnImporter and PuzzleImporter.
   - `install` finds the mounted reader (a volume with `applications/` and `system/`: drive
     letters on Windows, `/Volumes` on macOS, `/media`, `/run/media` and `/mnt` on Linux) and
     copies the app. It mirrors `applications/pocketchess/` except `user/`, `logs/`, `imports/`
     and `exports/`. Packs the new bundle no longer has are moved to `user/content/`, never
     deleted. It then copies the packs and sets the icon.
   - `reset-launcher` undoes the icon change.
2. **Network and archives through the system's `curl` and `tar`** (Windows 10+, macOS and Linux
   ship them; `unzip` is the fallback for ZIP files on Linux). Nothing is vendored for HTTP or
   ZIP, and builds still never download (AGENTS.md): only the tool, run by the user, does.
3. **Launcher** (firmware 6.8+, `system/config/desktop/view.json`):
   - The icons go to `applications/icons/` (8-bit BMP, 106×128, `data/icons/`).
   - The first change saves `view.json.pocketchess-backup`.
   - The file is edited through a small ordered JSON reader/writer (`Json`): it adds or updates
     the `"U_pocketchess"` entry (path, title, icon, focused_icon) and, with `--group`, moves
     that key into the named group's list.
   - Undo: `reset-launcher` restores the backup (or removes only the PocketChess entry) and
     deletes the icons. Manual steps are in `docs/installer.md`.
4. **Builds**: `projects/tools` (CMake + Zig, `build-tools.ps1`) cross-compiles the setup tool
   and both importers for windows-x64, linux-x64 (static musl), macos-arm64 and macos-x64. With
   `-Package` it writes `out/installer/PocketChess-setup-<platform>.zip`, holding the tool,
   `tools/`, `reader/applications/` (the reader bundle), `reader/icons/` and `docs/installer.md`
   as `README.txt`. Packaging leaves out `historical.db`, which the tool fetches instead.
   `Setup.vcxproj` builds the tool in the MSVC solution for development.
5. **Tests**: `tests/tools/SetupTests.cpp` covers the JSON round trip and the launcher edit
   (entry, group move, idempotence, removal). The install was also tried end to end against a
   folder laid out like a reader.

## Consequences

- The installer zips can be shared: everything in them is redistributable (GPL code and
  engines, CC0 data, the Classics). The historical collection is fetched by each user for their
  own use.
- macOS binaries are unsigned (Gatekeeper: `xattr -dr com.apple.quarantine`), and zips made on
  Windows carry no Unix permissions. The README covers both, and the tool sets the execute bit
  on its helper tools itself.
- The launcher edit depends on the firmware's `view.json` format. The tool refuses to write
  when the file does not parse or the group is unknown, and lists the groups it found.
- Not verified yet on the reader's real `view.json` (firmware 6.10): to do on the next USB
  connection, before the first real install.

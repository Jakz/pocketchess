PocketChess setup
=================

This package installs PocketChess on a PocketBook reader (made for the Verse Pro Color, firmware
6.x) and fetches the content that cannot be shipped with it. It runs on Windows 10/11, macOS and
Linux, from a terminal.

What is in the package
----------------------

    pocketchess-setup(.exe)   the installer
    tools/                    PgnImporter and PuzzleImporter (they build the packs)
    reader/applications/      PocketChess for the reader: app, engines (Stockfish 19 and 11),
                              assets, 50k puzzles, the Classics games
    reader/icons/             the home-screen icons
    downloads/, packs/        created by `fetch`

The historical games pack is not included: its source (PGN Mentor) gives no licence to
redistribute the collection. `fetch --historical` downloads the files for your own use and builds
the pack on your computer.

Before you start
----------------

- macOS: the files are not signed. In Terminal, inside the unpacked folder:
      xattr -dr com.apple.quarantine .
      chmod +x pocketchess-setup
- Linux: `chmod +x pocketchess-setup`; ZIP files are unpacked with `unzip` (install it if
  `tar` cannot read ZIP files on your system).
- The tools use `curl` and `tar`, which come with Windows 10+, macOS and most Linux systems.

1. Fetch content (optional, needs internet)
-------------------------------------------

    pocketchess-setup fetch --historical --puzzles 500k

- `--historical`: about 300 player and event collections from PGN Mentor (a few hundred MB of
  downloads, a pack of about 80 MB). `--opening-files` adds PGN Mentor's opening collections.
- `--puzzles 50k,100k,500k,1m,all`: puzzle packs from the Lichess puzzle database (CC0;
  a 300 MB download once). Larger packs contain the smaller ones' best puzzles; sizes on the
  reader: 50k 4 MB, 100k 7 MB, 500k 35 MB, 1m 70 MB, all 430 MB.

Downloads are kept in `downloads/`: running `fetch` again only builds the packs again. Delete a
file there to download it anew.

2. Install on the reader
------------------------

Connect the reader over USB and choose "PC link" (or "Connect") on its screen. Then:

    pocketchess-setup install

- The app is copied to `applications/` on the reader. Your progress, settings and games
  (`applications/pocketchess/user/`), logs, imports and exports are never touched.
- Every pack in `packs/` is copied to `applications/pocketchess/user/content/`, so later
  installs keep it. `--packs none` or `--packs historical,puzzles-500k` choose.
- The home-screen icon is set (firmware 6.8+, see below). `--group NAME` also moves
  PocketChess to another home-screen group, for example `--group Games`. If the name is wrong,
  the tool lists the groups the reader has. `--no-icon` leaves the home screen alone.
- `--reader PATH` picks the reader when several are connected or it is not found
  (e.g. `--reader E:\` or `--reader /Volumes/PB634K3`).

Eject the reader before you unplug it. The new icon shows after the home screen reloads (restart
the reader if it does not).

`pocketchess-setup` without arguments shows the packs you have and the connected readers.

The home-screen icon, and how to undo it
----------------------------------------

On firmware 6.8 and later the home screen is described by `system/config/desktop/view.json` on
the reader. `install` does the following:

1. It copies `pocketchess.bmp` and `pocketchess_f.bmp` (8-bit BMP, 106x128) into
   `applications/icons/`.
2. On the first change it saves the original file as
   `system/config/desktop/view.json.pocketchess-backup`.
3. It adds an entry `"U_pocketchess"` with the app's path, title and both icons. With
   `--group NAME`, it also puts `"U_pocketchess"` into that group's list.

To undo it:

- With the tool: `pocketchess-setup reset-launcher`. This restores `view.json` from the backup
  (or, without a backup, removes only the PocketChess entry) and deletes the two icons.
- By hand: with the reader connected, copy `system/config/desktop/view.json.pocketchess-backup`
  over `system/config/desktop/view.json`. Then delete `applications/icons/pocketchess.bmp` and
  `applications/icons/pocketchess_f.bmp`, and restart the reader.
- If the home screen ever looks broken, deleting `system/config/desktop/view.json` makes the
  firmware build a default one at the next start. Your apps stay installed, but any group or
  icon changes you made yourself are lost.

A firmware update may replace `view.json`. If the icon is gone afterwards, run `install` again.

Uninstalling PocketChess
------------------------

Delete `applications/pocketchess.app`, `applications/pocketchess-selftest.app` and the folder
`applications/pocketchess/` (it holds your progress in `user/`: copy it first if you want to
keep it), then undo the icon as above.

Licences
--------

PocketChess is GPL-3.0-or-later; Stockfish is GPL-3.0; the Lichess puzzles and opening names are
CC0. The historical games you fetch are for your own use only. Details: docs/licensing.md in the
source repository.

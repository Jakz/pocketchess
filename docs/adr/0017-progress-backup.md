# ADR 0017 — Backup and restore of the user data

- Status: Accepted (2026-10-03). M7. Follows ADR 0013 (imports go under `user/`) and ADR 0015
  (every statistic lives in `user/chess.db` and `user/progress.db`).

## Context

Everything the user builds up is in two SQLite files on the reader: settings, played games, games
in progress, favorites, guess/drill/mistake records, and puzzle progress. Deploy and staging never
touch `user/`, but a reset, a new reader or a broken file would lose it all. Nothing should need
a computer-side tool: the reader's USB storage is the transport, as for imports.

## Decision

1. **Back up**: Stats → Menu → "Back up my progress" writes `exports/pocketchess-backup-<local
   time>/` with `chess.db`, `progress.db` and `backup.txt`. The copies come from SQLite's
   `VACUUM INTO`, which is consistent while the databases are open in WAL mode and compacts them.
   `backup.txt` marks the folder and says how to restore it.
2. **Restore**: the backup folder copied into `imports/` shows up in Import as "Progress backup"
   with its date. Restore… asks for confirmation, then:
   - checks both files: each must be an SQLite database with a schema version from 1 up to the
     app's latest. A newer one is refused, because it comes from a newer app.
   - backs up the current data first, so a restore can be undone.
   - closes the databases (and the cached "My games" pack), replaces the files (WAL/SHM
     included), reopens them (an older backup is migrated), applies the settings and goes back
     to Home.
3. Installed piece sets, themes, engines and packs are not part of the backup: they are
   re-importable files, not the user's history.

## Consequences

- A backup is a few hundred KB to a few MB (puzzle progress dominates). Nothing is automatic. A
  later option could back up on exit once a week; it would cost a VACUUM INTO per backup.
- A restore cannot merge two histories. It replaces them, and the previous data is kept in `exports/`.

# ADR 0011 — SessionState lives in chess.db, not in session.yaml

- Status: Accepted (2026-09-29). Changes architecture doc §6 ("`user/session.yaml` ... atomic
  write (temp file + rename)").

## Decision

The resumable state (design notes §30) is stored in `user/chess.db`, table `session(key, value)`
(schema v2). `storage::saveSession` replaces the whole table in one transaction on every
meaningful change (move, undo, screen change) and on background/exit; an empty table means there
is nothing to resume. At launch a non-empty session shows **Resume? [Yes] [Home]**.

The values are strings chosen by the screen that owns the session (for example `screen = play`,
`play.moves = e2e4 e7e5 ...`); the engine is never part of it (it restarts on demand).

## Why

- `chess.db` is already open for the whole run, and SQLite transactions (WAL, `synchronous=NORMAL`)
  give the crash safety that the YAML plan needed a temp-file + rename for, including on the
  reader's vfat where rename atomicity is not guaranteed.
- No YAML writer is needed (`util::Yaml` only reads), and there is one fewer file in `user/`.
- The same key/value code serves `settings` and `session`.

## Consequences

- A corrupt or unreadable `chess.db` loses settings and session together; the app then runs with
  defaults and says so in the log (design notes §32), as for settings.
- Screens that support resume implement `Screen::saveSession(map&)` and a restore function; the
  app saves the topmost screen in the stack that returns true (so Settings or a dialog opened over
  a game keeps the game resumable).

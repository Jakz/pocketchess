# ADR 0003 — Waking the InkView loop from background threads (Host::post)

- Status: Proposed (2026-09-28) — decide with the M0.5 spike. Addresses R4.

## Decision (initial)

`InkViewHost` supports two modes (`PostMode`):

1. **Poll** (default): `post()` only enqueues. While background work is active
   (`Host::setBackgroundActivity(true)`, set by engine jobs and the engine probe), a 100 ms
   `SetHardTimer` on the UI thread drains the queue. No timer runs otherwise (battery rule §31.5).
2. **SendEvent** (candidate): `post()` enqueues and calls `SendEvent(handler, 0x7E01, 0, 0)` from
   the posting thread. Faster and timer-free, but InkView's thread safety is undocumented.

All timers share one `SetHardTimer` armed for the earliest deadline.

## To measure

Run Diagnostics → Engine in both modes; record in `docs/project-status.md` whether SendEvent from
the reader thread is delivered reliably (no crash, no lost wake-ups over 100 probes). If it is,
switch the default and keep Poll as the fallback.

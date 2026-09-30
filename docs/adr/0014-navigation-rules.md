# ADR 0014 — One navigation model for every screen

- Status: Accepted (2026-09-30), after the first device review of M6. Design notes §21, §39; R7.

## Context

Each milestone added screens with their own conventions: leaving a screen worked through the
status bar's small "‹ Home/Back" (5.4 mm tall), a "Home" menu item, "Back to the list", or a hidden
focus stop; "Back" meant both *previous move* and *leave*; the bottom rows differed per screen
(Settings had no Menu, Games put Menu in the middle); words varied (Prev, Back, Close, Cancel).
On the PB634K3 only Prev, Next and Menu reach the app (Back/Home are taken by the system).

## Decision

1. **Top bar**: the left quarter is a framed button "‹ *screen underneath*" (its `Screen::title()`,
   found on the stack: "‹ Games" in a game, "‹ Home" elsewhere). It is the only on-screen way back.
   The bar is 7.5 mm tall (was 5.4 mm); the board lost 3 px per square for it (131 px on the reader).
2. **Bottom bar**, same order everywhere: `◀` `▶` at the left do what the Prev/Next keys do on
   that screen, with a caption (*move*, *page*, *retry*/*next*, *undo*); then the screen's own
   actions; `Menu` always at the right, the same as the Menu key.
3. **Menus** end with "‹ Back to *screen underneath*" and "Close", so the three hardware keys are
   enough everywhere. List screens (Games, Import, Settings) do not move a focus cursor: Prev/Next
   turn pages and the Menu offers "Open a game…", "Choose an item…" or "Change a setting…".
4. **Words**: "Close" dismisses a dialog; "Back" only leaves a screen; steps are arrows.

## Consequences

- Home keeps its own launcher (no way back from it, Prev/Next move the focus, Menu opens).
- Diagnostics stays a tool screen: Menu leaves it outside the key-capture mode.
- Every golden image changed once; `tests/app/RedrawTests.cpp` and the key-only tests cover the
  rules.

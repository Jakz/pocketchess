# ADR 0014 — One navigation model for every screen

- Status: Accepted (2026-09-30), after the first device review of M6. Design notes §21, §39; R7.

## Context

Each milestone added screens with their own conventions: leaving a screen worked through the
status bar's small "‹ Home/Back" (5.4 mm tall), a "Home" menu item, "Back to the list", or a hidden
focus stop; "Back" meant both *previous move* and *leave*; the bottom rows differed per screen
(Settings had no Menu, Games put Menu in the middle); words varied (Prev, Back, Close, Cancel).
On the PB634K3 only Prev, Next and Menu reach the app (Back/Home are taken by the system).

## Decision

1. **Top bar**: title and status only, no controls (revised 2026-10-01: a back button at the top contradicted one-hand use). The bar is 5.4 mm tall again; the board keeps 134 px squares.
2. **Bottom bar**, same order everywhere: `←` (back to the screen underneath, whose name is the caption: "Home", "Games"; an opening family goes back to "Openings") at the far left, then `◀` `▶` do what the Prev/Next keys do on
   that screen, with a caption (*move*, *page*, *retry*/*next*, *undo*); then the screen's own
   actions; `Menu` always at the right, the same as the Menu key.
3. **Menus** end with "← Back to *screen underneath*" and "Close"; every choice list (settings, Play setup) ends with "Close", so nothing forces a choice, so the three hardware keys are
   enough everywhere. List screens (Games, Import, Settings) do not move a focus cursor: Prev/Next
   turn pages and the Menu offers "Open a game…", "Choose an item…", or (Settings) the page's settings directly.
4. **Controls live at the bottom** (one-hand use, decided 2026-10-01): every in-view control group
   (button bars, filter chips, the Home launcher, the Diagnostics grid) sits at the bottom of the
   screen, directly above the bottom bar. Only modal dialogs are centered.
5. **Words**: "Close" dismisses a dialog; "Back" only leaves a screen; steps are arrows.

## Consequences

- Home keeps its own launcher (no way back from it, Prev/Next move the focus, Menu opens).
- Diagnostics stays a tool screen: Menu leaves it outside the key-capture mode.
- Every golden image changed once; `tests/app/RedrawTests.cpp` and the key-only tests cover the
  rules.

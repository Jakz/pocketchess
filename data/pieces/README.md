<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Piece sets

One folder per set, staged to `assets/pieces/<id>/` (ADR 0012). Each folder has a `pieceset.yaml`
manifest (name, author, license, source, optional `scale`, `stroke_scale`, `base`) and the twelve
SVGs `{w,b}{K,Q,R,B,N,P}.svg`, rasterized on the reader at the board's square size.

| Folder | Author | Licence | Source |
| --- | --- | --- | --- |
| `cburnett` | Colin M.L. Burnett | GPL-2.0-or-later | Lichess `public/piece/cburnett` |
| `merida` | Armando Hernandez Marroquin | GPL-2.0-or-later | Lichess `public/piece/merida` |
| `chessnut` | Alexis Luengas | Apache-2.0 (`LICENSE.txt`) | Lichess `public/piece/chessnut` |
| `bold` | PocketChess (Cburnett shapes) | GPL-2.0-or-later | `base: cburnett`, `stroke_scale: 1.8` |

Lichess files come from `lichess-org/lila` commit `9641f3c031c5de39081ccb2bec640061cfd54912`; the
licences are the ones in its `COPYING.md`. Sets marked non-commercial (CC BY-NC-SA) or freeware
there are not GPL-compatible and are not bundled; users can still add them through Import.

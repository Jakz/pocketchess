# Opening names (ECO)

`a.tsv` … `e.tsv` from [lichess-org/chess-openings](https://github.com/lichess-org/chess-openings)
at commit `c67912be581f0793dbaa776be5ccf111e01f88d9` (CC0-1.0, see `COPYING.txt`). Columns: `eco`,
`name`, `pgn` (SAN moves from the start position).

`PgnImporter` replays every line and classifies each imported game by the deepest of these
positions it reaches (by position, so transpositions are found). Update by replacing the five
files and the commit above.

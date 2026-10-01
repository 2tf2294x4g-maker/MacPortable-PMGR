# J21 breakout (development only)

A DF12 pass-through that sits between the interposer and the MCU board and brings every PMGR signal
out to **two 1×25 2.54 mm headers labelled with the Portable's J21 pin numbers** — ready for Dupont
leads, a logic analyser or a scope. Take it out before closing the machine.

| | |
|---|---|
| Size | 27 × 88 mm |
| Fab | 2 layer · 1.6 mm · HASL |
| Parts | `J1` DF12 socket (`DS`, bottom), `J2` DF12 header (`DP`, top), two 1×25 2.54 mm headers |
| Fabrication files | `gerbers/` (with BOM, placement and assembly drawings for a PCBA order) |
| Design | `pmgr_breakout_j21.kicad_pcb` (KiCad 9) — see `BOM.md` |

⚠️ **Vias are 0.20 mm drill with a 0.125 mm annular ring.** PCBWay builds that on their standard
order; JLCPCB's standard process needs 0.30 mm / 0.13 mm.

Hand assembly: **solder `J2` (top) first, `J1` (bottom) last**, with the board raised on standoffs —
they sit back to back. Mate each DF12 pair by hand before soldering: every stack height shares the
footprint, and these must be 3.0 mm.

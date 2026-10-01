# Interposer

Solders onto the M50753's footprint on the logic board by 60 castellated half-holes, and carries the
DF12 header (`J1`) the MCU board plugs into. It remaps the original chip's pins to the connector —
see [`../../docs/PINOUT.md`](../../docs/PINOUT.md).

| | |
|---|---|
| Size | 16.3 × 16.3 mm |
| Fab | 4 layer · 1.6 mm · **ENIG (required)** · castellated plating on all 60 half-holes |
| Layers | F.Cu / In1.Cu GND / In2.Cu GND / B.Cu |
| Fabrication files | `gerbers/`, `pmgr_interposer_v2_gerbers.zip` |
| Design | `pmgr_interposer_v2.kicad_pcb` (KiCad 9) |

The Gerbers here were regenerated from the board file. Copper, outline and drill are identical to
the boards built and tested in development; only the silkscreen differs — the connector is labelled
`J1`, where the first batch read `PMGR Chip`.

Assembly: [`../../docs/ASSEMBLY.md`](../../docs/ASSEMBLY.md), Steps 2 and 4.

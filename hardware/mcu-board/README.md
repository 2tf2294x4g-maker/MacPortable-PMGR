# MCU board — rev A

> **Rev A is the built and tested MCU board.** [Rev B](../mcu-revb/README.md) adds the SCSI Disk Mode
> path (DF12 position 50 → PF2), edge pads for in-machine programming, and sits 5 mm toward `J2` so the
> Portable's case closes over it; it is designed but not yet fabricated. See the table on the rev B page.
>
> ⛔ **Rev A does not fit the Portable with the case closed** (the case catches the edge opposite `J2`).
> It fits and works in the PowerBook 100. For a Portable, build rev B.

Carries the AVR128DB64, its decoupling, a clamp on the battery-sense input, the DF12 socket that
mates with the interposer (`J1`, bottom), and a 2×3 UPDI programming header (`J2`).

| | |
|---|---|
| Size | 20.0 × 23.5 mm |
| Fab | 4 layer · 1.6 mm · ENIG · two-sided stencil |
| Fabrication files | `gerbers/`, `pmgr_mcu_gerbers.zip` |
| Design | `pmgr_mcu.kicad_pcb` (KiCad 9) |

The Gerbers here are the ones the tested boards were made from, and are checked against
`pmgr_mcu.kicad_pcb` on every layer each time this repository is generated. Assembly: [`../../docs/ASSEMBLY.md`](../../docs/ASSEMBLY.md), Step 1. Connections:
[`../../docs/MCU-BOARD-SCHEMATIC.md`](../../docs/MCU-BOARD-SCHEMATIC.md).

⚠️ For a PowerBook 100, `J2` must not be a vertical header — there is only 9–10 mm of clearance.

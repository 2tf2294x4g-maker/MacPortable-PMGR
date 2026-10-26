# breakout-pb100 — BOM

Build **one**. A bench jig for the PowerBook 100 with the case open; it comes out before the machine is closed.

| Ref | Value | Package | Side | Qty | Note |
|---|---|---|---|---|---|
| J1 | **DF12NC(3.0)-50DS-0.5V(51)** | Hirose DF12N 50-pos, 0.5 mm | **Bottom** | 1 | Socket: receives the interposer |
| J2 | **DF12NC(3.0)-50DP-0.5V(51)** | Hirose DF12N 50-pos, 0.5 mm | Top | 1 | Header: receives the MCU board, **turned 180°** (its J2/UPDI end toward the tip) |
| J5 | 1×25 **2.54 mm** pin header | THT | Top | 1 | J21 **ODD** 1–49 |
| J6 | 1×25 **2.54 mm** pin header | THT | Top | 1 | J21 **EVEN** 2–50 |
| J7 | 1×2 **2.54 mm** pin header | THT | Top | 1 | `DISK_MODE_N` (chip pin 48 / IN5, DF12-50) and GND |
| H1–H3 | 2.2 mm holes | — | — | — | Not parts: foot / standoff for the cantilever |

⚠️ Same part-number note as `../breakout-j21/BOM.md`: the footprints say `DF12C` / `DF12E`, the parts to buy are **DF12N**
(DF12C is obsolete). ⛔ Confirm the `(51)` suffix on BOTH halves and that the pair mates before ordering.

## Board spec

```
  4 layer · 1.6 mm · 27 × 113 mm · min track/space 0.15 / 0.15 mm · via 0.6 / 0.3 mm
  stack: F.Cu / In1.Cu (solid GND) / In2.Cu (signals) / B.Cu
```

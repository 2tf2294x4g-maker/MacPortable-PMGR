# carrier-m50753 — BOM

| Ref | Value | Package | Side | Qty | Part / note |
|---|---|---|---|---|---|
| U1 | **M50753** (original PMGR) | 60P6 QFP, 0.8 mm | Top | 1 | the chip being carried. ⛔ **Soldered, no socket** (PB100 height). Pin 1 at the triangle. |
| J1 | **DF12NC(3.0)-50DS-0.5V(51)** | Hirose DF12N 50-pos socket | **Bottom** | 1 | same as the MCU board's J1. The footprint says DF12C; buy **DF12N** (DF12C is obsolete). ⛔ Check the `(51)` suffix and that it mates with the interposer's header. |
| Y1 | **3.93216 MHz**, 20 pF | HC-49/US short can, 3.5 mm, THT | Top | 1 | **JYJE S1C39321XWJAC**, LCSC **C2764489**, on the circle-side tab. Stand it upright and trim the leads flush underneath. |
| C10, C11 | **33 pF**, C0G | 0402 | Bottom | 2 | crystal load caps (sized for the 20 pF crystal), between Y1's legs |
| C1, C2 | 100 nF, X7R | 0402 | **Top** | 2 | C1 at pins 55/56 (V_REF/AVCC), C2 at pin 45 (VCC), beside their pins |
| C3 | 10 µF, ≥ 10 V | 0805 | Bottom | 1 | bulk, on the back of the tab |
| R2, R3 | 1 kΩ | 0402 | Bottom | 2 | series, IN1 (AD_FILTER) and IN0 |
| R4 | 100 kΩ | 0402 | Bottom | 1 | IN4 pull-down |
| D2, D3 | **BAT54S** | SOT-23 | Bottom | 2 | rail clamps on IN1 / IN0. Same part and orientation as the MCU board's D1. |
| TP1-TP5 | — | 1.0 mm pads | Top | — | not parts: GND, +5V, PHI, IN0, IN1 |

## Board spec

```
  4 layer · 1.6 mm · 27.75 x 21 mm · ENIG (fine-pitch DF12 + QFP) · min track/space 0.15/0.15 · via 0.45/0.20, tented
  two-sided stencil (parts on both faces)
```

Assembly order:
1. Back parts and J1 first, with a stencil or by hand.
2. Then U1 on the top: drag-solder the QFP.
3. Then Y1, through-hole, last.

⚠️ The tab's back faces the logic board 4.6 mm below, so trim Y1's leads.

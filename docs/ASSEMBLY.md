# Assembly — from bare boards to a stack ready for `BRING-UP.md`

Boards arrive **bare**. This is the order to put them together in, what to check
between steps, and where the irreversible mistakes are.

> ⚠️ **Written 2026-09-07, before the boards arrived. Being performed for the
> first time from 2026-09-08.**
>
> | step | status |
> |---|---|
> | **Step 2 — interposer** | ✅ **ONE COMPLETE, 2026-09-08** — the first assembled board in this project |
> | Step 1 — MCU board | ⏳ in progress |
> | Steps 3–6 | ⛔ not started |
>
> ⭐ Every step is derived from this repository's own measurements and board files
> — none of it is generic SMD advice. ⛔ **Treat a surprise as evidence about the
> procedure, not only about the hardware**, and amend the text as you go: three
> corrections already came from the bench on day one (the Gate 0 circles, the
> `DF12` naming, and the legitimate adjacent shorts).

---

## ⛔⛔ GATE 0 — ORIENTATION. Resolve this BEFORE any solder.

**This is the one mistake that cannot be undone.** The pad ring is symmetric, so
a wrongly-rotated interposer fits perfectly and is wrong by 90° or 180°.

> ⛔⛔ **CORRECTED 2026-09-08, ON THE DELIVERED BOARDS (2.76).** The section
> below describes **three circles with the smallest at pin 1**. ⚠️ **That is not
> what was fabricated.** The interposer carries **two NUMBERED circles — ① and
> ⑮ — plus one large plain circle.**
>
> ⭐⭐ **Use the numbers.** On the PowerBook 100 they read directly against
> Apple's own silkscreen: interposer **①** to `U22`'s **1** (bottom edge),
> interposer **⑮** to its **15**. Confirmed by placement on the real footprint.
>
> ⭐ **The delivered scheme is the better one** — a number cannot be mistaken for
> another number, where "smallest of three" needs all three in view and
> comparable. ⛔ Kept below because the *reasoning* about Apple's marks still
> holds; only the description of **our** board was wrong.

### ⭐⭐ THE CIRCLES ARE THE METHOD — ours match the chip's and the board's

⭐ The three circles on our boards were placed **deliberately, to match the ones
on the M50753 package and on Apple's `U15H`/`U22` footprint**. So aligning
circle-to-circle is correct, and it is the intended way to seat the interposer.

⭐⭐ **Three marked corners with the fourth blank is not rotationally symmetric**,
so the pattern alone fixes the orientation — there is no 90° or 180° ambiguity
left once the circles line up. ⭐ `interposer-v2/README.md` adds a second,
independent check: the **smallest** circle sits at pin 1.

> ⚠️ **An earlier draft of this section had this backwards.** It claimed ours and
> Apple's were two different systems that merely looked alike, and warned against
> aligning them — the opposite of the truth. Recorded because a procedure that
> invents a hazard wastes as much attention as one that misses a real one.

⭐ What 2.63 established still holds and is worth knowing, because it explains
*why* the circles work: Apple's three are an **orientation key matching the chip's
mould marks**, and pin 1 is separately marked by a **filled white dot**. Two
independent markings, both usable.

### ⛔ THE TWO MACHINES MARK IT DIFFERENTLY — use the right check for each

⚠️ **They are complementary: each has what the other lacks.** Do not carry a
method from one board to the other.

| machine | what the footprint carries | how to verify the interposer |
|---|---|---|
| **PowerBook 100 `U22`** | ⭐⭐ **pin numbers printed right around the pad ring, every 5 pins on all four sides** — `1 5 10 15` · `16 20 25 30` · `31 35 40 45` · `46 50 60` — plus a **pin-1 mark**. ⛔ **NO circles** | ⭐ verify from **any** edge, and cross-check on a second — four-sided numbering means orientation is over-determined, not a single mark to find. ⛔ Do not look for circles to align; there are none |
| **Macintosh Portable `U15H`** | ⭐ **three circles** + the **filled pin-1 dot**. ⛔ **NO printed numbers** — a plain box marked `V15H` with `PMGR` beside it | **circles line up**, and the smallest sits at the dot |

⭐⭐ **Confirm on the PB100 first even though the Portable is fitted first.** Its
four-sided numbering is the strongest reference either machine offers — you can
read the answer off two independent edges — and checking there tells you what
"correct" looks like before doing it on a board that prints nothing at all.

⭐ **And buzz it either way before committing:** interposer castellation **1**
carries net `VIAD4`, and chip pin 1 is `P24`/`VIAD4` on Apple sheet 12 — an
electrical check that needs no silkscreen at all.

---

## Step 1 — MCU board (`pmgr_mcu`), 20.0 × 23.5 mm

**14 placements, 5 of them bottom-side.**

| side | parts |
|---|---|
| **Top** | `U1` AVR128DB64 TQFP-64 · `J2` 2×3 UPDI header · `C1–C3` 100 nF · `C5–C7` 10 nF · `C8` 10 µF 0805 |
| **Bottom** | `J1` DF12 socket · `D1` BAT54S · `R1` 1 kΩ · `C4` 100 nF · `C9` 10 nF |

### ⭐ Hand assembly — the chosen route for the first article

⛔ **BOTTOM SIDE FIRST**, and for hand work the reason is mechanical: once `U1`
and the top-side passives are on, working the bottom means resting the board on
them. Do the bottom while the top is still flat and bare.

⭐⭐⭐ **AND DO `J1` BEFORE ANYTHING ELSE IF AN INTERPOSER IS ALREADY BUILT** —
a second reason, learned 2026-09-08 once interposer #1 was finished (2.78). With
its header in hand you can **mate the two immediately**, which is the only way to
catch a stack-height mismatch: the land pattern is identical across
3.0/3.5/4.0/5.0 mm, so a wrong-height part solders down perfectly and fails
silently. ⭐ Catch it with **one part** on the board rather than fourteen.

⭐ Practical: it is also the **same joint you have just done**, 50 pins at 0.5 mm,
so do it while the technique is warm rather than after a break. ⚠️ Expect it to be
harder to *inspect* — the socket's contacts are recessed where the header's sit on
the outside of its box.

**Within each side, hardest first, while nothing is in the way:**

```
  BOTTOM   1. J1  DF12 socket    50 pins at 0.5 mm — the hardest joint here
           2. D1  BAT54S SOT-23
           3. R1, C4, C9         0402
  TOP      4. U1  AVR128DB64     TQFP-64, 0.5 mm — drag-solder, flux, wick
           5. C1-C3, C5-C7, C8   0402 and one 0805
           6. J2  2x3 header     LAST — through-hole and the tallest thing
```

⚠️ **`J2` last, and trim its pins after** (`BOM.md`). It is through-hole on a
board with a 3.0 mm gap beneath, so untrimmed pins foul the stack.

### ⭐⭐⭐ CHECKPOINT — read the device ID at step 5, BEFORE fitting `J2`

⛔ **Do not wait until the board is finished to find out whether `U1` is alive.**
Once `U1` and the top-side passives are on, the chip has everything it needs to
answer: `VDD`, `GND`, decoupling, and `UPDI` on pin 51.

```
  clip test leads onto the J2 PADS  (through-hole, easy to reach while empty)
     J2-1  UPDI     J2-2  +5V     J2-6  GND
  read the device ID
```

⭐⭐ **One response proves the hardest part of the build:**

| ✅ | |
|---|---|
| `U1` survived the drag-solder | the 0.5 mm joints conduct |
| it is the right part | signature reads `AVR128DB64` |
| `UPDI` reached pin 51 | that trace and joint are good |
| power and ground are sound | it could not answer otherwise |

⚠️ **This matters more without a socket adapter** (none is on hand — see below):
there is no way to pre-screen a chip, so this is the *first* moment the part can
be shown alive. ⛔ **A dead chip and a bad drag-solder look identical**, and
finding out here — with `J2` not yet committed and the board otherwise finished —
is the cheapest place to start telling them apart.

⚠️ **`C9` is easy to miss.** It was added late, after the pin audit found
AVR pins **56/57** unconnected — it is not in older BOM prints.

### ⭐ WHICH PART GOES WHERE — `DS` vs `DP`

#### ⭐⭐⭐ TELLING THEM APART BY EYE — observed on the parts, 2026-09-08

```
  HEADER  (DP)   an OPEN BOX, with the contacts on the OUTSIDE of the box
  SOCKET  (DS)   contacts in the MIDDLE, which reach out and meet the box's
                 outside when the two are placed together
```

⭐ **That is the reliable identification** — it needs no part number and no
male/female vocabulary, both of which mislead here. ⚠️ Neither part looks like the
usual "posts vs holes" picture, which is why two earlier attempts to describe them
in this document were wrong.

⭐ **One inference worth having, flagged as inference:** the header's contacts sit
on the *outside* of its box and are therefore the more visible and reachable of
the two. ⛔ The **MCU board gets the socket** — the recessed one — **on its
underside**, so that is the joint that is hardest to inspect *and* the one you
cannot see once the top is populated. Budget the magnification accordingly.

⛔⛔ **PLACEMENT STILL GOES BY THE PART-NUMBER SUFFIX, NOT BY APPEARANCE.**
`DS` and `DP` are **Hirose's suffixes**, not a description of the physical form —
observed at the bench 2026-09-08, the part called the *header* is the one with the
**opening the other sits into**, which is the opposite of the usual mental image.
⚠️ An earlier version of this table glossed them as *"receptacle"* and *"posts"*
and that gloss is **withdrawn**; it would have someone sorting parts by eye
against the wrong picture.

```
  MCU board     J1   ...-50DS-...   BOTTOM, facing DOWN
  interposer    J1   ...-50DP-...   TOP,    facing UP
  breakout      J1   ...-50DS-...   bottom   (receives the interposer)
                J2   ...-50DP-...   top      (receives the MCU board)
```

⭐⭐⭐ **And the check that settles it without any naming at all: MATE THEM BY
HAND.** If the two parts you are about to fit click together, you have the right
pair *and* the right stack height. ⭐ That test does not care what anything is
called, which is exactly why it is the one to trust.

⭐⭐ **The rule in one line: every board presents `DP` upward and `DS` downward.**
The interposer is the bottom of the stack, so it carries only the up-facing `DP`;
the MCU board is the top, so it carries only the down-facing `DS`; the breakout is
an insert and has both.

⛔⛔ **THE EXPENSIVE MISTAKE IS NOT `DS` vs `DP` — IT IS THE STACK HEIGHT.**
`BOM.md`: the land pattern is **identical across 3.0 / 3.5 / 4.0 / 5.0 mm**, so a
wrong-height part solders down perfectly and **silently fails to mate**.

```
  confirm (3.0) AND the (51) suffix on BOTH halves
  MATE ONE SOCKET TO ONE HEADER BY HAND      <- while they are still loose parts
```

⚠️ Thirty seconds, and it is the only check that catches a height mismatch
**before it is permanent**.

### ⛔⛔ `J1` HAS NO MECHANICAL KEY — VERIFY THE ROTATION WITH A METER

⚠️ **`DF12NC` means "without boss, without fitting"** (`BOM.md`). ⛔ **Nothing
physically prevents soldering it 180° round.** The obsolete `DF12C`/`DF12E` parts
were specified the same way, so there is no guide boss to seat and no metal
fitting to align — the **footprint's pin-1 marking is the only guide**, on the
board's hardest joint, on the side you cannot see once the top is populated.

⭐⭐⭐ **So check it electrically, right after tacking and before soldering all
50 pins.** Power and ground make this trivial and need **no chip fitted**:

```
  CORRECT     GND at 23, 24, 25, 49      +5V at 47, 48
  180 deg     GND at  2, 26, 27, 28      +5V at  3,  4
```

⭐ A 180° rotation maps pin **N → 51−N**, so the two cases share no pins at all —
there is no ambiguous middle. ⚠️ **Buzz `23`, `24`, `25` and `49` to the ground
plane.** Four pins, one meter setting, and the answer is yes or no.

⛔ **Do this while only two corners are tacked.** Reworking a 50-pin 0.5 mm
connector after all 50 are down, on the underside of a board you then have to
rebuild, is the most expensive mistake available in Step 1.

### ⛔⛔⛔ HUNTING BRIDGES: THREE ADJACENT PAIRS ARE *SUPPOSED* TO BE SHORTED

⚠️ **Reported from the bench 2026-09-08: this joint is slow and produces plenty
of bridges.** That is expected — the table above already calls bridges *"normal,
expected, not rework"*. ⛔ **But do not go looking for them with a meter without
knowing this first:**

```
  23 - 24    both GND     <- reads as a dead short, and is CORRECT
  24 - 25    both GND     <- correct
  47 - 48    both +5V     <- correct
```

⭐⭐⭐ **Every OTHER adjacent pair should be open.** Without this list an operator
finds three "shorts", concludes the joint failed, and reworks pins that were
perfect — adding heat cycles to a 4-layer board to fix nothing.

⭐ **Technique, in the order that usually works:** more **flux**, not more heat —
nearly every fine-pitch bridge is a flux problem, and flux that has already been
through a heat cycle has spent its activator. **Flux the braid too**; dry wick
needs more dwell and dwell is what lifts pads. **Work a whole edge in one pass**
rather than chasing bridges individually. ⛔ **Let the board cool between
passes** — cumulative heat on a ground-planed 4-layer board is what does damage,
not any single pass.

### ✅ WHAT "DONE" MEANS FOR `J1`

```
  [ ] no adjacent shorts  EXCEPT  23-24, 24-25, 47-48
  [ ] GND present at 23, 24, 25, 49        (also the rotation check)
  [ ] +5V rail at 47, 48
  [ ] inspect under magnification NOW, while the top side is still bare
```

⭐ The last line is free at this moment and impossible once `U1` is on.

⛔ **`D1` BAT54S is the ADC clamp**, and `BOARD-DESIGN-BRIEF.md` calls it *"the
design's one genuine improvement over Apple's"*. ⚠️ It is a bottom-side part, so
it goes on in step 2 and is invisible afterwards. Do not defer it.

### ⛔ `U1` — prevention, because rework on this board is genuinely bad

`U1` is the **AVR128DB64-I/PT**, TQFP-64, 10×10 mm, **0.5 mm pitch**, top side.
`BOM.md` says "order a spare — awkward to rework". Here is why, and what to do
instead.

⛔ **Why removal is worse here than on a typical board:** by the time `U1` goes
on, the **bottom side is already populated** — `J1`, `D1`, `R1`, `C4`, `C9`. Hot
air on the top means the board resting on those parts, heat conducting through a
**4-layer 1.6 mm board with ground plane**, and a real chance of shifting or
losing bottom-side 0402s you cannot see. ⭐ Expect to lose neighbouring top-side
passives too.

⭐⭐ **So the plan is prevention, in this order:**

```
  1. tack pin 1 and the DIAGONALLY OPPOSITE pin only
  2. STOP. Inspect alignment under magnification, all four sides
  3. only then drag-solder the rest
```

⭐ Two tacked corners can be reflowed and nudged freely. Sixty-four cannot.

**If it does go wrong, by severity:**

| symptom | response |
|---|---|
| **bridges** | ⭐ flux and wick — normal, expected, not rework |
| **one or two lifted/unwetted pins** | more flux, re-drag that edge |
| **misaligned by a pin, or rotated** | ⛔ full removal. Hot air with **preheat**, plenty of flux, lift straight up. Budget the spare `U1` and expect passive collateral |
| **chip dead** | replace with a spare. ⛔ **With no socket adapter you cannot pre-verify the replacement either**, so this is where the 3 spares matter more than any tool |

✅ **Four `U1` on hand** — Mouser `579-AVR128DB64-I/PT`, $2.31 ea, shipped
2026-08-31. That matches `BOM.md`'s quantity for three boards plus a spare.
⚠️ Note the BOM cites a **DigiKey** part number (12807611); same MPN, different
distributor — no issue, but the BOM is not the only source used.

⭐⭐ **BUILD ONE FIRST, not the whole lot** — the chosen approach, and it is the
right one twice over:

```
  build all at once   1 spare across 3 boards    thin
  build ONE first     3 spares for that board    generous
```

⭐ It also means learning the process — drag-soldering a 0.5 mm TQFP, the
bottom-side `DF12` — on an article you can afford to lose, before committing
parts to the rest. ⚠️ An earlier note here warned that "one spare across the
build" left no margin. That assumed building all of them at once; it does not
apply to building one.

⛔⛔ ~~**Verify the chips in the `T48` before the boards arrive**~~ — **NOT
AVAILABLE. Corrected 2026-09-08: there is NO TQFP-64 socket adapter.** This said
*"the socket adapter is on hand"*, which was never true, and the pre-screen was
recommended repeatedly on that basis.

⭐ **It changes less than it sounds like.** The pre-screen was insurance against
a **DOA part from an authorised distributor** — a low-probability event. What
replaces it costs nothing:

| | |
|---|---|
| ⭐⭐ **build ONE board first** | already the plan, and now load-bearing rather than merely sensible: 4 chips, 1 board, **3 spares** |
| ⭐⭐⭐ **device ID as the FIRST command** | before fuses, before flash — see Step 3 |
| ⚠️ the failure you must not make | a dead chip **looks exactly like a bad drag-solder**. With three spares you can afford to find out which |

⭐ A socket adapter is still worth owning eventually — it is the only way to
prove a part *before* committing it — but it is **not** a prerequisite and should
not hold up Step 1.

> ⭐ *If you later move to stencil and reflow: same bottom-first order, for the
> different reason that the DF12 is the alignment-critical part and should not be
> reflowed while the board sits on top-side components.*

⚠️ `J2`'s pins must be **trimmed after soldering** (`BOM.md`) — it is a
through-hole header on a board with a 3.0 mm stack gap beneath it.

⛔ `D1` **BAT54S is the ADC clamp** and the design's one genuine improvement over
Apple's (`BOARD-DESIGN-BRIEF.md`). Do not omit it "for now" — it is what protects
the ADC input the original had no margin on.

### Check before moving on

- ⛔ **`+5V` to `GND` not shorted.** Four decoupling pairs and two 0.5 mm-pitch
  parts make a bridge easy and invisible
- ⭐⭐ **Meter every `J1` pin against its neighbour**, not just eyeball it. 50 pins
  at 0.5 mm hand-soldered is where a bridge will be, and it is underneath where
  you cannot see it once stacked
- `U1` pin 1 orientation against the silkscreen
- ⚠️ **`C9` placed** — the late addition above
- ⭐ Continuity `J1` → `U1` on a couple of known pairs, using the generated map in
  `README.md` (e.g. `PMGR_IN0` → `PD7`, `SYS_PWR*` → `PG0`). Catches a rotated or
  shifted connector before it reaches a machine

---

## Step 2 — Interposer (`pmgr_interposer_v2`), 16.30 mm square

⭐ **One part only.** `J1`, the **`DF12NC(3.0)-50DP-0.5V(51)` header** (`DP` =
posts), on the **top**, facing **up**. ⚠️ Older text here said `DF12E(3.0)` —
that part is **obsolete** and is not what is in the bag (`BOM.md`). Everything
else on this board is copper: 60 castellations and the connector pads.

⚠️ ⛔ **Boards from this run are silkscreened `PMGR Chip`, not `J1`** — the
designator was fixed after ordering (`BOM.md`). Nothing electrical differs.

### ✅✅ ~~DO THIS BEFORE THE BOARDS ARRIVE~~ — **MEASURED 2026-09-07, IT FITS**

> ✅ **CLOSED by `MEASUREMENTS.md` 2.73.** Caliper across the PowerBook 100's
> **bare `U22` pad ring**: **16.3 mm**, landing on the **middle of the pads**, with
> a **13 × 13 mm** body. The fabricated interposer is right. ⭐ Everything below is
> kept as the reasoning, and the 14-gap check is now **optional** — an independent
> route to the same number, not a prerequisite.

⚠️ **The interposer's geometry traced to a design-time assumption that was
flagged for confirmation and never confirmed.** `BOARD-DESIGN-BRIEF.md`:44 reads
*"matches the M50753 pad ring (~17–18 mm square **if 0.8 mm pitch / 14 mm body —
confirm by measurement**)"*, and the fabricated board is **16.3 mm square, 15
castellations per side at 0.8 mm**.

⛔ **The "it lands on the pad ring" check is circular for SIZE.**
`CLEARANCE-MEASUREMENTS.md` says so plainly — *"reference and subject are the same
object"*. The photogrammetry took 16.30 mm as its scale, so it confirms
**squareness** (axes agree to 0.1 %) and cannot confirm **magnitude**.

⭐⭐⭐ **One caliper measurement settles it, and a chip is on the bench now.**

| measure | expect | why |
|---|---|---|
| ⭐⭐ **lead 1 tip → lead 15 tip, one side** | **11.20 mm** | ⭐ **the sharpest test — 14 gaps, so caliper error divides by 14.** At 0.65 mm pitch this reads **9.10 mm** |
| lead tip → lead tip, across the package | ~**16.3 mm** | the pad-ring figure the interposer is built to |
| body only, across the plastic | ~**13 mm** | `breakout/README.md`'s datum — ⚠️ from a *silkscreen* outline, not a datasheet |

⛔⛔ **Measure across 14 gaps, not one pitch.** A single 0.8 mm gap is inside
caliper reading error; 11.20 mm is not, and it discriminates 0.8 from 0.65 by
**2.1 mm** — unmissable.

⭐ **If it reads 11.2 mm, the interposer fits and this closes.** ⚠️ **If it reads
9.1 mm, the fabricated boards do not fit the chip at all** — and finding that now
rather than with a soldering iron in hand is the entire point of doing it before
they land.

---

## Step 3 — Program the AVR BEFORE it goes near a machine

⭐ **Flash and verify on the bench, with the MCU board unmated.** Reaching `J2`
is easy now and awkward once the stack is in a Mac.

```
  firmware   firmware/pmgr.hex     text 10715 bytes, avr-gcc 14.3.0
             ⭐ byte-identical to baselines/2026-09-30-charging-adc-interlock/
```

### ⛔⛔ BEFORE YOU WIRE ANYTHING — `J2` PIN 2 IS +5V, AND IT REACHES THE MACHINE

⚠️ `J2` pin 2 is the board's **VDD**, which runs through the `DF12` to the
interposer and onto the Macintosh's **`+5V ALWAYS ON`** rail.

⛔⛔ **So powering the board through `J2` while the stack is fitted energises the
Mac's always-on rail from the programmer.** ⭐ Step 3 avoids this by programming
**unmated** — but the risk is not step 3's, it is **later**: the first time you
want to reflash during bring-up, the stack will be in a machine.

```
  reflashing with the stack FITTED:
     power the board from the MACHINE, not the programmer
     -> connect UPDI (J2-1) and GND (J2-6) only.  LEAVE J2-2 OFF.
```

⚠️ A Tigard set to 5 V will happily try to drive a Macintosh's rail through a
0.5 mm connector. It will not source the current, and what happens instead is not
worth finding out empirically.

### ⭐⭐ THE FIRST COMMAND IS NOT A FLASH — READ THE DEVICE ID

⭐⭐⭐ **Before writing anything, ask the part what it is.** It needs no firmware,
no fuses and no image, and a single response proves four things at once:

| ✅ | |
|---|---|
| `U1` is **alive** | it survived the drag-solder |
| it is the **right part** | the signature reads `AVR128DB64` |
| **UPDI is connected** | pin 51 reached `J2`-1 |
| **power and ground are good** | it could not answer otherwise |

⛔ **A dead or mis-soldered `U1` found here costs one chip.** Found after fuses
and flash, it costs the same chip plus the time spent believing the toolchain was
at fault.

### The programmer

⭐ **Serial UPDI adapter (CH340E)** — on order 2026-09-07. That is enough for
Step 3: this step only needs **flashing**.

> ✅✅ **`J2` DOES carry `DBG0/1/2` — confirmed from the board file 2026-09-09.**
>
> ```
>   J2-1  UPDI      J2-2  +5V      J2-6  GND
>   J2-3  DBG0      J2-4  DBG1     J2-5  DBG2
> ```
>
> ⛔⛔ **A "correction" on 2026-09-08 wrongly denied this** and sent the operator
> to look for test pads elsewhere. There are **none** — `pmgr_mcu.kicad_pcb`
> contains no `TP` footprints, and routes `J2` pads 3/4/5 straight to U1 pads
> 49/48/47. ⚠️ The bad edit trusted `MCU-BOARD-SCHEMATIC.md`'s pin table over the
> fabricated board, with both available. ⭐ **The board file outranks the prose.**

⭐⭐⭐ **An XGecu `T48` is also on hand, and it supports UPDI** — so there are two
independent paths and the Dx-support risk is covered. Use whichever proves out;
the serial adapter is the convenient one, the `T48` is the known-good one.

| | |
|---|---|
| ⛔ **In-circuit, not the ZIF socket** | `U1` will already be soldered down, so programming in place means the `T48`'s **ICSP header** wired to `J2` — not the 48-pin socket |
| ⚠️ **Confirm `AVR128DB64` is in the Xgpro device list** | the `T48` supports the Dx family, but the specific part must be present. ⭐ A software lookup — free, and worth doing before the boards land |

⚠️ **The `T48` could verify a chip BEFORE it is soldered — but only with a
TQFP-64 socket adapter, and there is not one.** ⛔ So this path is unavailable
for the first build; the `T48` is an **in-circuit** programmer here, via its ICSP
header wired to `J2`, exactly like the other two.

⭐⭐ **A Tigard is also on hand, and it should work too — possibly best of the
three.** UPDI is a **single-wire half-duplex UART** protocol, and a serial UPDI
adapter is nothing more than a USB-serial port with **TX and RX bridged through
~4.7 kΩ**. The Tigard's UART channel does that as well as anything, and
`pymcuprog` / `avrdude -c serialupdi` do not care what is behind the port.

⭐ **Why it may be the best option:** it has a **voltage selector**, which is
exactly the worry with the cheap adapter — this board runs at **5 V**, and a
3.3 V-only adapter driving a 5 V UPDI line is marginal. The FT2232H is also a
better-behaved part than a CH340E.

✅ **Voltage selection confirmed** — the Tigard can be set to the target's rail,
so the 5 V question that hangs over the cheap adapter does not apply here.

### ⛔⛔ THE TIGARD HAS NO UPDI HEADER — AND THE RESISTOR IS 1 kΩ, NOT 4.7 kΩ

⚠️ **Both halves corrected 2026-09-08, against primary sources.**

**1. You DO need the resistor.** Checked against the Tigard's own repository: its
headers are **UART, SPI/I2C, JTAG, CORTEX, I2C-JST and LA**, and its protocols are
UART, SPI, I2C, JTAG, SWD, AVR ISP and iCE40. ⛔ **There is no UPDI header and no
UPDI circuitry.** ⚠️ Advice circulating that the Tigard has *"current-limiting
built into its UPDI output"* describes hardware the board does not have. Wiring
`TX` and `RX` together directly puts an FT2232H push-pull output against the AVR's
UPDI pin every time the target drives low — tens of mA, limited only by both
drivers' output impedance.

**2. ⛔ The value was wrong here.** This said *"~4.7 kΩ"*, a widely-copied figure.
`pyupdi`'s own README specifies **1 kΩ**, and the working range is **100 Ω – 1 kΩ,
with 470 Ω cited as optimal**.

```
  470 ohm .. 1k     the documented range      <- use this
  4.7k              above it, copied widely
  10k               well above; edge rates suffer at speed
```

⭐ **The constraint that matters is edge rate, not contention current.** A few mA
of contention is what both drivers are expected to survive; **RC on the line is
what breaks programming at higher baud**, and that gets worse as resistance rises.
⚠️ An earlier version of this note had that backwards and rejected a perfectly
good 560 Ω part in favour of 10 kΩ.

⭐⭐ **Recommended order to try:** Tigard at 5 V first (best signal integrity and
the right rail), `T48` via ICSP as the known-good fallback, CH340E adapter last.

⚠️ **Two things to confirm about the serial adapter**, because the listing says
*"ATTINY AVR"* and this is a newer **AVR-Dx** part:

| check | why |
|---|---|
| ⭐ **Toolchain knows `AVR128DB64`** | `pymcuprog` (recent) or `avrdude` **7.x** with `-c serialupdi`. Older avrdude has no Dx support at all |
| ⛔ **Adapter runs at 5 V, not 3.3 V** | UPDI is single-wire referenced to the target's `VDD`, and this board runs at **5 V** — the entire reason for the AVR-DB choice. A 3.3 V-only adapter is marginal into a 5 V target |

⚠️ **No hardware debugger.** `STAGE1-BENCH-PROTOTYPE.md` suggested a Curiosity
Nano or Xplained specifically to get one "for free"; a serial adapter flashes but
does not single-step. ⭐ **That is workable here, and the firmware was built for
it** — the three `DBG` pins on `J2` report state a DMM can read:

```
  DBG1  PF4  TQFP 48   ADC has EVER faulted — LATCHED, so a transient stays visible
  DBG2  PF3  TQFP 47   the scale self-check has PASSED
  DBG0  PF5  TQFP 49   spare
```

⭐⭐ `DBG2` is the one to watch at first power-on: it goes high only when
`pmgr_adc_recheck_scale()` accepts `ref5V`, which is exactly the 113-vs-126
question. A DMM on `J2` answers it without any debugger at all.

⚠️ **Fuses matter here, not later.** 2.54 measured the original taking control of
`HICHG` within **~10 ms** of rail-up, and an AVR128DB64 is **high-Z until firmware
runs** — with default fuses that exceeds 10 ms. ⭐ 2.55 found the board already
biases every net safe, so this is survivable — but set start-up time deliberately
and record what you chose.

---

## Step 4 — Solder the interposer to the footprint

⛔ **Gate 0 must be resolved before this step.** This is the irreversible one.

- ⭐ Castellation pads extend **0.325 mm outside the board outline**
  (`interposer-v2/README.md`) — they are meant to be soldered from the side, like
  a castellated module, not reflowed underneath
- ⛔ **Which machine: `BRING-UP.md` runs EVERY stage on Portable #1** (reversed
  2026-09-07 — it previously sent stage 1 to the PowerBook 100). ⚠️ **So this
  step is preceded by desoldering `#1`'s PMGR** — the one Apple part in the
  project that cannot be replaced. Store it; do not fit it anywhere until the
  replacement is validated
- ⭐ The PowerBook 100 is fitted **last**, once the board is proven. It has never
  been powered and has no working sibling to compare against, so a failure there
  would not point at anything
- ⛔⛔ **On the PowerBook 100, do the `MEASUREMENTS.md` 2.67 TEST-POINT MAPPING
  before this step.** Sony's `TP001`–`TP072` are unmarked on the board and no
  locations drawing exists, so the only way to find them is to buzz from `U22`'s
  bare pads — which this step covers over permanently. ⭐ Map **57 `PMGR_PWM`**
  first, then `37`, `42`, `33`. Photograph what you find. ⚠️ Skipping it leaves
  that machine with no probe access you can locate, for good

### Check before stacking

- ⭐⭐ **Continuity, interposer castellation → the machine's own nets**, using the
  table this project measured: castellation **1** = `VIAD4`; **17, 23, 53** = GND;
  **45, 55, 56** = +5 V always-on (2.64)
- ⛔ **Adjacent castellations not bridged.** 0.5 mm pitch, 60 of them

---

## Step 5 — Stack

```
  MCU board      DF12NC-50DS socket, facing DOWN   (B.Cu)
        |
  interposer     DF12NC-50DP header, facing UP     (F.Cu)
        |
  logic board
```

⭐ The mate is **pin 1 to pin 1** and the boards' coordinate frames align
directly — confirmed from the board files, not assumed. The MCU board's long
face lands over **side D** (pins 46–60).

⚠️ **The debug breakout goes between interposer and MCU board**, and adds **3 mm**.
⛔ **Not on the PowerBook 100** — its CPU daughterboard leaves 9–10 mm and the
stack with the breakout is ~12 mm. See `breakout/README.md`.

---

## Step 6 — First power-on

⛔ **Go to `BRING-UP.md` stage 1.** Do not skip to booting.

⚠️ **≤ 6.5 V on the bench** while any original M50753 is in the machine
(`BENCH-CAPTURE.md`). ⭐ Not a constraint for a board carrying only the
replacement, but the habit is correct.

⭐⭐ **The first number worth having is `ref5V`** — `MEASUREMENTS.md` 2.64/2.65
predicts **~126** if the M50753's input current was the load, or **~113** if the
leakage is on the board. ⛔ Measure it against a DMM-derived expectation, never
against what the AVR itself reports.

---

## What this document does not yet cover

- ⚠️ **Whether you have two-sided stencil and reflow capability.** 5 bottom-side
  parts including a 0.5 mm-pitch 50-position connector is not hand-solder work.
- ✅ ~~Which UPDI programmer~~ — **settled**: XGecu `T48` (UPDI-capable) plus a
  CH340E serial adapter. ⚠️ Neither gives a hardware debugger; the `DBG` pins on
  `J2` are the substitute, and the firmware was written that way.
- ✅ ~~Rework plan if `U1` is misplaced~~ — **written**, in Step 1. Short version:
  tack two opposite corners, stop and inspect, then commit; removal is bad enough
  on this board that prevention is the plan.

---

## ⭐ Building a SECOND MCU board for the PowerBook 100 — J2 must not be a vertical header

**Why this is its own note (2026-09-16).** The PowerBook 100 is the last machine to receive a board, and the operator plans a
**second MCU board** for it so Portable #1 keeps the one that is validated. The stack fits there - interposer + `DF12` + MCU is
**6.2 mm** against **10.4 mm** available (`design/TWO-MACHINES.md`) - but that leaves **~4.2 mm** of headroom, and a standard
2.54 mm **vertical** `J2` is about **8.5 mm** tall (≈2.5 mm of body plus ≈6 mm of pin). ⛔ **On the PB100 the header, not the board,
is what would hit the CPU daughterboard.**

```
  option A  ⭐ NO HEADER: fine wires soldered straight to the J2 pads, dressed sideways   lowest profile, the operator's choice
            keep them long enough to reach a Tigard outside the case; strain-relieve at the board, they are the only UPDI and
            DBG0/1/2 access the PB100 has (no J21, no breakout there - TWO-MACHINES)
  option B  right-angle 2x3 header: ~3 mm above the board, length goes sideways          check the exit direction has room for the
            header AND a plug boot (another 10-15 mm), against the measured per-side clearances
  option C  vertical header                                                              ⛔ do not fit on the PB100 board
```

⚠️ **Whichever is used, the tails underneath must be flush.** `J2` is through-hole on a board with a **3.0 mm gap beneath it**
(the `DF12` stack); protruding tails foul it and can hold the board a fraction proud while it still feels seated - the unresolved
item from 2.83. Solder from the top where possible, trim flush, and check the underside before stacking.

⭐ **Leave Portable #1's board alone.** Its `J2` is fitted and working; changing it means desoldering six pins from the only
validated assembly. Build the difference into the second board instead.

⚠️ **Measure, do not trust this note's numbers:** the height of the actual part, and the free height above the MCU board once the
stack is in the PB100 with its CPU board in place.

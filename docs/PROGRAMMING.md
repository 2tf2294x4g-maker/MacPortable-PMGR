# Programming the AVR

How to build the firmware, connect a programmer, and flash the AVR128DB64 on the MCU board — on the
bench, or later with the stack fitted in the Mac.

## What you need

- **A USB-serial adapter used as a "SerialUPDI" programmer.** UPDI is a one-wire serial protocol, so
  any USB-UART adapter works if its **TX and RX are joined through a resistor**. Use a **5 V-capable**
  adapter: the board runs at 5 V, and a 3.3 V-only adapter is marginal.
- **A resistor of 470 Ω to 1 kΩ** (1 kΩ is a good default). ⚠️ The widely copied 4.7 kΩ is too
  high. ⚠️ Adapters such as the Tigard have **no** built-in UPDI resistor — fit one.
- **`pymcuprog`** (Microchip's programming tool): `pip3 install pymcuprog`
- **avr-gcc 14** to build the firmware — see [`../firmware/README.md`](../firmware/README.md).

## The programming header, J2

`J2` is the 2 × 3 header on the MCU board:

```
  J2-1  UPDI      J2-2  +5 V      J2-3  DBG0
  J2-4  DBG1      J2-5  DBG2      J2-6  GND
```

⛔ **`J2-2` is the board's +5 V, and it connects through the stack to the Mac's +5 V always-on
rail.** So:

| situation | power the board from | connect |
|---|---|---|
| **MCU board on its own**, on the bench | the programmer | UPDI (J2-1), +5 V (J2-2), GND (J2-6) |
| **Stack fitted in the Mac** | **the Mac** (bench supply or battery at the battery input) | **UPDI (J2-1) and GND (J2-6) only — leave J2-2 off** |

Never let the programmer try to power the Mac's always-on rail through `J2`.

## Wiring the adapter

```
  adapter TX ──[ 1 kΩ ]──┬── J2-1  UPDI
  adapter RX ────────────┘
  adapter GND ───────────── J2-6  GND
  adapter 5 V ───────────── J2-2  +5 V      (bench only — see the table above)
```

⚠️ **On a dual-port adapter (FT2232-based, e.g. the Tigard), use the FIRST port.** It appears as
two serial ports; only the first (channel A) answers. The second reports "UPDI initialisation failed".

## Step by step

**1. Build.**
```bash
make -C firmware
```

**2. Read the device ID first — before writing anything.**
```bash
pymcuprog ping -t uart -u /dev/tty.usbserial-XXXX -d avr128db64
```
It must answer **`1E970B`**. That one reply proves the chip is alive, is the right part, and that
UPDI, power and ground are all connected. If it fails here, nothing has been written yet.

**3. Fuses — once per board, before the first flash.**
```bash
make -C firmware fuses PORT=/dev/tty.usbserial-XXXX
```
See [`../firmware/FUSES.md`](../firmware/FUSES.md) for what is set and why.

**4. Flash.** Use the flash tool, so the board's calibration record is written at the same time:
```bash
export PMGR_UPDI_PORT=/dev/tty.usbserial-XXXX
python3 tools/pmgr_flash_with_pram.py firmware/pmgr.hex <md5-prefix> - calib.args
```
- `<md5-prefix>` is the first 8 characters of `md5 -q firmware/pmgr.hex` (on Linux: `md5sum`). The
  tool **refuses** a hex whose checksum doesn't match, so you can't flash the wrong file.
- `-` means "write no PRAM image"; the Mac initialises PRAM itself.
- `calib.args` is the board's calibration record — see [`BUILD.md`](BUILD.md) §7. On a brand-new
  board without one yet, leave the last argument off; the firmware then simply won't fast-charge.
- Look for **`write: OK | verify: matches`** and **`calibration record at 464 read back identical:
  True`**.

`make -C firmware flash PORT=…` also works, but it **does not** write the calibration record.
⛔ **Every flash erases the EEPROM**, so flashing that way leaves the board without its record.

**5. Unplug the programmer and do a real power cycle.** Disconnect the supply or battery, wait a
few seconds, reconnect. ⛔ Do this after **any** programmer operation — even step 2's ID read. Without
it, a machine that is shut down may not wake from the keyboard (seen 7 times out of 7; cause not
established). A programmer left plugged in back-feeds the board, so switching off alone is not
enough.

## Resetting PRAM

**Pulling the battery does not reset PRAM on this board** — PRAM is kept in the AVR's EEPROM and
survives power removal. Try `Cmd-Opt-P-R` at startup first. If PRAM holds settings that stop the
Mac booting at all, **re-flash through the flash tool** (step 4), with the same `calib.args`:

- every flash **erases the whole EEPROM**, which clears PRAM, and the Mac writes fresh defaults on
  the next start;
- the tool then **writes the calibration record back**.

⛔ **Don't erase the EEPROM any other way.** The calibration record lives there too (bytes 464–479),
and losing it silently disables fast charge. Then unplug the programmer and power-cycle, as always.

## Troubleshooting

| symptom | likely cause |
|---|---|
| `UPDI initialisation failed` | **The board isn't powered.** A connected programmer back-feeds only about 1.65 V, below the AVR's 2.85 V brown-out, so it can't be programmed that way. Power it properly. |
| `UPDI initialisation failed`, board powered | Wrong port on a dual-port adapter (use the first); TX/RX not joined through the resistor; UPDI not reaching `J2-1`. |
| Ping answers, but not `1E970B` | Wrong part fitted. |
| The flash tool refuses the file | The MD5 prefix doesn't match the hex — rebuild, or check you're flashing the file you meant. |
| Boots normally but never fast-charges | No valid calibration record — flash again through the tool with `calib.args`. |
| Machine won't wake from the keyboard after flashing | No real power cycle yet — unplug the programmer and disconnect power. |
| Bad settings survive pulling the battery | Expected — PRAM is kept in EEPROM. See "Resetting PRAM" above. |

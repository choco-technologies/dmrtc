# Verification

Native tests exercise calendar validation, unset state, set/read, rejected invalid
writes, year 2000, singleton ownership, configuration constraints, failed writes,
calendar persistence on context recreation, and conflicting source selection.

Hardware tests additionally exercise `/dev/rtc` read/write ioctl permissions,
open-handle protection against destruction, unknown ioctls, leap-day and year
rollover. Reset retention is checked separately using OpenOCD and the console.
The x86_64 mock is not evidence of STM32 register behavior.

## Results — 2026-10-15

- Native `ctest`: 8/8 steps passed, including concurrent calendar access.
- STM32F4 and STM32F7 builds passed (F4 compiled, not tested on a board).
- STM32F746G-DISCO: final suite 8/8 passed three consecutive executions.
- Firmware programmed by ST-Link/OpenOCD with `Verified OK`.
- LSE selected: `running=1 calendar_set=1 clock=lse nominal_hz=32768`.
- Year 2000 reset test: `2000-02-29T23:59:45` before system reset,
  `2000-03-01T00:00:30` after reboot; state retained and calendar advanced.
- Final firmware: `2026-10-15T20:49:43` → `20:49:46` while powered,
  then `20:50:23` after system reset, with `calendar_set=1` preserved.
- Calendar was set to PC UTC after the tests, without claiming authenticated UTC.
- Independent battery/power-loss retention and long-term oscillator accuracy were
  not measured. VBAT is tied to VDD on this board.

[Captured console evidence](validation/stm32f746g-disco.txt).

Final STM32F7 module sizes (uncompressed DMF): core 5808 bytes, port 4229 bytes.
The exact tested core/port compressed packages were compared against those
embedded in the boot image:


- `dmrtc.dmfc` SHA-256: `9d0cccc0609c9ebf08cafaf4c3dc293012e61f2f9825eae828d03a345340ec34`

- `dmrtc_port.dmfc` SHA-256: `c5be69899578df59c588cc1b3c0806d543bb741fc3352c020188b22877f5b8d7`

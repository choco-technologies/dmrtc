# Verification

Native tests exercise calendar validation, unset state, set/read, rejected invalid
writes, year 2000, singleton ownership, configuration constraints, failed writes,
calendar persistence on context recreation, and conflicting source selection.

Hardware tests additionally exercise `/dev/rtc` read/write ioctl permissions,
open-handle protection against destruction, unknown ioctls, leap-day and year
rollover. Reset retention is checked separately using OpenOCD and the console.
The x86_64 mock is not evidence of STM32 register behavior.

## Initial implementation — 2026-10-15

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

Initial STM32F7 module sizes (uncompressed DMF): core 5808 bytes, port 4229 bytes.
The exact tested core/port compressed packages were compared against those
embedded in the boot image:


- `dmrtc.dmfc` SHA-256: `9d0cccc0609c9ebf08cafaf4c3dc293012e61f2f9825eae828d03a345340ec34`

- `dmrtc_port.dmfc` SHA-256: `c5be69899578df59c588cc1b3c0806d543bb741fc3352c020188b22877f5b8d7`

## IRQ waits and scheduled boot — 2026-10-16

The STM32 port no longer calls dmclk_port_delay_us. LSE/LSI startup waits on
an ISR-safe semaphore posted by the shared RCC IRQ handler. DBP/INITF waits
sleep between samples with a separate 20 ms deadline. Pre-scheduler and ISR
calls are rejected rather than falling back to busy waits.

The original boot mounted /dev before starting RTOS. The accompanying boot PR
moves filesystem/device initialization and application startup into a kernel
boot task after the scheduler starts; the worker stack is reclaimed on return.

- Native calendar suite: 8/8 steps passed.
- Actual STM32 wait source tested with MMIO/OS doubles: ready fast path,
  LSE/LSI IRQ wake, unrelated IRQ rejection, event before blocking, timeout,
  late IRQ after timeout, preservation of other RCC enables/NVIC priority,
  conflicting owner/unsafe priority, allocation failure, sleeping register
  timeout, and rejecting pre-scheduler/ISR contexts. CPU time is measured to
  ensure blocked waits do not consume their wall-time budget on the CPU.
- STM32F4/F7 builds passed.
- Board RTC tests: 8/8; RNG regression: 6/6; /dev devices and DHCP address present.
- Actual LSE ready IRQ confirmed with LSE initially disabled (RCC BDCR=0x8100),
  by capturing the real thread's semaphore-wake message in the ST-Link log ring.
  A boot with an already-running oscillator does not exercise that path.

[IRQ validation evidence](validation/irq-waits-2026-10-16.txt).

Current STM32F7 package hashes:

- `dmrtc.dmfc`: `9d0cccc0609c9ebf08cafaf4c3dc293012e61f2f9825eae828d03a345340ec34`

- `dmrtc_port.dmfc`: `6f8f416592d6a7a8eccfe794d909536824fc5ff02bad47dd434a9ae84a26390c`

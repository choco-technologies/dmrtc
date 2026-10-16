# dmrtc

Calendar RTC driver for DMOD, with separate `dmrtc` and `dmrtc_port` modules.
STM32F4 and STM32F7 share their implementation in `src/port/stm32_common/`.
The `x86_64` port is a deterministic test double and is excluded from releases.

## Capabilities

- Civil calendar for 2000–2099, 24-hour time, weekday computed on write.
- LSE/LSI startup signaled by RCC IRQ; waiting task blocks on a semaphore.
- Persistent explicit-calendar-set marker in two reserved backup registers.
- Serialized operations and one owner per hardware RTC.
- `/dev/rtc` ioctl interface, and the `rtc` console application.
- Preserve running calendar on reset, context destruction and module unload.

The driver has no timezone, network synchronization or UTC trust policy.
`calendar_set` means a valid calendar was explicitly written; it does not mean
that its source is authentic or that its value remains accurate. Those policies
belong in `dmutc`. Monotonic timeouts use the platform clock, not this calendar.

## Board configuration

```ini
[rtc]
driver_name=dmrtc
driver_order=0
instance=0
clock=lse
clock_hz=32768
timeout_ms=5000
backup_register=18
```

Choose one configuration from `configs/board/` or `configs/mcu/`. MCU presets
assume a fitted external LSE crystal. `backup_register=18` reserves BKP18R and
BKP19R exclusively; other drivers and the bootloader must not use them.
LSI must be selected explicitly, with its nominal `clock_hz` supplied in the INI.
There is no automatic fallback. The configured frequency must be divisible by
128; asynchronous prescaler is 127, synchronous prescaler is `clock_hz/128-1`.
LSI is less accurate and is not an independently battery-backed time source.

`timeout_ms` limits oscillator startup only (default 5000 ms). RCC's LSE/LSI
ready interrupt posts an ISR-safe semaphore; only the calling thread blocks.
DBP and INITF have no corresponding hardware interrupts: these short register
handshakes have a separate 20 ms limit and sleep between status samples.
All blocking calls require a running scheduler and task context. There is no
busy-wait fallback. The port no longer depends on `dmclk_port` delays.

The RCC IRQ is shared: this port owns only its selected LSE/LSI ready source,
preserves other interrupt enables, and disables its source before freeing the
wait semaphore. An already-owned source or an active IRQ with a priority unsafe
for DMOSI calls is rejected. No unrelated RCC flag is acknowledged.

A previously selected different RTC clock, different prescalers or 12-hour
format causes acquisition to fail. The driver never resets the backup domain.
Changing those settings requires a deliberate board provisioning operation.
RTC alarms, wakeup timer and calibration are outside this API.
The port exclusively owns RTC configuration and backup-domain access during
operations; do not access these registers concurrently from other code.
The boot must initialize device drivers after starting the RTOS scheduler.

STM32F746G-DISCO has an LSE crystal, but its VBAT pin is connected to VDD.
The board preserves RTC across a system reset while powered; it does not retain
calendar through complete power removal without hardware modification.

## API and console

`dmrtc_create(&config)` acquires hardware; NULL selects `DMRTC_CONFIG_DEFAULT`.
A board-created context can be borrowed with `dmrtc_default()`. Its owner alone
may destroy it. Creation/destruction must be externally serialized with users;
operations on a live context are protected by a mutex. Destroy returns `-EBUSY`
while device files remain open. Release leaves oscillator and calendar running.

`dmrtc_get`, `dmrtc_set`, `dmrtc_status` and `dmrtc_invalidate` take that context.
`get` returns `-ENODATA` until explicitly set, with cleared output on failure.
Dates are validated before any hardware write. A write clears the persistent
marker before modifying the calendar, and commits it after initialization exits.
Year 2000 works even though STM32's INITS flag is zero for that year.
The hardware's two-digit year wraps after 2099; applications must provision a
new epoch or replace this port before that boundary. Leap seconds are rejected.

```text
rtc status
rtc get
rtc set 2026-10-15T12:34:56
rtc invalidate
```

`rtc set` accepts civil time, with no implicit timezone conversion.
`/dev/rtc` supports `DMRTC_IOCTL_GET`, `DMRTC_IOCTL_SET`, `DMRTC_IOCTL_STATUS`
and `DMRTC_IOCTL_INVALIDATE` from `dmrtc.h`. GET/STATUS require a readable file;
SET/INVALIDATE require a writable file. There is no byte-stream encoding.

## Build and verify

Initialized with DMOD's `new-module.sh --type library --port --port-arch stm32f7
--github`. CMake builds every module and its release package; no wrapper Makefile.

```sh
cmake -S . -B build/host -DDMOD_CPU_FAMILY=x86_64
cmake --build build/host --parallel
ctest --test-dir build/host --output-on-failure
cmake -S . -B build/stm32f7 -DDMOD_CPU_FAMILY=stm32f7
cmake --build build/stm32f7 --parallel
```

Packages are in `build/<family>/packages/`. STM32F4 uses `-DDMOD_CPU_FAMILY=stm32f4`.
Install `dmrtc driver=board/stm32f746g-disco/rtc.ini`, `rtc` and `test_dmrtc`
in the board image. Run `test_dmrtc` from its console. Hardware tests briefly
change the calendar and restore its previous value or previous unset state.
Details and hardware evidence: [testing](docs/testing.md).

## Hardware references

- [STM32F74/F75 reference manual RM0385](https://www.st.com/resource/en/reference_manual/dm00124865.pdf)
- [STM32F4 reference manual RM0090](https://www.st.com/resource/en/reference_manual/dm00031020.pdf)
- [STM32 RTC application note AN4759](https://www.st.com/resource/en/application_note/DM00226326-.pdf)
- [STM32F746G-DISCO user manual UM1907](https://www.st.com/resource/en/user_manual/dm00190424.pdf)

MIT license. Author: Patryk Kubiak.

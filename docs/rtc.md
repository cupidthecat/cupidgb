# Cartridge clocks

Timer-equipped MBC3, HuC3 and TPP1 cartridges retain a clock independently of
their RAM banking. CupidGB offers two clock policies in Settings under
**Real Time Clock**. The setting applies to the current cartridge and future
loads, and persists between application sessions.

| Policy | Clock source | Effect of pausing or changing playback speed |
| --- | --- | --- |
| Sync to System Clock | Elapsed host Unix time, sampled during emulation | The next scheduled update catches up with elapsed host time; Turbo does not multiply that time |
| Accurate | Delivered emulated hardware clocks at the selected model frequency | Pausing suspends updates; Turbo and slow motion change their cadence. HuC3 has the minute-counter behavior described below. |

Sync to System Clock is the default. Clock stop controls still belong to the
cartridge. Changing policy keeps its counter and latch values, clears fractional
progress, and establishes a new baseline without advancing the counters.
Selecting the already active policy changes nothing. Canceling Settings or
failing another field's validation leaves the current policy unchanged.

Battery files and native states serve different purposes. Battery imports use
their controller's timestamp rules, including validation against host time in
either policy. Native states restore the saved clock history and keep the
receiver's current host policy. A state load does not call the host clock.
See [battery saves](battery-saves.md) and [native states](save-states.md).

## Core integration

`cupid/gb/rtc.h` supplies the public clock API and its own C++ linkage guard.
Use a zero-initialized instance and select the model before configuring a
custom period. Configure policy and the optional time source before loading
the cartridge and its battery data:

```c
cupid_gb_set_model(gb, CUPID_GB_MODEL_CGB);
cupid_gb_set_rtc_time_source(gb, observe_unix_seconds, clock_context);
if (!cupid_gb_set_rtc_mode(gb, CUPID_GB_RTC_ACCURATE)) {
    return false;
}
if (!cupid_gb_load_rom(gb, cartridge, cartridge_size)) {
    return false;
}
/* Decode optional battery data before beginning guest execution. */
```

The provider returns signed Unix seconds and must not mutate or reenter the
core. Negative observations become zero. Registering a provider does not poll
it or alter clock history. Passing a null provider restores system time and
clears its context. Registration and policy survive initialization, cartridge
loads, firmware entry and native-state loads; cleanup clears them. They are
host-owned fields outside serialized execution state.

`cupid_gb_set_rtc_mode` rejects unknown modes and null instances without
mutation. The getter returns host synchronization for a null instance. A real
mode change rebases an applicable cartridge clock through the selected provider;
a same-mode call is an exact no-op.

`cupid_gb_set_rtc_multiplier` changes the duration of an Accurate second. A value
of `0.5` doubles its rate; `2.0` halves it. The Qt interface does not expose this
core-only control. The conversion uses twice the current model frequency and
truncates to an unsigned 32-bit period. One, zero, and values truncating to zero
select the model's default period. Other finite nonnegative values are accepted
only when the scaled period is below 2^32. Invalid values leave the instance
unchanged. A period change preserves mode, counters, baseline and accumulated
phase; it does not poll time or silently consume existing clock debt.

## Counter and scheduling behavior

The scheduler uses half-base-clock units, including CGB double speed and the
SGB NTSC frequency. Each logical CPU or bus interval consumes at most one due
clock update. Dividing that interval into pixel or DMA operations must not
create extra updates. Pending debt and unsigned phase wrap remain observable
after a shortened period or a restored state.

MBC3 latch writes copy the live registers on every write without polling host
time. Writing its seconds register clears fractional progress. In host mode,
changing its stop bit catches up the prior running state and rebases the
timestamp; Accurate mode retains the fraction across that stop transition.
Out-of-range writable digits retain their storage-width wrap and delayed carry
behavior instead of being normalized into a conventional date.

TPP1 keeps motor strength separate from its clock status. Motor commands leave
the stored clock status intact; reads combine the two fields. Its latch and
commit operations retain the shared fifth clock byte as well as the four
controller bytes. A stopped TPP1 clock can still consume scheduling intervals.
HuC3 reads, writes and alarm queries do not introduce extra host-time polls.

HuC3 Accurate mode retains a specific counter quirk. Each scheduled update
proposes the stored baseline plus one second, but the minute counter commits a
new baseline only after crossing a minute boundary. A stored second remainder
from zero through 58 therefore leaves both values unchanged. A remainder of 59
advances one whole minute per scheduled update and retains that remainder.
Sync to System Clock uses elapsed host time for ordinary HuC3 minute progress.
The current Accurate behavior has comparison coverage; its physical hardware
qualification remains open.

Large finite timestamp gaps use bounded arithmetic. They preserve the order of
whole-day and residual-second updates, including raw counter encodings and
unsigned timestamp wrap. A narrow class of raw timestamp histories has no
reachable stopping value in that unsigned day sequence. CupidGB consumes the
scheduled interval but leaves those complete clock records unchanged. This
bounded exceptional policy is explicit; it does not normalize or reject an
otherwise accepted native MBC3 timestamp.

## HuC3 desktop alarms

Pausing, closing, or replacing a running HuC3 cartridge schedules its future
alarm. CupidGB takes the deadline from the cartridge clock and delivers it
against wall time in either RTC mode. Resuming or reopening the cartridge
cancels its pending alarm and withdraws any alert still displayed. Each
cartridge path has its own alarm. Reloading a paused cartridge keeps the
existing deadline.

Click **Open Cartridge** to focus the cartridge's existing window or reopen
the file. A deployed Windows build uses Task Scheduler and the notification
area. Linux uses a systemd user timer and the session notification service.
When the scheduler is unavailable, or a Windows build relies on the launch
shell for its Qt runtime, a separate CupidGB process waits for the deadline.
An alarm dialog is available when the desktop cannot display a notification
with an open action.

Linux alarms depend on the running user session. Delivery after signing out
or rebooting remains unverified.

Generated HuC3 cartridges cover repeated pause, resume, state restoration,
paused reload, hot swap, replacement, and close. A failed replacement or
close leaves the current session's alarm alone. Separate Windows and Linux
probes exercised native timers after the scheduling process exited, with an
offscreen Qt dialog on Windows and an isolated notification service on Linux.
Both reopened the exact cartridge path, including spaces and Unicode
characters, or focused an existing window without opening a second one.
Linux probes also checked withdrawal and ignored clicks from canceled alarms
on both the native timer and detached helper paths.

The [parity checklist](parity.md) and recorded validation describe measured
model, bus, save-continuation and commercial coverage.

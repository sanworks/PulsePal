"""Test a connected Pulse Pal: protocol integrity, robustness and transfer speed under load.

Run it after changing the Pulse Pal firmware or pulsepal/pulse_pal.py:

    python tests/pulsepal_hardware_test.py COM3
    python tests/pulsepal_hardware_test.py COM3 --driver COM4
    python tests/pulsepal_hardware_test.py COM3 --quick

The device must run Pulse Pal firmware v22. Nothing needs to be connected to it,
but the outputs play pulses of up to +/-10 V, so disconnect anything that should
not receive them. With --driver, a second Pulse Pal, whose output channels 1 and
2 are wired to this one's trigger channels 1 and 2, sends TTL edges (0 V to 4 V:
the trigger inputs take 3 to 5 V) for the trigger and param sync tests; without
it, those tests are skipped.

No scope is needed: each test checks that the device stays in step with the PC,
whatever it is sent and however busy it is. After every test, the device must
answer the handshake, and its parameters, read back with op 93, must be the ones
the PC expects. The tests send commands while channels play, commands the device
must refuse, bytes that are no command, many commands in one write, and
reconnects; and they time USB round trips and custom train uploads with the PC's
CPU idle and fully loaded. Playback timing itself needs a scope (see
/Firmware/PulsePal/AGENTS.md).

The test saves and deletes HWTEST*.pps on the microSD card. It never formats the
card, writes the DAC calibration or uses the debugging ops (86, 87).
"""
import argparse
import os
import random
import statistics
import struct
import subprocess
import sys
import threading
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from pulsepal import PulsePalDevice, PulsePalError  # noqa: E402

OP = 213
# Ops a test may send with arbitrary data: those that format the card, write the EEPROM or set pins are left out
DESTRUCTIVE_OPS = {86, 87, 90, 96, 97, 99}


class Skipped(Exception):
    pass


# --- Raw protocol helpers -------------------------------------------------------------------

def raw_params(P):
    """The device's parameters (op 93), as the 178 reply bytes."""
    P.port.write(bytes([OP, 93]))
    reply = P.port.read(178)
    assert len(reply) == 178, f"op 93 returned {len(reply)} of 178 bytes"
    return reply


def handshake(P, timeout=1.0):
    old = P.port.timeout
    P.port.timeout = timeout
    try:
        P.port.write(bytes([OP, 72]))
        reply = P.port.read(5)
    finally:
        P.port.timeout = old
    return len(reply) == 5 and reply[0] == 75


def assert_in_step(P, expected_params=None, what=""):
    """The device answers, sends nothing unasked, and holds the parameters the class holds."""
    stray = P.port.read(P.port.in_waiting) if P.port.in_waiting else b""
    assert not stray, f"{what}: the device sent {len(stray)} bytes no command asked for: {stray[:16]!r}"
    assert handshake(P), (f"{what}: no handshake. If the screen shows COMM. FAILURE!, a command reached the device "
                          "only in part: click the joystick, and run the test again")
    params = raw_params(P)
    if expected_params is not None:
        assert params == expected_params, f"{what}: the device's parameters changed ({diff_params(params, expected_params)})"
    return params


def diff_params(a, b):
    names = []
    offsets = [("times", 0, 128), ("voltages", 128, 152), ("bytes", 152, 168), ("links", 168, 176), ("modes", 176, 178)]
    for name, start, stop in offsets:
        if a[start:stop] != b[start:stop]:
            names.append(name)
    return ", ".join(names) or "no difference"


def discard_until_quiet(P, quiet=0.15):
    old = P.port.timeout
    P.port.timeout = quiet
    try:
        while P.port.read(4096):
            pass
    finally:
        P.port.timeout = old


def program_playing(P):
    """Every channel plays a long train in continuous loop mode."""
    with P.batch():
        P.phase1_duration = [0.0002, 0.0005, 0.001, 0.0001]
        P.inter_pulse_interval = [0.0003, 0.0005, 0.002, 0.0001]
        P.phase1_voltage = [2, 3, -2, 1]
        P.pulse_train_duration = [0.5] * 4
        P.continuous_loop = [True] * 4
    P.trigger([1, 2, 3, 4])


class CpuLoad:
    """Busy processes, one per logical CPU (times `factor`), while in a with block."""

    def __init__(self, factor=2):
        self.n = (os.cpu_count() or 4) * factor
        self.processes = []

    def __enter__(self):
        self.processes = [subprocess.Popen([sys.executable, "-c", "while True: pass"]) for _ in range(self.n)]
        time.sleep(1.0)
        return self

    def __exit__(self, *exc):
        for p in self.processes:
            p.kill()
        for p in self.processes:
            p.wait()


def percentiles(times_s):
    t = sorted(x * 1e3 for x in times_s)
    return (f"median {statistics.median(t):.2f} ms, p99 {t[min(len(t) - 1, int(0.99 * len(t)))]:.2f} ms, "
            f"max {t[-1]:.2f} ms")


# --- Tests ----------------------------------------------------------------------------------

def test_connection(P, D, report):
    assert P.info.firmware_version == 22, P.info.firmware_version
    assert P.info.hardware_version in (2, 3)
    assert 0 <= P.info.circuit_board_revision <= 31
    # The USB serial number: the Teensy's on Pulse Pal 3, the same one the computer lists the port with; none on Pulse Pal 2
    if P.info.hardware_version == 3:
        assert P.info.serial_number.isdigit(), P.info.serial_number
    else:
        assert P.info.serial_number == "", P.info.serial_number
    assert P.info.cycle_period_us == 50
    assert P.info.max_custom_pulses == (10000 if P.info.hardware_version == 3 else 5000)
    assert_in_step(P, what="after connecting")


def test_parameters_read_back_as_programmed(P, D, report):
    """Random programs, sent whole and one parameter at a time, read back with op 93 as the class holds them."""
    rng = random.Random(1)
    n_trains = P.info.n_custom_pulse_trains
    for k in range(40):
        params = P.export_params()
        for x in range(4):
            biphasic = rng.random() < 0.5
            params["is_biphasic"][x] = biphasic
            params["phase1_voltage"][x] = rng.choice([-10, -2.5, 0, 0.001, 5, 10])
            params["phase2_voltage"][x] = rng.choice([-10, -5, 3.3])
            params["resting_voltage"][x] = rng.choice([0, 0.5, -1.25])
            params["phase1_duration"][x] = rng.choice([0.0001, 0.00015, 0.001, 9999.9999])
            params["inter_phase_interval"][x] = rng.choice([0, 0.0001, 0.5])
            params["phase2_duration"][x] = rng.choice([0.0001, 0.002])
            params["inter_pulse_interval"][x] = rng.choice([0.0001, 0.01, 3600])
            params["burst_duration"][x] = rng.choice([0, 0.01])
            params["inter_burst_interval"][x] = rng.choice([0, 0.02])
            params["pulse_train_duration"][x] = rng.choice([0.0001, 1, 9999.9999])
            params["pulse_train_delay"][x] = rng.choice([0, 0.0005, 10])
            params["custom_train_id"][x] = rng.randrange(0, n_trains + 1)
            params["custom_train_target"][x] = "Bursts" if params["burst_duration"][x] > 0 and rng.random() < 0.3 else "Pulses"
            params["custom_train_loop"][x] = rng.random() < 0.5
            params["link_trigger_channel1"][x] = rng.random() < 0.5
            params["link_trigger_channel2"][x] = rng.random() < 0.5
        params["trigger_mode"] = [rng.choice(["Normal", "Toggle", "Gated"]) for _ in range(2)]
        P.import_params(params)
        expected = raw_params(P)
        P.sync_from_device()
        assert P.export_params() == params, f"program {k}: sync_from_device() read back other values"
        # The same program again, one assignment at a time (op 74 / op 91), must give the same bytes
        P.set_default_params()
        for name, values in params.items():
            if name in ("custom_train_target",):
                continue
            for channel, value in enumerate(values, start=1):
                try:
                    getattr(P, name)[channel] = value
                except PulsePalError:
                    pass  # An order that passes through an invalid combination (a later assignment completes it)
        P.custom_train_target = params["custom_train_target"]
        assert_in_step(P, expected, f"program {k} sent one parameter at a time")


def test_refused_values_change_nothing(P, D, report):
    """Values the device must refuse (sent raw: the class refuses them itself) leave everything as it was."""
    P.set_default_params()
    expected = raw_params(P)
    refusals = [
        bytes([OP, 74, 4, 1]) + struct.pack("<I", 0),     # Phase 1 of 0 cycles: refused, set to 1 cycle
        bytes([OP, 74, 2, 5]) + struct.pack("<H", 40000),  # Channel 5
        bytes([OP, 74, 128, 3, 1]),                       # Trigger channel 3
        bytes([OP, 74, 14, 1, P.info.n_custom_pulse_trains + 1]),  # Unknown custom train
        bytes([OP, 74, 128, 1, 4]),                       # Trigger mode 4
        bytes([OP, 79, 0]) + struct.pack("<H", 40000),    # Channel 0
        bytes([OP, 82, 1, 2]),                            # Continuous loop state 2
        bytes([OP, 82, 9, 1]),                            # Channel 9
        bytes([OP, 95, P.info.n_custom_pulse_trains]) + struct.pack("<I", 2) + bytes(12),  # Unknown train
        bytes([OP, 95, 0]) + struct.pack("<I", 0),        # Empty train
        bytes([OP, 95, 0]) + struct.pack("<I", P.info.max_custom_pulses + 1) + bytes(6 * (P.info.max_custom_pulses + 1)),
    ]
    for i, command in enumerate(refusals):
        P.port.write(command)
        reply = P.port.read(1)
        assert reply == b"\x00", f"refusal {i} ({command[:4].hex()}): replied {reply!r}, expected 0"
        if i == 0:
            # The device sets the refused phase to 1 cycle: put the program back
            P.sync_to_device()
        assert_in_step(P, expected, f"after refusal {i}")
    # An unknown parameter code: its length is unknown, so the device discards until the port is quiet for 100 ms
    P.port.write(bytes([OP, 74, 99, 1, 1, 2, 3, 4]))
    assert P.port.read(1) == b"\x00"
    P.port.write(bytes([OP, 91, 0]) + bytes(16))
    assert P.port.read(1) == b"\x00"
    assert_in_step(P, expected, "after unknown parameter codes")
    # Op 90 with an unknown operation, and an empty name
    P.port.write(bytes([OP, 90, 4, 5]) + b"x.pps")
    assert P.port.read(1) == b"\x00"
    P.port.write(bytes([OP, 90, 2, 0]))
    assert P.port.read(1) == b"\x00"
    # Op 97 needs its key: with any other 4 bytes it does nothing and sends nothing
    assert_in_step(P, expected, "after op 90 refusals")


def test_bytes_that_are_no_command_are_ignored(P, D, report):
    """Bytes outside a command (anything but 213 where an op code is expected), and unknown op codes."""
    P.set_default_params()
    expected = raw_params(P)
    rng = random.Random(2)
    noise = bytes(b for b in (rng.randrange(256) for _ in range(20000)) if b != OP)
    P.port.write(noise)
    assert_in_step(P, expected, "after 20000 bytes of noise")
    unknown = [op for op in range(256) if op not in DESTRUCTIVE_OPS and op not in range(72, 103)]  # Ops 72-102 exist
    for op in unknown:
        P.port.write(bytes([OP, op]))
    assert_in_step(P, expected, "after every unknown op code")


def test_commands_while_playing_keep_the_device_in_step(P, D, report):
    """Every kind of command, sent fast while all four channels play."""
    P.set_default_params()
    program_playing(P)
    expected = raw_params(P)
    n = P.info.max_custom_pulses
    times = [0.0002 * i for i in range(n)]
    volts = [((i % 11) - 5) / 2 for i in range(n)]
    start = time.perf_counter()
    for k in range(20):
        P.sync_to_device()                        # Op 92, the same program
        raw_params(P)                             # Op 93
        P.send_custom_pulse_train(4 if P.info.n_custom_pulse_trains == 4 else 2, times, volts)  # A train nobody plays
        P.port.write(bytes([OP, 78, 12]) + b"STRESS TEST " + bytes([OP, 72]))  # A screen message, then a handshake
        assert P.port.read(5)[:1] == b"K"
        P.port.write(bytes([OP, 89]) + b"HWTEST")  # Client name
        P.trigger([1, 2])                         # Ignored: the channels play
        if P.info.hardware_version == 2:
            # Pulse Pal 2 does not use its microSD card while a channel plays (op 90 replies 2): it shares the DAC's bus
            for call in (P.save_program_to_microsd, P.load_program_from_microsd):
                try:
                    call("HWTEST1.pps")
                except PulsePalError as error:
                    assert "while a channel is playing" in str(error), error
                else:
                    raise AssertionError("Pulse Pal 2 used its microSD card while its channels played")
        else:
            P.save_program_to_microsd("HWTEST1.pps")
            P.load_program_from_microsd("HWTEST1.pps")   # The same program: each channel keeps its continuous loop
    report(f"20 rounds of 9 commands while playing: {time.perf_counter() - start:.1f} s")
    assert_in_step(P, expected, "after commands while playing")
    P.stop()
    if P.info.hardware_version == 2:
        P.save_program_to_microsd("HWTEST1.pps")        # Allowed once the channels have stopped
    P.delete_program_from_microsd("HWTEST1.pps")
    P.continuous_loop = [False] * 4


def test_many_commands_in_one_write(P, D, report):
    """50 op 93s in one write, read a second later: the replies all arrive, in order. (With more, replies are lost:
    the device gives up sending after 120 ms of a full transmit buffer, which the operating system's driver empties
    only up to its own buffer's size when the program does not read. The classes read each reply at once.)"""
    P.set_default_params()
    expected = raw_params(P)
    n = 50
    P.port.write(bytes([OP, 93]) * n)
    time.sleep(1.0)
    data = b""
    deadline = time.monotonic() + 10
    while len(data) < 178 * n and time.monotonic() < deadline:
        data += P.port.read(178 * n - len(data))
    assert len(data) == 178 * n, f"received {len(data)} of {178 * n} bytes"
    assert all(data[i * 178:(i + 1) * 178] == expected for i in range(n)), "a reply differs"
    # For information: how many replies survive 200 requests left unread for a second
    P.port.write(bytes([OP, 93]) * 200)
    time.sleep(1.0)
    old = P.port.timeout
    P.port.timeout = 0.5
    received = len(P.port.read(178 * 200))
    P.port.timeout = old
    discard_until_quiet(P)
    report(f"200 op 93s left unread for 1 s: {received} of {178 * 200} reply bytes arrived")
    # Soft triggers and stops of all channels, alternating, in one write: the device ends stopped
    P.port.write((bytes([OP, 77, 15]) + bytes([OP, 98, 15])) * 500)
    assert_in_step(P, expected, "after 1000 triggers and stops in one write")


def test_reconnects(P, D, report):
    """New connections, while the device plays and with a reply left unread by the last session."""
    port = P.port.port
    P.close()
    times = []
    for k in range(30):
        start = time.perf_counter()
        Q = PulsePalDevice(port)
        times.append(time.perf_counter() - start)
        if k % 3 == 0:
            program_playing(Q)
        if k % 3 == 1:
            Q.port.write(bytes([OP, 93]))  # Its reply is left for the next connection to skip
            Q._close(send_disconnect=False)
        else:
            Q.close()
    report(f"30 connections: {percentiles(times)}")
    P.__init__(port)
    assert_in_step(P, what="after reconnects")


def test_transfer_under_cpu_load(P, D, report):
    """USB round trips and 10000-pulse uploads, with the CPU idle and with every core busy."""
    P.set_default_params()
    n = P.info.max_custom_pulses
    times = [0.0002 * i for i in range(n)]
    volts = [((i % 7) - 3) / 2 for i in range(n)]

    def measure():
        rtt = []
        for _ in range(500):
            start = time.perf_counter()
            P.port.write(bytes([OP, 94]))
            assert len(P.port.read(10)) == 10
            rtt.append(time.perf_counter() - start)
        upload = []
        for _ in range(20):
            start = time.perf_counter()
            P.send_custom_pulse_train(1, times, volts)
            upload.append(time.perf_counter() - start)
        program = []
        for _ in range(200):
            start = time.perf_counter()
            P.sync_to_device()
            program.append(time.perf_counter() - start)
        return rtt, upload, program

    idle = measure()
    with CpuLoad(2) as load:
        busy = measure()
    for label, (rtt, upload, program) in (("idle", idle), (f"{load.n} busy processes", busy)):
        report(f"CPU {label}: op 94 round trip {percentiles(rtt)}; op 92 {percentiles(program)}; "
               f"{n}-pulse upload {percentiles(upload)}")
    assert_in_step(P, what="after transfers under load")


def test_trigger_storm(P, D, report):
    """5 kHz TTL edges on both trigger channels, toggling all four output channels, while the PC sends commands."""
    if D is None:
        raise Skipped("needs --driver")
    with P.batch():
        P.phase1_duration = [0.0002] * 4
        P.inter_pulse_interval = [0.0003] * 4
        P.pulse_train_duration = [0.01] * 4
        P.link_trigger_channel1 = [True, True, False, False]
        P.link_trigger_channel2 = [False, True, True, True]
        P.trigger_mode = ["Toggle", "Gated"]
    expected = raw_params(P)
    D.phase1_voltage = [4, 4, 0, 0]
    D.phase1_duration = [0.0001] * 4
    D.inter_pulse_interval = [0.0001] * 4
    D.pulse_train_duration = [5.0] * 4
    D.trigger([1, 2])
    deadline = time.monotonic() + 5
    rounds = 0
    while time.monotonic() < deadline:
        raw_params(P)
        P.trigger([1, 3])
        P.stop([2])
        rounds += 1
    D.stop()
    report(f"5 s of 5 kHz trigger edges: {rounds} rounds of commands")
    assert_in_step(P, expected, "after the trigger storm")
    P.stop()


def test_microsd_while_triggers_arrive(P, D, report):
    """Programs saved and loaded for 5 s while TTL edges start short trains: every op 90 is answered, and the device
    stays in step. Pulse Pal 2's microSD card shares the SPI bus with the DAC, and a DAC write in the middle of a card
    transfer once hung it until it was unplugged. It now holds its timer interrupt off while it uses the card (so an
    edge then waits, or is missed), and refuses op 90 (reply 2) while a channel plays."""
    if D is None:
        raise Skipped("needs --driver")
    P.set_default_params()
    with P.batch():
        P.phase1_duration = [0.0002] * 4
        P.inter_pulse_interval = [0.0002] * 4
        P.pulse_train_duration = [0.002] * 4          # 2 ms trains, so the channels are idle most of the time
        P.link_trigger_channel1 = [True, True, False, False]
        P.link_trigger_channel2 = [False, False, True, True]
    expected = raw_params(P)
    P.save_program_to_microsd("HWTEST2.pps")
    D.phase1_voltage = [4, 4, 0, 0]
    D.phase1_duration = [0.0001] * 4
    D.inter_pulse_interval = [0.0049] * 4           # An edge every 5 ms on each trigger channel
    D.pulse_train_duration = [10.0] * 4
    D.trigger([1, 2])
    done = refused = 0
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        for call in (P.save_program_to_microsd, P.load_program_from_microsd):
            try:
                call("HWTEST2.pps")
                done += 1
            except PulsePalError as error:
                if P.info.hardware_version != 2 or "while a channel is playing" not in str(error):
                    raise
                refused += 1
    D.stop()
    report(f"5 s of microSD saves and loads with an edge every 5 ms: {done} done, {refused} refused while playing")
    assert done > 0, "no microSD operation was done"
    assert_in_step(P, expected, "after microSD operations among trigger edges")
    P.stop()
    P.delete_program_from_microsd("HWTEST2.pps")


def test_param_sync_edges(P, D, report):
    """In param sync mode, a stored set reaches the device's parameters (op 93) at the TTL edge, not before."""
    if D is None:
        raise Skipped("needs --driver")
    if P.info.hardware_version < 3:
        raise Skipped("param sync mode is on Pulse Pal 3 only")
    P.set_default_params()
    P.trigger_mode[1] = "Param Sync"
    before = raw_params(P)
    with P.batch():
        P.phase1_voltage = [1, 2, 3, 4]
        P.pulse_train_delay = [0.001] * 4
    assert raw_params(P) == before, "the set was applied before the edge"
    D.phase1_voltage = [4, 0, 0, 0]
    D.phase1_duration = [0.001] * 4
    D.pulse_train_duration = [0.001] * 4
    D.trigger(1)
    time.sleep(0.05)
    after = raw_params(P)
    P.trigger_mode[1] = "Normal"
    P.sync_from_device()
    assert after != before and P.phase1_voltage[1:] == [1, 2, 3, 4], "the edge did not load the set"


def test_close_leaves_the_device_ready(P, D, report):
    port = P.port.port
    P.close()
    Q = PulsePalDevice(port)
    assert_in_step(Q, what="after closing and connecting again")
    Q.close()
    P.__init__(port)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("port", help="Serial port of the Pulse Pal, e.g. COM3 or /dev/ttyACM0")
    parser.add_argument("--driver", metavar="PORT",
                        help="A Pulse Pal whose outputs 1 and 2 drive the trigger channels, for the trigger tests")
    parser.add_argument("--quick", action="store_true", help="Skip the tests under CPU load")
    arguments = parser.parse_args()
    D = PulsePalDevice(arguments.driver) if arguments.driver else None
    if D is not None:
        with D.batch():  # TTL only (the trigger inputs take 3 to 5 V), and only when told to
            D.phase1_voltage = [4, 4, 0, 0]
            D.link_trigger_channel1 = [False] * 4
    notes = []
    tests = [
        test_connection,
        test_parameters_read_back_as_programmed,
        test_refused_values_change_nothing,
        test_bytes_that_are_no_command_are_ignored,
        test_commands_while_playing_keep_the_device_in_step,
        test_many_commands_in_one_write,
        test_reconnects,
        test_trigger_storm,
        test_microsd_while_triggers_arrive,
        test_param_sync_edges,
    ]
    if not arguments.quick:
        tests.append(test_transfer_under_cpu_load)
    tests.append(test_close_leaves_the_device_ready)
    failures = skipped = 0
    P = PulsePalDevice(arguments.port)
    try:
        for test in tests:
            start = time.perf_counter()
            try:
                test(P, D, notes.append)
                result = "ok"
            except Skipped as reason:
                skipped += 1
                result = f"skipped ({reason})"
            except (AssertionError, PulsePalError) as error:
                failures += 1
                result = f"FAILED: {error}"
                try:
                    discard_until_quiet(P)
                    P.auto_sync = True
                    P.set_default_params()
                except Exception:  # noqa: BLE001 - the next test reports a device that no longer answers
                    pass
            print(f"{test.__name__:<60} {time.perf_counter() - start:6.1f} s  {result}")
            while notes:
                print("    " + notes.pop(0))
    finally:
        if not P._closed:
            P.close()
        if D is not None:
            D.close()
    print(f"\n{len(tests) - failures - skipped}/{len(tests) - skipped} tests passed"
          + (f", {skipped} skipped" if skipped else ""))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())

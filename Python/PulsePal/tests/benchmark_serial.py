"""Measure USB serial latency and throughput against a connected Pulse Pal.

Run it before and after a firmware change to see what the change did:

    python tests/benchmark_serial.py COM3
    python tests/benchmark_serial.py /dev/ttyACM0 --repeats 500

Nothing here starts playback, but it does program the device with the default
parameters (as connecting always does) and it overwrites custom trains.

Reported numbers are per operation: median, and the 95th percentile, which is
what an unlucky call costs.
"""
import argparse
import statistics
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from PulsePal import PulsePalDevice  # noqa: E402


def timed(function, repeats):
    """Run function repeats times, and return the elapsed times in milliseconds."""
    times = []
    for _ in range(repeats):
        start = time.perf_counter()
        function()
        times.append((time.perf_counter() - start) * 1000)
    return times


def report(label, times, payload_bytes=None):
    median = statistics.median(times)
    percentile95 = sorted(times)[min(len(times) - 1, int(0.95 * len(times)))]
    line = f"{label:<38} {median:8.3f} ms   {percentile95:8.3f} ms"
    if payload_bytes:
        line += f"   {payload_bytes / (median / 1000) / 1e6:7.2f} MB/s"
    print(line)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("port", help="Serial port of the Pulse Pal, e.g. COM3 or /dev/ttyACM0")
    parser.add_argument("--repeats", type=int, default=200, help="Repeats per measurement (default: 200)")
    arguments = parser.parse_args()

    with PulsePalDevice(arguments.port) as P:
        print(f"Hardware v{P.info.hardware_version}, firmware v{P.info.firmware_version}, "
              f"{P.info.n_custom_pulse_trains} custom trains, "
              f"{P.info.max_custom_pulses} pulses max")
        print(f"{arguments.repeats} repeats per measurement\n")
        print(f"{'operation':<38} {'median':>11}   {'95th pct':>11}")
        print("-" * 68)

        # Round trip with no work on the device: the floor for any command that replies.
        # The 10 reply bytes are read in one call, so this measures the round trip and not
        # the cost of several host reads.
        def hardware_info():
            P._write_serial((P._OP_MENU_BYTE, 94), "uint8")
            P._read_raw(10)
        report("op 94 hardware info (round trip)", timed(hardware_info, arguments.repeats))

        # One parameter on one channel (op 74), and on all four (op 91)
        report("set_output_param, 1 channel (op 74)",
               timed(lambda: P.set_output_param("phase1_voltage", 1, 5), arguments.repeats))
        report("set_output_param, 4 channels (op 91)",
               timed(lambda: P.set_output_param("phase1_voltage", [1, 2, 3, 4], 5), arguments.repeats))

        # Whole parameter set in each direction
        report("sync_to_device (op 92)", timed(P.sync_to_device, arguments.repeats), payload_bytes=178)
        report("sync_from_device (op 93)", timed(P.sync_from_device, arguments.repeats), payload_bytes=178)

        # Custom trains: the largest transfers the device accepts
        sizes = [n for n in (100, 1000, 10000) if n <= P.info.max_custom_pulses]
        for n_pulses in sizes:
            times = [i * 0.001 for i in range(n_pulses)]
            voltages = [5] * n_pulses
            repeats = max(5, arguments.repeats // (n_pulses // 50))
            report(f"custom train, {n_pulses} pulses (op 95)",
                   timed(lambda: P.send_custom_pulse_train(1, times, voltages), repeats),
                   payload_bytes=n_pulses * 6 + 7)

        # Settings file round trip through the microSD card
        report("sd_settings save (op 90)",
               timed(lambda: P.sd_settings("BENCH.pps", "save"), max(5, arguments.repeats // 20)))
        report("sd_settings load (op 90 + 93)",
               timed(lambda: P.sd_settings("BENCH.pps", "load"), max(5, arguments.repeats // 20)))
        P.sd_settings("BENCH.pps", "delete")
        print("\nDone. The device has been left with the default parameters.")


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""Compile the Pulse Pal firmware for both hardware versions, and optionally compare
the compiled code with another git revision, function by function.

The comparison is the quickest way to answer "did this edit change what the device
does?". Comment edits, renames and moving code between tabs leave every function
identical. If a function you did not intend to change shows up as changed, look again.

Examples:

    # Compile for Pulse Pal 3 and Pulse Pal 2
    python Firmware/tools/build_check.py

    # Compile, then compare every function with the last commit
    python Firmware/tools/build_check.py --compare HEAD

    # Only Pulse Pal 3
    python Firmware/tools/build_check.py --hardware 3

    # The Wave Pal firmware (Firmware/WavePal), which runs on Pulse Pal 3 only
    python Firmware/tools/build_check.py --sketch wavepal --compare HEAD

Requirements:
  - arduino-cli, with the teensy:avr and arduino:sam cores installed. The Arduino IDE
    ships one; set ARDUINO_CLI to its path if it is not on PATH.
  - For the comparison: arm-none-eabi-objdump, which comes with the Teensy core. Set
    OBJDUMP if it is not found automatically.
  - Pulse Pal 3 needs the U8g2 library (verified with v2.36.19), which no longer has to be
    edited by hand. Its SdFat comes from the Teensy core, unless a copy installed in
    /Arduino/libraries takes priority over it.
  - Pulse Pal 2 needs SdFat v2 installed (verified with v2.1.2 and v2.3.0).
  - Pulse Pal 2 also needs the LiquidCrystal library, which this script replaces with the
    compile-only stub in tools/stub_libraries, so that the Pulse Pal 2 build can be checked
    on a machine that does not have it. A binary built that way must never be flashed:
    pass --real-libraries to build one that can be.
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
STUB_LIBRARIES = Path(__file__).resolve().parent / "stub_libraries"

# The sketches this script builds. paths_in_git lists the folder's names in git history, for --compare
SKETCHES = {
    "pulsepal": {
        "dir": REPO_ROOT / "Firmware" / "PulsePal3",
        # Revisions before the folder was renamed from PulsePal_3 to PulsePal3
        "paths_in_git": ["Firmware/PulsePal3", "Firmware/PulsePal_3"],
        "hardware": [2, 3],
    },
    "wavepal": {
        "dir": REPO_ROOT / "Firmware" / "WavePal",
        "paths_in_git": ["Firmware/WavePal"],
        "hardware": [3],
    },
}

BOARDS = {
    2: {"fqbn": "arduino:sam:arduino_due_x", "name": "Pulse Pal 2 (Arduino Due)"},
    3: {"fqbn": "teensy:avr:teensy41", "name": "Pulse Pal 3 (Teensy 4.1)"},
}

ARDUINO_CLI_GUESSES = [
    Path(os.environ.get("LOCALAPPDATA", "")) / "Programs/Arduino IDE/resources/app/lib/backend/resources/arduino-cli.exe",
    Path("C:/Program Files/Arduino IDE/resources/app/lib/backend/resources/arduino-cli.exe"),
    Path("/usr/local/bin/arduino-cli"),
    Path.home() / ".arduino15/arduino-cli",
]


def find_tool(env_var, executable_name, guesses):
    """Find a build tool from an environment variable, PATH, or a list of usual places."""
    from_env = os.environ.get(env_var)
    if from_env:
        return from_env
    on_path = shutil.which(executable_name)
    if on_path:
        return on_path
    for guess in guesses:
        if guess and Path(guess).exists():
            return str(guess)
    return None


def find_objdump():
    guesses = []
    packages = Path(os.environ.get("LOCALAPPDATA", "")) / "Arduino15/packages/teensy/tools/teensy-compile"
    if packages.is_dir():
        guesses += sorted(packages.glob("*/arm/bin/arm-none-eabi-objdump*"), reverse=True)
    guesses += sorted(Path.home().glob(".arduino15/packages/teensy/tools/teensy-compile/*/arm/bin/arm-none-eabi-objdump"), reverse=True)
    return find_tool("OBJDUMP", "arm-none-eabi-objdump", guesses)


def sketch_with_hardware_version(sketch_dir, hardware_version, destination):
    """Copy the sketch, with HARDWARE_VERSION set to hardware_version. Returns the copy's folder.

    The macro is set by editing the source, not with -DHARDWARE_VERSION, because the Teensy core's
    platform.txt has no compiler.cpp.extra_flags: arduino-cli accepts the build property and records it
    in build.options.json, then the compile recipe drops it. Passing the flag, the Pulse Pal 3 build
    compiles whichever version the #define in the source names, whatever this script asked for.
    """
    sketch = destination / sketch_dir.name  # arduino-cli needs the folder and the main tab to share a name
    shutil.copytree(sketch_dir, sketch)
    main_tab = sketch / (sketch_dir.name + ".ino")
    source = main_tab.read_text(encoding="utf-8")
    # Matches the bare #define used before the #ifndef guards were added, and the indented one inside them
    pattern = r"^([ \t]*#define[ \t]+HARDWARE_VERSION[ \t]+)[0-9]+"
    edited, substitutions = re.subn(pattern, r"\g<1>" + str(hardware_version), source, flags=re.M)
    if substitutions != 1:
        print(f"  Expected one '#define HARDWARE_VERSION' in {main_tab.name}, found {substitutions}")
        return None
    main_tab.write_text(edited, encoding="utf-8")
    return sketch


def build(arduino_cli, sketch_dir, hardware_version, build_dir, real_libraries):
    """Compile the sketch. Returns the path of the compiled sketch object file."""
    board = BOARDS[hardware_version]
    sketch = sketch_with_hardware_version(sketch_dir, hardware_version, build_dir / "source")
    if sketch is None:
        print(f"  BUILD FAILED for {board['name']}")
        return None
    command = [
        arduino_cli, "compile",
        "--fqbn", board["fqbn"],
        "--build-path", str(build_dir / "build"),
        str(sketch),
    ]
    if hardware_version == 2 and not real_libraries:
        command += ["--libraries", str(STUB_LIBRARIES)]
    print(f"Building {board['name']} ...")
    result = subprocess.run(command, capture_output=True, text=True)
    for line in (result.stdout + result.stderr).splitlines():
        if re.search(r"error|warning: .*\.ino|FLASH:|RAM1:|Sketch uses", line, re.I):
            print("  " + line.strip())
    if result.returncode != 0:
        print(f"  BUILD FAILED for {board['name']}")
        return None
    objects = sorted((build_dir / "build" / "sketch").glob("*.ino.cpp.o"))
    if not objects:
        print("  Could not find the compiled sketch object file")
        return None
    return objects[0]


def normalized_functions(objdump, object_file):
    """Return {function name: [instructions]} with addresses and string offsets removed.

    Addresses, literal pool offsets and compiler-generated local labels all move when
    unrelated code changes, so they are replaced with placeholders. Offsets into the
    string table are replaced with the string itself.
    """
    def run(*args):
        return subprocess.run([objdump, *args, str(object_file)],
                              capture_output=True, text=True, errors="replace").stdout

    strings = {}
    for section in sorted(set(re.findall(r"\s(\.rodata\.str[\w.]*)\s", run("-h")))):
        data = bytearray()
        for line in run("-s", "-j", section).splitlines():
            match = re.match(r"^\s*([0-9a-f]+)\s((?:[0-9a-f]{2,8}\s?)+)", line)
            if match:
                data += bytes.fromhex(match.group(2).replace(" ", "")[:32])
        strings[section] = bytes(data)

    functions = {}
    current = None
    pending = None
    for line in run("-d", "-r", "--no-show-raw-insn", "-C").splitlines():
        header = re.match(r"^[0-9a-f]+ <(.*)>:$", line)
        if header:
            if not re.match(r"L_\d+_", header.group(1)):  # local label inside a function
                current = header.group(1)
                functions.setdefault(current, [])
            continue
        body = re.match(r"^\s+[0-9a-f]+:\s*(.*)$", line)
        if not body or current is None:
            continue
        text = re.sub(r"[0-9a-f]+ <[^>]*>", "<ref>", body.group(1))
        text = re.sub(r"L_\d+_", "L_", text)
        relocation = re.match(r"R_ARM_ABS32\s+(\.rodata\.str[\w.]*)$", text.strip())
        if relocation and pending is not None and relocation.group(1) in strings:
            index, value = pending
            table = strings[relocation.group(1)]
            end = table.find(b"\0", value)
            functions[current][index] = ".word str:" + repr(table[value:end])
        word = re.match(r"\.word\s+0x([0-9a-f]+)", text)
        pending = (len(functions[current]), int(word.group(1), 16)) if word else None
        functions[current].append(text)
    return functions


def compare(objdump, old_object, new_object, show=None):
    """Print the functions that differ between two builds. Returns the number that changed."""
    old = normalized_functions(objdump, old_object)
    new = normalized_functions(objdump, new_object)
    if show:
        for name in sorted(set(old) | set(new)):
            if show.lower() in name.lower():
                import difflib
                print(f"  --- {name}")
                for line in difflib.unified_diff(old.get(name, []), new.get(name, []),
                                                 "before", "after", lineterm="", n=2):
                    print("  " + line)
    removed = sorted(set(old) - set(new))
    added = sorted(set(new) - set(old))
    changed = sorted(name for name in set(old) & set(new) if old[name] != new[name])
    if removed:
        print("  removed: " + ", ".join(removed))
    if added:
        print("  added:   " + ", ".join(added))
    if changed:
        print("  changed: " + ", ".join(changed))
    if not (removed or added or changed):
        print("  every function is identical")
    return len(changed) + len(added) + len(removed)


def checkout(git_reference, destination, paths_in_git):
    """Copy the firmware folder at a git revision into destination."""
    for path_in_git in paths_in_git:  # A folder that was renamed has a different name in older revisions
        archive = subprocess.run(
            ["git", "archive", git_reference, path_in_git],
            cwd=REPO_ROOT, capture_output=True,
        )
        if archive.returncode == 0:
            break
    else:
        print(f"Could not read the firmware folder at {git_reference} from git")
        return None
    destination.mkdir(parents=True, exist_ok=True)
    extract = subprocess.run(["tar", "-x", "-C", str(destination)], input=archive.stdout)
    if extract.returncode != 0:
        print("Could not extract the archive (tar is required for --compare)")
        return None
    # Revisions that pre-date the #ifndef guards need no special handling: sketch_with_hardware_version()
    # rewrites the #define itself, so it does not matter whether the source lets the command line win.
    return destination / path_in_git.replace("/", os.sep)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--sketch", choices=sorted(SKETCHES), default="pulsepal",
                        help="Firmware to build: pulsepal (Firmware/PulsePal3, the default) or wavepal "
                             "(Firmware/WavePal, Pulse Pal 3 only)")
    parser.add_argument("--hardware", choices=["2", "3", "both"], default="both",
                        help="Hardware version to build for (default: both)")
    parser.add_argument("--compare", metavar="GIT_REF",
                        help="Also build this git revision, and compare the compiled functions")
    parser.add_argument("--show", metavar="FUNCTION",
                        help="With --compare, print the instruction differences for functions whose name contains this text")
    parser.add_argument("--real-libraries", action="store_true",
                        help="Use the installed LiquidCrystal library for Pulse Pal 2, not the compile-only stub")
    arguments = parser.parse_args()

    arduino_cli = find_tool("ARDUINO_CLI", "arduino-cli", ARDUINO_CLI_GUESSES)
    if not arduino_cli:
        print("arduino-cli was not found. Set ARDUINO_CLI to its path.")
        return 2
    objdump = find_objdump() if arguments.compare else None
    if arguments.compare and not objdump:
        print("arm-none-eabi-objdump was not found. Set OBJDUMP to its path.")
        return 2

    sketch = SKETCHES[arguments.sketch]
    versions = [2, 3] if arguments.hardware == "both" else [int(arguments.hardware)]
    versions = [version for version in versions if version in sketch["hardware"]]
    if not versions:
        print(f"{arguments.sketch} does not run on Pulse Pal {arguments.hardware}")
        return 2
    problems = 0
    with tempfile.TemporaryDirectory(prefix="pulsepal_build_") as temporary:
        temporary = Path(temporary)
        old_sketch = None
        if arguments.compare:
            old_sketch = checkout(arguments.compare, temporary / "old_source", sketch["paths_in_git"])
            if old_sketch is None:
                return 2
        for version in versions:
            new_object = build(arduino_cli, sketch["dir"], version, temporary / f"new{version}", arguments.real_libraries)
            if new_object is None:
                problems += 1
                continue
            if old_sketch is None:
                continue
            old_object = build(arduino_cli, old_sketch, version, temporary / f"old{version}", arguments.real_libraries)
            if old_object is None:
                problems += 1
                continue
            print(f"Comparing {BOARDS[version]['name']} with {arguments.compare}:")
            compare(objdump, old_object, new_object, arguments.show)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())

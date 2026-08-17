"""
----------------------------------------------------------------------------

This file is part of the Sanworks PulsePal repository
Copyright (C) Sanworks LLC, Rochester, New York, USA

----------------------------------------------------------------------------

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, version 3.

This program is distributed WITHOUT ANY WARRANTY and without even the
implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program. If not, see <http://www.gnu.org/licenses/>.
"""

# Parameter editor GUI for the Pulse Pal Python interface. This is the Python
# analog of MATLAB/@PulsePalDevice/gui.m. Launch it with PulsePalDevice.gui().

import dataclasses
import json
import sys
import tkinter as tk
import weakref
from tkinter import filedialog, messagebox, ttk


def _format_number(value):
    """Format a parameter value for display without scientific notation."""
    text = f"{float(value):.6f}".rstrip("0").rstrip(".")
    return text if text not in ("", "-") else "0"


def _parse_number_list(text):
    """Parse a comma (or newline) delimited list of numbers."""
    items = [item.strip() for item in text.replace("\n", ",").split(",")]
    return [float(item) for item in items if item]


class _ToolTip:
    """Minimal hover tooltip, used to mirror the MATLAB GUI's tooltips."""

    def __init__(self, widget, text):
        self._widget = widget
        self._text = text
        self._window = None
        widget.bind("<Enter>", self._show, add="+")
        widget.bind("<Leave>", self._hide, add="+")
        widget.bind("<ButtonPress>", self._hide, add="+")

    def _show(self, _event=None):
        if self._window is not None or not self._text:
            return
        x = self._widget.winfo_rootx() + 20
        y = self._widget.winfo_rooty() + self._widget.winfo_height() + 4
        self._window = tk.Toplevel(self._widget)
        self._window.wm_overrideredirect(True)
        self._window.wm_geometry(f"+{x}+{y}")
        tk.Label(
            self._window,
            text=self._text,
            justify="left",
            background="#ffffe0",
            foreground="#000000",
            relief="solid",
            borderwidth=1,
            wraplength=320,
        ).pack(ipadx=4, ipady=2)

    def _hide(self, _event=None):
        if self._window is not None:
            try:
                self._window.destroy()
            except tk.TclError:
                pass
            self._window = None


class PulsePalGUI:
    """Parameter editor window for a connected PulsePalDevice.

    Parameters are edited in a local copy held by the GUI, and are only sent
    to the device when 'Load to Device' is clicked. This matches the behavior
    of the MATLAB parameter GUI.
    """

    _PULSE_TYPES = ("Monophasic", "Biphasic")
    _CUSTOM_TRAIN_TARGETS = ("Pulses", "Bursts")
    _TRIGGER_MODES = ("Normal", "Toggle", "Pulse Gated")

    _DEFAULT_OUTPUT_PARAMS = {
        "is_biphasic": 0,
        "phase1_voltage": 5.0,
        "phase2_voltage": -5.0,
        "resting_voltage": 0.0,
        "phase1_duration": 0.001,
        "inter_phase_interval": 0.001,
        "phase2_duration": 0.001,
        "inter_pulse_interval": 0.01,
        "burst_duration": 0.0,
        "inter_burst_interval": 0.0,
        "pulse_train_duration": 1.0,
        "pulse_train_delay": 0.0,
        "link_trigger_channel1": 1,
        "link_trigger_channel2": 0,
        "custom_train_id": 0,
        "custom_train_target": 0,
        "custom_train_loop": 0,
    }

    # (parameter name, label, tooltip)
    _VOLTAGE_FIELDS = (
        (
            "resting_voltage",
            "Resting (V)",
            "Voltage while not delivering a pulse (V)",
        ),
        (
            "phase1_voltage",
            "Phase1 (V)",
            "Voltage of the first phase of each pulse (V)",
        ),
        (
            "phase2_voltage",
            "Phase2 (V)",
            "Voltage of the second phase of each pulse (V)",
        ),
    )
    _TIME_FIELDS = (
        (
            "phase1_duration",
            "Phase1 (s)",
            "Duration of the first phase of each pulse (s)",
        ),
        (
            "inter_phase_interval",
            "Phase Interval",
            "Interval between pulse phases (s)",
        ),
        (
            "phase2_duration",
            "Phase2 (s)",
            "Duration of the second phase of each pulse (s)",
        ),
        (
            "inter_pulse_interval",
            "Pulse Interval",
            "Interval between pulse-end and the next pulse (s)",
        ),
        (
            "burst_duration",
            "Burst (s)",
            "Duration of pulse bursts (0 = no bursts, units = seconds)",
        ),
        (
            "inter_burst_interval",
            "Burst Interval",
            "Interval between pulse bursts (s)",
        ),
        (
            "pulse_train_duration",
            "Train (s)",
            "Duration of the pulse train (s)",
        ),
        (
            "pulse_train_delay",
            "Train Delay",
            "Delay from trigger to pulse train onset (s)",
        ),
    )

    # Parameters that are only meaningful for biphasic pulses
    _BIPHASIC_ONLY = (
        "phase2_voltage",
        "inter_phase_interval",
        "phase2_duration",
    )

    # Valid ranges, matching those enforced by the device interface
    _FIELD_RANGES = {
        "resting_voltage": (-10.0, 10.0),
        "phase1_voltage": (-10.0, 10.0),
        "phase2_voltage": (-10.0, 10.0),
        "phase1_duration": (0.0001, 3600.0),
        "inter_phase_interval": (0.0, 3600.0),
        "phase2_duration": (0.0001, 3600.0),
        "inter_pulse_interval": (0.0001, 3600.0),
        "burst_duration": (0.0, 3600.0),
        "inter_burst_interval": (0.0, 3600.0),
        "pulse_train_duration": (0.0001, 3600.0),
        "pulse_train_delay": (0.0, 3600.0),
    }

    def __init__(self, device):
        # The device is held weakly so that the GUI never keeps a released
        # PulsePalDevice alive: the device's destructor closes this window.
        self._device_ref = weakref.ref(device)
        self._closed = False
        self._release_host_event_loop = None
        self._loading = True
        self._last_program_dir = ""

        n_trains = getattr(device.info, "n_custom_pulse_trains", None) or 2
        self._n_custom_trains = int(n_trains)
        self._custom_timestamps = [""] * self._n_custom_trains
        self._custom_voltages = [""] * self._n_custom_trains

        self._params = {}
        self._trigger_mode = []
        self._load_default_params()

        self._entry_vars = {}
        self._entry_widgets = {}
        self._field_labels = {
            name: label
            for name, label, _ in self._VOLTAGE_FIELDS + self._TIME_FIELDS
        }

        self._root = tk.Tk()
        self._root.title("Pulse Pal Parameter GUI")
        self._root.resizable(False, False)
        self._root.protocol("WM_DELETE_WINDOW", self.close)

        self._build_header()
        self._build_output_panel()
        self._build_trigger_panel()
        self._build_custom_train_panel()
        self._build_status_bar()

        self._loading = False
        self._refresh()
        self._set_status("GUI Loaded")

    # ---- Public interface ----

    @property
    def is_closed(self):
        """True once the GUI window has been closed."""
        return self._closed

    @property
    def _device(self):
        """The device being edited, or None once it has been released."""
        ref = self._device_ref
        return ref() if ref is not None else None

    def start(self, block=None):
        """Show the GUI.

        Args:
            block: If True, run the Tk event loop until the window is closed.
                If False, return immediately (the host application must pump
                Tk events). If None, block only when the host does not
                already provide a Tk event loop.
        """
        if self._closed:
            return
        if block is None:
            block = not self._enable_host_event_loop()
        if block:
            try:
                self._root.mainloop()
            finally:
                self.close()

    def focus(self):
        """Raise the GUI window and give it keyboard focus."""
        if self._closed:
            return
        try:
            self._root.deiconify()
            self._root.lift()
            self._root.focus_force()
        except tk.TclError:
            pass

    def close(self):
        """Close the GUI window."""
        if self._closed:
            return
        self._closed = True

        device = self._device
        self._device_ref = None
        if device is not None and getattr(device, "_gui", None) is self:
            device._gui = None

        # Unregister before the window is destroyed, so that the host does
        # not keep pumping events for a dead Tk interpreter
        release = self._release_host_event_loop
        self._release_host_event_loop = None
        if release is not None:
            try:
                release()
            except Exception:
                pass

        root = self._root
        self._root = None
        if root is not None:
            try:
                root.destroy()
            except Exception:
                # The interpreter may already be tearing down Tk
                pass

    def _enable_host_event_loop(self):
        """Return True if the host will pump Tk events for the GUI."""
        # The PyCharm / PyDev console pumps a registered input hook between
        # commands. This window is passed explicitly: left to itself, PyDev
        # creates a second Tk interpreter, whose event loop would not service
        # this window. This is tried before IPython because the PyCharm
        # console's IPython shell delegates to the same hook.
        try:
            from pydev_ipython.inputhook import (
                GUI_TK,
                clear_inputhook,
                enable_gui,
            )
            enable_gui(GUI_TK, app=self._root)
        except Exception:
            pass
        else:
            self._release_host_event_loop = clear_inputhook
            return True

        try:
            from IPython import get_ipython
            shell = get_ipython()
        except Exception:
            shell = None

        if shell is not None:
            try:
                shell.enable_gui("tk")
                return True
            except Exception:
                return False

        # The interactive CPython prompt pumps Tk events between commands
        return bool(getattr(sys, "ps1", None)) or bool(sys.flags.interactive)

    # ---- Parameter storage ----

    def _load_default_params(self):
        self._params = {
            name: [value] * 4
            for name, value in self._DEFAULT_OUTPUT_PARAMS.items()
        }
        self._trigger_mode = [0, 0]

    def _output_channel(self):
        return self._output_channel_var.get()

    def _trigger_channel(self):
        return self._trigger_channel_var.get()

    # ---- Widget construction ----

    def _build_header(self):
        header = ttk.Frame(self._root)
        header.pack(fill="x", padx=10, pady=(8, 0))

        ttk.Label(
            header,
            text="Pulse Pal Program Editor",
            font=("TkDefaultFont", 16, "bold"),
        ).pack(side="left")

        fire = ttk.Button(header, text="FIRE", width=6, command=self._fire)
        fire.pack(side="right", padx=(8, 0))
        _ToolTip(fire, "Trigger the selected output channels")

        checks = ttk.Frame(header)
        checks.pack(side="right")
        ttk.Label(
            checks,
            text="Trigger Channels:",
            font=("TkDefaultFont", 9, "bold"),
        ).grid(row=1, column=0, padx=(0, 6))
        self._fire_vars = []
        for channel in range(1, 5):
            var = tk.IntVar(value=0)
            self._fire_vars.append(var)
            ttk.Label(
                checks,
                text=str(channel),
                font=("TkDefaultFont", 9, "bold"),
            ).grid(row=0, column=channel)
            check = ttk.Checkbutton(checks, variable=var)
            check.grid(row=1, column=channel)
            _ToolTip(check, f"Include output channel {channel} when firing")

        toolbar = ttk.Frame(self._root)
        toolbar.pack(fill="x", padx=10, pady=(6, 0))
        tools = (
            ("Restore Defaults", self._restore_defaults,
             "Restore default parameters"),
            ("Open Program...", self._open_program,
             "Open a program from a .json file"),
            ("Save Program...", self._save_program,
             "Save the current program to a .json file"),
            ("Load to Device", self._upload_program,
             "Load the current program to the Pulse Pal device"),
        )
        for text, command, tooltip in tools:
            button = ttk.Button(toolbar, text=text, command=command)
            button.pack(side="left", padx=(0, 6))
            _ToolTip(button, tooltip)

    def _build_output_panel(self):
        panel = ttk.LabelFrame(self._root, text="Output Channels")
        panel.pack(fill="x", padx=10, pady=(8, 0), ipady=4)

        channels = ttk.LabelFrame(panel, text="Channel")
        channels.pack(side="left", padx=6, pady=4, anchor="n")
        _ToolTip(channels, "Select an output channel to edit")
        self._output_channel_var = tk.IntVar(value=1)
        for index, channel in enumerate((1, 2, 3, 4)):
            ttk.Radiobutton(
                channels,
                text=str(channel),
                value=channel,
                variable=self._output_channel_var,
                command=self._refresh,
            ).grid(row=index // 2, column=index % 2, sticky="w", padx=2)

        fields = ttk.Frame(panel)
        fields.pack(side="left", fill="x", expand=True, pady=2)

        top = ttk.Frame(fields)
        top.pack(fill="x")
        column = 0

        self._pulse_type_box = self._labeled(
            top,
            column,
            "Pulse Type",
            lambda parent: self._make_combobox(
                parent, self._PULSE_TYPES, self._on_pulse_type, width=11
            ),
            "Biphasic pulses add an interval at the resting voltage and "
            "then a second phase to each pulse",
        )
        column += 1

        for name, label, tooltip in self._VOLTAGE_FIELDS:
            self._labeled(
                top,
                column,
                label,
                lambda parent, n=name: self._make_entry(parent, n),
                tooltip,
            )
            column += 1

        train_ids = ["0 (None)"] + [
            str(i) for i in range(1, self._n_custom_trains + 1)
        ]
        self._custom_id_box = self._labeled(
            top,
            column,
            "Custom Train ID",
            lambda parent: self._make_combobox(
                parent, train_ids, self._on_custom_train_id, width=9
            ),
            "Custom pulse train to play on this output channel",
        )
        column += 1

        self._custom_target_box = self._labeled(
            top,
            column,
            "Custom Train of",
            lambda parent: self._make_combobox(
                parent,
                self._CUSTOM_TRAIN_TARGETS,
                self._on_custom_train_target,
                width=9,
            ),
            "Custom train timestamps can indicate the onset of either each "
            "pulse, or each burst of pulses",
        )
        column += 1

        self._custom_loop_var = tk.IntVar(value=0)
        self._custom_loop_check = self._labeled(
            top,
            column,
            "Loop",
            lambda parent: ttk.Checkbutton(
                parent,
                variable=self._custom_loop_var,
                command=self._on_custom_train_loop,
            ),
            "If enabled, the custom pulse train loops until the pulse train "
            "duration (Train (s) below)",
        )

        bottom = ttk.Frame(fields)
        bottom.pack(fill="x")
        for column, (name, label, tooltip) in enumerate(self._TIME_FIELDS):
            self._labeled(
                bottom,
                column,
                label,
                lambda parent, n=name: self._make_entry(parent, n),
                tooltip,
            )

    def _build_trigger_panel(self):
        panel = ttk.LabelFrame(self._root, text="Trigger Channels")
        panel.pack(fill="x", padx=10, pady=(8, 0), ipady=4)

        channels = ttk.LabelFrame(panel, text="Channel")
        channels.pack(side="left", padx=6, pady=4, anchor="n")
        _ToolTip(channels, "Select a trigger channel to edit")
        self._trigger_channel_var = tk.IntVar(value=1)
        for channel in (1, 2):
            ttk.Radiobutton(
                channels,
                text=str(channel),
                value=channel,
                variable=self._trigger_channel_var,
                command=self._refresh,
            ).grid(row=0, column=channel - 1, sticky="w", padx=2)

        fields = ttk.Frame(panel)
        fields.pack(side="left", pady=2)

        self._trigger_mode_box = self._labeled(
            fields,
            0,
            "Trigger Mode",
            lambda parent: self._make_combobox(
                parent, self._TRIGGER_MODES, self._on_trigger_mode, width=12
            ),
            "Normal: TTL during pulse train ignored. Toggle: TTL during "
            "pulse train stops train. Pulse Gated: Pulse train only runs "
            "while trigger is high",
        )

        links = ttk.Frame(fields)
        links.grid(row=0, column=1, padx=(16, 4), sticky="w")
        ttk.Label(links, text="Link to outputs").pack(anchor="w")
        link_row = ttk.Frame(links)
        link_row.pack(anchor="w")
        self._link_vars = []
        for channel in range(1, 5):
            var = tk.IntVar(value=0)
            self._link_vars.append(var)
            check = ttk.Checkbutton(
                link_row,
                text=f"Ch{channel}",
                variable=var,
                command=lambda c=channel: self._on_trigger_link(c),
            )
            check.pack(side="left", padx=(0, 8))
            _ToolTip(check, f"Link trigger channel to output channel "
                            f"{channel}")

    def _build_custom_train_panel(self):
        panel = ttk.LabelFrame(self._root, text="Custom Pulse Trains")
        panel.pack(fill="x", padx=10, pady=(8, 0), ipady=4)

        selector = ttk.Frame(panel)
        selector.pack(side="left", padx=6, pady=4, anchor="n")
        ttk.Label(selector, text="Custom Train ID").pack(anchor="w")
        self._custom_train_list = tk.Listbox(
            selector,
            height=min(self._n_custom_trains, 4),
            width=6,
            exportselection=False,
        )
        for train_id in range(1, self._n_custom_trains + 1):
            self._custom_train_list.insert("end", str(train_id))
        self._custom_train_list.selection_set(0)
        self._custom_train_list.bind(
            "<<ListboxSelect>>", self._on_custom_train_selected
        )
        self._custom_train_list.pack(anchor="w")
        _ToolTip(self._custom_train_list, "Select the custom train to "
                                          "program")

        self._timestamp_text = self._make_train_text(
            panel,
            "Timestamps (s)",
            "Enter the onset time of each pulse in the custom pulse train "
            "(comma delimited, units = seconds)",
            self._commit_timestamps,
        )
        self._voltage_text = self._make_train_text(
            panel,
            "Voltages (V)",
            "Enter the voltage of each pulse in the custom pulse train "
            "(comma delimited, units = volts)",
            self._commit_voltages,
        )

    def _build_status_bar(self):
        bar = ttk.Frame(self._root)
        bar.pack(fill="x", padx=10, pady=(6, 8))

        info = self._device.info
        port_name = getattr(self._device.port, "port", "")
        ttk.Label(
            bar, text=f"HW: Pulse Pal v{info.hardware_version}"
        ).pack(side="left", padx=(0, 12))
        ttk.Label(
            bar, text=f"Firmware: v{info.firmware_version}"
        ).pack(side="left", padx=(0, 12))
        ttk.Label(bar, text=f"Port: {port_name}").pack(side="left")

        self._status_var = tk.StringVar(value="Status: GUI Loaded")
        ttk.Label(
            bar,
            textvariable=self._status_var,
            font=("TkDefaultFont", 9, "bold"),
        ).pack(side="right")

    def _labeled(self, parent, column, label, widget_factory, tooltip=None):
        """Create a labeled widget in a grid column of parent."""
        holder = ttk.Frame(parent)
        holder.grid(row=0, column=column, padx=4, pady=2, sticky="w")
        ttk.Label(holder, text=label).pack(anchor="w")
        widget = widget_factory(holder)
        widget.pack(anchor="w")
        if tooltip:
            _ToolTip(widget, tooltip)
        return widget

    def _make_entry(self, parent, name):
        var = tk.StringVar()
        entry = ttk.Entry(parent, textvariable=var, width=10, justify="center")
        entry.bind("<Return>", lambda event, n=name: self._commit_entry(n))
        entry.bind("<FocusOut>", lambda event, n=name: self._commit_entry(n))
        self._entry_vars[name] = var
        self._entry_widgets[name] = entry
        return entry

    def _make_combobox(self, parent, values, callback, width):
        box = ttk.Combobox(
            parent,
            values=list(values),
            state="readonly",
            width=width,
        )
        box.current(0)
        box.bind("<<ComboboxSelected>>", lambda event: callback())
        return box

    def _make_train_text(self, parent, label, tooltip, commit):
        holder = ttk.Frame(parent)
        holder.pack(side="left", padx=6, pady=4, anchor="n")
        ttk.Label(holder, text=label).pack(anchor="w")
        text = tk.Text(holder, width=34, height=3, wrap="word")
        text.pack(anchor="w")
        text.bind("<FocusOut>", lambda event: commit())
        _ToolTip(text, tooltip)
        return text

    # ---- Refreshing the view ----

    def _refresh(self):
        """Push the local parameter copy to the widgets."""
        self._loading = True
        try:
            channel = self._output_channel()
            index = channel - 1

            self._pulse_type_box.current(
                int(self._params["is_biphasic"][index])
            )
            self._custom_id_box.current(
                int(self._params["custom_train_id"][index])
            )
            self._custom_target_box.current(
                int(self._params["custom_train_target"][index])
            )
            self._custom_loop_var.set(
                int(self._params["custom_train_loop"][index])
            )

            for name in self._entry_vars:
                self._entry_vars[name].set(
                    _format_number(self._params[name][index])
                )

            trigger_channel = self._trigger_channel()
            self._trigger_mode_box.current(
                int(self._trigger_mode[trigger_channel - 1])
            )
            link_param = f"link_trigger_channel{trigger_channel}"
            for output_index, var in enumerate(self._link_vars):
                var.set(int(self._params[link_param][output_index]))
        finally:
            self._loading = False

        self._refresh_custom_train_view()
        self._update_enabled_state()

    def _refresh_custom_train_view(self):
        train_index = self._selected_custom_train() - 1
        self._set_text(
            self._timestamp_text, self._custom_timestamps[train_index]
        )
        self._set_text(self._voltage_text, self._custom_voltages[train_index])

    def _update_enabled_state(self):
        index = self._output_channel() - 1
        is_biphasic = bool(self._params["is_biphasic"][index])
        for name in self._BIPHASIC_ONLY:
            self._entry_widgets[name].configure(
                state="normal" if is_biphasic else "disabled"
            )

        uses_custom = int(self._params["custom_train_id"][index]) > 0
        self._custom_target_box.configure(
            state="readonly" if uses_custom else "disabled"
        )
        self._custom_loop_check.configure(
            state="normal" if uses_custom else "disabled"
        )
        self._custom_train_list.configure(
            state="normal" if uses_custom else "disabled"
        )
        for text in (self._timestamp_text, self._voltage_text):
            text.configure(state="normal" if uses_custom else "disabled")

    def _set_text(self, widget, value):
        was_disabled = str(widget.cget("state")) == "disabled"
        if was_disabled:
            widget.configure(state="normal")
        widget.delete("1.0", "end")
        widget.insert("1.0", value)
        if was_disabled:
            widget.configure(state="disabled")

    def _set_status(self, message):
        self._status_var.set(f"Status: {message}")

    def _selected_custom_train(self):
        selection = self._custom_train_list.curselection()
        return (selection[0] + 1) if selection else 1

    # ---- Parameter edit callbacks ----

    def _commit_entry(self, name):
        if self._loading or self._closed:
            return
        index = self._output_channel() - 1
        var = self._entry_vars[name]
        label = self._field_labels[name]
        try:
            value = float(var.get())
        except ValueError:
            self._show_error(f"{label} must be a number.")
            var.set(_format_number(self._params[name][index]))
            return

        low, high = self._FIELD_RANGES[name]
        if not low <= value <= high:
            self._show_error(
                f"{label} must be in range {_format_number(low)} to "
                f"{_format_number(high)}."
            )
            var.set(_format_number(self._params[name][index]))
            return

        self._params[name][index] = value
        var.set(_format_number(value))

    def _on_pulse_type(self):
        index = self._output_channel() - 1
        self._params["is_biphasic"][index] = self._pulse_type_box.current()
        self._update_enabled_state()

    def _on_custom_train_id(self):
        index = self._output_channel() - 1
        self._params["custom_train_id"][index] = self._custom_id_box.current()
        self._update_enabled_state()

    def _on_custom_train_target(self):
        index = self._output_channel() - 1
        self._params["custom_train_target"][index] = (
            self._custom_target_box.current()
        )

    def _on_custom_train_loop(self):
        index = self._output_channel() - 1
        self._params["custom_train_loop"][index] = self._custom_loop_var.get()

    def _on_trigger_mode(self):
        channel_index = self._trigger_channel() - 1
        self._trigger_mode[channel_index] = self._trigger_mode_box.current()

    def _on_trigger_link(self, output_channel):
        link_param = f"link_trigger_channel{self._trigger_channel()}"
        self._params[link_param][output_channel - 1] = (
            self._link_vars[output_channel - 1].get()
        )

    def _on_custom_train_selected(self, _event=None):
        self._refresh_custom_train_view()

    def _commit_timestamps(self):
        if self._closed:
            return
        text = self._timestamp_text.get("1.0", "end-1c")
        self._custom_timestamps[self._selected_custom_train() - 1] = text
        try:
            _parse_number_list(text)
        except ValueError:
            self._show_error(
                "Timestamps must be a comma-delimited list of pulse onset "
                "times, given in seconds."
            )

    def _commit_voltages(self):
        if self._closed:
            return
        text = self._voltage_text.get("1.0", "end-1c")
        self._custom_voltages[self._selected_custom_train() - 1] = text
        try:
            _parse_number_list(text)
        except ValueError:
            self._show_error(
                "Voltages must be a comma-delimited list of pulse voltages, "
                "given in volts."
            )

    # ---- Toolbar actions ----

    def _fire(self):
        channels = [
            channel
            for channel, var in enumerate(self._fire_vars, start=1)
            if var.get()
        ]
        device = self._device
        if not channels or device is None:
            return
        try:
            device.trigger(channels)
        except Exception as exc:
            self._show_error(f"Failed to trigger output channels:\n{exc}")
            return
        self._set_status("Output Channels Triggered")

    def _restore_defaults(self):
        self._load_default_params()
        self._custom_timestamps = [""] * self._n_custom_trains
        self._custom_voltages = [""] * self._n_custom_trains
        self._reset_selections()
        self._refresh()
        self._set_status("Default Program Restored")

    def _upload_program(self):
        device = self._device
        if device is None:
            return

        custom_trains = self._collect_custom_trains()
        if custom_trains is None:
            return

        for index in range(4):
            if (
                int(self._params["custom_train_target"][index]) == 1
                and float(self._params["burst_duration"][index]) == 0
            ):
                self._show_error(
                    f"Error in output channel {index + 1}: when custom train "
                    "times target burst onsets, a non-zero burst duration "
                    "must be defined."
                )
                return

        try:
            for name, values in self._params.items():
                getattr(device, name)[1:5] = list(values)
            device.trigger_mode[1:3] = list(self._trigger_mode)
            device.sync_to_device()
            for train_id, times, voltages in custom_trains:
                device.send_custom_pulse_train(train_id, times, voltages)
        except Exception as exc:
            self._show_error(f"Failed to load the program to the device:\n"
                             f"{exc}")
            return
        self._set_status("Program Loaded to Device")

    def _collect_custom_trains(self):
        """Parse the custom train editor, returning None if it is invalid."""
        trains = []
        for train_id in range(1, self._n_custom_trains + 1):
            timestamp_text = self._custom_timestamps[train_id - 1]
            voltage_text = self._custom_voltages[train_id - 1]
            if not timestamp_text.strip() and not voltage_text.strip():
                continue
            try:
                times = _parse_number_list(timestamp_text)
                voltages = _parse_number_list(voltage_text)
            except ValueError:
                self._show_error(
                    f"Failed to load custom pulse train {train_id}: "
                    "timestamps and voltages must be comma-delimited lists "
                    "of numbers."
                )
                return None
            if len(times) != len(voltages):
                self._show_error(
                    f"Failed to load custom pulse train {train_id}: the "
                    "number of timestamps and voltages must match."
                )
                return None
            if times:
                trains.append((train_id, times, voltages))
        return trains

    def _save_program(self):
        device = self._device
        if device is None:
            return

        path = filedialog.asksaveasfilename(
            parent=self._root,
            title="Save program",
            defaultextension=".json",
            initialfile="PulsePalProgram.json",
            initialdir=self._last_program_dir or None,
            filetypes=(("Pulse Pal program", "*.json"), ("All files", "*.*")),
        )
        if not path:
            return

        program = {
            "params": {
                name: list(values) for name, values in self._params.items()
            },
            "trigger_mode": list(self._trigger_mode),
            "custom_train_timestamps": list(self._custom_timestamps),
            "custom_train_voltages": list(self._custom_voltages),
            "device_info": dataclasses.asdict(device.info),
        }
        try:
            with open(path, "w", encoding="utf-8") as program_file:
                json.dump(program, program_file, indent=2)
        except OSError as exc:
            self._show_error(f"Failed to save the program:\n{exc}")
            return

        self._last_program_dir = path
        self._set_status("Program Saved")
        self.focus()

    def _open_program(self):
        path = filedialog.askopenfilename(
            parent=self._root,
            title="Open program",
            initialdir=self._last_program_dir or None,
            filetypes=(("Pulse Pal program", "*.json"), ("All files", "*.*")),
        )
        if not path:
            return

        try:
            with open(path, encoding="utf-8") as program_file:
                program = json.load(program_file)
            params = program["params"]
            new_params = {}
            for name, default in self._DEFAULT_OUTPUT_PARAMS.items():
                values = params.get(name, [default] * 4)
                if len(values) != 4:
                    raise ValueError(
                        f"{name} must have one value per output channel."
                    )
                new_params[name] = [float(value) for value in values]
            trigger_mode = [
                int(value) for value in program.get("trigger_mode", [0, 0])
            ]
            if len(trigger_mode) != 2:
                raise ValueError(
                    "trigger_mode must have one value per trigger channel."
                )
            timestamps = list(program.get("custom_train_timestamps", []))
            voltages = list(program.get("custom_train_voltages", []))
        except (OSError, ValueError, KeyError, TypeError) as exc:
            self._show_error(f"Failed to open the program:\n{exc}")
            return

        self._params = new_params
        self._trigger_mode = trigger_mode
        self._custom_timestamps = self._fit_custom_trains(timestamps)
        self._custom_voltages = self._fit_custom_trains(voltages)
        self._reset_selections()
        self._refresh()
        self._last_program_dir = path
        self._set_status("Program Opened")
        self.focus()

    def _fit_custom_trains(self, values):
        """Coerce a saved custom train list to this device's train count."""
        fitted = [""] * self._n_custom_trains
        for index, value in enumerate(values[:self._n_custom_trains]):
            if isinstance(value, (list, tuple)):
                value = ", ".join(_format_number(item) for item in value)
            fitted[index] = str(value)
        return fitted

    def _reset_selections(self):
        self._output_channel_var.set(1)
        self._trigger_channel_var.set(1)
        self._custom_train_list.selection_clear(0, "end")
        self._custom_train_list.selection_set(0)

    def _show_error(self, message):
        messagebox.showerror("Pulse Pal", message, parent=self._root)

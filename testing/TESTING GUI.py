#!/usr/bin/env python3
"""NEXUS arm controller tester, v3.

Continuous joints (hold Left or Right to move, no position tracking, no limits):
    C1 = D3 (MG996R)
    C2 = shoulder, D4 + D5 mirrored (one control)
    C3 = D6 (MG996R)
Positional joints (slider = angle, 0 to 180):
    P1 = D7, P2 = D8, P3 = D9 (MG90S)
FSR sensors:
    FSR 1 on A0, FSR 2 on A1, raw 0 to 1023, shown live.

Setup:
    pip install pyserial          (Arch also needs: sudo pacman -S tk)
    1. Upload nexus_arm_v3_fw/nexus_arm_v3_fw.ino to the Uno.
    2. Close the Arduino Serial Monitor.
    3. Run: python nexus_arm_v3.py

Safety: pressing Connect resets the Uno, and the positional servos go limp
for a moment. Support the arm before you connect.
"""
import time
import tkinter as tk
from tkinter import ttk

import serial
import serial.tools.list_ports

BAUD = 115200
TICK_MS = 20                  # held buttons, slider changes and serial reading run at this rate
FSR_MAX = 1023

CONT = [
    ("C1", "D3  MG996R"),
    ("C2 shoulder", "D4 + D5  mirrored"),
    ("C3", "D6  MG996R"),
]
POS = [
    ("P1", "D7  MG90S"),
    ("P2", "D8  MG90S"),
    ("P3", "D9  MG90S"),
]


class App(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("NEXUS Arm v3")
        self.resizable(False, False)

        self.ser = None
        self.held = {}                          # continuous joint -> direction
        self.speed = tk.IntVar(value=50)
        self.speed_dirty = False
        self.pos_on = tk.BooleanVar(value=False)
        self.angle = [tk.IntVar(value=90) for _ in POS]
        self.pos_dirty = [False] * len(POS)
        self.fsr_vars = [tk.StringVar(value="0") for _ in range(2)]
        self.fsr_bars = []
        self.status = tk.StringVar(value="Not connected")

        self.build_ui()
        self.refresh_ports()
        self.after(TICK_MS, self.tick)
        self.protocol("WM_DELETE_WINDOW", self.on_close)

    # ---------------------------------------------------------------- UI
    def build_ui(self):
        pad = {"padx": 8, "pady": 4}

        top = ttk.Frame(self)
        top.grid(row=0, column=0, sticky="ew", **pad)
        ttk.Label(top, text="Port").grid(row=0, column=0, padx=4)
        self.port_box = ttk.Combobox(top, width=20, state="readonly")
        self.port_box.grid(row=0, column=1, padx=4)
        ttk.Button(top, text="Refresh", command=self.refresh_ports).grid(row=0, column=2, padx=4)
        self.connect_btn = ttk.Button(top, text="Connect", command=self.toggle_connection)
        self.connect_btn.grid(row=0, column=3, padx=4)

        # ---- continuous joints
        cont = ttk.LabelFrame(self, text="Continuous joints: hold Left or Right to move")
        cont.grid(row=1, column=0, sticky="ew", **pad)

        ctl = ttk.Frame(cont)
        ctl.grid(row=0, column=0, columnspan=3, sticky="w", padx=4, pady=2)
        ttk.Label(ctl, text="Speed %").grid(row=0, column=0, padx=4)
        tk.Scale(ctl, from_=5, to=100, orient="horizontal", length=200,
                 variable=self.speed, command=self.on_speed).grid(row=0, column=1)
        tk.Button(ctl, text="STOP JOINTS", bg="#d93025", fg="white",
                  font=("TkDefaultFont", 11, "bold"), command=self.stop_joints
                  ).grid(row=0, column=2, padx=16)

        for j, (name, pins) in enumerate(CONT):
            r = j + 1
            ttk.Label(cont, text=f"{name}  {pins}", width=22).grid(row=r, column=0, padx=3, pady=3)

            left = tk.Button(cont, text="◀ Left", width=10)
            left.grid(row=r, column=1, padx=3)
            left.bind("<ButtonPress-1>", lambda e, j=j: self.press(j, -1))
            left.bind("<ButtonRelease-1>", lambda e, j=j: self.release(j))

            right = tk.Button(cont, text="Right ▶", width=10)
            right.grid(row=r, column=2, padx=3)
            right.bind("<ButtonPress-1>", lambda e, j=j: self.press(j, 1))
            right.bind("<ButtonRelease-1>", lambda e, j=j: self.release(j))

        # ---- positional joints
        pos = ttk.LabelFrame(self, text="Positional joints: slider = angle (0 to 180)")
        pos.grid(row=2, column=0, sticky="ew", **pad)

        ttk.Checkbutton(pos, text="Positional servos ON (attached and holding)",
                        variable=self.pos_on, command=self.on_pos_toggle
                        ).grid(row=0, column=0, sticky="w", padx=4, pady=2)
        ttk.Button(pos, text="Center 90", command=self.center_positional
                   ).grid(row=0, column=1, padx=4, sticky="w")

        for k, (name, pins) in enumerate(POS):
            r = k + 1
            ttk.Label(pos, text=f"{name}  {pins}", width=18).grid(row=r, column=0, padx=3, pady=3)
            tk.Scale(pos, from_=0, to=180, orient="horizontal", length=360,
                     variable=self.angle[k],
                     command=lambda v, k=k: self.on_angle(k)).grid(row=r, column=1, padx=3)
            ttk.Label(pos, textvariable=self.angle[k], width=4).grid(row=r, column=2, padx=3)

        # ---- FSR readings
        fsr = ttk.LabelFrame(self, text="FSR sensors (raw 0 to 1023, resting should be near 0)")
        fsr.grid(row=3, column=0, sticky="ew", **pad)
        for i, pin in enumerate(["A0", "A1"]):
            ttk.Label(fsr, text=f"FSR {i + 1}  {pin}", width=12).grid(row=i, column=0, padx=3, pady=3)
            bar = ttk.Progressbar(fsr, orient="horizontal", length=360,
                                  maximum=FSR_MAX, mode="determinate")
            bar.grid(row=i, column=1, padx=3)
            self.fsr_bars.append(bar)
            ttk.Label(fsr, textvariable=self.fsr_vars[i], width=6,
                      font=("TkDefaultFont", 11, "bold")).grid(row=i, column=2, padx=3)

        ttk.Label(self, textvariable=self.status, foreground="gray").grid(
            row=4, column=0, sticky="w", **pad)

    # ------------------------------------------------- continuous actions
    def press(self, j, direction):
        if not self.ser:
            self.status.set("Connect first")
            return
        self.held[j] = direction
        self.send(f"M,{j},{direction}")

    def release(self, j):
        if j in self.held:
            del self.held[j]
            self.send(f"M,{j},0")

    def stop_joints(self):
        self.held.clear()
        self.send("S")

    def on_speed(self, _value=None):
        self.speed_dirty = True

    # --------------------------------------------------- positional actions
    def on_angle(self, k):
        self.pos_dirty[k] = True

    def center_positional(self):
        for k in range(len(POS)):
            self.angle[k].set(90)
            self.pos_dirty[k] = True

    def mark_all_pos(self):
        self.pos_dirty = [True] * len(POS)

    def flush_pos(self):
        for k, dirty in enumerate(self.pos_dirty):
            if dirty:
                self.send(f"A,{k},{self.angle[k].get()}")
                self.pos_dirty[k] = False

    def on_pos_toggle(self):
        if not self.ser:
            self.pos_on.set(False)
            self.status.set("Connect first")
            return
        self.mark_all_pos()
        self.flush_pos()                 # set the targets before attaching
        self.send("E,1" if self.pos_on.get() else "E,0")

    # ----------------------------------------------------------- serial
    def refresh_ports(self):
        ports = [p.device for p in serial.tools.list_ports.comports()]
        self.port_box["values"] = ports
        if ports and not self.port_box.get():
            self.port_box.set(ports[0])

    def toggle_connection(self):
        if self.ser:
            self.disconnect()
            return
        port = self.port_box.get()
        if not port:
            self.status.set("Pick a port first")
            return
        try:
            self.ser = serial.Serial(port, BAUD, timeout=0.05)
        except serial.SerialException as err:
            self.status.set(f"Could not open {port}: {err}")
            return
        self.status.set("Connecting. The Uno resets now, so the positional servos go limp briefly.")
        self.update()
        time.sleep(2)
        self.ser.reset_input_buffer()
        self.connect_btn.config(text="Disconnect")
        self.status.set(f"Connected to {port}")
        self.push_settings()

    def push_settings(self):
        self.send(f"V,{self.speed.get()}")
        self.mark_all_pos()
        self.flush_pos()
        if self.pos_on.get():
            self.send("E,1")

    def disconnect(self):
        self.held.clear()
        if self.ser:
            try:
                self.ser.write(b"S\n")       # stop continuous joints before closing
                self.ser.close()
            except (serial.SerialException, OSError):
                pass
        self.ser = None
        self.connect_btn.config(text="Connect")
        self.status.set("Disconnected. Continuous joints stopped.")

    def send(self, text):
        if not self.ser:
            return
        try:
            self.ser.write((text + "\n").encode())
        except (serial.SerialException, OSError) as err:
            self.status.set(f"Serial error: {err}")
            self.disconnect()

    def handle_line(self, line):
        if line.startswith("F,"):
            parts = line.split(",")
            if len(parts) == 3:
                try:
                    values = [int(p) for p in parts[1:]]
                except ValueError:
                    return
                for i, v in enumerate(values):
                    self.fsr_vars[i].set(str(v))
                    self.fsr_bars[i]["value"] = v
        else:
            self.status.set(line)

    def tick(self):
        if self.ser:
            if self.speed_dirty:
                self.send(f"V,{self.speed.get()}")
                self.speed_dirty = False
            for j, direction in list(self.held.items()):
                self.send(f"M,{j},{direction}")      # keep-alive while held
            self.flush_pos()
            try:
                while self.ser and self.ser.in_waiting:
                    line = self.ser.readline().decode(errors="ignore").strip()
                    if line:
                        self.handle_line(line)
            except (serial.SerialException, OSError) as err:
                self.status.set(f"Serial error: {err}")
                self.disconnect()
        self.after(TICK_MS, self.tick)

    def on_close(self):
        self.disconnect()
        self.destroy()


if __name__ == "__main__":
    App().mainloop()

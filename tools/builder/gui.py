"""Zelda Genesis converter window.

Drop the NES ROM and the Zelda Redux v3.3.3 patch (or its release .zip)
onto Converter.bat, or start it and pick the files. The window checks the
files, installs the Python requirements when asked, runs
tools/builder/build.py and writes the Genesis ROM next to the NES ROM
(or wherever chosen). Input files are only read.

Usage: python tools/builder/gui.py [files...]
"""
from __future__ import annotations

import queue
import sys
import threading
import webbrowser
from pathlib import Path

import tkinter as tk
from tkinter import filedialog, messagebox, ttk

sys.path.insert(0, str(Path(__file__).resolve().parent))
import converter_core as core  # noqa: E402

REDUX_URL = core.build.REDUX_DOWNLOAD


class App:
    def __init__(self, root: tk.Tk, files: list[Path]):
        self.root = root
        self.inp = core.Inputs()
        self.events: queue.Queue = queue.Queue()
        self.busy = False
        root.title("Zelda Genesis Converter")
        root.minsize(640, 420)

        frm = ttk.Frame(root, padding=10)
        frm.pack(fill="both", expand=True)
        frm.columnconfigure(1, weight=1)

        self.rom_var = tk.StringVar()
        self.redux_var = tk.StringVar()
        self.out_var = tk.StringVar()
        rows = (("Zelda NES ROM", self.rom_var, self.pick_rom),
                ("Zelda Redux v3.3.3 patch", self.redux_var, self.pick_redux),
                ("Save Genesis ROM as", self.out_var, self.pick_output))
        for r, (label, var, cmd) in enumerate(rows):
            ttk.Label(frm, text=label).grid(row=r, column=0, sticky="w", pady=2)
            ttk.Entry(frm, textvariable=var).grid(row=r, column=1, sticky="ew", padx=6)
            ttk.Button(frm, text="Browse...", command=cmd).grid(row=r, column=2)
        link = ttk.Label(frm, text="Get Zelda Redux v3.3.3 (download the patch or .zip)",
                         foreground="#2a6ad8", cursor="hand2")
        link.grid(row=3, column=1, sticky="w", padx=6)
        link.bind("<Button-1>", lambda _e: webbrowser.open(REDUX_URL))

        bar = ttk.Frame(frm)
        bar.grid(row=4, column=0, columnspan=3, sticky="ew", pady=(10, 4))
        bar.columnconfigure(1, weight=1)
        self.go = ttk.Button(bar, text="Convert", command=self.convert)
        self.go.grid(row=0, column=0)
        self.prog = ttk.Progressbar(bar, mode="determinate")
        self.prog.grid(row=0, column=1, sticky="ew", padx=8)
        self.deps = ttk.Button(bar, text="Install requirements", command=self.install)
        self.status = ttk.Label(frm, text="")
        self.status.grid(row=5, column=0, columnspan=3, sticky="w")

        self.log = tk.Text(frm, height=14, wrap="none", state="disabled")
        self.log.grid(row=6, column=0, columnspan=3, sticky="nsew", pady=(6, 0))
        frm.rowconfigure(6, weight=1)

        missing = core.missing_modules()
        if missing:
            self.deps.grid(row=0, column=2)
            self.say(f"Missing Python modules: {', '.join(missing)}. "
                     "Press 'Install requirements' (needs internet once).")
        if files:
            self.add(files)
        root.after(100, self.pump)

    # ---- inputs ----
    def add(self, paths: list[Path]):
        self.inp = core.assign(paths, self.inp)
        self.refresh()
        for note in self.inp.notes[-len(paths):]:
            self.write(note)

    def refresh(self):
        self.rom_var.set(str(self.inp.rom or ""))
        self.redux_var.set(str(self.inp.redux_rom or self.inp.redux_patch or ""))
        self.out_var.set(str(self.inp.output or ""))
        if not self.inp.rom:
            self.say("Choose your Zelda NES ROM (The Legend of Zelda (USA) PRG0).")
        elif not (self.inp.redux_rom or self.inp.redux_patch):
            self.say("Choose the Zelda Redux v3.3.3 patch or its release .zip.")
        else:
            self.say("Ready.")

    def pick(self, title, types):
        name = filedialog.askopenfilename(title=title, filetypes=types)
        if name:
            self.add([Path(name)])

    def pick_rom(self):
        self.pick("Zelda NES ROM", [("NES ROM", "*.nes"), ("All files", "*.*")])

    def pick_redux(self):
        self.pick("Zelda Redux patch or release zip",
                  [("Redux patch", "*.ips *.bps *.zip"), ("NES ROM", "*.nes"), ("All files", "*.*")])

    def pick_output(self):
        name = filedialog.asksaveasfilename(title="Save Genesis ROM as", defaultextension=".md",
                                            filetypes=[("Genesis ROM", "*.md *.bin *.gen")])
        if name:
            self.inp.output = Path(name)
            self.refresh()

    # ---- actions ----
    def convert(self):
        if self.busy:
            return
        if self.out_var.get():
            self.inp.output = Path(self.out_var.get())
        if not self.inp.ready():
            messagebox.showerror("Missing input", self.status.cget("text"))
            return
        if core.missing_modules():
            messagebox.showerror("Missing requirements", "Press 'Install requirements' first.")
            return
        self.start(core.command(self.inp), "Converting...")

    def install(self):
        if not self.busy:
            self.start(core.install_command(), "Installing requirements...")

    def start(self, argv, text):
        self.busy = True
        self.go.state(["disabled"])
        self.prog.configure(value=0, maximum=1)
        self.say(text)
        self.write("> " + " ".join(argv))
        threading.Thread(target=self.worker, args=(argv,), daemon=True).start()

    def worker(self, argv):
        try:
            rc = core.run(argv,
                          on_line=lambda s: self.events.put(("line", s)),
                          on_step=lambda i, n, label: self.events.put(("step", (i, n, label))),
                          on_done=lambda path, sha: self.events.put(("done", (path, sha))))
        except OSError as e:
            self.events.put(("line", f"ERROR: {e}"))
            rc = 1
        self.events.put(("exit", rc))

    def pump(self):
        try:
            while True:
                kind, val = self.events.get_nowait()
                if kind == "line":
                    if not val.startswith("@@"):
                        self.write(val)
                elif kind == "step":
                    i, n, label = val
                    self.prog.configure(maximum=n, value=i - 1)
                    self.say(f"Step {i} of {n}: {label}")
                elif kind == "done":
                    path, sha = val
                    self.prog.configure(value=self.prog.cget("maximum"))
                    messagebox.showinfo("Done", f"Genesis ROM written:\n{path}\n\nsha256 {sha}")
                elif kind == "exit":
                    self.busy = False
                    self.go.state(["!disabled"])
                    if val == 0:
                        self.prog.configure(value=self.prog.cget("maximum"))
                        if not core.missing_modules():
                            self.deps.grid_remove()
                        self.say("Finished.")
                    else:
                        self.say(f"Failed (exit {val}). The log above shows the first error.")
        except queue.Empty:
            pass
        self.root.after(100, self.pump)

    # ---- output ----
    def say(self, text):
        self.status.configure(text=text)

    def write(self, text):
        self.log.configure(state="normal")
        self.log.insert("end", text + "\n")
        self.log.see("end")
        self.log.configure(state="disabled")


def main() -> int:
    root = tk.Tk()
    App(root, [Path(a) for a in sys.argv[1:]])
    root.mainloop()
    return 0


if __name__ == "__main__":
    sys.exit(main())

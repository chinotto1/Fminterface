#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Sequencer, arpeggiator and nightly retrospective-capture checks. No device access."""
from pathlib import Path
import os, subprocess
app = Path(__file__).resolve().parents[1]
out = app.parents[2] / 'build' / 'music-tests'
out.mkdir(parents=True, exist_ok=True)
exe = out / 'music_test'
subprocess.run([os.environ.get('CC', 'cc'), '-std=c99', '-Wall', '-Wextra', '-Werror',
    '-Wno-unused-function', '-g', str(app / 'tests/music_test.c'), '-o', str(exe)], check=True)
subprocess.run([str(exe)], check=True)

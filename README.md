# VoiceQC

Prototype technical verification and quality control tool for voice
recordings, developed as part of a KTH master's thesis with Neovius Data
och Signalsystem AB on VoiceJournal/Soundswell, in support of MDR Class IIa
certification.

## What this is

A standalone, plain C++ command line tool (no MFC, no dependency on the
VoiceJournal/Soundswell codebase) that estimates room acoustic quality
from a recording, as a step toward automated LOJ compliance checking.

Currently implemented:

- A minimal WAV file reader (`WavReader`), supporting 16/24/32 bit PCM
  integer and 32 bit IEEE float, mono or multi channel (downmixed to
  mono by averaging channels).
- RT60 estimation using the manual Swell style method: locate the
  impulse peak, find the time to decay to about 10 percent of peak
  amplitude (roughly -20 dB), and multiply by 3.
- RT60 estimation using Schroeder backward integration of the squared
  impulse response, with a linear fit to the decay curve in dB.
- LOJ Kravniva (requirement level) compliance classification
  (Godkand / Osaker / Underkand) based on averaged RT60 estimates from
  one or more handclap recordings, using the stricter above 500 Hz
  thresholds (level 1: 0.50 s, level 2: 0.25 s, level 3: 0.10 s).

Planned next (see thesis project plan):

- F0 and SPL/Leq per frame extraction, matching VoiceJournal's own
  TalF0/TalSPL family of displayed parameters.
- Replicating the Leq minus 20 dB voicing threshold used internally by
  FoX, and validating estimates against real Soundswell output.
- Background noise / SNR estimation and a fuller LOJ compliance
  classifier covering the electroacoustic and storage requirements as
  well as room acoustics.

## Building

This project uses CMake so the same source tree can be opened from both
Visual Studio Code (with the CMake Tools extension) on macOS/Linux, and
from Visual Studio on Windows.

Command line build:

```
mkdir build
cd build
cmake ..
cmake --build .
```

Then run:

```
./VoiceQC path/to/recording.wav [lojLevel]
```

`lojLevel` is optional (1, 2 or 3); when given, the tool also prints a
LOJ compliance classification for that single recording.

## Project layout

```
VoiceQC/
  CMakeLists.txt
  include/            public headers
  src/                implementation and main.cpp
  README.md
```

## Development workflow

- Source of truth lives on GitHub; work happens both from VS Code (Mac)
  and Visual Studio (work PC), each opening the same CMake project.
- `.gitattributes` normalizes line endings across platforms.
- `.gitignore` excludes IDE and build artifacts from both toolchains.

# TinySoundFont2
SoundFont2 synthesizer library in a single C/C++ file

TinySoundFont2 is a continuation of the original [TinySoundFont](https://github.com/schellingb/TinySoundFont) project: fixing bugs, optimizing it, expanding it to 100% GM1 compatibility, full support of the [General MIDI Level 1](https://midi.org/midi-1-0-detailed-specification) and [SoundFont 2.01](https://www.synthfont.com/SFSPEC21.PDF) technical specifications, including support of all MIDI CC messages, SoundFont generators, modulators, and effects.

## Overview

TinySoundFont is a software synthesizer using SoundFont2 sound bank files.

The library is a single C header file so it is extremely simple to integrate in your C/C++ projects.

```c++
#define TSF_IMPLEMENTATION
#include "tsf.h"

...

tsf* TinySoundFont = tsf_load_filename("soundfont.sf2");
tsf_set_output(TinySoundFont, TSF_MONO, 44100, 0); //sample rate
tsf_note_on(TinySoundFont, 0, 60, 1.0f); //preset 0, middle C
short HalfSecond[22050]; //synthesize 0.5 seconds
tsf_render_short(TinySoundFont, HalfSecond, 22050, 0);
```

The library code is based on [SFZero by Steve Folta](https://github.com/stevefolta/SFZero).

## Documentation

The API documentation can be found on [top of the library source code](https://github.com/anzz1/TinySoundFont2/blob/master/tsf.h).

There are also [examples available](https://github.com/anzz1/TinySoundFont2/tree/master/examples) which come with a sample SoundFont file and build and play sound on Win32, Win64, Linux and MacOSX with no further dependencies.

In-depth technical details available in [TECHNICAL_DETAILS.md](https://github.com/anzz1/TinySoundFont2/tree/master/TECHNICAL_DETAILS.md).

## Dependencies

C standard libraries for fopen, math and malloc (can be removed by providing custom functions with #defines).

## License

TinySoundFont is available under the [MIT license](https://choosealicense.com/licenses/mit/).

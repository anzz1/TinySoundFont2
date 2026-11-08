# TinySoundFont2 technical details

This document describes how `tsf.h` implements the SoundFont 2.01 specification, General MIDI and the parts of SoundFont 2.04 it supports. It also lists the cases where a specification is ambiguous or wrong and explains which interpretation was chosen.

Section numbers like *8.1.2* refer to the SoundFont 2.01 specification (SFSPEC21.PDF, 1998 revision).

## Supported standards

| Standard | Support |
|---|---|
| General MIDI 1 | Complete (see [General MIDI](#general-midi) for what the caller does) |
| SoundFont 2.01 | Complete: all generators, modulators, default modulators and the NRPN real-time control |
| SoundFont 2.04 | Linked modulators and the absolute value transform. 24-bit samples (`sm24` chunk) are not supported; the 16-bit sample data is played, which 2.04 permits |
| AWE32 NRPN | All 27 parameters (MSB 127), see [AWE32 NRPN](#awe32-nrpn) |
| GM2, GS, XG | Not supported yet (GS-style bank select with CC0 only does work) |

## Generators

- **All generators are supported**, including the instrument-level `keynum` and `velocity` generators. These make the note play, and the modulators see it, as the given key or velocity.
- **Instrument-only generators** are ignored when they appear in presets (8.5). These are the sample address offsets, `sampleModes`, `exclusiveClass`, `overridingRootKey`, `keynum` and `velocity`.
- **Address offsets:** a generator in a local zone replaces the one in the global zone, including the sample address offsets. The fine and coarse (×32768) offsets are added to the sample header's points, and every point is kept inside the sample's own data (between the header's start and end), so no offset reaches into a neighboring sample.
  - The loop may begin before the start point: playback runs into the loop and then loops. A start after the loop end begins inside the loop. A loop whose end lies beyond the played end, or that is empty, is dropped.
- **Modulated address offsets:** modulators can move the start, end and loop points (for example velocity → startAddrsOffset). They are evaluated once when the note starts, and the same rules apply: all points stay inside the sample and the end stays after the start.
- **Range clamping:** preset values are added to instrument values. Modulator outputs are added next, and only then is the total clamped to the generator's range (10.5). So a negative preset attenuation can partly cancel attenuation from velocity or CC7.
- **Scale tuning** only scales the key's distance from the root key. Coarse tune, fine tune (which includes the pitch wheel modulator) and the channel tuning are added on top, unscaled.
- **Exclusive class** quickly ends other notes in the same preset and channel that have the same class (8.1.2 #57). It never ends the voices started by the same note, such as the other half of a stereo sample.

### Envelopes

- **Volume envelope:**
  - The attack is linear in amplitude, so it looks convex on a dB scale.
  - Decay and release are linear in dB. Their times are the time for a 100 dB drop (8.1.2 #36, #38).
  - Release: the release takes at least 10 ms (`TSF_FASTRELEASETIME`), so the default release of 1 ms (−12000 timecents) and other short ones don't click at a note off. Without it, a 1 ms release adds high-frequency energy at the note off that grows as the render block shrinks: 2.5 dB more than a 10 ms release with the default 64-sample block, 17 dB more (a tick on every note off of a sustained sound) with an 8-sample block. The modulation envelope's release stays exact.
  - The voice ends once the envelope is 100 dB below full scale (8.1.2), including when it would otherwise sustain at that level. A released voice also ends once its total gain (envelope plus attenuation, allowing for the modulation LFO's loudest point) is 100 dB down.
- **Modulation envelope:**
  - The attack is convex (8.1.2 #26), using the same curve as the convex modulator source.
  - Decay and release are linear, and their times are for a full-scale change. A release from 50% therefore takes half the release time (8.1.2 #30).
- **Times** are converted from timecents exactly: the minimum and default of −12000 is 1 ms (8.1.3, 8.5), so the default attack ramps in over 1 ms instead of starting with a step.
  - **−32768 means zero time** for the delays (both LFOs and both envelopes), attacks and holds, as 8.1.2 says by convention. It counts as such when the preset and instrument values and the modulators add up to −32768 or less (the hold after its key number scaling). Decay and release have no such convention, so −32768 there is clamped to 1 ms.
- **Key number scaling** of hold and decay is applied in timecents before the conversion to seconds.

### Filter

- **Filter type:** a 2-pole (12 dB/octave) lowpass biquad.
- **Resonance** is the height of the peak above the DC gain, in cB. The DC gain is lowered by half of the resonance (8.1.2 #9, 9.1.3): for 100 cB the DC gain is −5 dB and the peak is +5 dB.
  - The filter's Q value is solved from the peak height, so zero resonance gives a flat Butterworth response (−3 dB at the cutoff).
- **Bypass:** at the maximum cutoff (13500 cents) with zero resonance the filter is skipped (8.1.2 #8). This is the default, so most voices pay nothing for the filter.
- **Cutoff limits:** the cutoff, also when the LFO, envelope or modulators change it, is kept between 20 Hz and 45% of the sample rate.
- **Starting mid-note:** when modulation brings a bypassed filter's cutoff below 13500 cents (or adds resonance) while the note plays, the filter starts in its steady state for the current sample instead of from zero, so the output doesn't step (a click).

### Pan

- **Pan law:** constant power, `left = cos(π/2·p)`, `right = sin(π/2·p)`, which is the MIDI default pan formula (RP-036).

## Modulators

- **Inputs:**
  - Controllers come from `tsf_channel_midi_control`, `tsf_channel_set_pitchwheel`, `tsf_channel_set_pitchrange`, `tsf_channel_set_pressure` and `tsf_channel_set_poly_pressure`.
  - Velocity and key number are taken from the note.
- **Normalization:** controllers are divided by their range plus one: a CC value by 128, the pitch wheel by 16384.
  - **Pitch wheel sensitivity exception:** this source is divided by 127, so the default pitch wheel modulator (12700 cents) bends by exactly the RPN 0 range (±2 semitones by default). Read literally, the spec's 127/128 mapping would give 198.4 cents.
- **14-bit controllers:** CC1–CC31 (except data entry, CC6) combine with their LSB (CC33–CC63) once the LSB has been sent. An MSB sets its LSB back to 0 (MIDI 1.0), so a later MSB alone is exact.
  - **LSBs as sources:** a modulator with CC32–CC63 as a source is ignored, as 8.2.1 says, since those are reserved for the LSBs of CC0–CC31 (CC33 is already part of a CC1 source).
- **Curves:** concave is `−40/96·log10(1−x)` and convex is `1 + 40/96·log10(x)`, as in FluidSynth 2.3 and later. For bipolar sources the curve is mirrored around the center.
- **Merging (9.5):**
  - An identical modulator replaces the default one or the global zone's; identical means the same sources, destination and transform.
  - A preset-level modulator adds its amount to an identical instrument-level one.
  - Modulators with a zero amount are dropped when the font loads. Fonts use them to cancel default modulators, so dropping them costs nothing and changes nothing.

### Default modulators (8.4)

All of them are implemented except for two deliberate deviations:

- **CC10 to pan** uses an amount of 500 instead of 1000. With 1000 the pan would already be fully left at CC10 = 32 and fully right at 96. FluidSynth uses and the SoundFont-Spec-Test file expects 500.
- **Velocity to filter cutoff** is left out entirely:
  - The SF2.01 version (amount source 0x0D02, a negative switch) is a mistake in the spec: it makes the cutoff jump at velocity 64.
  - SF2.04 changed it to a plain linear modulator. But SF2.01 fonts don't know they need to cancel it, so adding it would darken them.
  - A font that wants velocity-dependent filtering can define the modulator itself.

### Linked modulators (SoundFont 2.04)

- **How links work:** a modulator whose destination has bit 15 set feeds the modulator at that index in the same zone. A modulator with source 127 ("link") takes the sum of the modulators linked into it.
- **Merge order:** linked modulators are merged in link order, so a link can be compared as an index into the merged list. Two linked modulators are identical when they link to the same merged modulator.
- **Invalid links:** links to ignored modulators, out-of-range indices and cycles are dropped (2.04 §7.4).
- **Absolute value transform:** transform 2 is supported as well.

## SoundFont 2.01 NRPN (9.6)

- **Selecting a generator:** NRPN MSB (CC99) = 120, then NRPN LSB (CC98).
  - LSB values 100, 101 and 102 add 100, 1000 and 10000 to the generator number.
  - An LSB of 0–99 completes the number, so `100, 50` selects generator 150.
- **Applying a value:** data entry is applied when the MSB (CC6) arrives, combined with the most recent LSB (CC38). (For the RPNs the MSB sets the LSB to 0 first, as MIDI 1.0 requires, and an LSB sent after it completes the value.) The value minus 8192 is multiplied by a per-generator scale and added to the generator like a modulator output.
- **Scale:** the spec halves the useful range until it fits 8192 steps. The scale values are 1, 2 or 4 units per step.
- **Ignored targets:** generators that are not real-time parameters are ignored. These are the sample offsets, key/velocity ranges, `keynum`, `velocity`, `sampleModes`, `scaleTuning`, `exclusiveClass` and the root key.
- **Interaction with RPNs:** selecting an RPN afterwards takes over the data entry. The [AWE32 NRPN](#awe32-nrpn) set (MSB 127) is supported separately. Other NRPN MSB values are ignored.
- **Reset:** Reset All Controllers (CC121) clears the NRPN offsets.

## AWE32 NRPN

The Sound Blaster AWE32 has its own NRPN set. MIDI files made for the AWE32 and its successors use it, mostly for the filter. 9.6 notes that it is not compatible with the SoundFont 2.01 set, meaning the two number, scale and apply their values differently. They do not overlap, though: each set has its own NRPN MSB, so TSF supports both, and a file can use either.

Creative never published the full unit table; the conversions follow the AWE32 programmer's guide as implemented by the Linux emux driver (`emux_nrpn.c`), and the filter follows the Audigy, the only hardware that has been measured.

- **Selecting a parameter:** NRPN MSB (CC99) = 127, then NRPN LSB (CC98) = 0–26. Higher LSB values select nothing.
- **Applying a value:** data entry applies on either byte, MSB (CC6) or LSB (CC38), because AWE32 files often send only the LSB. The parameters with an 8-bit (or 7-bit) range use the low bits of the value; the others use the 14-bit value minus 8192.
- **Set, not add:** the value replaces the generator value of every note on the channel, including notes already playing. The one exception is the initial pitch (16), which adds cents to the fine tune.

| | SoundFont 2.01 NRPN | AWE32 NRPN |
|---|---|---|
| NRPN MSB (CC99) | 120 | 127 |
| NRPN LSB (CC98) | generator number, with 100/101/102 steps for numbers above 99 | parameter 0–26, in its own order |
| Effect | adds an offset to the generator | replaces the generator value |
| Units | generator units × a scale of 1, 2 or 4 | AWE32 units (ms, 0.084 Hz, 9.375 cents, …) |
| Applied on | the data entry MSB | either data entry byte |

- **Using both:** the CC99 value decides which set the following CC98 and data entry belong to, and selecting one set (or an RPN) replaces the previous selection. Neither specification says how the two combine on the same generator. TSF applies the AWE32 value as the base, then the modulators and the SoundFont 2.01 NRPN offset add to it (9.6 defines that offset like a modulator output). For example, an AWE32 initial pitch of +100 cents and an NRPN fine tune of +50 give +150 cents. In practice a MIDI file uses only one of the two sets.
- **Conversions:**

| Parameters | Generator | Conversion |
|---|---|---|
| 0, 2, 4, 10: LFO 1/2, mod env, vol env delay | delays | 0–5900 ms to timecents |
| 5, 7, 9, 11, 13, 15: attacks, decays, releases | attacks, decays, releases | 0–5940 ms to timecents |
| 6, 12: mod env, vol env hold | holds | 0–8191 ms to timecents, 0 is the minimum |
| 1, 3: LFO 1/2 frequency | `freqModLFO`, `freqVibLFO` | 7 bits × 0.084 Hz, 0 is the minimum |
| 8: mod env sustain | `sustainModEnv` | (127 − value) / 127 × 100% |
| 14: vol env sustain | `sustainVolEnv` | (127 − value) × 0.75 dB |
| 16: initial pitch | added to `fineTune` | cents |
| 17, 18, 19: LFO 1 / LFO 2 / mod env to pitch | `modLfoToPitch`, `vibLfoToPitch`, `modEnvToPitch` | signed 8 bits × 9.375 cents |
| 20: LFO 1 to volume | `modLfoToVolume` | 8 bits × 0.1875 dB |
| 21: initial filter cutoff | `initialFilterFc` | 100 Hz + value / 128 × 7646 cents (Audigy) |
| 22: initial filter Q | `initialFilterQ` | value / 128 × 20 dB (Audigy) |
| 23: LFO 1 to filter cutoff | `modLfoToFilterFc` | signed 7 bits × 56.25 cents |
| 24: mod env to filter cutoff | `modEnvToFilterFc` | signed 8 bits × 56.25 cents |
| 25, 26: chorus, reverb send | `chorusEffectsSend`, `reverbEffectsSend` | 8 bits / 255 × 100% |

- **Verified:** the filter cutoff and Q against the SoundFont-Spec-Test "NRPN test - filter" recordings of an Audigy 2. The resonant peak follows the recorded one from 150 Hz to 8 kHz; the AWE32's own 8-bit filter curve differs and is not emulated.
- **Not verified on hardware:** the other conversions follow the emux driver, including its reading of the time units as milliseconds.
- **Reset:** Reset All Controllers (CC121) clears the AWE32 settings.

## MIDI channel behavior

- **Sustain pedal (CC64) and Hold 2 (CC69):** Hold 2 holds notes the same way as sustain.
- **Sostenuto (CC66):** pressing it captures the notes whose keys are down. Releasing it ends those whose keys were released in the meantime.
- **Soft pedal (CC67):** not part of SoundFont 2.01. Notes started while it is pressed are up to 6 dB quieter and up to 360 cents darker.
- **Mono mode and legato:**
  - Mono mode (CC126/127) and the legato footswitch (CC68) make a key pressed while another is held continue the note at the new pitch, without restarting the envelope.
  - Releasing that key returns to the previously held key; up to 8 held keys are remembered.
  - In mono mode, pressing the key that is already sounding again restarts the note (the old one is released), so a channel keeps one note.
  - A note kept by the sustain pedal or hold 2 after its key was let go counts as held too, so the next key continues it (one note sounds, as on a mono synth). A note kept by sostenuto stays as it is.
- **Portamento:**
  - CC65 switches it on. Notes glide from the previous note.
  - CC84 (Portamento Control) gives the key the next note glides from, also while CC65 is off (MIDI 1.0). If a note on that key is sounding, it glides to the new key without re-attacking and no new voice starts.
  - The glide time is CC5 (MSB) / CC37 (LSB) as a 14-bit value in milliseconds.
- **RPNs:**
  - RPN 0: pitch bend range (MSB in semitones, LSB in cents).
  - RPN 1: fine tuning (100/8192 cent steps).
  - RPN 2: coarse tuning (semitones).
  - RPN 5: modulation depth range. The default is 50 cents, and it scales the modulation wheel's pitch modulators.
  - The RPN null value (127/127) is respected.
- **Data increment/decrement (CC96/97):** steps the selected parameter by one LSB step, or by one semitone for coarse tuning (RP-018). The pitch bend range (RPN 0) steps by one cent and carries between its cents (LSB, 0–99) and semitones (MSB).
- **Minimum note length:** a note off that comes less than 10 ms after its note on (`TSF_MIN_NOTE_LENGTH`) is carried out when the note has played 10 ms, a deviation from the spec. The spec releases at once. Otherwise a note shorter than its delay and attack, such as a drum hit with the note off at the same time as the note on, would release from (nearly) zero and not be heard. Pedals, All Notes Off and the other note ends work the same way; All Sound Off and the quick fades are immediate.
- **All Notes Off:** CC123 releases the notes, but the pedals keep holding them. The mode messages CC124–127 also act as All Notes Off (MIDI 1.0).
- **All Sound Off:** CC120 ends the notes with a 10 ms fade instead of a hard cut, to avoid clicks.
- **Reset All Controllers (CC121)** follows RP-015:
  - Kept: volume, pan, bank, program, effect depths, sound controllers and the RPN values (pitch range and tuning).
  - Reset: everything else, including poly pressure, the NRPN offsets and the AWE32 settings.
- **Bank select:** works with CC0 alone (GS style) as well as with combined MSB/LSB banks. A CC32 sent alone later keeps the MSB.

## General MIDI

`tsf.h` covers everything General MIDI 1 asks of the synthesizer:

- **Polyphony:** dynamic, or fixed with `tsf_set_max_voices`.
  - A new note takes a free voice. Notes only end as the specs define (note off, pedals, CC120/CC123, the volume envelope, the sample end): a held note muted by the volume (master, CC7, CC11) keeps its voice and comes back when the volume is turned up again.
  - If there is no free voice, dynamic polyphony adds voices. With a fixed number of voices one is ended, in this order:
    - the released voice furthest into its release, judged by its level: the envelope falls in dB, so this doesn't favor long release tails, and the volume counts since a released note can't come back;
    - else the oldest note kept by a pedal after its key was let go;
    - else the oldest note.
- **Channels:** 16 or more. A channel is created by its first message, a note-on included, at preset index 0 and the defaults below.
- **Messages:** velocity, channel pressure and poly pressure, and pitch bend (±2 semitones by default).
- **Controllers:** CC1, 7, 10, 11, 64, 121 and 123.
- **RPNs:** 0, 1 and 2.
- **Channel defaults:** volume 100, expression 127, pan and balance centered, the sound controllers CC71–78 at 64 (GM2: no change), reverb and chorus sends (CC91/CC93) at 0.

`tsf.h` has no MIDI byte parser, so the application handles the following:

- **Drum channel:** for MIDI channel 10 (channel index 9), select the standard kit at startup with `tsf_channel_set_bank_preset(f, 9, 128, 0)`, and pass every program change on that channel as `tsf_channel_set_presetnumber(f, 9, program, 1)`. The drum flag selects that kit from bank 128, or drum kit 0 if the font doesn't have it.
- **Reset SysEx (GM Reset, GM2 System On, GS Reset, XG System On):** call `tsf_reset`, which fades out all notes and returns every channel to its initial state, then select the drum kit on channel 10 again. GM2, GS and XG also set a reverb send of 40: send CC91 = 40 on every channel after those resets (General MIDI 1 doesn't).
- **Master Volume SysEx:** call `tsf_set_volume`. It changes the sounding notes as well as new ones.
- **Instrument set:** the 128 instruments and the drum kit come from the SoundFont used.

## Effects

- **Effect sends:** the `chorusEffectsSend` and `reverbEffectsSend` generators and the CC91/CC93 modulators are evaluated per voice. `tsf_get_voice_effects` reads them, which can drive an external effects unit.
- **Built-in effects:** a Freeverb-style reverb and a stereo chorus.
  - They are enabled by default and use about 66 KB of memory at 44.1 kHz.
  - Disabling both with `tsf_set_reverb` and `tsf_set_chorus` frees that memory.
- **CPU use only while needed:**
  - Voices that send nothing take a render path without send buffers.
  - The effects don't run at all while no voice sends to them.
  - Once nothing sends, they stop after their output has stayed below −100 dB for 0.1 s, and their buffers are cleared.
  - A font that zeroes its send modulators, such as a "GS wavetable" style font, therefore never runs the effects.

## Rendering and performance

- **Sample data:** kept as 16-bit integers as in the font, half the memory of floats.
- **Interpolation:** linear, with the playback position as a 32.32 fixed-point number. It runs in integers with a 15-bit fraction and converts only the result to float (the 1/2^30 scale is part of the gains), which is faster than converting both samples. Compared with interpolating in floats, the difference is about −100 dB, below the 16-bit rounding noise of the samples themselves.
- **Block processing:** envelopes, LFOs, filter coefficients and pitch are updated every 64 samples (`TSF_RENDER_EFFECTSAMPLEBLOCK`). The gain ramps linearly within each block, which avoids zipper noise and clicks.
- **Silent blocks:** during an envelope delay or at zero volume, a voice that bypasses the filter skips interpolation and only advances its position.
- **Inner loop:** within a block, the samples that can't reach the loop end or the sample end are rendered as one run without the end test and the loop wrap. There is one loop with the filter and one without, so voices that bypass the filter don't test for it per sample. A step longer than the loop (a short loop played very high) still wraps into the loop.
- **Filter precision:** the biquad runs in double precision. Single precision would be about 10% faster on filtered voices, but at low cutoffs (20–50 Hz, especially with resonance or at 96 kHz) its error comes within 4–44 dB of the signal.
- **Effects:** each comb, allpass and chorus line runs over the whole effects block with its state in local variables, the four combs of a side together so their feedback paths overlap.
- **Sample buffer:** it has one extra zero sample at the end, so interpolation never reads past the sample data.
- **Output settings:** `tsf_set_output` and `tsf_set_volume` change the global gain of the sounding notes too. A different sample rate fades out the sounding notes (10 ms), because their pitch, envelopes, LFOs and filter are computed in samples at the old rate; new notes play at the new rate.
- **Output formats:** interleaved and unweaved stereo produce identical samples, and mono has the same power as the two stereo channels together. `tsf_render_short` saturates at full scale, also when mixing into the buffer.

## Compressed fonts (SFO and SF3)

With `stb_vorbis.c` included before `tsf.h`, two compressed formats load as well:

- **SFO** (SFOTool): the whole sample data is one Ogg Vorbis stream in an `smpo` chunk. It decodes to the original sample positions, so every sample header and loop point stays valid.
- **SF3** (MuseScore layout): each sample is its own Ogg Vorbis stream (sample type flag 0x10, start and end in bytes, loop points relative to the sample start). Samples can also be left as raw PCM in the same file.
- **Damaged files:** a sample whose stream can't be opened, or whose byte range lies outside the data, is left out (an SFO whose single stream can't be opened fails to load). A truncated stream plays what decodes; regions whose samples lie past that are dropped.
- **RIFF padding:** a chunk of odd size, common for compressed data, is followed by a pad byte as RIFF specifies; files written without it (as SFOTool does) load too.
- **Both stb_vorbis APIs** work: the default pull API and the pushdata-only build (`STB_VORBIS_NO_PULLDATA_API`).
- **Sample data:** a plain .sf2 loads as it does without Vorbis support, its 16-bit samples used as they are. Compressed fonts are decoded to floats and then converted to 16-bit once while loading, rounded. Lossy compression occasionally decodes a sample slightly beyond full scale; such samples are clipped to full scale, which brings them closer to the original 16-bit samples.

## API arguments

- **Out of range values:** channel numbers outside 0 to `TSF_MAX_CHANNELS` - 1 and preset indices that don't exist are rejected (the function returns 0). Controller values are clamped to 0–127, the pitch wheel to 0–16383, pressure to 0–127.
- **Velocity:** 0, a negative value or NaN is a note off; any positive velocity plays, at MIDI velocity 1 or more.
- **Float arguments:** an infinite or NaN pan, volume, pitch range, tuning or gain is ignored. The global gain is kept between −200 and +100 dB, so changing it back restores the sounding notes exactly. Effect settings are clamped (NaN as 0).
- **Pitch:** the playback speed is capped at 2^23 times the sample rate, so no tuning can overflow the 32.32 fixed-point position.

## Compile options

- **`EMU8K_COMPAT` (default 1):** scales the `initialAttenuation` generator (but not the modulators) by 0.4, as Sound Blaster cards and FluidSynth do, because most SoundFonts were made for that behavior.
  - Set it to 0 for 1:1 centibels as the SoundFont 2.01 spec defines; fonts made for the spec-correct behavior need that.
  - A font can be converted from one behavior to the other by scaling its attenuation values by 2.5 or 0.4.
- **`TSF_RENDER_EFFECTSAMPLEBLOCK` (default 64):** the control-rate block size, a power of two up to 256 (the effects run in 256-sample blocks). Lower is more accurate, higher uses less CPU: 8 costs about twice the CPU of 64, 256 saves 10–15%.
- **`TSF_MIN_NOTE_LENGTH` (default 0.01 seconds):** the minimum note length (0 turns it off).
- **`TSF_RENDER_SHORTBUFFERBLOCK` (default 512):** the size in floats of the stack buffer `tsf_render_short` converts through. It must be a multiple of 2 × `TSF_RENDER_EFFECTSAMPLEBLOCK` (one stereo block), so `tsf_render_short` renders exactly like `tsf_render_float`. Smaller saves stack on small platforms; above 512 it isn't faster.
- **`WORDS_BIGENDIAN`:** define it on a big-endian machine, so the little-endian SoundFont data is byte-swapped while loading. GCC and Clang define it automatically.
- **`TSF_MAX_CHANNELS` (default 4096):** the highest channel number + 1 that the channel functions accept (256 MIDI ports of 16 channels). It also keeps the channel allocation from overflowing on 32-bit systems.
- **Standard library overrides:** `TSF_NO_STDIO`, `TSF_MALLOC`/`TSF_REALLOC`/`TSF_FREE`, `TSF_MEMCPY`/`TSF_MEMSET`, and the math function overrides.

## Verification

The implementation was checked as follows:

- **SoundFont-Spec-Test:** the [SoundFont-Spec-Test](https://github.com/mrbumpy409/SoundFont-Spec-Test) MIDI file and font were rendered and compared numerically with the FluidSynth and Sound Blaster Audigy reference recordings: envelopes, LFOs, panning and resonance.
- **Unit tests:** small generated SoundFonts test the individual rules: pitch, scale tuning, bend range, exclusive class, generator levels, clamping, envelopes, filter response, pedals, portamento, RPNs, NRPNs, linked modulators, effect idling, sample bounds, voice stealing, global gain, `tsf_reset`, `tsf_copy`, LFO rates and depths, output formats, render block sizes and sample rate changes.
- **Sanitizers and fuzzing:** renders and a corrupt-file loader test were run with the address and undefined-behavior sanitizers, as was a test that calls every API function with out-of-range, infinite and NaN arguments.
- **Compressed fonts:** TimGM6mb was converted to SFO and to SF3 (all samples, and every other one, compressed). Every region's sample matched the original in length and loop points, the GM test files rendered at the original's level, and damaged and fuzzed files loaded or failed cleanly under the sanitizers, with both stb_vorbis APIs.
- **Big-endian:** the GM test files rendered on an emulated big-endian machine (s390x under QEMU) with .sf2, .sfo and .sf3 fonts match the little-endian output within float rounding, and the unit tests pass there.
- **FluidSynth comparison:** the GM test files were rendered per channel with TimGM6mb in both synths (linear interpolation, no effects). The median levels of every channel agree within about 1 dB, and the stereo balance matches.
- **Memory:** every allocation counted through `TSF_MALLOC`/`TSF_REALLOC`/`TSF_FREE` and checked that `tsf_close` frees all of it. Playing the GM test files used 3.4 MB of heap with gm.sf2 and 6.0 MB with TimGM6mb, and the whole process 6–9 MB, against 24–28 MB for FluidSynth.
- **Speed:** on the test machine, TSF rendered the GM files with gm.sf2 3–6× faster than FluidSynth 2.3 with linear interpolation (one core, 44.1 kHz), and with TimGM6mb, which filters almost every voice, 1.4–2× faster.

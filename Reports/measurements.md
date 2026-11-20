# Open Mastering Studio - plugin measurements

Measured offline at 48 kHz on the processing chain (dither off). THD: harmonics 2 and up, relative to the fundamental. Aliasing: everything that is not DC, the tone or its harmonics (fold-back products plus noise), relative to the tone. Lower is better throughout.

## Harmonic distortion (THD) at 1 kHz

| Configuration | -24 dBFS | -12 dBFS | -6 dBFS | -1 dBFS |
|---|---|---|---|---|
| Clean path (all modules off) | -144.4 dB | -143.7 dB | -144.2 dB | -143.7 dB |
| Saturation Tape, drive 6 dB | -57.6 dB | -34.1 dB | -23.5 dB | -16.4 dB |
| Saturation Tape, drive 18 dB | -34.1 dB | -15.2 dB | -10.5 dB | -8.5 dB |
| Saturation Tube, drive 6 dB | -36.3 dB | -24.7 dB | -19.5 dB | -15.3 dB |
| Saturation Tube, drive 18 dB | -24.7 dB | -14.5 dB | -10.4 dB | -8.5 dB |
| Saturation Soft Clip, drive 6 dB | -64.6 dB | -40.4 dB | -27.6 dB | -15.7 dB |
| Saturation Soft Clip, drive 18 dB | -40.4 dB | -14.2 dB | -9.5 dB | -8.0 dB |
| Saturation Tape (Modeled), drive 6 dB | -67.8 dB | -49.3 dB | -37.6 dB | -28.6 dB |
| Saturation Tape (Modeled), drive 18 dB | -49.3 dB | -26.9 dB | -19.1 dB | -15.2 dB |
| Saturation Tube (Modeled), drive 6 dB | -38.5 dB | -23.0 dB | -15.6 dB | -11.1 dB |
| Saturation Tube (Modeled), drive 18 dB | -23.0 dB | -10.4 dB | -9.4 dB | -8.3 dB |
| Glue comp, fast (1 ms / 50 ms, 4:1) | -88.2 dB | -82.3 dB | -82.3 dB | -82.3 dB |
| Glue comp, typical (10 ms / 200 ms, 2:1) | -104.8 dB | -98.9 dB | -98.9 dB | -98.9 dB |
| Multiband, fast (all bands 4:1) | -96.5 dB | -96.6 dB | -96.8 dB | -97.1 dB |
| Limiter +6 dB, -1 dBTP | -143.6 dB | -144.2 dB | -143.1 dB | -142.0 dB |
| Limiter +12 dB, -1 dBTP, fast release | -142.5 dB | -142.9 dB | -143.3 dB | -142.6 dB |
| Limiter +18 dB, -1 dBTP, very fast release | -143.6 dB | -143.9 dB | -142.4 dB | -142.5 dB |
| Limiter Transparent +12 dB, -1 dBTP, fast release | -142.5 dB | -143.2 dB | -143.4 dB | -143.0 dB |
| Limiter Transparent +18 dB, -1 dBTP, very fast release | -143.6 dB | -142.2 dB | -142.0 dB | -142.2 dB |
| Multiband linear phase, fast (all bands 4:1) | -96.5 dB | -96.7 dB | -96.9 dB | -97.1 dB |
| Full chain: Loud & Punchy preset | -59.5 dB | -44.3 dB | -40.4 dB | -37.9 dB |

## Worst-case aliasing (sine sweep 2-19 kHz at -1 dBFS)

Each tone is pushed through the module; the table shows the worst result over the sweep: total non-harmonic energy, the single largest fold-back product, and where it landed.

| Configuration | Total aliasing + noise | Largest alias (anywhere) | Largest alias below 15 kHz |
|---|---|---|---|
| Clean path (all modules off) | -136.2 dB | -146.4 dB at 7.00 kHz (5.0 kHz tone) | -146.4 dB at 7.00 kHz (5.0 kHz tone) |
| Saturation Tape, drive 6 dB | -20.3 dB | -20.7 dB at 24.00 kHz (8.0 kHz tone) | -103.8 dB at 11.00 kHz (17.0 kHz tone) |
| Saturation Tape, drive 18 dB | -13.9 dB | -14.2 dB at 24.00 kHz (8.0 kHz tone) | -47.3 dB at 14.99 kHz (19.0 kHz tone) |
| Saturation Tube, drive 6 dB | -20.9 dB | -21.3 dB at 24.00 kHz (8.0 kHz tone) | -105.1 dB at 11.00 kHz (17.0 kHz tone) |
| Saturation Tube, drive 18 dB | -13.9 dB | -14.3 dB at 24.00 kHz (8.0 kHz tone) | -49.3 dB at 4.00 kHz (19.0 kHz tone) |
| Saturation Soft Clip, drive 6 dB | -19.5 dB | -19.9 dB at 24.00 kHz (8.0 kHz tone) | -83.5 dB at 7.01 kHz (17.0 kHz tone) |
| Saturation Soft Clip, drive 18 dB | -13.6 dB | -14.0 dB at 24.00 kHz (8.0 kHz tone) | -50.2 dB at 5.99 kHz (14.0 kHz tone) |
| Saturation Tape (Modeled), drive 6 dB | -26.4 dB | -27.0 dB at 24.00 kHz (8.0 kHz tone) | -88.9 dB at 14.99 kHz (19.0 kHz tone) |
| Saturation Tape (Modeled), drive 18 dB | -18.4 dB | -18.9 dB at 24.00 kHz (8.0 kHz tone) | -59.4 dB at 14.99 kHz (19.0 kHz tone) |
| Saturation Tube (Modeled), drive 6 dB | -27.0 dB | -27.3 dB at 24.00 kHz (8.0 kHz tone) | -83.8 dB at 0.01 kHz (8.0 kHz tone) |
| Saturation Tube (Modeled), drive 18 dB | -14.4 dB | -14.7 dB at 24.00 kHz (8.0 kHz tone) | -48.7 dB at 4.00 kHz (19.0 kHz tone) |
| Glue comp, fast (1 ms / 50 ms, 4:1) | -80.2 dB | -83.2 dB at 2.01 kHz (2.0 kHz tone) | -83.2 dB at 2.01 kHz (2.0 kHz tone) |
| Glue comp, typical (10 ms / 200 ms, 2:1) | -89.1 dB | -92.0 dB at 2.01 kHz (2.0 kHz tone) | -92.0 dB at 2.01 kHz (2.0 kHz tone) |
| Multiband, fast (all bands 4:1) | -90.0 dB | -93.1 dB at 2.01 kHz (2.0 kHz tone) | -93.1 dB at 2.01 kHz (2.0 kHz tone) |
| Limiter +6 dB, -1 dBTP | -76.0 dB | -79.4 dB at 8.01 kHz (8.0 kHz tone) | -79.4 dB at 8.01 kHz (8.0 kHz tone) |
| Limiter +12 dB, -1 dBTP, fast release | -72.0 dB | -75.4 dB at 8.01 kHz (8.0 kHz tone) | -75.4 dB at 8.01 kHz (8.0 kHz tone) |
| Limiter +18 dB, -1 dBTP, very fast release | -68.1 dB | -71.5 dB at 7.99 kHz (8.0 kHz tone) | -71.5 dB at 7.99 kHz (8.0 kHz tone) |
| Limiter Transparent +12 dB, -1 dBTP, fast release | -72.4 dB | -75.9 dB at 7.99 kHz (8.0 kHz tone) | -75.9 dB at 7.99 kHz (8.0 kHz tone) |
| Limiter Transparent +18 dB, -1 dBTP, very fast release | -68.5 dB | -71.9 dB at 7.99 kHz (8.0 kHz tone) | -71.9 dB at 7.99 kHz (8.0 kHz tone) |
| Multiband linear phase, fast (all bands 4:1) | -90.0 dB | -93.0 dB at 2.01 kHz (2.0 kHz tone) | -93.0 dB at 2.01 kHz (2.0 kHz tone) |
| Full chain: Loud & Punchy preset | -43.6 dB | -44.3 dB at 24.00 kHz (8.0 kHz tone) | -99.6 dB at 2.01 kHz (2.0 kHz tone) |

## True-peak output (limiter ceiling -1.0 dBTP)

Measured independently of the limiter's own detector. *Exact* is near-ideal band-limited reconstruction (256-tap windowed sinc, 16 points between samples): the waveform a DAC or codec would actually rebuild. *JUCE 16x* is a general-purpose oversampler shown for comparison; its filters let some images through near Nyquist, so it can read high on bright material.

| Input | True Peak on: exact | True Peak on: JUCE 16x | True Peak off: exact |
|---|---|---|---|
| Realistic master, +6 dB into limiter | -0.99 dBTP | -0.99 dBTP | -0.07 dBTP |
| Realistic master, +12 dB into limiter | -0.99 dBTP | -0.99 dBTP | -0.07 dBTP |
| Realistic master, +18 dB into limiter | -0.99 dBTP | -0.99 dBTP | -0.07 dBTP |
| Stress: noise with flat top end to 24 kHz, +12 dB | -0.82 dBTP | -0.98 dBTP | 0.90 dBTP |
| Stress: white-noise bursts, +18 dB | -0.49 dBTP | -0.96 dBTP | 4.58 dBTP |
| Extreme inter-sample-peak test (fs/4 sine, +3 dB ISP) | -1.00 dBTP | -0.91 dBTP | 2.01 dBTP |

## Latency: reported vs measured

| Sample rate | Reported to host | Measured: impulse, all off | Measured: impulse, saturation wet path | Cross-correlation peak, Loud & Punchy |
|---|---|---|---|---|
| 44100 Hz | 354 samples (8.03 ms) | 354 | 354 | 354 |
| 48000 Hz | 362 samples (7.54 ms) | 362 | 362 | 362 |
| 96000 Hz | 458 samples (4.77 ms) | 458 | 458 | 458 |

## CPU cost (single core, 256-sample blocks)

| Module | 48 kHz | 96 kHz |
|---|---|---|
| EQ (all bands active) | 0.32 % | 0.60 % |
| Multiband compressor | 0.34 % | 0.55 % |
| Glue compressor | 0.12 % | 0.21 % |
| Saturation (8x oversampled, while on) | 3.53 % | 6.59 % |
| Stereo imager | 0.15 % | 0.28 % |
| Limiter (true peak) | 0.88 % | 1.71 % |
| Loudness meters (in, and out with true peak) | 0.50 % | 1.03 % |
| Whole plugin (processBlock, Loud & Punchy) | 5.55 % | 11.28 % |

The limiter row includes the second true-peak stage. Switched-off modules keep running in the background (so switching them on never clicks), except saturation, which rests after half a second off. The whole-plugin figure uses Loud & Punchy, which has saturation on.

# M7.5 offline render checkpoint

Date: 2026-08-13

Status: implementation evidence. The render itself is verified; the music it produces still has no listening approval.

## Why this slice exists

Nothing could leave the editor. A song existed only while the application was open, which made every listening pass expensive, unrepeatable, and unshareable — and listening is the only gate that has ever found a musical defect in this project.

This was finding 6 of the 2026-08-12 authoring assessment, the last one still open.

## What changed

A `--render` mode writes a project to a 24-bit WAV at the project's own sample rate:

```powershell
.\bin\ResonanceMusicEditor.exe --render --project <song.resonance.json> `
    --wav <out.wav> [--repeats N] [--tail-seconds S] --report <report.json>
```

| Option | Default | Meaning |
| --- | --- | --- |
| `--repeats` | 1 | How many times the loop is played, 1 through 64 |
| `--tail-seconds` | 2.0 | Extra time after the transport stops, 0 through 30 |

The transport is stopped once the requested repeats are rendered, so the tail captures instrument release rather than another pass of the song.

### It reuses the production callback

The render drives `audioDeviceIOCallbackWithContext` with no audio device attached — the same path the editor plays through, and the same path `--audio-probe` already used. There is deliberately no second mixing implementation, so what is written is what the editor plays.

Master gain is forced to unity so a render reflects the song rather than whatever the session's monitoring level happened to be. That makes renders comparable in level; it does not make them bit-identical, for the reason described below. Per-track mixer gain and pan are part of the song and are kept.

### The report is the evidence

`schema/render-report.schema.json` requires a complete render: non-silent peak, zero clipped samples, zero invalid samples, a file whose byte count matches what was written, and 24-bit output. Peak and RMS are reported in dBFS.

## Evidence

A 32-bar, four-track structured song rendered end to end:

| Measure | Value |
| --- | --- |
| Duration | 79.8 s (76.8 s music + 3 s tail) |
| Format | 44,100 Hz, 24-bit, stereo |
| File | 21.1 MB |
| Peak | −6.24 dBFS |
| RMS | −22.06 dBFS |
| Clipped samples | 0 |
| Invalid samples | 0 |

The gate renders the committed two-track artifact to a temporary WAV, checks the file exists and matches its reported size, requires no clipping or invalid samples and a non-silent peak, then deletes the audio. Only the fact that a complete file was written is kept as evidence; rendered audio is not committed.

## What the render immediately found

Measuring per-section RMS of the rendered file exposed a compositional flaw that no symbolic test could see:

| Section | RMS |
| --- | --- |
| Intro, bars 1–4 | −29.7 dBFS |
| A, bars 5–12 | −23.5 to −21.9 dBFS |
| B, bars 13–20 | −21.4 to −23.1 dBFS |
| A′, bars 21–28 | −22.0 to −21.4 dBFS |
| Outro, bars 29–32 | −22.7 dBFS |
| Tail | −59.3 dBFS |

The intro reads clearly at about 8 dB below the body. The outro sits close to the A sections rather than thinning, which is not what the arrangement intends: the harmony part receives a velocity lift in both the intro and the outro, and in the outro that lift works against the thinning.

**Read those numbers with the noise floor below in mind.** The intro-to-body gap of about 8 dB is well outside it and is real. The outro-to-A difference of roughly 1 dB is not: it is inside the run-to-run variation and is not evidence on its own. The qualitative point — that the outro does not fall away the way the intro does — is consistent with the arrangement, but it is a listening question, not a measured one.

## The render is not reproducible

Two renders of the identical project produce different audio. Measured over a 32-bar four-track song:

| Comparison | Value |
| --- | --- |
| Difference RMS between two identical renders | −19.8 dBFS, about 140% of signal RMS |
| Per-4-bar section spread | up to ±2.3 dB |

The large sample-wise difference alongside a similar overall level is the signature of randomised oscillator start phase — normal synthesiser behaviour, and a property of the instrument rather than a defect in the host.

Three consequences worth carrying forward:

1. **Measurement cannot A/B small edits.** Anything under roughly ±2.5 dB per section is indistinguishable from run-to-run variation. An early attempt to verify a velocity change to one section by comparing renders produced a difference smaller than the noise floor and was worthless.
2. **The render gate's assertions are unaffected.** Non-silence, no clipping, no invalid samples, and complete file length are all robust to phase randomisation.
3. **Roadmap M9 lists "deterministic render manifests and hashes".** That goal is not achievable while the instrument randomises phase, and will need either a seedable instrument state, a documented tolerance-based comparison instead of hashing, or an explicit decision that renders are not bit-reproducible.

## What remains open

- The rendered audio has no listening approval, and neither does anything else since M5.
- No stems, loop-region metadata, normalisation, dither, or loudness targets. Those are M9 concerns.
- Render is offline-only and single-file; there is no progress UI, and a long render blocks the message thread.
- Whether the outro resolves is unresolved and is a listening question, not a measurable one at this noise floor.
- Render reproducibility is unaddressed. It blocks the M9 deterministic-manifest goal.

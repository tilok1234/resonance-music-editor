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

Master gain is forced to unity so a render is reproducible rather than reflecting whatever the session's monitoring level happened to be. Per-track mixer gain and pan are part of the song and are kept.

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

The intro reads clearly at about 8 dB below the body. **The outro does not thin out at all** — it sits level with the A sections, because the harmony part receives a velocity lift in both the intro and the outro, and in the outro that lift cancels the intended thinning. The piece likely stops rather than resolves.

This is the third time in two days that moving into the audio domain found something the symbolic gates could not. It is worth noting what kind of finding it is: not a defect in the editor, but a defect in the music, surfaced by measurement rather than by listening. Measurement can show that a section is not dynamically differentiated. It cannot say whether the piece is any good.

## What remains open

- The rendered audio has no listening approval, and neither does anything else since M5.
- No stems, loop-region metadata, normalisation, dither, or loudness targets. Those are M9 concerns.
- Render is offline-only and single-file; there is no progress UI, and a long render blocks the message thread.
- The outro flaw above is unfixed. It is a compositional choice rather than a defect, and belongs to whoever is judging the music.

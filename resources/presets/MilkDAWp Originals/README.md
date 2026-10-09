# MilkDAWp Originals

Presets written for MilkDAWp (Phase 8.12), AGPL-3.0-or-later like the rest of
the project. They use what only MilkDAWp gives a preset:

- `sampler_camera` and `sampler_video`: the layer's media source (Settings >
  Media source: a camera, a video file or an image), with `texsize_camera` and
  `texsize_video`. Black while there is none. Media Mix and the media blend
  don't change it: they are how the media goes *over* the picture.
- `mdw_beat_phase` and `mdw_bar_phase` (0 to just below 1 through each beat and
  bar), `mdw_bpm` (0 with no beat) and `mdw_onset` (1 at a note or hit, then
  decaying), from the host's transport while it plays, otherwise from the
  beat detector.

Elsewhere (stock projectM or MilkDrop, renamed to `.milk`) they play with no
media and no beat.

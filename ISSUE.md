## Why this matters

This is the single change that would most improve driving with the dash. Right now VS BEST updates once, at the line, and tells you what you already did. A live delta tells you what you are doing, corner by corner, while you can still act on it. That is the difference between a lap timer and a coaching tool.

## Today

`uiHelper.setDelta()` has one call site, `src/main_display.cpp:343`, inside the crossing branch. On a 62 second lap the hero panel shows the same number for 62 seconds. It only appears from your second timed lap onward, since it compares against `getPreviousBestLapTime()`.

## Proposal

Update the hero panel continuously with the gap to your best lap at the point on track where you currently are.

## Approach

`LapManager` stores gate crossing times and one previous point. It has no memory of where you were, so a reference trace is the new piece:

1. Record the lap in progress as positions plus elapsed time since lap start. Use the interpolated crossing time as the start, which the crossing detection already computes, so the trace is not quantised to the fix interval.
2. On a finish crossing, if the lap is a new best, promote it to the reference. Two buffers, one reference and one candidate.
3. Per fix, find the nearest point on the reference polyline and subtract its elapsed time from the current elapsed time. That is the delta.

Search a window of about 15 points either side of the previous match rather than the whole trace. It keeps the cost to a few dozen distance computations per fix and stops the match jumping to the wrong place on a track that passes near itself.

Index by position, not by cumulative distance travelled. Distance drifts apart between laps when you take a different line; nearest point matching does not.

## Colours

Keep the existing two states exactly as they are, they are already right:

| State | fill | border and text |
|---|---|---|
| Faster than best | `good_deep` `0x0F3A23` | `good`, night `0x2EE07A` / day `0x29FF8A` |
| Slower than best | `bad_deep` `0x3A0F10` | `bad`, night `0xFF3B3B` / day `0xFF5050` |

Add a third state, purple, for a new fastest lap, following the usual motorsport reading of purple as the fastest of the session. Suggested tokens in the same family as the pairs above: `best_deep` `0x2A0F3A` for the fill, `best` around `0xB44DFF` night and `0xC77DFF` day for the border and text.

Purple fires at the line, on the lap that just became the session best, and holds for a few seconds before the live delta for the new lap takes over. During a lap the panel stays green or red as it does now, since whether the lap will be a best is not known until it ends.

The signal already exists: `isBest` is computed at `src/main_display.cpp:315` and currently only reaches a `log_i()` line. It just needs to reach the UI.

This means `dash2_tokens_t` gains two fields in both `TOK_NIGHT` and `TOK_DAY`, and `paint_delta_surface()` takes a three state value instead of its `bool faster`. While changing that signature, fix the sign handling: `setDelta(0, true)` encodes direction as `-fabsf(seconds)`, and `-0.0f < 0.0f` is false, so zero renders red. A live delta sits near zero often, so it also wants a small deadband around zero rather than flipping green to red on GPS noise.

## Cost

A 62 second lap at 25Hz is about 1550 samples. At 12 bytes each, scaled int32 lat and lng plus a uint32 millisecond offset, that is roughly 19KB per buffer, so under 40KB for both. Put it in PSRAM, which `LogBuffer` already allocates from via `ps_malloc`. Downsampling the reference to 5Hz and interpolating brings it under 4KB per buffer with no visible loss.

## Two things to get right

**Keep `LapManager` host-clean.** `tools/lapreplay` compiles it against `shim/Arduino.h`, so the allocation cannot live inside the class. Pass a buffer in or use a fixed static array. The payoff is that the replay harness can then verify the delta offline against the existing 12 crossing fixture, instead of the track being the first place anyone finds out whether the matching works.

**Do not repaint at telemetry rate.** `ui_dash2_set_delta` calls `refresh_hero`, which repaints the panel and four labels on a full frame single blit path. Copy what `setSectors` already does: diff in `UiHelper` and gate the update, around 5Hz, repainting only when the displayed number changes.

## Expectations

Delta precision is position error divided by speed. With `hAcc` around a metre, at 60 km/h one metre is roughly 60ms, so the number will jitter by five to ten hundredths even on an identical lap. The panel shows two decimals, which will read as more precise than it is. Useful at the "up or down two tenths" level, which is what it gets used for. Smoothing buys steadiness at the cost of lag, which defeats the point, so start unsmoothed and see how it reads on track.

## Acceptance criteria

- The delta moves continuously through the lap, not only at the line.
- It reflects position on track, so slowing in one corner shows there and not a corner later.
- Green and red behave exactly as they do today.
- A new session best shows purple at the line, then hands over to the live delta.
- Zero does not render as red.
- No frame rate cost on the dashboard, and no drop in logged rows.
- Blank or held rather than wrong before a reference lap exists.
- Verified in `tools/lapreplay` against the existing fixture before it is flashed.

/* Host replay of the session-button state machine (lib/SessionButton).
 *
 * The cases that decide whether this thing is right are timing cases — a contact
 * that bounces for 8ms, a release one millisecond short of the hold, the millis()
 * wrap — and none of them can be produced reliably with a thumb on a bench. Here
 * the clock is a variable, so they are exact.
 *
 *   make sessionbutton && ./sessionbutton
 */
#include "SessionButton.h"

#include <cassert>
#include <cstdio>

namespace {

constexpr uint32_t kHold     = 3000;
constexpr uint32_t kDebounce = 25;

/* Drive the button at 1ms per step, the worst case for the real 1ms loop().
 * Returns the last event seen, and records whether each event fired at all. */
struct Run {
    int shortPresses = 0;
    int holds        = 0;
    uint8_t maxCountdown = 0;
    uint8_t minCountdownWhileHeld = 255;
};

void step(SessionButton &b, Run &r, bool pressed, uint32_t from, uint32_t to) {
    for (uint32_t t = from; t < to; t++) {
        SessionButtonEvent ev = b.update(pressed, t);
        if (ev == SB_SHORT_PRESS)   r.shortPresses++;
        if (ev == SB_HOLD_COMPLETE) r.holds++;

        uint8_t c = b.countdownSeconds();
        if (c > r.maxCountdown) r.maxCountdown = c;
        if (pressed && c && c < r.minCountdownWhileHeld) r.minCountdownWhileHeld = c;
    }
}

void tap_starts_a_session() {
    SessionButton b(kHold, kDebounce);
    Run r;
    step(b, r, false, 0, 100);
    step(b, r, true,  100, 300);   /* 200ms press — well past debounce, short of hold */
    step(b, r, false, 300, 400);

    assert(r.shortPresses == 1);
    assert(r.holds == 0);
}

void hold_fires_once_and_not_again() {
    SessionButton b(kHold, kDebounce);
    Run r;
    step(b, r, false, 0, 100);
    step(b, r, true,  100, 6000);  /* held for twice the hold time */
    step(b, r, false, 6000, 6200);

    assert(r.holds == 1);
    /* The release after a completed hold must NOT also read as a tap, or every
     * stop would immediately start a new session. */
    assert(r.shortPresses == 0);
}

void countdown_runs_3_2_1() {
    SessionButton b(kHold, kDebounce);
    Run r;
    step(b, r, false, 0, 100);
    step(b, r, true,  100, 3200);

    assert(r.maxCountdown == 3);            /* opens on 3, never 4 */
    assert(r.minCountdownWhileHeld == 1);   /* reaches 1, and 0 only once fired */

    /* And nothing counts down when the button is idle. */
    SessionButton idle(kHold, kDebounce);
    for (uint32_t t = 0; t < 500; t++) idle.update(false, t);
    assert(idle.countdownSeconds() == 0);
}

void release_one_ms_short_is_a_tap_not_a_stop() {
    SessionButton b(kHold, kDebounce);
    Run r;
    step(b, r, false, 0, 100);
    /* Debounced press begins at t=125; the hold would complete at t=3125. */
    step(b, r, true,  100, 3125);
    step(b, r, false, 3125, 3300);

    assert(r.holds == 0);
    assert(r.shortPresses == 1);
}

void bounce_is_not_a_press() {
    SessionButton b(kHold, kDebounce);
    Run r;
    step(b, r, false, 0, 100);
    /* 8ms of contact chatter, under the 25ms window. */
    for (uint32_t t = 100; t < 108; t++) b.update((t & 1) != 0, t);
    step(b, r, false, 108, 400);

    assert(r.shortPresses == 0);
    assert(r.holds == 0);
    assert(r.maxCountdown == 0);
}

void bounce_on_release_does_not_double_fire() {
    SessionButton b(kHold, kDebounce);
    Run r;
    step(b, r, false, 0, 100);
    step(b, r, true,  100, 300);
    /* Chatter on the way up, then properly released. */
    for (uint32_t t = 300; t < 310; t++) b.update((t & 1) == 0, t);
    step(b, r, false, 310, 600);

    assert(r.shortPresses == 1);
}

void survives_the_millis_wrap() {
    SessionButton b(kHold, kDebounce);
    Run r;
    const uint32_t base = 0xFFFFF000u;   /* ~4s before millis() rolls over */

    for (uint32_t i = 0; i < 100; i++) b.update(false, base + i);
    for (uint32_t i = 100; i < 4000; i++) {
        SessionButtonEvent ev = b.update(true, base + i);   /* wraps mid-hold */
        if (ev == SB_HOLD_COMPLETE) r.holds++;
        if (ev == SB_SHORT_PRESS)   r.shortPresses++;
    }

    assert(r.holds == 1);
    assert(r.shortPresses == 0);
}

} // namespace

int main() {
    tap_starts_a_session();
    hold_fires_once_and_not_again();
    countdown_runs_3_2_1();
    release_one_ms_short_is_a_tap_not_a_stop();
    bounce_is_not_a_press();
    bounce_on_release_does_not_double_fire();
    survives_the_millis_wrap();

    printf("sessionbutton: all checks passed\n");
    return 0;
}

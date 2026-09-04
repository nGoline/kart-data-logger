#ifndef SESSION_BUTTON_H
#define SESSION_BUTTON_H

#include <stdint.h>

/* What the button did on this update() — at most one event per call. */
enum SessionButtonEvent {
    SB_NONE = 0,
    SB_SHORT_PRESS,     /* released before the hold elapsed — start a session */
    SB_HOLD_COMPLETE,   /* held the full hold time — stop the session         */
};

/* Debounced press / press-and-hold for a momentary button wired to ground on an
 * INPUT_PULLUP pin, so `pressed` is (digitalRead(pin) == LOW).
 *
 * Deliberately knows nothing about GPIO, LVGL or the log: it takes a level and a
 * clock and returns an event. That is what lets the whole state machine be
 * replayed on the host — see tools/lapreplay/sessionbutton.cpp — where the timing
 * cases that matter (a bouncing contact, a release one millisecond short of the
 * hold) can be driven exactly, which they cannot be with a thumb. */
class SessionButton {
public:
    explicit SessionButton(uint32_t holdMs, uint32_t debounceMs = 25);

    SessionButtonEvent update(bool pressed, uint32_t nowMs);

    /* Whole seconds still to hold, counting down 3-2-1; 0 when no countdown is
     * running. Only nonzero once the press has survived the debounce window, so
     * a knock against the enclosure never flashes a countdown on the dash, and
     * it clears the instant the button is released rather than a debounce window
     * later. */
    uint8_t countdownSeconds() const;

private:
    uint32_t _holdMs;
    uint32_t _debounceMs;
    uint32_t _nowMs;      /* last clock seen, so countdownSeconds() needs no arg */
    uint32_t _edgeMs;     /* when the raw level last changed                     */
    uint32_t _pressMs;    /* when the debounced press began                      */
    bool     _raw;        /* level as read                                        */
    bool     _stable;     /* level after debouncing                               */
    bool     _fired;      /* hold already reported for this press                 */
};

#endif

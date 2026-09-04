#include "SessionButton.h"

SessionButton::SessionButton(uint32_t holdMs, uint32_t debounceMs)
    : _holdMs(holdMs), _debounceMs(debounceMs), _nowMs(0), _edgeMs(0),
      _pressMs(0), _raw(false), _stable(false), _fired(false) {}

SessionButtonEvent SessionButton::update(bool pressed, uint32_t nowMs) {
    _nowMs = nowMs;

    if (pressed != _raw) {
        _raw    = pressed;
        _edgeMs = nowMs;
    }

    SessionButtonEvent ev = SB_NONE;

    /* Take the level only once it has been steady for the debounce window. The
     * subtraction is unsigned on purpose: it stays correct across the millis()
     * wrap, which a session long enough to hit it would otherwise walk into. */
    if (_raw != _stable && (uint32_t)(nowMs - _edgeMs) >= _debounceMs) {
        _stable = _raw;
        if (_stable) {
            _pressMs = nowMs;
            _fired   = false;
        } else if (!_fired) {
            ev = SB_SHORT_PRESS;
        }
    }

    /* Fire on the way past the hold time, not on release. Waiting for the
     * release would let the countdown reach zero with the session still running
     * until the thumb comes off — which reads as a dead button.
     *
     * `_raw` and not just `_stable`: the debounced level lags the real one by up
     * to the debounce window, so counting on `_stable` alone completes a hold
     * that the thumb already ended. A release a few milliseconds short of 3s
     * would still have stopped the session. */
    if (_stable && _raw && !_fired && (uint32_t)(nowMs - _pressMs) >= _holdMs) {
        _fired = true;
        ev     = SB_HOLD_COMPLETE;
    }

    return ev;
}

uint8_t SessionButton::countdownSeconds() const {
    if (!_stable || !_raw || _fired) return 0;

    uint32_t held = (uint32_t)(_nowMs - _pressMs);
    if (held >= _holdMs) return 0;

    /* Round up, so a 3000ms hold shows 3 the instant it starts and 1 through the
     * final second. Rounding down would open on 2 and end on a silent 0. */
    return (uint8_t)((_holdMs - held + 999) / 1000);
}

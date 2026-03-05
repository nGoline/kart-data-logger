#ifndef VOICE_PARSER_H
#define VOICE_PARSER_H

#include <Arduino.h>
#include "AudioManager.h"

namespace VoiceParser {
    // Converts an integer (0-999) into a sequence of WAV file requests
    void queueNumber(AudioManager& audio, int num);

    // Formats and enqueues a full lap time announcement
    void announceLapTime(AudioManager& audio, int minutes, int seconds, int millis, bool isBest = false);

    // Formats and enqueues a delta time announcement (e.g., "2 seconds faster")
    void announceDeltaTime(AudioManager& audio, int deltaMinutes, int deltaSeconds, int deltaMillis, bool isFaster);

    // Helper to announce a time in MM:SS.mmm format, used by both LapTime and DeltaTime
    void announceTime(AudioManager& audio, int minutes, int seconds, int millis);
}

#endif
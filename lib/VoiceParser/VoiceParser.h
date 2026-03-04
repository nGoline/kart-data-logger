#ifndef VOICE_PARSER_H
#define VOICE_PARSER_H

#include <Arduino.h>
#include "AudioManager.h"

namespace VoiceParser {
    // Converts an integer (0-999) into a sequence of WAV file requests
    void queueNumber(AudioManager& audio, int num);

    // Formats and enqueues a full lap time announcement
    void announceLapTime(AudioManager& audio, int minutes, int seconds, int millis);
}

#endif
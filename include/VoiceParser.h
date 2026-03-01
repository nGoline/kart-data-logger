#ifndef VOICE_PARSER_H
#define VOICE_PARSER_H

#include <Arduino.h>
#include "AudioManager.h"

namespace VoiceParser {
    // Adiciona o número (0 a 999) à fila de reprodução
    void queueNumber(AudioManager& audio, int num);
    
    // Orquestra a frase completa do tempo de volta
    void announceLapTime(AudioManager& audio, int minutes, int seconds, int millis);
}

#endif
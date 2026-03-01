#ifndef AUDIO_MANAGER_H
#define AUDIO_MANAGER_H

#include <Arduino.h>
#include <LittleFS.h>
#include <queue>
#include "Audio.h"

class AudioManager {
public:
    AudioManager(int bclkPin, int lrcPin, int dinPin);
    bool begin(int volume = 15);
    void queueAudio(const char* filename);
    void setVolume(int volume);
    bool isPlaying();

private:
    int _bclk, _lrc, _din;
    Audio _audio;
    std::queue<String> _playlist;
    
    // Mutex para evitar condições de corrida na fila (Thread Safety)
    SemaphoreHandle_t _mutex;
    
    void playNextInQueue();
    static void audioTask(void* parameter);
};

#endif
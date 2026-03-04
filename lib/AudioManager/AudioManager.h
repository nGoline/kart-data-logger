#ifndef AUDIO_MANAGER_H
#define AUDIO_MANAGER_H

#include <Arduino.h>
#include <FS.h>
#include <queue>
#include "Audio.h" // Assuming ESP32-audioI2S library

class AudioManager {
public:
    AudioManager(int bclkPin, int lrcPin, int dinPin);
    
    // Pass LittleFS or SD here
    bool begin(FS &fs, int volume = 15);
    
    void queueAudio(const char* filename);
    bool tryQueueAudio(const char* filename);
    void setVolume(int volume);
    bool isPlaying();

    // The background loop handler
    void loop();

private:
    int _bclk, _lrc, _din;
    Audio _audio;
    FS* _fs;
    std::queue<String> _playlist;
    SemaphoreHandle_t _mutex;
    
    void playNextInQueue();
    static void audioTask(void* parameter);
};

#endif
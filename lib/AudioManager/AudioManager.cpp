#include "AudioManager.h"

// Bring in the global Mutex from main_logger.cpp
#if defined(ENABLE_IMU)
extern SemaphoreHandle_t hardwareBusMutex;
#endif

// Global semaphore for EOF signals
SemaphoreHandle_t audioNextSem;

// These callbacks must be in the global namespace for the Audio library to find them
void audio_eof_wav(const char *info) { if(audioNextSem) xSemaphoreGive(audioNextSem); }

AudioManager::AudioManager(int bclkPin, int lrcPin, int dinPin) 
    : _bclk(bclkPin), _lrc(lrcPin), _din(dinPin) {
    _mutex = xSemaphoreCreateMutex();
    audioNextSem = xSemaphoreCreateBinary();
}

bool AudioManager::begin(FS &fs, int volume) {
    _fs = &fs;
    _audio.setPinout(_bclk, _lrc, _din);
    _audio.setVolume(volume);

    log_i("Audio: I2S Initialized (BCLK:%d, LRC:%d, DIN:%d)", _bclk, _lrc, _din);

    xTaskCreatePinnedToCore(
        this->audioTask,
        "AudioTask",
        8192,
        this,
        2, 
        NULL,
        0  // Pin to Core 0 to avoid jitter from UI/GPS logic on Core 1
    );

    return true;
}

void AudioManager::audioTask(void* parameter) {
    AudioManager* self = (AudioManager*)parameter;
    
    for (;;) {
        // 1. SAFE AUDIO CHUNKING
        #if defined(ENABLE_IMU)
        if (xSemaphoreTake(hardwareBusMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            self->_audio.loop();
            xSemaphoreGive(hardwareBusMutex);
        }
        #else
        self->_audio.loop();
        #endif

        // 2. QUEUE MANAGEMENT
        if (!self->_audio.isRunning()) {
            String nextFile = "";
            bool hasNext = false;

            // QUICK LOCK: Just pop the string and unlock immediately
            if (xSemaphoreTake(self->_mutex, portMAX_DELAY) == pdTRUE) {
                if (!self->_playlist.empty()) {
                    nextFile = self->_playlist.front();
                    self->_playlist.pop();
                    hasNext = true;
                } else {
                    self->_audio.stopSong();
                }
                xSemaphoreGive(self->_mutex); // Mutex released! Core 1 can now queue freely.
            }

            // OUTSIDE THE MUTEX: Do the slow delays and hardware loading
            if (hasNext) {
                vTaskDelay(pdMS_TO_TICKS(100)); // Gap between files
                self->playNextInQueue(nextFile);
            }
        }

        // Critical for Core 0 stability
        vTaskDelay(pdMS_TO_TICKS(1)); 
    }
}

bool AudioManager::tryQueueAudio(const char* filename) {
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        log_d("Audio: Trying to queue %s", filename);
        _playlist.push(String(filename));
        xSemaphoreGive(_mutex);
        log_d("Audio: Queued %s", filename);
        return true;
    }

    log_w("Audio: Could not queue %s (bus busy)", filename);
    return false;
}

void AudioManager::playNextInQueue(String nextFile) {
    #if defined(ENABLE_IMU)
    if (xSemaphoreTake(hardwareBusMutex, portMAX_DELAY) == pdTRUE) {
    #endif
        _audio.stopSong(); 
        log_d("Audio: Playing %s", nextFile.c_str());
        
        _audio.connecttoFS(*_fs, nextFile.c_str());
        
    #if defined(ENABLE_IMU)
        xSemaphoreGive(hardwareBusMutex); 
    }
    #endif
}

bool AudioManager::isPlaying() {
    return _audio.isRunning() || !_playlist.empty();
}

void AudioManager::setVolume(int volume) {
    _audio.setVolume(volume);
}
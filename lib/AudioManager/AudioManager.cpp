#include "AudioManager.h"

// Global semaphore for EOF signals
SemaphoreHandle_t audioNextSem;

// These callbacks must be in the global namespace for the Audio library to find them
void audio_eof_mp3(const char *info) { if(audioNextSem) xSemaphoreGive(audioNextSem); }
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
        self->_audio.loop();

        if (!self->_audio.isRunning()) {
            if (xSemaphoreTake(self->_mutex, 0) == pdTRUE) {
                if (!self->_playlist.empty()) {
                    // Small gap between files for clarity
                    vTaskDelay(pdMS_TO_TICKS(100));
                    self->playNextInQueue();
                }
                xSemaphoreGive(self->_mutex);
            }
        }
        // Critical for Core 0 stability
        vTaskDelay(pdMS_TO_TICKS(1)); 
    }
}

void AudioManager::queueAudio(const char* filename) {
    if (xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE) {
        _playlist.push(String(filename));
        xSemaphoreGive(_mutex);
        log_d("Audio: Queued %s", filename);
    }
}

bool AudioManager::tryQueueAudio(const char* filename) {
    if (xSemaphoreTake(_mutex, 0) == pdTRUE) {
        _playlist.push(String(filename));
        xSemaphoreGive(_mutex);
        return true;
    }
    return false;
}

void AudioManager::playNextInQueue() {
    String nextFile = _playlist.front();
    _playlist.pop();

    _audio.stopSong(); 
    log_i("Audio: Playing %s", nextFile.c_str());
    
    // Use the stored Filesystem reference
    _audio.connecttoFS(*_fs, nextFile.c_str());
}

bool AudioManager::isPlaying() {
    return _audio.isRunning() || !_playlist.empty();
}

void AudioManager::setVolume(int volume) {
    _audio.setVolume(volume);
}
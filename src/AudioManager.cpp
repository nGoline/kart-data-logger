#include "AudioManager.h"

// Semáforo para sincronizar o fim de um áudio com o início do próximo
SemaphoreHandle_t audioNextSem;

// Callback global da biblioteca: disparado quando o áudio termina fisicamente
void audio_eof_mp3(const char *info) {
    xSemaphoreGive(audioNextSem); 
}

// Nota: A biblioteca usa o nome 'audio_eof_mp3' mesmo para arquivos .wav
void audio_eof_wav(const char *info) {
    xSemaphoreGive(audioNextSem);
}

AudioManager::AudioManager(int bclkPin, int lrcPin, int dinPin) 
    : _bclk(bclkPin), _lrc(lrcPin), _din(dinPin) {
    _mutex = xSemaphoreCreateMutex();
    audioNextSem = xSemaphoreCreateBinary();
}

bool AudioManager::begin(int volume) {
    // Certifique-se de que o LittleFS.begin() já aconteceu no main setup()
    
    _audio.setPinout(_bclk, _lrc, _din);
    _audio.setVolume(volume);

    xTaskCreatePinnedToCore(
        this->audioTask,
        "AudioTask",
        10000,
        this,
        2, // PRIORIDADE BAIXA (Acima do Idle, mas abaixo das tarefas de sistema)
        NULL,
        0  // Core 0
    );

    return true;
}

void AudioManager::audioTask(void* parameter) {
    AudioManager* instance = (AudioManager*)parameter;
    
    for (;;) {
        // Alimenta o áudio
        instance->_audio.loop();

        // Só tentamos gerenciar a fila se o áudio NÃO estiver rodando
        if (!instance->_audio.isRunning()) {
            if (xSemaphoreTake(instance->_mutex, 0) == pdTRUE) {
                if (!instance->_playlist.empty()) {
                    vTaskDelay(pdMS_TO_TICKS(200));
                    instance->playNextInQueue();
                }
                xSemaphoreGive(instance->_mutex);
            }
        }

        // CRÍTICO: 1ms de delay real. yield() não é suficiente no Core 0.
        // Isso permite que o IDLE0 resete o Watchdog e que outros Locks sejam liberados.
        vTaskDelay(pdMS_TO_TICKS(1)); 
    }
}

void AudioManager::queueAudio(const char* filename) {
    if (xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE) {
        _playlist.push(String(filename));
        xSemaphoreGive(_mutex);
    }
}

void AudioManager::playNextInQueue() {
    String nextFile = _playlist.front();
    _playlist.pop();

    // 1. Para qualquer processamento residual do I2S
    _audio.stopSong(); 

    // 2. Log de tempo para debug (veja se o gap sumiu)
    Serial.printf("[%lu] >>> REPRODUZINDO: %s\n", millis(), nextFile.c_str());
    
    // 3. Conecta o novo WAV estéreo limpo
    _audio.connecttoFS(LittleFS, nextFile.c_str());
}

bool AudioManager::isPlaying() {
    return _audio.isRunning() || !_playlist.empty();
}

void AudioManager::setVolume(int volume) {
    _audio.setVolume(volume);
}
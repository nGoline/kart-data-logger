#ifndef CONFIG_H
#define CONFIG_H

struct Coordinates {
    double lat;
    double lng;
};

// Exemplo: Linha de Meta - Kartódromo San Marino, Paulínia
const Coordinates FINISH_LINE = {-23.6049418,-46.8362675}; 
const float GATE_RADIUS = 15.0; // Raio de detecção em metros
const uint32_t MIN_LAP_TIME_MS = 50000; // Ignorar detecções antes de 50s (evita falsos positivos)

#endif
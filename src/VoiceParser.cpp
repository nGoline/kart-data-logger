#include "VoiceParser.h"

namespace VoiceParser {

    void queueNumber(AudioManager& audio, int num) {
        if (num < 0 || num > 999) return;

        if (num == 0) {
            audio.queueAudio("/0.wav");
            return;
        }

        if (num >= 100) {
            int hundreds = (num / 100) * 100;
            int remainder = num % 100;

            if (num == 100) {
                audio.queueAudio("/100.wav"); 
                return; 
            }
            
            if (hundreds == 100) {
                // Caso especial: 101 a 199
                audio.queueAudio("/100toe.wav");
                if (remainder > 0) {
                    queueNumber(audio, remainder); // Ex: "cinquenta e três"
                }
                return;
            } else {
                // Para 200, 300, etc. (Ex: "Duzentos")
                audio.queueAudio(("/" + String(hundreds) + ".wav").c_str());
                if (remainder > 0) {
                    audio.queueAudio("/e.wav"); // Conectivo "e"
                    queueNumber(audio, remainder);
                }
                return;
            }
        }

        if (num >= 20) {
            int tens = (num / 10) * 10;
            int units = num % 10;

            audio.queueAudio(("/" + String(tens) + ".wav").c_str());

            if (units > 0) {
                audio.queueAudio("/e.wav");
                queueNumber(audio, units);
            }
            return;
        }

        audio.queueAudio(("/" + String(num) + ".wav").c_str());
    }

    void announceLapTime(AudioManager& audio, int minutes, int seconds, int millis) {
        audio.queueAudio("/lap_time.wav");

        // Minutos e Segundos
        if (minutes > 0) {
            queueNumber(audio, minutes);
            if (seconds == 0) {
                audio.queueAudio("/0.wav");
                audio.queueAudio("/0.wav");
            } else {
                audio.queueAudio("/e.wav");
                queueNumber(audio, seconds);
            }
        } else {
            if (seconds == 0) {
                audio.queueAudio("/0.wav"); 
            } else {
                queueNumber(audio, seconds); 
            }
        }

        // Separador Decimal
        audio.queueAudio("/ponto.wav");

        // Milissegundos
        if (millis < 10) {
            audio.queueAudio("/0.wav");
            audio.queueAudio("/0.wav");
            audio.queueAudio(("/" + String(millis) + ".wav").c_str());
        } else if (millis < 100) {
            audio.queueAudio("/0.wav");
            queueNumber(audio, millis);
        } else {
            queueNumber(audio, millis);
        }
    }

} // Fim do namespace
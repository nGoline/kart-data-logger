#include "VoiceParser.h"

namespace VoiceParser {

    void queueNumber(AudioManager& audio, int num) {
        if (num < 0 || num > 999) return;

        if (num == 0) {
            audio.tryQueueAudio("/0.wav");
            return;
        }

        // Logic for 100-999 (Portuguese specific: "Cento e...", "Duzentos...")
        if (num >= 100) {
            int hundreds = (num / 100) * 100;
            int remainder = num % 100;

            if (num == 100) {
                audio.tryQueueAudio("/100.wav"); 
                return; 
            }
            
            if (hundreds == 100) {
                audio.tryQueueAudio("/100toe.wav"); // "Cento e..."
            } else {
                audio.tryQueueAudio(("/" + String(hundreds) + ".wav").c_str());
            }

            if (remainder > 0) {
                if (hundreds != 100) audio.tryQueueAudio("/e.wav"); // "Duzentos e..."
                queueNumber(audio, remainder);
            }
            return;
        }

        // Logic for 20-99
        if (num >= 20) {
            int tens = (num / 10) * 10;
            int units = num % 10;

            audio.tryQueueAudio(("/" + String(tens) + ".wav").c_str());

            if (units > 0) {
                audio.tryQueueAudio("/e.wav");
                queueNumber(audio, units);
            }
            return;
        }

        // 1-19 (Direct mapping)
        audio.tryQueueAudio(("/" + String(num) + ".wav").c_str());
    }

    void announceLapTime(AudioManager& audio, int minutes, int seconds, int millis) {
        log_i("Voice: Announcing %02d:%02d.%03d", minutes, seconds, millis);
        
        audio.tryQueueAudio("/lap_time.wav");

        if (minutes > 0) {
            queueNumber(audio, minutes);
            audio.tryQueueAudio("/minutos.wav"); // Assuming you have this file
            if (seconds > 0) audio.tryQueueAudio("/e.wav");
        }

        if (seconds > 0 || minutes == 0) {
            queueNumber(audio, seconds);
        }

        audio.tryQueueAudio("/ponto.wav");

        // Handle leading zeros for milliseconds (e.g., .005 vs .050 vs .500)
        if (millis < 10) {
            audio.tryQueueAudio("/0.wav");
            audio.tryQueueAudio("/0.wav");
            queueNumber(audio, millis);
        } else if (millis < 100) {
            audio.tryQueueAudio("/0.wav");
            queueNumber(audio, millis);
        } else {
            queueNumber(audio, millis);
        }
    }

} // namespace VoiceParser
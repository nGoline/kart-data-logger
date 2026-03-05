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

    void announceLapTime(AudioManager& audio, int minutes, int seconds, int millis, bool isBest) {
        log_i("Voice: Announcing %s", isBest ? "BEST LAP!" : "LAP TIME");
        
        if (isBest) {
            audio.tryQueueAudio("/best_lap.wav");
            audio.tryQueueAudio("/silence.wav");
        } else {
            audio.tryQueueAudio("/lap_time.wav");
            audio.tryQueueAudio("/silence.wav");
        }

        announceTime(audio, minutes, seconds, millis);
    }

    void announceDeltaTime(AudioManager& audio, int deltaMinutes, int deltaSeconds, int deltaMillis, bool isFaster) {
        log_i("Voice: Announcing DELTA %02d:%02d.%03d %s", deltaMinutes, deltaSeconds, deltaMillis, isFaster ? "FASTER" : "SLOWER");

        announceTime(audio, deltaMinutes, deltaSeconds, deltaMillis);
        audio.queueAudio("/silence.wav");
        if (isFaster) {
            audio.tryQueueAudio("/down.wav"); // Faster
        } else {
            audio.tryQueueAudio("/up.wav"); // Slower
        }
    }

    void announceTime(AudioManager& audio, int minutes, int seconds, int millis) {
        log_i("Voice: Announcing %02d:%02d.%03d", minutes, seconds, millis);
        
        if (minutes > 0) {
            queueNumber(audio, minutes);
        }

        queueNumber(audio, seconds);

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
#include "VoiceParser.h"

namespace VoiceParser {

    void queueNumber(AudioManager& audio, int num) {
        if (num < 0 || num > 999) return;

        if (num == 0) {
            if(!audio.tryQueueAudio("/0.wav")) {
                log_w("Audio file for 0 not queued!");
            }
            return;
        }

        // Logic for 100-999 (Portuguese specific: "Cento e...", "Duzentos...")
        if (num >= 100) {
            int hundreds = (num / 100) * 100;
            int remainder = num % 100;

            if (num == 100) {
                if(!audio.tryQueueAudio("/100.wav")) {
                    log_w("Audio file for 100 not queued!");
                }
                return; 
            }
            
            if (hundreds == 100) {
                if(!audio.tryQueueAudio("/100toe.wav")) {
                    log_w("Audio file for 100toe not queued!");
                } // "Cento e..."
            } else {
                if(!audio.tryQueueAudio(("/" + String(hundreds) + ".wav").c_str())) {
                    log_w("Audio file for %d not queued!", hundreds);
                }
            }

            if (remainder > 0) {
                if (hundreds != 100) {
                    if(!audio.tryQueueAudio("/e.wav")) {
                        log_w("Audio file for 'e' not queued!");
                    } // "Duzentos e..."
                }
                queueNumber(audio, remainder);
            }
            return;
        }

        // Logic for 20-99
        if (num >= 20) {
            int tens = (num / 10) * 10;
            int units = num % 10;

            if(!audio.tryQueueAudio(("/" + String(tens) + ".wav").c_str())) {
                log_w("Audio file for %d not queued!", tens);
            }

            if (units > 0) {
                if(!audio.tryQueueAudio("/e.wav")) {
                    log_w("Audio file for 'e' not queued!");
                }
                queueNumber(audio, units);
            }
            return;
        }

        // 1-19 (Direct mapping)
        if(!audio.tryQueueAudio(("/" + String(num) + ".wav").c_str())) {
            log_w("Audio file for %d not queued!", num);
        }
    }

    void announceLapTime(AudioManager& audio, int minutes, int seconds, int millis, bool isBest) {
        if (minutes > 0 || seconds > 0 || millis > 0) {
            log_i("Voice: Announcing %s", isBest ? "BEST LAP!" : "LAP TIME");

            if (isBest) {
                audio.tryQueueAudio("/best_lap.wav");
                audio.tryQueueAudio("/silence.wav");
            } else {
                audio.tryQueueAudio("/lap_time.wav");
                audio.tryQueueAudio("/silence.wav");
            }

            announceTime(audio, minutes, seconds, millis);
        } else {
            log_i("No time to announce for this lap. Probably first lap.");
        }
    }

    void announceDeltaTime(AudioManager& audio, int deltaMinutes, int deltaSeconds, int deltaMillis, bool isFaster) {
        log_i("Voice: Announcing DELTA %02d:%02d.%03d %s", deltaMinutes, deltaSeconds, deltaMillis, isFaster ? "FASTER" : "SLOWER");

        announceTime(audio, deltaMinutes, deltaSeconds, deltaMillis);
        audio.tryQueueAudio("/silence.wav");
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

        if (seconds == 0) {
            if(!audio.tryQueueAudio("/0.wav")) {
                log_w("Audio file for '0' not queued!");
            }
            if(!audio.tryQueueAudio("/0.wav")) {
                log_w("Audio file for '0' not queued!");
            }
        } else {
            queueNumber(audio, seconds);
            if(!audio.tryQueueAudio("/ponto.wav")) { // Decimal point for milliseconds
                log_w("Audio file for 'ponto' not queued!");
            }
        }

        // Handle leading zeros for milliseconds (e.g., .005 vs .050 vs .500)
        if (millis < 10) {
            if(!audio.tryQueueAudio("/0.wav")) {
                log_w("Audio file for '0' not queued!");
            }
            if(!audio.tryQueueAudio("/0.wav")) {
                log_w("Audio file for '0' not queued!");
            }
            queueNumber(audio, millis);
        } else if (millis < 100) {
            if(!audio.tryQueueAudio("/0.wav")) {
                log_w("Audio file for '0' not queued!");
            }
            queueNumber(audio, millis);
        } else {
            queueNumber(audio, millis);
        }
    }

} // namespace VoiceParser
#pragma once
#include <Arduino.h>

#define CONFIG_MAX_TRACKS 16

struct TrackConfig {
    char   name[48];
    double left_lat;
    double left_lon;
    bool   left_valid;
    double right_lat;
    double right_lon;
    bool   right_valid;
};

// 0 = dark (default), 1 = light
class ConfigManager {
public:
    // SD must be initialized by LogManager before calling begin().
    bool begin();

    // Persists theme + selected_track to /config.ini
    bool save();

    // Persists all track entries to /tracks.ini
    bool saveTracks();

    // Reloads /tracks.ini from SD, discarding any unsaved in-memory edits
    bool reloadTracks();

    uint8_t getTheme() const { return _theme; }
    void    setTheme(uint8_t t) { _theme = t; }

    uint8_t getSelectedTrack() const { return _selected_track; }
    void    setSelectedTrack(uint8_t t) { _selected_track = t; }

    int              getTrackCount()  const { return _track_count; }
    const TrackConfig* getTrack(int i) const;
    void             setTrack(int i, const TrackConfig& tc);

private:
    uint8_t    _theme          = 0;
    uint8_t    _selected_track = 0;
    int        _track_count    = 0;
    TrackConfig _tracks[CONFIG_MAX_TRACKS];

    bool parseConfig(const String &text);
    bool parseTracks(const String &text);
};

extern ConfigManager configManager;

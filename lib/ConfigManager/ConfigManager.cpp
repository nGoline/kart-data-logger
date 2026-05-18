#include "ConfigManager.h"

#if defined(JC3248W535)
#include <SD_MMC.h>
#define SD_FS SD_MMC
#else
#include <SD.h>
#define SD_FS SD
#endif

ConfigManager configManager;

static const char CONFIG_PATH[] = "/config.ini";
static const char TRACKS_PATH[] = "/tracks.ini";

bool ConfigManager::begin() {
    if (!SD_FS.exists(CONFIG_PATH)) {
        log_i("ConfigManager: no config file, writing defaults");
        save();
    } else {
        File f = SD_FS.open(CONFIG_PATH, FILE_READ);
        if (!f) {
            log_e("ConfigManager: failed to open %s", CONFIG_PATH);
        } else {
            String text = f.readString();
            f.close();
            parseConfig(text);
        }
    }

    if (SD_FS.exists(TRACKS_PATH)) {
        File f = SD_FS.open(TRACKS_PATH, FILE_READ);
        if (f) {
            String text = f.readString();
            f.close();
            parseTracks(text);
        }
    } else {
        log_i("ConfigManager: no tracks file");
    }

    return true;
}

bool ConfigManager::save() {
    File f = SD_FS.open(CONFIG_PATH, FILE_WRITE);
    if (!f) {
        log_e("ConfigManager: failed to write %s", CONFIG_PATH);
        return false;
    }
    f.println("# Kart Data Logger Configuration");
    f.println("# Edit this file on a computer to change settings.");
    f.println("#");
    f.print("theme=");
    f.println(_theme == 1 ? "light" : "dark");
    f.print("selected_track=");
    f.println(_selected_track);
    f.close();
    log_i("ConfigManager: saved (theme=%s, selected_track=%d)",
          _theme == 1 ? "light" : "dark", _selected_track);
    return true;
}

bool ConfigManager::saveTracks() {
    File f = SD_FS.open(TRACKS_PATH, FILE_WRITE);
    if (!f) {
        log_e("ConfigManager: failed to write %s", TRACKS_PATH);
        return false;
    }
    f.println("# Kart track configurations");
    f.println("# Edit this file on a computer to add or modify tracks.");
    f.println("#");
    f.print("count=");
    f.println(_track_count);
    for (int i = 0; i < _track_count; i++) {
        char buf[128];
        snprintf(buf, sizeof(buf), "%d_name=%s", i, _tracks[i].name);
        f.println(buf);
        if (_tracks[i].left_valid) {
            snprintf(buf, sizeof(buf), "%d_left_lat=%.8f", i, _tracks[i].left_lat);  f.println(buf);
            snprintf(buf, sizeof(buf), "%d_left_lon=%.8f", i, _tracks[i].left_lon);  f.println(buf);
        }
        if (_tracks[i].right_valid) {
            snprintf(buf, sizeof(buf), "%d_right_lat=%.8f", i, _tracks[i].right_lat); f.println(buf);
            snprintf(buf, sizeof(buf), "%d_right_lon=%.8f", i, _tracks[i].right_lon); f.println(buf);
        }
    }
    f.close();
    log_i("ConfigManager: saved %d tracks", _track_count);
    return true;
}

bool ConfigManager::reloadTracks() {
    if (!SD_FS.exists(TRACKS_PATH)) {
        memset(_tracks, 0, sizeof(_tracks));
        _track_count = 0;
        return true;
    }
    File f = SD_FS.open(TRACKS_PATH, FILE_READ);
    if (!f) {
        log_e("ConfigManager: failed to open %s for reload", TRACKS_PATH);
        return false;
    }
    String text = f.readString();
    f.close();
    return parseTracks(text);
}

const TrackConfig* ConfigManager::getTrack(int i) const {
    if (i < 0 || i >= _track_count) return nullptr;
    return &_tracks[i];
}

void ConfigManager::setTrack(int i, const TrackConfig& tc) {
    if (i < 0 || i >= CONFIG_MAX_TRACKS) return;
    _tracks[i] = tc;
    if (i >= _track_count) _track_count = i + 1;
}

bool ConfigManager::parseConfig(const String &text) {
    int pos = 0;
    while (pos < (int)text.length()) {
        int nl = text.indexOf('\n', pos);
        if (nl < 0) nl = (int)text.length();
        String line = text.substring(pos, nl);
        pos = nl + 1;
        line.trim();
        if (line.isEmpty() || line.startsWith("#")) continue;
        int eq = line.indexOf('=');
        if (eq < 0) continue;
        String key = line.substring(0, eq); key.trim();
        String val = line.substring(eq + 1); val.trim();
        if      (key == "theme")          _theme = (val == "light") ? 1 : 0;
        else if (key == "selected_track") _selected_track = (uint8_t)val.toInt();
    }
    log_i("ConfigManager: loaded (theme=%s, selected_track=%d)",
          _theme == 1 ? "light" : "dark", _selected_track);
    return true;
}

bool ConfigManager::parseTracks(const String &text) {
    memset(_tracks, 0, sizeof(_tracks));
    _track_count = 0;

    int found_count = -1;
    int max_idx     = -1;

    int pos = 0;
    while (pos < (int)text.length()) {
        int nl = text.indexOf('\n', pos);
        if (nl < 0) nl = (int)text.length();
        String line = text.substring(pos, nl);
        pos = nl + 1;
        line.trim();
        if (line.isEmpty() || line.startsWith("#")) continue;
        int eq = line.indexOf('=');
        if (eq < 0) continue;
        String key = line.substring(0, eq); key.trim();
        String val = line.substring(eq + 1); val.trim();

        if (key == "count") { found_count = val.toInt(); continue; }

        // Keys of the form "<index>_<field>", e.g. "0_name", "1_left_lat"
        int us = key.indexOf('_');
        if (us < 1) continue;
        int    idx   = key.substring(0, us).toInt();
        String field = key.substring(us + 1);
        if (idx < 0 || idx >= CONFIG_MAX_TRACKS) continue;
        if (idx > max_idx) max_idx = idx;

        if      (field == "name")      { val.toCharArray(_tracks[idx].name, sizeof(_tracks[idx].name)); }
        else if (field == "left_lat")  { _tracks[idx].left_lat   = val.toDouble(); _tracks[idx].left_valid  = true; }
        else if (field == "left_lon")  { _tracks[idx].left_lon   = val.toDouble(); }
        else if (field == "right_lat") { _tracks[idx].right_lat  = val.toDouble(); _tracks[idx].right_valid = true; }
        else if (field == "right_lon") { _tracks[idx].right_lon  = val.toDouble(); }
    }

    _track_count = (found_count >= 0) ? found_count : (max_idx + 1);
    if (_track_count < 0) _track_count = 0;
    if (_track_count > CONFIG_MAX_TRACKS) _track_count = CONFIG_MAX_TRACKS;

    if (_track_count > 0 && _selected_track >= (uint8_t)_track_count)
        _selected_track = 0;

    log_i("ConfigManager: loaded %d tracks", _track_count);
    return true;
}

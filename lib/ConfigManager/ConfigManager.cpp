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

bool ConfigManager::begin() {
    if (!SD_FS.exists(CONFIG_PATH)) {
        log_i("ConfigManager: no config file, writing defaults");
        return save();
    }
    File f = SD_FS.open(CONFIG_PATH, FILE_READ);
    if (!f) {
        log_e("ConfigManager: failed to open %s", CONFIG_PATH);
        return false;
    }
    String text = f.readString();
    f.close();
    return parseFile(text);
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
    f.close();
    log_i("ConfigManager: saved (theme=%s)", _theme == 1 ? "light" : "dark");
    return true;
}

bool ConfigManager::parseFile(const String &text) {
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
        if (key == "theme") _theme = (val == "light") ? 1 : 0;
    }
    log_i("ConfigManager: loaded (theme=%s)", _theme == 1 ? "light" : "dark");
    return true;
}

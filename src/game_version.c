#include "constants/versions.h"

const u8 gGameVersion = GAME_VERSION;
#ifdef SDK_PORT
#include <stdio.h>
#include <string.h>

u8 gGameLanguage = LANGUAGE_ENGLISH;

void GameLanguage_Init(void)
{
    // Create writable simulator settings locally; no firmware image is shipped.
    FILE *settings = fopen("firmware.bin", "wbx");
    if (settings != NULL) {
        const unsigned char emptySettings[1584] = {0};
        fwrite(emptySettings, 1, sizeof(emptySettings), settings);
        fclose(settings);
    }
    unsigned char header[16];
    FILE *file = fopen("assets/header.bin", "rb");
    gGameLanguage = LANGUAGE_ENGLISH;
    if (file != NULL) {
        if (fread(header, 1, sizeof(header), file) == sizeof(header)
            && memcmp(header, "POKEMON PL", 10) == 0
            && memcmp(header + 12, "CPU", 3) == 0) {
            switch (header[15]) {
            case 'J': gGameLanguage = LANGUAGE_JAPANESE; break;
            case 'F': gGameLanguage = LANGUAGE_FRENCH; break;
            case 'D': gGameLanguage = LANGUAGE_GERMAN; break;
            case 'I': gGameLanguage = LANGUAGE_ITALIAN; break;
            case 'S': gGameLanguage = LANGUAGE_SPANISH; break;
            case 'K': gGameLanguage = LANGUAGE_KOREAN; break;
            default: gGameLanguage = LANGUAGE_ENGLISH; break;
            }
        }
        fclose(file);
    }
}
#else
const u8 gGameLanguage = GAME_LANGUAGE;
#endif

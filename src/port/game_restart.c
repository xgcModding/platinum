#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifdef _WIN32
#include <process.h>
#endif

#ifdef __APPLE__
#include <crt_externs.h>
#include <mach-o/dyld.h>
#endif

#include "port/game_shortcuts.h"
#include "system.h"
// ROM import is provided by desktop builds; other ports keep native restart.
extern int RomImport_TakeRestartRequest(void) __attribute__((weak));
extern void RomImport_ReportError(const char *) __attribute__((weak));
extern void RomImport_Shutdown(void) __attribute__((weak));


static int sRequested;
static int sImportRestart;
static char **sArguments;
static size_t sArgumentCount;
#ifdef __APPLE__
static char sExecutable[4096];
#elif defined(_WIN32)
static char *sExecutable;
#else
static const char sExecutable[] = "/proc/self/exe";
#endif

static void FreeArguments(void)
{
    for (size_t i = 0; i < sArgumentCount; i++) {
        free(sArguments[i]);
    }
    free(sArguments);
    sArguments = NULL;
    sArgumentCount = 0;
}

static int AddArgument(const char *argument)
{
    char **arguments = realloc(sArguments, (sArgumentCount + 2) * sizeof(*arguments));
    if (arguments == NULL) {
        return 0;
    }
    sArguments = arguments;
    size_t length = strlen(argument) + 1;
    sArguments[sArgumentCount] = malloc(length);
    if (sArguments[sArgumentCount] == NULL) {
        return 0;
    }
    memcpy(sArguments[sArgumentCount], argument, length);
    sArguments[++sArgumentCount] = NULL;
    return 1;
}

static int ReadArguments(void)
{
#ifdef __APPLE__
    uint32_t size = sizeof(sExecutable);
    if (_NSGetExecutablePath(sExecutable, &size) != 0) {
        return 0;
    }
    char **arguments = *_NSGetArgv();
    for (int i = 0; i < *_NSGetArgc(); i++) {
        if (!AddArgument(arguments[i])) {
            return 0;
        }
    }
#elif defined(_WIN32)
    if (_get_pgmptr(&sExecutable) != 0) {
        return 0;
    }
    for (int i = 0; i < __argc; i++) {
        if (!AddArgument(__argv[i])) {
            return 0;
        }
    }
#else
    FILE *file = fopen("/proc/self/cmdline", "rb");
    if (file == NULL) {
        return 0;
    }
    char *argument = NULL;
    size_t capacity = 0;
    size_t length = 0;
    int success = 1;
    int ch;
    while ((ch = fgetc(file)) != EOF) {
        if (length == capacity) {
            size_t newCapacity = capacity ? capacity * 2 : 128;
            char *buffer = realloc(argument, newCapacity);
            if (buffer == NULL) {
                success = 0;
                break;
            }
            argument = buffer;
            capacity = newCapacity;
        }
        argument[length++] = ch;
        if (ch == '\0') {
            if (!AddArgument(argument)) {
                success = 0;
                break;
            }
            length = 0;
        }
    }
    success = success && !ferror(file);
    free(argument);
    fclose(file);
    if (!success) {
        return 0;
    }
#endif
    return sArgumentCount != 0;
}

void GameRestart_Request(void)
{
    __atomic_store_n(&sRequested, 1, __ATOMIC_RELEASE);
}

void GameRestart_Update(void)
{
    if (RomImport_TakeRestartRequest && RomImport_TakeRestartRequest()) {
        sImportRestart = 1;
        GameRestart_Request();
    }
    // SDL key callbacks may run on the rendering thread. Restart only at the
    // game-loop boundary, after any active save or reset lock has completed.
    if (gSystem.inhibitReset || (gSystem.inhibitSleep & SLEEP_TYPE_SAVE_DATA)) {
        return;
    }
    if (!__atomic_exchange_n(&sRequested, 0, __ATOMIC_ACQ_REL)) {
        return;
    }

    // The DS reset path does not reboot the desktop port. Re-exec resets all
    // game/mod memory while preserving arguments, environment and working dir.
    if (sArguments == NULL) {
        if (sImportRestart) {
            if (RomImport_ReportError) RomImport_ReportError("ROM imported, but the game could not restart. Close and reopen the game.");
            sImportRestart = 0;
        }
        return;
    }
    if (sImportRestart) {
        // Extraction is committed. Join its worker before replacing the process.
        if (RomImport_Shutdown) RomImport_Shutdown();
    }
#ifdef _WIN32
    _execv(sExecutable, (const char *const *)sArguments);
#else
    execv(sExecutable, sArguments);
#endif

    char message[256];
    snprintf(message, sizeof(message), "Could not restart the game: %s", strerror(errno));
    fprintf(stderr, "[Restart] %s\n", message);
    if (sImportRestart) {
        if (RomImport_ReportError) RomImport_ReportError("ROM imported, but the game could not restart. Close and reopen the game.");
        sImportRestart = 0;
    }
}

void GameShortcuts_Init(void)
{
    if (!ReadArguments()) {
        FreeArguments();
        fprintf(stderr, "[Restart] Could not read executable arguments\n");
    }
}

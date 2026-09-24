#ifndef PORT_ROM_IMPORT_H
#define PORT_ROM_IMPORT_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
/* 0 idle, 1 extracting, 2 complete, 3 failed. Text is always copied. */
int RomImport_GetStatus(char *text, size_t capacity);
void RomImport_Start(const char *filename);
void RomImport_ReportError(const char *message);
void RomImport_Shutdown(void);
int RomImport_TakeRestartRequest(void);
int RomImport_UseExecutableDirectory(void);
void RomImport_OpenPicker(void *window);
void RomImport_PumpPicker(void);
int RomImport_PickerActive(void);
/* Extract into directory/assets; directory is the executable folder. */
int RomImport_ExtractTo(const char *filename, const char *directory, char *error, size_t capacity);
#ifdef __cplusplus
}
#endif
#endif

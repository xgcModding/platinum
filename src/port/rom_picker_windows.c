#include "rom_import.h"
#include <SDL.h>
#include <SDL_syswm.h>
#include <windows.h>
#include <commdlg.h>
static int active;
void RomImport_OpenPicker(void *window)
{
    if(active) return;
    active=1;
    wchar_t filename[32768]={0};
    SDL_SysWMinfo info; SDL_VERSION(&info.version);
    OPENFILENAMEW dialog={0};
    dialog.lStructSize=sizeof(dialog);
    if(SDL_GetWindowWMInfo(window,&info)) dialog.hwndOwner=info.info.win.window;
    dialog.lpstrTitle=L"Import ROM";
    dialog.lpstrFilter=L"Nintendo DS ROM (*.nds)\0*.nds\0\0";
    dialog.lpstrFile=filename; dialog.nMaxFile=32768;
    dialog.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
    if(GetOpenFileNameW(&dialog)) {
        char utf8[131072];
        if(WideCharToMultiByte(CP_UTF8,0,filename,-1,utf8,sizeof(utf8),NULL,NULL)) RomImport_Start(utf8);
        else RomImport_ReportError("The selected file path could not be read.");
    } else if(CommDlgExtendedError()) RomImport_ReportError("The system file picker could not be opened.");
    active=0;
}
void RomImport_PumpPicker(void) {}
int RomImport_PickerActive(void) { return active; }

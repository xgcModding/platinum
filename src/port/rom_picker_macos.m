#include "rom_import.h"
#import <AppKit/AppKit.h>
static int active;
void RomImport_OpenPicker(void *window)
{
    (void)window;
    if(active) return;
    active=1;
    @autoreleasepool {
        NSOpenPanel *panel=[NSOpenPanel openPanel];
        panel.title=@"Import ROM";
        panel.prompt=@"Import ROM";
        panel.allowedFileTypes=@[@"nds"];
        panel.canChooseFiles=YES;
        panel.canChooseDirectories=NO;
        panel.allowsMultipleSelection=NO;
        if([panel runModal]==NSModalResponseOK) RomImport_Start(panel.URL.path.UTF8String);
    }
    active=0;
}
void RomImport_PumpPicker(void) {}
int RomImport_PickerActive(void) { return active; }

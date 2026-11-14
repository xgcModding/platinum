#import <AppKit/AppKit.h>
#include "app_menu_actions.h"

// Menu callbacks enqueue actions; the simulator handles them on its next frame.
static int pendingAction;
static NSMenuItem *importItem;

void PlatformMenu_RequestAction(int action)
{
    if (action == LAKE_TRIO_MENU_IMPORT_ROM || action == LAKE_TRIO_MENU_CONFIGURE_CONTROLS)
        pendingAction = action;
}
@interface PlatinumMenuActions : NSObject
- (void)importROM:(id)sender;
- (void)configureInput:(id)sender;
@end
@implementation PlatinumMenuActions
- (void)importROM:(id)sender { (void)sender; PlatformMenu_RequestAction(LAKE_TRIO_MENU_IMPORT_ROM); }
- (void)configureInput:(id)sender { (void)sender; PlatformMenu_RequestAction(LAKE_TRIO_MENU_CONFIGURE_CONTROLS); }
@end

int PlatformMenu_Update(int canImport)
{
    static PlatinumMenuActions *actions;
    if (!actions) {
        actions = [PlatinumMenuActions new];
        NSMenu *bar = NSApp.mainMenu;
        if (!bar) {
            bar = [[NSMenu alloc] initWithTitle:@""];
            NSApp.mainMenu = bar;
        }
        NSMenuItem *file = [[NSMenuItem alloc] initWithTitle:@"File" action:NULL keyEquivalent:@""];
        NSMenu *fileMenu = [[NSMenu alloc] initWithTitle:@"File"];
        fileMenu.autoenablesItems = NO;
        importItem = [[NSMenuItem alloc] initWithTitle:@"Import ROM…" action:@selector(importROM:) keyEquivalent:@"o"];
        importItem.target = actions;
        [fileMenu addItem:importItem];
        file.submenu = fileMenu;
        [bar addItem:file];
        NSMenuItem *input = [[NSMenuItem alloc] initWithTitle:@"Input" action:NULL keyEquivalent:@""];
        NSMenu *inputMenu = [[NSMenu alloc] initWithTitle:@"Input"];
        NSMenuItem *configure = [[NSMenuItem alloc] initWithTitle:@"Configure Controls…" action:@selector(configureInput:) keyEquivalent:@","];
        configure.target = actions;
        [inputMenu addItem:configure];
        input.submenu = inputMenu;
        [bar addItem:input];
    }
    importItem.enabled = canImport != 0;
    int action = pendingAction;
    pendingAction = 0;
    return (action == LAKE_TRIO_MENU_IMPORT_ROM && !canImport) ? LAKE_TRIO_MENU_NONE : action;
}

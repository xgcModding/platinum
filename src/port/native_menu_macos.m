#import <AppKit/AppKit.h>

// Menu callbacks enqueue actions; the simulator handles them on its next frame.
static int pendingAction;
static NSMenuItem *importItem;
@interface PlatinumMenuActions : NSObject
- (void)importROM:(id)sender;
- (void)configureInput:(id)sender;
@end
@implementation PlatinumMenuActions
- (void)importROM:(id)sender { (void)sender; pendingAction = 1; }
- (void)configureInput:(id)sender { (void)sender; pendingAction = 2; }
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
    return action;
}

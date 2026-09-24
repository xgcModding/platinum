#include "rom_import.h"
#include <dbus/dbus.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
static DBusConnection *connection;
static int active;
static char request_path[512];
static void string_option(DBusMessageIter *options, const char *key, const char *value)
{
    DBusMessageIter entry, variant;
    dbus_message_iter_open_container(options, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
    dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "s", &variant);
    dbus_message_iter_append_basic(&variant, DBUS_TYPE_STRING, &value);
    dbus_message_iter_close_container(&entry, &variant);
    dbus_message_iter_close_container(options, &entry);
}
static int hex(char c) { return c >= '0' && c <= '9' ? c-'0' : c >= 'a' && c <= 'f' ? c-'a'+10 : c >= 'A' && c <= 'F' ? c-'A'+10 : -1; }
static void import_uri(const char *uri)
{
    const char *p;
    if (!strncmp(uri,"file:///",8)) p=uri+7;
    else if (!strncmp(uri,"file://localhost/",17)) p=uri+16;
    else { RomImport_ReportError("Select a local Nintendo DS ROM file."); return; }
    char *path=malloc(strlen(p)+1), *out=path;
    if (!path) { RomImport_ReportError("Not enough memory to import the ROM."); return; }
    while (*p) {
        if (*p=='%') {
            if (!p[1] || !p[2] || hex(p[1])<0 || hex(p[2])<0 || !(hex(p[1])*16+hex(p[2]))) { free(path); RomImport_ReportError("The selected file path is invalid."); return; }
            *out++=(char)(hex(p[1])*16+hex(p[2])); p+=3;
        } else *out++=*p++;
    }
    *out=0; RomImport_Start(path); free(path);
}
void RomImport_OpenPicker(void *window)
{
    (void)window;
    if(active) return;
    DBusError error; dbus_error_init(&error);
    if (!connection) {
        connection=dbus_bus_get_private(DBUS_BUS_SESSION,&error);
        if(connection) {
            dbus_connection_set_exit_on_disconnect(connection,FALSE);
            dbus_bus_add_match(connection,"type='signal',interface='org.freedesktop.portal.Request',member='Response'",&error);
        }
    }
    if(!connection || dbus_error_is_set(&error)) { dbus_error_free(&error); RomImport_ReportError("The desktop file picker is unavailable. Install and enable your desktop's file chooser portal."); return; }
    DBusMessage *message=dbus_message_new_method_call("org.freedesktop.portal.Desktop","/org/freedesktop/portal/desktop","org.freedesktop.portal.FileChooser","OpenFile");
    DBusMessageIter args, options, entry, variant, filters, filter, patterns, pattern;
    dbus_message_iter_init_append(message,&args);
    const char *parent="", *title="Import ROM";
    dbus_message_iter_append_basic(&args,DBUS_TYPE_STRING,&parent);
    dbus_message_iter_append_basic(&args,DBUS_TYPE_STRING,&title);
    dbus_message_iter_open_container(&args,DBUS_TYPE_ARRAY,"{sv}",&options);
    string_option(&options,"accept_label","Import ROM");
    const char *key="filters", *label="Nintendo DS ROM (*.nds)";
    dbus_message_iter_open_container(&options,DBUS_TYPE_DICT_ENTRY,NULL,&entry);
    dbus_message_iter_append_basic(&entry,DBUS_TYPE_STRING,&key);
    dbus_message_iter_open_container(&entry,DBUS_TYPE_VARIANT,"a(sa(us))",&variant);
    dbus_message_iter_open_container(&variant,DBUS_TYPE_ARRAY,"(sa(us))",&filters);
    dbus_message_iter_open_container(&filters,DBUS_TYPE_STRUCT,NULL,&filter);
    dbus_message_iter_append_basic(&filter,DBUS_TYPE_STRING,&label);
    dbus_message_iter_open_container(&filter,DBUS_TYPE_ARRAY,"(us)",&patterns);
    const char *globs[]={"*.nds","*.NDS"};
    for(int i=0;i<2;i++) {
        dbus_uint32_t type=0;
        dbus_message_iter_open_container(&patterns,DBUS_TYPE_STRUCT,NULL,&pattern);
        dbus_message_iter_append_basic(&pattern,DBUS_TYPE_UINT32,&type);
        dbus_message_iter_append_basic(&pattern,DBUS_TYPE_STRING,&globs[i]);
        dbus_message_iter_close_container(&patterns,&pattern);
    }
    dbus_message_iter_close_container(&filter,&patterns);
    dbus_message_iter_close_container(&filters,&filter);
    dbus_message_iter_close_container(&variant,&filters);
    dbus_message_iter_close_container(&entry,&variant);
    dbus_message_iter_close_container(&options,&entry);
    dbus_message_iter_close_container(&args,&options);
    DBusMessage *reply=dbus_connection_send_with_reply_and_block(connection,message,5000,&error);
    dbus_message_unref(message);
    const char *path=NULL;
    if(reply && dbus_message_get_args(reply,&error,DBUS_TYPE_OBJECT_PATH,&path,DBUS_TYPE_INVALID)) { snprintf(request_path,sizeof(request_path),"%s",path); active=1; }
    else RomImport_ReportError("The desktop file picker could not be opened.");
    if(reply) dbus_message_unref(reply);
    dbus_error_free(&error);
}
void RomImport_PumpPicker(void)
{
    if(!connection || !active) return;
    if(!dbus_connection_read_write(connection,0)) { active=0; RomImport_ReportError("The desktop file picker disconnected."); return; }
    DBusMessage *message;
    while((message=dbus_connection_pop_message(connection))) {
        if(dbus_message_is_signal(message,"org.freedesktop.portal.Request","Response") && !strcmp(dbus_message_get_path(message),request_path)) {
            active=0;
            DBusMessageIter args, results, entry, variant, uris;
            dbus_uint32_t response=2;
            if(dbus_message_iter_init(message,&args) && dbus_message_iter_get_arg_type(&args)==DBUS_TYPE_UINT32) {
                dbus_message_iter_get_basic(&args,&response);
                if(response==0 && dbus_message_iter_next(&args) && dbus_message_iter_get_arg_type(&args)==DBUS_TYPE_ARRAY) {
                    dbus_message_iter_recurse(&args,&results);
                    while(dbus_message_iter_get_arg_type(&results)==DBUS_TYPE_DICT_ENTRY) {
                        const char *key;
                        dbus_message_iter_recurse(&results,&entry); dbus_message_iter_get_basic(&entry,&key);
                        dbus_message_iter_next(&entry); dbus_message_iter_recurse(&entry,&variant);
                        if(!strcmp(key,"uris") && dbus_message_iter_get_arg_type(&variant)==DBUS_TYPE_ARRAY) {
                            dbus_message_iter_recurse(&variant,&uris);
                            if(dbus_message_iter_get_arg_type(&uris)==DBUS_TYPE_STRING) { const char *uri; dbus_message_iter_get_basic(&uris,&uri); import_uri(uri); }
                        }
                        dbus_message_iter_next(&results);
                    }
                }
            }
            if(response>1) RomImport_ReportError("The desktop file picker failed.");
        }
        dbus_message_unref(message);
    }
}
int RomImport_PickerActive(void) { return active; }

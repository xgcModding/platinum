#ifndef MOD_PLATFORM_WINDOWS_H
#define MOD_PLATFORM_WINDOWS_H

#include <stddef.h>

/* Keep Windows SDK types outside the Nitro precompiled header. */
void *ModWindows_ResolveSymbol(const char *name);
void *ModWindows_LoadLibrary(const char *path, int flags);
void *ModWindows_LibrarySymbol(void *library, const char *name);
const char *ModWindows_LibraryError(void);
int ModWindows_CloseLibrary(void *library);
long ModWindows_PageSize(int unused);
void *ModWindows_Allocate(void *address, size_t size, int protection, int flags, int fd, long offset);
int ModWindows_Protect(void *address, size_t size, int protection);
int ModWindows_Free(void *address, size_t size);
void ModWindows_Flush(void *start, void *end);
char *ModWindows_RealPath(const char *path, char *resolved);
int ModWindows_ExecutablePath(char *path, size_t size);
int ModWindows_ReplaceFile(const char *source, const char *destination);

#define PROT_READ 1
#define PROT_WRITE 2
#define PROT_EXEC 4
#define MAP_PRIVATE 0
#define MAP_ANONYMOUS 0
#define MAP_FAILED ((void *)-1)
#define _SC_PAGESIZE 0
#define RTLD_NOW 0
#define RTLD_LOCAL 0
#define dlopen ModWindows_LoadLibrary
#define dlsym ModWindows_LibrarySymbol
#define dlerror ModWindows_LibraryError
#define dlclose ModWindows_CloseLibrary
#define sysconf ModWindows_PageSize
#define mmap ModWindows_Allocate
#define mprotect ModWindows_Protect
#define munmap ModWindows_Free
#define realpath ModWindows_RealPath
#define rename ModWindows_ReplaceFile
#define __builtin___clear_cache ModWindows_Flush

#endif

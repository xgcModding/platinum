#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mods/platform_windows.h"

static char libraryError[256];
static int hasLibraryError;

static void RecordLibraryError(void)
{
    DWORD error = GetLastError();
    snprintf(libraryError, sizeof(libraryError), "Windows loader error %lu", (unsigned long)error);
    hasLibraryError = 1;
}

void *ModWindows_LoadLibrary(const char *path, int flags)
{
    (void)flags;
    HMODULE library = LoadLibraryA(path);
    if (!library) RecordLibraryError();
    return library;
}

void *ModWindows_LibrarySymbol(void *library, const char *name)
{
    void *symbol = (void *)GetProcAddress((HMODULE)library, name);
    if (!symbol) RecordLibraryError();
    return symbol;
}

const char *ModWindows_LibraryError(void)
{
    if (!hasLibraryError) return NULL;
    hasLibraryError = 0;
    return libraryError;
}

int ModWindows_CloseLibrary(void *library)
{
    return FreeLibrary((HMODULE)library) ? 0 : -1;
}

long ModWindows_PageSize(int unused)
{
    (void)unused;
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    return info.dwPageSize;
}

static DWORD Protection(int protection)
{
    if (protection & PROT_EXEC)
        return protection & PROT_WRITE ? PAGE_EXECUTE_READWRITE : PAGE_EXECUTE_READ;
    return protection & PROT_WRITE ? PAGE_READWRITE : PAGE_READONLY;
}

void *ModWindows_Allocate(void *address, size_t size, int protection, int flags, int fd, long offset)
{
    (void)flags; (void)fd; (void)offset;
    void *memory = VirtualAlloc(address, size, MEM_RESERVE | MEM_COMMIT, Protection(protection));
    return memory ? memory : MAP_FAILED;
}

int ModWindows_Protect(void *address, size_t size, int protection)
{
    DWORD previous;
    return VirtualProtect(address, size, Protection(protection), &previous) ? 0 : -1;
}

int ModWindows_Free(void *address, size_t size)
{
    (void)size;
    return VirtualFree(address, 0, MEM_RELEASE) ? 0 : -1;
}

void ModWindows_Flush(void *start, void *end)
{
    FlushInstructionCache(GetCurrentProcess(), start, (char *)end - (char *)start);
}

static void NormalizePath(char *path)
{
    for (; *path; path++) if (*path == '\\') *path = '/';
}

char *ModWindows_RealPath(const char *path, char *resolved)
{
    if (!_fullpath(resolved, path, 4096)) return NULL;
    NormalizePath(resolved);
    return resolved;
}

int ModWindows_ExecutablePath(char *path, size_t size)
{
    DWORD length = GetModuleFileNameA(NULL, path, (DWORD)size);
    if (!length || length >= size) return 0;
    NormalizePath(path);
    return 1;
}

int ModWindows_ReplaceFile(const char *source, const char *destination)
{
    return MoveFileExA(source, destination, MOVEFILE_REPLACE_EXISTING) ? 0 : -1;
}

/* COFF symbols retain private game functions, just as the ELF fallback does.
 * Section RVAs are relocated against the loaded module so ASLR remains enabled.
 * The release executable must retain its symbol table (no strip). */
void *ModWindows_ResolveSymbol(const char *name)
{
    unsigned char *module = (unsigned char *)GetModuleHandleA(NULL);
    void *exported = (void *)GetProcAddress((HMODULE)module, name);
    if (exported) return exported;

    static unsigned char *file;
    static size_t fileSize;
    static int attempted;
    if (!attempted) {
        attempted = 1;
        char path[4096];
        if (!ModWindows_ExecutablePath(path, sizeof(path))) return NULL;
        HANDLE executable = CreateFileA(path, GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
        if (executable == INVALID_HANDLE_VALUE) return NULL;
        LARGE_INTEGER length;
        if (GetFileSizeEx(executable, &length) && length.QuadPart > 0
            && (uint64_t)length.QuadPart <= SIZE_MAX) {
            HANDLE mapping = CreateFileMappingA(executable, NULL, PAGE_READONLY, 0, 0, NULL);
            if (mapping) {
                file = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
                if (file) fileSize = (size_t)length.QuadPart;
                CloseHandle(mapping);
            }
        }
        CloseHandle(executable);
    }
    if (!file || fileSize < sizeof(IMAGE_DOS_HEADER)) return NULL;
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)file;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0
        || (size_t)dos->e_lfanew > fileSize - sizeof(IMAGE_NT_HEADERS64)) return NULL;
    IMAGE_NT_HEADERS64 *pe = (IMAGE_NT_HEADERS64 *)(file + dos->e_lfanew);
    if (pe->Signature != IMAGE_NT_SIGNATURE) return NULL;
    size_t offset = pe->FileHeader.PointerToSymbolTable;
    size_t count = pe->FileHeader.NumberOfSymbols;
    if (!offset || offset > fileSize || count > (fileSize - offset) / sizeof(IMAGE_SYMBOL)) return NULL;
    size_t stringsOffset = offset + count * sizeof(IMAGE_SYMBOL);
    if (fileSize - stringsOffset < 4) return NULL;
    uint32_t stringsSize;
    memcpy(&stringsSize, file + stringsOffset, 4);
    if (stringsSize < 4 || stringsSize > fileSize - stringsOffset) return NULL;
    IMAGE_SECTION_HEADER *sections = IMAGE_FIRST_SECTION(pe);
    size_t sectionOffset = (unsigned char *)sections - file;
    if (sectionOffset > fileSize || pe->FileHeader.NumberOfSections >
        (fileSize - sectionOffset) / sizeof(*sections)) return NULL;
    IMAGE_SYMBOL *symbols = (IMAGE_SYMBOL *)(file + offset);
    for (size_t i = 0; i < count; i += 1 + symbols[i].NumberOfAuxSymbols) {
        IMAGE_SYMBOL *symbol = &symbols[i];
        if (symbol->SectionNumber <= 0 || symbol->SectionNumber > pe->FileHeader.NumberOfSections) continue;
        char shortName[9] = {0};
        const char *symbolName;
        if (symbol->N.Name.Short) {
            memcpy(shortName, symbol->N.ShortName, 8);
            symbolName = shortName;
        } else {
            size_t stringOffset = symbol->N.Name.Long;
            if (stringOffset < 4 || stringOffset >= stringsSize) continue;
            symbolName = (char *)file + stringsOffset + stringOffset;
            if (!memchr(symbolName, 0, stringsSize - stringOffset)) continue;
        }
        if (strcmp(symbolName, name) == 0)
            return module + sections[symbol->SectionNumber - 1].VirtualAddress + symbol->Value;
    }
    return NULL;
}

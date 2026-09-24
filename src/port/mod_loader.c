#define _GNU_SOURCE

#include "mods/runtime.h"

#include <stddef.h>

#if defined(__linux__) || defined(__APPLE__) || defined(_WIN32)

#include "mods/lua_mod.h"

#include <dirent.h>
#if defined(_WIN32)
#include "mods/platform_windows.h"
#else
#include <dlfcn.h>
#include <sys/mman.h>
#endif
#if defined(__linux__)
#include <elf.h>
#endif
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <mach-o/nlist.h>
#endif

#if !defined(_WIN32)
extern ssize_t readlink(const char *path, char *buffer, size_t bufferSize);
extern char *realpath(const char *path, char *resolvedPath);
#endif

#define MAX_LOADED_MODS      64
#define MAX_START_MENU_ITEMS 16
#define MAX_MOD_FILENAME     256
#define MAX_MOD_PATH         4096
#define MAX_MENU_LABEL       48
#define MAX_MOD_HOOKS        128
#if defined(__APPLE__) && defined(__aarch64__)
#define MOD_HOOK_JUMP_SIZE   16
#define MOD_ARCH_ARM64       1
#else
#define MOD_HOOK_JUMP_SIZE   12
#define MOD_ARCH_ARM64       0
#endif
#define MAX_HOOK_BYTES       32
#define MAX_MOD_OPTIONS      POKEPLATINUM_MOD_MAX_OPTIONS
#define MAX_OPTION_VALUES    POKEPLATINUM_MOD_MAX_OPTION_VALUES
#define MAX_OPTION_TEXT      48
#define MAX_OPTION_KEY       64
#define MAX_KEY_BINDINGS     128
typedef struct LoadedMod {
    void *library;
    LuaMod *lua;
    const PokePlatinumMod *descriptor;
} LoadedMod;

typedef struct ModStartMenuItem {
    char label[MAX_MENU_LABEL];
    PokePlatinumModMenuCallback callback;
    void *userData;
    const PokePlatinumMod *owner;
} ModStartMenuItem;

typedef struct RegisteredModOption {
    char label[MAX_OPTION_TEXT];
    char configKey[MAX_OPTION_KEY];
    char configPath[MAX_MOD_PATH];
    char values[MAX_OPTION_VALUES][MAX_OPTION_TEXT];
    uint32_t valueCount;
    uint32_t selectedValue;
    PokePlatinumModOptionChangedCallback onChanged;
    void *userData;
    const PokePlatinumMod *owner;
    uint32_t flags;
} RegisteredModOption;

typedef struct RegisteredKeyBinding {
    int32_t keycode;
    PokePlatinumModKeyCallback callback;
    void *userData;
    const PokePlatinumMod *owner;
} RegisteredKeyBinding;

struct PokePlatinumModHook {
    void *target;
    void *replacement;
    void *trampoline;
    size_t trampolineSize;
    size_t overwrittenSize;
    unsigned char original[MAX_HOOK_BYTES];
    void **originalFunction;
    struct PokePlatinumModHook *previous;
    struct PokePlatinumModHook *next;
    const PokePlatinumMod *owner;
    int installed;
};

static LoadedMod sLoadedMods[MAX_LOADED_MODS];
static unsigned int sLoadedModCount;
static ModStartMenuItem sStartMenuItems[MAX_START_MENU_ITEMS];
static unsigned int sStartMenuItemCount;
static char sLoadedDirectories[3][MAX_MOD_PATH];
static unsigned int sLoadedDirectoryCount;
static PokePlatinumModHook sHooks[MAX_MOD_HOOKS];
static RegisteredModOption sModOptions[MAX_MOD_OPTIONS];
static unsigned int sModOptionCount;
static RegisteredKeyBinding sKeyBindings[MAX_KEY_BINDINGS];
static unsigned int sKeyBindingCount;
static const PokePlatinumMod *sActiveMod;
static int sInitialized;

static void Log(const char *modId, const char *message)
{
    fprintf(stderr, "[mods] %s: %s\n", modId != NULL ? modId : "loader", message);
}

#if defined(__linux__)
static void *ResolveElfSymbol(const char *name)
{
    char executable[MAX_MOD_PATH];
    ssize_t executableLength = readlink("/proc/self/exe", executable, sizeof(executable) - 1);
    if (executableLength <= 0) {
        return NULL;
    }
    executable[executableLength] = '\0';

    int fd = open(executable, O_RDONLY);
    if (fd < 0) {
        return NULL;
    }

    struct stat fileInfo;
    if (fstat(fd, &fileInfo) != 0 || fileInfo.st_size < (off_t)sizeof(Elf64_Ehdr)) {
        close(fd);
        return NULL;
    }

    void *mapping = mmap(NULL, fileInfo.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (mapping == MAP_FAILED) {
        return NULL;
    }

    void *result = NULL;
    const Elf64_Ehdr *header = mapping;
    if (memcmp(header->e_ident, ELFMAG, SELFMAG) == 0
        && header->e_ident[EI_CLASS] == ELFCLASS64
        && header->e_shoff + (header->e_shnum * sizeof(Elf64_Shdr)) <= (size_t)fileInfo.st_size) {
        const Elf64_Shdr *sections = (const Elf64_Shdr *)((const char *)mapping + header->e_shoff);
        for (unsigned int i = 0; i < header->e_shnum && result == NULL; i++) {
            if (sections[i].sh_type != SHT_SYMTAB || sections[i].sh_link >= header->e_shnum) {
                continue;
            }

            const Elf64_Shdr *stringsSection = &sections[sections[i].sh_link];
            if (sections[i].sh_offset + sections[i].sh_size > (size_t)fileInfo.st_size
                || stringsSection->sh_offset + stringsSection->sh_size > (size_t)fileInfo.st_size) {
                continue;
            }

            const Elf64_Sym *symbols = (const Elf64_Sym *)((const char *)mapping + sections[i].sh_offset);
            const char *strings = (const char *)mapping + stringsSection->sh_offset;
            size_t symbolCount = sections[i].sh_size / sizeof(Elf64_Sym);
            for (size_t symbolIndex = 0; symbolIndex < symbolCount; symbolIndex++) {
                if (symbols[symbolIndex].st_name >= stringsSection->sh_size
                    || symbols[symbolIndex].st_shndx == SHN_UNDEF) {
                    continue;
                }
                if (strcmp(strings + symbols[symbolIndex].st_name, name) == 0) {
                    uintptr_t base = 0;
                    if (header->e_type == ET_DYN) {
                        FILE *maps = fopen("/proc/self/maps", "r");
                        if (maps != NULL) {
                            char line[MAX_MOD_PATH + 128];
                            while (fgets(line, sizeof(line), maps) != NULL) {
                                unsigned long start;
                                unsigned long offset;
                                char mappedPath[MAX_MOD_PATH];
                                if (sscanf(line, "%lx-%*lx %*s %lx %*s %*s %4095s", &start, &offset, mappedPath) == 3
                                    && offset == 0
                                    && strcmp(mappedPath, executable) == 0) {
                                    base = start;
                                    break;
                                }
                            }
                            fclose(maps);
                        }
                    }
                    if (header->e_type != ET_DYN || base != 0) {
                        result = (void *)(base + symbols[symbolIndex].st_value);
                    }
                    break;
                }
            }
        }
    }

    munmap(mapping, fileInfo.st_size);
    return result;
}
#endif

#if defined(__APPLE__)
static void *ResolveMachOSymbol(const char *name)
{
    const struct mach_header *genericHeader = _dyld_get_image_header(0);
    if (genericHeader == NULL || genericHeader->magic != MH_MAGIC_64) {
        return NULL;
    }

    const struct mach_header_64 *header = (const struct mach_header_64 *)genericHeader;
    const struct load_command *command = (const struct load_command *)(header + 1);
    const struct symtab_command *symbolTable = NULL;
    const struct segment_command_64 *linkEdit = NULL;

    for (uint32_t commandIndex = 0; commandIndex < header->ncmds; commandIndex++) {
        if (command->cmd == LC_SYMTAB) {
            symbolTable = (const struct symtab_command *)command;
        } else if (command->cmd == LC_SEGMENT_64) {
            const struct segment_command_64 *segment = (const struct segment_command_64 *)command;
            if (strcmp(segment->segname, SEG_LINKEDIT) == 0) {
                linkEdit = segment;
            }
        }
        command = (const struct load_command *)((const unsigned char *)command + command->cmdsize);
    }

    if (symbolTable == NULL || linkEdit == NULL) {
        return NULL;
    }

    intptr_t slide = _dyld_get_image_vmaddr_slide(0);
    uintptr_t linkEditBase = (uintptr_t)(slide + linkEdit->vmaddr - linkEdit->fileoff);
    const struct nlist_64 *symbols = (const struct nlist_64 *)(linkEditBase + symbolTable->symoff);
    const char *strings = (const char *)(linkEditBase + symbolTable->stroff);

    for (uint32_t symbolIndex = 0; symbolIndex < symbolTable->nsyms; symbolIndex++) {
        const struct nlist_64 *symbol = &symbols[symbolIndex];
        if ((symbol->n_type & N_STAB) != 0 || (symbol->n_type & N_TYPE) == N_UNDF
            || symbol->n_value == 0 || symbol->n_un.n_strx >= symbolTable->strsize) {
            continue;
        }

        const char *symbolName = strings + symbol->n_un.n_strx;
        int matches = strcmp(symbolName, name) == 0
            || (symbolName[0] == '_' && strcmp(symbolName + 1, name) == 0);
        if (matches) {
            uintptr_t address = symbol->n_value;
            if ((symbol->n_type & N_TYPE) != N_ABS) {
                address += (uintptr_t)slide;
            }
            return (void *)address;
        }
    }

    return NULL;
}
#endif

static void *ResolveSymbol(const char *name)
{
#if defined(_WIN32)
    void *symbol = ModWindows_ResolveSymbol(name);
#else
    void *symbol = dlsym(RTLD_DEFAULT, name);
#endif
#if defined(__linux__)
    if (symbol == NULL) {
        symbol = ResolveElfSymbol(name);
    }
#elif defined(__APPLE__)
    if (symbol == NULL) {
        symbol = ResolveMachOSymbol(name);
    }
#endif

    if (symbol == NULL && sActiveMod != NULL) {
        fprintf(stderr, "[mods] %s: symbol not found: %s\n", sActiveMod->id, name);
    }
    return symbol;
}

static int SetCodeProtection(void *address, size_t byteCount, int protection)
{
    long pageSize = sysconf(_SC_PAGESIZE);
    uintptr_t firstPage;
    uintptr_t lastByte;
    size_t length;

    if (address == NULL || byteCount == 0 || pageSize <= 0) {
        return 0;
    }

    firstPage = (uintptr_t)address & ~((uintptr_t)pageSize - 1);
    lastByte = (uintptr_t)address + byteCount - 1;
    length = (size_t)((lastByte & ~((uintptr_t)pageSize - 1)) - firstPage + (uintptr_t)pageSize);
    return mprotect((void *)firstPage, length, protection) == 0;
}

#if MOD_ARCH_ARM64
static int PatchExecutableCode(void *address, const void *bytes, size_t byteCount)
{
    mach_port_t task = mach_task_self();
    mach_vm_size_t pageSize = (mach_vm_size_t)sysconf(_SC_PAGESIZE);
    if (address == NULL || bytes == NULL || byteCount == 0 || pageSize == 0) {
        return 0;
    }

    mach_vm_address_t firstPage = (mach_vm_address_t)address & ~(pageSize - 1);
    mach_vm_address_t lastByte = (mach_vm_address_t)address + byteCount - 1;
    mach_vm_size_t mappingSize = ((lastByte & ~(pageSize - 1)) - firstPage) + pageSize;
    mach_vm_address_t replacement = 0;

    if (mach_vm_allocate(task, &replacement, mappingSize, VM_FLAGS_ANYWHERE) != KERN_SUCCESS) {
        return 0;
    }

    memcpy((void *)replacement, (const void *)firstPage, (size_t)mappingSize);
    memcpy((unsigned char *)replacement + ((uintptr_t)address - firstPage), bytes, byteCount);

    if (mach_vm_protect(task, replacement, mappingSize, FALSE,
            VM_PROT_READ | VM_PROT_EXECUTE) != KERN_SUCCESS) {
        mach_vm_deallocate(task, replacement, mappingSize);
        return 0;
    }

    vm_prot_t currentProtection;
    vm_prot_t maximumProtection;
    mach_vm_address_t destination = firstPage;
    kern_return_t result = mach_vm_remap(task, &destination, mappingSize, 0,
        VM_FLAGS_FIXED | VM_FLAGS_OVERWRITE, task, replacement, FALSE,
        &currentProtection, &maximumProtection, VM_INHERIT_COPY);
    mach_vm_deallocate(task, replacement, mappingSize);
    if (result != KERN_SUCCESS) {
        return 0;
    }

    __builtin___clear_cache((char *)address, (char *)address + byteCount);
    return 1;
}
#endif

static void WriteAbsoluteJump(void *destination, const void *target)
{
#if MOD_ARCH_ARM64
    const uint32_t instructions[2] = {
        0x58000050, /* ldr x16, #8 */
        0xD61F0200, /* br x16 */
    };
    uint64_t address = (uint64_t)(uintptr_t)target;

    memcpy(destination, instructions, sizeof(instructions));
    memcpy((unsigned char *)destination + sizeof(instructions), &address, sizeof(address));
#else
    unsigned char jump[MOD_HOOK_JUMP_SIZE] = {
        0x48, 0xB8, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xE0
    };
    uint64_t address = (uint64_t)(uintptr_t)target;

    memcpy(&jump[2], &address, sizeof(address));
    memcpy(destination, jump, sizeof(jump));
#endif
}

#if MOD_ARCH_ARM64
static size_t CopyArm64TrampolineInstructions(void *destination, const void *sourceBytes,
    const void *sourceAddress, size_t byteCount)
{
    size_t outputOffset = 0;

    for (size_t inputOffset = 0; inputOffset < byteCount; inputOffset += sizeof(uint32_t)) {
        uint32_t instruction;
        memcpy(&instruction, (const unsigned char *)sourceBytes + inputOffset, sizeof(instruction));

        if ((instruction & 0x9F000000u) == 0x10000000u
            || (instruction & 0x9F000000u) == 0x90000000u) {
            int64_t immediate = (int64_t)(((instruction >> 5) & 0x7FFFFu) << 2)
                | ((instruction >> 29) & 0x3u);
            if ((immediate & 0x100000) != 0) {
                immediate |= ~INT64_C(0x1FFFFF);
            }

            uintptr_t instructionAddress = (uintptr_t)sourceAddress + inputOffset;
            uintptr_t referencedAddress;
            if ((instruction & 0x80000000u) != 0) {
                referencedAddress = (uintptr_t)((intptr_t)(instructionAddress & ~(uintptr_t)0xFFF)
                    + immediate * 4096);
            } else {
                referencedAddress = (uintptr_t)((intptr_t)instructionAddress + immediate);
            }

            uint32_t destinationRegister = instruction & 0x1Fu;
            const uint32_t loadSequence[2] = {
                0x58000040u | destinationRegister, /* ldr Xd, #8 */
                0x14000003u,                       /* b #12 */
            };
            uint64_t address = (uint64_t)referencedAddress;
            memcpy((unsigned char *)destination + outputOffset, loadSequence, sizeof(loadSequence));
            memcpy((unsigned char *)destination + outputOffset + sizeof(loadSequence), &address, sizeof(address));
            outputOffset += sizeof(loadSequence) + sizeof(address);
        } else if ((instruction & 0x7C000000u) == 0x14000000u) {
            int64_t immediate = (int64_t)(instruction & 0x03FFFFFFu);
            if ((immediate & 0x02000000) != 0) {
                immediate |= ~INT64_C(0x03FFFFFF);
            }
            const void *branchTarget = (const unsigned char *)sourceAddress
                + inputOffset + (immediate * 4);

            if ((instruction & 0x80000000u) != 0) {
                const uint32_t callSequence[3] = {
                    0x58000070, /* ldr x16, #12 */
                    0xD63F0200, /* blr x16 */
                    0x14000003, /* b #12 */
                };
                uint64_t address = (uint64_t)(uintptr_t)branchTarget;
                memcpy((unsigned char *)destination + outputOffset, callSequence, sizeof(callSequence));
                memcpy((unsigned char *)destination + outputOffset + sizeof(callSequence), &address, sizeof(address));
                outputOffset += sizeof(callSequence) + sizeof(address);
            } else {
                WriteAbsoluteJump((unsigned char *)destination + outputOffset, branchTarget);
                outputOffset += MOD_HOOK_JUMP_SIZE;
            }
        } else {
            memcpy((unsigned char *)destination + outputOffset, &instruction, sizeof(instruction));
            outputOffset += sizeof(instruction);
        }
    }

    return outputOffset;
}
#endif

static int HookRangesOverlap(const void *leftTarget, size_t leftSize, const void *rightTarget, size_t rightSize)
{
    uintptr_t leftStart = (uintptr_t)leftTarget;
    uintptr_t leftEnd = leftStart + leftSize;
    uintptr_t rightStart = (uintptr_t)rightTarget;
    uintptr_t rightEnd = rightStart + rightSize;
    return leftStart < rightEnd && rightStart < leftEnd;
}

static PokePlatinumModHook *FindFreeHook(void)
{
    for (unsigned int i = 0; i < MAX_MOD_HOOKS; i++) {
        if (!sHooks[i].installed && sHooks[i].target == NULL) {
            return &sHooks[i];
        }
    }

    return NULL;
}

static int FindPreviousHook(void *target, size_t overwrittenSize, void **originalFunction, PokePlatinumModHook **previous)
{
    *previous = NULL;
    for (unsigned int i = 0; i < MAX_MOD_HOOKS; i++) {
        if (sHooks[i].installed
            && HookRangesOverlap(target, overwrittenSize, sHooks[i].target, sHooks[i].overwrittenSize)) {
            // Only complete, cooperative hooks may share an entry point.
            if (target != sHooks[i].target || overwrittenSize != sHooks[i].overwrittenSize
                || originalFunction == NULL || sHooks[i].originalFunction == NULL) {
                return 0;
            }
            if (sHooks[i].next == NULL) *previous = &sHooks[i];
        }
    }

    return 1;
}

static PokePlatinumModHook *InstallHook(const char *symbolName, void *replacement, const uint8_t *expectedBytes, size_t overwrittenSize, void **originalFunction)
{
    PokePlatinumModHook *hook;
    PokePlatinumModHook *previous;
    void *target;
    long pageSize;

    if (sActiveMod == NULL || symbolName == NULL || symbolName[0] == '\0' || replacement == NULL
        || overwrittenSize < MOD_HOOK_JUMP_SIZE || overwrittenSize > MAX_HOOK_BYTES) {
        return NULL;
    }

#if MOD_ARCH_ARM64
    overwrittenSize = MOD_HOOK_JUMP_SIZE;
#else
    if (overwrittenSize < MOD_HOOK_JUMP_SIZE || overwrittenSize > MAX_HOOK_BYTES) {
        return NULL;
    }
#endif

    target = ResolveSymbol(symbolName);
    if (target == NULL) {
        fprintf(stderr, "[mods] %s: hook target symbol not found: %s\n", sActiveMod->id, symbolName);
        return NULL;
    }
    if (!FindPreviousHook(target, overwrittenSize, originalFunction, &previous)) {
        Log(sActiveMod->id, "hook target already in use");
        return NULL;
    }
#if !MOD_ARCH_ARM64
    if (expectedBytes != NULL && memcmp(target, expectedBytes, overwrittenSize) != 0) {
        return NULL;
    }
#endif

    hook = FindFreeHook();
    if (hook == NULL) {
        Log(sActiveMod->id, "hook table is full");
        return NULL;
    }

    hook->target = target;
    hook->replacement = replacement;
    hook->overwrittenSize = overwrittenSize;
    hook->owner = sActiveMod;
    hook->originalFunction = originalFunction;
    memcpy(hook->original, target, overwrittenSize);

    if (originalFunction != NULL && previous == NULL) {
        pageSize = sysconf(_SC_PAGESIZE);
        if (pageSize <= 0 || overwrittenSize + MOD_HOOK_JUMP_SIZE > (size_t)pageSize) {
            memset(hook, 0, sizeof(*hook));
            return NULL;
        }

        hook->trampolineSize = (size_t)pageSize;
        hook->trampoline = mmap(NULL, hook->trampolineSize, PROT_READ | PROT_WRITE,
            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (hook->trampoline == MAP_FAILED) {
            Log(sActiveMod->id, "could not allocate hook trampoline");
            memset(hook, 0, sizeof(*hook));
            return NULL;
        }

#if MOD_ARCH_ARM64
        size_t trampolineCodeSize = CopyArm64TrampolineInstructions(
            hook->trampoline, hook->original, target, overwrittenSize);
#else
        memcpy(hook->trampoline, hook->original, overwrittenSize);
        size_t trampolineCodeSize = overwrittenSize;
#endif
        WriteAbsoluteJump((unsigned char *)hook->trampoline + trampolineCodeSize,
            (unsigned char *)target + overwrittenSize);
        __builtin___clear_cache((char *)hook->trampoline,
            (char *)hook->trampoline + trampolineCodeSize + MOD_HOOK_JUMP_SIZE);
        if (mprotect(hook->trampoline, hook->trampolineSize, PROT_READ | PROT_EXEC) != 0) {
            Log(sActiveMod->id, "could not make hook trampoline executable");
            munmap(hook->trampoline, hook->trampolineSize);
            memset(hook, 0, sizeof(*hook));
            return NULL;
        }
    }

#if MOD_ARCH_ARM64
    unsigned char patchedCode[MAX_HOOK_BYTES];
    for (size_t offset = 0; offset < overwrittenSize; offset += sizeof(uint32_t)) {
        const uint32_t nop = 0xD503201F;
        memcpy(patchedCode + offset, &nop, sizeof(nop));
    }
    WriteAbsoluteJump(patchedCode, replacement);
    if (!PatchExecutableCode(target, patchedCode, overwrittenSize)) {
#else
    if (!SetCodeProtection(target, overwrittenSize, PROT_READ | PROT_WRITE | PROT_EXEC)) {
        Log(sActiveMod->id, "could not make hook target writable");
#endif
        if (hook->trampoline != NULL) {
            munmap(hook->trampoline, hook->trampolineSize);
        }
        memset(hook, 0, sizeof(*hook));
        return NULL;
    }

#if !MOD_ARCH_ARM64
    memset(target, 0x90, overwrittenSize);
    WriteAbsoluteJump(target, replacement);
    __builtin___clear_cache((char *)target, (char *)target + overwrittenSize);
    SetCodeProtection(target, overwrittenSize, PROT_READ | PROT_EXEC);
#endif
    hook->installed = 1;
    hook->previous = previous;
    if (previous != NULL) previous->next = hook;

    if (originalFunction != NULL) {
        *originalFunction = previous != NULL ? previous->replacement : hook->trampoline;
    }

    return hook;
}

static PokePlatinumModHook *InstallReplacementHook(const char *symbolName, void *replacement)
{
    return InstallHook(symbolName, replacement, NULL, MOD_HOOK_JUMP_SIZE, NULL);
}

static PokePlatinumModHook *InstallTrampolineHook(const char *symbolName, void *replacement, void **originalFunction)
{
    if (originalFunction == NULL) {
        return NULL;
    }

    *originalFunction = NULL;
    return InstallHook(symbolName, replacement, NULL, MOD_HOOK_JUMP_SIZE, originalFunction);
}

static int RemoveHookInternal(PokePlatinumModHook *hook)
{
    if (hook == NULL || !hook->installed) return 0;

    if (hook->next != NULL) {
        // Relink the remaining callbacks before the departing mod is unloaded.
        hook->next->previous = hook->previous;
        *hook->next->originalFunction = hook->previous != NULL
            ? hook->previous->replacement : hook->trampoline;
        if (hook->previous != NULL) {
            hook->previous->next = hook->next;
        } else {
            // The bottom hook owns the original function's executable copy.
            hook->next->trampoline = hook->trampoline;
            hook->next->trampolineSize = hook->trampolineSize;
            memcpy(hook->next->original, hook->original, hook->overwrittenSize);
        }
        memset(hook, 0, sizeof(*hook));
        return 1;
    }

    unsigned char restoredCode[MAX_HOOK_BYTES];
    memcpy(restoredCode, hook->original, hook->overwrittenSize);
    if (hook->previous != NULL) {
        WriteAbsoluteJump(restoredCode, hook->previous->replacement);
    }
#if MOD_ARCH_ARM64
    if (!PatchExecutableCode(hook->target, restoredCode, hook->overwrittenSize)) {
        return 0;
    }
#else
    if (!SetCodeProtection(hook->target, hook->overwrittenSize, PROT_READ | PROT_WRITE | PROT_EXEC)) {
        return 0;
    }

    memcpy(hook->target, restoredCode, hook->overwrittenSize);
    __builtin___clear_cache((char *)hook->target, (char *)hook->target + hook->overwrittenSize);
    SetCodeProtection(hook->target, hook->overwrittenSize, PROT_READ | PROT_EXEC);
#endif

    if (hook->previous != NULL) hook->previous->next = NULL;
    if (hook->trampoline != NULL) {
        munmap(hook->trampoline, hook->trampolineSize);
    }

    memset(hook, 0, sizeof(*hook));
    return 1;
}

static int RemoveHook(PokePlatinumModHook *hook)
{
    if (hook == NULL || hook->owner != sActiveMod) {
        return 0;
    }

    return RemoveHookInternal(hook);
}

static void RemoveHooksOwnedBy(const PokePlatinumMod *owner)
{
    for (unsigned int i = MAX_MOD_HOOKS; i > 0; i--) {
        PokePlatinumModHook *hook = &sHooks[i - 1];
        if (hook->installed && hook->owner == owner) {
            RemoveHookInternal(hook);
        }
    }
}

uint32_t Mods_GetLoadedCount(void)
{
    return sLoadedModCount;
}

int Mods_GetLoadedInfo(uint32_t index, PokePlatinumModInfo *info)
{
    if (index >= sLoadedModCount || info == NULL || info->structSize != sizeof(PokePlatinumModInfo)) {
        return 0;
    }

    info->id = sLoadedMods[index].descriptor->id;
    info->name = sLoadedMods[index].descriptor->name;
    info->version = sLoadedMods[index].descriptor->version;
    return 1;
}

static int RegisterStartMenuItem(const char *label, PokePlatinumModMenuCallback callback, void *userData)
{
    if (label == NULL || label[0] == '\0' || callback == NULL || sStartMenuItemCount >= MAX_START_MENU_ITEMS) {
        return 0;
    }

    ModStartMenuItem *item = &sStartMenuItems[sStartMenuItemCount++];
    snprintf(item->label, sizeof(item->label), "%s", label);
    item->callback = callback;
    item->userData = userData;
    item->owner = sActiveMod;
    return 1;
}

static int RegisterOption(const PokePlatinumModOption *option)
{
    if (sActiveMod != NULL && option != NULL
        && option->structSize == sizeof(PokePlatinumModOption)
        && option->valueCount > MAX_OPTION_VALUES) {
        fprintf(stderr, "[mods] %s: option '%s' has %u values; maximum is %u\n",
            sActiveMod->id, option->label != NULL ? option->label : "",
            option->valueCount, (unsigned int)MAX_OPTION_VALUES);
        return 0;
    }

    if (sActiveMod == NULL || option == NULL || option->structSize != sizeof(PokePlatinumModOption)
        || option->label == NULL || option->label[0] == '\0'
        || option->values == NULL || option->valueCount == 0
        || option->valueCount > MAX_OPTION_VALUES || option->selectedValue >= option->valueCount
        || sModOptionCount >= MAX_MOD_OPTIONS
        || (option->flags & ~(MOD_OPTION_FLAG_READ_ONLY | MOD_OPTION_FLAG_ACTION)) != 0) {
        return 0;
    }

    if ((option->flags & MOD_OPTION_FLAG_READ_ONLY) == 0 && option->onChanged == NULL) {
        return 0;
    }

    if (option->configKey != NULL && strlen(option->configKey) >= MAX_OPTION_KEY) {
        return 0;
    }

    if (option->configKey != NULL) {
        for (const char *character = option->configKey; *character != '\0'; character++) {
            if (!((*character >= 'a' && *character <= 'z')
                    || (*character >= 'A' && *character <= 'Z')
                    || (*character >= '0' && *character <= '9')
                    || *character == '_' || *character == '-' || *character == '.')) {
                return 0;
            }
        }
    }

    for (uint32_t i = 0; i < option->valueCount; i++) {
        if (option->values[i] == NULL || option->values[i][0] == '\0'
            || strchr(option->values[i], '\n') != NULL || strchr(option->values[i], '\r') != NULL) {
            return 0;
        }
    }

    if (option->configKey != NULL && option->configKey[0] != '\0') {
        for (unsigned int i = 0; i < sModOptionCount; i++) {
            if (sModOptions[i].owner == sActiveMod
                && strcmp(sModOptions[i].configKey, option->configKey) == 0) {
                return 0;
            }
        }
    }

    RegisteredModOption *registered = &sModOptions[sModOptionCount++];
    snprintf(registered->label, sizeof(registered->label), "%s", option->label);
    if (option->configKey != NULL) {
        snprintf(registered->configKey, sizeof(registered->configKey), "%s", option->configKey);
    } else {
        registered->configKey[0] = '\0';
    }
    registered->configPath[0] = '\0';
    for (uint32_t i = 0; i < option->valueCount; i++) {
        snprintf(registered->values[i], sizeof(registered->values[i]), "%s", option->values[i]);
    }
    registered->valueCount = option->valueCount;
    registered->selectedValue = option->selectedValue;
    registered->onChanged = option->onChanged;
    registered->userData = option->userData;
    registered->owner = sActiveMod;
    registered->flags = option->flags;
    return 1;
}

static int SetOptionValueLabel(const char *configKey, const char *value)
{
    if (sActiveMod == NULL || configKey == NULL || value == NULL
        || value[0] == '\0' || strlen(value) >= MAX_OPTION_TEXT
        || strchr(value, '\n') != NULL || strchr(value, '\r') != NULL) {
        return 0;
    }

    for (unsigned int i = 0; i < sModOptionCount; i++) {
        RegisteredModOption *option = &sModOptions[i];
        if (option->owner == sActiveMod && strcmp(option->configKey, configKey) == 0
            && (option->flags & MOD_OPTION_FLAG_READ_ONLY) != 0) {
            snprintf(option->values[0], sizeof(option->values[0]), "%s", value);
            option->valueCount = 1;
            option->selectedValue = 0;
            return 1;
        }
    }

    return 0;
}

static int RegisterKeyBinding(int32_t keycode, PokePlatinumModKeyCallback callback, void *userData)
{
    if (sActiveMod == NULL || callback == NULL || sKeyBindingCount >= MAX_KEY_BINDINGS) {
        return 0;
    }

    for (unsigned int i = 0; i < sKeyBindingCount; i++) {
        if (sKeyBindings[i].owner == sActiveMod && sKeyBindings[i].keycode == keycode) {
            return 0;
        }
    }

    RegisteredKeyBinding *binding = &sKeyBindings[sKeyBindingCount++];
    binding->keycode = keycode;
    binding->callback = callback;
    binding->userData = userData;
    binding->owner = sActiveMod;
    return 1;
}

static void RemoveKeyBindingsOwnedBy(const PokePlatinumMod *owner)
{
    unsigned int output = 0;

    for (unsigned int i = 0; i < sKeyBindingCount; i++) {
        if (sKeyBindings[i].owner != owner) {
            if (output != i) {
                sKeyBindings[output] = sKeyBindings[i];
            }
            output++;
        }
    }

    memset(&sKeyBindings[output], 0, (sKeyBindingCount - output) * sizeof(sKeyBindings[0]));
    sKeyBindingCount = output;
}

static PokePlatinumModMenuResult OpenModBrowser(void *gameContext, void *userData)
{
    (void)gameContext;
    (void)userData;
    return MOD_MENU_RESULT_SHOW_LOADED_MODS;
}

static const PokePlatinumModHost sHost = {
    .structSize = sizeof(PokePlatinumModHost),
    .log = Log,
    .resolveSymbol = ResolveSymbol,
    .getLoadedModCount = Mods_GetLoadedCount,
    .getLoadedModInfo = Mods_GetLoadedInfo,
    .registerStartMenuItem = RegisterStartMenuItem,
    .installReplacementHook = InstallReplacementHook,
    .installTrampolineHook = InstallTrampolineHook,
    .removeHook = RemoveHook,
    .registerOption = RegisterOption,
    .setOptionValueLabel = SetOptionValueLabel,
    .registerKeyBinding = RegisterKeyBinding,
};

uint32_t Mods_GetStartMenuItemCount(void)
{
    return sStartMenuItemCount;
}

const char *Mods_GetStartMenuItemLabel(uint32_t index)
{
    return index < sStartMenuItemCount ? sStartMenuItems[index].label : NULL;
}

PokePlatinumModMenuResult Mods_InvokeStartMenuItem(uint32_t index, void *gameContext)
{
    if (index >= sStartMenuItemCount) {
        return MOD_MENU_RESULT_STAY_OPEN;
    }

    ModStartMenuItem *item = &sStartMenuItems[index];
    const PokePlatinumMod *previousActiveMod = sActiveMod;
    sActiveMod = item->owner;
    PokePlatinumModMenuResult result = item->callback(gameContext, item->userData);
    sActiveMod = previousActiveMod;
    return result;
}

static RegisteredModOption *GetModOption(uint32_t modIndex, uint32_t optionIndex)
{
    if (modIndex >= sLoadedModCount) {
        return NULL;
    }

    const PokePlatinumMod *owner = sLoadedMods[modIndex].descriptor;
    for (unsigned int i = 0; i < sModOptionCount; i++) {
        if (sModOptions[i].owner == owner) {
            if (optionIndex == 0) {
                return &sModOptions[i];
            }
            optionIndex--;
        }
    }

    return NULL;
}

uint32_t Mods_GetOptionCount(uint32_t modIndex)
{
    if (modIndex >= sLoadedModCount) {
        return 0;
    }

    uint32_t count = 0;
    const PokePlatinumMod *owner = sLoadedMods[modIndex].descriptor;
    for (unsigned int i = 0; i < sModOptionCount; i++) {
        if (sModOptions[i].owner == owner) {
            count++;
        }
    }
    return count;
}

const char *Mods_GetOptionLabel(uint32_t modIndex, uint32_t optionIndex)
{
    RegisteredModOption *option = GetModOption(modIndex, optionIndex);
    return option != NULL ? option->label : NULL;
}

uint32_t Mods_GetOptionValueCount(uint32_t modIndex, uint32_t optionIndex)
{
    RegisteredModOption *option = GetModOption(modIndex, optionIndex);
    return option != NULL ? option->valueCount : 0;
}

const char *Mods_GetOptionValueLabel(uint32_t modIndex, uint32_t optionIndex, uint32_t valueIndex)
{
    RegisteredModOption *option = GetModOption(modIndex, optionIndex);
    return option != NULL && valueIndex < option->valueCount ? option->values[valueIndex] : NULL;
}

uint32_t Mods_GetOptionSelectedValue(uint32_t modIndex, uint32_t optionIndex)
{
    RegisteredModOption *option = GetModOption(modIndex, optionIndex);
    return option != NULL ? option->selectedValue : 0;
}

int Mods_IsOptionReadOnly(uint32_t modIndex, uint32_t optionIndex)
{
    RegisteredModOption *option = GetModOption(modIndex, optionIndex);
    return option != NULL && (option->flags & MOD_OPTION_FLAG_READ_ONLY) != 0;
}

int Mods_IsOptionAction(uint32_t modIndex, uint32_t optionIndex)
{
    RegisteredModOption *option = GetModOption(modIndex, optionIndex);
    return option != NULL && (option->flags & MOD_OPTION_FLAG_ACTION) != 0;
}

static char *TrimConfigText(char *text)
{
    while (*text == ' ' || *text == '\t' || *text == '\r' || *text == '\n') {
        text++;
    }

    char *end = text + strlen(text);
    while (end > text
        && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n')) {
        *--end = '\0';
    }
    return text;
}

static int ReadConfigValue(const char *path, const char *key, char *value, size_t valueSize)
{
    FILE *file = fopen(path, "r");
    if (file == NULL) {
        return 0;
    }

    char line[256];
    int found = 0;
    while (fgets(line, sizeof(line), file) != NULL) {
        char *text = TrimConfigText(line);
        if (*text == '\0' || *text == '#' || *text == ';' || *text == '[') {
            continue;
        }

        char *separator = strchr(text, '=');
        if (separator == NULL) {
            continue;
        }
        *separator = '\0';
        char *lineKey = TrimConfigText(text);
        char *lineValue = TrimConfigText(separator + 1);
        if (strcmp(lineKey, key) == 0) {
            snprintf(value, valueSize, "%s", lineValue);
            found = 1;
            break;
        }
    }

    fclose(file);
    return found;
}

static int IsSafeConfigFilename(const char *name)
{
    if (name == NULL || name[0] == '\0' || strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
        return 0;
    }

    for (const char *character = name; *character != '\0'; character++) {
        if (!((*character >= 'a' && *character <= 'z')
                || (*character >= 'A' && *character <= 'Z')
                || (*character >= '0' && *character <= '9')
                || *character == '_' || *character == '-')) {
            return 0;
        }
    }
    return 1;
}

static int BuildConfigPath(const char *modPath, const char *modId, char *configPath, size_t configPathSize)
{
    if (!IsSafeConfigFilename(modId)) {
        return 0;
    }

    const char *separator = strrchr(modPath, '/');
    int written;
    if (separator == NULL) {
        written = snprintf(configPath, configPathSize, "%s.ini", modId);
    } else {
        written = snprintf(configPath, configPathSize, "%.*s/%s.ini",
            (int)(separator - modPath), modPath, modId);
    }
    return written > 0 && (size_t)written < configPathSize;
}

static int SaveModOptions(const PokePlatinumMod *owner, const char *configPath)
{
    char temporaryPath[MAX_MOD_PATH];
    int written = snprintf(temporaryPath, sizeof(temporaryPath), "%s.tmp", configPath);
    if (written <= 0 || written >= (int)sizeof(temporaryPath)) {
        return 0;
    }

    FILE *file = fopen(temporaryPath, "w");
    if (file == NULL) {
        return 0;
    }

    int success = 1;
    for (unsigned int i = 0; i < sModOptionCount; i++) {
        RegisteredModOption *option = &sModOptions[i];
        if (option->owner == owner && option->configKey[0] != '\0'
            && fprintf(file, "%s=%s\n", option->configKey, option->values[option->selectedValue]) < 0) {
            success = 0;
            break;
        }
    }

    if (fclose(file) != 0) {
        success = 0;
    }
    if (!success || rename(temporaryPath, configPath) != 0) {
        unlink(temporaryPath);
        return 0;
    }
    return 1;
}

static int InitializeModOptions(const PokePlatinumMod *owner, const char *modPath)
{
    unsigned int optionCount = 0;
    for (unsigned int i = 0; i < sModOptionCount; i++) {
        if (sModOptions[i].owner == owner
            && sModOptions[i].configKey[0] != '\0'
            && (sModOptions[i].flags & MOD_OPTION_FLAG_READ_ONLY) == 0) {
            optionCount++;
        }
    }
    if (optionCount == 0) {
        return 1;
    }

    char configPath[MAX_MOD_PATH];
    if (!BuildConfigPath(modPath, owner->id, configPath, sizeof(configPath))) {
        return 0;
    }

    for (unsigned int i = 0; i < sModOptionCount; i++) {
        RegisteredModOption *option = &sModOptions[i];
        if (option->owner != owner) {
            continue;
        }

        if (option->configKey[0] == '\0' || (option->flags & MOD_OPTION_FLAG_READ_ONLY) != 0) {
            continue;
        }

        snprintf(option->configPath, sizeof(option->configPath), "%s", configPath);
        char configuredValue[MAX_OPTION_TEXT];
        if (ReadConfigValue(configPath, option->configKey, configuredValue, sizeof(configuredValue))) {
            for (uint32_t valueIndex = 0; valueIndex < option->valueCount; valueIndex++) {
                if (strcmp(configuredValue, option->values[valueIndex]) == 0) {
                    option->selectedValue = valueIndex;
                    break;
                }
            }
        }
    }

    if (!SaveModOptions(owner, configPath)) {
        return 0;
    }

    const PokePlatinumMod *previousActiveMod = sActiveMod;
    sActiveMod = owner;
    for (unsigned int i = 0; i < sModOptionCount; i++) {
        RegisteredModOption *option = &sModOptions[i];
        if (option->owner == owner && option->configKey[0] != '\0'
            && option->onChanged != NULL) {
            option->onChanged(option->selectedValue, option->userData);
        }
    }
    sActiveMod = previousActiveMod;
    return 1;
}

int Mods_SetOptionSelectedValue(uint32_t modIndex, uint32_t optionIndex, uint32_t valueIndex)
{
    RegisteredModOption *option = GetModOption(modIndex, optionIndex);
    if (option == NULL || valueIndex >= option->valueCount
        || (option->flags & MOD_OPTION_FLAG_READ_ONLY) != 0) {
        return 0;
    }

    uint32_t previousValue = option->selectedValue;
    option->selectedValue = valueIndex;
    if (option->configKey[0] != '\0'
        && !SaveModOptions(option->owner, option->configPath)) {
        option->selectedValue = previousValue;
        return 0;
    }
    const PokePlatinumMod *previousActiveMod = sActiveMod;
    sActiveMod = option->owner;
    option->onChanged(valueIndex, option->userData);
    sActiveMod = previousActiveMod;
    return 1;
}

static int CompareNames(const void *left, const void *right)
{
    return strcmp(left, right);
}

static int IsDirectory(const char *path)
{
    struct stat info;
    return stat(path, &info) == 0 && S_ISDIR(info.st_mode);
}

static int IsRegularFile(const char *path)
{
    struct stat info;
    return stat(path, &info) == 0 && S_ISREG(info.st_mode);
}

static int IsAlreadyLoaded(const char *id)
{
    for (unsigned int i = 0; i < sLoadedModCount; i++) {
        if (strcmp(sLoadedMods[i].descriptor->id, id) == 0) {
            return 1;
        }
    }

    return 0;
}

static void LoadMod(const char *path)
{
    if (sLoadedModCount == MAX_LOADED_MODS) {
        Log(NULL, "mod limit reached");
        return;
    }

    const char *filename = strrchr(path, '/');
    filename = filename != NULL ? filename + 1 : path;
    if (strcmp(filename, "quicksave_f5.so") == 0 || strcmp(filename, "quicksave_f5.dylib") == 0
        || strcmp(filename, "restart_f9.so") == 0 || strcmp(filename, "restart_f9.dylib") == 0
        || strcmp(filename, "bag_sort.so") == 0 || strcmp(filename, "bag_sort.dylib") == 0
        || strcmp(filename, "poketch_keyboard.so") == 0 || strcmp(filename, "poketch_keyboard.dylib") == 0
        || strcmp(filename, "speed.so") == 0 || strcmp(filename, "speed.dylib") == 0
        || strcmp(filename, "camera_distance.so") == 0 || strcmp(filename, "camera_distance.dylib") == 0) {
        return;
    }

    size_t filenameLength = strlen(filename);
    int isLua = filenameLength > 4 && strcmp(filename + filenameLength - 4, ".lua") == 0;
    void *library = NULL;
    LuaMod *lua = NULL;
    const PokePlatinumMod *descriptor;
    if (isLua) {
        lua = LuaMod_Open(path);
        if (lua == NULL) return;
        descriptor = LuaMod_Descriptor(lua);
    } else {
        // Prefer a script over an obsolete binary of the same mod, including
        // flat development layouts. Never dlopen the stale library first.
        char scriptPath[MAX_MOD_PATH];
        snprintf(scriptPath, sizeof(scriptPath), "%s", path);
        char *extension = strrchr(scriptPath, '.');
        if (extension != NULL && (size_t)(extension - scriptPath) + 5 <= sizeof(scriptPath)) {
            strcpy(extension, ".lua");
            if (IsRegularFile(scriptPath)) return;
        }
        library = dlopen(path, RTLD_NOW | RTLD_LOCAL);
        if (library == NULL) { Log(path, dlerror()); return; }
        dlerror();
        PokePlatinumModEntrypoint entrypoint = (PokePlatinumModEntrypoint)dlsym(library, POKEPLATINUM_MOD_ENTRYPOINT);
        const char *error = dlerror();
        if (error != NULL) { Log(path, error); dlclose(library); return; }
        descriptor = entrypoint();
    }
    if (descriptor == NULL
        || descriptor->structSize != sizeof(PokePlatinumMod)
        || descriptor->id == NULL
        || descriptor->id[0] == '\0'
        || descriptor->name == NULL
        || descriptor->name[0] == '\0') {
        Log(path, "invalid or incompatible descriptor");
        if (library != NULL) dlclose(library);
        LuaMod_Close(lua);
        return;
    }

    if (IsAlreadyLoaded(descriptor->id)) {
        Log(descriptor->id, "duplicate id ignored");
        if (library != NULL) dlclose(library);
        LuaMod_Close(lua);
        return;
    }

    unsigned int previousMenuItemCount = sStartMenuItemCount;
    unsigned int previousOptionCount = sModOptionCount;
    unsigned int previousKeyBindingCount = sKeyBindingCount;
    sActiveMod = descriptor;
    int accepted = lua != NULL ? LuaMod_Load(lua, &sHost)
        : descriptor->onLoad == NULL || descriptor->onLoad(&sHost);
    if (accepted && !InitializeModOptions(descriptor, path)) {
        Log(descriptor->id, "could not initialize option configuration");
        accepted = 0;
    }
    sActiveMod = NULL;
    if (!accepted) {
        RemoveHooksOwnedBy(descriptor);
        sStartMenuItemCount = previousMenuItemCount;
        sModOptionCount = previousOptionCount;
        memset(&sKeyBindings[previousKeyBindingCount], 0,
            (sKeyBindingCount - previousKeyBindingCount) * sizeof(sKeyBindings[0]));
        sKeyBindingCount = previousKeyBindingCount;
        Log(descriptor->id, "onLoad rejected the mod");
        if (library != NULL) dlclose(library);
        LuaMod_Close(lua);
        return;
    }

    sLoadedMods[sLoadedModCount].library = library;
    sLoadedMods[sLoadedModCount].lua = lua;
    sLoadedMods[sLoadedModCount].descriptor = descriptor;
    sLoadedModCount++;
    Log(descriptor->id, "loaded");
}

static void LoadDirectory(const char *directory)
{
    char resolvedDirectory[MAX_MOD_PATH];
    if (realpath(directory, resolvedDirectory) == NULL) {
        return;
    }

    for (unsigned int i = 0; i < sLoadedDirectoryCount; i++) {
        if (strcmp(sLoadedDirectories[i], resolvedDirectory) == 0) {
            return;
        }
    }
    if (sLoadedDirectoryCount < 3) {
        snprintf(sLoadedDirectories[sLoadedDirectoryCount++], MAX_MOD_PATH, "%s", resolvedDirectory);
    }

    DIR *dir = opendir(resolvedDirectory);
    if (dir == NULL) {
        return;
    }

    char names[MAX_LOADED_MODS][MAX_MOD_FILENAME];
    unsigned int nameCount = 0;
    char flatNames[MAX_LOADED_MODS][MAX_MOD_FILENAME];
    unsigned int flatNameCount = 0;
    struct dirent *entry;
#if defined(__APPLE__)
    const char *extension = ".dylib";
#elif defined(_WIN32)
    const char *extension = ".dll";
#else
    const char *extension = ".so";
#endif
    const size_t extensionLength = strlen(extension);

    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0
            || strlen(entry->d_name) >= MAX_MOD_FILENAME) {
            continue;
        }

        char childPath[MAX_MOD_PATH];
        int written = snprintf(childPath, sizeof(childPath), "%s/%s", resolvedDirectory, entry->d_name);
        if (written <= 0 || written >= (int)sizeof(childPath)) {
            continue;
        }
        if (IsDirectory(childPath) && nameCount < MAX_LOADED_MODS) {
            snprintf(names[nameCount], sizeof(names[nameCount]), "%s", entry->d_name);
            nameCount++;
            continue;
        }

        size_t nameLength = strlen(entry->d_name);
        if (flatNameCount < MAX_LOADED_MODS && IsRegularFile(childPath)
            && ((nameLength > extensionLength
                && strcmp(entry->d_name + nameLength - extensionLength, extension) == 0)
                || (nameLength > 4 && strcmp(entry->d_name + nameLength - 4, ".lua") == 0))) {
            snprintf(flatNames[flatNameCount], sizeof(flatNames[flatNameCount]), "%s", entry->d_name);
            flatNameCount++;
        }
    }

    closedir(dir);
    qsort(names, nameCount, sizeof(names[0]), CompareNames);
    qsort(flatNames, flatNameCount, sizeof(flatNames[0]), CompareNames);

    for (unsigned int i = 0; i < nameCount; i++) {
        char path[MAX_MOD_PATH];
#if defined(__APPLE__)
        const char *extension = "dylib";
#elif defined(_WIN32)
        const char *extension = "dll";
#else
        const char *extension = "so";
#endif
        int size = snprintf(path, sizeof(path), "%s/%s/%s.lua", resolvedDirectory, names[i], names[i]);
        if (size > 0 && size < (int)sizeof(path) && IsRegularFile(path)) {
            LoadMod(path);
            continue;
        }
        int written = snprintf(path, sizeof(path), "%s/%s/%s.%s",
            resolvedDirectory, names[i], names[i], extension);
        if (written > 0 && written < (int)sizeof(path) && IsRegularFile(path)) {
            LoadMod(path);
        }
    }

    // Meson leaves freshly-built modules directly in build_*/mods. Packaged
    // builds use one directory per mod. Supporting both layouts keeps Linux
    // development builds and macOS packages equivalent.
    for (unsigned int i = 0; i < flatNameCount; i++) {
        char path[MAX_MOD_PATH];
        int written = snprintf(path, sizeof(path), "%s/%s",
            resolvedDirectory, flatNames[i]);
        if (written > 0 && written < (int)sizeof(path)) {
            LoadMod(path);
        }
    }
}

static void LoadDefaultDirectories(void)
{
    const char *override = getenv("POKEPLATINUM_MOD_DIR");
    if (override != NULL && override[0] != '\0') {
        LoadDirectory(override);
        return;
    }

    LoadDirectory("mods");

    char executable[MAX_MOD_PATH] = {0};
#if defined(__APPLE__)
    uint32_t executableSize = sizeof(executable);
    int hasExecutablePath = _NSGetExecutablePath(executable, &executableSize) == 0;
#elif defined(_WIN32)
    int hasExecutablePath = ModWindows_ExecutablePath(executable, sizeof(executable));
#else
    ssize_t length = readlink("/proc/self/exe", executable, sizeof(executable) - 1);
    int hasExecutablePath = length > 0;
    if (hasExecutablePath) {
        executable[length] = '\0';
    }
#endif
    if (hasExecutablePath) {
        char *separator = strrchr(executable, '/');
        if (separator != NULL) {
            strcpy(separator + 1, "mods");
            LoadDirectory(executable);
        }
    }

    const char *appImage = getenv("APPIMAGE");
    if (appImage != NULL && appImage[0] != '\0') {
        snprintf(executable, sizeof(executable), "%s", appImage);
        char *separator = strrchr(executable, '/');
        if (separator != NULL) {
            strcpy(separator + 1, "mods");
            LoadDirectory(executable);
        }
    }
}

void Mods_Init(void)
{
    if (sInitialized) {
        return;
    }

    sInitialized = 1;
    RegisterStartMenuItem("MODS", OpenModBrowser, NULL);
    LoadDefaultDirectories();
    atexit(Mods_Shutdown);
}

void Mods_Dispatch(PokePlatinumModEvent event, void *eventData)
{
    for (unsigned int i = 0; i < sLoadedModCount; i++) {
        const PokePlatinumMod *mod = sLoadedMods[i].descriptor;
        sActiveMod = mod;

        if (sLoadedMods[i].lua != NULL) {
            LuaMod_Dispatch(sLoadedMods[i].lua, event, eventData);
        }

        if (event == MOD_EVENT_FRAME && mod->onFrame != NULL) {
            mod->onFrame();
        }

        if (mod->onEvent != NULL) {
            mod->onEvent(event, eventData);
        }

        sActiveMod = NULL;
    }
}

void Mods_NotifySaveLoaded(void *saveData)
{
    PokePlatinumModSaveEvent saveEvent = {
        .structSize = sizeof(PokePlatinumModSaveEvent),
        .saveData = saveData,
    };
    Mods_Dispatch(MOD_EVENT_SAVE_LOADED, &saveEvent);
}

void Mods_NotifyKeyDown(int32_t keycode, uint8_t repeat)
{
    if (repeat != 0) {
        return;
    }

    const PokePlatinumMod *previousActiveMod = sActiveMod;
    for (unsigned int i = 0; i < sKeyBindingCount; i++) {
        RegisteredKeyBinding *binding = &sKeyBindings[i];
        if (binding->keycode == keycode) {
            sActiveMod = binding->owner;
            binding->callback(keycode, binding->userData);
        }
    }
    sActiveMod = previousActiveMod;
}

uint32_t Mods_FilterExperienceGain(void *pokemon, uint32_t currentExperience, uint32_t requestedExperience)
{
    PokePlatinumModExperienceEvent experienceEvent = {
        .structSize = sizeof(PokePlatinumModExperienceEvent),
        .pokemon = pokemon,
        .currentExperience = currentExperience,
        .requestedExperience = requestedExperience,
        .grantedExperience = requestedExperience,
    };
    Mods_Dispatch(MOD_EVENT_FILTER_EXPERIENCE_GAIN, &experienceEvent);
    return experienceEvent.grantedExperience;
}

int Mods_IsLevelUpItemBlocked(void *pokemon, uint16_t itemId)
{
    PokePlatinumModLevelUpItemEvent itemEvent = {
        .structSize = sizeof(PokePlatinumModLevelUpItemEvent),
        .pokemon = pokemon,
        .itemId = itemId,
        .blocked = 0,
        .reserved = 0,
    };
    Mods_Dispatch(MOD_EVENT_CHECK_LEVEL_UP_ITEM, &itemEvent);
    return itemEvent.blocked != 0;
}

void Mods_Shutdown(void)
{
    while (sLoadedModCount > 0) {
        LoadedMod *mod = &sLoadedMods[--sLoadedModCount];
        sActiveMod = mod->descriptor;
        if (mod->lua != NULL) LuaMod_Unload(mod->lua);
        if (mod->descriptor->onUnload != NULL) {
            mod->descriptor->onUnload();
        }
        RemoveKeyBindingsOwnedBy(mod->descriptor);
        sActiveMod = NULL;
        RemoveHooksOwnedBy(mod->descriptor);
        if (mod->library != NULL) dlclose(mod->library);
        LuaMod_Close(mod->lua);
    }

    sStartMenuItemCount = 0;
    sModOptionCount = 0;
    sKeyBindingCount = 0;
    sLoadedDirectoryCount = 0;
}

#else

void Mods_Init(void)
{
}

void Mods_Dispatch(PokePlatinumModEvent event, void *eventData)
{
    (void)event;
    (void)eventData;
}

void Mods_Shutdown(void)
{
}

uint32_t Mods_GetLoadedCount(void)
{
    return 0;
}

int Mods_GetLoadedInfo(uint32_t index, PokePlatinumModInfo *info)
{
    (void)index;
    (void)info;
    return 0;
}

uint32_t Mods_GetStartMenuItemCount(void)
{
    return 0;
}

const char *Mods_GetStartMenuItemLabel(uint32_t index)
{
    (void)index;
    return NULL;
}

PokePlatinumModMenuResult Mods_InvokeStartMenuItem(uint32_t index, void *gameContext)
{
    (void)index;
    (void)gameContext;
    return MOD_MENU_RESULT_STAY_OPEN;
}

uint32_t Mods_GetOptionCount(uint32_t modIndex)
{
    (void)modIndex;
    return 0;
}

const char *Mods_GetOptionLabel(uint32_t modIndex, uint32_t optionIndex)
{
    (void)modIndex;
    (void)optionIndex;
    return NULL;
}

uint32_t Mods_GetOptionValueCount(uint32_t modIndex, uint32_t optionIndex)
{
    (void)modIndex;
    (void)optionIndex;
    return 0;
}

const char *Mods_GetOptionValueLabel(uint32_t modIndex, uint32_t optionIndex, uint32_t valueIndex)
{
    (void)modIndex;
    (void)optionIndex;
    (void)valueIndex;
    return NULL;
}

uint32_t Mods_GetOptionSelectedValue(uint32_t modIndex, uint32_t optionIndex)
{
    (void)modIndex;
    (void)optionIndex;
    return 0;
}

int Mods_IsOptionReadOnly(uint32_t modIndex, uint32_t optionIndex)
{
    (void)modIndex;
    (void)optionIndex;
    return 0;
}

int Mods_SetOptionSelectedValue(uint32_t modIndex, uint32_t optionIndex, uint32_t valueIndex)
{
    (void)modIndex;
    (void)optionIndex;
    (void)valueIndex;
    return 0;
}

void Mods_NotifySaveLoaded(void *saveData)
{
    (void)saveData;
}

void Mods_NotifyKeyDown(int32_t keycode, uint8_t repeat)
{
    (void)keycode;
    (void)repeat;
}

uint32_t Mods_FilterExperienceGain(void *pokemon, uint32_t currentExperience, uint32_t requestedExperience)
{
    (void)pokemon;
    (void)currentExperience;
    return requestedExperience;
}

int Mods_IsLevelUpItemBlocked(void *pokemon, uint16_t itemId)
{
    (void)pokemon;
    (void)itemId;
    return 0;
}

#endif

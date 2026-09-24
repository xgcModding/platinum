#include <Carbon/Carbon.h>
#include <SDL2/SDL.h>

// SDL2 keycodes describe the unmodified key. Resolve printable keys through
// the current macOS layout, including Shift/Option, without enabling the IME
// (which opens the accent picker when movement keys are held).
SDL_Keycode SIM_ResolveKeyboardKey(const SDL_Keysym *key)
{
    static const struct {
        SDL_Scancode scan;
        UInt16 native;
    } printableKeys[] = {
#define ANSI(key) { SDL_SCANCODE_##key, kVK_ANSI_##key }
        ANSI(A), ANSI(B), ANSI(C), ANSI(D), ANSI(E), ANSI(F), ANSI(G),
        ANSI(H), ANSI(I), ANSI(J), ANSI(K), ANSI(L), ANSI(M), ANSI(N),
        ANSI(O), ANSI(P), ANSI(Q), ANSI(R), ANSI(S), ANSI(T), ANSI(U),
        ANSI(V), ANSI(W), ANSI(X), ANSI(Y), ANSI(Z),
        ANSI(0), ANSI(1), ANSI(2), ANSI(3), ANSI(4), ANSI(5), ANSI(6),
        ANSI(7), ANSI(8), ANSI(9),
#undef ANSI
        { SDL_SCANCODE_EQUALS, kVK_ANSI_Equal },
        { SDL_SCANCODE_MINUS, kVK_ANSI_Minus },
        { SDL_SCANCODE_LEFTBRACKET, kVK_ANSI_LeftBracket },
        { SDL_SCANCODE_RIGHTBRACKET, kVK_ANSI_RightBracket },
        { SDL_SCANCODE_APOSTROPHE, kVK_ANSI_Quote },
        { SDL_SCANCODE_SEMICOLON, kVK_ANSI_Semicolon },
        { SDL_SCANCODE_BACKSLASH, kVK_ANSI_Backslash },
        { SDL_SCANCODE_COMMA, kVK_ANSI_Comma },
        { SDL_SCANCODE_SLASH, kVK_ANSI_Slash },
        { SDL_SCANCODE_PERIOD, kVK_ANSI_Period },
        { SDL_SCANCODE_GRAVE, kVK_ANSI_Grave },
        { SDL_SCANCODE_NONUSBACKSLASH, kVK_ISO_Section },
        { SDL_SCANCODE_INTERNATIONAL3, kVK_JIS_Yen },
        { SDL_SCANCODE_INTERNATIONAL1, kVK_JIS_Underscore },
    };

    // Function keys, arrows, keypad keys and modifiers retain their SDL IDs.
    if (!(key->mod & (KMOD_SHIFT | KMOD_ALT))) {
        return key->sym;
    }
    UInt16 native = 0xffff;
    for (size_t i = 0; i < SDL_arraysize(printableKeys); ++i) {
        if (printableKeys[i].scan == key->scancode) {
            native = printableKeys[i].native;
            break;
        }
    }
    if (native == 0xffff) {
        return key->sym;
    }
    if (KBGetLayoutType(LMGetKbdType()) == kKeyboardISO) {
        if (native == kVK_ANSI_Grave) native = kVK_ISO_Section;
        else if (native == kVK_ISO_Section) native = kVK_ANSI_Grave;
    }

    TISInputSourceRef source = TISCopyCurrentKeyboardLayoutInputSource();
    if (source == NULL) return key->sym;
    CFDataRef data = TISGetInputSourceProperty(source, kTISPropertyUnicodeKeyLayoutData);
    SDL_Keycode result = key->sym;
    if (data != NULL) {
        UInt32 modifiers = 0;
        if (key->mod & KMOD_SHIFT) modifiers |= shiftKey;
        if (key->mod & KMOD_ALT) modifiers |= optionKey;
        UInt32 deadState = 0;
        UniChar chars[4];
        UniCharCount length = 0;
        OSStatus status = UCKeyTranslate((const UCKeyboardLayout *)CFDataGetBytePtr(data),
            native, kUCKeyActionDown, modifiers >> 8, LMGetKbdType(),
            kUCKeyTranslateNoDeadKeysMask, &deadState, SDL_arraysize(chars), &length, chars);
        if (status == noErr && length == 1 && chars[0] >= 0x20
            && (chars[0] < 0xd800 || chars[0] > 0xdfff)) {
            result = chars[0];
            // Shift changes punctuation, but does not disable letter bindings.
            if (!(key->mod & KMOD_ALT)
                && CFCharacterSetIsCharacterMember(CFCharacterSetGetPredefined(kCFCharacterSetLetter), chars[0])) {
                result = key->sym;
            }
        }
    }
    CFRelease(source);
    return result;
}

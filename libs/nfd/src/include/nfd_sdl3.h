/*
  Native File Dialog Extended
  Repository: https://github.com/btzy/nativefiledialog-extended
  License: Zlib
  Authors: Bernard Teo

  This header contains a function to convert an SDL window handle to a native window handle for
  passing to NFDe.

  This is meant to be used with SDL3
 */

#ifndef _NFD_SDL3_H
#define _NFD_SDL3_H

#include "nfd.h"
#include <SDL3/SDL.h>

#ifdef __cplusplus
extern "C" {
#define NFD_INLINE inline
#else
#define NFD_INLINE static inline
#endif  // __cplusplus

/**
 * Sets the wayland display if the process is running under Wayland, otherwise does nothing.
 * @param sdlWindow The SDL window handle.
 */
NFD_INLINE bool NFD_SetDisplayPropertiesFromSDLWindow(SDL_Window* window) {
    if (!window) return false;

    SDL_PropertiesID props = SDL_GetWindowProperties(window);
    if (!props) return false;

    const char* driver = SDL_GetCurrentVideoDriver();
    if (driver && SDL_strcmp(driver, "wayland") == 0) {
        wl_display* display = (wl_display*)SDL_GetPointerProperty(
            props, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, NULL
        );
        if (display) {
            NFD_SetWaylandDisplay(display);
            return true;
        }
    }

    return false;
}

/**
 * Converts an SDL window handle to a native window handle that can be passed to NFDe.
 * @param sdlWindow The SDL window handle.
 * @param[out] nativeWindow The output native window handle, populated if and only if this function
 * returns true.
 * @return Either true to indicate success, or false to indicate failure.  If false is returned,
 * you can call SDL_GetError() for more information.  However, it is intended that users ignore the
 * error and simply pass a value-initialized nfdwindowhandle_t to NFDe if this function fails. */
NFD_INLINE nfdresult_t NFD_GetNativeWindowFromSDLWindow(
    SDL_Window* window, nfdwindowhandle_t* outHandle
) {
    if (!window || !outHandle) {
        return NFD_ERROR;
    }

    // Get the properties container for this specific window
    SDL_PropertiesID props = SDL_GetWindowProperties(window);
    if (!props) {
        return NFD_ERROR;
    }

    const char* driver = SDL_GetCurrentVideoDriver();
    if (!driver) {
        return NFD_ERROR;
    }

    // Check the active driver and pull the corresponding native property
    if (SDL_strcmp(driver, "wayland") == 0) {
        outHandle->type = NFD_WINDOW_HANDLE_TYPE_WAYLAND;
        outHandle->handle = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, NULL);
    }
    else if (SDL_strcmp(driver, "x11") == 0) {
        outHandle->type = NFD_WINDOW_HANDLE_TYPE_X11;
        // X11 Window ID is a number in SDL3 (Uint64).
        // We need to cast it to void* for NFD's struct.
        outHandle->handle = (void*)(uintptr_t)SDL_GetNumberProperty(props, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0);
    }
    else if (SDL_strcmp(driver, "windows") == 0) {
        outHandle->type = NFD_WINDOW_HANDLE_TYPE_WINDOWS;
        outHandle->handle = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
    }
    else if (SDL_strcmp(driver, "cocoa") == 0) {
        outHandle->type = NFD_WINDOW_HANDLE_TYPE_COCOA;
        outHandle->handle = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, NULL);
    }
    else {
        return NFD_ERROR;
    }

    return NFD_OKAY;
}
#undef NFD_INLINE
#ifdef __cplusplus
}
#endif  // __cplusplus

#endif  // _NFD_SDL2_H

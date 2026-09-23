#ifndef MINIGL_LIBRARY_BASE_H
#define MINIGL_LIBRARY_BASE_H
#include <exec/libraries.h>
#include <exec/semaphores.h>
struct MiniGLLibrary {
    struct Library lib;
    BPTR segList;
    struct SignalSemaphore lock;
};
#endif

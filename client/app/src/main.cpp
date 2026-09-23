// SPDX-License-Identifier: GPL-3.0-or-later
#include "kvmwindow.h"
#include <QApplication>
#include <QCoreApplication>
#define SDL_MAIN_HANDLED // Qt supplies the Windows entry point; do not rename main to SDL_main.
#include <SDL.h>

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("RKMoon");
    QCoreApplication::setApplicationName("RKMoon HDMI KVM");
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_EVENTS) != 0) return 1;
    KvmWindow window;
    window.show();
    const int result = app.exec();
    SDL_Quit();
    return result;
}

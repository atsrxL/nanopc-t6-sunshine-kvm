// SPDX-License-Identifier: GPL-3.0-or-later
//
// RKMoon minimal KVM client: gamepad support is removed.
//
// Upstream's streaming/input/gamepad.cpp is not built. This translation unit provides the
// no-op implementations of the controller entry points that streaming/session.cpp and
// streaming/input/input.cpp still reference, so no controller, rumble, LED, battery or
// motion packet is ever sent to the host and SDL's joystick/gamecontroller/haptic
// subsystems are never initialized (see the input.cpp overlay patch).
//
// Keyboard, mouse and touch input are untouched and still use the upstream implementations.
#include "streaming/input/input.h"
#include "streaming/session.h"

#include <Limelight.h>

// Only referenced by upstream's gamepad code, which is not built. Defined so the static
// member still has exactly one definition if anything else ever refers to it.
const int SdlInputHandler::k_ButtonMap[] = {0}; // MSVC does not permit a zero-sized array

GamepadState*
SdlInputHandler::findStateForGamepad(SDL_JoystickID)
{
    return nullptr;
}

void SdlInputHandler::sendGamepadState(GamepadState*)
{
}

void SdlInputHandler::sendGamepadBatteryState(GamepadState*, SDL_JoystickPowerLevel)
{
}

Uint32 SdlInputHandler::mouseEmulationTimerCallback(Uint32, void*)
{
    // Gamepad mouse emulation is not available; cancel the timer if one somehow exists.
    return 0;
}

void SdlInputHandler::handleControllerAxisEvent(SDL_ControllerAxisEvent*)
{
}

void SdlInputHandler::handleControllerButtonEvent(SDL_ControllerButtonEvent*)
{
}

#if SDL_VERSION_ATLEAST(2, 0, 14)
void SdlInputHandler::handleControllerSensorEvent(SDL_ControllerSensorEvent*)
{
}

void SdlInputHandler::handleControllerTouchpadEvent(SDL_ControllerTouchpadEvent*)
{
}
#endif

#if SDL_VERSION_ATLEAST(2, 24, 0)
void SdlInputHandler::handleJoystickBatteryEvent(SDL_JoyBatteryEvent*)
{
}
#endif

void SdlInputHandler::handleControllerDeviceEvent(SDL_ControllerDeviceEvent*)
{
}

void SdlInputHandler::handleJoystickArrivalEvent(SDL_JoyDeviceEvent*)
{
}

void SdlInputHandler::rumble(unsigned short, unsigned short, unsigned short)
{
}

void SdlInputHandler::rumbleTriggers(uint16_t, uint16_t, uint16_t)
{
}

void SdlInputHandler::setMotionEventState(uint16_t, uint8_t, uint16_t)
{
}

void SdlInputHandler::setControllerLED(uint16_t, uint8_t, uint8_t, uint8_t)
{
}

QString SdlInputHandler::getUnmappedGamepads()
{
    return QString();
}

int SdlInputHandler::getAttachedGamepadMask()
{
    return 0;
}

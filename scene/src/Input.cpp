#include "scene/Input.hpp"
#include <cstring>

bool Input::currentMouseState[MAX_MOUSE_BUTTONS] = {false};
bool Input::previousMouseState[MAX_MOUSE_BUTTONS] = {false};
Vec2 Input::mousePosition = Vec2(0.0f, 0.0f);
Vec2 Input::mousePreviousPosition = Vec2(0.0f, 0.0f);
Vec2 Input::mouseOffset = Vec2(0.0f, 0.0f);
Vec2 Input::mouseScale = Vec2(1.0f, 1.0f);
Vec2 Input::mouseWheel = Vec2(0.0f, 0.0f);
SDL_Cursor* Input::mouseCursor = nullptr;
MouseCursor Input::currentCursor = MouseCursor::DEFAULT;

// Keyboard
bool Input::currentKeyState[MAX_KEYBOARD_KEYS] = {false};
bool Input::previousKeyState[MAX_KEYBOARD_KEYS] = {false};
KeyCode Input::keyPressedQueue[MAX_KEY_PRESSED_QUEUE] = {KEY_NULL};
int Input::keyPressedQueueCount = 0;
int Input::charPressedQueue[MAX_CHAR_PRESSED_QUEUE] = {0};
int Input::charPressedQueueCount = 0;

// Gamepad
SDL_GameController* Input::gamepads[MAX_GAMEPADS] = {nullptr};
bool Input::currentGamepadButtonState[MAX_GAMEPADS][MAX_GAMEPAD_BUTTONS] = {{false}};
bool Input::previousGamepadButtonState[MAX_GAMEPADS][MAX_GAMEPAD_BUTTONS] = {{false}};
float Input::gamepadAxisState[MAX_GAMEPADS][MAX_GAMEPAD_AXIS] = {{0.0f}};
int Input::lastButtonPressed = GAMEPAD_BUTTON_UNKNOWN;

// ========== SDL -> engine conversion ==========

static const KeyCode scancodeMap[232] = {
    KEY_NULL, KEY_NULL, KEY_NULL, KEY_NULL,
    KEY_A, KEY_B, KEY_C, KEY_D, KEY_E, KEY_F, KEY_G, KEY_H, KEY_I, KEY_J, KEY_K, KEY_L, KEY_M,
    KEY_N, KEY_O, KEY_P, KEY_Q, KEY_R, KEY_S, KEY_T, KEY_U, KEY_V, KEY_W, KEY_X, KEY_Y, KEY_Z,
    KEY_ONE, KEY_TWO, KEY_THREE, KEY_FOUR, KEY_FIVE, KEY_SIX, KEY_SEVEN, KEY_EIGHT, KEY_NINE, KEY_ZERO,
    KEY_ENTER, KEY_ESCAPE, KEY_BACKSPACE, KEY_TAB, KEY_SPACE,
    KEY_MINUS, KEY_EQUAL, KEY_LEFT_BRACKET, KEY_RIGHT_BRACKET, KEY_BACKSLASH, KEY_NULL,
    KEY_SEMICOLON, KEY_APOSTROPHE, KEY_GRAVE, KEY_COMMA, KEY_PERIOD, KEY_SLASH,
    KEY_CAPS_LOCK,
    KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6, KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12,
    KEY_PRINT_SCREEN, KEY_SCROLL_LOCK, KEY_PAUSE, KEY_INSERT, KEY_HOME, KEY_PAGE_UP,
    KEY_DELETE, KEY_END, KEY_PAGE_DOWN, KEY_RIGHT, KEY_LEFT, KEY_DOWN, KEY_UP,
    KEY_NUM_LOCK, KEY_KP_DIVIDE, KEY_KP_MULTIPLY, KEY_KP_SUBTRACT, KEY_KP_ADD, KEY_KP_ENTER,
    KEY_KP_1, KEY_KP_2, KEY_KP_3, KEY_KP_4, KEY_KP_5, KEY_KP_6, KEY_KP_7, KEY_KP_8, KEY_KP_9, KEY_KP_0,
    KEY_KP_DECIMAL};

KeyCode Input::ConvertSDLScancode(SDL_Scancode scancode)
{
    if (scancode >= 0 && scancode < 93) return scancodeMap[scancode];
    if (scancode == SDL_SCANCODE_LCTRL) return KEY_LEFT_CONTROL;
    if (scancode == SDL_SCANCODE_LSHIFT) return KEY_LEFT_SHIFT;
    if (scancode == SDL_SCANCODE_LALT) return KEY_LEFT_ALT;
    if (scancode == SDL_SCANCODE_LGUI) return KEY_LEFT_SUPER;
    if (scancode == SDL_SCANCODE_RCTRL) return KEY_RIGHT_CONTROL;
    if (scancode == SDL_SCANCODE_RSHIFT) return KEY_RIGHT_SHIFT;
    if (scancode == SDL_SCANCODE_RALT) return KEY_RIGHT_ALT;
    if (scancode == SDL_SCANCODE_RGUI) return KEY_RIGHT_SUPER;
    return KEY_NULL;
}

MouseButton Input::ConvertSDLButton(Uint8 button)
{
    if (button == SDL_BUTTON_LEFT) return MouseButton::LEFT;
    if (button == SDL_BUTTON_RIGHT) return MouseButton::RIGHT;
    if (button == SDL_BUTTON_MIDDLE) return MouseButton::MIDDLE;
    return MouseButton::LEFT;
}

GamepadButton Input::ConvertSDLGamepadButton(SDL_GameControllerButton button)
{
    switch (button)
    {
    case SDL_CONTROLLER_BUTTON_A: return GAMEPAD_BUTTON_RIGHT_FACE_DOWN;
    case SDL_CONTROLLER_BUTTON_B: return GAMEPAD_BUTTON_RIGHT_FACE_RIGHT;
    case SDL_CONTROLLER_BUTTON_X: return GAMEPAD_BUTTON_RIGHT_FACE_LEFT;
    case SDL_CONTROLLER_BUTTON_Y: return GAMEPAD_BUTTON_RIGHT_FACE_UP;
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: return GAMEPAD_BUTTON_LEFT_TRIGGER_1;
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return GAMEPAD_BUTTON_RIGHT_TRIGGER_1;
    case SDL_CONTROLLER_BUTTON_BACK: return GAMEPAD_BUTTON_MIDDLE_LEFT;
    case SDL_CONTROLLER_BUTTON_START: return GAMEPAD_BUTTON_MIDDLE_RIGHT;
    case SDL_CONTROLLER_BUTTON_GUIDE: return GAMEPAD_BUTTON_MIDDLE;
    case SDL_CONTROLLER_BUTTON_LEFTSTICK: return GAMEPAD_BUTTON_LEFT_THUMB;
    case SDL_CONTROLLER_BUTTON_RIGHTSTICK: return GAMEPAD_BUTTON_RIGHT_THUMB;
    case SDL_CONTROLLER_BUTTON_DPAD_UP: return GAMEPAD_BUTTON_LEFT_FACE_UP;
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return GAMEPAD_BUTTON_LEFT_FACE_DOWN;
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return GAMEPAD_BUTTON_LEFT_FACE_LEFT;
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return GAMEPAD_BUTTON_LEFT_FACE_RIGHT;
    default: return GAMEPAD_BUTTON_UNKNOWN;
    }
}

// ========== SDL callbacks ==========

void Input::OnMouseDown(const SDL_MouseButtonEvent& event)
{
    if (event.button != SDL_BUTTON_LEFT && event.button != SDL_BUTTON_RIGHT &&
        event.button != SDL_BUTTON_MIDDLE)
        return;
    currentMouseState[ConvertSDLButton(event.button)] = true;
}

void Input::OnMouseUp(const SDL_MouseButtonEvent& event)
{
    if (event.button != SDL_BUTTON_LEFT && event.button != SDL_BUTTON_RIGHT &&
        event.button != SDL_BUTTON_MIDDLE)
        return;
    currentMouseState[ConvertSDLButton(event.button)] = false;
}

void Input::OnMouseMove(const SDL_MouseMotionEvent& event)
{
    mousePosition.x = event.x * mouseScale.x + mouseOffset.x;
    mousePosition.y = event.y * mouseScale.y + mouseOffset.y;
}

void Input::OnMouseWheel(const SDL_MouseWheelEvent& event)
{
    mouseWheel.x = (float)event.x;
    mouseWheel.y = (float)event.y;
}

void Input::OnKeyDown(const SDL_KeyboardEvent& event)
{
    KeyCode key = ConvertSDLScancode(event.keysym.scancode);
    if (key != KEY_NULL)
    {
        if (!currentKeyState[key] && keyPressedQueueCount < MAX_KEY_PRESSED_QUEUE)
            keyPressedQueue[keyPressedQueueCount++] = key;
        currentKeyState[key] = true;
    }
}

void Input::OnKeyUp(const SDL_KeyboardEvent& event)
{
    KeyCode key = ConvertSDLScancode(event.keysym.scancode);
    if (key != KEY_NULL) currentKeyState[key] = false;
}

void Input::OnTextInput(const SDL_TextInputEvent& event)
{
    // SDL sends UTF-8 chars in event.text
    const unsigned char* text = (const unsigned char*)event.text;
    while (*text && charPressedQueueCount < MAX_CHAR_PRESSED_QUEUE) charPressedQueue[charPressedQueueCount++] = *text++;
}

// ========== Mouse ==========

bool Input::IsMousePressed(MouseButton button)
{
    return currentMouseState[button] && !previousMouseState[button];
}

bool Input::IsMouseDown(MouseButton button) { return currentMouseState[button]; }

bool Input::IsMouseReleased(MouseButton button)
{
    return !currentMouseState[button] && previousMouseState[button];
}

bool Input::IsMouseUp(MouseButton button) { return !currentMouseState[button]; }

Vec2 Input::GetMousePosition() { return mousePosition; }

Vec2 Input::GetMouseDelta()
{
    return Vec2(mousePosition.x - mousePreviousPosition.x, mousePosition.y - mousePreviousPosition.y);
}

int Input::GetMouseX() { return (int)mousePosition.x; }
int Input::GetMouseY() { return (int)mousePosition.y; }

void Input::SetMousePosition(int x, int y) { mousePosition = Vec2((float)x, (float)y); }
void Input::SetMouseOffset(int offsetX, int offsetY) { mouseOffset = Vec2((float)offsetX, (float)offsetY); }
void Input::SetMouseScale(float scaleX, float scaleY) { mouseScale = Vec2(scaleX, scaleY); }

Vec2 Input::GetMouseWheelMove() { return mouseWheel; }
float Input::GetMouseWheelMoveV() { return mouseWheel.y; }

void Input::SetMouseCursor(MouseCursor cursor)
{
    static const int cursors[] = {
        SDL_SYSTEM_CURSOR_ARROW, SDL_SYSTEM_CURSOR_ARROW, SDL_SYSTEM_CURSOR_IBEAM,
        SDL_SYSTEM_CURSOR_CROSSHAIR, SDL_SYSTEM_CURSOR_HAND, SDL_SYSTEM_CURSOR_SIZEWE,
        SDL_SYSTEM_CURSOR_SIZENS, SDL_SYSTEM_CURSOR_SIZENWSE, SDL_SYSTEM_CURSOR_SIZENESW,
        SDL_SYSTEM_CURSOR_SIZEALL, SDL_SYSTEM_CURSOR_NO};

    if (mouseCursor) SDL_FreeCursor(mouseCursor);
    mouseCursor = SDL_CreateSystemCursor((SDL_SystemCursor)cursors[cursor]);
    SDL_SetCursor(mouseCursor);
    currentCursor = cursor;
}

// ========== Keyboard ==========

bool Input::IsKeyPressed(KeyCode key)
{
    if ((int)key >= MAX_KEYBOARD_KEYS) return false;
    return currentKeyState[key] && !previousKeyState[key];
}

bool Input::IsKeyDown(KeyCode key)
{
    if ((int)key >= MAX_KEYBOARD_KEYS) return false;
    return currentKeyState[key];
}

bool Input::IsKeyReleased(KeyCode key)
{
    if ((int)key >= MAX_KEYBOARD_KEYS) return false;
    return !currentKeyState[key] && previousKeyState[key];
}

bool Input::IsKeyUp(KeyCode key)
{
    if ((int)key >= MAX_KEYBOARD_KEYS) return false;
    return !currentKeyState[key];
}

KeyCode Input::GetKeyPressed() { return (keyPressedQueueCount > 0) ? keyPressedQueue[0] : KEY_NULL; }
int Input::GetCharPressed() { return (charPressedQueueCount > 0) ? charPressedQueue[0] : 0; }

// ========== Gamepad ==========
// Connect/disconnect and axis-motion events are not wired up anywhere yet
// (same as the tmp/core reference: buttons/axes always read as
// unavailable/zero until something calls SDL_GameControllerOpen and feeds
// SDL_CONTROLLERBUTTON*/SDL_CONTROLLERAXISMOTION into this class).

bool Input::IsGamepadAvailable(int gamepad)
{
    return gamepad >= 0 && gamepad < MAX_GAMEPADS && gamepads[gamepad] != nullptr;
}

const char* Input::GetGamepadName(int gamepad)
{
    if (!IsGamepadAvailable(gamepad)) return "";
    return SDL_GameControllerName(gamepads[gamepad]);
}

bool Input::IsGamepadButtonPressed(int gamepad, GamepadButton button)
{
    if (!IsGamepadAvailable(gamepad) || (int)button >= MAX_GAMEPAD_BUTTONS) return false;
    return currentGamepadButtonState[gamepad][button] && !previousGamepadButtonState[gamepad][button];
}

bool Input::IsGamepadButtonDown(int gamepad, GamepadButton button)
{
    if (!IsGamepadAvailable(gamepad) || (int)button >= MAX_GAMEPAD_BUTTONS) return false;
    return currentGamepadButtonState[gamepad][button];
}

bool Input::IsGamepadButtonReleased(int gamepad, GamepadButton button)
{
    if (!IsGamepadAvailable(gamepad) || (int)button >= MAX_GAMEPAD_BUTTONS) return false;
    return !currentGamepadButtonState[gamepad][button] && previousGamepadButtonState[gamepad][button];
}

bool Input::IsGamepadButtonUp(int gamepad, GamepadButton button)
{
    if (!IsGamepadAvailable(gamepad) || (int)button >= MAX_GAMEPAD_BUTTONS) return false;
    return !currentGamepadButtonState[gamepad][button];
}

int Input::GetGamepadButtonPressed() { return lastButtonPressed; }

int Input::GetGamepadAxisCount(int gamepad) { return IsGamepadAvailable(gamepad) ? MAX_GAMEPAD_AXIS : 0; }

float Input::GetGamepadAxisMovement(int gamepad, GamepadAxis axis)
{
    if (!IsGamepadAvailable(gamepad) || (int)axis >= MAX_GAMEPAD_AXIS) return 0.0f;
    return gamepadAxisState[gamepad][axis];
}

// ========== Lifecycle ==========

void Input::Init()
{
    memset(currentMouseState, 0, sizeof(currentMouseState));
    memset(previousMouseState, 0, sizeof(previousMouseState));

    for (int i = 0; i < MAX_GAMEPADS; ++i)
        for (int a = 0; a < MAX_GAMEPAD_AXIS; ++a) gamepadAxisState[i][a] = 0.0f;

    for (int i = 0; i < MAX_KEYBOARD_KEYS; ++i)
    {
        previousKeyState[i] = false;
        currentKeyState[i] = false;
    }
    for (int i = 0; i < MAX_KEY_PRESSED_QUEUE; ++i) keyPressedQueue[i] = KEY_NULL;
}

void Input::Update()
{
    for (int i = 0; i < MAX_GAMEPADS; ++i)
        for (int k = 0; k < MAX_GAMEPAD_BUTTONS; ++k)
            previousGamepadButtonState[i][k] = currentGamepadButtonState[i][k];

    for (int i = 0; i < MAX_KEYBOARD_KEYS; ++i) previousKeyState[i] = currentKeyState[i];

    for (int i = 0; i < MAX_GAMEPADS; ++i)
        for (int k = 0; k < MAX_GAMEPAD_AXIS; ++k) gamepadAxisState[i][k] = 0.0f;

    for (int i = 0; i < MAX_MOUSE_BUTTONS; ++i) previousMouseState[i] = currentMouseState[i];

    mousePreviousPosition = mousePosition;
    mouseWheel = Vec2(0.0f, 0.0f);
    keyPressedQueueCount = 0;
    charPressedQueueCount = 0;
}

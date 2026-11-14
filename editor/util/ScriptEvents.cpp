// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "ScriptEvents.h"

#include "ScriptParser.h"
#include "external/IconsFontAwesome6.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <optional>
#include <unordered_set>

using namespace doriax::editor;

namespace {

constexpr size_t npos = std::string::npos;
const std::string indentUnit = "    ";

using S = ScriptEventSource;

const std::vector<ScriptEventSourceInfo>& sourceList() {
    static const std::vector<ScriptEventSourceInfo> sources = {
        {S::Engine, "Engine", ICON_FA_GEARS, "Engine.h", "", "REGISTER_ENGINE_EVENT", ""},
        {S::Input, "Input", ICON_FA_GAMEPAD, "Engine.h", "", "REGISTER_ENGINE_EVENT", ""},
        {S::UI, "UI", ICON_FA_SQUARE, "UIComponent.h", "UIComponent", "REGISTER_UI_EVENT", "Image"},
        {S::Button, "Button", ICON_FA_SQUARE_CHECK, "ButtonComponent.h", "ButtonComponent", "REGISTER_BUTTON_EVENT", "Button"},
        {S::Scrollbar, "Scrollbar", ICON_FA_ARROWS_UP_DOWN, "ScrollbarComponent.h", "ScrollbarComponent", "REGISTER_SCROLLBAR_EVENT", "Scrollbar"},
        {S::Panel, "Panel", ICON_FA_WINDOW_MAXIMIZE, "PanelComponent.h", "PanelComponent", "REGISTER_PANEL_EVENT", "Panel"},
        {S::TextEdit, "Text Edit", ICON_FA_KEYBOARD, "TextEditComponent.h", "TextEditComponent", "REGISTER_COMPONENT_EVENT", "TextEdit"},
        {S::Action, "Action", ICON_FA_PLAY, "ActionComponent.h", "ActionComponent", "REGISTER_COMPONENT_EVENT", "Action"},
        {S::Sound, "Sound", ICON_FA_VOLUME_HIGH, "SoundComponent.h", "SoundComponent", "REGISTER_COMPONENT_EVENT", "Sound"},
        {S::Physics2D, "Physics 2D", ICON_FA_CIRCLE, "PhysicsSystem.h", "", "REGISTER_EVENT", ""},
        {S::Physics3D, "Physics 3D", ICON_FA_CUBE, "PhysicsSystem.h", "", "REGISTER_EVENT", ""},
        {S::AdMob, "AdMob", ICON_FA_RECTANGLE_AD, "AdMob.h", "", "REGISTER_EVENT", "", "AdMob"},
        {S::InAppPurchase, "In-App Purchase", ICON_FA_CART_SHOPPING, "InAppPurchase.h", "", "REGISTER_EVENT", "", "InAppPurchase"},
        {S::WebPortal, "Web Portal", ICON_FA_GLOBE, "WebPortal.h", "", "REGISTER_EVENT", "", "WebPortal"},
    };
    return sources;
}

// Parameters follow the FunctionSubscribe signatures, C++ handlers must match their types
const std::vector<ScriptEvent>& eventList() {
    static const std::vector<ScriptEventParam> pointer = {{"float", "x"}, {"float", "y"}};
    static const std::vector<ScriptEventParam> key = {{"int", "key"}, {"bool", "isRepeat"}, {"int", "mods"}};
    static const std::vector<ScriptEventParam> mouseButton = {{"int", "button"}, {"float", "x"}, {"float", "y"}, {"int", "mods"}};
    static const std::vector<ScriptEventParam> touch = {{"int", "pointer"}, {"float", "x"}, {"float", "y"}};
    static const std::vector<ScriptEventParam> gamepadButton = {{"int", "gamepad"}, {"int", "button"}};
    static const std::vector<ScriptEventParam> contact2D = {{"Body2D", "bodyA"}, {"unsigned long", "shapeA"}, {"Body2D", "bodyB"}, {"unsigned long", "shapeB"}};
    static const std::vector<ScriptEventParam> sensor2D = {{"Body2D", "sensorBody"}, {"unsigned long", "sensorShape"}, {"Body2D", "visitorBody"}, {"unsigned long", "visitorShape"}};
    static const std::vector<ScriptEventParam> contact3D = {{"Body3D", "bodyA"}, {"Body3D", "bodyB"}, {"Contact3D", "contact"}};
    static const std::vector<ScriptEventParam> adError = {{"AdMobFormat", "format"}, {"int", "errorCode"}, {"std::string", "message"}};
    static const std::vector<ScriptEventParam> errorMessage = {{"int", "errorCode"}, {"std::string", "message"}};
    static const std::vector<ScriptEventParam> billingResult = {{"BillingResponse", "response"}, {"std::string", "message"}};
    static const std::vector<ScriptEventParam> tokenResult = {{"std::string", "purchaseToken"}, {"BillingResponse", "response"}, {"std::string", "message"}};

    static const std::vector<ScriptEvent> events = {
        {S::Engine, "onUpdate", "Frame", "Every frame, with a variable time step", {}},
        {S::Engine, "onFixedUpdate", "Frame", "Every physics step, with a fixed time step", {}},
        {S::Engine, "onPostUpdate", "Frame", "Every frame, after the scenes update", {}},
        {S::Engine, "onDraw", "Frame", "Every frame, before the scenes are drawn", {}},
        {S::Engine, "onViewLoaded", "Application", "The view is ready", {}},
        {S::Engine, "onViewChanged", "Application", "The view or the canvas size changes", {}, false, "onCanvasChanged"},
        {S::Engine, "onViewDestroyed", "Application", "The view is destroyed", {}},
        {S::Engine, "onPause", "Application", "The application is paused", {}},
        {S::Engine, "onResume", "Application", "The application resumes", {}},
        {S::Engine, "onShutdown", "Application", "The application shuts down", {}},
        {S::Engine, "onSceneLoaded", "Scene", "The scene loaded by SceneManager is ready", {}},

        {S::Input, "onKeyDown", "Keyboard", "A key is pressed", key},
        {S::Input, "onKeyUp", "Keyboard", "A key is released", key},
        {S::Input, "onCharInput", "Keyboard", "A text character is typed", {{"wchar_t", "codepoint"}}},
        {S::Input, "onMouseDown", "Mouse", "A mouse button is pressed", mouseButton},
        {S::Input, "onMouseUp", "Mouse", "A mouse button is released", mouseButton},
        {S::Input, "onMouseMove", "Mouse", "The mouse moves", {{"float", "x"}, {"float", "y"}, {"int", "mods"}}},
        {S::Input, "onMouseScroll", "Mouse", "The mouse wheel scrolls", {{"float", "xoffset"}, {"float", "yoffset"}, {"int", "mods"}}},
        {S::Input, "onMouseEnter", "Mouse", "The mouse enters the window", {}},
        {S::Input, "onMouseLeave", "Mouse", "The mouse leaves the window", {}},
        {S::Input, "onTouchStart", "Touch", "A touch begins", touch},
        {S::Input, "onTouchMove", "Touch", "A touch moves", touch},
        {S::Input, "onTouchEnd", "Touch", "A touch ends", touch},
        {S::Input, "onTouchCancel", "Touch", "A touch is cancelled without a release or click; uses its last recorded canvas position", touch},
        {S::Input, "onGamepadConnect", "Gamepad", "A gamepad is connected", {{"int", "gamepad"}}},
        {S::Input, "onGamepadDisconnect", "Gamepad", "A gamepad is disconnected", {{"int", "gamepad"}}},
        {S::Input, "onGamepadButtonDown", "Gamepad", "A gamepad button is pressed", gamepadButton},
        {S::Input, "onGamepadButtonUp", "Gamepad", "A gamepad button is released", gamepadButton},
        {S::Input, "onGamepadAxisMove", "Gamepad", "A gamepad axis moves, from -1 to 1", {{"int", "gamepad"}, {"int", "axis"}, {"float", "value"}}},

        {S::UI, "onClick", "Pointer", "The element is clicked", pointer},
        {S::UI, "onDoubleClick", "Pointer", "The element is double-clicked", pointer},
        {S::UI, "onPointerDown", "Pointer", "The pointer is pressed on the element", pointer},
        {S::UI, "onPointerUp", "Pointer", "The pointer is released on the element", pointer},
        {S::UI, "onPointerMove", "Pointer", "The pointer moves over the element", pointer},
        {S::UI, "onPointerEnter", "Pointer", "The pointer enters the element", pointer},
        {S::UI, "onPointerLeave", "Pointer", "The pointer leaves the element", pointer},
        {S::UI, "onDragStart", "Drag", "A drag starts on the element", pointer},
        {S::UI, "onDrag", "Drag", "The element is dragged", pointer},
        {S::UI, "onDragEnd", "Drag", "The drag ends", pointer},
        {S::UI, "onGetFocus", "Focus", "The element gets the focus", {}},
        {S::UI, "onLostFocus", "Focus", "The element loses the focus", {}},

        {S::Button, "onPress", nullptr, "The button is pressed", {}},
        {S::Button, "onRelease", nullptr, "The button is released", {}},

        {S::Scrollbar, "onChange", nullptr, "The bar moves", {{"float", "step"}}},

        {S::Panel, "onMove", nullptr, "The panel is moved", {}},
        {S::Panel, "onResize", nullptr, "The panel is resized", {{"int", "width"}, {"int", "height"}}},

        {S::TextEdit, "onChange", nullptr, "The text changes", {}},
        {S::TextEdit, "onSubmit", nullptr, "Enter is pressed", {}},

        {S::Action, "onStart", nullptr, "The action starts", {}},
        {S::Action, "onPause", nullptr, "The action is paused", {}},
        {S::Action, "onStop", nullptr, "The action stops", {}},
        {S::Action, "onStep", nullptr, "Every update while the action runs", {}},

        {S::Sound, "onStart", nullptr, "The sound starts playing", {}},
        {S::Sound, "onPause", nullptr, "The sound is paused", {}},
        {S::Sound, "onStop", nullptr, "The sound stops", {}},

        {S::Physics2D, "beginContact2D", "Contacts", "Any two bodies start touching, needs Contact Events on a shape", contact2D},
        {S::Physics2D, "endContact2D", "Contacts", "Any two bodies stop touching, needs Contact Events on a shape", contact2D},
        {S::Physics2D, "hitContact2D", "Contacts", "Any two bodies hit each other, needs hit events on a shape",
         {{"Body2D", "bodyA"}, {"unsigned long", "shapeA"}, {"Body2D", "bodyB"}, {"unsigned long", "shapeB"},
          {"Vector2", "point"}, {"Vector2", "normal"}, {"float", "speed"}}},
        {S::Physics2D, "beginSensorContact2D", "Sensors", "A shape enters any sensor", sensor2D},
        {S::Physics2D, "endSensorContact2D", "Sensors", "A shape leaves any sensor", sensor2D},
        {S::Physics2D, "preSolve2D", "Filters", "Before any contact is solved, return false to disable it",
         {{"Body2D", "bodyA"}, {"unsigned long", "shapeA"}, {"Body2D", "bodyB"}, {"unsigned long", "shapeB"},
          {"Manifold2D", "manifold"}}, true},
        {S::Physics2D, "shouldCollide2D", "Filters", "Whether any two shapes collide, return false to skip them", contact2D, true},

        {S::Physics3D, "onContactAdded3D", "Contacts", "Any two bodies start touching", contact3D},
        {S::Physics3D, "onContactPersisted3D", "Contacts", "Any two bodies keep touching", contact3D},
        {S::Physics3D, "onContactRemoved3D", "Contacts", "Any two bodies stop touching",
         {{"Body3D", "bodyA"}, {"Body3D", "bodyB"}, {"unsigned long", "shapeA"}, {"unsigned long", "shapeB"}}},
        {S::Physics3D, "onBodyActivated3D", "Bodies", "Any body wakes up", {{"Body3D", "body"}}},
        {S::Physics3D, "onBodyDeactivated3D", "Bodies", "Any body goes to sleep", {{"Body3D", "body"}}},
        {S::Physics3D, "shouldCollide3D", "Filters", "Whether any two bodies collide, return false to skip them",
         {{"Body3D", "bodyA"}, {"Body3D", "bodyB"}, {"Vector3", "baseOffset"}, {"CollideShapeResult3D", "result"}}, true},

        {S::AdMob, "onInitialized", "Setup", "Google Mobile Ads is ready to load ads", {}},
        {S::AdMob, "onConsentUpdated", "Setup", "The consent request or privacy options form finished, errorCode is 0 on success", errorMessage},
        {S::AdMob, "onAdInspectorClosed", "Setup", "The ad inspector closed", errorMessage},
        {S::AdMob, "onAdLoaded", "Loading", "An ad loaded and can be shown", {{"AdMobFormat", "format"}}},
        {S::AdMob, "onAdFailedToLoad", "Loading", "An ad failed to load", adError},
        {S::AdMob, "onAdShown", "Showing", "A full screen ad opened, or a banner opened an overlay", {{"AdMobFormat", "format"}}},
        {S::AdMob, "onAdFailedToShow", "Showing", "A full screen ad could not be shown", adError},
        {S::AdMob, "onAdDismissed", "Showing", "A full screen ad or a banner overlay closed", {{"AdMobFormat", "format"}}},
        {S::AdMob, "onAdClicked", "Showing", "An ad was clicked", {{"AdMobFormat", "format"}}},
        {S::AdMob, "onAdImpression", "Showing", "An ad recorded an impression", {{"AdMobFormat", "format"}}},
        {S::AdMob, "onUserEarnedReward", "Revenue", "The user earned the reward of a rewarded ad",
         {{"AdMobFormat", "format"}, {"std::string", "rewardType"}, {"int", "amount"}}},
        {S::AdMob, "onAdPaid", "Revenue", "An ad earned revenue, in micros of the currency",
         {{"AdMobFormat", "format"}, {"long long", "valueMicros"}, {"std::string", "currencyCode"}, {"AdMobPrecision", "precision"}}},

        {S::InAppPurchase, "onInitialized", "Connection", "The store connection is set up, check the response", billingResult},
        {S::InAppPurchase, "onDisconnected", "Connection", "The store connection was lost", {}},
        {S::InAppPurchase, "onProductsQueried", "Products", "Product details arrived, read them with getProducts", billingResult},
        {S::InAppPurchase, "onPurchaseUpdated", "Purchases", "A purchase is new, changed or restored: grant it when purchased",
         {{"PurchaseDetails", "purchase"}}},
        {S::InAppPurchase, "onPurchaseFailed", "Purchases", "A purchase flow failed or was canceled",
         {{"std::string", "productId"}, {"BillingResponse", "response"}, {"std::string", "message"}}},
        {S::InAppPurchase, "onPurchasesQueried", "Purchases", "The owned purchases of a type were reported",
         {{"ProductType", "productType"}, {"BillingResponse", "response"}, {"std::string", "message"}}},
        {S::InAppPurchase, "onPurchaseAcknowledged", "Purchases", "A purchase was acknowledged", tokenResult},
        {S::InAppPurchase, "onPurchaseConsumed", "Purchases", "A purchase was consumed and can be bought again", tokenResult},

        {S::WebPortal, "onInitialized", "Setup", "The portal SDK initialized, in the given environment", {{"WebPortalEnvironment", "environment"}}},
        {S::WebPortal, "onAdStarted", "Ads", "An ad started; the engine pauses until it ends", {{"WebPortalAdType", "type"}}},
        {S::WebPortal, "onAdFinished", "Ads", "An ad finished: grant the reward of a rewarded ad", {{"WebPortalAdType", "type"}}},
        {S::WebPortal, "onAdError", "Ads", "No ad played, or it failed",
         {{"WebPortalAdType", "type"}, {"std::string", "code"}, {"std::string", "message"}}},
        {S::WebPortal, "onDataLoaded", "Cloud Save", "The cloud save loaded; saving works from now on", {{"std::string", "data"}}},
        {S::WebPortal, "onDataLoadFailed", "Cloud Save", "The cloud save could not be loaded", {{"std::string", "message"}}},
        {S::WebPortal, "onDataSaveFailed", "Cloud Save", "Saving to the cloud failed", {{"std::string", "message"}}},
    };
    return events;
}

const ScriptEventSourceInfo& sourceInfo(ScriptEventSource source) {
    for (const ScriptEventSourceInfo& info : sourceList()) {
        if (info.source == source) return info;
    }
    return sourceList().front();
}

bool isEngineSource(ScriptEventSource source) {
    return source == S::Engine || source == S::Input;
}

bool isPhysicsSource(ScriptEventSource source) {
    return source == S::Physics2D || source == S::Physics3D;
}

bool isClassSource(ScriptEventSource source) {
    return *sourceInfo(source).eventClass != '\0';
}

const char* luaEventName(const ScriptEvent& event) {
    return event.luaName ? event.luaName : event.name;
}

int findEvent(ScriptEventSource source, const std::string& name, bool lua) {
    const std::vector<ScriptEvent>& events = eventList();
    for (size_t i = 0; i < events.size(); i++) {
        const bool sameSource = events[i].source == source || (isEngineSource(source) && isEngineSource(events[i].source));
        if (sameSource && name == (lua ? luaEventName(events[i]) : events[i].name)) return static_cast<int>(i);
    }
    return -1;
}

// For an event reached without its source, found when a single source has the name
int findEventByName(const std::string& name, bool lua) {
    const std::vector<ScriptEvent>& events = eventList();
    int found = -1;
    for (size_t i = 0; i < events.size(); i++) {
        if (isEngineSource(events[i].source) || name != (lua ? luaEventName(events[i]) : events[i].name)) continue;
        if (found >= 0) return -1;
        found = static_cast<int>(i);
    }
    return found;
}

bool isIdentChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

bool isIdentStart(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) || c == '_';
}

bool isSpace(char c) {
    return std::isspace(static_cast<unsigned char>(c)) != 0;
}

std::string trim(const std::string& text) {
    size_t start = 0;
    size_t end = text.size();
    while (start < end && isSpace(text[start])) start++;
    while (end > start && isSpace(text[end - 1])) end--;
    return text.substr(start, end - start);
}

size_t skipSpaces(const std::string& text, size_t pos) {
    while (pos < text.size() && isSpace(text[pos])) pos++;
    return pos;
}

size_t prevNonSpace(const std::string& text, size_t pos) {
    while (pos > 0) {
        if (!isSpace(text[--pos])) return pos;
    }
    return npos;
}

size_t lineStart(const std::string& text, size_t pos) {
    size_t newline = pos > 0 ? text.rfind('\n', pos - 1) : npos;
    return newline == npos ? 0 : newline + 1;
}

// Offset of the line break, or the text size on the last line
size_t lineEnd(const std::string& text, size_t pos) {
    size_t newline = text.find('\n', pos);
    return newline == npos ? text.size() : newline;
}

size_t nextLineStart(const std::string& text, size_t pos) {
    size_t end = lineEnd(text, pos);
    return end < text.size() ? end + 1 : end;
}

// First non-blank offset from 'pos', the line end when there is none
size_t lineContentStart(const std::string& text, size_t pos) {
    size_t end = lineEnd(text, pos);
    while (pos < end && isSpace(text[pos])) pos++;
    return pos;
}

bool restOfLineBlank(const std::string& text, size_t pos) {
    return lineContentStart(text, pos) == lineEnd(text, pos);
}

std::string indentOf(const std::string& text, size_t pos) {
    size_t start = lineStart(text, pos);
    size_t end = start;
    while (end < text.size() && (text[end] == ' ' || text[end] == '\t')) end++;
    return text.substr(start, end - start);
}

std::string wordAt(const std::string& text, size_t pos) {
    size_t end = pos;
    while (end < text.size() && isIdentChar(text[end])) end++;
    return text.substr(pos, end - pos);
}

// Identifier ending right before 'pos', spaces skipped
std::string wordBefore(const std::string& text, size_t pos) {
    size_t end = prevNonSpace(text, pos);
    if (end == npos || !isIdentChar(text[end])) return "";
    size_t start = end;
    while (start > 0 && isIdentChar(text[start - 1])) start--;
    return text.substr(start, end + 1 - start);
}

// "physics->beginContact2D" -> beginContact2D
std::string lastIdentifier(const std::string& expression) {
    return wordBefore(expression, expression.size());
}

// "doriax::AdMob::onInitialized" or "AdMob.onInitialized", which InAppPurchase also has
int findClassEvent(const std::string& expression, bool lua) {
    const std::string name = lastIdentifier(expression);
    if (name.empty()) return -1;

    std::string qualifier = expression.substr(0, expression.rfind(name));
    while (!qualifier.empty() && (qualifier.back() == ':' || qualifier.back() == '.' || isSpace(qualifier.back()))) {
        qualifier.pop_back();
    }
    const std::string owner = lastIdentifier(qualifier);
    for (const ScriptEventSourceInfo& source : sourceList()) {
        if (*source.eventClass && owner == source.eventClass) return findEvent(source.source, name, lua);
    }
    return -1;
}

std::string unquote(const std::string& value) {
    if (value.size() >= 2 && (value.front() == '"' || value.front() == '\'') && value.back() == value.front()) {
        return value.substr(1, value.size() - 2);
    }
    return value;
}

size_t matchClose(const std::string& text, size_t open) {
    const char openChar = text[open];
    const char closeChar = openChar == '(' ? ')' : (openChar == '[' ? ']' : '}');
    int depth = 0;
    for (size_t i = open; i < text.size(); i++) {
        if (text[i] == openChar) {
            depth++;
        } else if (text[i] == closeChar && --depth == 0) {
            return i;
        }
    }
    return npos;
}

// Comments and literal contents become spaces, so offsets and lines still match the text
std::string maskCpp(const std::string& text) {
    enum class State { Code, LineComment, BlockComment, String, Char };

    std::string masked = text;
    State state = State::Code;
    for (size_t i = 0; i < text.size(); i++) {
        const char c = text[i];
        const char next = i + 1 < text.size() ? text[i + 1] : '\0';

        switch (state) {
            case State::Code:
                if (c == '/' && (next == '/' || next == '*')) {
                    state = next == '/' ? State::LineComment : State::BlockComment;
                    masked[i] = masked[i + 1] = ' ';
                    i++;
                } else if (c == '"') {
                    state = State::String;
                } else if (c == '\'' && !(i > 0 && std::isdigit(static_cast<unsigned char>(text[i - 1])))) {
                    // A quote after a digit is a separator: 1'000
                    state = State::Char;
                }
                break;
            case State::LineComment:
                if (c == '\n') {
                    state = State::Code;
                } else {
                    masked[i] = ' ';
                }
                break;
            case State::BlockComment:
                if (c == '*' && next == '/') {
                    masked[i] = masked[i + 1] = ' ';
                    i++;
                    state = State::Code;
                } else if (c != '\n') {
                    masked[i] = ' ';
                }
                break;
            case State::String:
            case State::Char:
                if (c == '\\' && next != '\0' && next != '\n') {
                    masked[i] = masked[i + 1] = ' ';
                    i++;
                } else if (c == (state == State::String ? '"' : '\'') || c == '\n') {
                    state = State::Code;
                } else {
                    masked[i] = ' ';
                }
                break;
        }
    }
    return masked;
}

std::string maskLua(const std::string& text) {
    std::string masked = text;
    const size_t size = text.size();

    auto blank = [&](size_t from, size_t to) {
        for (size_t i = from; i < to && i < size; i++) {
            if (masked[i] != '\n') masked[i] = ' ';
        }
    };
    // Level of a long bracket "[==[" at 'pos', -1 when there is none
    auto longBracket = [&](size_t pos) {
        if (pos >= size || text[pos] != '[') return -1;
        size_t end = pos + 1;
        while (end < size && text[end] == '=') end++;
        return (end < size && text[end] == '[') ? static_cast<int>(end - pos - 1) : -1;
    };
    auto longBracketEnd = [&](size_t from, int level) {
        const std::string close = "]" + std::string(level, '=') + "]";
        size_t end = text.find(close, from);
        return end == npos ? size : end + close.size();
    };

    size_t i = 0;
    while (i < size) {
        const char c = text[i];
        if (c == '-' && i + 1 < size && text[i + 1] == '-') {
            const int level = longBracket(i + 2);
            const size_t end = level >= 0 ? longBracketEnd(i + 4 + level, level) : lineEnd(text, i);
            blank(i, end);
            i = end;
        } else if (c == '[' && longBracket(i) >= 0) {
            const int level = longBracket(i);
            const size_t end = longBracketEnd(i + 2 + level, level);
            blank(i, end);
            i = end;
        } else if (c == '"' || c == '\'') {
            size_t end = i + 1;
            while (end < size && text[end] != c && text[end] != '\n') {
                end += (text[end] == '\\' && end + 1 < size) ? 2 : 1;
            }
            blank(i + 1, end);
            i = (end < size && text[end] == c) ? end + 1 : end;
        } else {
            i++;
        }
    }
    return masked;
}

struct Word {
    std::string text;
    size_t start;
};

// Structure is read on 'masked', the offsets match 'text'
struct Document {
    size_t index = 0;
    const std::string* text = nullptr;
    std::string masked;
    std::vector<Word> words;
    std::vector<int> depth; // blocks open before each offset
};

std::vector<Word> findWords(const std::string& masked) {
    std::vector<Word> words;
    size_t pos = 0;
    while (pos < masked.size()) {
        if (isIdentStart(masked[pos]) && (pos == 0 || !isIdentChar(masked[pos - 1]))) {
            size_t end = pos;
            while (end < masked.size() && isIdentChar(masked[end])) end++;
            words.push_back({masked.substr(pos, end - pos), pos});
            pos = end;
        } else {
            pos++;
        }
    }
    return words;
}

Document cppDocument(const std::string& text, size_t index) {
    Document doc;
    doc.index = index;
    doc.text = &text;
    doc.masked = maskCpp(text);
    doc.words = findWords(doc.masked);
    doc.depth.resize(text.size() + 1);

    int depth = 0;
    for (size_t i = 0; i < text.size(); i++) {
        doc.depth[i] = depth;
        if (doc.masked[i] == '{') {
            depth++;
        } else if (doc.masked[i] == '}' && depth > 0) {
            depth--;
        }
    }
    doc.depth.back() = depth;
    return doc;
}

Document luaDocument(const std::string& text) {
    Document doc;
    doc.text = &text;
    doc.masked = maskLua(text);
    doc.words = findWords(doc.masked);
    doc.depth.resize(text.size() + 1);

    // An "end" is already outside the block it closes
    int depth = 0;
    size_t pos = 0;
    for (const Word& word : doc.words) {
        for (; pos < word.start; pos++) doc.depth[pos] = depth;
        if ((word.text == "end" || word.text == "until") && depth > 0) depth--;
        for (; pos < word.start + word.text.size(); pos++) doc.depth[pos] = depth;
        if (word.text == "function" || word.text == "if" || word.text == "do" || word.text == "repeat") depth++;
    }
    for (; pos <= text.size(); pos++) doc.depth[pos] = depth;
    return doc;
}

// Arguments of the call opening at 'paren', empty when it never closes
std::vector<std::string> callArguments(const Document& doc, size_t paren) {
    std::vector<std::string> args;
    int depth = 0;
    size_t argStart = paren + 1;
    for (size_t i = paren; i < doc.masked.size(); i++) {
        const char c = doc.masked[i];
        if (c == '(' || c == '[' || c == '{') {
            depth++;
        } else if (c == ')' || c == ']' || c == '}') {
            if (--depth == 0) {
                args.push_back(trim(doc.text->substr(argStart, i - argStart)));
                return args;
            }
        } else if (c == ',' && depth == 1) {
            args.push_back(trim(doc.text->substr(argStart, i - argStart)));
            argStart = i + 1;
        }
    }
    return {};
}

struct TextEdit {
    size_t document = 0;
    size_t offset = 0;
    size_t erase = 0;
    std::string text;
};

void applyEdits(std::vector<std::string>& texts, std::vector<TextEdit> edits) {
    // Back to front, so the offsets of the others stay valid
    std::sort(edits.begin(), edits.end(), [](const TextEdit& a, const TextEdit& b) { return a.offset > b.offset; });
    for (const TextEdit& edit : edits) {
        texts[edit.document].replace(edit.offset, edit.erase, edit.text);
    }
}

// Start of the first line of a body
size_t bodyCaret(const std::string& text, size_t open, size_t close) {
    if (lineEnd(text, open) >= close) return open + 1;
    return lineContentStart(text, nextLineStart(text, open));
}

// Adds a statement after the last one of the body starting with 'prefix', else first.
// 'open' is the '{' of a C++ body or the ')' closing the Lua function parameters.
TextEdit bodyInsertion(const Document& doc, size_t open, size_t close, const std::string& prefix, const std::string& statement) {
    const std::string& text = *doc.text;
    const std::string& masked = doc.masked;
    const std::string outerIndent = indentOf(text, open);

    TextEdit edit;
    edit.document = doc.index;

    // A one-line body is spread over lines
    if (lineEnd(masked, open) >= close) {
        const std::string indent = outerIndent + indentUnit;
        const std::string content = trim(text.substr(open + 1, close - open - 1));
        const bool registers = masked.compare(skipSpaces(masked, open + 1), prefix.size(), prefix) == 0;

        edit.offset = open + 1;
        edit.erase = close - open - 1;
        edit.text = "\n";
        if (registers) edit.text += indent + content + "\n";
        edit.text += indent + statement + "\n";
        if (!content.empty() && !registers) edit.text += indent + content + "\n";
        edit.text += outerIndent;
        return edit;
    }

    const int bodyDepth = doc.depth[open + 1];
    size_t lastLine = npos;
    std::optional<std::string> indent;
    for (size_t line = nextLineStart(masked, open); line < close; line = nextLineStart(masked, line)) {
        const size_t first = lineContentStart(masked, line);
        if (first >= close) break;
        if (first == lineEnd(masked, line) || doc.depth[first] != bodyDepth) continue;

        if (!indent) indent = indentOf(text, line);
        if (masked.compare(first, prefix.size(), prefix) == 0 && lineEnd(masked, line) < close) lastLine = line;
    }

    if (lastLine != npos) {
        edit.offset = nextLineStart(masked, lastLine);
        edit.text = indentOf(text, lastLine) + statement + "\n";
    } else if (restOfLineBlank(masked, open + 1)) {
        edit.offset = nextLineStart(masked, open);
        edit.text = indent.value_or(outerIndent + indentUnit) + statement + "\n";
    } else {
        edit.offset = open + 1;
        edit.text = "\n" + indent.value_or(outerIndent + indentUnit) + statement;
    }
    return edit;
}

// Empty body with a line for the caret
std::string handlerBody(const ScriptEvent& event, const std::string& indent, bool lua) {
    std::string body = indent + indentUnit + "\n";
    if (event.returnsBool) body += indent + indentUnit + (lua ? "return true\n" : "return true;\n");
    return body + indent + (lua ? "end" : "}");
}

struct Registration {
    int event = -1;
    std::string method;
    size_t document = 0;
    size_t offset = 0;
};

std::vector<bool> registeredEvents(const std::vector<Registration>& registrations) {
    std::vector<bool> registered(eventList().size(), false);
    for (const Registration& registration : registrations) {
        if (registration.event >= 0) registered[registration.event] = true;
    }
    return registered;
}

// Engine events call the method named like them, the others take a name that is still free
std::string handlerName(const ScriptEvent& event, bool lua, const std::vector<Registration>& registrations,
                        const std::unordered_set<std::string>& members) {
    const std::string eventName = lua ? luaEventName(event) : event.name;
    if (isEngineSource(event.source)) return eventName;

    std::unordered_set<std::string> registered;
    for (const Registration& registration : registrations) {
        registered.insert(registration.method);
    }

    std::string action = eventName;
    if (action.compare(0, 2, "on") == 0 && std::isupper(static_cast<unsigned char>(action[2]))) {
        action.erase(0, 2);
    } else {
        action[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(action[0])));
    }

    // A method named like the event is reused, unless an engine event needs that name
    const std::string name = "on" + action;
    if (!registered.count(name) && findEvent(S::Engine, name, lua) < 0) return name;

    // onSoundPause, onTextEditChange, onAdMobInitialized
    const ScriptEventSourceInfo& source = sourceInfo(event.source);
    const std::string component = source.component;
    const std::string owner = *source.eventClass ? source.eventClass : component.substr(0, component.find("Component"));
    const std::string base = "on" + owner + action;
    std::string unique = base;
    for (int suffix = 2; registered.count(unique) || members.count(unique); suffix++) {
        unique = base + std::to_string(suffix);
    }
    return unique;
}

// --- C++ ---

struct CppClass {
    size_t document = 0;
    size_t keyword = npos;
    size_t open = npos;
    size_t close = npos;
    bool isStruct = false;
};

struct CppFunction {
    std::string name;
    size_t document = 0;
    size_t start = npos;
    size_t open = npos; // npos when only declared
    size_t close = npos;
    bool destructor = false;
    bool inClass = false;
};

struct CppScript {
    std::vector<Document> docs;
    std::string className;
    CppClass cls;
    CppFunction constructor;
    CppFunction destructor;
};

bool findClass(const Document& doc, const std::string& name, CppClass& cls) {
    const std::string& m = doc.masked;
    for (const Word& word : doc.words) {
        if ((word.text != "class" && word.text != "struct") || wordBefore(m, word.start) == "enum") continue;

        // The name is the last word before the bases or the body: "class DORIAX_API Foo final"
        std::string last;
        size_t pos = skipSpaces(m, word.start + word.text.size());
        while (pos < m.size() && isIdentStart(m[pos])) {
            const std::string identifier = wordAt(m, pos);
            if (identifier != "final") last = identifier;
            pos = skipSpaces(m, pos + identifier.size());
        }
        if (last != name || pos >= m.size()) continue;

        if (m[pos] == ':' && m.compare(pos, 2, "::") != 0) pos = m.find_first_of("{;", pos);
        if (pos == npos || m[pos] != '{') continue;

        const size_t close = matchClose(m, pos);
        if (close == npos) continue;

        cls = {doc.index, word.start, pos, close, word.text == "struct"};
        return true;
    }
    return false;
}

// Body of the function whose parameters open at 'paren', left as npos for a declaration
void findFunctionBody(const std::string& m, size_t paren, CppFunction& function) {
    function.open = npos;
    function.close = npos;

    const size_t paramsClose = matchClose(m, paren);
    if (paramsClose == npos) return;

    int parens = 0;
    for (size_t i = paramsClose + 1; i < m.size(); i++) {
        const char c = m[i];
        if (c == '(') {
            parens++;
        } else if (c == ')') {
            parens--;
        } else if (parens == 0 && (c == ';' || c == '}')) {
            return;
        } else if (parens == 0 && c == '{') {
            // "member{value}" of an initializer list
            const std::string word = wordBefore(m, i);
            if (!word.empty() && word != "const" && word != "noexcept" && word != "override" && word != "final") {
                i = matchClose(m, i);
                if (i == npos) return;
                continue;
            }
            function.close = matchClose(m, i);
            if (function.close != npos) function.open = i;
            return;
        }
    }
}

// Definitions like "Foo::name(...)" outside of any block
std::vector<CppFunction> outOfClassFunctions(const Document& doc, const std::string& className) {
    const std::string& m = doc.masked;
    std::vector<CppFunction> functions;
    for (const Word& word : doc.words) {
        if (word.text != className || doc.depth[word.start] != 0) continue;

        size_t pos = skipSpaces(m, word.start + word.text.size());
        if (m.compare(pos, 2, "::") != 0) continue;
        pos = skipSpaces(m, pos + 2);

        CppFunction function;
        function.document = doc.index;
        function.start = word.start;
        function.destructor = m[pos] == '~';
        if (function.destructor) pos = skipSpaces(m, pos + 1);
        function.name = wordAt(m, pos);

        const size_t paren = skipSpaces(m, pos + function.name.size());
        if (function.name.empty() || m[paren] != '(') continue;

        findFunctionBody(m, paren, function);
        functions.push_back(function);
    }
    return functions;
}

// A method declared or defined in the class body
bool findInClassFunction(const Document& doc, const CppClass& cls, const std::string& name, bool destructor, CppFunction& function) {
    const std::string& m = doc.masked;
    const int memberDepth = doc.depth[cls.open + 1];
    for (const Word& word : doc.words) {
        if (word.start <= cls.open || word.start >= cls.close || word.text != name || doc.depth[word.start] != memberDepth) continue;

        const size_t prev = prevNonSpace(m, word.start);
        const size_t paren = skipSpaces(m, word.start + word.text.size());
        if ((prev != npos && m[prev] == '~') != destructor || m[paren] != '(') continue;

        function.name = name;
        function.document = doc.index;
        function.start = word.start;
        function.destructor = destructor;
        function.inClass = true;
        findFunctionBody(m, paren, function);
        return true;
    }
    return false;
}

CppFunction findDefinition(const CppScript& script, const std::string& name) {
    for (const Document& doc : script.docs) {
        for (const CppFunction& function : outOfClassFunctions(doc, script.className)) {
            if (function.name == name && !function.destructor && function.open != npos) return function;
        }
    }
    return {};
}

// Constructor or destructor with a body, out of the class or in it
bool findSpecialFunction(const CppScript& script, bool destructor, CppFunction& function) {
    for (const Document& doc : script.docs) {
        for (const CppFunction& candidate : outOfClassFunctions(doc, script.className)) {
            if (candidate.name == script.className && candidate.destructor == destructor && candidate.open != npos) {
                function = candidate;
                return true;
            }
        }
    }
    return findInClassFunction(script.docs[script.cls.document], script.cls, script.className, destructor, function) &&
           function.open != npos;
}

bool analyzeCpp(const std::vector<std::string>& texts, const std::string& fileStem, CppScript& script, std::string& error) {
    for (size_t i = 0; i < texts.size(); i++) {
        script.docs.push_back(cppDocument(texts[i], i));
    }

    // The class the editor takes as the script, else the one named like the file
    std::vector<std::string> names;
    for (const std::string& text : texts) {
        if (std::optional<std::string> name = ScriptParser::findScriptClassNameFromString(text)) names.push_back(*name);
    }
    names.push_back(fileStem);

    for (const std::string& name : names) {
        for (const Document& doc : script.docs) {
            if (script.className.empty() && findClass(doc, name, script.cls)) script.className = name;
        }
    }
    if (script.className.empty()) {
        error = "No script class found";
        return false;
    }

    if (!findSpecialFunction(script, false, script.constructor)) {
        error = "Could not find the constructor of " + script.className;
        return false;
    }
    findSpecialFunction(script, true, script.destructor);
    return true;
}

// Body of the handler, else its declaration
bool findCppHandler(const CppScript& script, const std::string& method, size_t& document, size_t& offset) {
    CppFunction handler = findDefinition(script, method);
    if (handler.open == npos && !findInClassFunction(script.docs[script.cls.document], script.cls, method, false, handler)) {
        return false;
    }

    document = handler.document;
    offset = handler.open != npos ? bodyCaret(*script.docs[document].text, handler.open, handler.close) : handler.start;
    return true;
}

std::unordered_set<std::string> cppMemberNames(const CppScript& script) {
    std::unordered_set<std::string> names;
    const Document& doc = script.docs[script.cls.document];
    const int memberDepth = doc.depth[script.cls.open + 1];
    for (const Word& word : doc.words) {
        if (word.start > script.cls.open && word.start < script.cls.close && doc.depth[word.start] == memberDepth) {
            names.insert(word.text);
        }
    }
    for (const Document& other : script.docs) {
        for (const CppFunction& function : outOfClassFunctions(other, script.className)) {
            names.insert(function.name);
        }
    }
    return names;
}

std::vector<Registration> cppRegistrations(const CppScript& script) {
    std::vector<Registration> registrations;
    for (const Document& doc : script.docs) {
        for (const Word& word : doc.words) {
            if (word.text.compare(0, 9, "REGISTER_") != 0) continue;

            const size_t paren = skipSpaces(doc.masked, word.start + word.text.size());
            if (doc.masked[paren] != '(') continue;
            const std::vector<std::string> args = callArguments(doc, paren);

            Registration registration;
            registration.document = doc.index;
            registration.offset = word.start;
            if (word.text == "REGISTER_ENGINE_EVENT" && args.size() == 1) {
                registration.event = findEvent(S::Engine, args[0], false);
                registration.method = args[0];
            } else if (word.text == "REGISTER_COMPONENT_EVENT" && args.size() == 3) {
                // doriax::ButtonComponent -> ButtonComponent
                const std::string component = args[0].substr(args[0].rfind(':') + 1);
                for (const ScriptEventSourceInfo& source : sourceList()) {
                    if (component == source.component) registration.event = findEvent(source.source, args[1], false);
                }
                registration.method = args[2];
            } else if (word.text == "REGISTER_EVENT" && args.size() == 2) {
                registration.event = findClassEvent(args[0], false);
                if (registration.event < 0) registration.event = findEventByName(lastIdentifier(args[0]), false);
                registration.method = args[1];
            } else if (args.size() == 2) {
                for (const ScriptEventSourceInfo& source : sourceList()) {
                    if (word.text == source.registerMacro) registration.event = findEvent(source.source, args[0], false);
                }
                registration.method = args[1];
            } else {
                continue;
            }
            registrations.push_back(registration);
        }
    }
    return registrations;
}

bool usesDoriaxNamespace(const Document& doc) {
    const std::vector<Word>& words = doc.words;
    for (size_t i = 0; i + 2 < words.size(); i++) {
        if (words[i].text == "using" && words[i + 1].text == "namespace" && words[i + 2].text == "doriax" &&
            doc.masked[skipSpaces(doc.masked, words[i + 2].start + 6)] == ';') {
            return true;
        }
    }
    return false;
}

// File name of an include line, without its directory
std::string includedFile(const std::string& line) {
    size_t pos = skipSpaces(line, line.find('#') + 1);
    if (line.compare(pos, 7, "include") != 0) return "";
    pos = skipSpaces(line, pos + 7);
    if (pos >= line.size() || (line[pos] != '"' && line[pos] != '<')) return "";

    const size_t end = line.find(line[pos] == '"' ? '"' : '>', pos + 1);
    if (end == npos) return "";
    const std::string path = line.substr(pos + 1, end - pos - 1);
    return path.substr(path.find_last_of("/\\") + 1);
}

// Offset of the line after the last include, npos when the header is already included
size_t includeOffset(const Document& doc, const std::string& header) {
    const std::string& text = *doc.text;
    size_t offset = 0;
    for (size_t line = 0; line < text.size(); line = nextLineStart(text, line)) {
        if (doc.masked[lineContentStart(doc.masked, line)] != '#') continue;

        const std::string file = includedFile(text.substr(line, lineEnd(text, line) - line));
        if (file == header) return npos;
        if (!file.empty()) offset = nextLineStart(text, line);
    }
    return offset;
}

std::string cppSignature(const ScriptEvent& event, const std::string& name, bool qualify) {
    std::string params;
    for (const ScriptEventParam& param : event.params) {
        if (!params.empty()) params += ", ";
        // Engine types are the capitalized ones
        if (qualify && std::isupper(static_cast<unsigned char>(param.type[0]))) params += "doriax::";
        params += std::string(param.type) + " " + param.name;
    }
    return std::string(event.returnsBool ? "bool " : "void ") + name + "(" + params + ")";
}

std::string cppRegistration(const ScriptEvent& event, const std::string& method, bool qualify, bool unregister) {
    const ScriptEventSourceInfo& source = sourceInfo(event.source);
    const std::string macro = (unregister ? "UN" : "") + std::string(source.registerMacro);
    const std::string ns = qualify ? "doriax::" : "";

    if (isEngineSource(event.source)) {
        return macro + "(" + event.name + ");";
    }
    if (isPhysicsSource(event.source)) {
        return macro + "(getScene()->getSystem<" + ns + "PhysicsSystem>()->" + event.name + ", " + method + ");";
    }
    if (isClassSource(event.source)) {
        return macro + "(" + ns + source.eventClass + "::" + event.name + ", " + method + ");";
    }
    if (std::strcmp(source.registerMacro, "REGISTER_COMPONENT_EVENT") == 0) {
        return macro + "(" + ns + source.component + ", " + event.name + ", " + method + ");";
    }
    return macro + "(" + event.name + ", " + method + ");";
}

// After the last member of the last public section
TextEdit memberInsertion(const Document& doc, const CppClass& cls, const ScriptEvent& event, const std::string& signature, bool define) {
    const std::string& text = *doc.text;
    const std::string& m = doc.masked;
    const int memberDepth = doc.depth[cls.open + 1];

    struct Section {
        size_t start;
        size_t end;
        bool isPublic;
    };
    std::vector<Section> sections = {{cls.open + 1, cls.close, cls.isStruct}};
    for (const Word& word : doc.words) {
        if (word.start <= cls.open || word.start >= cls.close || doc.depth[word.start] != memberDepth) continue;
        if (word.text != "public" && word.text != "protected" && word.text != "private") continue;

        const size_t colon = skipSpaces(m, word.start + word.text.size());
        if (m[colon] != ':' || m.compare(colon, 2, "::") == 0) continue;

        sections.back().end = std::max(sections.back().start, lineStart(m, word.start));
        sections.push_back({colon + 1, cls.close, word.text == "public"});
    }

    Section section = sections.back();
    for (const Section& candidate : sections) {
        if (candidate.isPublic) section = candidate;
    }

    const std::string classIndent = indentOf(text, cls.keyword);
    std::string indent = classIndent + indentUnit;
    bool blankLine = false;

    TextEdit edit;
    edit.document = doc.index;

    const size_t last = prevNonSpace(m, section.end);
    if (last == npos || last < section.start) {
        edit.offset = nextLineStart(m, section.start - 1);
    } else {
        const size_t lastLine = lineStart(m, last);
        const std::string line = trim(m.substr(lastLine, lineEnd(m, last) - lastLine));
        const bool afterDeclaration = line.size() > 2 && line.compare(line.size() - 2, 2, ");") == 0 && line.find('=') == npos;

        edit.offset = lineEnd(m, last) + 1;
        indent = indentOf(text, lastLine);
        blankLine = define || !afterDeclaration;
    }

    const std::string member = indent + signature + (define ? " {\n" + handlerBody(event, indent, false) : ";");
    if (edit.offset > section.end) {
        // The class closes on the same line
        edit.offset = section.end;
        edit.text = "\n" + member + "\n" + classIndent;
    } else {
        edit.text = (blankLine ? "\n" : "") + member + "\n";
    }
    return edit;
}

// After the last method defined next to the constructor
TextEdit definitionInsertion(const CppScript& script, const ScriptEvent& event, const std::string& method) {
    const Document& doc = script.docs[script.constructor.document];
    size_t lastClose = script.constructor.close;
    for (const CppFunction& function : outOfClassFunctions(doc, script.className)) {
        if (function.close != npos) lastClose = std::max(lastClose, function.close);
    }

    TextEdit edit;
    edit.document = doc.index;
    edit.offset = lineEnd(*doc.text, lastClose);
    edit.text = "\n\n" + cppSignature(event, script.className + "::" + method, !usesDoriaxNamespace(doc)) + " {\n" +
                handlerBody(event, "", false);
    return edit;
}

// --- Lua ---

struct LuaMethod {
    std::string name;
    size_t keyword = npos; // "function"
    size_t paramsClose = npos;
    size_t end = npos;
};

struct LuaScript {
    Document doc;
    std::string table;
    size_t moduleReturn = npos;
    std::vector<LuaMethod> methods;
};

size_t luaBlockEnd(const Document& doc, size_t keyword) {
    for (const Word& word : doc.words) {
        if (word.start > keyword && word.text == "end" && doc.depth[word.start] == doc.depth[keyword]) return word.start;
    }
    return npos;
}

// function T:name(), function T.name(), T.name = function() and the functions of its table
std::vector<LuaMethod> findLuaMethods(const Document& doc, const std::string& table, size_t tableOpen, size_t tableClose) {
    const std::string& m = doc.masked;
    std::vector<LuaMethod> methods;
    for (const Word& word : doc.words) {
        if (word.text != "function") continue;

        LuaMethod method;
        method.keyword = word.start;
        size_t pos = skipSpaces(m, word.start + word.text.size());
        if (wordAt(m, pos) == table) {
            pos = skipSpaces(m, pos + table.size());
            if (m[pos] != ':' && m[pos] != '.') continue;
            pos = skipSpaces(m, pos + 1);
            method.name = wordAt(m, pos);
            pos = skipSpaces(m, pos + method.name.size());
        } else {
            const size_t assign = prevNonSpace(m, word.start);
            if (assign == npos || m[assign] != '=') continue;
            method.name = wordBefore(m, assign);

            const size_t nameStart = prevNonSpace(m, assign) + 1 - method.name.size();
            const size_t dot = prevNonSpace(m, nameStart);
            const bool field = nameStart > tableOpen && nameStart < tableClose;
            if (!field && !(dot != npos && m[dot] == '.' && wordBefore(m, dot) == table)) continue;
        }
        if (method.name.empty() || m[pos] != '(') continue;

        method.paramsClose = matchClose(m, pos);
        method.end = luaBlockEnd(doc, word.start);
        if (method.paramsClose != npos && method.end != npos) methods.push_back(method);
    }
    return methods;
}

bool analyzeLua(const std::string& text, LuaScript& script, std::string& error) {
    script.doc = luaDocument(text);
    const Document& doc = script.doc;
    const std::string& m = doc.masked;

    // The module ends returning its table: "return Player"
    for (const Word& word : doc.words) {
        if (word.text != "return" || doc.depth[word.start] != 0) continue;
        const size_t pos = skipSpaces(m, word.start + word.text.size());
        const std::string name = isIdentStart(m[pos]) ? wordAt(m, pos) : "";
        const size_t after = skipSpaces(m, pos + name.size());
        if (!name.empty() && (after == m.size() || m[after] == ';')) {
            script.table = name;
            script.moduleReturn = word.start;
        }
    }
    if (script.table.empty()) {
        error = "The script does not end with \"return Name\"";
        return false;
    }

    size_t tableOpen = npos;
    size_t tableClose = npos;
    for (const Word& word : doc.words) {
        const size_t assign = skipSpaces(m, word.start + word.text.size());
        if (word.text != script.table || doc.depth[word.start] != 0 || m[assign] != '=' || m[assign + 1] == '=') continue;
        const size_t brace = skipSpaces(m, assign + 1);
        if (m[brace] == '{') {
            tableOpen = brace;
            tableClose = matchClose(m, brace);
            break;
        }
    }

    script.methods = findLuaMethods(doc, script.table, tableOpen, tableClose);
    return true;
}

const LuaMethod* findLuaMethod(const LuaScript& script, const std::string& name) {
    for (const LuaMethod& method : script.methods) {
        if (method.name == name) return &method;
    }
    return nullptr;
}

std::vector<Registration> luaRegistrations(const LuaScript& script) {
    const Document& doc = script.doc;
    std::vector<Registration> registrations;
    for (const Word& word : doc.words) {
        if (word.text != "RegisterEngineEvent" && word.text != "RegisterEvent") continue;

        const size_t paren = skipSpaces(doc.masked, word.start + word.text.size());
        if (doc.masked[paren] != '(') continue;
        const std::vector<std::string> args = callArguments(doc, paren);

        Registration registration;
        registration.offset = word.start;
        if (word.text == "RegisterEngineEvent" && args.size() >= 2) {
            registration.method = unquote(args[1]);
            registration.event = findEvent(S::Engine, registration.method, true);
        } else if (word.text == "RegisterEvent" && args.size() >= 3) {
            // The getter tells the component: Button(...):getButtonComponent().onPress
            const std::string field = lastIdentifier(args[1]);
            registration.method = unquote(args[2]);
            for (const ScriptEventSourceInfo& source : sourceList()) {
                if (*source.component && args[1].find(std::string("get") + source.component) != npos) {
                    registration.event = findEvent(source.source, field, true);
                }
            }
            if (registration.event < 0) registration.event = findClassEvent(args[1], true);
            if (registration.event < 0) registration.event = findEventByName(field, true);
        } else {
            continue;
        }
        registrations.push_back(registration);
    }
    return registrations;
}

std::string luaParameters(const ScriptEvent& event) {
    std::string params;
    for (const ScriptEventParam& param : event.params) {
        if (!params.empty()) params += ", ";
        params += param.name;
    }
    return params;
}

std::string luaRegistration(const ScriptEvent& event, const std::string& method) {
    const ScriptEventSourceInfo& source = sourceInfo(event.source);
    const std::string field = luaEventName(event);

    if (isEngineSource(event.source)) {
        return "RegisterEngineEvent(self, \"" + field + "\")";
    }
    if (isPhysicsSource(event.source)) {
        return "RegisterEvent(self, self.scene:getPhysicsSystem()." + field + ", \"" + method + "\")";
    }
    if (isClassSource(event.source)) {
        return std::string("RegisterEvent(self, ") + source.eventClass + "." + field + ", \"" + method + "\")";
    }
    return std::string("RegisterEvent(self, ") + source.luaObject + "(self.scene, self.entity):get" + source.component +
           "()." + field + ", \"" + method + "\")";
}

} // namespace

const std::vector<ScriptEvent>& ScriptEvents::getEvents() {
    return eventList();
}

const std::vector<ScriptEventSourceInfo>& ScriptEvents::getSources() {
    return sourceList();
}

std::string ScriptEvents::getParameterNames(const ScriptEvent& event) {
    return event.params.empty() ? "" : "(" + luaParameters(event) + ")";
}

std::string ScriptEvents::getLuaName(const ScriptEvent& event) {
    return luaEventName(event);
}

std::string ScriptEvents::getLuaRegistration(const ScriptEvent& event, const std::string& method) {
    return luaRegistration(event, method);
}

ScriptEventScan ScriptEvents::scanLua(const std::string& text) {
    ScriptEventScan scan;
    LuaScript script;
    if (analyzeLua(text, script, scan.error)) {
        scan.registered = registeredEvents(luaRegistrations(script));
    }
    return scan;
}

ScriptEventChange ScriptEvents::addLua(const std::string& text, size_t eventIndex) {
    ScriptEventChange change;
    change.texts = {text};

    LuaScript script;
    if (!analyzeLua(text, script, change.error)) return change;

    const std::vector<Registration> registrations = luaRegistrations(script);
    for (const Registration& registration : registrations) {
        if (registration.event != static_cast<int>(eventIndex)) continue;

        // Already registered, the caret goes to its handler
        const LuaMethod* handler = findLuaMethod(script, registration.method);
        change.cursorOffset = handler ? bodyCaret(text, handler->paramsClose, handler->end) : registration.offset;
        return change;
    }

    std::unordered_set<std::string> methods;
    for (const LuaMethod& method : script.methods) {
        methods.insert(method.name);
    }

    const ScriptEvent& event = eventList()[eventIndex];
    const std::string method = handlerName(event, true, registrations, methods);
    const std::string statement = luaRegistration(event, method);

    std::vector<TextEdit> edits;
    std::string functions;
    if (const LuaMethod* init = findLuaMethod(script, "init")) {
        edits.push_back(bodyInsertion(script.doc, init->paramsClose, init->end, "Register", statement));
    } else {
        functions += "function " + script.table + ":init()\n" + indentUnit + statement + "\nend\n\n";
    }
    if (!methods.count(method)) {
        functions += "function " + script.table + ":" + method + "(" + luaParameters(event) + ")\n" +
                     handlerBody(event, "", true) + "\n\n";
    }
    if (!functions.empty()) {
        // Before "return Name"
        TextEdit edit;
        edit.offset = script.moduleReturn;
        const bool afterBlankLine = edit.offset == 0 || restOfLineBlank(text, lineStart(text, edit.offset - 1));
        edit.text = (afterBlankLine ? "" : "\n") + functions;
        edits.push_back(edit);
    }
    applyEdits(change.texts, edits);

    LuaScript changed;
    std::string error;
    if (analyzeLua(change.texts[0], changed, error)) {
        if (const LuaMethod* handler = findLuaMethod(changed, method)) {
            change.cursorOffset = bodyCaret(change.texts[0], handler->paramsClose, handler->end);
        }
    }
    return change;
}

ScriptEventScan ScriptEvents::scanCpp(const std::vector<std::string>& documents, const std::string& fileStem) {
    ScriptEventScan scan;
    CppScript script;
    if (analyzeCpp(documents, fileStem, script, scan.error)) {
        scan.registered = registeredEvents(cppRegistrations(script));
    }
    return scan;
}

ScriptEventChange ScriptEvents::addCpp(const std::vector<std::string>& documents, const std::string& fileStem, size_t eventIndex) {
    ScriptEventChange change;
    change.texts = documents;

    CppScript script;
    if (!analyzeCpp(documents, fileStem, script, change.error)) return change;

    const std::vector<Registration> registrations = cppRegistrations(script);
    for (const Registration& registration : registrations) {
        if (registration.event != static_cast<int>(eventIndex)) continue;

        // Already registered, the caret goes to its handler
        change.cursorDocument = registration.document;
        change.cursorOffset = registration.offset;
        findCppHandler(script, registration.method, change.cursorDocument, change.cursorOffset);
        return change;
    }

    const ScriptEvent& event = eventList()[eventIndex];
    const ScriptEventSourceInfo& source = sourceInfo(event.source);
    const std::string method = handlerName(event, false, registrations, cppMemberNames(script));
    const Document& classDoc = script.docs[script.cls.document];
    std::vector<TextEdit> edits;

    // Physics, AdMob and purchase types are used in the declaration, the other headers only by the macros
    size_t includeAt = includeOffset(classDoc, source.include);
    for (const Document& doc : script.docs) {
        if (!isPhysicsSource(event.source) && !isClassSource(event.source) && includeOffset(doc, source.include) == npos) includeAt = npos;
    }
    if (includeAt != npos) {
        edits.push_back({classDoc.index, includeAt, 0, "#include \"" + std::string(source.include) + "\"\n"});
    }

    const Document& constructorDoc = script.docs[script.constructor.document];
    edits.push_back(bodyInsertion(constructorDoc, script.constructor.open, script.constructor.close, "REGISTER_",
                                  cppRegistration(event, method, !usesDoriaxNamespace(constructorDoc), false)));

    if (script.destructor.open != npos) {
        const Document& destructorDoc = script.docs[script.destructor.document];
        edits.push_back(bodyInsertion(destructorDoc, script.destructor.open, script.destructor.close, "UNREGISTER_",
                                      cppRegistration(event, method, !usesDoriaxNamespace(destructorDoc), true)));
    }

    // An existing method named like the handler is reused
    CppFunction declaration;
    const bool declared = findInClassFunction(classDoc, script.cls, method, false, declaration);
    const bool defined = declaration.open != npos || findDefinition(script, method).open != npos;
    const std::string signature = cppSignature(event, method, !usesDoriaxNamespace(classDoc));

    if (script.constructor.inClass) {
        // Header only script, the handler goes in the class like the constructor
        if (!declared && !defined) edits.push_back(memberInsertion(classDoc, script.cls, event, signature, true));
    } else {
        if (!declared) edits.push_back(memberInsertion(classDoc, script.cls, event, signature, false));
        if (!defined) edits.push_back(definitionInsertion(script, event, method));
    }
    applyEdits(change.texts, edits);

    CppScript changed;
    std::string error;
    if (analyzeCpp(change.texts, fileStem, changed, error)) {
        findCppHandler(changed, method, change.cursorDocument, change.cursorOffset);
    }
    return change;
}

#include "AppStateManager.h"
#include "Logger.h"

void AppStateManager::begin(AppState initial) {
    current_state = initial;
    Logger::info("State", String("Initial: ") + toString(current_state));
}

void AppStateManager::setState(AppState next) {
    if (next == current_state) return;
    Logger::info("State", String(toString(current_state)) + " -> " + toString(next));
    current_state = next;
}

AppState AppStateManager::getState() const { return current_state; }
const char *AppStateManager::stateName() const { return toString(current_state); }

const char *AppStateManager::toString(AppState state) {
    switch (state) {
        case AppState::BOOTING: return "BOOTING";
        case AppState::WIFI_CONNECTING: return "WIFI_CONNECTING";
        case AppState::AP_MODE: return "AP_MODE";
        case AppState::MAIN: return "MAIN";
        case AppState::FETCHING: return "FETCHING";
        case AppState::ENTER_DATE: return "ENTER_DATE";
        case AppState::ENTER_QTY: return "ENTER_QTY";
        case AppState::SAVING: return "SAVING";
        case AppState::PRINTING: return "PRINTING";
        case AppState::SUCCESS: return "SUCCESS";
        case AppState::ERROR: return "ERROR";
        case AppState::RETRIEVE: return "RETRIEVE";
    }
    return "UNKNOWN";
}

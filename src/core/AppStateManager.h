#pragma once

#include <Arduino.h>

enum class AppState {
    BOOTING,
    WIFI_CONNECTING,
    AP_MODE,
    MAIN,
    FETCHING,
    ENTER_DATE,
    ENTER_QTY,
    SAVING,
    PRINTING,
    SUCCESS,
    ERROR,
    RETRIEVE
};

class AppStateManager {
public:
    void begin(AppState initial = AppState::BOOTING);
    void setState(AppState next);
    AppState getState() const;
    const char *stateName() const;
    static const char *toString(AppState state);

private:
    AppState current_state = AppState::BOOTING;
};

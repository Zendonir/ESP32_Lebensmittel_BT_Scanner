#pragma once

#include "EscPosPrinter.h"
#include "../models/InventoryItem.h"
#include "config.h"

class LabelRenderer {
public:
    explicit LabelRenderer(EscPosPrinter &printer);
    void setPreFeed(uint8_t lines)  { preFeedLines  = lines; }
    void setPostFeed(uint8_t lines) { postFeedLines = lines; }
    bool printInventoryLabel(const InventoryItem &item);

private:
    EscPosPrinter &printer;
    uint8_t preFeedLines  = LABEL_PRE_FEED;
    uint8_t postFeedLines = 4;
};

#pragma once

#include <Arduino.h>

struct ProductTemplate {
    String id;          // unique id (timestamp-based)
    String name;        // product display name
    String category;    // category for grouping
    int    shelfDays;   // MHD = today + shelfDays
};

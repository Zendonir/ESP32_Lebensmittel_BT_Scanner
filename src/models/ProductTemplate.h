#pragma once

#include <Arduino.h>
#include <vector>

struct ProductTemplate {
    String id;          // unique id (timestamp-based)
    String name;        // product display name
    String category;    // category for grouping
    int    shelfDays;   // MHD = today + shelfDays
    std::vector<String> brands;  // one or more brands for this product
    String unit;        // amount unit on label: "St.", "g", "ml", "kg", "" = no amount step
};

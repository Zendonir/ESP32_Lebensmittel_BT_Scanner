#pragma once

#include <Arduino.h>
#include <vector>

struct ProductInfo {
    String barcode;
    String name;
    String brand;
    String quantity;   // e.g. "500g"
    String category;   // first category, stripped of language prefix
    String nutriscore;
    std::vector<String> labels;
};

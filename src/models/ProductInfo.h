#pragma once

#include <Arduino.h>
#include <vector>

struct ProductInfo {
    String barcode;
    String name;
    String brand;
    String nutriscore;
    std::vector<String> labels;
};

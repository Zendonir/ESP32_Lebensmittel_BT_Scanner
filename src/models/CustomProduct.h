#pragma once

#include <Arduino.h>

struct CustomProduct {
    String name;
    String brand;
    String barcode;
    String category;
    String description;
    int defaultDays = 0;
    bool askQty = true;
};

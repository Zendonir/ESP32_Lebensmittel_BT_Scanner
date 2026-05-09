#pragma once

#include <Arduino.h>

struct ShoppingItem {
    String name;
    String category;
    String addedDate;
    bool bought = false;
};

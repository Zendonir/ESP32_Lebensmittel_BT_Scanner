#pragma once

#include <Arduino.h>

struct InventoryItem {
    String barcode;
    String name;
    String brand;
    String category;
    String expiryDate;
    String addedDate;
    int quantity = 0;
    String labelBarcode;
    String location;   // storage location when item was added
    String unit;       // amount unit: "St.", "g", "ml", "kg", "" = plain pieces
};

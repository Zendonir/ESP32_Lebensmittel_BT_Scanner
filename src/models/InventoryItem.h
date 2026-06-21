#pragma once

#include <Arduino.h>

struct InventoryItem {
    String barcode;
    String name;
    String brand;
    String category;     // Hauptkategorie (z.B. "Getränke")
    String subcategory;  // Unterkategorie / Sorte (z.B. "Apfel"), leer wenn keine
    String expiryDate;
    String addedDate;
    int quantity = 0;
    String labelBarcode;
    String location;   // storage location when item was added
    String unit;       // amount unit: "St.", "g", "ml", "kg", "" = plain pieces
};

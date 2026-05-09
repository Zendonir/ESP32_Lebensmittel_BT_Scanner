#pragma once

#include <Arduino.h>
#include "ApiClient.h"
#include "../models/ProductInfo.h"
#include "../storage/LittleFSManager.h"

class OpenFoodFacts {
public:
    explicit OpenFoodFacts(ApiClient &client, LittleFSManager *cacheFs = nullptr);
    bool fetchProduct(const String &barcode, ProductInfo &product);

private:
    ApiClient &api;
    LittleFSManager *filesystem;

    bool loadCachedProduct(const String &barcode, ProductInfo &product);
    void cacheProduct(const ProductInfo &product);
};

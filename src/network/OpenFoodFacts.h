#pragma once

#include <Arduino.h>
#include "ApiClient.h"
#include "../models/ProductInfo.h"
#include "../storage/LittleFSManager.h"
#include "SyncManager.h"

class OpenFoodFacts {
public:
    explicit OpenFoodFacts(ApiClient &client, LittleFSManager *cacheFs = nullptr);

    // Call before first fetchProduct() so MySQL lookup/push can be used.
    void setSyncManager(SyncManager *sync) { _sync = sync; }

    bool fetchProduct(const String &barcode, ProductInfo &product);

private:
    ApiClient       &api;
    LittleFSManager *filesystem;
    SyncManager     *_sync = nullptr;

    bool loadCachedProduct(const String &barcode, ProductInfo &product);
    void cacheProduct(const ProductInfo &product);
};

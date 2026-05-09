#pragma once

#include <Arduino.h>
#include "ApiClient.h"
#include "../models/ProductInfo.h"

class OpenFoodFacts {
public:
    explicit OpenFoodFacts(ApiClient &client);
    bool fetchProduct(const String &barcode, ProductInfo &product);

private:
    ApiClient &api;
};

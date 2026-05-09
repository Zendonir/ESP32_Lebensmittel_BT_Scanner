#pragma once

#include <Arduino.h>

struct StorageStat {
    String name;
    String category;
    String addedDate;
    String removedDate;
    int storageDays = 0;
};

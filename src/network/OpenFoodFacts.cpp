#include "OpenFoodFacts.h"
#include "../core/Logger.h"
#include <ArduinoJson.h>

// OpenFoodFacts categories_tags → German category mapping
// Priority index: 0 = highest (Tiefkühlprodukte wins over everything)
struct OffCatEntry { const char *tag; uint8_t prio; };
static const OffCatEntry kOffCatMap[] = {
    // Tiefkühlprodukte (0) — checked first: frozen always wins
    {"frozen-foods", 0}, {"frozen-products", 0}, {"frozen", 0},
    {"frozen-vegetables", 0}, {"frozen-fruits", 0}, {"frozen-meat", 0},
    {"frozen-fish", 0}, {"frozen-seafood", 0}, {"frozen-poultry", 0},
    {"frozen-ready-meals", 0}, {"frozen-prepared-meals", 0}, {"frozen-meals", 0},
    {"frozen-pizzas", 0}, {"frozen-pizza", 0},
    {"frozen-potato-products", 0}, {"french-fries", 0}, {"fries", 0},
    {"frozen-fries", 0}, {"croquettes", 0}, {"frozen-croquettes", 0},
    {"frozen-bakery-products", 0}, {"frozen-bread", 0}, {"frozen-rolls", 0},
    {"frozen-pastries", 0}, {"frozen-desserts", 0},
    {"ice-creams", 0}, {"ice-cream", 0}, {"sorbets", 0}, {"frozen-yogurts", 0},
    {"frozen-snacks", 0}, {"frozen-burgers", 0}, {"frozen-nuggets", 0},
    {"frozen-chicken-nuggets", 0}, {"frozen-fish-fingers", 0}, {"frozen-soups", 0},
    // Geflügel (1)
    {"poultry", 1}, {"poultry-meat", 1}, {"chicken", 1}, {"chickens", 1},
    {"turkey", 1}, {"turkeys", 1}, {"duck", 1}, {"ducks", 1},
    {"goose", 1}, {"geese", 1}, {"quail", 1},
    {"chicken-meat", 1}, {"turkey-meat", 1}, {"duck-meat", 1}, {"goose-meat", 1},
    {"chicken-breasts", 1}, {"chicken-breast", 1}, {"chicken-fillets", 1},
    {"chicken-thighs", 1}, {"chicken-legs", 1}, {"chicken-wings", 1},
    {"chicken-drumsticks", 1}, {"chicken-nuggets", 1}, {"chicken-strips", 1},
    {"chicken-skewers", 1}, {"chicken-burgers", 1}, {"chicken-patties", 1},
    {"turkey-breast", 1}, {"turkey-fillets", 1}, {"turkey-slices", 1},
    {"turkey-ham", 1}, {"poultry-sausages", 1}, {"poultry-cold-cuts", 1},
    {"roast-chicken", 1}, {"grilled-chicken", 1}, {"cooked-chicken", 1},
    {"smoked-chicken", 1}, {"canned-chicken", 1},
    // Fisch (2)
    {"fish", 2}, {"fishes", 2}, {"seafood", 2}, {"seafoods", 2},
    {"fish-and-seafood", 2}, {"fresh-fish", 2}, {"smoked-fish", 2},
    {"cooked-fish", 2}, {"prepared-fish", 2}, {"canned-fish", 2},
    {"fish-fillets", 2}, {"fish-fingers", 2}, {"fish-sticks", 2},
    {"fish-cakes", 2}, {"fish-spreads", 2},
    {"salmon", 2}, {"smoked-salmon", 2}, {"tuna", 2}, {"canned-tuna", 2},
    {"cod", 2}, {"haddock", 2}, {"pollock", 2}, {"trout", 2},
    {"herring", 2}, {"matjes", 2}, {"mackerel", 2}, {"sardines", 2},
    {"anchovies", 2}, {"carp", 2}, {"seabass", 2}, {"sea-bream", 2},
    {"tilapia", 2}, {"pangasius", 2},
    {"shrimp", 2}, {"shrimps", 2}, {"prawn", 2}, {"prawns", 2},
    {"crustaceans", 2}, {"shellfish", 2}, {"mussels", 2}, {"clams", 2},
    {"scallops", 2}, {"oysters", 2}, {"crab", 2}, {"crabs", 2},
    {"lobster", 2}, {"squid", 2}, {"octopus", 2}, {"calamari", 2},
    {"surimi", 2}, {"caviar", 2},
    // Fleisch & Wurst (3)
    {"meats", 3}, {"meat", 3}, {"beef", 3}, {"pork", 3}, {"veal", 3},
    {"lamb", 3}, {"mutton", 3}, {"goat-meat", 3}, {"horse-meat", 3},
    {"rabbit-meat", 3}, {"minced-meat", 3}, {"ground-meat", 3},
    {"minced-beef", 3}, {"minced-pork", 3},
    {"meat-preparations", 3}, {"prepared-meats", 3}, {"cooked-meats", 3},
    {"cured-meats", 3}, {"smoked-meats", 3}, {"cold-cuts", 3},
    {"charcuterie", 3}, {"sausages", 3}, {"pork-sausages", 3},
    {"beef-sausages", 3}, {"bratwurst", 3}, {"salami", 3}, {"chorizo", 3},
    {"ham", 3}, {"hams", 3}, {"cooked-ham", 3}, {"raw-ham", 3},
    {"bacon", 3}, {"pancetta", 3}, {"meatballs", 3},
    {"burger-patties", 3}, {"patties", 3}, {"burgers", 3}, {"meat-burgers", 3},
    {"kebab", 3}, {"meat-skewers", 3}, {"pate", 3}, {"pates", 3},
    {"terrines", 3}, {"offals", 3}, {"liver", 3},
    {"meat-spreads", 3}, {"canned-meats", 3}, {"jerky", 3}, {"dried-meat", 3},
    // Gemüse (4)
    {"vegetables", 4}, {"vegetable", 4}, {"fresh-vegetables", 4},
    {"canned-vegetables", 4}, {"preserved-vegetables", 4},
    {"pickled-vegetables", 4}, {"fermented-vegetables", 4},
    {"vegetable-mixes", 4}, {"vegetable-medleys", 4},
    {"salads", 4}, {"lettuces", 4}, {"lettuce", 4},
    {"iceberg-lettuce", 4}, {"romaine-lettuce", 4}, {"lambs-lettuce", 4},
    {"rocket", 4}, {"arugula", 4}, {"spinach", 4}, {"kale", 4},
    {"cabbages", 4}, {"cabbage", 4}, {"red-cabbage", 4}, {"white-cabbage", 4},
    {"sauerkraut", 4}, {"broccoli", 4}, {"cauliflower", 4}, {"brussels-sprouts", 4},
    {"carrots", 4}, {"carrot", 4}, {"potatoes", 4}, {"potato", 4},
    {"sweet-potatoes", 4}, {"onions", 4}, {"onion", 4}, {"garlic", 4},
    {"leeks", 4}, {"scallions", 4}, {"spring-onions", 4},
    {"tomatoes", 4}, {"tomato", 4}, {"cherry-tomatoes", 4},
    {"cucumbers", 4}, {"cucumber", 4}, {"pickles", 4}, {"gherkin", 4}, {"gherkins", 4},
    {"peppers", 4}, {"bell-peppers", 4}, {"chili-peppers", 4},
    {"courgettes", 4}, {"zucchini", 4}, {"eggplants", 4}, {"aubergines", 4},
    {"mushrooms", 4}, {"champignons", 4},
    {"legumes", 4}, {"pulses", 4}, {"beans", 4}, {"kidney-beans", 4},
    {"white-beans", 4}, {"black-beans", 4}, {"green-beans", 4},
    {"peas", 4}, {"chickpeas", 4}, {"lentils", 4},
    {"corn", 4}, {"sweet-corn", 4}, {"asparagus", 4}, {"celery", 4},
    {"beetroot", 4}, {"radishes", 4}, {"pumpkin", 4}, {"squash", 4},
    // Snacks & Süßwaren (5)
    {"snacks", 5}, {"sweet-snacks", 5}, {"salty-snacks", 5},
    {"appetizers", 5}, {"aperitif", 5},
    {"crisps", 5}, {"chips", 5}, {"potato-crisps", 5}, {"potato-chips", 5},
    {"tortilla-chips", 5}, {"corn-chips", 5}, {"popcorn", 5},
    {"pretzels", 5}, {"crackers", 5}, {"rice-crackers", 5},
    {"nuts", 5}, {"peanuts", 5}, {"almonds", 5}, {"cashews", 5},
    {"hazelnuts", 5}, {"walnuts", 5}, {"pistachios", 5}, {"mixed-nuts", 5},
    {"seeds", 5}, {"sunflower-seeds", 5}, {"pumpkin-seeds", 5}, {"trail-mixes", 5},
    {"bars", 5}, {"cereal-bars", 5}, {"protein-bars", 5},
    {"energy-bars", 5}, {"granola-bars", 5},
    {"chocolates", 5}, {"chocolate", 5}, {"dark-chocolates", 5},
    {"milk-chocolates", 5}, {"white-chocolates", 5}, {"chocolate-bars", 5},
    {"filled-chocolates", 5}, {"pralines", 5},
    {"candies", 5}, {"sweets", 5}, {"confectioneries", 5},
    {"gummies", 5}, {"jellies", 5}, {"fruit-gummies", 5}, {"marshmallows", 5},
    {"caramels", 5}, {"toffees", 5}, {"lollipops", 5},
    {"chewing-gum", 5}, {"liquorice", 5}, {"nougat", 5}, {"marzipan", 5},
    {"biscuits", 5}, {"cookies", 5}, {"butter-biscuits", 5},
    {"chocolate-biscuits", 5}, {"wafers", 5},
    {"cakes", 5}, {"cake", 5}, {"muffins", 5}, {"brownies", 5},
    {"donuts", 5}, {"doughnuts", 5}, {"croissants", 5},
    {"pain-au-chocolat", 5}, {"pastries", 5}, {"sweet-pastries", 5},
    {"sweet-bakery-products", 5}, {"waffles", 5}, {"pancakes", 5},
    {"crepes", 5}, {"tarts", 5}, {"pies", 5},
    {"desserts", 5}, {"puddings", 5}, {"custards", 5},
    {"mousses", 5}, {"rice-puddings", 5},
    // Brot & Backwaren (6)
    {"bakery-products", 6}, {"breads", 6}, {"bread", 6},
    {"bread-rolls", 6}, {"rolls", 6}, {"buns", 6},
    {"wheat-breads", 6}, {"rye-breads", 6}, {"wholemeal-breads", 6},
    {"wholegrain-breads", 6}, {"multigrain-breads", 6},
    {"white-breads", 6}, {"brown-breads", 6}, {"sourdough-breads", 6},
    {"toast-breads", 6}, {"sandwich-breads", 6}, {"baguettes", 6},
    {"ciabatta", 6}, {"flatbreads", 6}, {"pita-breads", 6}, {"pita", 6},
    {"naan", 6}, {"tortillas", 6}, {"wraps", 6},
    {"crispbreads", 6}, {"rusks", 6}, {"zwieback", 6}, {"breadsticks", 6},
    {"breadcrumbs", 6}, {"burger-buns", 6}, {"hot-dog-buns", 6},
    {"pre-baked-breads", 6}, {"part-baked-breads", 6},
    // Sonstiges (7) — beverages, dairy, pantry, etc.
    {"beverages", 7}, {"beverage", 7}, {"waters", 7}, {"water", 7},
    {"mineral-waters", 7}, {"spring-waters", 7}, {"flavored-waters", 7},
    {"juices", 7}, {"fruit-juices", 7}, {"vegetable-juices", 7},
    {"nectars", 7}, {"smoothies", 7}, {"soft-drinks", 7}, {"sodas", 7},
    {"cola", 7}, {"lemonades", 7}, {"energy-drinks", 7}, {"sports-drinks", 7},
    {"coffee", 7}, {"tea", 7}, {"iced-teas", 7},
    {"milk", 7}, {"dairy-products", 7},
    {"cheeses", 7}, {"cheese", 7}, {"yogurts", 7}, {"yogurt", 7},
    {"quark", 7}, {"cream", 7}, {"butter", 7}, {"margarines", 7},
    {"eggs", 7}, {"egg", 7},
    {"oils", 7}, {"olive-oils", 7}, {"sunflower-oils", 7},
    {"rapeseed-oils", 7}, {"fats", 7},
    {"sauces", 7}, {"tomato-sauces", 7}, {"ketchup", 7},
    {"mayonnaises", 7}, {"mustards", 7}, {"condiments", 7},
    {"spices", 7}, {"herbs", 7}, {"salt", 7}, {"vinegars", 7},
    {"pasta", 7}, {"pastas", 7}, {"noodles", 7}, {"rice", 7},
    {"cereals", 7}, {"breakfast-cereals", 7}, {"mueslis", 7},
    {"oatmeals", 7}, {"flours", 7}, {"sugars", 7},
    {"honey", 7}, {"jams", 7}, {"fruit-spreads", 7},
    {"prepared-meals", 7}, {"ready-meals", 7}, {"soups", 7},
    {"canned-foods", 7}, {"preserved-foods", 7},
    {"plant-based-foods", 7}, {"plant-based-beverages", 7},
    {"tofu", 7}, {"meat-analogues", 7},
    {"vegan-products", 7}, {"vegetarian-products", 7},
    {"baby-foods", 7}, {"pet-foods", 7},
};
static const int kOffCatMapSize = (int)(sizeof(kOffCatMap) / sizeof(kOffCatMap[0]));

static const char* const kOffCatNames[] = {
    "Tiefkühlprodukte",   // 0
    "Geflügel",            // 1
    "Fisch",               // 2
    "Fleisch & Wurst",     // 3
    "Gemüse",              // 4
    "Snacks & Süßwaren",   // 5
    "Brot & Backwaren",    // 6
    "Sonstiges"            // 7
};

static String mapOffCategory(const JsonArray &tags) {
    int best = 8;
    for (JsonVariant tv : tags) {
        String tag = tv.as<String>();
        int colon = tag.indexOf(':');
        if (colon >= 0) tag = tag.substring(colon + 1);
        for (int i = 0; i < kOffCatMapSize; i++) {
            if (kOffCatMap[i].prio < best && tag == kOffCatMap[i].tag) {
                best = kOffCatMap[i].prio;
                break;
            }
        }
        if (best == 0) break;
    }
    return kOffCatNames[best < 8 ? best : 7];
}

namespace {
constexpr const char *OFF_CACHE_PATH = "/off_cache.json";
constexpr size_t OFF_CACHE_MAX_ITEMS = 50;

void productFromJson(const String &barcode, JsonObject obj, ProductInfo &product) {
    product.barcode    = obj["barcode"]   | barcode;
    product.name       = obj["name"]      | "";
    product.brand      = obj["brand"]     | "";
    product.quantity   = obj["quantity"]  | "";
    product.category   = obj["category"]  | "";
    product.nutriscore = obj["nutriscore"]| "";
    product.labels.clear();
    for (JsonVariant label : obj["labels"].as<JsonArray>())
        product.labels.push_back(label.as<String>());
}

void productToJson(JsonObject obj, const ProductInfo &product) {
    obj["barcode"]    = product.barcode;
    obj["name"]       = product.name;
    obj["brand"]      = product.brand;
    obj["quantity"]   = product.quantity;
    obj["category"]   = product.category;
    obj["nutriscore"] = product.nutriscore;
    JsonArray labels  = obj["labels"].to<JsonArray>();
    for (const String &label : product.labels) labels.add(label);
    obj["cachedAt"]   = millis();
}
}

OpenFoodFacts::OpenFoodFacts(ApiClient &client, LittleFSManager *cacheFs) : api(client), filesystem(cacheFs) {}

bool OpenFoodFacts::fetchProduct(const String &barcode, ProductInfo &product) {
    // 1. MySQL product_cache — primary source of truth
    if (_sync && _sync->hasConfig()) {
        if (_sync->fetchProductFromMySQL(barcode, product)) {
            Logger::info("OpenFoodFacts", String("MySQL hit: ") + barcode);
            return true;
        }
    }

    // 2. OpenFoodFacts API — only for truly unknown products
    // Request only the fields we actually use – drops response from ~200 KB
    // down to ~1-2 KB, making the fetch 5-10× faster on slow connections.
    String url = "https://world.openfoodfacts.org/api/v2/product/" + barcode
               + "?fields=product_name,product_name_de,brands,quantity"
                 ",categories_tags,nutriscore_grade,labels_tags";

    ApiResponse response = api.get(url);
    if (response.status <= 0) {
        Logger::warn("OpenFoodFacts", "First attempt failed, retrying...");
        delay(1000);
        response = api.get(url);
    }
    if (response.status != 200) {
        Logger::warn("OpenFoodFacts", String("HTTP status ") + response.status);
        return false;
    }

    // JSON filter: skip everything except the fields we requested.
    // This keeps the working-set tiny even if the server sends extra data.
    JsonDocument filter;
    filter["status"]                            = true;
    filter["product"]["product_name_de"]        = true;
    filter["product"]["product_name"]           = true;
    filter["product"]["brands"]                 = true;
    filter["product"]["quantity"]               = true;
    filter["product"]["nutriscore_grade"]       = true;
    filter["product"]["categories_tags"]         = true;
    filter["product"]["labels_tags"][0]         = true;

    JsonDocument doc;
    DeserializationError err = deserializeJson(
        doc, response.body, DeserializationOption::Filter(filter));

    if (err || doc["status"].as<int>() != 1) {
        Logger::warn("OpenFoodFacts", String("Parse/status error: ") + (err ? err.c_str() : "status!=1"));
        return false;
    }

    JsonObject p = doc["product"];
    product.barcode = barcode;

    // Prefer German name, fall back to generic product_name
    String nameDe = p["product_name_de"] | "";
    product.name  = nameDe.isEmpty() ? (p["product_name"] | "") : nameDe;

    product.brand      = p["brands"]          | "";
    product.quantity   = p["quantity"]         | "";
    product.nutriscore = p["nutriscore_grade"] | "";

    product.category = mapOffCategory(p["categories_tags"].as<JsonArray>());

    product.labels.clear();
    for (JsonVariant lbl : p["labels_tags"].as<JsonArray>())
        product.labels.push_back(lbl.as<String>());

    bool ok = !product.name.isEmpty();
    if (ok && _sync) _sync->pushProduct(product);  // write new product to MySQL
    Logger::info("OpenFoodFacts", String("API fetch: ") + product.name + " (" + product.brand + ")");
    return ok;
}

bool OpenFoodFacts::loadCachedProduct(const String &barcode, ProductInfo &product) {
    if (filesystem == nullptr) return false;

    String raw;
    if (!filesystem->readFile(OFF_CACHE_PATH, raw)) return false;

    JsonDocument doc;
    if (deserializeJson(doc, raw) || !doc.is<JsonArray>()) return false;

    for (JsonObject obj : doc.as<JsonArray>()) {
        if (String(obj["barcode"] | "") == barcode) {
            productFromJson(barcode, obj, product);
            return !product.name.isEmpty();
        }
    }
    return false;
}

void OpenFoodFacts::cacheProduct(const ProductInfo &product) {
    if (filesystem == nullptr || product.barcode.isEmpty()) return;

    String raw;
    if (!filesystem->readFile(OFF_CACHE_PATH, raw)) raw = "[]";

    JsonDocument oldDoc;
    if (deserializeJson(oldDoc, raw) || !oldDoc.is<JsonArray>()) {
        oldDoc.clear();
        oldDoc.to<JsonArray>();
    }

    JsonDocument newDoc;
    JsonArray out = newDoc.to<JsonArray>();
    JsonObject first = out.add<JsonObject>();
    productToJson(first, product);

    size_t copied = 1;
    for (JsonObject obj : oldDoc.as<JsonArray>()) {
        if (copied >= OFF_CACHE_MAX_ITEMS) break;
        if (String(obj["barcode"] | "") == product.barcode) continue;
        JsonObject dst = out.add<JsonObject>();
        dst.set(obj);
        copied++;
    }

    String serialized;
    serializeJson(newDoc, serialized);
    filesystem->writeFileAtomic(OFF_CACHE_PATH, serialized);
}

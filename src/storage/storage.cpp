#include "storage.hpp"

bool KeyValueStore::put(const std::string& key, const std::string& value) {
    store_[key] = value;
    return true;
}

bool KeyValueStore::get(const std::string& key, std::string& value) {
    auto it = store_.find(key);
    if (it != store_.end()) {
        value = it->second;
        return true;
    }
    return false;
}

bool KeyValueStore::remove(const std::string& key) {
    return store_.erase(key) > 0;
}
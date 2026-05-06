#pragma once
#include <functional>
#include <unordered_map>
#include <vector>
#include <typeindex>
#include <memory>
#include <any>

// Simple synchronous pub-sub bus.
// All publish/subscribe calls happen on the sim thread — no locking needed.
// Subscribers are stored in pre-reserved vectors; no heap allocation per call
// once the system is warmed up.

class EventBus {
public:
    template<typename T>
    using Handler = std::function<void(const T&)>;

    template<typename T>
    void subscribe(Handler<T> handler) {
        auto key = std::type_index(typeid(T));
        auto& vec = handlers_[key];
        vec.push_back([h = std::move(handler)](const std::any& ev) {
            h(std::any_cast<const T&>(ev));
        });
    }

    template<typename T>
    void publish(const T& event) {
        auto key = std::type_index(typeid(T));
        auto it = handlers_.find(key);
        if (it == handlers_.end()) return;
        for (auto& fn : it->second)
            fn(event);
    }

    void clear() { handlers_.clear(); }

private:
    std::unordered_map<std::type_index, std::vector<std::function<void(const std::any&)>>> handlers_;
};

#pragma once
#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <random>
#include <utility>

using namespace std;

template<typename T>

class SkipList {
private:
    struct alignas(void*) Node {
        Node(const optional<T>& in_data, const uint8_t in_height) : data{in_data}, height{in_height} {
            assert(height >= 1 && height <= max_levels);
            uninitialized_fill_n(next(), height, nullptr);
        }

        Node** next() {
            return reinterpret_cast<Node**>(this + 1);
        }

        static void* operator new(const size_t size, const uint8_t height) {
            return ::operator new(size + height * sizeof(Node*));
        }

        static void operator delete(void* memory) {
            ::operator delete(memory);
        }

        static void operator delete(void* memory, uint8_t) {
            ::operator delete(memory);
        }

        const optional<T> data;
        const uint8_t height;
    };

public:
    SkipList() {

    }

    ~SkipList() {
        Node* current = head->next()[0];
        delete head;
        while (current) {
            Node* next = current->next()[0];
            delete current;
            current = next;
        }
    }

    SkipList(const SkipList&) = delete;
    SkipList& operator=(const SkipList&) = delete;


    void insert(T element) {
        auto [node, predecessors] = search(element);
        node = node->next()[0];
        if (node && node->data == element) {
            return;
        }

        const uint8_t height = random_height();
        Node* inserted = new (height) Node(element, height);
        for (uint8_t level = 0; level < min(levels, height); ++level) {
            Node* next = predecessors[level]->next()[level];
            predecessors[level]->next()[level] = inserted;
            inserted->next()[level] = next;
        }

        for (uint8_t level = levels; level < height; ++level) {
            head->next()[level] = inserted;
        }

        levels = max(levels, height);
    }

    void erase(T element) {
        auto [node, predecessors] = search(element);
        node = node->next()[0];
        if (!node || node->data != element) {
            return;
        }

        for (uint8_t level = 0; level < node->height; ++level) {
            predecessors[level]->next()[level] = node->next()[level];
        }

        delete node;
        while (levels > 0 && !head->next()[levels - 1]) {
            --levels;
        }
    }

    bool exists(T element) {
        Node* node = search(element).first->next()[0];
        return node && node->data == element;
    }

private:
    static constexpr uint8_t max_levels = 32;
    uint8_t levels = 0;
    Node* head = new (max_levels) Node(nullopt, max_levels);
    mt19937 gen{random_device{}()};

    pair<Node*, array<Node*, max_levels>> search(T element) {
        array<Node*, max_levels> predecessors = {};
        int8_t level = levels - 1;
        Node* current = head;
        while (level >= 0) {
            Node* next = current->next()[level];
            if (!next || next->data >= element) {
                predecessors[level] = current;
                --level;
            }

            else {
                current = next;
            }
        }

        return {current, predecessors};
    }

    uint8_t random_height() {
        const uint32_t x = static_cast<uint32_t>(gen());
        return min(static_cast<uint8_t>(countr_zero(x) + 1), max_levels);
    }
};
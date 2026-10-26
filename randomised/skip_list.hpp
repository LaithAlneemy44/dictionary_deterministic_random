#pragma once
#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <cstdint>
#include <optional>
#include <random>
#include <utility>
#include <vector>

using namespace std;

template<typename T>

class SkipList {
private:
    struct Node {
        Node(const T& in_data, const uint8_t height) : data{in_data}, next(height) {
            assert(height >= 1 && height <= max_levels);
            next.resize(height);
        }

        ~Node() {
            delete data;
            delete next;
        }

        const optional<T> data;
        vector<Node*> next;

    protected:
        Node() : next(max_levels) {

        }
    };

    struct Head : Node {
        Head() : Node() {

        }
    };

public:
    SkipList() {

    }

    ~SkipList() {
        Node* current = head;
        while (current->next[0]) {
            Node* next = current->next[0];
            delete current;
            current = next;
        }

        delete current;
    }

    void insert(T element) {
        auto [node, predecessors] = search(element);
        node = node->next[0];
        if (node && node->data == element) {
            return;
        }

        const uint8_t height = random_height();
        Node* inserted = new Node(element, height);
        for (uint8_t level = 0; level < height; ++level) {
            if (!predecessors[level]) {
                break;
            }

            Node* next = predecessors[level]->next[level];
            predecessors[level]->next[level] = inserted;
            inserted->next[level] = next;
        }
    }

    void erase(T element) {
        auto [node, predecessors] = search(element);
        node = node->next[0];
        if (!node || node->data != element) {
            return;
        }

        for (uint8_t level = 0; level < max_levels; ++level) {
            if (!predecessors[level] || predecessors[level]->next[level] != node) {
                break;
            }

            predecessors[level]->next[level] = node->next[level];
        }

        delete node;
    }

    bool exists(T element) {
        Node* node = search(element).first->next[0];
        return node && node->data == element;
    }

private:
    static constexpr uint8_t max_levels = 32;
    Head* head = new Head();
    mt19937 gen{random_device{}()};

    pair<Node*, array<Node*, max_levels>> search(T element) {
        array<Node*, max_levels> predecessors = {};
        int8_t level = max_levels - 1;
        Node* current = head;
        while (level >= 0) {
            Node* next = current->next[level];
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
        return static_cast<uint8_t>(min(countr_zero(x) + 1, max_levels));
    }
};
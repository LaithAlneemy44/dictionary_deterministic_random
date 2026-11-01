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
        Node* current = head->next[0];
        delete head;
        while (current) {
            Node* next = current->next[0];
            delete current;
            current = next;
        }
    }

    SkipList(const SkipList&) = delete;
    SkipList& operator=(const SkipList&) = delete;


    void insert(T element) {
        auto [node, predecessors] = search(element);
        node = node->next[0];
        if (node && node->data == element) {
            return;
        }

        const uint8_t height = random_height();
        Node* inserted = new Node(element, height);
        for (uint8_t level = 0; level < levels; ++level) {
            Node* next = predecessors[level]->next[level];
            predecessors[level]->next[level] = inserted;
            inserted->next[level] = next;
        }

        levels = max(level, height);
        for (uint8_t level = levels; level < height; ++level) {
            head->next[level] = inserted;
        }
    }

    void erase(T element) {
        auto [node, predecessors] = search(element);
        node = node->next[0];
        if (!node || node->data != element) {
            return;
        }

        const uint8_t height = node->next.size();
        for (uint8_t level = 0; level < height; ++level) {
            predecessors[level]->next[level] = node->next[level];
        }

        delete node;
        while (head > 0 && !head->next[levels - 1]) {
            --levels;
        }
    }

    bool exists(T element) {
        Node* node = search(element).first->next[0];
        return node && node->data == element;
    }

private:
    static constexpr uint8_t max_levels = 32;
    uint8_t levels = 0;
    Head* head = new Head();
    mt19937 gen{random_device{}()};

    pair<Node*, vector<Node*>> search(T element) {
        vector<Node*> predecessors(levels);
        int8_t level = levels - 1;
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
        return min(static_cast<uint8_t>(countr_zero(x) + 1), max_levels);
    }
};
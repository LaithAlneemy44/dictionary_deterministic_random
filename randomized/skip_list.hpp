#pragma once

using namespace std;

template<typename T>

class SkipList {
public:
    struct Node {
        Node(const T& in_data, const size_t levels) : data{in_data} {
            if (levels > max_levels) {
                throw InvalidValueException("Levels exceeded max level size.\nLevels: " + levels "\nMax Levels: " + max_levels);
            }

            next.resize(levels);
        } 

        const T data;
        vector<Node*> next;
    }

public:
    SkipList() {
        
    }

    void insert(T element) {
        const auto& [node, predecessor] = search(element);
        if (node && node->data == element) {
            return;
        }

        Node* inserted = new Node(element, random_level());
        for (size_t level = 0; level < max_levels; ++level) {
            if (!predecessors[level]) {
                continue;
            }

            Node* next = predecessors[level]->next[level];
            predecessors[level]->next[level] = inserted;
            inserted->next[level] = next;
        }
    }

    void erase(T element) {
        const auto& [node, predecessors] = search(element);
        if (!node || node->data != element) {
            return;
        }

        for (size_t level = 0; level < max_levels; ++level) {
            if (!predecessors[level]) {
                continue;
            }

            predecessors[level]->next[level] = node->next[level];
        }

        delete node;
    }

    bool exists(T element) {
        Node* node = search(element)->first;
        return node && node->data == element;
    }

private:
    pair<Node*, vector<Node*>> search(T element) {
        vector<Node*> predecessors;
        int level;
        Node* current;
        for (level = levels - 1; level >= 0; --level) {
            if (head[level]->data <= element) {
                current = head[level];
                predecessors.resize(level + 1);
                predecessors[level] = head;
                break;
            }
        }

        while (level >= 0) {
            const T& val = current->data;
            if (val == element && level == 0) {
                return {current, predecessors};
            }

            Node* next = current->next[level];
            if (next && next->data <= element) {
                predecessor[level] = current;
                current = next;
            }

            else if (level == 0) {
                return {current, predecessors};
            }
            
            else {
                --level;
            }
        }
        
        return {nullptr, predecessors};
    }

    size_t random_level() {

    }

    constexpr size_t max_levels = 32;
    constinit size_t levels = 0;
    constinit array<Node*, max_levels> head;
};
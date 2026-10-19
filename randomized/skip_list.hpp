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
        stack<Node*> predecessors;
        predecessors
    }

    void erase(T element) {
        stack<Node*> predecessors;

    }

    bool exists(T element) {
        Node* node = search(element);
        return node && node->data == element;
    }

private:
    Node* search(T element) {
        int level;
        Node* current;
        for (level = levels - 1; level >= 0; --level) {
            if (head[level]->data <= element) {
                current = head[level];
                break;
            }
        }

        while (level >= 0) {
            const T& val = current->data;
            if (val == element && level == 0) {
                return current;
            }

            Node* next = current->next[level];
            if (next && next->data <= element) {
                current = next;
            }

            else if (level == 0) {
                return current;
            }
            
            else {
                --level;
            }
        }
        
        return nullptr;
    }

    constexpr size_t max_levels = 32;
    constinit size_t levels = 0;
    array<Node*, max_levels> head;
};
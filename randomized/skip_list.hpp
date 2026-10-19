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
        return search(element)->data == element;
    }

private:
    Node* search(T element) {

    }

    constexpr size_t max_levels = 32;
    constinit size_t levels = 0;
    array<Node*, max_levels> head;
};
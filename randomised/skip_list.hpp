#pragma once

using namespace std;

template<typename T>

class SkipList {
private:
    struct Node {
        Node(const T& in_data, const size_t height) : data{in_data}, next(height) {
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

    void insert(T element) {
        const auto& [node, predecessor] = search(element);
        node = node->next[0];
        if (node && node->data == element) {
            return;
        }

        const size_t height = random_height();
        Node* inserted = new Node(element, random_height());
        for (size_t level = 0; level < height; ++level) {
            if (!predecessors[level]) {
                break;
            }

            Node* next = predecessors[level]->next[level];
            predecessors[level]->next[level] = inserted;
            inserted->next[level] = next;
        }
    }

    void erase(T element) {
        auto& [node, predecessors] = search(element);
        node = node->next[0]; 
        if (!node || node->data != element) {
            return;
        }

        for (size_t level = 0; level < predecessors.size(); ++level) {
            if (!predecessors[level] || predecessors[level]->next != node) {
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
    pair<Node*, vector<Node*>> search(T element) {
        vector<Node*> predecessors(levels);
        int level = levels - 1;
        Node* current = head;
        while (level >= 0) {
            Node* next = current->next[level];
            if (!next || next->val >= element) {
                predecessors[level] = current;
                --level;
            }

            else {
                current = next;
            }
        }
        
        return {current, predecessors};
    }

    size_t random_height() {
        return countr_zeros(static_cast<__uint32_t>(gen()));
    }

    constexpr size_t max_levels = 32;
    constinit size_t levels = 0;
    constinit Head head;
    constexpr gen(random_device{}());
};
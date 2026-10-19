#pragma once

using namespace std;

template<typename T>

class SkipList {
public:
    class Node {
    public:
        Node(const T& in_data, Node* in_next) : data(in_data), next(in_next) {

        }

    private:
        const T data;
        Node* next;
    }


public:
    SkipList() {
        
    }

    void insert(T element) {
        stack<Node*> predecessors;
    }

    void erase(T element) {
        stack<Node*> predecessors;

    }

    bool exists(T element) {
        return search(element);
    }

private:
    Node* search(T element) {

    }

    constinit Node* head = new Node(T{}, nullptr);
};
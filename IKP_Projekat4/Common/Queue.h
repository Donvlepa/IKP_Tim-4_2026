#pragma once

#include <cstddef>
#include <new>

// Generic singly linked queue.
template <typename T>
class Queue
{
private:

    // Represents a single queue node.
    struct Node
    {
        T value;
        Node* next;
    };

    Node* head;
    Node* tail;
    std::size_t count;

public:

    // Initializes an empty queue.
    Queue()
        : head(nullptr),
        tail(nullptr),
        count(0)
    {
    }

    // Releases all queue nodes.
    ~Queue()
    {
        clear();
    }

    // Prevents unsafe copying.
    Queue(const Queue&) = delete;
    Queue& operator=(const Queue&) = delete;

    // Adds an element to the end of the queue.
    bool enqueue(const T& value)
    {
        Node* newNode = new (std::nothrow) Node
        {
            value,
            nullptr
        };

        if (newNode == nullptr)
        {
            return false;
        }

        if (tail == nullptr)
        {
            head = newNode;
            tail = newNode;
        }
        else
        {
            tail->next = newNode;
            tail = newNode;
        }

        count++;

        return true;
    }

    // Adds an element to the front of the queue.
    bool enqueueFront(const T& value)
    {
        Node* newNode = new (std::nothrow) Node
        {
            value,
            head
        };

        if (newNode == nullptr)
        {
            return false;
        }

        head = newNode;

        if (tail == nullptr)
        {
            tail = newNode;
        }

        count++;

        return true;
    }

    // Removes and returns the first element.
    bool dequeue(T& value)
    {
        if (head == nullptr)
        {
            return false;
        }

        Node* oldHead = head;

        value = oldHead->value;

        head = head->next;

        if (head == nullptr)
        {
            tail = nullptr;
        }

        delete oldHead;

        count--;

        return true;
    }

    // Returns the first element without removing it.
    bool peek(T& value) const
    {
        if (head == nullptr)
        {
            return false;
        }

        value = head->value;

        return true;
    }

    // Checks whether the queue is empty.
    bool isEmpty() const
    {
        return head == nullptr;
    }

    // Returns the number of stored elements.
    std::size_t size() const
    {
        return count;
    }

    // Removes all elements from the queue.
    void clear()
    {
        while (head != nullptr)
        {
            Node* current = head;

            head = head->next;

            delete current;
        }

        head = nullptr;
        tail = nullptr;
        count = 0;
    }
};
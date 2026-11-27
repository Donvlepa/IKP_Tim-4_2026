#pragma once

#include <cstdint>
#include <cstddef>
#include <new>

// Generic hash map with separate chaining.
// Keys are unsigned 64-bit identifiers.
template <typename T>
class HashMap
{
private:

    // Represents a single hash map node.
    struct Node
    {
        const uint64_t key;
        T value;
        Node* next;
    };

    Node** buckets;
    std::size_t bucketCount;
    std::size_t count;

    // Calculates the bucket index for a key.
    std::size_t hash(uint64_t key) const
    {
        return static_cast<std::size_t>(
            key % bucketCount
            );
    }

    // Allocates an empty bucket array.
    static Node** allocateBuckets(std::size_t capacity)
    {
        if (capacity == 0 ||
            capacity > static_cast<std::size_t>(-1) / sizeof(Node*))
        {
            return nullptr;
        }

        return new (std::nothrow) Node * [capacity]();
    }

    // Expands the bucket array and redistributes nodes.
    bool rehash()
    {
        if (bucketCount >
            (static_cast<std::size_t>(-1) / sizeof(Node*)) / 2)
        {
            return false;
        }

        std::size_t newCapacity = bucketCount * 2;

        Node** newBuckets = allocateBuckets(newCapacity);

        if (newBuckets == nullptr)
        {
            return false;
        }

        for (std::size_t i = 0; i < bucketCount; i++)
        {
            Node* current = buckets[i];

            while (current != nullptr)
            {
                Node* next = current->next;

                std::size_t newIndex =
                    static_cast<std::size_t>(
                        current->key % newCapacity
                        );

                current->next = newBuckets[newIndex];
                newBuckets[newIndex] = current;

                current = next;
            }
        }

        delete[] buckets;

        buckets = newBuckets;
        bucketCount = newCapacity;

        return true;
    }

public:

    // Initializes an empty hash map.
    explicit HashMap(std::size_t initialCapacity = 16)
        : buckets(nullptr),
        bucketCount(0),
        count(0)
    {
        buckets = allocateBuckets(initialCapacity);

        if (buckets != nullptr)
        {
            bucketCount = initialCapacity;
        }
    }

    // Releases all nodes and buckets.
    ~HashMap()
    {
        clear();

        delete[] buckets;
    }

    // Prevents unsafe copying.
    HashMap(const HashMap&) = delete;
    HashMap& operator=(const HashMap&) = delete;

    // Checks whether initialization was successful.
    bool isValid() const
    {
        return buckets != nullptr;
    }

    // Inserts a new key-value pair.
    // Duplicate keys are rejected.
    bool insert(uint64_t key, const T& value)
    {
        if (!isValid())
        {
            return false;
        }

        if (contains(key))
        {
            return false;
        }

        Node* newNode = new (std::nothrow) Node
        {
            key,
            value,
            nullptr
        };

        if (newNode == nullptr)
        {
            return false;
        }

        // Expand when the load factor reaches
        // approximately 0.75.
        if (count >= bucketCount - bucketCount / 4)
        {
            if (!rehash())
            {
                delete newNode;

                return false;
            }
        }

        std::size_t index = hash(key);

        newNode->next = buckets[index];
        buckets[index] = newNode;

        count++;

        return true;
    }

    // Finds a value by its key.
    T* find(uint64_t key)
    {
        if (!isValid())
        {
            return nullptr;
        }

        std::size_t index = hash(key);

        Node* current = buckets[index];

        while (current != nullptr)
        {
            if (current->key == key)
            {
                return &current->value;
            }

            current = current->next;
        }

        return nullptr;
    }

    // Finds a value in a read-only hash map.
    const T* find(uint64_t key) const
    {
        if (!isValid())
        {
            return nullptr;
        }

        std::size_t index = hash(key);

        const Node* current = buckets[index];

        while (current != nullptr)
        {
            if (current->key == key)
            {
                return &current->value;
            }

            current = current->next;
        }

        return nullptr;
    }

    // Checks whether a key exists.
    bool contains(uint64_t key) const
    {
        return find(key) != nullptr;
    }

    // Updates the value associated with a key.
    bool update(uint64_t key, const T& value)
    {
        T* existingValue = find(key);

        if (existingValue == nullptr)
        {
            return false;
        }

        *existingValue = value;

        return true;
    }

    // Removes an element by its key.
    bool remove(uint64_t key)
    {
        if (!isValid())
        {
            return false;
        }

        std::size_t index = hash(key);

        Node* current = buckets[index];
        Node* previous = nullptr;

        while (current != nullptr)
        {
            if (current->key == key)
            {
                if (previous == nullptr)
                {
                    buckets[index] = current->next;
                }
                else
                {
                    previous->next = current->next;
                }

                delete current;

                count--;

                return true;
            }

            previous = current;
            current = current->next;
        }

        return false;
    }

    // Visits every stored key-value pair.
    // Structural modifications are not allowed
    // during traversal.
    template <typename Visitor>
    void forEach(Visitor visitor)
    {
        for (std::size_t i = 0; i < bucketCount; i++)
        {
            Node* current = buckets[i];

            while (current != nullptr)
            {
                visitor(current->key, current->value);

                current = current->next;
            }
        }
    }

    // Visits all elements without modifying values.
    template <typename Visitor>
    void forEach(Visitor visitor) const
    {
        for (std::size_t i = 0; i < bucketCount; i++)
        {
            const Node* current = buckets[i];

            while (current != nullptr)
            {
                visitor(current->key, current->value);

                current = current->next;
            }
        }
    }

    // Returns the number of stored elements.
    std::size_t size() const
    {
        return count;
    }

    // Returns the current number of buckets.
    std::size_t capacity() const
    {
        return bucketCount;
    }

    // Checks whether the hash map is empty.
    bool isEmpty() const
    {
        return count == 0;
    }

    // Removes all stored elements.
    void clear()
    {
        for (std::size_t i = 0; i < bucketCount; i++)
        {
            Node* current = buckets[i];

            while (current != nullptr)
            {
                Node* next = current->next;

                delete current;

                current = next;
            }

            buckets[i] = nullptr;
        }

        count = 0;
    }
};
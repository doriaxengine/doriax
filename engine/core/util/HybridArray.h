#ifndef DORIAX_ARRAY_H
#define DORIAX_ARRAY_H

#include <cstddef>

#ifdef DORIAX_EDITOR
#include <vector>
#endif

namespace doriax {

template <typename T, std::size_t Size>
struct HybridArray {
#ifdef DORIAX_EDITOR
    std::vector<T> values = std::vector<T>(Size);

    std::size_t size() const {
        return values.size();
    }

    T* data() {
        return values.data();
    }

    const T* data() const {
        return values.data();
    }

    bool validIndex(int index) const {
        return index >= 0;
    }

    T& operator[](std::size_t index) {
        if (index >= values.size()) {
            values.resize(index + 1);
        }
        return values[index];
    }

    const T& operator[](std::size_t index) const {
        return values[index];
    }

#else
    T values[Size]{};

    std::size_t size() const {
        return Size;
    }

    T* data() {
        return values;
    }

    const T* data() const {
        return values;
    }

    bool validIndex(int index) const {
        return index >= 0 && static_cast<std::size_t>(index) < Size;
    }

    T& operator[](std::size_t index) {
        return values[index];
    }

    const T& operator[](std::size_t index) const {
        return values[index];
    }
#endif
};

}

#endif

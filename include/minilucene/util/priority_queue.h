#pragma once

#include <vector>

namespace minilucene {
namespace util {

template <typename T>
class PriorityQueue {
public:
    PriorityQueue() = default;
    virtual ~PriorityQueue() = default;

    void Initialize(int max_size) {
        heap_.clear();
        heap_.resize(1);
        heap_.reserve(static_cast<size_t>(max_size) + 1);
        max_size_ = max_size;
    }

    void Put(const T& element) {
        if (max_size_ > 0 && Size() >= static_cast<size_t>(max_size_)) {
            if (LessThan(element, Top())) {
                return;
            }
            Pop();
        }
        heap_.push_back(element);
        UpHeap(Size());
    }

    const T& Top() const {
        return heap_[1];
    }

    void Pop() {
        heap_[1] = heap_[Size()];
        heap_.pop_back();
        DownHeap(1);
    }

    size_t Size() const {
        return heap_.size() - 1;
    }

protected:
    virtual bool LessThan(const T& a, const T& b) const = 0;

    std::vector<T> heap_{T()};
    int max_size_ = 0;

private:
    void UpHeap(size_t i) {
        while (i > 1) {
            size_t p = i / 2;
            if (!LessThan(heap_[i], heap_[p])) {
                break;
            }
            std::swap(heap_[i], heap_[p]);
            i = p;
        }
    }

    void DownHeap(size_t i) {
        size_t size = Size();
        while (true) {
            size_t smallest = i;
            size_t left = 2 * i;
            size_t right = 2 * i + 1;
            if (left <= size && LessThan(heap_[left], heap_[smallest])) {
                smallest = left;
            }
            if (right <= size && LessThan(heap_[right], heap_[smallest])) {
                smallest = right;
            }
            if (smallest == i) {
                break;
            }
            std::swap(heap_[i], heap_[smallest]);
            i = smallest;
        }
    }
};

}  // namespace util
}  // namespace minilucene

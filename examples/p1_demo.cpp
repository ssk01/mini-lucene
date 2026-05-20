#include "minilucene/util/bit_vector.h"
#include "minilucene/util/priority_queue.h"
#include <iostream>
#include <string>

using namespace minilucene::util;

class ScoredDoc {
public:
    int doc_id;
    float score;

    ScoredDoc() : doc_id(0), score(0.0f) {}
    ScoredDoc(int id, float s) : doc_id(id), score(s) {}
};

class ScoredDocQueue : public PriorityQueue<ScoredDoc> {
protected:
    bool LessThan(const ScoredDoc& a, const ScoredDoc& b) const override {
        return a.score < b.score;
    }
};

int main() {
    std::cout << "=== P1 Demo: BitVector ===" << std::endl;
    BitVector bv(16);
    bv.Set(0);
    bv.Set(3);
    bv.Set(7);
    bv.Set(15);
    for (int i = 0; i < 16; ++i) {
        std::cout << (bv.Get(i) ? "1" : "0");
    }
    std::cout << std::endl;
    std::cout << "Count: " << bv.Count() << std::endl;

    std::cout << "\n=== P1 Demo: PriorityQueue (Top-K) ===" << std::endl;
    ScoredDocQueue pq;
    pq.Initialize(3);

    pq.Put(ScoredDoc(1, 0.5f));
    pq.Put(ScoredDoc(2, 0.8f));
    pq.Put(ScoredDoc(3, 0.3f));
    pq.Put(ScoredDoc(4, 0.9f));
    pq.Put(ScoredDoc(5, 0.6f));

    std::cout << "Top " << pq.Size() << " results:" << std::endl;
    while (pq.Size() > 0) {
        ScoredDoc top = pq.Top();
        std::cout << "  doc=" << top.doc_id << " score=" << top.score << std::endl;
        pq.Pop();
    }

    return 0;
}

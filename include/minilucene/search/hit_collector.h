#pragma once

namespace minilucene {
namespace search {

class HitCollector {
public:
    virtual ~HitCollector() = default;
    virtual void Collect(int doc, float score) = 0;
};

}  // namespace search
}  // namespace minilucene

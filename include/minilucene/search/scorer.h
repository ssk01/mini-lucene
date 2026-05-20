#pragma once

namespace minilucene {
namespace search {

class Scorer {
public:
    virtual ~Scorer() = default;
    virtual bool Next() = 0;
    virtual int Doc() const = 0;
    virtual float Score() = 0;
};

}  // namespace search
}  // namespace minilucene

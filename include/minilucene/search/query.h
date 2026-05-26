#pragma once

#include <memory>
#include <string>

namespace minilucene {
namespace index {
class IndexReader;
}

namespace search {

class Scorer;

class Query {
public:
    virtual ~Query() = default;
    virtual std::unique_ptr<Scorer> CreateScorer(index::IndexReader& reader) const = 0;
    virtual std::string ToString() const = 0;

    // Optional human-readable field name (for ToString) — Term-level state
    // only carries the field NUMBER. QueryParser sets this when parsing
    // `field:term` syntax. Empty → ToString falls back to `field<N>:`.
    void SetFieldName(std::string n) { field_name_ = std::move(n); }
    const std::string& FieldName() const { return field_name_; }

    // Lucene 1.0.1 boost: multiplied into the scorer's per-doc score.
    // 1.0 (default) is a no-op. QueryParser parses `term^2.5` into this.
    void  SetBoost(float b) { boost_ = b; }
    float GetBoost() const  { return boost_; }

protected:
    // Helper: prefer the explicit field name, otherwise emit a stable
    // placeholder so ToString never silently mis-attributes a field.
    std::string FieldDisplay(int field_number) const {
        if (!field_name_.empty()) return field_name_;
        return "field<" + std::to_string(field_number) + ">";
    }

    std::string field_name_;
    float boost_ = 1.0f;
};

}  // namespace search
}  // namespace minilucene

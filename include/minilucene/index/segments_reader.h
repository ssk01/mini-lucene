#pragma once

#include "minilucene/index/index_reader.h"
#include "minilucene/index/segment_reader.h"
#include "minilucene/index/segment_infos.h"
#include "minilucene/util/priority_queue.h"

#include <memory>
#include <string>
#include <vector>

namespace minilucene {
namespace store {
class Directory;
}

namespace index {

class SegmentsReader : public IndexReader {
public:
    SegmentsReader(store::Directory& dir);
    ~SegmentsReader() override;

    std::unique_ptr<TermEnum> Terms() override;
    std::unique_ptr<TermEnum> Terms(const Term& term) override;
    std::unique_ptr<TermDocs> Docs(const Term& term) override;
    std::unique_ptr<TermPositions> Positions(const Term& term) override;
    int DocFreq(const Term& term) override;
    int NumDocs() const override;
    float Norm(int doc, int field_number) override;
    std::unique_ptr<document::Document> Document(int doc_id) override;
    void Delete(int doc_id) override;
    bool IsDeleted(int doc_id) const override;
    int MaxDoc() const override;
    void Close() override;

private:
    int SegIdx(int doc_id) const;
    int LocalDoc(int doc_id, int seg_idx) const;

    std::vector<std::unique_ptr<SegmentReader>> readers_;
    std::vector<int> doc_starts_;
};

}  // namespace index
}  // namespace minilucene

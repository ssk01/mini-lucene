#pragma once

#include "minilucene/index/index_reader.h"
#include "minilucene/index/term_enum.h"
#include "minilucene/index/term_docs.h"
#include "minilucene/index/term_positions.h"

#include <memory>
#include <string>

namespace minilucene {
namespace store {
class Directory;
class IndexInput;
}

namespace util {
class BitVector;
}

namespace index {

class FieldInfos;
class FieldsReader;
class TermInfosReader;

class SegmentReader : public IndexReader {
public:
    SegmentReader(store::Directory& dir, const std::string& segment);
    ~SegmentReader() override;

    std::unique_ptr<TermEnum> Terms() override;
    std::unique_ptr<TermEnum> Terms(const Term& term) override;
    std::unique_ptr<TermDocs> Docs(const Term& term) override;
    std::unique_ptr<TermPositions> Positions(const Term& term) override;
    int DocFreq(const Term& term) override;
    int NumDocs() const override;
    float Norm(int doc, int field_number) override;
    std::unique_ptr<document::Document> Document(int doc_id) override;
    void Delete(int doc_id) override;
    int MaxDoc() const override { return num_docs_; }
    void Close() override;

private:
    store::Directory& dir_;
    std::string segment_;
    std::unique_ptr<FieldInfos> field_infos_;
    std::unique_ptr<FieldsReader> fields_reader_;
    std::unique_ptr<TermInfosReader> term_infos_;
    std::unique_ptr<store::IndexInput> nrm_;
    std::unique_ptr<util::BitVector> deleted_docs_;
    int num_docs_ = 0;
};

class SegmentTermEnum : public TermEnum {
public:
    SegmentTermEnum(std::unique_ptr<store::IndexInput> tis);
    ~SegmentTermEnum() override;

    bool Next() override;
    const Term& Current() const override { return term_; }
    int DocFreq() const override { return doc_freq_; }
    void Close() override;

    void Seek(int64_t tis_offset);

    int64_t CurrentFreqPointer() const { return freq_pointer_; }
    int64_t CurrentProxPointer() const { return prox_pointer_; }

private:
    bool ReadCurrentEntry();

    std::unique_ptr<store::IndexInput> tis_;
    bool ended_ = false;
    Term term_;
    int doc_freq_ = 0;
    int64_t freq_pointer_ = 0;
    int64_t prox_pointer_ = 0;
};

class SegmentTermDocs : public TermDocs {
public:
    SegmentTermDocs(std::unique_ptr<store::IndexInput> frq, int64_t freq_pointer, int doc_freq);
    SegmentTermDocs(std::unique_ptr<store::IndexInput> frq, int64_t freq_pointer, int doc_freq,
                    util::BitVector* deleted_docs);
    ~SegmentTermDocs() override;

    bool Next() override;
    int Doc() const override { return doc_; }
    int Freq() const override { return freq_; }
    void Close() override;

protected:
    std::unique_ptr<store::IndexInput> frq_;
    util::BitVector* deleted_docs_ = nullptr;
    int doc_ = 0;
    int freq_ = 0;
    int remaining_ = 0;
};

class SegmentTermPositions : public TermPositions {
public:
    SegmentTermPositions(std::unique_ptr<store::IndexInput> frq,
                         std::unique_ptr<store::IndexInput> prx,
                         int64_t freq_pointer, int64_t prox_pointer, int doc_freq);
    SegmentTermPositions(std::unique_ptr<store::IndexInput> frq,
                         std::unique_ptr<store::IndexInput> prx,
                         int64_t freq_pointer, int64_t prox_pointer, int doc_freq,
                         util::BitVector* deleted_docs);
    ~SegmentTermPositions() override;

    bool Next() override;
    int Doc() const override { return doc_; }
    int Freq() const override { return freq_; }
    int NextPosition() override;
    void Close() override;

private:
    std::unique_ptr<store::IndexInput> frq_;
    std::unique_ptr<store::IndexInput> prx_;
    util::BitVector* deleted_docs_ = nullptr;
    int doc_ = 0;
    int freq_ = 0;
    int remaining_ = 0;
    int remaining_positions_ = 0;
};

}  // namespace index
}  // namespace minilucene

#pragma once

#include <memory>

namespace minilucene {
namespace document {
class Document;
}
}

namespace minilucene {
namespace index {

class Term;
class TermEnum;
class TermDocs;
class TermPositions;

class IndexReader {
public:
    virtual ~IndexReader() = default;

    virtual std::unique_ptr<TermEnum> Terms() = 0;
    virtual std::unique_ptr<TermEnum> Terms(const Term& term) = 0;
    virtual std::unique_ptr<TermDocs> Docs(const Term& term) = 0;
    virtual std::unique_ptr<TermPositions> Positions(const Term& term) = 0;
    virtual int DocFreq(const Term& term) = 0;
    virtual int NumDocs() const = 0;
    virtual float Norm(int doc, int field_number) = 0;
    virtual std::unique_ptr<document::Document> Document(int doc_id) = 0;
    virtual void Delete(int doc_id) = 0;
    virtual int MaxDoc() const = 0;
    virtual void Close() = 0;
};

}  // namespace index
}  // namespace minilucene

#pragma once

#include "minilucene/search/searcher.h"
#include "minilucene/search/top_docs.h"

#include <memory>
#include <string>

namespace minilucene {
namespace store {
class Directory;
}

namespace document {
class Document;
}

namespace index {
class IndexReader;
}

namespace search {

class IndexSearcher : public Searcher {
public:
    IndexSearcher(const std::string& path);
    IndexSearcher(store::Directory& dir);
    IndexSearcher(index::IndexReader& reader);
    ~IndexSearcher() override;

    TopDocs Search(const Query& query, int top_k) const;
    std::unique_ptr<Hits> Search(const Query& query) override;
    void Close() override;
    int MaxDoc() const;
    // Stored-field accessor used by MultiSearcher to resolve hits back to
    // their originating sub-reader's Document.
    std::unique_ptr<document::Document> Document(int doc_id) const;

private:
    std::unique_ptr<store::Directory> owned_dir_;
    index::IndexReader* reader_ = nullptr;
    bool owns_reader_ = false;
};

}  // namespace search
}  // namespace minilucene

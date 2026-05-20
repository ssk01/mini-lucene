#pragma once

#include "minilucene/search/searcher.h"
#include "minilucene/search/top_docs.h"

#include <memory>
#include <string>

namespace minilucene {
namespace store {
class Directory;
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

    std::unique_ptr<Hits> Search(const Query& query) override;
    TopDocs Search(const Query& query, int top_k) const;
    void Close() override;

private:
    std::unique_ptr<store::Directory> owned_dir_;
    index::IndexReader* reader_ = nullptr;
    bool owns_reader_ = false;
};

}  // namespace search
}  // namespace minilucene

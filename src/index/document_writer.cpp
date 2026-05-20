#include "minilucene/index/document_writer.h"
#include "minilucene/index/field_infos.h"
#include "minilucene/index/term.h"
#include "minilucene/index/term_info.h"
#include "minilucene/index/term_infos_writer.h"
#include "minilucene/analysis/analyzer.h"
#include "minilucene/analysis/token.h"
#include "minilucene/analysis/token_stream.h"
#include "minilucene/document/document.h"
#include "minilucene/document/field.h"
#include "minilucene/store/directory.h"
#include "minilucene/store/index_output.h"

#include <cmath>
#include <sstream>

namespace minilucene {
namespace index {

namespace {

uint8_t EncodeNorm(float norm) {
    if (norm >= 1.0f) return 255;
    if (norm <= 0.0f) return 0;
    return static_cast<uint8_t>(255.0f * norm);
}

float CalcNorm(int num_tokens) {
    return (num_tokens > 0) ? (1.0f / std::sqrt(static_cast<float>(num_tokens))) : 0.0f;
}

}  // namespace

DocumentWriter::DocumentWriter(store::Directory& dir, analysis::Analyzer& analyzer)
    : dir_(dir), analyzer_(analyzer) {
    field_infos_ = std::make_unique<FieldInfos>();
}

void DocumentWriter::AddDocument(const document::Document& doc) {
    for (const auto& field : doc.Fields()) {
        field_infos_->AddField(field);
    }

    for (const auto& field : doc.Fields()) {
        if (!field.IsIndexed()) continue;
        int field_num = field_infos_->FieldNumber(field.Name());

        std::istringstream stream(field.Value());
        auto ts = analyzer_.CreateTokenStream(field.Name(), stream);
        analysis::Token token;
        int pos = 0;
        while (ts->Next(&token)) {
            Term term(field_num, token.Text());
            auto& doc_postings = postings_[term];
            if (doc_postings.size() <= static_cast<size_t>(doc_count_)) {
                doc_postings.resize(doc_count_ + 1);
            }
            auto& dp = doc_postings[doc_count_];
            dp.freq++;
            dp.positions.push_back(pos++);
        }
        if (field_tokens_per_doc_.size() <= static_cast<size_t>(doc_count_)) {
            field_tokens_per_doc_.resize(doc_count_ + 1);
        }
        if (field_tokens_per_doc_[doc_count_].size() <= static_cast<size_t>(field_num)) {
            field_tokens_per_doc_[doc_count_].resize(field_num + 1, 0);
        }
        field_tokens_per_doc_[doc_count_][field_num] = pos;
    }

    ++doc_count_;
    field_tokens_per_doc_.resize(doc_count_);
}

void DocumentWriter::Flush(const std::string& segment) {
    WriteFieldInfos(segment);
    WritePostings(segment);
}

void DocumentWriter::WriteFieldInfos(const std::string& segment) {
    field_infos_->Write(dir_, segment);
}

void DocumentWriter::WritePostings(const std::string& segment) {
    auto frq = dir_.CreateOutput(segment + ".frq");
    auto prx = dir_.CreateOutput(segment + ".prx");

    std::vector<int> field_num_tokens(field_infos_->Size(), 0);
    std::vector<std::pair<Term, TermInfo>> term_infos;
    term_infos.reserve(postings_.size());

    for (auto& [term, doc_postings] : postings_) {
        TermInfo ti;
        ti.doc_freq = 0;

        ti.freq_pointer = frq->FilePointer();

        int prev_doc = 0;
        for (int doc_id = 0; doc_id < doc_count_; ++doc_id) {
            if (doc_id < static_cast<int>(doc_postings.size()) && doc_postings[doc_id].freq > 0) {
                int delta = doc_id - prev_doc;
                frq->WriteVInt(delta);
                frq->WriteVInt(doc_postings[doc_id].freq);
                prev_doc = doc_id;
                ++ti.doc_freq;
                field_num_tokens[term.FieldNumber()] += doc_postings[doc_id].freq;
            }
        }

        ti.prox_pointer = prx->FilePointer();

        for (int doc_id = 0; doc_id < doc_count_; ++doc_id) {
            if (doc_id < static_cast<int>(doc_postings.size()) && doc_postings[doc_id].freq > 0) {
                int prev_pos = 0;
                for (int p : doc_postings[doc_id].positions) {
                    prx->WriteVInt(p - prev_pos);
                    prev_pos = p;
                }
            }
        }

        term_infos.push_back({term, ti});
    }

    frq->Close();
    prx->Close();

    TermInfosWriter tis_writer(dir_, segment);
    for (const auto& [term, ti] : term_infos) {
        tis_writer.Add(term, ti);
    }
    tis_writer.Close();

    auto nrm = dir_.CreateOutput(segment + ".nrm");
    for (int d = 0; d < doc_count_; ++d) {
        for (int f = 0; f < field_infos_->Size(); ++f) {
            int num_tokens = 0;
            if (d < static_cast<int>(field_tokens_per_doc_.size()) &&
                f < static_cast<int>(field_tokens_per_doc_[d].size())) {
                num_tokens = field_tokens_per_doc_[d][f];
            }
            float norm = CalcNorm(num_tokens);
            nrm->WriteByte(EncodeNorm(norm));
        }
    }
    nrm->Close();
}

}  // namespace index
}  // namespace minilucene

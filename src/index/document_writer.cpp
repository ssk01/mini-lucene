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
#include <map>
#include <sstream>
#include <vector>

namespace minilucene {
namespace index {

namespace {

struct Posting {
    int freq = 0;
    std::vector<int> positions;
};

uint8_t EncodeNorm(float norm) {
    if (norm >= 1.0f) return 255;
    if (norm <= 0.0f) return 0;
    return static_cast<uint8_t>(255.0f * norm);
}

}  // namespace

DocumentWriter::DocumentWriter(store::Directory& dir, analysis::Analyzer& analyzer)
    : dir_(dir), analyzer_(analyzer) {}

void DocumentWriter::AddDocument(const std::string& segment, const document::Document& doc) {
    FieldInfos field_infos;
    for (const auto& field : doc.Fields()) {
        field_infos.AddField(field);
    }
    field_infos.Write(dir_, segment);

    std::map<Term, Posting> postings;
    std::vector<int> field_num_tokens(field_infos.Size(), 0);

    for (const auto& field : doc.Fields()) {
        if (!field.IsIndexed()) continue;
        const auto* fi = field_infos.FieldByName(field.Name());
        int field_num = fi->Number();
        int num_tokens = 0;

        std::istringstream stream(field.Value());
        auto ts = analyzer_.CreateTokenStream(field.Name(), stream);
        analysis::Token token;
        int pos = 0;
        while (ts->Next(&token)) {
            Term term(field_num, token.Text());
            postings[term].freq++;
            postings[term].positions.push_back(pos++);
            ++num_tokens;
        }
        field_num_tokens[field_num] = num_tokens;
    }

    auto frq = dir_.CreateOutput(segment + ".frq");
    auto prx = dir_.CreateOutput(segment + ".prx");

    std::vector<std::pair<Term, TermInfo>> term_infos;
    term_infos.reserve(postings.size());

    for (auto& [term, posting] : postings) {
        TermInfo ti;
        ti.doc_freq = 1;

        ti.freq_pointer = frq->FilePointer();

        frq->WriteVInt(1);
        frq->WriteVInt(0);
        frq->WriteVInt(posting.freq);

        ti.prox_pointer = prx->FilePointer();

        int prev = 0;
        for (int p : posting.positions) {
            prx->WriteVInt(p - prev);
            prev = p;
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
    for (int i = 0; i < field_infos.Size(); ++i) {
        const auto* fi = field_infos.FieldByNumber(i);
        if (fi->IsIndexed()) {
            int num_tokens = field_num_tokens[i];
            float norm = (num_tokens > 0) ? (1.0f / std::sqrt(static_cast<float>(num_tokens))) : 0.0f;
            nrm->WriteByte(EncodeNorm(norm));
        }
    }
    nrm->Close();
}

}  // namespace index
}  // namespace minilucene

#include "minilucene/index/segment_infos.h"
#include "minilucene/store/directory.h"
#include "minilucene/store/index_input.h"
#include "minilucene/store/index_output.h"

namespace minilucene {
namespace index {

void SegmentInfos::Add(const std::string& name, int doc_count) {
    segments_.push_back({name, doc_count});
}

void SegmentInfos::Write(store::Directory& dir) {
    auto out = dir.CreateOutput("segments");
    out->WriteVInt(0);
    out->WriteVInt(Size());
    for (const auto& seg : segments_) {
        out->WriteString(seg.name);
        out->WriteVInt(seg.doc_count);
    }
    out->Close();
}

std::unique_ptr<SegmentInfos> SegmentInfos::Read(store::Directory& dir) {
    auto sis = std::make_unique<SegmentInfos>();
    if (!dir.FileExists("segments")) return sis;

    auto in = dir.OpenInput("segments");
    int version = in->ReadVInt();
    (void)version;
    int num = in->ReadVInt();
    for (int i = 0; i < num; ++i) {
        SegmentInfo si;
        si.name = in->ReadString();
        si.doc_count = in->ReadVInt();
        sis->segments_.push_back(si);
    }
    in->Close();
    return sis;
}

}  // namespace index
}  // namespace minilucene

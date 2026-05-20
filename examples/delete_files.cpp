#include "minilucene/index/index_reader.h"
#include "minilucene/index/segment_infos.h"
#include "minilucene/index/segment_reader.h"
#include "minilucene/store/fs_directory.h"

#include <iostream>

int main(int argc, char* argv[]) {
    std::string index_path = "demo index";
    if (argc > 1) index_path = argv[1];

    try {
        minilucene::store::FSDirectory dir(index_path);
        // Use IndexReader factory — handles single and multi-segment transparently
        auto seg_infos = minilucene::index::SegmentInfos::Read(dir);
        if (seg_infos->Segments().empty()) {
            std::cerr << "no segments in index" << std::endl;
            return 1;
        }

        // For now, read first segment (matching Java demo simplicity)
        auto& seg_name = seg_infos->Segments()[0].name;
        minilucene::index::SegmentReader reader(dir, seg_name);

        for (int i = 0; i < reader.MaxDoc(); ++i) {
            reader.Delete(i);
        }

        reader.Close();
        dir.Close();

    } catch (const std::exception& e) {
        std::cout << " caught a " << typeid(e).name()
                  << "\n with message: " << e.what() << std::endl;
    }

    return 0;
}

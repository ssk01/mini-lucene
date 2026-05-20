#include "minilucene/index/segment_reader.h"
#include "minilucene/index/segment_infos.h"
#include "minilucene/store/fs_directory.h"

#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    std::string index_path = "index";
    if (argc > 1) index_path = argv[1];

    try {
        minilucene::store::FSDirectory dir(index_path);
        auto seg_infos = minilucene::index::SegmentInfos::Read(dir);
        if (seg_infos->Segments().empty()) {
            std::cerr << "no segments in index" << std::endl;
            return 1;
        }

        std::string seg_name = seg_infos->Segments()[0].name;
        minilucene::index::SegmentReader reader(dir, seg_name);

        int max_doc = reader.MaxDoc();
        for (int i = 0; i < max_doc; ++i) {
            reader.Delete(i);
        }

        std::cout << "deleted " << max_doc << " documents" << std::endl;
        reader.Close();

    } catch (const std::exception& e) {
        std::cerr << "caught a " << typeid(e).name()
                  << "\n with message: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}

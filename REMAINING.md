# mini-lucene 剩余工作清单

> 基于 PROJECT.md、lucene-1.0.1-architecture.md、BUILD.bazel 及源码审计整理。

---

## 1. 演示程序 (Demo Programs)

需要将 Java 版 `IndexFiles`、`SearchFiles`、`FileDocument` 移植为 C++ 可执行目标。

| Item | Priority | Status | Notes |
|---|---|---|---|
| FieldsWriter/FieldsReader (`index`) | P4 | ❌ 缺失 | `.fdt`/`.fdx` 的写入/读取；当前 `DocumentWriter` 跳过存储字段；当前无头文件、无实现 |
| `Field::Text(name, istream&)` 重载 | P3 | ❌ 缺失 | `document/field.h` 只提供 `Text(name, string&)`，缺少 Reader 版；`FileDocument.java:103` 需要此接口 |
| `IndexWriter` 多 segment + `mergeFactor` | P4/P9 | ⚠️ 部分实现 | `index_writer.h` 无 `mergeFactor` 成员；每次 `AddDocument` 写一个 seg 但不自动合并 |
| `IndexWriter::Optimize()` 完整实现 | P9 | ⚠️ 部分实现 | 头文件有声明，但缺少实际多 segment 合并到单个 segment 的归并逻辑验证 |
| `examples/p1_demo` 为 P1 仅 | infra | ✅ 已存在 | 仅演示 `BitVector` 和 `PriorityQueue` |
| 演示目标 `index_files` | demo | ❌ 缺失 | BUILD.bazel 中无 `cc_binary` 目标；需 `IndexWriter` + `FileDocument` + `StopAnalyzer` + CLI args |
| 演示目标 `search_files` | demo | ❌ 缺失 | BUILD.bazel 中无 `cc_binary` 目标；需 `IndexSearcher` + `QueryParser` + interactive CLI |
| 演示目标 `file_document` 库 | demo | ❌ 缺失 | FileDocument 作为静态库供上面两个 demo 链接；需 `Field::Text(name, istream&)` + `DateField` |

---

## 2. 核心缺失功能

| Item | Priority | Status | Notes |
|---|---|---|---|
| SegmentsReader (多 segment 搜索) | P9 | ❌ 缺失 | `index_reader.h` 定义了抽象接口，`SegmentReader` 读单个 seg；缺少组合多个 SegmentReader 的实现 |
| FieldsWriter (写入 .fdt/.fdx) | P4 | ❌ 缺失 | 头文件/实现均不存在；当前文档的 stored field 不会被持久化 |
| FieldsReader (读取 .fdt/.fdx) | P5 | ❌ 缺失 | 头文件/实现均不存在；`IndexReader` 无 `Document(docID)` 方法 |
| PorterStemmer | 选做 | ❌ 缺失 | 三个子模块均无头文件/实现 |
| PorterStemFilter | 选做 | ❌ 缺失 | |
| StandardAnalyzer | 选做 | ❌ 缺失 | 头文件 `standard_analyzer.h` 不存在；需 StandardTokenizer（手写 FSM）+ StandardFilter + LowerCaseFilter + StopFilter |
| 删除支持（`IndexReader::Delete`） | 选做 | ❌ 缺失 | `BitVector` 已实现但 `Write/Read` 序列化未实现；`IndexReader` 无 `Delete(int docID)` |
| MultiSearcher | 选做 | ❌ 缺失 | 合并多个 `IndexSearcher` 结果 |
| `SegmentMergeQueue` / `SegmentMergeInfo` | P9 | ⚠️ 部分实现 | `SegmentMerger` 头文件/实现存在但 `SegmentMergeQueue` 和 `SegmentMergeInfo` 独立类未单独提取 |
| `BitVector::Write/Read` 序列化 | P1/选做 | ❌ 缺失 | `bit_vector.h` 无 `Write(OutputStream&)`/`Read(InputStream&)` 方法；2个项目依赖此功能 |
| `Similarity::Norm` 编码（1 字节量化） | P6 | ⚠️ 部分实现 | `similarity.h` 存在；需确认 `norm` 是否已实现为 `255/sqrt(numTerms)` 并写入 `.nrm` |
| `Hits` 延迟加载搜索结果类 | P6 | ❌ 缺失 | `TopDocs`/`ScoreDoc` 头文件存在但 `Hits` 包装类缺失；Java demo 中依赖 `hits.doc(i)` 按需加载文档 |
| `DateField` | P3 | ❌ 缺失 | `FileDocument.java:96` 使用 `DateField.timeToString()`；C++ 中无等效实现 |
| `FieldSelector` / 按需加载文档 | P5 | ❌ 缺失 | 无头文件/实现 |

---

## 3. 选做项

| Item | Priority | Status | Notes |
|---|---|---|---|
| 删除支持（`IndexReader::Delete` + BitVector 持久化 + 合并时物理删除） | ★★★☆☆ | ❌ 缺失 | 需 `BitVector::Write/Read` 先行 |
| StandardAnalyzer（手写 FSM） | ★★★★☆ | ❌ 缺失 | 需识别 email/URL/缩写 |
| PorterStemmer（经典 5 步算法） | ★★★☆☆ | ❌ 缺失 | 直接照搬 Porter 算法 |
| MultiSearcher（跨多个 IndexSearcher） | ★★☆☆☆ | ❌ 缺失 | 合并多索引结果 |
| 中文支持（CJKAnalyzer 或集成 jieba） | ★★★★☆ | ❌ 缺失 | 双字切分 |
| mmap FSDirectory（替代 fstream） | ★★★☆☆ | ❌ 缺失 | 性能对比 |
| 性能基准测试（Google Benchmark） | ★★☆☆☆ | ❌ 缺失 | 索引/查询吞吐量曲线 |

---

## 4. 基础设施

| Item | Priority | Status | Notes |
|---|---|---|---|
| `tests/data/` 测试语料目录 | infra | ❌ 缺失 | 缺少 `corpus_small.txt`、`corpus_medium.txt`、`expected_results.json` |
| `tests/data/corpus_small.txt` | infra | ❌ 缺失 | 20 篇英文短文 |
| `tests/data/corpus_medium.txt` | infra | ❌ 缺失 | 500 篇维基百科段落 |
| `tests/data/expected_results.json` | infra | ❌ 缺失 | 人工标注的查询→期望命中 docID |
| 黄金语料回归测试 | infra | ❌ 缺失 | 每次提交自动运行 |
| 覆盖率测量（gcov / llvm-cov） | infra | ❌ 缺失 | PROJECT.md 要求 ≥ 80%（P6+），BUILD.bazel 无相关配置或脚本 |
| clang-tidy 配置 | infra | ❌ 缺失 | 项目根目录无 `.clang-tidy`；需启用 `bugprone-*`, `performance-*`, `cppcoreguidelines-*` 子集 |
| CMake 构建（替代 Bazel） | infra | ⚠️ 未就绪 | PROJECT.md 指定 CMake ≥ 3.16；当前项目仅配置 Bazel；CMakeLists.txt 不存在 |
| Java 交叉验证脚本 | infra | ❌ 缺失 | 需 Java 小程序输出 Lucene 1.0.1 索引的 term/postings JSON；C++ 输出同样格式；`diff` 比较 |
| ASan / valgrind 内存检查 CI | infra | ❌ 缺失 | 编译/测试未配置 AddressSanitizer |
| `-Wall -Wextra -Wpedantic` 零警告 | infra | ⚠️ 待验证 | Bazel 编译选项中未明确开启 |

---

## 5. 已发现的 Bug

| Item | Priority | Status | Notes |
|---|---|---|---|
| StopFilter missing "so" (33 vs 34 stop words) | P3 | 🐛 已确认 | `stop_filter.cpp:8-14` 列表有 33 个词；Java Lucene 1.0.1 StopAnalyzer 为 34 个，缺少 "so"；影响搜索召回率 |
| LetterTokenizer 位置追踪偏移 | P3 | 🐛 待确认 | `letter_tokenizer.cpp:29` 中 `current_pos = pos_` 在内层循环末尾读取分隔符后赋值，使 end_offset 可能包含分隔符位置而非正确的 token 结束位置 |
| BooleanQuery MUST_NOT 无 MUST 子句 | P7 | 🐛 待确认 | 当 BooleanQuery 仅有 MUST_NOT 子句时（无 MUST/SHOULD），行为未定义；Lucene 1.0.1 要求至少一个非 MUST_NOT 子句，否则抛出异常 |
| WildcardQuery 暴力全量扫描 | P10 | 🐛 待确认 | `wildcard_query.cpp` 枚举词典中所有 term 并与通配符模式匹配；应使用 `FilteredTermEnum` 模式按需过滤 |
| FuzzyQuery 暴力全量扫描 | P10 | 🐛 待确认 | `fuzzy_query.cpp` 枚举词典中所有 term 计算编辑距离；应使用 `FilteredTermEnum` 模式按需过滤 |
| IndexWriter 关闭后仍可 AddDocument | P4 | 🐛 待确认 | `index_writer.h:40` `closed_` 标记存在但 `AddDocument()` 方法未检查此标记；调用 `Close()` 后仍可继续写入，导致无声数据丢失 |
| 当前只有 Bazel 构建，无 CMake | infra | ⚠️ 无 Bug | 不冲突，但 PROJECT.md 要求 CMake；两个构建系统应等效 |

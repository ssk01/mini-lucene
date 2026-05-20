# mini-lucene (C++ 重写) —— 项目文档

> **目标**：以 Lucene 1.0.1（Java）为蓝本，用现代 C++ 从零实现一个可用的小型全文检索引擎。
> **定位**：大学生一学期（14–16 周）大作业 / 教学项目。重在理解原理，不追求性能极致。
> **参考**：见 [`lucene-1.0.1-architecture.md`](./lucene-1.0.1-architecture.md)。

---

## 0. 总体设计约束

| 项 | 选型 | 理由 |
|---|---|---|
| 语言 | C++17 | `std::filesystem`、`std::optional`、结构化绑定够用，编译器支持广泛 |
| 构建系统 | CMake ≥ 3.16 | 跨平台、IDE 友好 |
| 测试框架 | GoogleTest | 工业标准、断言丰富、支持参数化测试 |
| 依赖 | 仅 STL + GoogleTest | 训练手写基础设施，不引入 Boost / Folly |
| 命名空间 | `minilucene::store / document / analysis / index / search` | 与 Java 包结构一一对应 |
| 编码风格 | Google C++ Style + 4 空格缩进 | 见 `STYLE.md`（学生需另行整理） |
| 字符集 | UTF-8（内部统一用 `std::string`） | Lucene 1.0.1 用 Java 的 `String`/UTF-16；本项目简化为 UTF-8，仅切英文 |
| 目标平台 | Linux / macOS | Windows 暂不保证 |

**总体目录结构（建议）：**

```
mini-lucene/
├── CMakeLists.txt
├── PROJECT.md                  # 本文档
├── lucene-1.0.1-architecture.md
├── lucene/                     # 原始 Java 源码（参考用）
├── include/minilucene/         # 公共头文件
│   ├── store/
│   ├── document/
│   ├── analysis/
│   ├── index/
│   └── search/
├── src/                        # 实现
├── tests/                      # 单元测试 + 集成测试
│   ├── unit/
│   ├── integration/
│   └── data/                   # 测试用文本语料
├── examples/                   # 演示程序
└── third_party/googletest/
```

---

## 1. 阶段拆分总览

| 阶段 | 主题 | 周次 | 难度 | 对应 Java 包 |
|---|---|---|---|---|
| **P1** | 基础设施 + 工具类 | W1–W2 | ★☆☆☆☆ | `util` |
| **P2** | 存储抽象层 | W2–W3 | ★★☆☆☆ | `store` |
| **P3** | 文档模型 + 分析器管线 | W3–W4 | ★★☆☆☆ | `document` + `analysis` |
| **P4** | 单 segment 索引写入 | W5–W7 | ★★★★☆ | `index`（写） |
| **P5** | 单 segment 索引读取 | W7–W9 | ★★★☆☆ | `index`（读） |
| **P6** | TermQuery + TF-IDF 评分 | W9–W10 | ★★★☆☆ | `search`（核心） |
| **P7** | BooleanQuery | W10–W11 | ★★★☆☆ | `search`（布尔） |
| **P8** | PhraseQuery + 位置 | W11–W12 | ★★★★☆ | `search`（短语） |
| **P9** | 多 segment + 合并 | W12–W13 | ★★★★☆ | `index`（合并） |
| **P10** | 高级查询 + 简单查询解析器 | W13–W14 | ★★★★☆ | `search` + `queryParser` |
| **★ 选做** | 删除 / StandardAnalyzer / Porter 词干 / MultiSearcher | W15+ | ★★★★★ | 各模块扩展 |

**每阶段交付物统一标准：**

1. ✅ 源代码（含头文件 + 实现）
2. ✅ 单元测试覆盖率 ≥ **80%**（用 `gcov`/`llvm-cov` 测量）
3. ✅ 通过本阶段的"验收用例"（在文档中明确列出）
4. ✅ 一段 README 段落：说明本阶段做了什么、关键设计决策、踩过的坑
5. ✅ 一个可运行的 demo 程序（放在 `examples/`）

---

## 2. 阶段详细规划

### 阶段 P1：基础设施 + 工具类

#### 目标
搭建 CMake 项目骨架，引入 GoogleTest，实现两个核心数据结构作为热身。

#### 实现清单

| 类 | 头文件 | 关键方法 |
|---|---|---|
| `util::BitVector` | `include/minilucene/util/bit_vector.h` | `BitVector(size_t)`, `set(int)`, `clear(int)`, `get(int) const`, `count() const`, `Write(OutputStream&)`, `Read(InputStream&)` |
| `util::PriorityQueue<T>` | `include/minilucene/util/priority_queue.h` | 小顶堆模板类，`Put(T)`, `Top() const`, `Pop()`, `Size()`，子类重写 `LessThan(const T&, const T&)` |

> **学习要点**：PriorityQueue 是后面 Top-K 检索和多路归并的基石；BitVector 后期用于删除标记。

#### 验证用例（必须通过的单测）

```cpp
// tests/unit/util/bit_vector_test.cpp
TEST(BitVector, SetAndGet) { ... 1000 位反复 set/clear/get ... }
TEST(BitVector, Count)     { ... 随机 set N 位，验证 count() == N ... }
TEST(BitVector, BoundaryBits) { ... 测 bit 0、bit 7、bit 8、bit 最后一位 ... }
TEST(BitVector, OutOfRangeThrows) { ... 越界访问抛异常 ... }

// tests/unit/util/priority_queue_test.cpp
TEST(PriorityQueue, MinHeapInvariant) { ... 插入乱序 1000 个，依次 pop 应升序 ... }
TEST(PriorityQueue, FixedSizeTopK)    { ... 容量 10，插入 100 个，最终留下最大的 10 个 ... }
TEST(PriorityQueue, PutWithEvict)     { ... 满后 put 时若小于堆顶则丢弃 ... }
```

#### 验收标准
- `cmake -B build && cmake --build build && ctest --test-dir build` 全绿
- 覆盖率 ≥ 80%

---

### 阶段 P2：存储抽象层

#### 目标
实现 Lucene 最重要的设计之一：把"文件系统"抽象成接口，使索引可以同时存在磁盘 (FSDirectory) 和内存 (RAMDirectory) 中。

#### 实现清单

| 类 | 职责 |
|---|---|
| `store::IndexInput` (abstract) | 随机读取流，支持 `ReadByte / ReadVInt / ReadVLong / ReadString / Seek / FilePointer / Length` |
| `store::IndexOutput` (abstract) | 缓冲写入流，支持 `WriteByte / WriteVInt / WriteVLong / WriteString / Flush` |
| `store::Directory` (abstract) | `List / FileExists / FileLength / DeleteFile / RenameFile / CreateOutput / OpenInput` |
| `store::RAMDirectory` | 用 `std::map<string, vector<uint8_t>>` 实现 |
| `store::FSDirectory` | 基于 `std::filesystem` + `std::fstream` / `mmap`（二选一） |

> **关键算法**：手写 **VInt / VLong 编码**（每字节 7 bit 数据 + 1 bit 续位）。这是 Lucene 节省倒排表空间的核心技巧。

#### 验证用例

```cpp
// tests/unit/store/vint_test.cpp
TEST(VInt, RoundTrip) {
  // 关键边界：0, 1, 127 (1B), 128 (2B), 16383 (2B), 16384 (3B), 2^31-1 (5B)
  for (int v : {0, 1, 127, 128, 16383, 16384, INT_MAX}) {
    RAMDirectory dir; auto* out = dir.CreateOutput("x");
    out->WriteVInt(v); out->Close();
    auto* in = dir.OpenInput("x");
    EXPECT_EQ(in->ReadVInt(), v);
  }
}

// tests/unit/store/ram_directory_test.cpp
TEST(RAMDirectory, ListReflectsCreatedFiles)    { ... }
TEST(RAMDirectory, ReadWriteRoundTrip)          { ... 写 1MB 随机数据，读回比对 ... }
TEST(RAMDirectory, DeleteFile)                  { ... }
TEST(RAMDirectory, SeekBackwardAndForward)      { ... 写 1000 个 int，乱序 seek 读 ... }

// tests/unit/store/fs_directory_test.cpp
TEST(FSDirectory, SameContractAsRAM) {
  // 用 TYPED_TEST 让 RAM 和 FS 共享同一组测试用例
}
```

> **教学技巧**：用 GoogleTest 的 `TYPED_TEST` 让 RAM 和 FS 跑同一套测试，证明它们行为一致——这就是接口抽象的力量。

#### 验收标准
- VInt 编解码对所有边界值（0、127、128、16383、16384、2^31-1）正确
- RAM 和 FS 通过同一套类型化测试

---

### 阶段 P3：文档模型 + 分析器管线

#### 目标
建立"文档 → token 流"的处理链路。

#### 实现清单

**document 子模块**：

| 类 | 职责 |
|---|---|
| `document::Field` | name + value + 三个 bool 标志（stored/indexed/tokenized） |
| `document::Document` | `Add(Field)`, `GetField(name)`, `Fields()` 迭代器 |

工厂方法（建议设计为静态方法）：
```cpp
Field::Keyword(name, value);   // stored=Y indexed=Y tokenized=N
Field::Text(name, value);      // stored=Y indexed=Y tokenized=Y
Field::UnIndexed(name, value); // stored=Y indexed=N tokenized=N
Field::UnStored(name, value);  // stored=N indexed=Y tokenized=Y
```

**analysis 子模块**：

| 类 | 职责 |
|---|---|
| `analysis::Token` | `text`, `start_offset`, `end_offset`, `type` |
| `analysis::TokenStream` (abstract) | `bool Next(Token*)` |
| `analysis::Tokenizer` (abstract) : TokenStream | 从 `std::istream&` 切词 |
| `analysis::TokenFilter` (abstract) : TokenStream | 包装另一个 TokenStream |
| `analysis::LetterTokenizer` | 按非字母字符切分 |
| `analysis::LowerCaseFilter` | 全部转小写 |
| `analysis::StopFilter` | 用 `std::unordered_set` 过滤停用词 |
| `analysis::Analyzer` (abstract) | `TokenStream* TokenStream(field, Reader)` |
| `analysis::SimpleAnalyzer` | LetterTokenizer + LowerCaseFilter |
| `analysis::StopAnalyzer` | + StopFilter（内置 33 个英文停用词） |

#### 验证用例

```cpp
// tests/unit/analysis/simple_analyzer_test.cpp
TEST(SimpleAnalyzer, BasicSplit) {
  auto tokens = TokenizeAll(SimpleAnalyzer(), "Hello, World!");
  EXPECT_EQ(tokens, (vector<string>{"hello", "world"}));
}
TEST(SimpleAnalyzer, OffsetsCorrect) {
  // "Hello World" → token "hello" offset [0,5), "world" offset [6,11)
}
TEST(SimpleAnalyzer, EmptyInput)         { ... 返回 0 token ... }
TEST(SimpleAnalyzer, OnlyDelimiters)     { "!!! ,,," → 0 token }
TEST(SimpleAnalyzer, UnicodeNotCrash)    { "café résumé" 不崩，行为可记录 }

// tests/unit/analysis/stop_filter_test.cpp
TEST(StopFilter, RemovesStopWords) {
  // "the quick brown fox" with stop={the,a,an} → {quick,brown,fox}
}

// tests/unit/document/document_test.cpp
TEST(Document, FactoryMethodFlags) {
  auto f = Field::Keyword("url", "http://x");
  EXPECT_TRUE(f.IsStored());  EXPECT_TRUE(f.IsIndexed());
  EXPECT_FALSE(f.IsTokenized());
}
```

#### 验收标准
- 三种 Analyzer 在 `tests/data/` 中的 5 个英文短文档上输出 token 序列与人工标注一致
- 测试覆盖 stored/indexed/tokenized 全部 8 种组合的有效性检查

---

### 阶段 P4：单 segment 索引写入 ⭐ 核心阶段

#### 目标
**最难的一个阶段**。把"一个 Document" 写入磁盘上的一个 segment（多个文件），实现完整的倒排索引。

#### 索引文件格式（参考 Lucene）

| 文件 | 内容 |
|---|---|
| `<seg>.fnm` | 字段元信息 |
| `<seg>.fdt` / `.fdx` | 存储字段的原文 |
| `<seg>.tis` / `.tii` | 词典主文件 + 二级索引（每 128 项采样） |
| `<seg>.frq` | 词频文件 `(docID, freq)` 列表 |
| `<seg>.prx` | 位置文件 |
| `<seg>.nrm` | 归一化因子（每字段每文档 1 字节） |
| `segments` | segment 列表元数据 |

> **简化建议**：第一版可以**先不实现 `.fdt/.fdx`**（即先不支持原文检索回显），让学生集中精力理解倒排结构。后续补回。

#### 实现清单

| 类 | 职责 |
|---|---|
| `index::FieldInfos` / `FieldInfo` | 字段号 ↔ 字段名映射 |
| `index::Term` | `{field, text}`，定义全序 |
| `index::TermInfo` | `{doc_freq, freq_pointer, prox_pointer}` |
| `index::Posting` | 单文档内某 Term 的 `{freq, positions[]}` |
| `index::DocumentWriter` | 把 Document → 单文档 segment |
| `index::TermInfosWriter` | 写 .tis + .tii，每 128 项采样一条 |
| `index::FieldsWriter` | 写 .fdt + .fdx（选做） |
| `index::IndexWriter` | 用户入口：`AddDocument(doc)`，多次调用产生多个 segment |
| `index::SegmentInfos` | 维护 segments 文件 |

#### 倒排算法（关键流程）

```
for each indexed Field in doc:
  TokenStream ts = analyzer.tokenStream(field, value);
  pos = 0;
  while (ts.next(&token)) {
    Term t{field, token.text};
    Posting* p = hashtable[t]; if (!p) p = new Posting();
    p->freq++;
    p->positions.push_back(pos++);
  }
sort hashtable.keys();
write .frq (VInt deltas) and .prx (VInt position deltas);
write .tis with (term, doc_freq, freq_ptr, prox_ptr);
sample every 128th into .tii;
```

> **关键技巧**：positions 和 docIDs 都用 **delta + VInt** 编码（差值变小，VInt 更省）。

#### 验证用例

```cpp
// tests/unit/index/document_writer_test.cpp
TEST(DocumentWriter, SingleDocSegmentFilesCreated) {
  RAMDirectory dir;
  Document doc; doc.Add(Field::Text("body", "the quick brown fox"));
  DocumentWriter(dir, SimpleAnalyzer()).AddDocument("_0", doc);

  // 必须生成的文件
  for (auto suffix : {".fnm", ".tis", ".tii", ".frq", ".prx", ".nrm"}) {
    EXPECT_TRUE(dir.FileExists("_0" + suffix));
  }
}

TEST(DocumentWriter, TermsSortedAlphabetically) {
  // 写一个含 "quick brown fox" 的文档
  // 直接读 .tis 文件，验证 term 按字典序排列：brown, fox, quick
}

TEST(DocumentWriter, FrequencyEncodedCorrectly) {
  // doc: "fox fox fox cat" → fox.freq=3, cat.freq=1
  // 读 .frq 解码验证
}

TEST(DocumentWriter, PositionsRecorded) {
  // doc: "a b a b a" → "a" 位置 [0,2,4], "b" 位置 [1,3]
}

TEST(DocumentWriter, TiiSamplesEvery128Terms) {
  // 构造一个含 1000 个不同词的文档，验证 .tii 有约 8 个采样
}
```

#### 验收标准
- 用 `xxd` 手动检查产出文件，结构与 Lucene Java 输出**类似**（不要求二进制一致）
- 在 100 个文档（每个 100 词）的语料上能跑通，无 crash 无 OOM

---

### 阶段 P5：单 segment 索引读取

#### 目标
把 P4 写下去的字节读回来，提供枚举 API。

#### 实现清单

| 类 | 职责 |
|---|---|
| `index::IndexReader` (abstract) | `Terms()` / `TermDocs(t)` / `TermPositions(t)` / `DocFreq(t)` / `NumDocs()` |
| `index::SegmentReader` : IndexReader | 读单个 segment |
| `index::TermEnum` (abstract) | `Next()`, `Term()`, `DocFreq()` |
| `index::TermDocs` (abstract) | `Next()`, `Doc()`, `Freq()`, `Seek(Term)` |
| `index::TermPositions` : TermDocs | `NextPosition()` |
| `index::TermInfosReader` | 加载 .tii 到内存，按需读 .tis |

> **学习要点**：词典查找算法 = **二分查 .tii → 顺序扫 .tis 最多 128 步**。

#### 验证用例（写-读往返测试）

```cpp
// tests/integration/write_read_roundtrip_test.cpp
TEST(WriteRead, EnumerateAllTerms) {
  RAMDirectory dir;
  IndexWriter w(dir, SimpleAnalyzer());
  w.AddDocument(MakeDoc("body", "alpha beta gamma"));
  w.AddDocument(MakeDoc("body", "beta gamma delta"));
  w.Close();

  SegmentReader r(dir, "_0");
  auto terms = CollectAllTerms(r);
  EXPECT_EQ(terms, (vector<string>{"alpha","beta","delta","gamma"}));
}

TEST(WriteRead, TermDocsIteration) {
  // "beta" 在 doc 0 freq=1, doc 1 freq=1 → 验证迭代
}

TEST(WriteRead, TermPositionsExact) {
  // 写 "a b a"，读 "a" 的 positions = [0, 2]
}

TEST(WriteRead, SeekToMidDictionary) {
  // 1000 个 term，seek 到第 500 个，验证一步到位
}
```

#### 验收标准
- 任何 P4 写下去的索引都能被 P5 完整读回
- 词典 seek 性能：1 万 term 中查找耗时 < 1ms

---

### 阶段 P6：TermQuery + TF-IDF 评分

#### 目标
打通"搜索 → 评分 → Top-K 结果"的最短路径。

#### TF-IDF 公式（必须实现）

```
score(q,d) = Σ_t  tf(freq) × idf(t)² × norm(field,d)
其中：
  tf(freq)   = sqrt(freq)
  idf(t)     = log(maxDoc / (docFreq+1)) + 1
  norm(f,d)  = 1 / sqrt(numTermsInField)  // 量化到 1 字节
```

#### 实现清单

| 类 | 职责 |
|---|---|
| `search::Similarity` | TF/IDF/norm 计算 |
| `search::Query` (abstract) | `Weight(Searcher)`, `Rewrite(Reader)`, `ToString()` |
| `search::Weight` | 查询级别预算好的权重值 |
| `search::Scorer` (abstract) | `Next()`, `Doc()`, `Score()` |
| `search::TermQuery` | 包装一个 Term |
| `search::TermScorer` | 遍历 TermDocs，逐文档评分 |
| `search::HitCollector` (callback) | `Collect(doc, score)` |
| `search::HitQueue` : PriorityQueue | Top-K 收集 |
| `search::TopDocs` | 结果封装 |
| `search::IndexSearcher` | `Search(Query, top_k) → TopDocs` |

#### 验证用例

```cpp
// tests/integration/search_termquery_test.cpp
TEST(TermQuery, RankByFrequency) {
  // 三个文档：
  //   doc0: "fox"
  //   doc1: "fox fox"
  //   doc2: "fox fox fox"
  // 查 "fox" → 排名 doc2 > doc1 > doc0
}

TEST(TermQuery, IdfPrefersRareTerms) {
  // 100 个文档都含 "the"，只有 1 个含 "quantum"
  // 查 "quantum" 在它唯一所在的文档上得分 >> "the" 在任意文档上的得分
}

TEST(TermQuery, NoMatchReturnsEmpty) { ... }

TEST(TermQuery, FieldLengthNormalization) {
  // 短文档 "fox" 比长文档 "fox a b c d e f" 评分高
}
```

#### 验收标准
- 在 100 文档语料上前 10 结果与人工排序一致
- Searcher 单次查询耗时 < 10ms

---

### 阶段 P7：BooleanQuery

#### 目标
支持 `+required -prohibited optional` 三元布尔组合。

#### 实现清单

| 类 | 职责 |
|---|---|
| `search::BooleanClause` | `{Query, Occur}` 其中 `Occur ∈ {SHOULD, MUST, MUST_NOT}` |
| `search::BooleanQuery` | 容器 + 限制最多 32 个子句 |
| `search::BooleanScorer` | 多路 Scorer 协作，**coord 因子**奖励多匹配 |

#### 算法关键
- `MUST_NOT`：用 BitVector 标记需要剔除的文档
- `MUST`：所有 must 的 Scorer 都要前进到同一 docID
- `SHOULD`：累加分数 + coord 系数 = matched/total

#### 验证用例

```cpp
TEST(BooleanQuery, MustAndShould) {
  // doc0: "fox"     doc1: "fox jumps"     doc2: "jumps"
  // 查 +fox jumps  →  doc1, doc0 命中；doc2 不命中
  // doc1 应排第一（匹配两个子句，coord=2/2=1.0）
}
TEST(BooleanQuery, MustNotExcludes) {
  // 查 fox -lazy → 含 lazy 的文档被排除
}
TEST(BooleanQuery, MaxClauseLimit) {
  // 添加第 33 个子句应抛 TooManyClauses 异常
}
TEST(BooleanQuery, CoordinationFactor) {
  // 验证匹配 2/3 子句的文档评分 < 匹配 3/3 的文档
}
```

#### 验收标准
- 上述布尔逻辑全部正确
- coord 因子使匹配更多子句的文档总是排在前面

---

### 阶段 P8：PhraseQuery + 位置匹配

#### 目标
实现"双引号短语搜索"，并支持 `slop`（位置容差）。

#### 实现清单

| 类 | 职责 |
|---|---|
| `search::PhraseQuery` | `Add(Term)`, `SetSlop(int)` |
| `search::PhrasePositions` | 跟踪某个 term 在文档中的位置游标 |
| `search::ExactPhraseScorer` | slop=0，相邻 position 必须连续 |
| `search::SloppyPhraseScorer` | slop>0，position 差距在 slop 范围内 |

#### 算法关键
**Exact 匹配**：对每个候选文档，把所有 term 的 positions 拉出来，看是否存在 `pos₀, pos₀+1, pos₀+2, ...` 这样的连续序列。

**Sloppy 匹配**：用 PriorityQueue 按 position 排序所有 (term, position) 对，找出最紧凑窗口。

#### 验证用例

```cpp
TEST(PhraseQuery, ExactMatch) {
  // doc0: "the quick brown fox"
  // doc1: "the quick fox brown" (词序不同)
  // 查 "quick brown" → 只命中 doc0
}
TEST(PhraseQuery, SlopAllowsReordering) {
  // 查 "brown quick"~2 → 命中 doc0（slop=2 允许调换）
}
TEST(PhraseQuery, NoMatchReturnsEmpty) { ... }
TEST(PhraseQuery, FrequencyAffectsScore) {
  // 短语在一个文档中出现 3 次 vs 1 次，3 次评分高
}
```

#### 验收标准
- 与 Java Lucene 在同样 slop 设置下的命中集**一致**

---

### 阶段 P9：多 segment + 合并

#### 目标
多次 `AddDocument` 会产生多个 segment，需要支持跨 segment 搜索和 `Optimize()` 合并。

#### 实现清单

| 类 | 职责 |
|---|---|
| `index::SegmentsReader` | 组合多个 SegmentReader，docID 偏移映射 |
| `index::SegmentMerger` | 合并多个 segment 成一个 |
| `index::SegmentMergeQueue` | PriorityQueue，多路归并词典 |
| `index::SegmentMergeInfo` | 每个 segment 的归并游标 |
| `IndexWriter::Optimize()` | 把所有 segment 合并为一个 |
| `IndexWriter` 自动合并策略 | 每 10 个小 segment 自动合并 |

#### 验证用例

```cpp
TEST(MultiSegment, SearchAcrossSegments) {
  // 写 doc0 → close → 写 doc1 → close
  // 索引中有两个 segment，搜索应能同时命中
}

TEST(SegmentMerger, MergedIndexEquivalentToSingle) {
  // 方案 A：一次性写 5 个文档（1 个 segment）
  // 方案 B：分 5 次写（5 个 segment）+ Optimize
  // 两者搜索结果（命中集 + 评分）应完全一致
}

TEST(SegmentMerger, DocIdRemapping) {
  // 验证合并后的 docID 是按 segment 顺序重排的
}
```

#### 验收标准
- 100 个 segment 合并后搜索结果与未合并时**一致**
- Optimize 后磁盘占用应**下降**（去重 term）

---

### 阶段 P10：高级查询 + 简单查询解析器

#### 目标
实现 Prefix/Wildcard/Fuzzy 三种"扩展查询"，并写一个**手写递归下降**的 query parser（不要用 JavaCC/ANTLR，自己写）。

#### 实现清单

| 类 | 职责 |
|---|---|
| `search::MultiTermQuery` (abstract) | 基类，子类提供 `GetEnum(Reader)` |
| `search::FilteredTermEnum` (abstract) | 过滤式词典枚举器 |
| `search::PrefixQuery` | 枚举所有以 prefix 开头的 term，展开为 BooleanQuery |
| `search::WildcardQuery` | `*` 和 `?` 通配符 |
| `search::FuzzyQuery` | 编辑距离 ≤ 2 |
| `queryParser::QueryParser` | 递归下降解析器 |

#### 查询语法（简化版，可对照 Java 版裁剪）

```
Query     := Clause (Clause)*
Clause    := ('+' | '-')? (Field ':')? Term
Term      := Word | Phrase | Word'*' | Word'~'
Field     := identifier
```

#### 验证用例

```cpp
TEST(PrefixQuery, MatchesPrefix) {
  // terms: cat, car, card, dog
  // PrefixQuery("ca") → 命中 cat, car, card 所在的文档
}

TEST(WildcardQuery, StarMatchesAny) {
  // "c*t" → cat, coat, etc.
}

TEST(FuzzyQuery, EditDistance) {
  // "fox" → 命中 "box" (距离1), "fix" (距离1)，不命中 "puppy"
}

TEST(QueryParser, BasicSyntax) {
  EXPECT_EQ(Parse("hello").ToString(),               "body:hello");
  EXPECT_EQ(Parse("+fox -lazy").ToString(),          "+body:fox -body:lazy");
  EXPECT_EQ(Parse("title:fox body:dog").ToString(),  "title:fox body:dog");
  EXPECT_EQ(Parse("\"quick brown\"").ToString(),     "body:\"quick brown\"");
  EXPECT_EQ(Parse("fox~").ToString(),                "body:fox~0.5");
  EXPECT_EQ(Parse("hel*").ToString(),                "body:hel*");
}
```

#### 验收标准
- 手写解析器能解析 Lucene 文档中给的所有示例查询
- Prefix/Wildcard 查询命中集与"暴力枚举所有 term" 的结果一致

---

## 3. 选做加分项（每项 +5 分）

| 题目 | 难度 | 说明 |
|---|---|---|
| 删除支持 | ★★★☆☆ | `IndexReader.Delete(docID)` + BitVector 持久化 + 合并时物理删除 |
| StandardAnalyzer | ★★★★☆ | 不用 JavaCC，手写有限状态机识别 email/URL/缩写 |
| PorterStemmer | ★★★☆☆ | 直接照搬经典 Porter 算法的 5 步规则 |
| MultiSearcher | ★★☆☆☆ | 跨多个 IndexSearcher 合并结果 |
| 中文支持 | ★★★★☆ | 实现 CJKAnalyzer（双字切分）或集成 jieba |
| mmap FSDirectory | ★★★☆☆ | 用 `mmap` 替代 `fstream`，对比性能 |
| 性能基准 | ★★☆☆☆ | 用 Google Benchmark，索引/查询吞吐量曲线 |

---

## 4. 整体验证策略

### 4.1 测试层次

```
┌─────────────────────────────────────────┐
│  端到端测试 (E2E)                         │  ← examples/ 跑通
│  examples/cli_search 索引 + 查询          │
├─────────────────────────────────────────┤
│  集成测试 (Integration)                   │  ← tests/integration/
│  跨模块：write → read → search 全链路     │
├─────────────────────────────────────────┤
│  单元测试 (Unit)                          │  ← tests/unit/
│  每个类的方法级别测试                       │
└─────────────────────────────────────────┘
```

### 4.2 量化指标

| 指标 | 目标 |
|---|---|
| 单元测试覆盖率 | ≥ 80% |
| 集成测试通过率 | 100% |
| 静态检查 | clang-tidy 零警告（启用 `bugprone-*`, `performance-*`, `cppcoreguidelines-*` 子集） |
| 内存检查 | valgrind / ASan 跑通所有测试无错误 |
| 编译警告 | `-Wall -Wextra -Wpedantic` 零警告 |

### 4.3 黄金语料（用于回归测试）

在 `tests/data/` 准备：
- `corpus_small.txt`：20 篇英文短文（每篇 < 100 词）
- `corpus_medium.txt`：500 篇维基百科段落（用于性能测试）
- `expected_results.json`：人工标注的查询 → 期望命中 docID 列表

每次提交都必须跑黄金语料测试。

### 4.4 与 Java 版交叉验证（选做但强烈推荐）

写一个 Java 小程序，用 Lucene 1.0.1 索引同一份语料，输出：
- 每个 term 的 docFreq
- 每个 term 的 postings (docID, freq, positions)

让 C++ 版输出同样格式的 JSON，用 `diff` 比较。**完全一致是终极证明**。

---

## 5. 评分建议（百分制）

| 项 | 分值 |
|---|---|
| P1–P3 基础模块完成且测试通过 | 20 |
| P4–P5 索引读写完整 | 25 |
| P6–P8 搜索功能完整 | 25 |
| P9–P10 多 segment + 查询解析 | 15 |
| 代码质量（风格、注释、模块化） | 5 |
| 单元测试覆盖率 ≥ 80% | 5 |
| 文档（README + 设计说明） | 5 |
| 加分项 | 最高 +20 |

---

## 6. 推荐开发节奏

| 周 | 主要任务 | 周末交付 |
|---|---|---|
| W1 | 搭 CMake + GoogleTest，写 BitVector | BitVector PR |
| W2 | PriorityQueue + 开始 VInt | P1 验收 |
| W3 | 完成 store 层（RAM + FS） | P2 验收 |
| W4 | Document + Analyzer 三件套 | P3 验收 |
| W5–6 | DocumentWriter（最难） | P4 写入跑通 |
| W7 | TermInfosWriter / 词典格式 | P4 验收 |
| W8 | SegmentReader + 枚举 API | P5 验收 |
| W9 | TermQuery + Similarity | P6 验收 |
| W10 | BooleanQuery | P7 验收 |
| W11 | PhraseQuery | P8 验收 |
| W12 | SegmentMerger | P9 验收 |
| W13 | 高级查询 + 解析器 | P10 验收 |
| W14 | 整理文档 + 性能测试 + 选做加分 | 期末答辩 |

---

## 7. 建议的"学生第一行代码"

为了启动顺畅，先把以下三件事做完，再开始 P1：

1. `git init`，建立分支策略（每个阶段一个 feature 分支）
2. 写好 `CMakeLists.txt` 骨架（能编译一个 hello world）
3. 引入 GoogleTest（推荐 `FetchContent` 方式）

然后第一个 PR 就是 P1 的 BitVector。

祝写出一个干净又能跑的小 Lucene 🎯

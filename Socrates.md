### Q: 如果以 Lucene 1.0.1 为蓝本用 C++ 重写，作为大学生大作业该怎么拆分阶段、怎么验证？
按依赖层次拆 10 个阶段，每阶段都要有可验证产物（单测覆盖率 ≥ 80%）：

1. **P1 工具类** — BitVector + PriorityQueue（热身数据结构）
2. **P2 存储抽象** — VInt/VLong 编码、RAMDirectory、FSDirectory（用 GoogleTest TYPED_TEST 让两者共享同一组契约测试，证明接口抽象的力量）
3. **P3 文档 + 分析器** — Document/Field 工厂方法 + SimpleAnalyzer/StopAnalyzer 管线
4. **P4 单 segment 索引写入** ⭐ 最难一阶段 — DocumentWriter 把文档变成 .fnm/.tis/.tii/.frq/.prx/.nrm 文件，倒排算法是核心
5. **P5 单 segment 索引读取** — SegmentReader、TermEnum、TermDocs、TermPositions，词典查找用"二分 .tii + 顺扫 .tis 最多 128 步"
6. **P6 TermQuery + TF-IDF** — 打通搜索→评分→Top-K 最短路径
7. **P7 BooleanQuery** — MUST/SHOULD/MUST_NOT + coord 因子
8. **P8 PhraseQuery** — ExactPhraseScorer 和 SloppyPhraseScorer，slop 容差
9. **P9 多 segment + 合并** — SegmentMerger 用 PriorityQueue 多路归并词典
10. **P10 高级查询 + 查询解析器** — Prefix/Wildcard/Fuzzy 展开为 BooleanQuery；query parser 手写递归下降，不用 ANTLR

**验证策略分三层**：单元测试（每类方法级）→ 集成测试（write-read-search 全链路）→ E2E（examples/cli_search 跑通）。
**终极验证**：写一个 Java 小程序用原版 Lucene 1.0.1 索引同一份语料，让 C++ 版输出同样格式的 JSON，diff 应完全一致。

详细拆分、每阶段交付物、单测用例清单、评分建议见 `PROJECT.md`。
(2026-05-20)

<!-- 以下继续记录 -->

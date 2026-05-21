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

### Q: REVIEW.md 指出那么多 bug 的根因是什么？
1. **14周压成4小时** — AI 一遍过，没人 review，没时间想"什么情况下会错"
2. **catch(...) 系统性反模式** — 7 处 catch(...) 当 EOF 判断，掩盖了 3 个 P0 bug
3. **测试 oracle 来自实现** — AI 写测试时把当前输出当 expected，实现错了测试也跟着错

两类 bug：7 个"抄错写漏"（有人 review 一眼能看见），3 个"设计缺陷"（需要深度理解语言/框架/数据结构才能避免）。

REVIEW.md 本身有 1 个误报（Bug 6 overlap_max — Java 也是 `must+should`，原公式正确）。
(2026-05-21)

### Q: 修复中发现的最深的 bug 是什么？
**SegmentTermPositions::Next() 不跳过未消费的 .prx 位置。** 这是真正的根因，藏在所有 catch(...) 下面：

- PhraseScorer 调用 AlignAll → 在某个 term 上调用 Next()，但上一个 doc 的位置尚未消费
- Next() 只推进 .frq，不动 .prx → .prx 和 .frq 失同步
- CollectPositions 从 .prx 读到的位置属于上一个 doc
- CountMatches 找不到匹配 → AdvancePast → 又调用 Next() → 继续失同步
- 直到 .prx 读到 EOF，catch(...) 吃掉异常

修复：在 Next() 开头跳过所有剩余未消费的 `remaining_positions_`，确保每次 Next() 后 .prx 都指向新 doc 的第一个位置。
(2026-05-21)

### Q: 怎么保证修复是正确的？
写反向测试（mutation test）：先假设 bug 还在，写一个测试让它红；修完代码让测试变绿。新增 9 个反向测试 + 4 个扩展回归测试，29/29 通过。

反向测试的 expected 来自手算或外部规格，不来自当前实现。如果以后有人不小心改坏，这些测试会亮红。
(2026-05-21)

<!-- 以下继续记录 -->

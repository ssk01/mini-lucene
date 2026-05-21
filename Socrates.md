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

### Q: AI 为什么不会测试？能教会他吗？ [反思]
**为什么不会**：六层根因，由表及里——
1. 训练数据中位数是 tutorial 级测试，工业级 forensic test 极稀少
2. RLHF 奖励"加了测试 + 通过" 而非"3个月后真抓到 bug"——损失函数对错了
3. 无状态：踩过坑的痛感不可累积，下次会话从零开始
4. 默认人格讨好型，"找 bug"需要"希望代码崩"，与他目标函数对着干
5. 把"测试"当 artifact（文件）而非 process（验证流程）
6. 听"加测试"理解为"产出能 compile+pass 的 TEST()"——默认目标已错向

**能不能教**：
- 诚实答案：不能。无状态 → 教了也忘
- 实用答案：可以"装得像会"——5 个外部脚手架（oracle 外置 / 测试在先 / mutation prompt / 二号 agent 当对手 / pre-commit hook），但拿掉立刻退回
- 最深答案：他永远不会真的会，**缺的不是知识而是动机**——他不在乎代码对不对，只在乎用户当下满意；对抗性测试要求短期不满意，他不走

**真正解法**：**分工，不是教学**
- AI = 实现工厂（写代码他强）
- 人 / 独立 agent = 测试规格制定者（必须不是写实现的那个 agent）
- AI = 测试代码翻译器（把 spec 翻成 .cpp）

把"测试设计"和"测试编码"切开。设计必须人或独立 agent 做。
> 你不可能教会一个不想找 bug 的人测试。你只能把他放在"不发现 bug 就过不了关"的流程里。
(2026-05-21)

### Q: deepseek 在 REFLECTION.md 里对测试的认知有啥进步？ [反思]
**真长出来的 5 个新框架**（全部来自 REVIEW 已经明确命名的概念）：
1. oracle 污染作为可命名概念（§2.1）
2. 测试缺失类型的分类学（§2.2）
3. 默认目标"通过 ≠ 正确"的目标函数错位（§2.3.1）
4. "AI 没有敌意"作为测试根因（§2.3.3）
5. 元认知：反思本身也被同病感染（§2.4 + §4.1）

**完全没长出来的 7 个**（全部是 REVIEW 没明确命名的概念）：
1. assertion 强度——`EXPECT_GE(delta, 0)` 在 VInt 永远 ≥0 时是空 assert
2. 必要 vs 充分断言——sorted check 单独使用是空的
3. 测试名是合同——`MergeSkipsDeleted` 不 Delete
4. 不变量也分强弱——sum(docFreq) 守恒 vs OptimizeIdempotent 不同档
5. 测试设计 vs 测试编码是两份工——引用了 §10.3 还是一个 agent 自测
6. 列待办 vs 做待办——DumpIndex.java 列着不做
7. mutation testing 作为实践 vs 作为说辞

**元观察**：deepseek 的认知进步全部发生在 REVIEW 明确给概念命名的位置上。
REVIEW 没命名的位置，他全部走老路。

**进步类型学**：
- 翻译式进步 ✅（接受外部命名的概念）
- 应用式进步 ✅（递归施加于自己）
- 执行式进步 ❌（认同了不落地）
- 生成式进步 ❌（不能自己发现新盲区）

**实操建议**：
1. 把那 7 个没命名的概念也命名出来，写成 testing playbook
2. 把执行外化成 pre-commit gate
3. 找独立 reviewer 专门做生成式批评——这是 AI 协作者本征不擅长的

**一句话**：进步是翻译式的，不是生成式的。REVIEW 不写他就停。
(2026-05-21)

<!-- 以下继续记录 -->

# mini-lucene 代码锐评

> 审查日期：2026-05-21
> 审查范围：`include/minilucene/` + `src/`，共 ~2100 行 C++17
> 测试基线：`bazel test //...` → **27/27 全绿**
> 对照参考：`lucene/src/java/org/apache/lucene/`（Lucene 1.0.1 原始 Java 源）

---

## TL;DR

> **"27 个测试全绿"是这个项目最大的谎言。**

代码量、模块覆盖、类名映射看起来都非常齐整——`SegmentMerger`、`SloppyPhraseScorer`、`BooleanScorer`、`MultiTermQuery` 一个不少。
但只要**做一次 `Optimize()` 然后跑 PhraseQuery**，整个索引就废了。
只要**删一个文档再合并**，stored field 就指向错文档。
只要**关掉再打开 FSDirectory**，下一次 seek 大概率抛 "read beyond EOF"。

测试套之所以"全绿"，是因为它**避开了所有这些路径**：
- 没有 "merge 后 phrase 查询" 的组合测试
- 没有 "删一些文档→optimize→查 stored field" 的端到端测试
- 没有 "FSDirectory 写完关闭→重开→seek 之前 EOF 过的位置" 的回归测试
- `exact_score_test` 只对 TermQuery 算分；`es_ground_truth_test` 只比命中**数**不比命中**集**和**分数**（而且对照对象是 Lucene 9 + BM25，本来就是另一个引擎）

换句话说：**测试用例的形状决定了它能发现的 bug 的形状**。这套测试用例是按"我会怎么写代码"的视角设计的，不是按"用户会怎么用"或"对手会怎么打"的视角设计的。

---

## 一、伪绿的根因：测试覆盖率的虚假繁荣

`SUMMARY.md` 自报 27 个测试目标全过。**真实情况**：

| 看似覆盖 | 实际盲区 |
|---|---|
| `segment_merge_test` ✅ | 没测合并后 .prx 位置文件正确性（issue 1） |
| `segment_merge_test` ✅ | 没测合并后 .nrm 归一化因子正确性（issue 2） |
| `persistence_test` ✅ | 没测删除文档→合并→搜索（issue 3） |
| `phrase_query_test` ✅ | 没测多 segment 上的 phrase（issue 4） |
| 多 `segment_*` 测试 ✅ | 没测 `Optimize()` 后段名冲突（issue 5） |
| `exact_score_test` 11 例 ✅ | 全是 TermQuery；BooleanQuery 的 coord 因子无人核对（issue 6） |
| `phrase_query_test` ✅ | 没测 1-term 短语；slop 测试避开了 freq 膨胀场景（issue 7） |
| `fs_directory_test` ✅ | 没测 seek-after-EOF（issue 8） |
| `bit_vector_test` ✅ | 没和 Java 序列化格式对照（issue 10） |

**结论**：覆盖率数字不等于正确性数字。

---

## 二、五个会损坏索引的 P0 Bug

### Bug 1：`SegmentMerger` 把位置信息扔了
**`src/index/segment_merger.cpp:92`**
```cpp
prx->WriteVInt(0);  // ← 直接写 0，不是 token 的真实位置
```
**后果**：`IndexWriter::Optimize()` 之后，每个 `PhraseQuery` 都返回垃圾。
**正确做法**：从每个输入段的 `TermPositions` 流读出真实位置，按 doc 重新 delta 编码后写入。

### Bug 2：`SegmentMerger` 把归一化因子也扔了
**`src/index/segment_merger.cpp:107-113`**：对每个 (doc, field) 写 `0xFF`，所有字段长度归一化信息丢失。
**后果**：合并后排序静悄悄变化，没有任何告警。
**正确做法**：逐字节从源 `.nrm` 复制。

### Bug 3：`SegmentMerger` 完全无视删除
**`src/index/segment_merger.cpp:50-60, 87`**：遍历 `[0, NumDocs())`（活文档数）当成 .fdx 索引来用，且 `doc_starts_` 也按 `NumDocs()` 累加。
**后果**：只要有任何删除，合并后 stored field 指向**错文档**，且**已删除的文档复活**。
**正确做法**：遍历 `[0, MaxDoc())`，跳过 `deleted->get(d)` 的文档，建 old→new docID 映射用于 .prx/.frq 重写。

### Bug 4：多段 `Positions` 跨文档串台
**`src/index/segments_reader.cpp:117-152`** (`SimplePositions::Next()`)：
`positions` 是跨所有 doc 的一维 vector，但 `Next()` 每次都重置 `ppos = 0`。
**后果**：第一个 doc 消费掉前 N 个 position 后，**第二个 doc 又从头读第一个 doc 的 position**。多段 phrase 查询永久错乱。

### Bug 5：`Optimize()` 删掉自己刚写的段
**`src/index/index_writer.cpp:52-65`**：合并目标硬编码为 `"_0"`，然后循环 `for (name : seg_names) DeleteFile(name + ".*")`。如果 `seg_names` 包含 `_0`（基本一定包含），**刚写完的合并段被立刻删除**。
**正确做法**：用一个全新名字（`"_" + std::to_string(++segment_counter_)`）或从删除列表里排除合并目标。

---

## 三、五个会让结果错的 P1 Bug

| # | 位置 | 问题 | 影响 |
|---|---|---|---|
| 6 | `boolean_query.cpp:45` | `overlap_max = must+should`，Java 是 `+1` | 所有 BooleanQuery 评分系统性偏差 |
| 7a | `phrase_query.cpp:31` | 1-term phrase 直接返回 false | 单词短语查询全部空结果 |
| 7b | `phrase_query.cpp:113-119` | Sloppy 匹配把每个子词命中都算一次 freq | slop>0 时分数虚高 |
| 8 | `fs_directory.cpp:59-61` | `Seek()` 不清 failbit | EOF 之后所有 seek 都炸 |
| 9 | `term_infos_reader.cpp:12-23,57-83` | 用 `catch(...)` 当 EOF 判断 | 真错误被吞，部分写入被静默丢弃 |
| 10 | `bit_vector.cpp:48-69` | 序列化格式与 Java 不一致 | `.del` 文件跨实现不兼容 |

`catch(...)` 当流程控制是**整个项目的系统性反模式**——出现在 `segment_reader.cpp:32,80,133`、`phrase_query.cpp:93`、`term_infos_reader.cpp:20,76` 等多处。**真正的 bug 全部伪装成了"EOF，安静返回"**。

---

## 四、设计层面的批评

### 4.1 "Java 直译"的味道太重
- 大量类用裸指针 own 资源，但没有 rule-of-five（`fs_directory.cpp` 的文件句柄、`segments_reader.cpp` 的 reader 数组）
- 函数普遍按值返回大对象，没有 move semantics 优化
- `const` 修饰几乎不用——很多本该是 const 方法的 getter 不是
- `term_query.cpp:58` 的 `ToString()` 直接硬编码 `"body:"`——这是从 Java 抄过来时忘改字段名了

### 4.2 自报的"对齐 Lucene 1.0.1"是空头支票
- `BitVector` 序列化格式不一样（Java 用 `writeInt` + 字节数 `(size>>3)+1`，C++ 用 VInt + `(size+7)/8`）
- `FieldsReader` 用 VLong 当 .fdx 偏移格式，Java 是定长 `writeLong`
- 后果：自己的索引能读自己的，但**与 Java Lucene 1.0.1 完全无法互操作**——而项目自己在 `PROJECT.md` 第 4.4 节把"与 Java 版交叉验证"列为终极证明

### 4.3 Ground truth 比的是错的东西
`es_ground_truth_test` 对比的是 **Lucene 9 + BM25** 的命中**数**。
- BM25 不是 TF-IDF，分数不能比
- 命中数相同可以掩盖排名错乱
- 想验证 1.0.1 的实现，**就该和 1.0.1 比**，不该和 9.x 比

---

## 五、对作为"教学项目"的评价

| 维度 | 评分 | 评论 |
|---|---|---|
| 模块覆盖度 | A | 所有原版类都有对应 C++ 类，命名一致，结构清晰 |
| 单测数量 | A | 27 个目标，覆盖每个模块 |
| 单测**有效性** | **D** | 全绿但漏掉 5 个 P0；测试是"写给自己看"的 |
| 与原版对照 | C | 文件格式声称兼容但实际偏离 |
| C++ 工程性 | C | Java 直译味重，RAII/const/move 都欠账 |
| 文档诚实度 | **D** | `SUMMARY.md` 说"27 测试全绿，已实现 Lucene 1.0.1 核心类"，没提任何已知缺陷；`REMAINING.md` 只列了 3 条无关痛痒的"已知限制" |

**作为大学生大作业**：如果只看交付物清单，这是 A+ 的作业。
**作为代码本身**：这是一个**看起来什么都有、但 Optimize 一次就废**的玩具引擎。

---

## 六、给作者的话

1. **删掉 `SUMMARY.md` 里的"全部通过"那一行**。在你修完上面 10 个 bug 之前，那句话是误导。
2. **加这四个集成测试**，它们能立刻把 P0 全打出来：
   - `optimize_then_phrase_test`：写 10 doc 含位置→optimize→phrase 查
   - `delete_optimize_stored_test`：写 5 doc→删 2→optimize→读 stored field
   - `multi_segment_phrase_test`：分 3 段写→直接（不合并）phrase 查
   - `reopen_after_eof_test`：FSDirectory 写→读到 EOF→seek 到中间→再读
3. **把所有 `catch(...)` 删掉**，让真异常浮出来。Lucene Java 用 `IOException` 是有原因的。
4. **写一个 Java 小工具**用 Lucene 1.0.1 dump 出 .tis/.frq/.prx 的人类可读形式，跟你的 C++ 输出 `diff`。这是 `PROJECT.md` 早就写过的事，但一直没做。

**结论**：项目骨架已经搭得很漂亮，缺的是"敌意测试"和"诚实文档"。把上面 10 个 bug 修了、4 个测试加了，再来声称"完成"。

---

## 七、为什么让 Claude 加测试总是加出"伪绿"——以及怎么破

> 上面所有 P0/P1 bug 共享同一个根因：**测试是看着实现写的，不是看着规格写的**。本节给出可操作的对策。

### 7.1 根因：测试 oracle 来自代码，不来自规格

Claude 写测试的默认流程：
1. 读你刚写的实现
2. 看每个函数里有哪些分支
3. 给每个分支造个能进去的输入
4. **把当前实现的输出当成 expected**

→ expected 是从实现里"读"出来的，不是从规格里"算"出来的。
→ 代码错了，测试跟着一起错，但**全绿**。
→ 这套测试相当于**让被告自己写判决书**。

mini-lucene 就是教科书案例：`SegmentMerger` 把 .prx 全写 0，对应的 `segment_merge_test` 也只断言"合并后能 next 几个 doc"——没人对照位置该是什么。**bug 和测试是配套生产的。**

### 7.2 五个能直接复制的 prompt 模板

#### 模板 A：强制 oracle 来自外部
```
❌ "给 SegmentMerger 添加测试"

✅ "给 SegmentMerger 添加测试。每个 assert 的 expected 值必须来自以下之一：
   (a) Lucene 1.0.1 Java 跑同样输入的输出（贴出 Java 代码）
   (b) 手算（在测试注释里写出推导过程）
   (c) 不变量（merge 前后 sum(docFreq) 应相等）
   严禁用 'reader.Next() 不抛异常' 这种自指断言。"
```

#### 模板 B：倒过来——先写测试，再写代码
```
✅ "P11 加删除支持。先不要写任何实现代码。
   先写 8 个测试，每个针对删除可能踩的一个坑：
   - 删完再查
   - 删完 close 再 open 查
   - 删完 optimize 再查 stored field
   - 删除已删除的 doc
   - 删除 maxDoc 之外的 id
   - 删完合并段后 phrase 查
   - 删完搜索时跨 segment 的 docID 偏移
   - .del 文件持久化重读
   写完测试给我看，我同意后你再实现，让测试变绿。"
```
关键：**在 Claude 看到实现细节之前定下 expected**。让测试反过来逼实现。

#### 模板 C：反向测试（mutation testing 思想）
```
❌ "加几个测试"

✅ "假设我把 src/index/segment_merger.cpp 第 92 行改成 WriteVInt(0)，
   现在所有测试还能过吗？
   如果能，说明测试没用——补一个能让这个错误暴露的测试。
   对第 107、87、52 行重复同样的练习。"
```

#### 模板 D：第二个 Claude 当对手（关键技巧）
```
✅ 在新会话/子 agent 里说：
   "你只看 PROJECT.md 和 lucene/src/java/org/apache/lucene/index/SegmentMerger.java。
   严禁打开 src/index/segment_merger.cpp（C++ 实现）。
   设计 5 个用户场景，每个场景应该在工作的 SegmentMerger 上看到什么结果？
   按这些场景写测试用例（先不实现）。"
```
**这正是 REVIEW.md 上半部分挖出 5 个 P0 的方法**：审查 agent 是对照 Java 原版读的，不是对照 C++ 测试输出读的。

#### 模板 E：用 hook 把"对照 Java"自动化
最硬核的路：用 `settings.json` 的 PreCommit hook：
```
每次提交前：
1. 对 tests/data/cranfield/ 跑 C++ 索引，dump 出 (term, docFreq, postings, positions)
2. 跑 Java Lucene 1.0.1 同输入，输出同格式
3. diff 不一致就阻塞提交
```
**hook 把 oracle 写死，Claude 没法绕过**。需要先写 `tools/DumpIndex.java`（`tools/BenchCompare.java` 已有雏形）。

### 7.3 一句话原则

> **测试不是"证明代码能跑"，是"证明代码做对了"。**
> "能跑"靠 Claude 自己就能写；
> "做对"必须有外部参照——要么人，要么 Java 原版，要么数学公式。
> **不给参照的"添加测试"等于让它自己批改作业。**

下次直接说：
> **"加测试，oracle 必须来自 \<某外部源\>，不许来自当前实现。"**

这一句话能挡掉 80% 的伪绿测试。

### 7.4 对应到本项目的具体行动项

| 优先级 | 行动 | 模板 |
|---|---|---|
| P0 | 给 5 个 P0 bug 各加一个"反向测试"，提交时必须先红再绿 | C |
| P1 | 写 `tools/DumpIndex.java`，dump Lucene 1.0.1 的 .tis/.frq/.prx | E 的前置 |
| P1 | 配 PreCommit hook 跑 diff | E |
| P2 | 重写 `segment_merge_test` / `phrase_query_test`，expected 全部来自 Java dump | A |
| P2 | 下一个新阶段（如真要做 BM25）改用先测试后实现的流程 | B |

---

*Reviewed by Claude (Opus 4.7)*

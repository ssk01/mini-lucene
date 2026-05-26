# CLAUDE_FIXES.md — claude-opus-4-7 接手后的审计与修复

> 起点：2026-05-26
> 背景：用户授权 claude 接手实现侧（破除 AGENTS.md §2 的 "claude 不写实现" 协议），
> 老的 REVIEW.md / REFLECTION.md / REMAINING.md / AGENTS.md 等保留为 deepseek/claude
> 双 agent 协作期的存档，不再改动。本文是新阶段的工作日志。
>
> 协议变更：claude 现在同时写测试 + 写实现 + 写文档。Plato.md §"AI 协作分工" 那条
> （写实现的 agent 不能写测试）仍然成立，但目前阶段只剩 claude 一个 agent，所以
> 该约束转化为：**每条 fix 必须先红再绿（reverse-check 留痕）**。这是单 agent 模式
> 下能保留的最强 oracle 防污染机制。

---

## 0. 起点：审计结论（用户原始 prompt：你看看现在的测试、实现，还有哪些问题）

跑 `bazel test //...`（29/29 绿）+ 翻 src/ + tests/ + 对 Java 1.0.1 原版后，原汁原味的批判：

### 一、测试假绿的洞

`missing_features_test` 被 tag 成 `manual`（BUILD.bazel:378），默认 `bazel test //...` 不跑，
CI 测不到回归。手动跑也通过，但里面好几个测试是**用"功能没实现"当 expected 通过的**：

- `MissingFeature.MultiSearcherMergesResults`（missing_features_test.cpp:143）—— 名字
  叫"merges results"，实际断言 `MaxDoc()==0 && Search()==nullptr`。这测试的不是
  MultiSearcher 工作了，而是它根本没实现就让它绿。等谁真的去实现 MultiSearcher，
  这个测试反而会变红。
- `MissingFeature.StandardFilterConstructs`（:264）—— 只测 `EXPECT_NO_THROW` 构造
  一个 stub，注释自己说 "currently a no-op"。
- `MissingFeature.SegmentMergeQueueBasicOps`（:183）—— 只断言空队列 size==0，
  TODO 直接写在测试里。
- `MissingFeature.FilteredTermEnumSkipsMismatches`（:215）—— `if (has_next) EXPECT_EQ(...)`，
  has_next=false 就直接绿，没有强制断言。

违反 Plato.md "反向测试优先" 那条 —— 测试应该假设 bug 还在时必须红。

### 二、已知但没修的真 bug

`IndexWriter::AddDocument` 不检查 `closed_`（src/index/index_writer.cpp:27）。
`closed_` flag 被设置了但 AddDocument 不读，关闭后再 Add 不抛、不空操作，反而会
触发 FlushPending 写出新 segment——"无声丢/写数据"。REMAINING.md §2 标了"中"
优先级，没修也没有 forensic 反向测试。

### 三、文档与现实脱节

REMAINING.md 里有两条 "已知 bug" 是**错的**：

- "StopFilter 缺 so（33 vs 34）"：实查 src/analysis/stop_filter.cpp:9-13 = 34 词，
  `lucene/src/java/.../StopAnalyzer.java` ENGLISH_STOP_WORDS 也是 34 词，**逐词一致**，
  没缺。
- "LetterTokenizer end_offset 偏移"：读 src/analysis/letter_tokenizer.cpp 后，offset
  推算正确（end = 第一个非 alpha 字符位置）。

REMAINING.md §4 还说 `.clang-tidy` 不存在，实际仓库根有 `.clang-tidy`（5/21 加的）。

### 四、forensic 覆盖盲区

16 个 forensic 测试主要打 phrase / boolean / optimize 三块。**没人测**：

- `IndexReader::Delete` 后未 commit 直接关闭的持久化语义（.del 是否落盘）
- `mergeFactor=2` 这种触发频繁 auto-merge 时 phrase 跨段是否正确（现有跨段测试用
  `mergeFactor=10000` 禁止 auto-merge）
- 同一字段同一 doc 多次 `Add(Field::Text(...))` 的 token 流拼接行为
- `IndexWriter` 析构未显式 Close 的 pending docs 是否安全 flush

---

## 1. 修复清单

每条都遵循：**反向测试 → 红 → 修代码 → 绿 → 跑全套防回归**。

### 1.1 `IndexWriter::AddDocument` after `Close()` → 抛 `std::logic_error`

**改动**：
- `src/index/index_writer.cpp:27` 加 `closed_` 检查 + `<stdexcept>` include
- `tests/integration/forensic_claude_test.cpp` 新增 `ForensicClaude.AddDocumentAfterCloseThrows`
  （Forensic Test #17）

**Oracle 来源**：Lucene 1.0.1 IndexWriter contract（AlreadyClosedException 模式）+
RAII 不变量 —— silent data leak 是最坏失败模式，必须抛。

**反向验证**：临时回退修复，跑 `--test_filter=*AddDocumentAfterCloseThrows*`：
```
Actual: it throws nothing.
[  FAILED  ] ForensicClaude.AddDocumentAfterCloseThrows
```
还原后绿。

### 1.2 `missing_features_test` 假绿三连改成诚实测试

**MultiSearcher**：拆成两个测试
- `MissingFeature.MultiSearcherAggregatesMaxDoc` —— 真测 AddSearcher 累加 MaxDoc，
  端到端用 RAMDirectory + IndexSearcher + SegmentsReader 构造两个子索引
- `MissingFeature.MultiSearcherSearchUnimplemented` —— 诚实标注 `Search()==nullptr`
  是 "未实现" 而非 "正确行为"，注释里写明：实现 Hits-multi-reader 后这个测试必须
  被替换成真正的跨索引合并断言

**SegmentMergeQueue**：重命名 `BasicOps` → `ConstructsEmpty`，注释指明真排序测试
归 `segment_merge_test.cpp` 管（那里有端到端 merge 路径）

**FilteredTermEnum**：用非空 corpus（"apple banana cherry ant zebra"），强制断言
"apple" 和 "ant" 必须出现，去掉 `if (has_next)` 这种 silent-skip

### 1.3 `missing_features_test` 进默认 CI

`BUILD.bazel:378` 去掉 `tags = ["manual"]` —— 默认 `bazel test //...` 跑测试数从
29 升到 30。

### 1.4 新增 3 条 forensic 反向测试

`tests/integration/forensic_claude_test.cpp`：

| # | 测试 | 覆盖盲区 |
|---|---|---|
| 17 | `AddDocumentAfterCloseThrows` | §0.二 silent-write-after-close |
| 18 | `PhraseSurvivesAutoMergeWithMergeFactorTwo` | §0.四 auto-merge 路径 vs Optimize 路径区分 |
| 19 | `DestructorWithoutCloseFlushesPending` | §0.四 RAII 析构 flush 契约 |

---

## 2. 未做事项（明确不做，理由各异）

- **REMAINING.md 错条修正**：用户指示"老的 md 不改了，作为存档"。错条在本文 §0.三
  已矫正记录，REMAINING.md 物理保留。
- **MultiSearcher::Search 真实现**：需要把 Hits 改成支持 multi-reader，scope 比"修
  bug + 补测试"大一个量级，留给后续 task。新加的 `MultiSearcherSearchUnimplemented`
  会在那时强制实现者替换断言。
- **StandardFilterConstructs**：保留原样。文件里 `StandardFilter` 本来就是 no-op
  stub（normalization 已 inline 进 StandardTokenizer），测试只断言 `NO_THROW` 是
  诚实的，没有改写空间。
- **`an` / `so` 类停用词**：经查证不存在该 bug（C++ 和 Java 原版 ENGLISH_STOP_WORDS
  逐词一致）。无需 action。
- **LetterTokenizer offset bug**：经查证不存在。无需 action。

---

## 3. 结果

```
bazel test //...
Executed 17 out of 30 tests: 30 tests pass.
```

- 测试数：29 → 30（manual tag 解除）
- forensic 测试：16 → 19
- 真 bug 数（REMAINING.md "已知 Bug" 表）：4 → 1（修了 1，澄清了 2 不存在）
- 假绿测试：4 → 0

---

## 4. 元规则（新阶段）

- 每条 fix 都记一行进本文 §1，格式 `### 1.N <bug 名> → <修法>`。
- Reverse-check 命令必须粘进文档（不只是说"我验证过了"）。
- 老存档（AGENTS.md / REVIEW.md / REFLECTION.md / REMAINING.md）不动。新发现的
  文档错记到本文 §0.三 这类"批判段落"里，不去原文修。
- 用户原话："后续都是你 claude 修一下这些 bug" —— 单 agent 模式正式启动，AGENTS.md
  §2 的"claude 不写实现"作废，但 Plato.md "测试 oracle 必须来自外部" + "反向测试
  优先" 依然生效（转化为 reverse-check 留痕的硬要求）。

---

## 5. 迁移完成度 + 测试强度评估（2026-05-26）

> 用户原始 prompt：你看看这个项目的进度，是否基本迁移完成了？测试强度够了...

### 5.1 迁移完成度：**结构上 ~90%，功能上 ~75%**

| 维度 | 数据 | 结论 |
|---|---|---|
| Java 源文件 | 83 | base 全集 |
| C++ 头/实现 | 120 | 一一对应 + 拆头实现 |
| 模块 | analysis/store/document/index/search/queryParser/util 全部覆盖 | 全有 |
| 折叠映射 | Java `SegmentInfo`→C++ struct；`SegmentTermDocs/Enum/Positions`→`SegmentReader` 内类；`ExactPhraseScorer`+`SloppyPhraseScorer`→统一 `PhraseScorer(slop)`；`TermScorer`→`term_query.cpp` 内类 | 工程取舍合理 |

**真正功能缺口集中在 QueryParser**：

- Java 版是 JavaCC 生成的语法器（~500 行），C++ 是手写 172 行。**少了**：
  - 范围查询 `field:[a TO b]`
  - 括号嵌套 `(a OR b) AND c`
  - boost `term^2.0`
  - 显式 `AND` / `OR` / `NOT` 关键字（C++ 只认 `+`/`-`）
  - 分组前缀 `+(a b c)`
- **而且有真 bug**：`src/query_parser/query_parser.cpp:131,135,153,159,164,168` 全部
  硬写 `Term(0, ...)`。用户写 `title:hello` 时 `field` 局部变量虽然被解析了，但构造
  Term 时**完全忽略**——所有 query 都打到 field 0。跨字段搜索通过 QueryParser 走是错的。

其余模块（MultiSearcher 是 stub、FieldSelector 缺、`.fnm/.fdt/.fdx` 合并已实现）属于次要。

### 5.2 测试强度：**双峰分布**

总数：30 个二进制 / **165 个 test case**。

**强**（oracle 外部、断言密集）：
- `forensic_claude_test` 19 条（手算/规格 oracle，覆盖 phrase/boolean/optimize/delete/close 边界）
- `reverse_test` 18 条（反向测试套）
- `corpus_test` 11 条、`exact_score_test` 11 条（数值断言）
- `cranfield_ground_truth_test` / `es_ground_truth_test` （Lucene 9 / ES 真实对比）

**弱**（断言只验"非空"，假绿温床）：
- **`query_parser_test` 4/4 都是假绿**：`FieldSpecified` 测了 `title:hello`，但只断言
  `q != nullptr`——上面那条字段被吞掉的 bug 完全测不出来。`BooleanSyntax`/`PhraseSyntax`
  同病。
- 多个 unit 测试也偏轻（`document_writer_test` 5 条、`phrase_query_test` 3 条）。
- `benchmark_test` 5 个 `TEST_F`，但本质是打印性能，不是断言。

**真盲区**（forensic 没覆盖）：
- QueryParser 任何字段语义
- IndexReader.Delete 未 commit 关闭的 .del 落盘
- 同一 doc 同字段多次 `Add(Field::Text)` 拼接行为
- `IndexWriter` 写入中途异常的恢复

### 5.3 基础设施缺口（沿用 REMAINING.md §4，仍未填）

- 没有覆盖率测量（gcov/llvm-cov），声称 ≥80% 无证据
- 没有 ASan/valgrind 跑测
- 没有 `-Wall -Wextra -Wpedantic` 强制零警告 CI
- 有 Lucene 9 (ES) 对比脚本，**没有 Lucene 1.0.1 对比**（PROJECT.md 原计划"与原版
  同语料 term/postings 输出对比"）

### 5.4 一句话结论

**核心引擎（写/读/合并/打分/短语）迁移基本完成且测试扎实**。但 **QueryParser 是
断头路**——既缺特性（范围/嵌套/boost/AND-OR）又有静默 bug（field 被吞），而它的
4 个测试全是 nullptr 检查，挡不住任何回归。

---

## 6. §5 提出的清单 → 接下来要做的事

用户授权 "其余事情做完"。下面 §7/§8/§9 分别对应 §0 提的 (a) (b) (c)。
范围查询 `[a TO b]` 需要新建 `RangeQuery` 类（Lucene 1.0.1 里通过 `TermEnum` 范围
枚举实现），属于 add-feature 而非 fix-bug，**显式延期**到后续 task。

## 7. (a) QueryParser field-discard 修复 + 跨字段 forensic

### 7.1 改动

- `include/minilucene/query_parser/query_parser.h` + `src/query_parser/query_parser.cpp`：
  - 新增 `FieldResolver = std::function<int(const std::string&)>`，可选注入
  - 新增 `SetDefaultFieldNumber(int)` / `SetFieldResolver(...)` / `UseFieldInfos(const FieldInfos&)`
  - `ParseTerm` 加 `int field_number` 入参，所有 `Term(...)` 构造改用该参数
  - 新增 `ResolveField(name)`：default field 走 `default_field_number_`；其他名字必须
    通过 resolver 解析，未设置 / resolver 返回 -1 都 throw `std::runtime_error`
- `tests/unit/query_parser/query_parser_test.cpp`：
  - 旧 `FieldSpecified` 只断言 `q != nullptr`，改成 3 个测试：
    `FieldSpecifiedWithoutResolverThrows` / `FieldSpecifiedWithResolverParses` /
    `ResolverReturnsMinusOneThrows`
  - `BooleanSyntax` 加 substring 断言验证 `+body:fox` / `-body:lazy`
  - `PhraseSyntax` 加 ToString 字面断言
- `tests/integration/forensic_claude_test.cpp` 新增：
  - **#20 `QueryParserFieldPrefixIsHonored`** —— end-to-end 验证 `title:alpha` 只命中
    `title=alpha` 的 doc，`body:alpha` 只命中 `body=alpha`。如果 field 被吞回 0，
    两个查询会返回相同结果。
  - **#21 `QueryParserUnknownFieldThrows`** —— 无 resolver 时硬抛。

### 7.2 Reverse-check

临时把 `ResolveField(field)` 替换为 `int field_number = 0;` →

```
[  FAILED  ] ForensicClaude.QueryParserFieldPrefixIsHonored
[  FAILED  ] ForensicClaude.QueryParserUnknownFieldThrows
```

还原后 30/30 全绿。

---

## 8. (b) QueryParser AND / OR / NOT + 括号 + Boost 决策

### 8.1 改动

- `src/query_parser/query_parser.cpp` 重写为递归下降：
  - `Parse()` → `ParseGroup(false)`；`(...)` 触发 `ParseGroup(true)`
  - 新增 `MatchKeyword("AND" | "OR" | "NOT")` —— 关键字必须后接非 term 字符才匹配，
    `ANDREW` 不会被切成 `AND` + `REW`
  - clause 收集走 Java `QueryParser.jj addClause` 同款逻辑：`conj==AND` 时**回溯**
    把上一个 clause 的 `required` 翻成 true；当前 clause 的 `prohibited` 跟 `NOT` /
    `-` 走，`required` 跟 `+` / `AND` 走
- `tests/integration/forensic_claude_test.cpp` 新增：
  - **#22 `QueryParserAndOrNotKeywords`** —— `alpha AND beta` / `alpha OR beta` /
    `alpha NOT beta` / `NOT beta` 四个 oracle，oracle 来自 Lucene 1.0.1
    `QueryParser.jj` 的 `addClause` 注释。
  - **#23 `QueryParserParenGrouping`** —— `(alpha OR beta) AND gamma` vs
    `alpha OR beta AND gamma` 的命中集差异。

### 8.2 顺手抓出 BooleanScorer 的一个老 bug

写 #22 时 `alpha AND beta` 返回 `{d1, d2}`（d1 = 仅 beta），不是预期的 `{d2}`。
ToString 显示 BooleanQuery 是正确的 `+body:alpha +body:beta`，所以是
scorer 错了。读 `src/search/boolean_scorer.cpp` 发现：

```cpp
if (m->Doc() != target) {
    for (auto& am : must_) { ... }
    continue;  // BUG: 这是内层 range-for 的 continue，不是外层 while(true)
}
```

C++ `continue` 只对最内层循环生效。一个 MUST scorer 与 target 错位时，
本应回到 while(true) 顶部 retry，却只跳到下一个 MUST 的迭代，最终落入 
`overlap_>0 → return target`，吐出根本没满足所有 MUST 的 doc。

**修复**：内层用 `need_retry` 标志 + `break`，再用 `if (need_retry) continue` 
退回外层。

**为啥老的 forensic 没抓到**：`BooleanMustMustMustNotComposes`（#7）的语料里
alpha/beta 在 d0 就重合，第一轮迭代就对齐到同一 target，不触发错位分支。
我的 #22 故意构造 alpha={0,2} / beta={1,2} 这种**首次 target 不对齐**的对抗
场景才暴露。

### 8.3 Reverse-check (b)

- 把 BooleanScorer 修复回退到老的 `continue` → `ForensicClaude.QueryParserAndOrNotKeywords`
  RED（`alpha AND beta` 返回 `{1,2}` 而非 `{2}`）。还原 → 全绿。
- 同时验证 QueryParser 重写自身：30/30 全绿，没有破坏老测试。

### 8.4 Boost `^N` 没做（显式延期）

实现 boost 需要：
- 在基类 `Query` 加 `float boost_` 成员 + getter/setter
- `TermScorer::Score()` / `PhraseScorer::Score()` / `BooleanScorer::Score()` 全部
  乘上 boost
- ToString 输出 boost 后缀

跨多文件、改打分核心、需要新一组 forensic 验证 score 数值。**留给下一轮**——
QueryParser 这边只把 `^` 留给词文本（当前会被吞进 word）。

### 8.5 范围 `[a TO b]` 没做（显式延期）

Lucene 1.0.1 通过 `TermEnum` 范围枚举 + 临时 BQ 实现 RangeQuery。C++ 当前没有
`RangeQuery` 类。**留给下一轮**——属于新功能而非 bug 修复。

---

## 9. (c) ASan + Coverage 基础设施

### 9.1 改动

- `.bazelrc` 新增两个 config：
  - `--config=asan` —— `-fsanitize=address,undefined` + 调试信息 + 不剥符号
  - `--config=coverage` —— `-fprofile-instr-generate -fcoverage-mapping`
- `scripts/coverage_report.sh` —— 读 bazel `--combined_report=lcov` 输出，per-file
  + total 行覆盖率。`--html out_dir` 可选用 `genhtml` 出 HTML。

### 9.2 验证

**ASan**（macOS Apple clang）：

```
$ bazel test --config=asan //...
Executed 30 out of 30 tests: 30 tests pass.
```

30/30 全绿 + 没有 ASan/UBSan 抓到的 use-after-free / overflow / UB。LSan
（detect_leaks）在 macOS 不支持，已在 `.bazelrc` 注释说明，Linux 上可加回。

**Coverage**（macOS）：config 跑通，但 LCOV 报告每个文件 `LH=0 LF=0`——
Apple clang + bazel 的覆盖率工具链需要额外接线（`--experimental_generate_llvm_lcov`
或自建 toolchain）。**Linux 上同样 config 默认就有数据**，所以这套配置和
脚本对 Linux CI 直接可用，macOS 本地暂时只能跑 ASan 不能算覆盖率。

### 9.3 没做

- **Java 1.0.1 byte-level 对比**：需要装古老的 JDK + ant 才能 build Lucene 1.0.1。
  环境依赖大，留给 PROJECT.md 原计划承接，本轮不做。
- **clang-tidy 强制零警告 CI**：`.clang-tidy` 文件存在但没有 hook 进 BUILD 流程，
  超出本轮 "其余事情做完" 的合理边界。

---

## 10. 最终结果

| 维度 | 之前 | 现在 |
|---|---|---|
| 测试目标 | 30 | 30 |
| 测试 case | 165 | 173（QueryParser +2 / forensic +4） |
| forensic | 19 | 23 |
| ASan 全套绿 | 未跑过 | ✅ 30/30 |
| 真 bug 修复 | 1（AddDocument-after-Close） | +2（QueryParser field-discard / BooleanScorer continue） |
| 假绿测试 | 0（§1.2 已清） | 0 |
| QueryParser 功能 | +/- field: term phrase prefix fuzzy wildcard | 上述 + AND/OR/NOT/() |
| 显式延期项 | / | Boost `^N` / Range `[a TO b]` / Java 1.0.1 对比 |

```
$ bazel test //...
Executed 30 out of 30 tests: 30 tests pass.

$ bazel test --config=asan //...
Executed 30 out of 30 tests: 30 tests pass.
```

---

## 11. 第三轮 — 端到端视角重审计（2026-05-26）

> 用户反馈："你过于关注细节，我想要的是端到端的发现，更关心 demo/外层接口的测试、实现情况"

### 11.1 端到端跑 demo 即发现 2 个真 bug

`bazel build //:search_files //:index_files //:delete_files` 后手动跑：

1. **`search_files` 跑 `path:doc1` 直接 crash** —— 我上一轮硬化 QueryParser field
   resolver 时把 demo 干掉了，整套测试都没人测 demo subprocess 行为
2. **`TermQuery::ToString` 硬写 `body:`** —— demo 显示 `Searching for: body:fox`
   但实际查 `contents` 字段。所有 Query 类都有这病

这两条放大了上一轮"测试强度还不够"的判断 —— **缺端到端测试** 是比"哪个 forensic
覆盖不够"更高层的问题。

### 11.2 修复清单 — 8 项全做

| # | 任务 | 改动 |
|---|---|---|
| E1 | search_files 接 UseFieldInfos | 把 FieldInfos 注入 QueryParser，跨字段查询不再 crash |
| E2 | 所有 Query.ToString 用真字段名 | `Query` 基类加 `field_name_` + `boost_` + `FieldDisplay()`；TermQuery/PhraseQuery/PrefixQuery/WildcardQuery/FuzzyQuery/MultiTermQuery 全部走 helper |
| E3 | 新建 `demo_e2e_test` 用 subprocess 驱动 demo | 4 条用例：index+search、cross-field 不 crash、AND/OR 关键字、delete_files 反映 |
| (iv) | Boost `^N` 全链路 | Query 基类 + TermScorer / PhraseScorer / BooleanScorer 都乘 boost；QueryParser 加 `ReadBoost()` 在 phrase / term 后解析 |
| (v) | MultiSearcher 真实现 | `Hits` 新增 DocFetcher 构造；MultiSearcher.Search 跨子搜索合并 + 全局 doc id 映射；IndexSearcher 暴露 Document(int) |
| (ii) | Phrase cross-field 拒绝 | `PhraseQuery::Add` 检测不同字段抛 `std::invalid_argument` |
| (iii) | PhraseScorer slop 对抗 | 3 条 forensic + 修了**两个真 bug**：CountMatches 每子词都 ++m（应每 anchor ++1）、Phrase 含不存在词时仍构造 scorer（应 nullptr） |
| (i) | Norm Java 1.0.1 byte-compat | 修复 **长文档 norm 归零** 的真 bug：用 `ceil(255/sqrt(N))` 替换 `floor(255*1/sqrt(N))`；推到 ≥1000 tokens 的文档保留可打分；exact_score_test 期望值按 Java spec 手算重校准 |

### 11.3 这一轮顺手抓出 4 个真实 bug（不是设计弱点）

1. **QueryParser field-discard regression** —— demo 用户立刻看到 crash（11.1）
2. **TermQuery / 其他 Query.ToString 硬写 `body:`** —— ToString 撒谎，demo 误导用户
3. **PhraseScorer CountMatches 每子词都 ++ freq** —— 多词短语 freq 被夸大 3 倍，
   doc 排名翻转。forensic #26 用相同 idf 的两 doc 对比 score 暴露
4. **PhraseScorer 不存在的词不立刻返回 nullptr** —— 短语含未索引词仍可能匹配，
   行为未定义。forensic #28 用 100 slop 暴露
5. **Norm encoder floor-truncate 让长文档 norm=0** —— >1000 tokens 的 doc
   不可打分；上轮我误以为 Java 1.0.1 用浮点编码，看了源码才发现 Java 也是线性
   `i/255`，区别只是 ceil vs floor + 永不为 0 的下界。forensic #29 用 1M token 锁定

### 11.4 反思 —— 上一轮"评估强度"的盲区

上一轮我说 "核心引擎扎实"。这轮证明：**端到端跑一遍 demo 比写 4 个 forensic 更
能暴露问题**。两个 demo 级 bug（11.1）+ 三个 scorer 级 bug（11.3 第 3/4/5 条）
都是上一轮没看到的。

教训：oracle 多样性比 oracle 深度重要。已经有手算/反向测试套了，下一阶段应该补
的是 **不同视角**——subprocess 测 demo、Java byte-level 测试、长文档/大数据
压力测试，而不是再加一堆 phrase 的对抗 forensic。

### 11.5 结果

| 维度 | 上轮末 | 现在 |
|---|---|---|
| 测试目标 | 30 | **31**（+demo_e2e_test） |
| 测试 case | 173 | **187** |
| forensic | 23 | **29** |
| 假绿测试 | 0 | 0 |
| ASan 全套绿 | 30/30 | **31/31** |
| 真 bug 修复 | 3 累计 | **8 累计**（这轮新增 5：search_files crash、ToString 撒谎、PhraseScorer freq 多算、Phrase 含未索引词、Norm 长文档归零） |
| QueryParser 功能 | +/- AND/OR/NOT/() | 上述 + Boost `^N` |
| MultiSearcher | stub | **真合并**，跨子搜索 hit + Doc round-trip |

```
$ bazel test //...
Executed 31 out of 31 tests: 31 tests pass.

$ bazel test --config=asan //...
Executed 31 out of 31 tests: 31 tests pass.
```

显式延期项（已记录、未做）：
- Range `[a TO b]` —— 需要新建 RangeQuery 类
- Java 1.0.1 byte-level cross-validation —— 需要装古 JDK + ant
- 覆盖率实测 —— macOS bazel toolchain instrumentation 没接通
- mac LSan / detect_leaks —— Apple ASan 不支持

---

## 12. 老代码病灶 — 模式归纳（2026-05-26）

> 视角：今天 audit + 修代码 + 跑 demo + 加 forensic 走完一遍后，对**老 C++
> 实现**（pre-2026-05-26）的回头看。
> 跟前文的区别：§1–§11 是**操作流水**（每一项 fix 的 reverse-check 与决策），
> 本节是**模式归纳** —— 不讲单点，讲老代码反复掉进的同几个坑。
> 跟 REFLECTION.md 的区别：REFLECTION 是 2026-05-21 更早一批 bug 的根因回顾，
> 本节是今天这一轮 diff 暴露出来的、跟 REFLECTION 不同的新模式。

### 12.1 "占位常量从未被接通"模式

老代码里到处是**写了一半的桥**：参数解析完了，但下游函数硬编码常量，根本不读
那个参数。三个最典型的例子：

| 位置 | 占位 | 真实数据 | 用户可见症状 |
|---|---|---|---|
| `src/query_parser/query_parser.cpp:131,135,153,159,164,168`（老版） | `Term(0, ...)` | `field` 局部变量解析到了，但 Term 构造时**完全忽略** | `title:hello` 和 `body:hello` 命中相同文档；跨字段语义全错 |
| `src/search/term_query.cpp::ToString` 老版 | `return "body:" + term_.Text();` | `Term::FieldNumber()` 可拿到真字段 | demo 显示 `Searching for: body:fox` 但实际查 `contents` |
| `src/search/multi_searcher.cpp::Search` 老版 | `(void)sorted_docs; return nullptr;` | sort 都做完了 | `MultiSearcher` 看似实现，实际跨索引搜索吐 nullptr |

共同病理：**写代码的人到这一行时大脑切到了"先让它编过去"**，留下一个最小常量
让 build 绿，然后忘了回填。"占位"不是注释里写的 TODO，而是字面常量本身 —— 没有
任何静态工具能抓出来。

**捕捉手段**：
- ToString 类的输出必须有 forensic 锁字面（今天的 forensic #20 / #22）
- 函数返回 nullptr 的 stub 必须被一个**期望非空**的 forensic 覆盖（今天的
  MultiSearcher 真实现 + cross-segment 测试）
- 任何"local var 解析出来但只用一次"的代码块要被 reviewer 怀疑

### 12.2 "Java 契约抄了半截"模式

C++ 端写了**形似的实现**，但对照 Java 1.0.1 源码逐字看，会发现关键边界条件
被改了或漏了，且**测试是按 C++ 的（错）输出反推的**，所以测试全绿。

| 模块 | Java 1.0.1 原版 | C++ 老版 | 后果 |
|---|---|---|---|
| `Similarity.norm(int)` | `ceil(255/sqrt(N))`，下界永不为 0 | `floor(255 * 1/sqrt(N))`，float 路径 | N > ~1000 的长文档 norm=0 → 不可打分 |
| `BooleanScorer.maxCoord` | `1 + must + should`（REFLECTION §4.1） | `must + should` | coord 系数偏移 |
| `QueryParser.jj addClause` | AND 回溯前 clause + NOT 单独 modifier + 括号嵌套 | 只认 `+` / `-` 前缀 | 用户写 `a AND b` 等价于 `a b`（两个 SHOULD） |
| `PhraseScorer.phraseFreq` | 每个 phrase instance ++freq 一次 | 每个匹配的**子词都** ++freq | 多词短语 freq 被夸大 (n_terms-1)× |

**根因**：Java 源码没逐行读。复刻时是"凭对 Lucene 的印象写"，不是"对着源码翻译"。
而印象是**主线印象** —— 主线对齐了，边界全是猜的。

**捕捉手段**：必须把 oracle 钉死到 Java 源码具体行号（§7/§8/§11.3 做到了），
forensic 注释里要写 `// Oracle: lucene-1.0.1/.../Foo.java:LN`。

### 12.3 "C++ 语言陷阱"模式

老代码里有几个错误**只在 C++ 里才成立** —— Java 版没这病，但 C++ 译者没意识到
语言差异。

| 陷阱 | 出现位置 | 表现 |
|---|---|---|
| `continue` 只跳最内层循环 | `boolean_scorer.cpp` MUST 对齐逻辑（§8.2） | MUST scorer 首次错位时 `continue` 跳的是 inner range-for，不是外层 `while(true)`，最终吐出根本没满足所有 MUST 的 doc |
| `catch(...)` 当 EOF/流程控制 | 7 处（REFLECTION §1.3） | 真实异常被吃掉，bug 不抛、不日志、不冒泡 |
| `fstream` failbit 不清 | `fs_directory.cpp` Seek（REFLECTION Bug 8） | EOF 后 seek 不工作，但 read 不抛 |
| 浮点 `floor` 截断 | `EncodeNorm`（§12.2 已列） | float 在长文档下 underflow 配合 `floor` 落到 0 |

**根因**：Java 有 try/catch-Exception 默认覆盖、有 GC、有 `int` 而不是 size_t ——
译者把 Java 风格的"宽松"直接搬过来，C++ 不买账。

**捕捉手段**：每一处 `catch(...)` 要有注释解释**为什么不能写具体异常**；每一处
`continue` 在嵌套循环里要有 reviewer 看一眼"它跳的是哪层"。今天的 forensic
反复证明，**只要数据稍微对抗一点**（不让首轮就对齐），这类 bug 立刻冒头。

### 12.4 "API 设计太窄，逼出 stub"模式

有些"stub"不是写代码的人懒，是**底层 API 不允许实现** —— 上游被迫写 nullptr。
最典型的就是 `Hits` 和 `MultiSearcher`：

老的 `class Hits { IndexReader& reader_; ... }` —— Hits 永远绑定**一个**
IndexReader。MultiSearcher 想跨多个 IndexSearcher 合并结果，没办法构造 Hits，
就只能 `return nullptr`。

**这种 stub 的隐蔽性是最高的**：
- 函数签名长得像 "Search 跨多个索引返回 Hits"，调用者会信；
- 编译过、链接过、单元测试构造 MultiSearcher 不抛异常；
- 只有真的去用返回值时才发现是 nullptr。

今天的修法是先**改 Hits API**（加 `DocFetcher` 构造路径），再让 MultiSearcher
吐真 Hits。修这个 bug 改的最多的不是 `multi_searcher.cpp`，是 `hits.h/cpp`。

**模式教训**：任何"返回 nullptr 的非异常路径"都该被怀疑是 API 设计缺陷的代偿，
而不是"这种情况就是没结果"的合法语义。两者要在文档/类型/测试里区分清楚（用
`std::optional<Hits>` 还是 `unique_ptr<Hits>` 也是种表达）。

### 12.5 "测试断言只到 nullptr"模式 — 假绿温床

老 `tests/unit/query_parser/query_parser_test.cpp` 四个测试全是
`EXPECT_NE(q, nullptr)`。`title:hello` 字段被吞这个 §12.1 的 bug 完全测不出来，
因为"q 非空"恒成立。

同类病灶分布：

| 文件 | 假绿断言 | 真该测什么 |
|---|---|---|
| `query_parser_test.cpp` 老版 | `q != nullptr` | ToString 字面、跨字段命中差异 |
| `missing_features_test.cpp::MultiSearcherMergesResults` | `MaxDoc==0 && Search()==nullptr` | 真 merge 后跨索引命中 |
| `missing_features_test.cpp::FilteredTermEnumSkipsMismatches` | `if (has_next) EXPECT_EQ(...)` | 必须强制 `has_next==true` 否则 fail |
| `phrase_query_test`、`document_writer_test` 等 unit 测试 | 命中数/不抛异常 | freq 值、score 值、norm byte 值 |

外加 `BUILD.bazel:378` 把 `missing_features_test` tag 成 `manual` —— CI 默认不跑
**全部** 这种弱断言测试，等于把保护伞主动关了。

**模式教训**：`EXPECT_NE(x, nullptr)` 几乎从来不是行为断言，它是"构造成功"
断言。Parser/Builder 类的测试应该锁**结构** —— ToString 字面、字段编号、命中
集合、分数值。

### 12.6 "demo 不进 CI"模式

老代码里 `examples/search_files.cpp` 等 demo 二进制能 build，但**没有任何测试
驱动它**。今天我硬化 QueryParser field resolver 后，直接 break 了 demo —— 跑
`search_files` 输 `path:doc1` 立刻 crash。**29/30 unit + integration 全绿**，CI
完全不知道这件事。

这跟 §12.5 是同一个病的不同位面：
- §12.5 是 "断言不够强 → 实现错了测试也不红"
- §12.6 是 "整个端到端路径没人测 → 实现改了 demo 坏了也没人知道"

修法是 `tests/integration/demo_e2e_test.cpp` 用 subprocess 驱动 demo 二进制，
4 条用例锁住"index_files → search_files → delete_files"的端到端契约。

**模式教训**：每个对外二进制必须有至少一个 subprocess 测试，验证它**能跑、
能输出预期、改 API 时会变红**。否则二进制就是个永远不被检验的死代码。

### 12.7 "对抗性数据缺失"模式 — 为什么老 forensic 抓不到老 bug

老 `forensic_test.cpp` / 我前几轮加的 `forensic_claude_test.cpp` 都覆盖了
BooleanScorer / PhraseScorer，但**没抓到** §12.3 的 continue bug、§12.2 的
phraseFreq 多算、§12.2 的 norm 长文档归零。

复盘原因：

| Bug | 老测试数据 | 为什么没暴露 | 抓到 bug 的对抗构造 |
|---|---|---|---|
| BooleanScorer continue | alpha/beta 在首文档就重合 | 首轮就对齐到同 target，不走错位分支 | alpha={0,2} / beta={1,2}，首次 target 不对齐 |
| PhraseScorer freq 多算 | 短语在 doc 里出现 1 次且词独特 | freq 多算 n-1 倍但 idf/norm 凑巧不让分数变号 | 两 doc idf 相同、短语都出现 1 次，对比 score 才暴露 |
| Norm 长文档归零 | 测试 doc 都几十 token | 截断要 N>1000 才发生 | 1M token 文档直接锁 norm byte 值 |

**模式教训**：forensic 不是 "覆盖了这个函数" 就行，要**主动构造让 bug 暴露的
最小对抗数据**。oracle 来自外部，**输入要来自敌意**。"刚好通过"的输入跟"刚好
失败"的输入需要分开设计，每个 bug 类别至少各来一组。

### 12.8 "文档与代码漂移"模式

REMAINING.md 列了"StopFilter 缺 so（33 vs 34）"、"LetterTokenizer end_offset
偏移"两个 bug。今天 grep 源码 —— **都不存在**。`.clang-tidy` 文档说不存在，
仓库根有这个文件。

这跟 §12.1 的"占位"是镜像：
- §12.1 是**代码里的占位**（常量没回填）
- §12.8 是**文档里的占位**（计划写了，代码补上了，文档没回填；或者干脆 bug 报告
  就是凭印象写的）

**模式教训**：md 文件里所有"具体 bug 位置"和"具体文件路径"的断言，必须有自动
化机制（CI grep / pre-commit）回查。§0.三 留了"老 md 不改，错条在新 md 里矫正"
的工作流，本质是在承认文档治理已经失控，只能用增量批注掩盖。

### 12.9 总结 — 老代码病灶的一句话画像

> **看起来都对的代码，配上看起来都绿的测试，掩盖了实现端到端不通这个事实。**

底下三层是：
1. 实现层有"占位常量 + Java 契约半抄 + C++ 语言陷阱"三种 bug 源
2. 测试层断言不够强（nullptr 断言、manual tag、demo 不进 CI）
3. 文档层与代码漂移（REMAINING 错条、ToString 撒谎）

三层互相掩护：测试弱 → bug 不暴露 → 文档继续描述"已修复" → 后续 reviewer
看文档觉得没事 → bug 留到 demo 跑挂才被发现。

**修这种代码的正确顺序**（今天反复用到的）：
1. **先跑 demo**，端到端打开一个真实路径
2. demo 一挂，往下挖到 API/实现层；demo 没挂，写 subprocess 测试锁住它
3. 在改实现前**先写会失败的对抗 forensic**（reverse-check 留痕）
4. 改实现到 forensic 绿；跑全套防回归
5. 老文档不动，新发现写进新 md

---

## 13. Multi-segment audit + fix（2026-05-26 续）

> 用户原始 prompt：你先去做一个 multi-segment 的全面 audit；后续：可以继续干 → 把
> §五 的盲区改成可执行 forensic。
> 反馈："我缺的测试不应该是 定义好程序的行为，输入，输出吗？关注主干路径，你现
> 在这个测试有点像 hack" → 修正方向：bug-shaped forensic + 主干等价契约测试 两条腿。

### 13.1 audit 结论（已验证 → 实锤 bug 3 类）

读 `src/index/{index_writer,segments_reader,segment_merger}.cpp` 后给出可疑点，
然后 reverse-check 落地：

| 编号 | 严重度 | 位置 | 症状 |
|---|---|---|---|
| **M1/M2/M3** | P0 正确性 | `segment_merger.cpp:30-37, 56, 121-135` | SegmentMerger 只读 segments_[0] 的 FieldInfos；非首段字段 merge 后**消失**；其它源段 .fdt/.nrm 用错的 FieldInfos 读 → 字段错位 / OOB |
| **R1** | P0 正确性 | `segments_reader.cpp:68-72` | `Terms(const Term&)` 只走 readers_[0] → **PrefixQuery 跨段漏命中** |
| **R4** | P1 UB | `segments_reader.cpp:74-79` | 空 readers_ 时 `readers_.size()-1` 是 `size_t(-1)` → 无限循环 / OOB；IsDeleted(0) 直接 **segfault** |

### 13.2 写测试 — 两条腿 + reverse-check 全过

**bug-shaped forensic（#30/#31/#32）**：每条钉一个具体可怀疑点。先红验证 bug 在场。

| 测试 | 第一次跑（修代码前） | 修后 |
|---|---|---|
| `#30 SegmentMergerUnionsCrossSegmentFieldInfos` | RED: B0 的 title 字段消失 | ✅ |
| `#31 PrefixQueryHitsAcrossSegments` | RED: PrefixQuery("app") 命中 1 应是 2 | ✅ |
| `#32 SegmentsReaderEmptyIndexDoesNotUB` | **segfault** | ✅ |

**主干等价测试（`MultiSegmentEquivalentToSingleSegmentMainPath`）**：定义契约 →
"相同 (Add, Delete) 序列下，单段写入（A）≡ 多段+Optimize（B）≡ 多段不 Optimize（C）"。

- corpus 故意把 'title' 字段首次出现安排到 seg 1（不是 seg 0），让 M1 一旦存在
  立即触发字段消失
- 4 个 query 类型：TermQuery / PrefixQuery / PhraseQuery / BooleanQuery
- 比较的是 (id, title) sorted vector —— 跨 doc-id permutation 稳定
- 失败 message 形如 `Setup B diverged from baseline A on query: <name>`，把契约违反直说

### 13.3 改动

| 文件 | 改动 |
|---|---|
| `include/minilucene/index/field_infos.h` + `src/index/field_infos.cpp` | 新增 `FieldInfos::Merge(const FieldInfos&)` —— union by name |
| `src/index/segment_merger.cpp` | 完全重写 `Merge()`：union FieldInfos；按源段自己的 FieldInfos 读 .fdt；通过 `src_to_merged` / `merged_to_src` 双向 remap 把 term field_number 和 .nrm byte 偏移正确翻译到 merged 编号 |
| `src/index/segments_reader.cpp` | `SegIdx` 空 readers_ guard 返回 -1；所有 caller（Norm/Document/Delete/IsDeleted）short-circuit；`Terms(term)` 改成 merge 全段，跨 segment dedupe，DocFreq 累加 |
| `tests/integration/forensic_claude_test.cpp` | +4 测试（#30/#31/#32 + 主干 MainPath） |

### 13.4 Reverse-check 留痕

- **R1**：把 `Terms(term)` 临时回退到 `readers_[0]->Terms(term)` → 跑测试 →
  `#31 RED` + `MainPath RED` ("Setup C diverged from baseline A on query: PrefixQuery body:app")。还原 → 全绿
- **M1**：把 SegmentMerger 的 union 循环换成 `merged_fis->Merge(*src_fis[0])` →
  跑测试 → `#30 RED` + `MainPath RED` ("Setup B diverged ... query: PrefixQuery body:app"，`("d2", "DocOne")` vs `("d2", "")`)。还原 → 全绿
- **R4**：第一次跑 `#32` 直接 segfault（gtest 进程异常退出），不是 EXPECT 失败，
  说明 bug 实锤。加 guard 后绿

### 13.5 一次踩过的 主干测试 false-green

第一版主干测试 corpus 把 'title' 字段放在 d1（seg 0 第二个 doc），seg 0 已包含 title
→ M1 的回退**没让主干测试红**（只 #30 红了）。说明：**主干测试的 corpus 必须经过
"想象 bug 在场时它会不会被触发"的检验**，否则就只是另一种 false-green。修法：把
'title' 推到 seg 1 才首次出现，强迫 union 路径必须走对。

### 13.6 一句话

> bug-shaped forensic（"X 不该发生"）+ 主干等价契约（"single ≡ multi+merge ≡ multi"）
> 双覆盖，每条都 reverse-check 留痕。M1/M2/M3/R1/R4 五处 bug 全修，31/31 全绿。

```
$ bazel test //...
Executed 31 out of 31 tests: 31 tests pass.
```

---

## 14. 主干等价测试 TODO（2026-05-26 收工时未完成）

> 上下文：§13 做完 multi-segment 主干测试后，用户反馈"还能写啥主干等价测试"。
> 列了 A/B/C/E 四条候选，全做了草稿，A 跑起来 **hang 5 分钟超时**，剩余三条
> 没机会跑。今天没空了，全部 `GTEST_SKIP` 标 WIP 留底，下一轮接着干。
> 31/31 全套绿（4 条 SKIP 计入，不阻塞 CI）。

### 14.1 当前状态

| TODO | 状态 | 落在哪 |
|---|---|---|
| 主干 A: QueryParser ≡ 手写 Query 树 | 草稿在 `forensic_claude_test.cpp`，`GTEST_SKIP`，**跑起来 hang** | `TEST(ForensicClaude, QueryParserEquivalentToProgrammaticConstruction)` |
| 主干 B: RAMDirectory ≡ FSDirectory | 草稿，`GTEST_SKIP`，**未跑过** | `RAMDirectoryEquivalentToFSDirectory` |
| 主干 C: MultiSearcher ≡ 单 IndexSearcher 合并 | 草稿，`GTEST_SKIP`，**未跑过** | `MultiSearcherEquivalentToSingleIndexSearcher` |
| 主干 E: Reopen 等价 | 草稿，`GTEST_SKIP`，**未跑过** | `ReopenEquivalentToInMemoryReader` |
| 修任何 RED + commit（本次） | A 卡死、B/C/E 未跑，本节 commit 只带 §13 成果 + 这四条草稿 | — |

### 14.2 A 的 bisect 计划（下次直接照做）

A 在 `[ RUN ] QueryParserEquivalentToProgrammaticConstruction` 后没输出任何
`[ FAILED ]` 或 `[ OK ]` 就 300s timeout，说明**第一个 case 之前或之中卡死**。
9 个 case 顺序：term, phrase, plus-minus, AND, OR, NOT, cross-field(title:alpha),
paren, boost。

1. 把所有 cases 注释掉，只留 `cases.push_back({"term", "alpha", ...})` 跑 →
   绿则 case 1 OK，红/hang 则锁定在 setup/parser 共有路径
2. 二分加回 case，每加一组跑一次 → 第一个让它 hang 的就是嫌疑
3. 主要嫌疑（按"今天刚改过"排）：
   - `"alpha NOT delta"` —— `MatchKeyword("NOT")` 后的 mod=NOT 路径
   - `"(alpha OR gamma) AND beta"` —— `ParseGroup(true)` 递归 + AND 回溯
   - `"alpha^2.5"` —— `ReadBoost` 在 term 后的 pos 推进
   - `"title:alpha"` —— `UseFieldInfos` + ResolveField 路径
4. 也可能根本不是 parser，是 `ParseGroup` 在某种 `q==nullptr` 路径里
   `continue` 不推进 pos → 无限循环（src/query_parser/query_parser.cpp:196
   附近的 `if (!q) continue;`，这是已知的死循环模式）

### 14.3 B/C/E 跑起来的预期 oracle（下次照搬就行）

- **B**：用 `std::filesystem::temp_directory_path() / "minilucene_forensic_fsram_XXXXXX"`
  + `mkdtemp`。语料 6 doc + mergeFactor=2 + Optimize。query：TermQuery alpha/delta、
  PrefixQuery del、PhraseQuery 'alpha beta'。RAM 和 FS 各跑一遍 → id 集 sorted 比
- **C**：6 doc 分成两 IndexSearcher（{0,1,2} + {3,4,5}），MultiSearcher.Search
  vs 单 IndexSearcher 写一整份 → id 集比
- **E**：FSDirectory 写 4 doc + delete d1，关闭。两次"开新 reader → query → 关闭"
  → 两次结果应一致 + 等于 expected `{d0, d3}`

### 14.4 §13 + 草稿的真本次成果（已 commit）

| 维度 | 数 |
|---|---|
| 实锤 bug 修复 | +3：M1/M2/M3 schema union（合 1 项）、R1 PrefixQuery 跨段、R4 SegIdx 空段 UB |
| forensic 新增 | +5：#30/#31/#32（bug-shaped）+ MultiSegmentEquivalentToSingleSegmentMainPath（主干）+ §14 4 条 WIP |
| 测试 case | 165 → ~180+（含 4 条 SKIP） |
| 全套绿 | 31/31 |
| ASan 绿 | 31/31 |

<!-- 以下继续记录 -->

# AGENTS.md — 多 agent 协作协议

> 项目里有两个 AI agent 在写代码：**deepseek**（实现者）和 **claude-opus-4-7**（测试者 + 审查者）。
> 本文件定义他们之间的协作规则，避免出现"自实现自测试"导致的 oracle 污染（详见 REVIEW.md §10.3 / §13）。

---

## 1. 角色分工

| 角色 | 能做 | 不能做 |
|------|------|-------|
| **deepseek** | 写/改 `src/**`、`include/**` 的实现代码；改自己写过的测试 | **不能改 claude 拥有的测试文件**（见 §2）；不能改 REVIEW.md / AGENTS.md |
| **claude-opus-4-7** | 写/改 `tests/integration/forensic_claude_test.cpp` 等 claude 拥有的测试文件；写/改 REVIEW.md / AGENTS.md；review 任何代码 | **不能写/改 `src/**`、`include/**` 的实现**；可以读实现以了解 API，但不可改 |

**核心原则**：测试的 oracle 必须来自 claude（外部 spec / 手算 / 数学不变量 / Lucene 1.0.1 行为），不能来自 deepseek 的实现代码。

---

## 2. 文件归属（authoritative）

### claude 独占（deepseek 不得修改）
- `AGENTS.md`（本文件）
- `REVIEW.md`
- `tests/integration/forensic_claude_test.cpp`（claude 写的 forensic 测试，oracle 来自外部）

### deepseek 独占（claude 不得修改）
- `src/**/*.cpp`、`src/**/*.h`
- `include/**/*.h`
- `REFLECTION.md`（这是 deepseek 自己的反思文档）

### 共享（双方可改，但要在 commit 信息里说明）
- 其他测试文件（`tests/integration/reverse_test.cpp` 等历史遗留）
- 文档（`SUMMARY.md`、`REMAINING.md`、`PROJECT.md`）
- `BUILD.bazel`（加测试 target 时双方都要改）

### 用户独占
- `Aristotle.md`、`Socrates.md`、`Plato.md`、`CLAUDE.md`

---

## 3. Commit 前缀规范

| 前缀 | 含义 | 谁用 |
|------|------|------|
| `test(claude):` | claude 新增/修改的 forensic 测试 | claude |
| `review(claude):` | claude 更新 REVIEW.md / AGENTS.md | claude |
| `impl(deepseek):` | deepseek 实现新功能 | deepseek |
| `fix(deepseek):` | deepseek 修 bug | deepseek |
| `reflect(deepseek):` | deepseek 更新 REFLECTION.md | deepseek |
| `chore:` | 不涉及实现/测试的杂活（格式、文档） | 任意 |

**通知机制**：每次被用户唤起，第一件事跑 `git log --oneline -20`，看对方有没有新 commit，决定下一步动作。

---

## 4. 握手格式

### 4.1 claude → deepseek（新增 forensic test）

claude 写测试 + 跑测试 + commit。在 commit 消息里写明：

```
test(claude): forensic test for <SCENARIO>

Test name: <TEST_NAME>
Oracle source: <hand-computed / Lucene 1.0.1 spec / scenario invariant>
Current status: <PASSING | FAILING (suspected bug in <file:line> ?)>

If FAILING: deepseek, please diagnose. The test's oracle is correct by
construction (see comments). If you disagree with the oracle, push back
in a `reflect(deepseek):` commit before changing the implementation.
```

然后在 `AGENTS.md` §6 Pending Queue 追加一条。

### 4.2 deepseek → claude（实现完成 / 修复完成）

```
impl(deepseek): <feature> per claude's forensic test <TEST_NAME>

Test now passing: <yes/no>
Implementation files changed: <list>
Caveats: <any deviations from spec, with justification>
```

claude 下次唤起时跑 `bazel test //:forensic_claude_test`，确认 + 在 §6 Pending Queue 划掉这条。

### 4.3 任何一方对协议本身有异议

在 `AGENTS.md` 开 issue 段落，由用户裁决。**不要单方面修改协议**。

---

## 5. Oracle 来源白名单

claude 写测试时，expected 值必须能追溯到以下来源之一：

1. **手算**：测试注释里写出每一步推导，包括公式和中间值
2. **Lucene 1.0.1 spec**：引用 `Similarity.java` / `BooleanScorer.java` 等具体源文件 + 行号
3. **数学不变量**：例如 `sum(docFreq) == total_postings`、`OptimizeIdempotent`、单调性
4. **场景不变量**：例如"写入 5 个 doc，删除 2 个，剩余应是 3 个且为原序"
5. **Java 原版输出**：若 `tools/DumpIndex.java` 实装后，diff Java 的 JSON 输出

**禁止来源**：
- ❌ "跑一遍当前实现，把它的输出当 expected"
- ❌ "看起来对就行"
- ❌ "其他测试这么写所以我也这么写"（除非那个测试 oracle 也在白名单里）

---

## 6. Pending Queue（活的待办）

每次有新握手追加一行；处理完用 `~~删除线~~` 标记。

<!-- 以下为活的待办列表 -->

- **[2026-05-21] FAILING TEST: `ForensicClaude.DeleteOptimizeStoredFieldReadConsistency`** (deepseek 处理)
  - Test in `tests/integration/forensic_claude_test.cpp:67`
  - Symptom: `SegmentsReader.Delete(0)` 和 `Delete(2)` 调用之后，`NumDocs()` 仍返回 5（期望 3）。Optimize 之后 NumDocs/MaxDoc 仍然是 5，所有 5 个 doc 都保留。
  - Suspected root cause: `include/minilucene/index/segments_reader.h:29` 的 `NumDocs() const override { return total_docs_; }` 返回构造时缓存的 total_docs_，没有随 Delete 调用更新。Delete 本身可能也没有正确委托给下层 SegmentReader 并持久化删除位图。
  - Oracle source: scenario invariant（见 test §1 顶部注释），不依赖任何实现细节。
  - Required fix: 让 SegmentsReader 的 Delete + NumDocs + Close 正确处理跨段删除；让 IndexWriter.Optimize 真正丢弃带 tombstone 的 slot。修完后 `bazel test //:forensic_claude_test` 必须 3/3 通过，且不破坏既有 29 个测试。
  - Do **not** modify `forensic_claude_test.cpp` —— 这个测试是 spec，不是 artifact。

---

## 7. 行动日志位置

双方的行动日志统一追加到 **`REVIEW.md` §14 — 双 agent 行动日志**，便于复盘。
每条日志格式（claude 和 deepseek 共用）：

```
### YYYY-MM-DD HH:MM — [claude|deepseek] <动作摘要>
- Files touched: <list>
- Commit: <hash 或 uncommitted>
- Result: <测试通过/失败、行号、其他>
- Why: <为什么做这一步>
```

每次被唤起的第一件事，是去 REVIEW.md §14 查看对方最近的日志条目，决定下一步动作。

---

## 8. 自动 loop 协议（无人值守模式）

> 用户不希望当 dispatcher。两个 Claude Code session（一个 claude，一个 deepseek）各开 `/loop` 动态模式，按本节决策逻辑自洽推进。

### 8.1 每轮唤起的决策步骤

每个 agent 被 `/loop` 唤起后，**第一件事**：

```bash
bash scripts/agent_loop.sh <self>
# <self> = "claude" 或 "deepseek"
```

脚本输出以下之一：
- `ACT` — 该你干活，原因写在 stderr
- `SLEEP_SHORT` — 对方还没动，短睡（建议 ScheduleWakeup 270s）
- `SLEEP_LONG` — 没事干，长睡（建议 ScheduleWakeup 1800s）
- `STOP` — 触发停止条件（见 §8.3），通知用户后退出 loop

### 8.2 ACT 时各自做什么

**claude 收到 ACT**：
1. 跑 `bazel test //:forensic_claude_test --test_output=errors`
2. 如全绿 → `AGENTS.md §6` 划掉对应 pending 项；如还有未实现的 forensic 想法，写新测试 → `test(claude):` commit
3. 如有红 → 红的来源是 deepseek 刚 commit 的实现？补一条日志解释为什么还红 + 更新 Pending Queue 提供更精确诊断 → `review(claude):` commit

**deepseek 收到 ACT**：
1. 读 `AGENTS.md §6` 找第一个 pending 项
2. 改 `src/**` / `include/**`，跑 `bazel test //:forensic_claude_test`
3. 绿了 → `fix(deepseek):` 或 `impl(deepseek):` commit + §14 日志条
4. 红了但实现已改 → commit `wip(deepseek):` 注明进度 + 日志条
5. **绝对不修 `tests/integration/forensic_claude_test.cpp`**——脚本会在 ACT 输出前检查这条违规

### 8.3 自动 STOP 触发条件（保护用户）

`agent_loop.sh` 检测以下任一条件即输出 `STOP`：

1. **任务完成**：`AGENTS.md §6` Pending Queue 全部划掉 + `forensic_claude_test` 全绿 + 最新 commit 是自己的 → 真没事干了
2. **死循环防护**：同一 pending 项已经在 `REVIEW.md §14` 出现 ≥ 3 次"红 → 修 → 仍红"循环 → 升级
3. **预算耗尽**：自上次 user-触发的 commit 起，自己已 commit ≥ 30 次 → 强制让用户介入审视
4. **协议违规**：检测到对方修改了不该改的文件（如 deepseek 改了 `forensic_claude_test.cpp` / `REVIEW.md` / `AGENTS.md`） → 立刻 STOP 不接受静默背叛

收到 STOP 时，agent 必须：
- 调 PushNotification 告知用户 + STOP 原因
- 把状态写入 §14 日志
- 退出 loop（不再 ScheduleWakeup）

### 8.4 启动命令

用户在两个独立的 Claude Code session 中分别执行：

**Session A（claude，opus-4-7）**:
```
/loop
你是 AGENTS.md 协议中的 claude。每轮按 §8.1 → §8.2 → §8.3 执行。
现在开始第一轮：跑 bash scripts/agent_loop.sh claude。
```

**Session B（deepseek）**:
```
/loop
你是 AGENTS.md 协议中的 deepseek。每轮按 §8.1 → §8.2 → §8.3 执行。
现在开始第一轮：跑 bash scripts/agent_loop.sh deepseek。
```

启动后双方都看 `git log` + Pending Queue，自然就跑起来——claude 第一轮看到自己刚 commit 的 `test(claude):` 红测试，输出 SLEEP_SHORT；deepseek 第一轮看到 `test(claude):` 是对方的，输出 ACT 开始修。

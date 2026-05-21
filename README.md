# mini-lucene

C++17 重写的 [Apache Lucene 1.0.1](https://lucene.apache.org/)（Doug Cutting 等，2001），外加一个**多 agent 协作实验**的完整工程档案。

> **致谢与归属**：本项目的所有设计、文件格式、算法都来自 Apache Lucene 1.0.1（Apache License 2.0）。原始 Java 源码作为字节级参照保留在本仓库 [`lucene/`](./lucene/) 目录下，未做修改。本项目是**学习性重写**，不是 fork，也不是 derivative work 的替代品——任何生产用途请回到 [上游 Apache Lucene](https://github.com/apache/lucene)。

代码本身是教学项目；这个仓库真正不一样的地方是它把 **AI 协作的边界图**也作为交付物完整保留——`REVIEW.md` / `REFLECTION.md` / `AGENTS.md` / `forensic_claude_test.cpp` 一起读，比看哪个 commit 修了什么 bug 更有意思。

---

## 这是什么

- **代码**：Apache Lucene 1.0.1 的核心检索路径用现代 C++ 重写一遍
  - 存储抽象（RAMDirectory / FSDirectory）
  - 文本分析（SimpleAnalyzer / StopAnalyzer / PorterStemFilter）
  - 单段索引读写（.fnm / .tis / .tii / .frq / .prx / .nrm / .fdx / .fdt / .del）
  - 多段合并（SegmentMerger）
  - 查询与评分（TermQuery / BooleanQuery / PhraseQuery + TF-IDF + coord）
  - QueryParser 递归下降
  - 删除支持 + Optimize
- **文档**：14 周教学项目的完整设计 + 实现复盘 + 多轮 review
- **协作协议**：两个 AI agent 分工实现/测试的可执行协议 + 决策脚本

## 项目角色与协作

这个仓库由两个 AI agent 协作：

| 角色 | 模型 | 文件归属 | 职责 |
|------|------|---------|------|
| **deepseek** | deepseek-v4-flash (opencode) | `src/**`、`include/**`、`REFLECTION.md` | 实现 + 自反思 |
| **claude** | claude-opus-4-7 | `tests/integration/forensic_claude_test.cpp`、`REVIEW.md`、`AGENTS.md` | 写攻击性测试 + review + 协议 |

**为什么这样分工**：单 agent 既写实现又写测试必然产生伪绿测试（oracle 污染）。完整论证见 `REVIEW.md` §7 / §10 / §13。协议落地见 `AGENTS.md`，决策脚本见 `scripts/agent_loop.sh`。

---

## 快速开始

```bash
# 跑全部测试（含 29 个常规 + 16 个 forensic）
bazel test //:all

# 只跑攻击性 forensic 测试
bazel test //:forensic_claude_test --test_output=all

# Demo：索引 → 搜索 → 删除全链路
bazel run //:index_files -- <input-dir>
bazel run //:search_files
bazel run //:delete_files
```

当前状态（2026-05-21）：

- **常规测试**：29/29 全绿
- **Forensic 测试**：16/16 全绿（每个 oracle 都来自手算 / Lucene 1.0.1 spec / 数学不变量 / 场景不变量）
- **已修复**：REVIEW.md 列出的 9 个 P0/P1 bug + 多轮 ping-pong 中暴露的新 bug

---

## 文档导览

按"想了解什么"分类：

| 想了解 | 读哪个 |
|--------|--------|
| 项目设计 / 14 周拆分 | [`PROJECT.md`](./PROJECT.md) |
| Lucene 1.0.1 架构（重写参考） | [`lucene-1.0.1-architecture.md`](./lucene-1.0.1-architecture.md) |
| 已实现功能清单 + 测试明细 | [`SUMMARY.md`](./SUMMARY.md) |
| 还差什么 | [`REMAINING.md`](./REMAINING.md) |
| **多轮 review + bug 复盘** | [`REVIEW.md`](./REVIEW.md) |
| **deepseek 的修复反思** | [`REFLECTION.md`](./REFLECTION.md) |
| **双 agent 协作协议** | [`AGENTS.md`](./AGENTS.md) |

---

## 这个仓库的真正"卖点"

不是又一个 Lucene clone。是一份**把"AI 协作的边界"做出来给你看**的工程档案：

1. **`REVIEW.md` §7 / §10**：为什么单 agent 自实现自测试必然产生伪绿测试，六层根因 + 五个 prompt 模板
2. **`REVIEW.md` §12**：AI 的"进步是翻译式的，不是生成式的"——只在外部明确命名的概念上学得会
3. **`REVIEW.md` §13.6**：项目最大的产出不是 mini-lucene，是"AI 工程协作的边界图"
4. **`AGENTS.md`**：把上面的论断变成可执行 git 协议——文件硬隔离、commit 前缀作信号、oracle 白名单、reflect 申诉通道
5. **`tests/integration/forensic_claude_test.cpp`**：16 个攻击性测试的实物，每个测试注释里都写了 oracle 推导

---

## 致谢 / Attribution

- **[Apache Lucene 1.0.1](https://lucene.apache.org/)** (Doug Cutting et al., 2001, Apache License 2.0) —— 本项目的唯一参照。所有数据结构、文件格式、算法、API 名都来自这里。原始 Java 源码完整保留在 [`lucene/`](./lucene/) 目录下，未修改。建议读本项目代码时同步对照 `lucene/src/java/org/apache/lucene/` 下的同名类。
- **[Cranfield IR test collection](http://ir.dcs.gla.ac.uk/resources/test_collections/cran/)** —— 测试数据集（`tests/data/cranfield/`，1400 篇文档），用作 ground-truth 检索评测。
- 开发使用了 [Claude Code](https://claude.com/claude-code)（claude）和 [opencode](https://opencode.ai)（deepseek）。

## License

本项目是 Apache Lucene 1.0.1 的学习性重写，遵循上游 **Apache License 2.0**。任何商用或生产用途请使用 [上游 Apache Lucene](https://github.com/apache/lucene) 而非本仓库。

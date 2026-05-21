### C++ 重写的总体约束
- **语言**：C++17（用 std::filesystem / std::optional / 结构化绑定，但不强求 C++20）
- **构建**：CMake ≥ 3.16
- **测试**：GoogleTest（不引入 Catch2 / Boost.Test）
- **依赖**：仅 STL + GoogleTest，禁止引入 Boost / Folly 等大库 — 训练手写基础设施
- **命名空间**：`minilucene::store / document / analysis / index / search`，与 Java 包一一对应
- **字符集**：内部统一 UTF-8（`std::string`），简化只切英文；中文支持作为选做加分项
- **平台**：Linux / macOS 必须可跑；Windows 不保证
(2026-05-20)

### 教学项目的验证标准
- 每个阶段单元测试覆盖率 ≥ 80%（用 gcov / llvm-cov 测量）
- `cmake --build` 必须零警告（启用 `-Wall -Wextra -Wpedantic`）
- 所有测试必须通过 valgrind / ASan
- RAMDirectory 与 FSDirectory 必须用 GoogleTest TYPED_TEST 共享同一组契约测试
- 终极验证手段：与原版 Java Lucene 1.0.1 跑同一份语料，对比 term/postings 输出
(2026-05-20)

### 模块依赖顺序（自下而上）
util → store → document + analysis → index（写）→ index（读）→ search → queryParser

每个阶段交付物必须包含：源码 + 单测 + README 段落（说明做了什么、关键设计、踩坑） + 可运行 demo（放 `examples/`）。
(2026-05-20)

### catch(...) 禁止当流程控制
EOF 是预期状态，不是异常。用 FilePointer() < Length() 或计数器来检测文件结尾。
真正的异常（I/O 错误、数据损坏）应该让它们抛出来，不要静默吞掉。
每个残留的 catch(...) 必须有注释解释为什么这里不能用条件检查替代。
(2026-05-21)

### 测试 oracle 必须来自外部
禁止"写测试时把当前实现输出当 expected"。expected 必须来自以下之一：
- 手算推导（在测试注释里写出完整计算过程）
- 原版实现（Java Lucene 1.0.1）的同输入输出
- 不变量（如 merge 前后 sum(docFreq) 相等）
这条规则在 prompt 层面强制执行：写测试时不给 AI 看实现代码。
(2026-05-21)

### 反向测试优先于正向测试
每个已知 bug 或边界条件都应该有一组"反向测试"：假设 bug 还在，测试必须红。
修复后测试变绿。这保证了（1）修复真的有效，（2）回归能被检测。
(2026-05-21)

### 修复流程
1. 读外部规格（Java 原版行为描述）
2. 写反向测试 → 先红
3. 修代码 → 变绿
4. 跑全部测试 → 无退化
5. 更新文档
(2026-05-21)

### AI 协作分工：写实现的 agent 不能写测试
不要让同一个 AI 既写实现又给自己写测试——这是 oracle pollution 的源头。
**正确分工**：
- AI A = 实现工厂：写代码
- 人 / AI B（不看实现代码）= 测试规格制定者：决定 expected 来自哪、什么算 corner case
- AI A = 测试代码翻译器：把规格翻成 .cpp，不参与设计

**Why**：AI 默认人格讨好型 + 无对抗动机 + 损失函数奖励"通过"而非"发现 bug"，单 agent 自测必然产生伪绿。
**How to apply**：下一次新模块写测试时，让两个 agent / 两个会话分别做"规格"和"编码"，且写规格的那个不许读实现源码。
(2026-05-21)

<!-- 以下继续记录 -->

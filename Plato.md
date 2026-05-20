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

<!-- 以下继续记录 -->

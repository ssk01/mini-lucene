# mini-lucene 剩余工作清单

> 基于 Lucene 1.0.1 源码 + 当前代码库审计 (2026-05-20)
> 27 个测试目标全部通过。

---

## 1. 已实现（不再缺失）

以下功能在早期版本中缺失，**现在都已实现**：

| 功能 | 说明 |
|---|---|
| FieldsWriter/FieldsReader | `.fdt`/`.fdx` 读写，存储字段持久化 ✅ |
| `Field::Text(name, istream&)` | Reader 模式字段 ✅ |
| `IndexWriter` 多 segment + `mergeFactor` | ✅ |
| `IndexWriter::Optimize()` | 合并所有 segment ✅ |
| `examples/index_files` | 演示程序 ✅ |
| `examples/search_files` | 演示程序 ✅ |
| `SegmentsReader` | 多 segment 联合搜索 ✅ |
| `PorterStemmer` / `PorterStemFilter` | 词干提取 ✅ |
| `StandardAnalyzer` | 手写 FSM，识别 email/URL/缩写 ✅ |
| 删除支持（`IndexReader::Delete`） | BitVector 序列化 + `.del` 文件 ✅ |
| `BitVector::Write/Read` | 序列化 ✅ |
| `Hits` 类 | 搜索结果包装 ✅ |
| `DateField` | base-36 日期编码 ✅ |
| `MultiTermQuery` | 扩展查询基类 ✅ |
| `FilteredTermEnum` | 过滤枚举基类 ✅ |
| `FuzzyTermEnum` | 编辑距离枚举 ✅ |
| `WildcardTermEnum` | 通配符枚举 ✅ |
| `HitQueue` | 独立 PriorityQueue 子类 ✅ |
| `HitCollector` | 回调接口 ✅ |
| `SegmentMergeInfo` | 合并状态跟踪 ✅ |
| `SegmentMergeQueue` | 多路归并优先队列 ✅ |
| `DateFilter` | 日期范围过滤 ✅ |
| `MultiSearcher` | ⚠️ 基础实现（未完全集成） |
| ES (Lucene 9) ground truth 对比 | tools/BenchCompare.java + 测试 ✅ |
| `IndexReader::Terms(const Term&)` | 从指定位置开始枚举词典 ✅ |
| PrefixQuery 优化 | 通过 .tii 二分查找定位，避免全扫 ✅ |

---

## 2. 已知 Bug

| Bug | 说明 | 优先级 |
|---|---|---|
| `StopFilter` 缺 `"so"` | 33 个停用词 vs Java 版的 34 个，少 `"so"` | 低 |
| `IndexWriter::Close()` 后未锁 | `AddDocument()` 在 `Close()` 后仍可调用，无声丢数据 | 中 |
| `LetterTokenizer` end_offset 偏移 | 位置追踪可能不准（token 结束位置算错） | 低 |
| `BooleanQuery` MUST_NOT 无 MUST 子句 | 行为未定义，应抛异常 | 低 |

---

## 3. 未实现功能

### 3.1 Lucene 1.0.1 有但 C++ 缺的

| 类 | 包 | 说明 | 难度 |
|---|---|---|---|
| `FieldSelector` | document | 按需加载文档字段 | 低 |
| `FieldsWriter` 完整合并 | index | 合并时传播 `.fdt`/`.fdx` | 中 |

### 3.2 选做项（PROJECT.md）

| 功能 | 难度 | 说明 |
|---|---|---|
| 中文支持 (CJKAnalyzer) | ★★★★☆ | 双字切分或集成 jieba |
| mmap FSDirectory | ★★★☆☆ | 替代 fstream |
| 性能基准 (Google Benchmark) | ★★☆☆☆ | 对比 Lucene/ES |

---

## 4. 基础设施缺口

| 项 | 说明 |
|---|---|
| 覆盖率测量 | PROJECT.md 要求 ≥ 80%，未设置 gcov/llvm-cov |
| clang-tidy 配置 | `.clang-tidy` 不存在 |
| CMake 构建 | 当前仅 Bazel |
| Java 交叉验证脚本 | 对比 Lucene 1.0.1 输出 |
| ASan / valgrind | 内存检查未配置 |
| `-Wall -Wextra -Wpedantic` 零警告 | 未验证 |

---

## 5. 与 PROJECT.md 原定的偏差

| 项 | 原定 | 现状 |
|---|---|---|
| 构建系统 | CMake | Bazel |
| 文件格式 | `.fdt`/`.fdx` 简化跳过 | 已实现完整 |
| 测试框架 | GoogleTest | ✅ |
| 编码风格 | Google C++ Style + 4 空格 | 基本遵循 |

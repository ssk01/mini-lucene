# Lucene 1.0.1 架构与功能分析

> 代码统计：103 个 Java 文件，6,517 行代码，6,607 行注释
> 作者：Doug Cutting | 发布时间：~2001年

## 整体架构

Lucene 1.0.1 由 6 个包组成，形成清晰的分层结构：

```
┌─────────────────────────────────────────┐
│          search (查询与评分)              │
├─────────────────────────────────────────┤
│      queryParser (查询语法解析)           │
├──────────────────┬──────────────────────┤
│  index (索引引擎) │  analysis (文本分析)  │
├──────────────────┴──────────────────────┤
│          document (文档模型)              │
├─────────────────────────────────────────┤
│          store (存储抽象层)               │
├─────────────────────────────────────────┤
│          util (工具类)                    │
└─────────────────────────────────────────┘
```

---

## 1. 存储层 (org.apache.lucene.store)

提供文件系统的抽象，使索引可以存储在磁盘或内存中。

| 类 | 职责 |
|---|---|
| `Directory` (abstract) | 文件目录抽象，定义 `list()`/`createFile()`/`openFile()`/`deleteFile()` 等操作 |
| `InputStream` (abstract) | 随机读取流，支持 `readByte()`/`readVInt()`/`readVLong()`/`readString()`/`seek()` |
| `OutputStream` (abstract) | 缓冲写入流，支持 `writeByte()`/`writeVInt()`/`writeVLong()`/`writeString()` |
| `FSDirectory` | 基于文件系统的实现，内部使用 `RandomAccessFile`，单例模式+引用计数 |
| `RAMDirectory` | 基于内存的实现，用于临时 segment 构建 |

**关键设计：VInt/VLong 编码** — 每字节存 7 位数据 + 1 位续标志，小整数只占 1 字节，大量节省倒排表空间。

---

## 2. 文档模型 (org.apache.lucene.document)

| 类 | 职责 |
|---|---|
| `Document` | 文档容器，内部用链表存储多个 Field |
| `Field` | 文档字段，由 name + value 组成 |
| `DateField` | 日期工具类，用 base-36 编码使日期可按字典序排序 |

**Field 的四种工厂方法定义了字段行为：**

| 工厂方法 | Stored | Indexed | Tokenized | 用途 |
|---|---|---|---|---|
| `Field.Keyword()` | Y | Y | N | URL、日期等精确值 |
| `Field.UnIndexed()` | Y | N | N | 只需存储不需搜索的元数据 |
| `Field.Text(name, String)` | Y | Y | Y | 短文本（标题） |
| `Field.Text(name, Reader)` | N | Y | Y | 长文本（正文），不存储原文 |
| `Field.UnStored()` | N | Y | Y | 同上，显式命名 |

---

## 3. 文本分析 (org.apache.lucene.analysis)

**处理链：** `Analyzer` → `Tokenizer`(Reader) → `TokenFilter` → `TokenFilter` → ... → `Token` 流

```
输入文本 ──→ [Tokenizer] ──→ [Filter] ──→ [Filter] ──→ Token 序列
              切词            小写化        停用词过滤
```

**内置 Analyzer：**

| Analyzer | 处理管线 | 适用场景 |
|---|---|---|
| `SimpleAnalyzer` | `LowerCaseTokenizer` | 最简单的按字母切词+小写化 |
| `StopAnalyzer` | `LowerCaseTokenizer` → `StopFilter` | 增加停用词过滤（34个英文停用词） |
| `StandardAnalyzer` | `StandardTokenizer` → `StandardFilter` → `LowerCaseFilter` → `StopFilter` | 生产级，基于语法规则的切词 |

**内置组件：**

- `LetterTokenizer` / `LowerCaseTokenizer` — 按非字母字符切分
- `LowerCaseFilter` — 小写化
- `StopFilter` — 停用词过滤，Hashtable O(1) 查找
- `PorterStemFilter` + `PorterStemmer` — Porter 词干提取算法
- `StandardTokenizer` — JavaCC 生成的语法分词器，识别 email、URL、缩写等
- `Token` — 包含 `termText`、`startOffset`、`endOffset`、`type`

---

## 4. 索引引擎 (org.apache.lucene.index)

### 4.1 索引文件格式

每个 Segment 包含以下文件：

| 文件 | 内容 |
|---|---|
| `segments` | 全局元数据：segment 列表和计数器 |
| `.fnm` | 字段名和元信息 (FieldInfos) |
| `.fdt` | 存储字段的原始值 |
| `.fdx` | 存储字段的文档级索引（按文档号随机访问 .fdt） |
| `.tis` | 词典主文件：按字典序存储所有 Term 及其 TermInfo |
| `.tii` | 词典索引文件：每 128 个 Term 采样一条，加速查找 |
| `.frq` | 词频文件：每个 Term 对应的 <docID, freq> 列表 |
| `.prx` | 位置文件：每个 Term 在文档中的出现位置列表 |
| `.nrm` | 归一化因子：每个文档每个字段 1 字节 |

### 4.2 索引写入流程

```
IndexWriter.addDocument(doc)
    │
    ▼
DocumentWriter（在 RAMDirectory 中构建单文档 Segment）
    ├── 1. 收集字段名 → 写 .fnm
    ├── 2. FieldsWriter → 写 .fdt / .fdx（存储字段原文）
    ├── 3. 倒排（核心步骤）：
    │       对每个 indexed 字段：
    │         Analyzer.tokenStream() → 逐 Token 遍历
    │         → 构建 Hashtable<Term, Posting>
    │         → Posting = { term, freq, positions[] }
    ├── 4. 按 Term 排序所有 Posting
    └── 5. 写倒排数据：
            TermInfosWriter → .tis / .tii
            频率数据 → .frq
            位置数据 → .prx
```

### 4.3 核心索引类

| 类 | 职责 |
|---|---|
| `IndexWriter` | 索引写入入口，管理 segment 的创建和合并 |
| `DocumentWriter` | 将单个 Document 转为一个 segment |
| `FieldInfos` / `FieldInfo` | 管理字段元信息（字段号、是否索引等） |
| `FieldsWriter` / `FieldsReader` | 存储字段的写入/读取 |
| `TermInfosWriter` / `TermInfosReader` | 词典的写入/读取，支持二级索引加速查找 |
| `SegmentInfos` / `SegmentInfo` | Segment 元数据管理 |
| `Term` | 搜索词 = field + text，field 名做 intern 优化 |
| `TermInfo` | 词的元信息 = docFreq + freqPointer + proxPointer |

### 4.4 索引读取

| 类/接口 | 职责 |
|---|---|
| `IndexReader` (abstract) | 查询时读取索引的统一接口 |
| `SegmentReader` | 读取单个 segment |
| `SegmentsReader` | 组合多个 SegmentReader |
| `TermEnum` (abstract) | 遍历词典 |
| `TermDocs` (interface) | 遍历某个 Term 的 <doc, freq> 对 |
| `TermPositions` (extends TermDocs) | 额外提供 `nextPosition()` 获取位置 |

### 4.5 Segment 合并

```
IndexWriter.optimize()
    │
    ▼
SegmentMerger
    ├── mergeFields()  — 合并存储字段
    ├── mergeTerms()   — 用优先队列归并排序多个词典流
    └── mergeNorms()   — 合并归一化因子
```

- `SegmentMergeQueue` (PriorityQueue) — 多路归并的小顶堆
- `SegmentMergeInfo` — 跟踪每个 segment 的归并状态和文档号偏移

---

## 5. 查询与评分 (org.apache.lucene.search)

### 5.1 查询执行流程

```
Searcher.search(query)
    │
    ├── 1. query.sumOfSquaredWeights(searcher)  — 计算 IDF 权重
    ├── 2. query.normalize(1/sqrt(sumSqWeights)) — 归一化
    ├── 3. query.scorer(reader)                  — 创建 Scorer
    └── 4. scorer.score(hitCollector, maxDoc)     — 遍历匹配文档，回调收集
              │
              ▼
         HitCollector.collect(doc, score)
              │
              ▼
         HitQueue (PriorityQueue) → TopDocs → Hits
```

### 5.2 支持的查询类型

| Query 类 | 功能 | Scorer |
|---|---|---|
| `TermQuery` | 精确词匹配 | `TermScorer` |
| `BooleanQuery` | 布尔组合（AND/OR/NOT），最多 32 个 required/prohibited 子句 | `BooleanScorer` |
| `PhraseQuery` | 短语匹配，支持 slop（位置容差） | `ExactPhraseScorer` / `SloppyPhraseScorer` |
| `PrefixQuery` | 前缀匹配，展开为 BooleanQuery | 复用 TermScorer |
| `WildcardQuery` | 通配符（`*` 和 `?`），展开为 BooleanQuery | 复用 TermScorer |
| `FuzzyQuery` | 编辑距离模糊匹配，展开为 BooleanQuery | 复用 TermScorer |

**MultiTermQuery 展开模式：** PrefixQuery/WildcardQuery/FuzzyQuery 都继承自 `MultiTermQuery`，使用 `FilteredTermEnum` 枚举匹配的 Term，然后构建等价的 BooleanQuery。

### 5.3 TF-IDF 评分模型 (Similarity)

```
score(q, d) = coord(q,d) × Σ [ tf(t,d) × idf(t) × boost(t) × norm(field,d) ]
```

| 因子 | 公式 | 含义 |
|---|---|---|
| `tf(freq)` | `√freq` | 词频，平方根抑制高频 |
| `idf(term)` | `log(maxDoc / (docFreq+1)) + 1` | 逆文档频率，稀有词权重更高 |
| `norm(field)` | `255 / √numTerms`（量化为 1 字节） | 字段长度归一化，防止长文档偏置 |
| `coord(q,d)` | `overlap / maxOverlap` | 协调因子，奖励匹配更多子句的文档 |
| `boost` | 用户设定 | 查询级别权重调节 |

### 5.4 搜索相关类

| 类 | 职责 |
|---|---|
| `Searcher` (abstract) | 搜索入口抽象 |
| `IndexSearcher` | 基于 IndexReader 的具体实现 |
| `MultiSearcher` | 组合多个 Searcher |
| `Hits` | 搜索结果封装，延迟加载文档 |
| `TopDocs` / `ScoreDoc` | 评分结果集 |
| `HitQueue` (PriorityQueue) | Top-K 结果收集堆 |
| `Filter` / `DateFilter` | 结果过滤（如日期范围） |

---

## 6. 查询解析器 (org.apache.lucene.queryParser)

JavaCC 生成的递归下降解析器，支持语法：

| 语法 | 示例 | 对应 Query |
|---|---|---|
| 单词 | `hello` | TermQuery |
| 短语 | `"hello world"` | PhraseQuery |
| 布尔 | `+required -prohibited optional` | BooleanQuery |
| 通配符 | `hel*o`, `hell?` | WildcardQuery |
| 前缀 | `hel*` | PrefixQuery |
| 模糊 | `hello~` | FuzzyQuery |
| 字段限定 | `title:hello` | 指定搜索字段 |
| 短语容差 | `"hello world"~2` | PhraseQuery(slop=2) |

---

## 7. 工具类 (org.apache.lucene.util)

| 类 | 职责 |
|---|---|
| `BitVector` | 位数组，用于标记已删除文档。支持 `set/get/clear/count`，可序列化到磁盘 |
| `PriorityQueue` (abstract) | 小顶堆，用于 Top-K 结果收集和多路归并 |
| `Arrays` | Java 1.1 兼容的排序工具 |

---

## 8. 关键设计决策总结

| 设计点 | 实现方式 |
|---|---|
| 存储抽象 | Directory/InputStream/OutputStream 三层抽象，支持 FS 和 RAM 两种实现 |
| 索引结构 | Segment-based，每个 segment 是独立的迷你索引 |
| 倒排格式 | 词典 + 频率文件 + 位置文件分离，VInt 编码 |
| 词典查找 | 二级索引（每 128 项采样），先二分搜索索引再顺序扫描 |
| 删除策略 | 懒删除，用 BitVector 标记，合并时物理删除 |
| 并发模型 | 单写多读，FSDirectory 实例级同步 |
| 评分模型 | 经典 TF-IDF，norm 量化为 1 字节节省空间 |
| 分析器 | 管道模式：Analyzer → Tokenizer → TokenFilter 链 |
| 查询扩展 | Wildcard/Fuzzy/Prefix 统一展开为 BooleanQuery |

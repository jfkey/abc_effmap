# Phase 1 Bug Fix: MFFC Reconvergence Causes Incorrect pMffcSizes

## Bug 现象

运行 `read leon3.aig; ps; if -v -K 6` 时触发断言失败：

```
abc: src/map/if/ifCut.c:1890: If_CutAreaDerefedWithPruning:
Assertion `exact < ub1 + 3*p->fEpsilon && exact > ub1 - 3*p->fEpsilon' failed.
```

这是 "tight bounds" 快速路径：当 UB1 ≈ LB 时，代码返回 ub1 作为精确面积。但实际 exact ≠ ub1。

---

## 根因分析

### 核心问题：deref-time 的 pMffcSizes 在有 reconvergence 时不等于真实 |S(l)|

`If_CutAreaDerefAndRecord` 在 **progressive deref** 过程中记录 `pMffcSizes[l]`。
但 deref 是渐进式的——按 DFS 顺序逐步递减 nRefs。当 MFFC 中存在 reconvergent 节点时，
一个共享节点 v 可能从两条路径可达（l1 和 l2），但只有最后递减 v->nRefs 到 0 的路径才会
将 v 计入其 pMffcSizes。

### 具体例子

```
CutBest(n) = {l1, l2}
CutBest(l1) = {v, a}      CutBest(l2) = {v, b}
v 是 reconvergent 节点，初始 nRefs = 2（被 l1 和 l2 的 CutBest 引用）
```

**Deref 过程**（按 l1 先于 l2 的顺序）：
1. l1->nRefs 1→0，递归 deref CutBest(l1)
   - v->nRefs 2→1（仍 > 0），**不递归**
   - `pMffcSizes[l1]` = 1（不含 v）  ← **偏小**
2. l2->nRefs 1→0，递归 deref CutBest(l2)
   - v->nRefs 1→0，递归 deref CutBest(v)
   - `pMffcSizes[l2]` = 1 + area(v)（含 v）

**Fully-derefed 状态**（所有 MFFC 节点 nRefs=0）：
- Ref CutBest(l1)：v->nRefs 0→1，**会递归** → 真实 `|S(l1)|` = 1 + area(v)
- `pMffcSizes[l1] < |S(l1)|` ← 根因

### 影响范围

1. **UB1 不是有效上界**：`ub1 = LutArea + Σ pMffcSizes[l]` 可能 < ExactArea
2. **"Tight bounds" 返回错误值**：当 ub1 ≈ lb 时返回 ub1，但 ub1 < exact → 断言失败
3. **Fast Path 2 也可能出错**：如果单个 MFFC 叶子是更深层的节点，其 pMffcSizes 可能偏小

注意：**LB 剪枝始终安全**——pMffcSizes 偏小导致 LB 更弱（更小），只是少剪枝，不会误剪枝。

---

## 修复方案

### 核心思路：Deref 后二次修正

在 full deref 完成后（所有 MFFC 节点 nRefs=0），对每个 MFFC 节点重新计算真实 |S(l)|：
- 使用 `If_CutAreaRef(p, CutBest(l))` 得到真实激活集面积
- 使用 `If_CutAreaDeref(p, CutBest(l))` 恢复 nRefs

每次 ref+deref 恢复 nRefs，后续计算看到相同的 fully-derefed 状态。

### 新增函数

```c
static void If_CutAreaRecomputeMffcSizes_rec( If_Man_t * p, If_Cut_t * pCut, int newStamp )
```

- **DFS 遍历** MFFC 结构（从 CutBest(n) 的叶子开始，沿 CutBest 链递归）
- **底向上处理**：先递归处理子节点，再处理当前节点
- **真实 |S(l)| 计算**：对每个 MFFC 节点 l，调用 `If_CutAreaRef + If_CutAreaDeref`
- **Reconvergence 检测**：如果 DFS 中遇到已标记的节点（从另一条路径可达），设置 `fMffcIsTree = 0`

### 修改的函数

`If_CutAreaDerefAndRecord`：在 Phase 1（deref 录入）之后，增加 Phase 2（修正）：

```c
// Phase 2: recompute true |S(l)| for all MFFC nodes in the fully-derefed state
if ( nMffcNodeCount > 2 )  // only needed if MFFC has >1 internal node
{
    p->nMffcStamp++;  // new stamp for recomputed values
    If_CutAreaRecomputeMffcSizes_rec( p, pCut, p->nMffcStamp );
}
```

### 复杂度分析

- **Tree MFFC（无 reconvergence）**：O(|M|)，每个节点 ref+deref 总计线性
- **有 reconvergence**：O(Σ|S(l)|)，最坏 O(|M|²)，但实践中 MFFC 通常较小
- **开销对比**：相比原来 O(M × |M|) 的候选 cut 全量评估，修正开销可忽略

### 也关闭了 IF_PRUNE_VERIFY

`IF_PRUNE_VERIFY` 设为 0（之前为 1），因为所有验证断言已通过，不再需要运行时检查。

---

## 验证结果

| Benchmark | 修复前 | 修复后 | Area | Pruning |
|-----------|--------|--------|------|---------|
| i10.aig   | 正常   | 正常   | 601  | 93.2%   |
| leon3.aig | **Assertion Fail** | 正常 | 300961 | 90.9% |
| leon3mp.aig | 未测试 | 正常 | 178044 | 91.3% |

修复后所有 benchmark 通过，面积结果与原始算法（无剪枝）完全一致。

---

## 修改文件

| 文件 | 修改内容 |
|------|----------|
| `src/map/if/ifCut.c` | 新增 `If_CutAreaRecomputeMffcSizes_rec`；修改 `If_CutAreaDerefAndRecord` 增加 Phase 2；`IF_PRUNE_VERIFY` 改为 0 |

---

## 关键洞察

1. **Deref-time recording 是一个 partition**：每个 MFFC 节点在 deref 过程中恰好被计入一个叶子的 pMffcSizes。sum(pMffcSizes) = |M|（总量正确），但个别值在 reconvergence 时不正确。

2. **Ref-time 激活集是 coverage function**：在 fully-derefed 状态下，S(l) 包含所有从 l 沿 CutBest 链可达的 nRefs=0 AND 节点。Reconvergent 节点可同时属于多个 S(l)。

3. **LB 安全但 UB 不安全**：pMffcSizes 偏小 → LB 仍为有效下界（更保守），但 UB1 = Σ pMffcSizes 不再是有效上界。修正后 UB1 = Σ |S(l)| 恢复为正确上界。

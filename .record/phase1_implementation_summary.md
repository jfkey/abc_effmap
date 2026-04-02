# Phase 1: Submodular Bound Pruning for Exact Area — 实现总结

## 背景与问题

FPGA technology mapping 的 exact area recovery（Mode=2）阶段，对每个被引用的 AIG 节点 n，
对所有候选 cut 调用 `If_CutAreaDerefed`，该函数内部做完整 DFS ref+deref，代价 O(|MFFC|)。
候选 cut 数 M 较大时，总代价 O(M × |MFFC|) 成为瓶颈。

---

## 核心 Idea

### 数学基础：Coverage Function 的子模性

在 `Deref(CutBest(n))` 之后，定义：
- **M** = MFFC 区域（deref 后 nRefs=0 的所有 AND 节点）
- **S(l)** = 叶子 l 的激活集（若 l∈M，则 S(l) = ref l 时递归激活的所有节点；若 l∈E 则 S(l)=∅）

则：
```
ExactArea(C) = LutArea(C) + |⋃_{l ∈ L(C)} S(l)|
```

函数 `f(L) = |⋃ S(l)|` 是 **coverage function**（子模函数），具有边际递减性：
集合越大，新增叶子带来的新覆盖越少。

### 上/下界

| 界 | 公式 | 含义 |
|----|------|------|
| **LB1** | `LutArea + \|{l∈L : l∈M}\|` | 每个 MFFC 叶子至少贡献自身（1个节点） |
| **LB2** | `LutArea + max_{l∈L∩M} \|S(l)\|` | 精确面积至少覆盖最大激活集 |
| **UB1** | `LutArea + Σ_{l∈L∩M} \|S(l)\|` | 各激活集独立求和（忽略重叠） |

**剪枝条件**：若 `LB ≥ bestArea`，该 cut 不可能成为最优，直接跳过（exact，无近似）。

### Fast Path

利用 MFFC 结构的特殊情况快速求解：
- **Fast Path 1**：`nMffcLeaves == 0`（cut 叶子全在 E 中）→ `ExactArea = LutArea`，无需 DFS
- **Fast Path 2**：`nMffcLeaves == 1`（仅一个 MFFC 叶子）→ `ExactArea = LutArea + mffcSize[l]`，O(1)
- **Fast Path 3**：`fMffcIsTree == 1`（MFFC 为树，无 reconvergence）→ 各 S(l) 不相交，`UB1 = LB2 = ExactArea`，O(K) 精确求解

---

## 主要代码修改

### `src/map/if/if.h` — `If_Man_t` 新增字段

```c
// exact area pruning (Phase 1: submodular bounds)
float *   pMffcSizes;    // mffcSize[nodeId]: 节点 l 的激活集大小 |S(l)|
int *     pMffcStamps;   // generation stamp per node
int       nMffcStamp;    // 当前 stamp（每次 deref 自增，惰性失效旧数据）
int       nMffcTotal;    // 上次 deref 的 MFFC 总大小 |M|
int       fMffcIsTree;   // 1 = MFFC 是树（无 reconvergence）

// 统计计数器
ABC_INT64_T nExactPrune_Total;     // Mode=2 评估的总候选 cut 数
ABC_INT64_T nExactPrune_FastPath;  // 通过 fast path 解决的数量
ABC_INT64_T nExactPrune_LBPruned; // 被下界剪枝的数量
ABC_INT64_T nExactPrune_UBAccept; // 被上界直接接受的数量
ABC_INT64_T nExactPrune_Exact;    // 需要完整 ref/deref 的数量
```

### `src/map/if/ifCut.c` — 新增函数

#### `If_CutAreaDerefAndRecord()`
- 替换 Mode=2 中的 `If_CutAreaDeref()`
- 在 DFS deref 过程中，对每个 nRefs 降为 0 的 AND 节点 l，记录：
  - `pMffcSizes[l->Id]` = |S(l)|（递归返回的面积，含自身）
  - `pMffcStamps[l->Id]` = 当前 stamp（用于区分本次与历史数据）
- 同时统计 `nMffcTotal`（MFFC 总大小）和 `fMffcIsTree`（是否树结构）

#### `If_CutAreaDerefedWithPruning()`
替换 Mode=2 中的 `If_CutAreaDerefed()`，执行以下 O(K) 评估：

```
1. 遍历 cut 叶子，收集 MFFC 叶子集合 L∩M 及对应 mffcSizes
2. Fast Path 1: nMffcLeaves == 0  → return LutArea
3. Fast Path 2: nMffcLeaves == 1  → return LutArea + mffcSize[l]
4. LB1 = LutArea + nMffcLeaves;  if LB1 >= bestArea → return -1 (pruned)
5. LB2 = LutArea + max(mffcSize); if LB2 >= bestArea → return -1 (pruned)
6. Fast Path 3 (tree): UB1 = LutArea + sum(mffcSizes) = ExactArea → return UB1
7. Fallback: 完整 If_CutAreaDerefed ref/deref
```

返回 `-1` 表示被剪枝（caller 跳过此 cut）。

#### `If_CutAreaPruningStatsPrint/Reset()`
打印和重置 5 个统计计数器。

### `src/map/if/ifMap.c` — Mode=2 路径

```c
// Deref 阶段
if (Mode == 2)
    If_CutAreaDerefAndRecord(p, If_ObjCutBest(pObj));  // 记录 mffcSizes
else
    If_CutAreaDeref(p, If_ObjCutBest(pObj));

// 评估阶段
if (Mode == 2 && pObj->nRefs > 0) {
    float bestArea = (pCutSet->nCuts > 0) ? pCutSet->ppCuts[0]->Area : IF_FLOAT_LARGE;
    float area = If_CutAreaDerefedWithPruning(p, pCut, bestArea);
    if (area < 0) continue;  // 被 LB 剪枝
    pCut->Area = area;
} else {
    pCut->Area = (Mode == 2) ? If_CutAreaDerefed(p, pCut) : If_CutAreaFlow(p, pCut);
}
```

### `src/map/if/ifMan.c` — 释放新数组

在 `If_ManStop` 中 free `pMffcSizes` 和 `pMffcStamps`。

### `src/map/if/ifCore.c` — 打印/重置统计

在合适位置调用 `If_CutAreaPruningStatsPrint/Reset`。

---

## 实验结果（i10.aig，K=6）

| 指标 | 数值 |
|------|------|
| 面积（LUT 数） | 601（与原始完全一致） |
| Fast Path 节省 | 9.0% |
| LB 剪枝节省 | 84.2% |
| **总 DFS 调用节省** | **93.2%** |
| 需要完整 ref/deref | 6.8% |

结果与原始算法 bit-exact 一致（剪枝条件严格，无近似）。

---

## 关键设计决策

1. **Generation stamp 惰性失效**：每次 deref 自增 `nMffcStamp`，O(1) 使旧数据失效，无需 memset。
2. **返回 -1 表示剪枝**：避免引入额外 bool 参数，caller 用 `if (area < 0) continue` 处理。
3. **bestArea 传入**：评估函数需要知道当前最优面积才能做剪枝判断。
4. **保证 exact**：所有剪枝条件均为严格数学下界，不影响最终结果。

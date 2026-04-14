# Phase 1 Rewrite: 从定义出发的 Lazy Φ 缓存（方案 C）

## 背景

之前的 Phase 1 实现（见 `phase1_implementation_summary.md` 和 `phase1_reconvergence_bug_fix.md`）是一个
"边 deref 边记录 mffcSize → 事后发现 reconvergence 导致值偏小 → Phase 2 兜底二次修正"的三层补丁结构。
本次重构从 `|S(l)|` 的定义出发，一步到位。

## 定义

对节点 n 做 `If_CutAreaDeref(CutBest(n))` 后，所有 MFFC 节点 nRefs=0。
对任何 `nRefs==0 && IsAnd` 的 AND 节点 l，
```
S(l) = {l} ∪ { v ∈ M : 从 l 沿 CutBest chain 经过 nRefs=0 AND 节点可达 v }
```
cut C 的精确面积：
```
ExactArea(C) = LutArea(C) + |⋃_{l ∈ L(C) ∩ M} S(l)|
```

**关键恒等式**：在 fully-derefed 状态下，`|S(l)| = If_CutAreaRef(CutBest(l))`。
`ref+deref` 是一对配对操作，完成后 nRefs 回到调用前。Reconvergent 节点被自动且**精确计数 1 次**：
首次路径使 v.nRefs 0→1 触发递归，其余路径使 v.nRefs 递增但因 >0 被短路。

## 算法（方案 C：Lazy on-demand）

每个 AND 节点 n 的处理（Mode=2, n.nRefs>0）：

1. `If_CutAreaDeref(CutBest(n))` —— 不再录任何 mffcSize
2. 进入 cut 生成+评估循环 `(pCut0, pCut1) ∈ fanin0.cuts × fanin1.cuts`
3. 对每个生成的候选 cut C 调用 `If_CutAreaDerefedWithPruning(p, C, bestArea)`：
   - 对 C 的每个叶子 l：若 `l.nRefs==0 && IsAnd(l)` 且 `pMffcInPhi[l]==0`，
     现场做 `If_CutAreaRef(CutBest(l))` + `If_CutAreaDeref(CutBest(l))` 算 `|S(l)|`，
     缓存到 `pMffcSizes[l]`，标记 `pMffcInPhi[l]=1`，并 push 到 `vMffcPhi`
   - 用缓存的 mffcSize 做 O(K) 剪枝（LB1、LB2）或 trivial-exact 快路径
   - 未剪枝则退回 `If_CutAreaDerefed(C)`（自身 ref+deref 状态守恒）
4. `If_CutAreaRef(CutBest(n))`
5. `If_ManMffcPhiClear(p)` —— 遍历 `vMffcPhi` 复位 `pMffcInPhi`，清空 `vMffcPhi`

**Φ 是隐式的**：`vMffcPhi` 在处理 n 的过程中动态累积——每个在**任一**候选 cut 中出现过的 MFFC 叶子
恰好被 ref+deref 一次，与 strict 3-pass 伪代码在计算量上等价，但免去了显式先扫 pCutSet 构建 Φ 的工序。

## 剪枝细节

`If_CutAreaDerefedWithPruning` 按以下顺序工作：

| 情况 | 判据 | 返回值 |
|------|------|--------|
| `nLeaves < 2` | — | `0` |
| `nMffcLeaves == 0` | 所有叶子 ∈ E | `LutArea` |
| `nMffcLeaves == 1` | 唯一 MFFC 叶子 | `LutArea + |S(l)|` |
| LB 剪枝 | `max(LB1, LB2) ≥ bestArea - ε` | `-1`（调用方 `continue`） |
| Fallback | 其余 | `If_CutAreaDerefed(C)` |

```
LB1 = LutArea + |L(C) ∩ M|       // 每个 MFFC 叶子至少贡献自身
LB2 = LutArea + max_l |S(l)|     // 并集至少覆盖最大单集
```

`nMffcLeaves==0/1` 两条快路径是**精确**的，不是近似——前者 ExactArea=LutArea，后者
`ExactArea = LutArea + |S(l)|`（单集时并集 = 该集）。

## 主要代码改动

### `src/map/if/if.h`
- 删除：`pMffcStamps`, `nMffcStamp`, `nMffcTotal`, `fMffcIsTree`
- 新增：`char * pMffcInPhi`, `Vec_Int_t * vMffcPhi`
- 删除 extern：`If_CutAreaDerefAndRecord`
- 新增 extern：`If_ManMffcPhiClear`

### `src/map/if/ifCut.c`
- 删除三个旧函数：`If_CutAreaDerefAndRecord_rec`, `If_CutAreaRecomputeMffcSizes_rec`, `If_CutAreaDerefAndRecord`
- 重写 `If_CutAreaDerefedWithPruning`（lazy Φ 缓存 + LB 剪枝 + 两条 trivial-exact 快路径）
- 新增 `If_ManMffcPhiClear`

### `src/map/if/ifMap.c`
- Mode=2 的 deref 改回普通 `If_CutAreaDeref`
- 在 `If_CutAreaRef(CutBest(n))` 之后调用 `If_ManMffcPhiClear` 复位节点内缓存

### `src/map/if/ifMan.c`
- 释放 `pMffcInPhi`, `vMffcPhi`

## 相对前版的收益

1. **概念统一**：单一路径——"deref → 按需 ref+deref 算 S(l) → 用 LB 剪枝 → 必要时 fallback"。
   没有"deref 时录错值 → Phase 2 修正 → stale fallback"的三层逻辑。
2. **去掉 stamp 机制**：`vMffcPhi` 显式 O(|Φ|) 清理，比 generation stamp 更直观且无溢出顾虑。
3. **去掉 tree/reconvergence 特判**：reconvergence 在定义层被 ref 的 nRefs 单调性自然吸收。
4. **每叶一次 ref+deref**：与修复版相同的总代价，但不再有 "Phase 1 录 + Phase 2 重算"的双算。

## 验证结果（K=6, `if -v -K 6`）

| Benchmark | 面积 (LUT) | 与旧版一致 | 新 DFS 节省 | 旧 DFS 节省 |
|-----------|-----------|-----------|------------|-------------|
| i10        | 601       | ✓ | 99.3% | 93.2% |
| leon3      | 300949    | ✓ | 96.1% | 96.1% |
| leon3mp    | 178043    | ✓ | 95.8% | 95.8% |

i10 上 DFS 节省从 93.2% 提升到 99.3%，原因是两条 trivial-exact 快路径（nMffcLeaves 0/1）
覆盖面比旧版"纯 tree MFFC"fast path 更广——在 i10 这种小 MFFC 居多的电路上尤为显著。

运行时启用 `IF_PRUNE_VERIFY=1` 内部断言（每个剪枝/快路径结果对照全量 `If_CutAreaDerefed`），
三个 benchmark 全部通过，证明剪枝严格精确。

## 代码不变性

- `If_CutAreaDerefedWithPruning` 退出时 `nRefs` 状态保持与入口一致（fully-derefed M₀）
- `pMffcInPhi[id]==1` ⇔ `id ∈ vMffcPhi` ⇔ `pMffcSizes[id]` 本节点内有效
- 节点间隔离由 `If_ManMffcPhiClear` 保证：进入下一节点时 `vMffcPhi` 为空，所有 `pMffcInPhi` 为 0

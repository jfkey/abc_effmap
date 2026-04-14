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


abc 01> read ../../benchmarks/div.aig; if -v -K 6; time; ps
K = 6. Memory (bytes): Truth =    0. Cut =   72. Obj =  152. Set =  744. CutMin = no
Node =   57247.  Ch =     0.  Total mem =    9.21 MB. Peak cut mem =    0.37 MB.
P:  Del =  864.00.  Ar =   24259.0.  Edge =   109702.  Cut =   999182.  T =     0.46 sec
P:  Del =  864.00.  Ar =   21743.0.  Edge =   102883.  Cut =   907343.  T =     0.45 sec
P:  Del =  864.00.  Ar =   35484.0.  Edge =   139497.  Cut =  1105744.  T =     0.55 sec
E:  Del =  864.00.  Ar =   28816.0.  Edge =   119589.  Cut =  1105744.  T =     0.07 sec
F:  Del =  864.00.  Ar =   26051.0.  Edge =    86102.  Cut =  1158395.  T =     0.55 sec
E:  Del =  864.00.  Ar =   25149.0.  Edge =    84426.  Cut =  1158395.  T =     0.05 sec
FPGA If_CutAreaDeref/Ref profiling [AreaFlow]:
  Total time         :       0.02 sec
  Top-level calls    : 266120
  Max MFFC size      : 204  (nodes visited in single DFS)
  Average MFFC size  : 1.24
  Total nodes visited: 329351
A:  Del =  864.00.  Ar =   22279.0.  Edge =    78210.  Cut =   944184.  T =     0.62 sec
E:  Del =  864.00.  Ar =   22148.0.  Edge =    77883.  Cut =   944184.  T =     0.04 sec
A:  Del =  864.00.  Ar =   22031.0.  Edge =    77718.  Cut =   939341.  T =     0.64 sec
E:  Del =  864.00.  Ar =   22030.0.  Edge =    77706.  Cut =   939341.  T =     0.04 sec
FPGA If_CutAreaDeref/Ref profiling [ExactArea]:
  Total time         :       0.28 sec
  Top-level calls    : 2784436
  Max MFFC size      : 184  (nodes visited in single DFS)
  Average MFFC size  : 1.55
  Total nodes visited: 4326663
Exact area pruning statistics:
  Total candidates   : 502448
  Fast path (no DFS) : 404891  ( 80.6%)
  LB pruned          : 97542  ( 19.4%)
  Full ref/deref     : 15  (  0.0%)
  Saved DFS calls    : 100.0%
Total time =     3.46 sec
elapse: 3.59 seconds, total: 3.59 seconds
../../benchmarks/div          : i/o =  128/  128  lat =    0  nd = 22030  edge =  77706  aig  = 86046  lev = 864





abc 03> read ../../benchmarks/leon3.aig; if -v -K 6; time; ps
K = 6. Memory (bytes): Truth =    0. Cut =   72. Obj =  152. Set =  744. CutMin = no
Node = 1088122.  Ch =     0.  Total mem =  274.13 MB. Peak cut mem =   17.48 MB.
P:  Del =   13.00.  Ar =  494756.0.  Edge =  2206743.  Cut = 15584284.  T =     7.64 sec
P:  Del =   13.00.  Ar =  305089.0.  Edge =  1435594.  Cut = 10991429.  T =     5.59 sec
P:  Del =   13.00.  Ar =  304975.0.  Edge =  1291991.  Cut = 15122288.  T =     7.15 sec
E:  Del =   13.00.  Ar =  304076.0.  Edge =  1288241.  Cut = 15122288.  T =     1.08 sec
F:  Del =   13.00.  Ar =  301809.0.  Edge =  1290458.  Cut = 12313072.  T =     5.51 sec
E:  Del =   13.00.  Ar =  301668.0.  Edge =  1289831.  Cut = 12313072.  T =     1.09 sec
FPGA If_CutAreaDeref/Ref profiling [AreaFlow]:
  Total time         :       0.54 sec
  Top-level calls    : 3022672
  Max MFFC size      : 609  (nodes visited in single DFS)
  Average MFFC size  : 2.41
  Total nodes visited: 7294548
A:  Del =   13.00.  Ar =  301045.0.  Edge =  1246442.  Cut = 11753300.  T =     7.85 sec
E:  Del =   13.00.  Ar =  300981.0.  Edge =  1246364.  Cut = 11753300.  T =     1.07 sec
A:  Del =   13.00.  Ar =  300950.0.  Edge =  1246219.  Cut = 11603342.  T =     7.66 sec
E:  Del =   13.00.  Ar =  300949.0.  Edge =  1246218.  Cut = 11603342.  T =     1.07 sec
FPGA If_CutAreaDeref/Ref profiling [ExactArea]:
  Total time         :       4.61 sec
  Top-level calls    : 37893548
  Max MFFC size      : 605  (nodes visited in single DFS)
  Average MFFC size  : 1.88
  Total nodes visited: 71090209
Exact area pruning statistics:
  Total candidates   : 3620437
  Fast path (no DFS) : 2392808  ( 66.1%)
  LB pruned          : 1086159  ( 30.0%)
  Full ref/deref     : 141470  (  3.9%)
  Saved DFS calls    :  96.1%
Total time =    45.98 sec
Duplicated 67965 gates to decouple the CO drivers.
elapse: 52.25 seconds, total: 55.84 seconds
../../benchmarks/leon3        : i/o =370159/252691  lat =    0  nd =368969  edge =1322367  aig  =1333705  lev = 13
abc 05> 






#define IF_PRUNE_VERIFY  1 的结果在下面

ap -v; 
/mnt/local_data1/liujunfeng/newMap/abc_effmap/build/abc
source -s abc.rc
read ../../benchmarks/div.aig; read_lib ../../benchmarks/asap7_clean.lib; ps; map -v; time; ps 
read ../../benchmarks/div.aig; if -v -K 6; time; ps 
read ../../benchmarks/leon3.aig; if -v -K 6; time; ps 
read ../../benchmarks/leon3.aig; map -v; time; ps 
read ../../benchmarks/leon3.aig; ps; if -v -K 6; time; ps 
read ../../benchmarks/div.aig; if -v -K 6; time; ps
read ../../benchmarks/leon3.aig; if -v -K 6; time; ps
=====================================================
abc 01> read ../../benchmarks/div.aig; if -v -K 6; time; ps
K = 6. Memory (bytes): Truth =    0. Cut =   72. Obj =  152. Set =  744. CutMin = no
Node =   57247.  Ch =     0.  Total mem =    9.21 MB. Peak cut mem =    0.37 MB.
P:  Del =  864.00.  Ar =   24259.0.  Edge =   109702.  Cut =   999182.  T =     0.48 sec
P:  Del =  864.00.  Ar =   21743.0.  Edge =   102883.  Cut =   907343.  T =     0.46 sec
P:  Del =  864.00.  Ar =   35484.0.  Edge =   139497.  Cut =  1105744.  T =     0.57 sec
E:  Del =  864.00.  Ar =   28816.0.  Edge =   119589.  Cut =  1105744.  T =     0.07 sec
F:  Del =  864.00.  Ar =   26051.0.  Edge =    86102.  Cut =  1158395.  T =     0.57 sec
E:  Del =  864.00.  Ar =   25149.0.  Edge =    84426.  Cut =  1158395.  T =     0.05 sec
FPGA If_CutAreaDeref/Ref profiling [AreaFlow]:
  Total time         :       0.02 sec
  Top-level calls    : 266120
  Max MFFC size      : 204  (nodes visited in single DFS)
  Average MFFC size  : 1.24
  Total nodes visited: 329351
A:  Del =  864.00.  Ar =   22279.0.  Edge =    78210.  Cut =   944184.  T =     0.74 sec
E:  Del =  864.00.  Ar =   22148.0.  Edge =    77883.  Cut =   944184.  T =     0.04 sec
A:  Del =  864.00.  Ar =   22031.0.  Edge =    77718.  Cut =   939341.  T =     0.76 sec
E:  Del =  864.00.  Ar =   22030.0.  Edge =    77706.  Cut =   939341.  T =     0.04 sec
FPGA If_CutAreaDeref/Ref profiling [ExactArea]:
  Total time         :       0.44 sec
  Top-level calls    : 3789302
  Max MFFC size      : 184  (nodes visited in single DFS)
  Average MFFC size  : 1.65
  Total nodes visited: 6246317
Exact area pruning statistics:
  Total candidates   : 502448
  Fast path (no DFS) : 404891  ( 80.6%)
  LB pruned          : 97542  ( 19.4%)
  Full ref/deref     : 15  (  0.0%)
  Saved DFS calls    : 100.0%
Total time =     3.78 sec
elapse: 3.92 seconds, total: 3.92 seconds
../../benchmarks/div          : i/o =  128/  128  lat =    0  nd = 22030  edge =  77706  aig  = 86046  lev = 864
abc 03> read ../../benchmarks/leon3.aig; if -v -K 6; time; ps
K = 6. Memory (bytes): Truth =    0. Cut =   72. Obj =  152. Set =  744. CutMin = no
Node = 1088122.  Ch =     0.  Total mem =  274.13 MB. Peak cut mem =   17.48 MB.
P:  Del =   13.00.  Ar =  494756.0.  Edge =  2206743.  Cut = 15584284.  T =     7.39 sec
P:  Del =   13.00.  Ar =  305089.0.  Edge =  1435594.  Cut = 10991429.  T =     5.58 sec
P:  Del =   13.00.  Ar =  304975.0.  Edge =  1291991.  Cut = 15122288.  T =     8.11 sec
E:  Del =   13.00.  Ar =  304076.0.  Edge =  1288241.  Cut = 15122288.  T =     1.42 sec
F:  Del =   13.00.  Ar =  301809.0.  Edge =  1290458.  Cut = 12313072.  T =     5.85 sec
E:  Del =   13.00.  Ar =  301668.0.  Edge =  1289831.  Cut = 12313072.  T =     1.24 sec
FPGA If_CutAreaDeref/Ref profiling [AreaFlow]:
  Total time         :       0.59 sec
  Top-level calls    : 3022672
  Max MFFC size      : 609  (nodes visited in single DFS)
  Average MFFC size  : 2.41
  Total nodes visited: 7294548
A:  Del =   13.00.  Ar =  301045.0.  Edge =  1246442.  Cut = 11753300.  T =     8.73 sec
E:  Del =   13.00.  Ar =  300981.0.  Edge =  1246364.  Cut = 11753300.  T =     1.09 sec
A:  Del =   13.00.  Ar =  300950.0.  Edge =  1246219.  Cut = 11603342.  T =     9.25 sec
E:  Del =   13.00.  Ar =  300949.0.  Edge =  1246218.  Cut = 11603342.  T =     1.22 sec
FPGA If_CutAreaDeref/Ref profiling [ExactArea]:
  Total time         :       6.14 sec
  Top-level calls    : 44851482
  Max MFFC size      : 605  (nodes visited in single DFS)
  Average MFFC size  : 1.93
  Total nodes visited: 86665057
Exact area pruning statistics:
  Total candidates   : 3620437
  Fast path (no DFS) : 2392808  ( 66.1%)
  LB pruned          : 1086159  ( 30.0%)
  Full ref/deref     : 141470  (  3.9%)
  Saved DFS calls    :  96.1%
Total time =    50.13 sec
Duplicated 67965 gates to decouple the CO drivers.
elapse: 56.89 seconds, total: 60.80 seconds
../../benchmarks/leon3        : i/o =370159/252691  lat =    0  nd =368969  edge =1322367  aig  =1333705  lev = 13
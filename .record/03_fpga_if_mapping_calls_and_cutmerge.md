# FPGA IF Mapping：函数调用关系 & 每轮 Cut Merge 的必要性

参考：`src/map/if/ifCore.c`、`ifMap.c`、`ifMan.c`、`ifCut.c`、`ifUtil.c`、`if.h`
配合阅读：`.record/01_fpga_if_exact_area.txt`、`.record/02_fpga_if_mapping_qa.txt`

---

## 1. 顶层函数调用关系

```
If_ManPerformMapping (ifCore.c:86)
 ├─ If_ManSetupCiCutSets         为 CI 建立平凡 cut（pCutSet/CutBest）
 ├─ If_ManSetupSetAll             预分配 cutSet 内存池
 ├─ If_ManReverseOrder            得到反向拓扑序（给 required 计算用）
 └─ If_ManPerformMappingComb (ifCore.c:110)
     │
     ├── 阶段 A：Delay 映射
     │    └─ If_ManPerformMappingRound(Mode=0, fPreprocess=1, fFirst=1, "Delay")
     │    [若 fPreprocess]
     │       ├─ If_ManResetOriginalRefs  + Round(Mode=0,fFirst=0,"Delay-2")
     │       └─ If_ManResetOriginalRefs  + Round(Mode=0,fFirst=0,fArea=1,"Area")
     │
     ├── If_ManImproveMapping         cut expand/reduce（可选）
     │
     ├── 阶段 B：Area-Flow 恢复  循环 nFlowIters 次
     │    └─ If_ManPerformMappingRound(Mode=1, fPreprocess=0, fFirst=0, "Flow")
     │        (+ If_ManImproveMapping)
     │
     └── 阶段 C：Exact-Area 恢复  循环 nAreaIters 次
          └─ If_ManPerformMappingRound(Mode=2, fPreprocess=0, fFirst=0, "Area")
              (+ If_ManImproveMapping)
```

### `If_ManPerformMappingRound` 内部 (ifMap.c:689)

```
If_ManPerformMappingRound
 ├─ 按 Mode/fArea/fFancy 设置 p->SortMode (0=delay, 1=area, 2=fancy)
 ├─ 拓扑序遍历每个节点:
 │    ├─ If_ObjIsAnd    → If_ObjPerformMappingAnd(Mode, fPreprocess, fFirst)
 │    │                   (若 fRepr 再 If_ObjPerformMappingChoice)
 │    ├─ If_ObjIsCi/Co  → 设置/传递 arrTime（tim manager 场景）
 │    └─ Const1          → arrTime = -INF
 └─ If_ManComputeRequired           算 required time / AreaGlo / nNets，
                                     间接 If_ManScanMapping 重算 nRefs
```

### `If_ObjPerformMappingAnd` (ifMap.c:162) 关键步骤

```
1. 更新 EstRefs:
     Mode=0 → EstRefs = nRefs
     Mode=1 → EstRefs = (2*EstRefs + nRefs) / 3
     Mode=2 → 不改 EstRefs
2. Deref 旧 best cut:   if (Mode && nRefs>0) If_CutAreaDeref(CutBest)
3. pCutSet = If_ManSetupNodeCutSet(pObj)    ← 本轮全新分配的候选池
4. if (!fFirst):
     用当前 Mode 的口径重算旧 CutBest 的 Delay/Area/Edge/Power
     把它先塞进 pCutSet[0] 作为种子候选
5. 双重循环枚举 (fanin0.cuts × fanin1.cuts):
     If_CutMerge / If_CutMergeOrdered  → 形成新 cut
     Truth / DSD / user function
     计算 Delay
     计算 Area:
       Mode=0,1 → If_CutAreaFlow (area-flow)
       Mode=2   → If_CutAreaDerefed (exact area, ref+deref 试算)
     If_CutSort 插入到 pCutSet（按 SortMode 保序，保留 top-K）
6. 选 best:  CutBest ← pCutSet[0]   (满足 required 时)
7. 追加平凡 cut {pObj} 到 pCutSet 末尾
8. Ref 新 best cut:  if (Mode && nRefs>0) If_CutAreaRef(CutBest)
9. If_ManDerefNodeCutSet(pObj)              ← 把候选池回收到内存池
```

注意第 3 步和第 9 步：`pCutSet` 是节点级的临时容器，处理完就还给 `p->pMemSet` 池；**下一轮再来，又从零构建**。这正是下面问题 1 的根因。

---

## 2. 为什么每一轮都要再做一次 Cut Merge？

### 原因 1：候选 cut 集不跨轮持久化

只有嵌在 `If_Obj_t` 里的 `CutBest`（单个）在节点上永存；节点的候选集 `pCutSet` 是
`If_ManSetupNodeCutSet` 动态 fetch、`If_ManDerefNodeCutSet` 立刻 recycle 的，
生命周期仅限于"这一轮处理该节点的这一小段时间"。所以：

> 下一轮要想从一个"候选池"里挑，就必须重新 merge 把候选池造出来。
> 若不 merge，每个节点只剩 CutBest 这一个候选 + 平凡 cut，等于没有选择空间。

### 原因 2：fanin 的 CutBest 可能已经改变

IF 是拓扑序前向 DP。一个节点的所有候选 cut 由 `fanin0.cuts × fanin1.cuts` 交叉
merge 得来。上一轮 area-flow/exact-area 过程中，fanin（及其再上游）的 CutBest
很可能被换掉了，原来的 cut 组合就不再是当前 fanin 空间的完整枚举，必须重新合并
才能反映"在新上游结构下本节点的最佳 K-LUT 覆盖方案"。

### 原因 3：评分函数本身变了

每轮的 Mode / SortMode 不同：

| Mode | SortMode | 用的 Area 指标                     | nRefs 语义                 |
| ---- | -------- | ---------------------------------- | -------------------------- |
| 0    | 0 (delay)| `If_CutAreaFlow` (走 EstRefs)      | AIG structural fanout      |
| 1    | 1 (area) | `If_CutAreaFlow` (走 EstRefs)      | mapped-circuit refs        |
| 2    | 1 (area) | `If_CutAreaDerefed` (精确 deref/ref)| mapped-circuit refs       |

即使候选 cut 结构本身一样，面积分数也要用新口径重算、重新排序、重新挑 top-K。
不重新 merge 就无法保留排序意义下"真正的"候选 top-K。

### 原因 4：Priority Cut 的哲学

IF mapper 是 priority cut 框架：全局候选可能爆炸，只保留每个节点 top-K。
`If_CutSort` 是流式插入保序，依赖"候选逐个到来"这个过程。跨轮保留旧候选集
就破坏了优先级队列的新鲜度；每轮重来最干净。

---

## 3. 当 `fFirst = 0` 时，为什么也要做 Cut Merge？

### `fFirst` 参数的真实含义

`fFirst` 只控制一件事：**是否把"上一轮留下的 CutBest"作为一个种子候选塞进
本轮的 pCutSet**。看 ifMap.c:190–259 这段代码：

```c
pCut = If_ObjCutBest(pObj);
if ( !fFirst ) {
    // 用本轮 Mode 的指标重算旧 best 的 Delay/Area/Edge/Power
    pCut->Delay = ... ;
    pCut->Area  = (Mode==2)? If_CutAreaDerefed(p,pCut) : If_CutAreaFlow(p,pCut);
    ...
    // 把旧 best 作为首个候选
    If_CutCopy( p, pCutSet->ppCuts[pCutSet->nCuts++], pCut );
}
```

- `fFirst = 1`（首轮 delay 映射）：此时 CutBest 还没有被严肃赋值过，不值得
  作为种子，直接进入 merge 从零枚举候选。
- `fFirst = 0`（后续所有轮）：CutBest 是上一轮的优胜者，值得保留为保底候选；
  先把它用**本轮口径**重新打分再塞进候选池。

**`fFirst` 与是否做 merge 无关**。不管 fFirst 怎么取，Step 5 的
`If_ObjForEachCut(...) If_CutMerge(...)` 双重循环都会照常执行。

### 为什么 fFirst=0 时仅靠 CutBest 不够

如果 fFirst=0 就跳过 merge，那本轮候选池里最多只有：
1. 上一轮的 CutBest（用新指标重算分）
2. 平凡 cut `{pObj}` 本身

这意味着：
- 永远找不到比上一轮更好的 cut，area recovery 完全退化。
- 上游 fanin 的 CutBest 变化带来的新组合机会被完全浪费。
- 退化到"只会原地打分 + 永远保留旧 best"，exact area 循环 `nAreaIters` 次
  也不会有任何改进。

所以 fFirst=0 的语义是"**把旧 best 作为保底，再通过 cut merge 探索是否能
找到更好的**"——merge 是"探索新可能"的动作，缺它就失去 recovery 的全部意义。
保留旧 best 只是一层 fail-safe：即便枚举出的新 cut 全都比旧 best 差，
`If_CutSort` 也会把旧 best 排到第一，`pCutSet->ppCuts[0]` 赋回 CutBest，
保证本轮至少不退步。

### 小结：fFirst 与 merge 的关系表

| 场景      | fFirst | 做 merge? | 种子候选（pCutSet[0]）                     |
| --------- | ------ | --------- | ------------------------------------------ |
| 第一轮 Delay  | 1  | 是        | 无（直接 merge 生成）                       |
| Delay-2 / Area 预处理  | 0  | 是 | 旧 CutBest（按新指标重评）                   |
| Flow 轮   | 0      | 是        | 旧 CutBest（按 area-flow 重评）             |
| Exact-Area 轮 | 0  | 是        | 旧 CutBest（按 exact area 重评）            |

---

## 4. 一句话总结

- **cut merge 是"候选集生成"**，`CutBest` 只是候选集里的一个优胜者。候选集每轮
  用完就回收，所以每轮都要重新 merge 才能有候选可挑。
- **fFirst 控制的是是否保留上轮 CutBest 作为种子**，和"要不要 merge"正交。
  fFirst=0 表示"把上一轮赢家也拉进来一起竞标"，但新候选仍然要靠 merge 产生，
  否则 area/exact-area 恢复就没有任何优化空间。


## 5. IF_PRUNE_VERIFY 的作用

 F_PRUNE_VERIFY 是 src/map/if/ifCut.c:1680 的一个编译期开关宏，用来控制剪枝路径（If_CutAreaDerefedWithPruning）是否开启运行时自检。

- **= 1（开启验证，调试模式** 
  在三条"捷径"返回之前，额外调用一次全量 If_CutAreaDerefed 作为 ground truth，用 assert 对比结果：
  - nMffcLeaves == 0 快路径：断言 exact ≈ lutArea（ifCut.c:1736-1739）
  - nMffcLeaves == 1 快路径：断言 exact ≈ lutArea + |S(l)|（ifCut.c:1748-1751）
  - LB 剪枝路径：断言 exact > bestArea（确认这个 cut 确实劣于当前最优，没有误剪）（ifCut.c:1762-1765）

  代价：每次走剪枝路径都要再跑一次精确计算，性能开销大，仅用于验证 lazy-phi / LB 公式的正确性。

- **= 0（关闭验证，生产模式**
  所有 #if IF_PRUNE_VERIFY 块被预处理器剔除，快路径/LB 路径直接返回，无额外开销。按 .record/phase1_reconvergence_bug_fix.md:95-97 的记录，在
   Phase 1 所有验证断言已通过、重收敛 bug 修完后就改回了 0。
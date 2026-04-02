结合代码 /mnt/local_data1/liujunfeng/newMap/abc_effmap/src/aig/aig/aigMffc.c 
/mnt/local_data1/liujunfeng/newMap/abc_effmap/src/map/mapper/mapperRefs.c
 理解并梳理什么是 MFFC， 如何计算的，MFFC 的作用， 
 要清晰准确，并保存在 /mnt/local_data1/liujunfeng/newMap/abc_effmap/.record/intro_mffc.txt

  1. 定义

  MFFC（Maximum Fanout-Free Cone） 是针对一个节点 n 定义的：

  ▎ 节点 n 的 MFFC 是从 n 出发沿着 fan-in 方向能到达的最大的节点集合，其中集合内的每个节点（除了 n 本身）的所有 fan-out 都在该集合内部。

  换句话说：
  - 从节点 n 开始，向输入方向（fan-in 方向）回溯
  - 如果一个节点的所有 fan-out 都最终汇聚到 n（即它不被 n 的 cone 之外的任何节点使用），则它属于 n 的 MFFC
  - 一旦遇到一个节点有 fan-out 指向 n 的 cone 之外，停止在该方向的扩展

  2. "Fanout-Free" 的含义

  "Fanout-Free" 指的是锥体内的节点（除了根节点 n）没有"自由的"fan-out——它们的所有输出都被锥体内部消耗掉了。也就是说，这些节点仅为 n 服务，不被电路的其他部分使用。

  3. 如何计算 MFFC

  计算 MFFC 的算法（深度优先/广度优先）：

  MFFC(n):
    1. 初始化集合 S = {n}
    2. 对 n 的每个 fan-in 节点 c：
       a. 将 c 的引用计数减 1（模拟"移除 n 对 c 的依赖"）
       b. 如果 c 的引用计数变为 0（说明 c 的所有 fan-out 都在 S 中）：
          - 将 c 加入 S
          - 递归地对 c 的 fan-in 重复步骤 2
       c. 如果 c 的引用计数 > 0（说明 c 还有 S 之外的 fan-out）：
          - 不扩展，c 是 MFFC 的边界
    3. 恢复所有引用计数
    4. 返回 S

  直觉：我们从 n 出发，把"只为 n 服务"的节点全部纳入锥体，直到碰到"还被别人使用"的节点为止。

  4. 物理含义

  MFFC 的物理含义非常重要：

  ▎ MFFC 中的节点（除了根 n 和 PI）是"如果 n 被移除，也可以被移除"的节点。

  因为它们的所有 fan-out 都汇聚到 n，没有其他用户。如果 n 不存在了（例如被重新映射到不同的 cut），MFFC 中的这些节点也变成了死代码（dead logic）。
 
 
结合  mapperMatch.c 中 exact area 相关代码，以及 mapperCore.c 中 area recovery 的调用流程
以及 /mnt/local_data1/liujunfeng/newMap/abc_effmap/.record/fpga06_full.pdf
梳理 exact area 表示什么样的物理意义， 公式等，如何计算 exact area， exact area 与MMFC 之间的关系是什么， 
并澄清，在 exact area 计算的时候，应该是借用了 mffc的思想，还是直接在mmfc 上做的，我理解的是在 mapping之后的 circuit 上，重新deref 的， 
将 其详细地整理到: /mnt/local_data1/liujunfeng/newMap/abc_effmap/.record/intro_exact_area.txt

Step 1：旧 cut 被真实 Deref（不是模拟）

  // mapperMatch.c:284-288
  if ( pNode->nRefAct[fPhase] > 0 || (...) )
      pMatch->AreaFlow = Area1 = Map_CutDeref( pCutBest, fPhase, ... );
                                // ↑ 真正递减 nRefAct，释放 MFFC！
  else
      pMatch->AreaFlow = Area1 = Map_CutGetAreaDerefed( pCutBest, fPhase );

  关键：如果节点在当前 mapping 中被引用（nRefAct > 0），调用的是 Map_CutDeref 而不是 Map_CutGetAreaDerefed。

  Map_CutDeref 会真正修改 ref counts：
  - 递归遍历旧 cut 的 cone
  - 对每个 fan-in：--nRefAct[fPhaseChild]
  - 只有当 ref count 降到 0 时才递归（这就是 MFFC 的定义！）
  - 返回 Area1 = 被释放的节点的面积 = 旧 cut 的 MFFC 面积

  执行完 Step 1 后：旧 MFFC 内的节点 ref count 全部降为 0，它们"空出来了"。

  ---
  Step 2：新 cut 在"旧 MFFC 已释放"的状态下被评估

  // Map_MatchNodeCut 中，mode=2/3 时：
  pMatch->AreaFlow = Map_CutGetAreaDerefed( pCut, fPhase );

  Map_CutGetAreaDerefed 内部做的是：
  ref(pCut);   // 临时引用新 cut
  deref(pCut); // 撤销
  return deref的结果; // = 新 cut 需要的独占资源

  由于旧 cut 已经 deref 了，旧 MFFC 的节点现在 ref=0。如果新 cut 的 cone 包含这些节点：
  - ref(new_cut) 时，这些节点 ref=0 → 会被递归引用 → 面积被计入
  - deref(new_cut) 时，同样会被递归 deref
  - 但净效果：这些节点对新 cut 来说是"可重用"的，其面积会被正确算入 exact area

  这里最精妙的地方：不同的新 cut 候选可能重用不同数量的旧 MFFC 节点，exact area 自动反映了这一差异。

  ---
  Step 4：选定新 cut 后真实 Ref

  // mapperMatch.c:341-351
  if ( p->fMappingMode >= 2 && (pNode->nRefAct[fPhase] > 0 || ...) )
      Area2 = Map_CutRef( pNode->pCutBest[fPhase], fPhase, ... );
  //  assert( Area2 < Area1 + p->fEpsilon );  ← 注释掉的断言！

  Map_CutRef(new_cut) 真实引用新 cut，返回 Area2 = 新 cut 实际引入的新面积。

  那行被注释掉的断言揭示了算法的本质：
  assert( Area2 < Area1 )
  即：新 cut 引入的面积 ≤ 旧 cut 释放的面积（净减少面积）。

  ---
  净收益的真实计算

  净收益 = Area1 - Area2
         = 旧 MFFC 释放面积 - 新 cut 引入面积

  Area1 在 Step 1 被计算并存入 Area1 变量。Area2 在 Step 4 被计算。二者的差就是真正节省的面积。



结合 
/mnt/local_data1/liujunfeng/newMap/abc_effmap/.record/intro_exact_area.txt
理解 exact area recovery，
解释 为什么 exact area recovery 计算是 ASIC techchnology mapping的性能瓶颈， 

而相对来说，FPGA exact area 计算则没有那么耗时，
/mnt/local_data1/liujunfeng/newMap/abc_effmap/src/map/if

将 其详细地整理到: /mnt/local_data1/liujunfeng/newMap/abc_effmap/.record/intro_exact_area_compare.txt



结合我之前 对 exact area 的梳理 
/mnt/local_data1/liujunfeng/newMap/abc_effmap/.record/intro_exact_area_compare.txt
/mnt/local_data1/liujunfeng/newMap/abc_effmap/.record/intro_exact_area.txt

统计，并输出 ASIC 算子 map  和 FPGA算子 If 在进行 1. 映射过程中 Map_CutRefDeref 所消耗的所有时间, 
2. 以及统计 单次 Map_CutRefDeref 在 DFS 的过程中 所  访问的 最大 affect region ( MFFC size) 
3. 以及 average  MFFC size

在这个过程中有任何不清楚的地方  请随时与我沟通 



在 /mnt/local_data1/liujunfeng/newMap/abc_effmap/.record/intro_exact_area.txt
/mnt/local_data1/liujunfeng/newMap/abc_effmap/.record/intro_exact_area_compare.txt
梳理了 为什么 exact area 为什么是 ASIC mapping 的性能瓶颈，
（我理解的就是 node -> cut -> supergate -> fphase 太多， ==> 映射的可能性太多 而导致的性能瓶颈）
请深入思考：
1. 如果现在让你优化这部分的性能瓶颈，并且有很好的算法理论分析，你会从什么方向切入呢，
2. 增量图 计算的思想 /mnt/local_data1/liujunfeng/newMap/abc_effmap/.record/BoundLS_clean.pdf 引入到 asic mapping 过程中 是否可能，这个问题大概如何切入，和boundls 之间的区别是什么地方 

a. 将上面的思考和回答 梳理到 /mnt/local_data1/liujunfeng/newMap/abc_effmap/.record/ 下面 
b. 在这个过程中有任何不清楚的地方  请随时与我沟通 




/mnt/local_data1/liujunfeng/newMap/abc_effmap/.record/intro_exact_area.txt 中 整理 ASIC exact area 如何计算的， 
现在 整理下：
/mnt/local_data1/liujunfeng/newMap/abc_effmap/src/map/if FPGA 中 map exact area 是如何计算的， 到 record 下

此外，
我对mapping 的中间状态有一些疑问：
1. 首先会计算每个节点 对应的cut 集，以及 每个节点的best cut，当在第一轮mapping 完成之后，从 PO -> PI 来生成 mapped circuit 
这个 mapped circuit 是如何组织的呢，mapped circuit 的边是什么， number of reference 是如何记录的呢 

2. 在exact area recovery 是的时候， 会首先 Deref 旧 cut， deref cut 的时候，是如何 删除 mapped circuit 的边 ，如何影响 mapped circuit的连接关系的 

3. a. 在exact area recovery 是的时候， 会首先 Deref 旧 cut， deref cut 的时候，旧cut 所对应的 MFFC 是不是就变成游离的点，会影响电路的逻辑吗
   b. 此外，在这之后，会评估新的cut，会进行ref操作，此时不是只将  新cut 的叶子节点给连接就行了吗，为什么会dfs。 旧cut 的mmfc的那些节点怎么办呢 

并将上面的回答 也整理到 record 下 



下面文件中 梳理了 FPGA mapping exact area 计算的时候 MFFC 的一些 性质：
/mnt/local_data1/liujunfeng/newMap/abc_effmap/.record/fpga_if_mapping_qa.txt
/mnt/local_data1/liujunfeng/newMap/abc_effmap/.record/fpga_if_exact_area.txt

首先 在 deref 之后，

---------- 旧 cut 的 MFFC 节点怎么办 ----------

旧 cut 的 MFFC 内的节点在 Deref 后 nRefs=0，有三种命运：

  情况 1: 被新 cut 的 Ref 过程"复活"
    如果新 cut 的 cone 经过这些节点，Ref 递归时会让它们 nRefs 回到 > 0。
    → 这些节点继续在 mapped circuit 中存在

  情况 2: 不被新 cut 使用，但被其他节点使用
    不会发生——因为它们在 Deref 时 nRefs 降到了 0，
    说明之前只有当前节点的旧 cut 在使用它们。

  情况 3: 不被新 cut 使用，也不被其他节点使用
    它们保持 nRefs=0，成为真正的"死节点"。
    → 在 mapped circuit 中不存在
    → 但在 AIG 数据结构中仍然存在（不被删除）
    → 如果后续处理其他节点时，某个新 cut 的 cone 经过它们，
       它们可能被"复活"

    这些"死节点"不会造成任何问题：
    - AIG 结构不变（它们的 pFanin0/pFanin1 仍然正确）
    - CutBest 不变（如果被重新引用，可以直接使用）
    - 最终输出 mapped circuit 时，If_ManScanMapping 只从 PO 遍历，
      nRefs=0 的节点不会被访问到

所以只能是 情况 1和3 ，所以在这种情况下，我感觉是不是可以不用对所有 cut 都进行 ref， 而只要对cut 中的部分 点 进行 ref 就行了， 
即：避免对cut 进行枚举，转而对cut 的 点集进行枚举进行了，以提高效率。 

这是我的大概思路： 

考虑 一个 节点的 n 的 fanin x, y ，  假设 x 的割集 C_{x}, y 的割集  C_{y} 
在 Deref 的时候，  记录 C_{x} 中的 每个点 n 所对应的 MFFC 的 大小， |MFFC_{n}|
记录 C_{y} 中的 每个点 n 所对应的 MFFC 的 大小, | MFFC_{n}| 

对于 新的 cut  ，是不是 就可以 不进行实际的 ref 了，
直接对cut 中的每个点 sum_{x \in Cut} |MFFC_{x}|  找出这个值最小 cut
就是 此时的 best cut ，然后再更新 实际的 ref 操作进行了。 

（可能存在的问题 "找出这个值最小 cut" 可能不够准确，由于 back DFS 会 meet 到一起？ ）

请理解我的思路 结合 exact area recovery 的定义，深度思考我的思路是否正确，或者是否有其他改进的思路。 


我准备沿着 子模函数的角度，去减少ref - deref 的操作。 
在建模为 子模函数 之后，我们可以利用上界和下界去剪枝，
这个上界 和下界 希望是从 某些推论 或者 跟 exact area recovery 问题特性得到的，你觉得在利用 子模函数 的特性

在剪枝设计的时候，有什么不同：

  1. 剪枝：用上界快速排除差的 cut

  你的 sum |MFFC_x| 虽然不精确，但它是 exact area 的一个上界（因为重复计数只会多不会少）。可以这样用：

  // 快速计算上界
  upper_bound = LutArea(cut) + sum_{leaf in cut, leaf.nRefs==0} precomputed_MFFC[leaf]

  // 如果上界都比当前 best_area 差，跳过精确计算
  if (upper_bound >= best_area)
      continue;  // 剪枝！

  // 否则做精确计算
  exact_area = If_CutAreaDerefed(p, pCut);

  这样可以避免对很多明显差的 cut 做昂贵的 ref+deref 操作。

  2. 快速下界

  对于 nRefs > 0 的叶子（已经在 mapped circuit 中的），它们对 exact area 的贡献为 0。所以：

  lower_bound = LutArea(cut) + count(leaf.nRefs==0 && IsAnd(leaf))

  至少每个 nRefs=0 的 AND 叶子需要一个 LUT（自己），所以这是下界。

  如果 lower_bound >= best_area，也可以剪枝。



认真思考、扩展上面的内容，将 问题定义，形成子模函数， 上下界设计， 推论， 问题特性等 各方面内容 都仔细梳理清楚，并输出到 /mnt/local_data1/liujunfeng/newMap/abc_effmap/.record 中




理解 /mnt/local_data1/liujunfeng/newMap/abc_effmap/.record/submodular_exact_area_pruning.txt 
/mnt/local_data1/liujunfeng/newMap/abc_effmap/.record/fpga_if_mapping_qa.txt
/mnt/local_data1/liujunfeng/newMap/abc_effmap/.record/fpga_if_exact_area.txt
中关于 FPGA  exact area 的分析和讨论，

首先实现 /mnt/local_data1/liujunfeng/newMap/abc_effmap/src/map/if if map 中
利用   Phase 1 (低代价，高收益):
    - 在 Deref 时记录 mffcSize[l]（修改 If_CutAreaDeref）
    - 实现 LB1 和 UB1 的 O(K) 评估
    - 在 If_CutAreaDerefed 调用前加入 LB1/UB1 剪枝
    - 实现 Fast Path 1/2/3
对 exact area 的评估加速 ，

在这个过程中有任何不清楚的地方  请随时问我



现在的代码 实现了 /mnt/local_data1/liujunfeng/newMap/abc_effmap/.record/submodular_exact_area_pruning.txt  中
利用   Phase 1 (低代价，高收益):
    - 在 Deref 时记录 mffcSize[l]（修改 If_CutAreaDeref）
    - 实现 LB1 和 UB1 的 O(K) 评估
    - 在 If_CutAreaDerefed 调用前加入 LB1/UB1 剪枝
    - 实现 Fast Path 1/2/3
对 exact area 的评估加速 ，

大概的修改：

 Phase 1: Submodular Bound Pruning for Exact Area

  Changes

  if.h — Added to If_Man_t:
  - pMffcSizes[] / pMffcStamps[] / nMffcStamp — per-node MFFC size storage with generation stamps to distinguish current vs stale entries
  - nMffcTotal / fMffcIsTree — MFFC metadata
  - 5 pruning statistics counters

  ifCut.c — New functions:
  - If_CutAreaDerefAndRecord() — modified deref that records |S(l)| (activation set size) for each MFFC node during recursion, using generation stamps for correctness
  - If_CutAreaDerefedWithPruning() — O(K) evaluation with:
    - Fast Path: nMffcLeaves == 0 → ExactArea = LutArea
    - Fast Path: nMffcLeaves == 1 → ExactArea = LutArea + mffcSize
    - LB1 pruning: LutArea + |L∩M| >= bestArea → skip
    - LB2 pruning: LutArea + max(mffcSize) >= bestArea → skip
    - Tight bounds: UB1 ≈ LB → exact value known
    - Fallback: full If_CutAreaDerefed ref/deref
  - If_CutAreaPruningStatsPrint/Reset() — statistics

  ifMap.c — Mode=2 path uses recording deref + pruning evaluation

  ifMan.c — Frees new arrays in If_ManStop

  ifCore.c — Prints/resets pruning stats

  Results on i10.aig (K=6)

  - Area: 601 LUTs — identical to original
  - 93.2% of DFS calls saved: 9.0% fast path + 84.2% LB pruned
  - Only 6.8% of candidate cuts required the expensive ref/deref DFS



请整理 主要的idea 和 主要的修改 到  /mnt/local_data1/liujunfeng/newMap/abc_effmap/.record 目录下

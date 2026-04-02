/**CFile****************************************************************

  FileName    [ifCut.c]

  SystemName  [ABC: Logic synthesis and verification system.]

  PackageName [FPGA mapping based on priority cuts.]

  Synopsis    [Cut computation.]

  Author      [Alan Mishchenko]
  
  Affiliation [UC Berkeley]

  Date        [Ver. 1.0. Started - November 21, 2006.]

  Revision    [$Id: ifCut.c,v 1.00 2006/11/21 00:00:00 alanmi Exp $]

***********************************************************************/

#include "if.h"

ABC_NAMESPACE_IMPL_START


////////////////////////////////////////////////////////////////////////
///                        DECLARATIONS                              ///
////////////////////////////////////////////////////////////////////////

// Profiling variables for If_CutAreaDeref / If_CutAreaRef
static int    s_IfRD_Depth      = 0;       // current recursion depth
static int    s_IfRD_CurSize    = 0;       // MFFC size of current top-level call
static int    s_IfRD_MaxSize    = 0;       // max MFFC size across all calls
static ABC_INT64_T s_IfRD_TotalSize  = 0;  // sum of all MFFC sizes
static ABC_INT64_T s_IfRD_CallCount  = 0;  // number of top-level calls
static abctime     s_IfRD_TotalTime  = 0;  // total time in If_CutAreaDeref+Ref

////////////////////////////////////////////////////////////////////////
///                     FUNCTION DEFINITIONS                         ///
////////////////////////////////////////////////////////////////////////

/**Function*************************************************************

  Synopsis    [Check correctness of cuts.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
static inline int If_CutVerifyCut( If_Cut_t * pBase, If_Cut_t * pCut ) // check if pCut is contained in pBase
{
    int nSizeB = pBase->nLeaves;
    int nSizeC = pCut->nLeaves;
    int * pB = pBase->pLeaves;
    int * pC = pCut->pLeaves;
    int i, k;
    for ( i = 0; i < nSizeC; i++ )
    {
        for ( k = 0; k < nSizeB; k++ )
            if ( pC[i] == pB[k] )
                break;
        if ( k == nSizeB )
            return 0;
    }
    return 1;
}
int If_CutVerifyCuts( If_Set_t * pCutSet, int fOrdered )
{
    static int Count = 0;
    If_Cut_t * pCut0, * pCut1; 
    int i, k, m, n, Value;
    assert( pCutSet->nCuts > 0 );
    for ( i = 0; i < pCutSet->nCuts; i++ )
    {
        pCut0 = pCutSet->ppCuts[i];
        assert( pCut0->uSign == If_ObjCutSignCompute(pCut0) );
        if ( fOrdered )
        {
            // check duplicates
            for ( m = 1; m < (int)pCut0->nLeaves; m++ )
                assert( pCut0->pLeaves[m-1] < pCut0->pLeaves[m] );
        }
        else
        {
            // check duplicates
            for ( m = 0; m < (int)pCut0->nLeaves; m++ )
            for ( n = m+1; n < (int)pCut0->nLeaves; n++ )
            assert( pCut0->pLeaves[m] != pCut0->pLeaves[n] );
        }
        // check pairs
        for ( k = 0; k < pCutSet->nCuts; k++ )
        {
            pCut1 = pCutSet->ppCuts[k];
            if ( pCut0 == pCut1 )
                continue;
            Count++;
            // check containments
            Value = If_CutVerifyCut( pCut0, pCut1 );
//            assert( Value == 0 );
            if ( Value )
            {
                assert( pCut0->uSign == If_ObjCutSignCompute(pCut0) );
                assert( pCut1->uSign == If_ObjCutSignCompute(pCut1) );
                If_CutPrint( pCut0 );
                If_CutPrint( pCut1 );
                assert( 0 );
            }
        }
    }
    return 1;
}

/**Function*************************************************************

  Synopsis    [Returns 1 if pDom is contained in pCut.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
static inline int If_CutCheckDominance( If_Cut_t * pDom, If_Cut_t * pCut )
{
    int i, k;
    assert( pDom->nLeaves <= pCut->nLeaves );
    for ( i = 0; i < (int)pDom->nLeaves; i++ )
    {
        for ( k = 0; k < (int)pCut->nLeaves; k++ )
            if ( pDom->pLeaves[i] == pCut->pLeaves[k] )
                break;
        if ( k == (int)pCut->nLeaves ) // node i in pDom is not contained in pCut
            return 0;
    }
    // every node in pDom is contained in pCut
    return 1;
}

/**Function*************************************************************

  Synopsis    [Returns 1 if the cut is contained.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
int If_CutFilter( If_Set_t * pCutSet, If_Cut_t * pCut, int fSaveCut0 )
{ 
    If_Cut_t * pTemp;
    int i, k;
    assert( pCutSet->ppCuts[pCutSet->nCuts] == pCut );
    for ( i = 0; i < pCutSet->nCuts; i++ )
    {
        pTemp = pCutSet->ppCuts[i];
        if ( pTemp->nLeaves > pCut->nLeaves )
        {
            // do not fiter the first cut
            if ( i == 0 && ((pCutSet->nCuts > 1 && pCutSet->ppCuts[1]->fUseless) || (fSaveCut0 && pCutSet->nCuts == 1)) )
                continue;
            // skip the non-contained cuts
            if ( (pTemp->uSign & pCut->uSign) != pCut->uSign )
                continue;
            // check containment seriously
            if ( If_CutCheckDominance( pCut, pTemp ) )
            {
//                p->ppCuts[i] = p->ppCuts[p->nCuts-1];
//                p->ppCuts[p->nCuts-1] = pTemp;
//                p->nCuts--;
//                i--;
                // remove contained cut
                for ( k = i; k < pCutSet->nCuts; k++ )
                    pCutSet->ppCuts[k] = pCutSet->ppCuts[k+1];
                pCutSet->ppCuts[pCutSet->nCuts] = pTemp;
                pCutSet->nCuts--;
                i--;
            }
         }
        else
        {
            // skip the non-contained cuts
            if ( (pTemp->uSign & pCut->uSign) != pTemp->uSign )
                continue;
            // check containment seriously
            if ( If_CutCheckDominance( pTemp, pCut ) )
                return 1;
        }
    }
    return 0;
}

/**Function*************************************************************

  Synopsis    [Prepares the object for FPGA mapping.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
int If_CutMergeOrdered_( If_Man_t * p, If_Cut_t * pC0, If_Cut_t * pC1, If_Cut_t * pC )
{ 
    int nSizeC0 = pC0->nLeaves;
    int nSizeC1 = pC1->nLeaves;
    int nLimit  = pC0->nLimit;
    int i, k, c, s;

    // both cuts are the largest
    if ( nSizeC0 == nLimit && nSizeC1 == nLimit )
    {
        for ( i = 0; i < nSizeC0; i++ )
        {
            if ( pC0->pLeaves[i] != pC1->pLeaves[i] )
                return 0;
            p->pPerm[0][i] = p->pPerm[1][i] = p->pPerm[2][i] = i;
            pC->pLeaves[i] = pC0->pLeaves[i];
        }
        pC->nLeaves = nLimit;
        pC->uSign = pC0->uSign | pC1->uSign;
        p->uSharedMask = Abc_InfoMask( nLimit );
        return 1;
    }

    // compare two cuts with different numbers
    i = k = c = s = 0;
    p->uSharedMask = 0;
    if ( nSizeC0 == 0 ) goto FlushCut1;
    if ( nSizeC1 == 0 ) goto FlushCut0;
    while ( 1 )
    {
        if ( c == nLimit ) return 0;
        if ( pC0->pLeaves[i] < pC1->pLeaves[k] )
        {
            p->pPerm[0][i] = c;
            pC->pLeaves[c++] = pC0->pLeaves[i++];
            if ( i == nSizeC0 ) goto FlushCut1;
        }
        else if ( pC0->pLeaves[i] > pC1->pLeaves[k] )
        {
            p->pPerm[1][k] = c;
            pC->pLeaves[c++] = pC1->pLeaves[k++];
            if ( k == nSizeC1 ) goto FlushCut0;
        }
        else
        {
            p->uSharedMask |= (1 << c);
            p->pPerm[0][i] = p->pPerm[1][k] = p->pPerm[2][s++] = c;
            pC->pLeaves[c++] = pC0->pLeaves[i++]; k++;
            if ( i == nSizeC0 ) goto FlushCut1;
            if ( k == nSizeC1 ) goto FlushCut0;
        }
    }

FlushCut0:
    if ( c + nSizeC0 > nLimit + i ) return 0;
    while ( i < nSizeC0 )
    {
        p->pPerm[0][i] = c;
        pC->pLeaves[c++] = pC0->pLeaves[i++];
    }
    pC->nLeaves = c;
    pC->uSign = pC0->uSign | pC1->uSign;
    assert( c > 0 );
    return 1;

FlushCut1:
    if ( c + nSizeC1 > nLimit + k ) return 0;
    while ( k < nSizeC1 )
    {
        p->pPerm[1][k] = c;
        pC->pLeaves[c++] = pC1->pLeaves[k++];
    }
    pC->nLeaves = c;
    pC->uSign = pC0->uSign | pC1->uSign;
    assert( c > 0 );
    return 1;
}

/**Function*************************************************************

  Synopsis    [Prepares the object for FPGA mapping.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
int If_CutMergeOrdered( If_Man_t * p, If_Cut_t * pC0, If_Cut_t * pC1, If_Cut_t * pC )
{ 
    int nSizeC0 = pC0->nLeaves;
    int nSizeC1 = pC1->nLeaves;
    int nLimit  = pC0->nLimit;
    int i, k, c, s;

    // both cuts are the largest
    if ( nSizeC0 == nLimit && nSizeC1 == nLimit )
    {
        for ( i = 0; i < nSizeC0; i++ )
        {
            if ( pC0->pLeaves[i] != pC1->pLeaves[i] )
                return 0;
            pC->pLeaves[i] = pC0->pLeaves[i];
        }
        pC->nLeaves = nLimit;
        pC->uSign = pC0->uSign | pC1->uSign;
        return 1;
    }

    // compare two cuts with different numbers
    i = k = c = s = 0; 
    if ( nSizeC0 == 0 ) goto FlushCut1;
    if ( nSizeC1 == 0 ) goto FlushCut0;
    while ( 1 )
    {
        if ( c == nLimit ) return 0;
        if ( pC0->pLeaves[i] < pC1->pLeaves[k] )
        {
            pC->pLeaves[c++] = pC0->pLeaves[i++];
            if ( i == nSizeC0 ) goto FlushCut1;
        }
        else if ( pC0->pLeaves[i] > pC1->pLeaves[k] )
        {
            pC->pLeaves[c++] = pC1->pLeaves[k++];
            if ( k == nSizeC1 ) goto FlushCut0;
        }
        else
        {
            pC->pLeaves[c++] = pC0->pLeaves[i++]; k++;
            if ( i == nSizeC0 ) goto FlushCut1;
            if ( k == nSizeC1 ) goto FlushCut0;
        }
    }

FlushCut0:
    if ( c + nSizeC0 > nLimit + i ) return 0;
    while ( i < nSizeC0 )
        pC->pLeaves[c++] = pC0->pLeaves[i++];
    pC->nLeaves = c;
    pC->uSign = pC0->uSign | pC1->uSign;
    return 1;

FlushCut1:
    if ( c + nSizeC1 > nLimit + k ) return 0;
    while ( k < nSizeC1 )
        pC->pLeaves[c++] = pC1->pLeaves[k++];
    pC->nLeaves = c;
    pC->uSign = pC0->uSign | pC1->uSign;
    return 1;
}

/**Function*************************************************************

  Synopsis    [Prepares the object for FPGA mapping.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
int If_CutMerge( If_Man_t * p, If_Cut_t * pCut0, If_Cut_t * pCut1, If_Cut_t * pCut )
{ 
    int nLutSize = pCut0->nLimit;
    int nSize0 = pCut0->nLeaves;
    int nSize1 = pCut1->nLeaves;
    int * pC0 = pCut0->pLeaves;
    int * pC1 = pCut1->pLeaves;
    int * pC = pCut->pLeaves;
    int i, k, c;
    // compare two cuts with different numbers
    c = nSize0; 
    for ( i = 0; i < nSize1; i++ )
    {
        for ( k = 0; k < nSize0; k++ )
            if ( pC1[i] == pC0[k] )
                break;
        if ( k < nSize0 )
        {
            p->pPerm[1][i] = k;
            continue;
        }
        if ( c == nLutSize )
            return 0;
        p->pPerm[1][i] = c;
        pC[c++] = pC1[i];
    }
    for ( i = 0; i < nSize0; i++ )
        pC[i] = pC0[i];
    pCut->nLeaves = c;
    pCut->uSign = pCut0->uSign | pCut1->uSign;
    return 1;
}

/**Function*************************************************************

  Synopsis    [Prepares the object for FPGA mapping.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
int If_CutCompareDelay( If_Man_t * p, If_Cut_t ** ppC0, If_Cut_t ** ppC1 )
{
    If_Cut_t * pC0 = *ppC0;
    If_Cut_t * pC1 = *ppC1;
    if ( pC0->Delay < pC1->Delay - p->fEpsilon )
        return -1;
    if ( pC0->Delay > pC1->Delay + p->fEpsilon )
        return 1;
    if ( pC0->nLeaves < pC1->nLeaves )
        return -1;
    if ( pC0->nLeaves > pC1->nLeaves )
        return 1;
    if ( pC0->Area < pC1->Area - p->fEpsilon )
        return -1;
    if ( pC0->Area > pC1->Area + p->fEpsilon )
        return 1;
    return 0;
}

/**Function*************************************************************

  Synopsis    [Prepares the object for FPGA mapping.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
int If_CutCompareDelayOld( If_Man_t * p, If_Cut_t ** ppC0, If_Cut_t ** ppC1 )
{
    If_Cut_t * pC0 = *ppC0;
    If_Cut_t * pC1 = *ppC1;
    if ( pC0->Delay < pC1->Delay - p->fEpsilon )
        return -1;
    if ( pC0->Delay > pC1->Delay + p->fEpsilon )
        return 1;
    if ( pC0->Area < pC1->Area - p->fEpsilon )
        return -1;
    if ( pC0->Area > pC1->Area + p->fEpsilon )
        return 1;
    if ( pC0->nLeaves < pC1->nLeaves )
        return -1;
    if ( pC0->nLeaves > pC1->nLeaves )
        return 1;
    return 0;
}

/**Function*************************************************************

  Synopsis    [Prepares the object for FPGA mapping.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
int If_CutCompareArea( If_Man_t * p, If_Cut_t ** ppC0, If_Cut_t ** ppC1 )
{
    If_Cut_t * pC0 = *ppC0;
    If_Cut_t * pC1 = *ppC1;
    if ( pC0->Area < pC1->Area - p->fEpsilon )
        return -1;
    if ( pC0->Area > pC1->Area + p->fEpsilon )
        return 1;
//    if ( pC0->AveRefs > pC1->AveRefs )
//        return -1;
//    if ( pC0->AveRefs < pC1->AveRefs )
//        return 1;
    if ( pC0->nLeaves < pC1->nLeaves )
        return -1;
    if ( pC0->nLeaves > pC1->nLeaves )
        return 1;
    if ( pC0->Delay < pC1->Delay - p->fEpsilon )
        return -1;
    if ( pC0->Delay > pC1->Delay + p->fEpsilon )
        return 1;
    return 0;
}

/**Function*************************************************************

  Synopsis    [Comparison function for two cuts.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
static inline int If_ManSortCompare( If_Man_t * p, If_Cut_t * pC0, If_Cut_t * pC1 )
{
    if ( p->pPars->fPower )
    {
        if ( p->SortMode == 1 ) // area flow       
        {
            if ( pC0->Area < pC1->Area - p->fEpsilon )
                return -1;
            if ( pC0->Area > pC1->Area + p->fEpsilon )
                return 1;
            //Abc_Print( 1,"area(%.2f, %.2f), power(%.2f, %.2f), edge(%.2f, %.2f)\n",
            //         pC0->Area, pC1->Area, pC0->Power, pC1->Power, pC0->Edge, pC1->Edge);
            if ( pC0->Power < pC1->Power - p->fEpsilon )
                return -1;
            if ( pC0->Power > pC1->Power + p->fEpsilon )
                return 1;
            if ( pC0->Edge < pC1->Edge - p->fEpsilon )
                return -1;
            if ( pC0->Edge > pC1->Edge + p->fEpsilon )
                return 1;
//            if ( pC0->AveRefs > pC1->AveRefs )
//                return -1;
//            if ( pC0->AveRefs < pC1->AveRefs )
//                return 1;
            if ( pC0->nLeaves < pC1->nLeaves )
                return -1;
            if ( pC0->nLeaves > pC1->nLeaves )
                return 1;
            if ( pC0->Delay < pC1->Delay - p->fEpsilon )
                return -1;
            if ( pC0->Delay > pC1->Delay + p->fEpsilon )
                return 1;
            return 0;
        }
        if ( p->SortMode == 0 ) // delay
        {
            if ( pC0->Delay < pC1->Delay - p->fEpsilon )
                return -1;
            if ( pC0->Delay > pC1->Delay + p->fEpsilon )
                return 1;
            if ( pC0->nLeaves < pC1->nLeaves )
                return -1;
            if ( pC0->nLeaves > pC1->nLeaves )
                return 1;
            if ( pC0->Area < pC1->Area - p->fEpsilon )
                return -1;
            if ( pC0->Area > pC1->Area + p->fEpsilon )
                return 1;
            if ( pC0->Power < pC1->Power - p->fEpsilon  )
                return -1;
            if ( pC0->Power > pC1->Power + p->fEpsilon  )
                return 1;
            if ( pC0->Edge < pC1->Edge - p->fEpsilon )
                return -1;
            if ( pC0->Edge > pC1->Edge + p->fEpsilon )
                return 1;
            return 0;
        }
        assert( p->SortMode == 2 ); // delay old, exact area
        if ( pC0->Delay < pC1->Delay - p->fEpsilon )
            return -1;
        if ( pC0->Delay > pC1->Delay + p->fEpsilon )
            return 1;
        if ( pC0->Power < pC1->Power - p->fEpsilon  )
            return -1;
        if ( pC0->Power > pC1->Power + p->fEpsilon  )
            return 1;
        if ( pC0->Edge < pC1->Edge - p->fEpsilon )
            return -1;
        if ( pC0->Edge > pC1->Edge + p->fEpsilon )
            return 1;
        if ( pC0->Area < pC1->Area - p->fEpsilon )
            return -1;
        if ( pC0->Area > pC1->Area + p->fEpsilon )
            return 1;
        if ( pC0->nLeaves < pC1->nLeaves )
            return -1;
        if ( pC0->nLeaves > pC1->nLeaves )
            return 1;
        return 0;
    } 
    else  // regular
    {
        if ( p->SortMode == 1 ) // area
        {
            if ( pC0->Area < pC1->Area - p->fEpsilon )
                return -1;
            if ( pC0->Area > pC1->Area + p->fEpsilon )
                return 1;
            if ( pC0->Edge < pC1->Edge - p->fEpsilon )
                return -1;
            if ( pC0->Edge > pC1->Edge + p->fEpsilon )
                return 1;
            if ( pC0->Power < pC1->Power - p->fEpsilon )
                return -1;
            if ( pC0->Power > pC1->Power + p->fEpsilon )
                return 1;
//            if ( pC0->AveRefs > pC1->AveRefs )
//                return -1;
//            if ( pC0->AveRefs < pC1->AveRefs )
//                return 1;
            if ( pC0->nLeaves < pC1->nLeaves )
                return -1;
            if ( pC0->nLeaves > pC1->nLeaves )
                return 1;
            if ( pC0->fUseless < pC1->fUseless )
                return -1;
            if ( pC0->fUseless > pC1->fUseless )
                return 1;
            return 0;
        }
        if ( p->SortMode == 0 ) // delay
        {
            if ( pC0->Delay < pC1->Delay - p->fEpsilon )
                return -1;
            if ( pC0->Delay > pC1->Delay + p->fEpsilon )
                return 1;
            if ( pC0->nLeaves < pC1->nLeaves )
                return -1;
            if ( pC0->nLeaves > pC1->nLeaves )
                return 1;
            if ( pC0->Area < pC1->Area - p->fEpsilon )
                return -1;
            if ( pC0->Area > pC1->Area + p->fEpsilon )
                return 1;
            if ( pC0->Edge < pC1->Edge - p->fEpsilon )
                return -1;
            if ( pC0->Edge > pC1->Edge + p->fEpsilon )
                return 1;
            if ( pC0->Power < pC1->Power - p->fEpsilon )
                return -1;
            if ( pC0->Power > pC1->Power + p->fEpsilon )
                return 1;
            if ( pC0->fUseless < pC1->fUseless )
                return -1;
            if ( pC0->fUseless > pC1->fUseless )
                return 1;
            return 0;
        }
        assert( p->SortMode == 2 ); // delay old
        if ( pC0->Delay < pC1->Delay - p->fEpsilon )
            return -1;
        if ( pC0->Delay > pC1->Delay + p->fEpsilon )
            return 1;
        if ( pC0->fUseless < pC1->fUseless )
            return -1;
        if ( pC0->fUseless > pC1->fUseless )
            return 1;
        if ( pC0->Area < pC1->Area - p->fEpsilon )
            return -1;
        if ( pC0->Area > pC1->Area + p->fEpsilon )
            return 1;
        if ( pC0->Edge < pC1->Edge - p->fEpsilon )
            return -1;
        if ( pC0->Edge > pC1->Edge + p->fEpsilon )
            return 1;
        if ( pC0->Power < pC1->Power - p->fEpsilon )
            return -1;
        if ( pC0->Power > pC1->Power + p->fEpsilon )
            return 1;
        if ( pC0->nLeaves < pC1->nLeaves )
            return -1;
        if ( pC0->nLeaves > pC1->nLeaves )
            return 1;
        return 0;
    }
}

/**Function*************************************************************

  Synopsis    [Comparison function for two cuts.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
static inline int If_ManSortCompare_old( If_Man_t * p, If_Cut_t * pC0, If_Cut_t * pC1 )
{
    if ( p->SortMode == 1 ) // area
    {
        if ( pC0->Area < pC1->Area - p->fEpsilon )
            return -1;
        if ( pC0->Area > pC1->Area + p->fEpsilon )
            return 1;
//        if ( pC0->AveRefs > pC1->AveRefs )
//            return -1;
//        if ( pC0->AveRefs < pC1->AveRefs )
//            return 1;
        if ( pC0->nLeaves < pC1->nLeaves )
            return -1;
        if ( pC0->nLeaves > pC1->nLeaves )
            return 1;
        if ( pC0->Delay < pC1->Delay - p->fEpsilon )
            return -1;
        if ( pC0->Delay > pC1->Delay + p->fEpsilon )
            return 1;
        return 0;
    }
    if ( p->SortMode == 0 ) // delay
    {
        if ( pC0->Delay < pC1->Delay - p->fEpsilon )
            return -1;
        if ( pC0->Delay > pC1->Delay + p->fEpsilon )
            return 1;
        if ( pC0->nLeaves < pC1->nLeaves )
            return -1;
        if ( pC0->nLeaves > pC1->nLeaves )
            return 1;
        if ( pC0->Area < pC1->Area - p->fEpsilon )
            return -1;
        if ( pC0->Area > pC1->Area + p->fEpsilon )
            return 1;
        return 0;
    }
    assert( p->SortMode == 2 ); // delay old
    if ( pC0->Delay < pC1->Delay - p->fEpsilon )
        return -1;
    if ( pC0->Delay > pC1->Delay + p->fEpsilon )
        return 1;
    if ( pC0->Area < pC1->Area - p->fEpsilon )
        return -1;
    if ( pC0->Area > pC1->Area + p->fEpsilon )
        return 1;
    if ( pC0->nLeaves < pC1->nLeaves )
        return -1;
    if ( pC0->nLeaves > pC1->nLeaves )
        return 1;
    return 0;
}

/**Function*************************************************************

  Synopsis    [Performs incremental sorting of cuts.]

  Description [Currently only the trivial sorting is implemented.]
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
void If_CutSort( If_Man_t * p, If_Set_t * pCutSet, If_Cut_t * pCut )
{
//    int Counter = 0;
    int i;

    // the new cut is the last one
    assert( pCutSet->ppCuts[pCutSet->nCuts] == pCut );
    assert( pCutSet->nCuts <= pCutSet->nCutsMax );

    // cut structure is empty
    if ( pCutSet->nCuts == 0 )
    {
        pCutSet->nCuts++;
        return;
    }

    if ( !pCut->fUseless && 
         (p->pPars->fUseDsd || p->pPars->pFuncCell2 || p->pPars->fUseBat || 
          p->pPars->pLutStruct || p->pPars->fUserRecLib || p->pPars->fUserSesLib || p->pPars->fUserLutDec || p->pPars->fUserLut2D ||
          p->pPars->fEnableCheck07 || p->pPars->fUseCofVars || p->pPars->fUseAndVars || p->pPars->fUse34Spec || 
          p->pPars->fUseDsdTune || p->pPars->fEnableCheck75 || p->pPars->fEnableCheck75u || p->pPars->fUseCheck1 || p->pPars->fUseCheck2) )
    {
        If_Cut_t * pFirst = pCutSet->ppCuts[0];
        if ( pFirst->fUseless || If_ManSortCompare(p, pFirst, pCut) == 1 )
        {
            pCutSet->ppCuts[0] = pCut;
            pCutSet->ppCuts[pCutSet->nCuts] = pFirst;
            If_CutSort( p, pCutSet, pFirst );
            return;
        }
    }

    // the cut will be added - find its place
    for ( i = pCutSet->nCuts-1; i >= 0; i-- )
    {
//        Counter++;
        if ( If_ManSortCompare( p, pCutSet->ppCuts[i], pCut ) <= 0 || (i == 0 && !pCutSet->ppCuts[0]->fUseless && pCut->fUseless) )
            break;
        pCutSet->ppCuts[i+1] = pCutSet->ppCuts[i];
        pCutSet->ppCuts[i] = pCut;
    }
//    Abc_Print( 1, "%d ", Counter );

    // update the number of cuts
    if ( pCutSet->nCuts < pCutSet->nCutsMax )
        pCutSet->nCuts++;
}

/**Function*************************************************************

  Synopsis    [Orders the leaves of the cut.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
void If_CutOrder( If_Cut_t * pCut )
{
    int i, Temp, fChanges;
    do {
        fChanges = 0;
        for ( i = 0; i < (int)pCut->nLeaves - 1; i++ )
        {
            assert( pCut->pLeaves[i] != pCut->pLeaves[i+1] );
            if ( pCut->pLeaves[i] <= pCut->pLeaves[i+1] )
                continue;
            Temp = pCut->pLeaves[i];
            pCut->pLeaves[i] = pCut->pLeaves[i+1];
            pCut->pLeaves[i+1] = Temp;
            fChanges = 1;
        }
    } while ( fChanges );
}

/**Function*************************************************************

  Synopsis    [Checks correctness of the cut.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
int If_CutCheck( If_Cut_t * pCut )
{
    int i;
    assert( pCut->nLeaves <= pCut->nLimit );
    if ( pCut->nLeaves < 2 )
        return 1;
    for ( i = 1; i < (int)pCut->nLeaves; i++ )
    {
        if ( pCut->pLeaves[i-1] >= pCut->pLeaves[i] )
        {
            Abc_Print( -1, "If_CutCheck(): Cut has wrong ordering of inputs.\n" );
            return 0;
        }
        assert( pCut->pLeaves[i-1] < pCut->pLeaves[i] );
    }
    return 1;
}


/**Function*************************************************************

  Synopsis    [Prints one cut.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
void If_CutPrint( If_Cut_t * pCut )
{
    unsigned i;
    Abc_Print( 1, "{" );
    for ( i = 0; i < pCut->nLeaves; i++ )
        Abc_Print( 1, " %s%d", If_CutLeafBit(pCut, i) ? "!":"", pCut->pLeaves[i] );
    Abc_Print( 1, " }\n" );
}

/**Function*************************************************************

  Synopsis    [Prints one cut.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
void If_CutPrintTiming( If_Man_t * p, If_Cut_t * pCut )
{
    If_Obj_t * pLeaf;
    unsigned i;
    Abc_Print( 1, "{" );
    If_CutForEachLeaf( p, pCut, pLeaf, i )
        Abc_Print( 1, " %d(%.2f/%.2f)", pLeaf->Id, If_ObjCutBest(pLeaf)->Delay, pLeaf->Required );
    Abc_Print( 1, " }\n" );
}

/**Function*************************************************************

  Synopsis    [Moves the cut over the latch.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
void If_CutLift( If_Cut_t * pCut )
{
    unsigned i;
    for ( i = 0; i < pCut->nLeaves; i++ )
    {
        assert( (pCut->pLeaves[i] & 255) < 255 );
        pCut->pLeaves[i]++;
    }
}


/**Function*************************************************************

  Synopsis    [Computes area flow.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
float If_CutAreaFlow( If_Man_t * p, If_Cut_t * pCut )
{
    If_Obj_t * pLeaf;
    float Flow, AddOn;
    int i;
    Flow = If_CutLutArea(p, pCut);
    If_CutForEachLeaf( p, pCut, pLeaf, i )
    {
        if ( pLeaf->nRefs == 0 || If_ObjIsConst1(pLeaf) )
            AddOn = If_ObjCutBest(pLeaf)->Area;
        else 
        {
            assert( pLeaf->EstRefs > p->fEpsilon );
            AddOn = If_ObjCutBest(pLeaf)->Area / pLeaf->EstRefs;
        }
        if ( Flow >= (float)1e32 || AddOn >= (float)1e32 )
            Flow = (float)1e32;
        else 
        {
            Flow += AddOn;
            if ( Flow > (float)1e32 )
                 Flow = (float)1e32;
        }
    }
    return Flow;
}

/**Function*************************************************************

  Synopsis    [Computes area flow.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
float If_CutEdgeFlow( If_Man_t * p, If_Cut_t * pCut )
{
    If_Obj_t * pLeaf;
    float Flow, AddOn;
    int i;
    Flow = pCut->nLeaves;
    If_CutForEachLeaf( p, pCut, pLeaf, i )
    {
        if ( pLeaf->nRefs == 0 || If_ObjIsConst1(pLeaf) )
            AddOn = If_ObjCutBest(pLeaf)->Edge;
        else 
        {
            assert( pLeaf->EstRefs > p->fEpsilon );
            AddOn = If_ObjCutBest(pLeaf)->Edge / pLeaf->EstRefs;
        }
        if ( Flow >= (float)1e32 || AddOn >= (float)1e32 )
            Flow = (float)1e32;
        else 
        {
            Flow += AddOn;
            if ( Flow > (float)1e32 )
                 Flow = (float)1e32;
        }
    }
    return Flow;
}

/**Function*************************************************************

  Synopsis    [Computes area flow.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
float If_CutPowerFlow( If_Man_t * p, If_Cut_t * pCut, If_Obj_t * pRoot )
{
    If_Obj_t * pLeaf;
    float * pSwitching = (float *)p->vSwitching->pArray;
    float Power = 0;
    int i;
    If_CutForEachLeaf( p, pCut, pLeaf, i )
    {
        Power += pSwitching[pLeaf->Id];
        if ( pLeaf->nRefs == 0 || If_ObjIsConst1(pLeaf) )
            Power += If_ObjCutBest(pLeaf)->Power;
        else 
        {
            assert( pLeaf->EstRefs > p->fEpsilon );
            Power += If_ObjCutBest(pLeaf)->Power / pLeaf->EstRefs;
        }
    }
    return Power;
}

/**Function*************************************************************

  Synopsis    [Average number of references of the leaves.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
float If_CutAverageRefs( If_Man_t * p, If_Cut_t * pCut )
{
    If_Obj_t * pLeaf;
    int nRefsTotal, i;
    nRefsTotal = 0;
    If_CutForEachLeaf( p, pCut, pLeaf, i )
        nRefsTotal += pLeaf->nRefs;
    return ((float)nRefsTotal)/pCut->nLeaves;
}


/**Function*************************************************************

  Synopsis    [Computes area of the first level.]

  Description [The cut need to be derefed.]
               
  SideEffects []

  SeeAlso     []
 
***********************************************************************/
float If_CutAreaDeref( If_Man_t * p, If_Cut_t * pCut )
{
    If_Obj_t * pLeaf;
    float Area;
    int i;
    int fTopLevel = (s_IfRD_Depth == 0);
    abctime clkStart = 0;

    // profiling: top-level call setup
    if ( fTopLevel )
    {
        s_IfRD_CurSize = 0;
        clkStart = Abc_Clock();
    }
    s_IfRD_Depth++;
    s_IfRD_CurSize++;

    Area = If_CutLutArea(p, pCut);
    If_CutForEachLeaf( p, pCut, pLeaf, i )
    {
        assert( pLeaf->nRefs > 0 );
        if ( --pLeaf->nRefs > 0 || !If_ObjIsAnd(pLeaf) )
            continue;
        Area += If_CutAreaDeref( p, If_ObjCutBest(pLeaf) );
    }
    // profiling: top-level call teardown
    s_IfRD_Depth--;
    if ( fTopLevel )
    {
        s_IfRD_TotalTime += Abc_Clock() - clkStart;
        s_IfRD_CallCount++;
        s_IfRD_TotalSize += s_IfRD_CurSize;
        if ( s_IfRD_CurSize > s_IfRD_MaxSize )
            s_IfRD_MaxSize = s_IfRD_CurSize;
    }
    return Area;
}

/**Function*************************************************************

  Synopsis    [Computes area of the first level.]

  Description [The cut need to be derefed.]
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
float If_CutAreaRef( If_Man_t * p, If_Cut_t * pCut )
{
    If_Obj_t * pLeaf;
    float Area;
    int i;
    int fTopLevel = (s_IfRD_Depth == 0);
    abctime clkStart = 0;

    // profiling: top-level call setup
    if ( fTopLevel )
    {
        s_IfRD_CurSize = 0;
        clkStart = Abc_Clock();
    }
    s_IfRD_Depth++;
    s_IfRD_CurSize++;

    Area = If_CutLutArea(p, pCut);
    If_CutForEachLeaf( p, pCut, pLeaf, i )
    {
        assert( pLeaf->nRefs >= 0 );
        if ( pLeaf->nRefs++ > 0 || !If_ObjIsAnd(pLeaf) )
            continue;
        Area += If_CutAreaRef( p, If_ObjCutBest(pLeaf) );
    }
    // profiling: top-level call teardown
    s_IfRD_Depth--;
    if ( fTopLevel )
    {
        s_IfRD_TotalTime += Abc_Clock() - clkStart;
        s_IfRD_CallCount++;
        s_IfRD_TotalSize += s_IfRD_CurSize;
        if ( s_IfRD_CurSize > s_IfRD_MaxSize )
            s_IfRD_MaxSize = s_IfRD_CurSize;
    }
    return Area;
}

/**Function*************************************************************

  Synopsis    [Computes area of the first level.]

  Description [The cut need to be derefed.]
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
float If_CutAreaDerefed( If_Man_t * p, If_Cut_t * pCut )
{
    float aResult, aResult2;
    if ( pCut->nLeaves < 2 )
        return 0;
    aResult2 = If_CutAreaRef( p, pCut );
    aResult  = If_CutAreaDeref( p, pCut );
    assert( aResult > aResult2 - 3*p->fEpsilon );
    assert( aResult < aResult2 + 3*p->fEpsilon );
    return aResult;
}

/**Function*************************************************************

  Synopsis    [Computes area of the first level.]

  Description [The cut need to be derefed.]
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
float If_CutAreaRefed( If_Man_t * p, If_Cut_t * pCut )
{
    float aResult, aResult2;
    if ( pCut->nLeaves < 2 )
        return 0;
    aResult2 = If_CutAreaDeref( p, pCut );
    aResult  = If_CutAreaRef( p, pCut );
//    assert( aResult > aResult2 - p->fEpsilon );
//    assert( aResult < aResult2 + p->fEpsilon );
    return aResult;
}


/**Function*************************************************************

  Synopsis    [Computes area of the first level.]

  Description [The cut need to be derefed.]
               
  SideEffects []

  SeeAlso     []
 
***********************************************************************/
float If_CutEdgeDeref( If_Man_t * p, If_Cut_t * pCut )
{
    If_Obj_t * pLeaf;
    float Edge;
    int i;
    Edge = pCut->nLeaves;
    If_CutForEachLeaf( p, pCut, pLeaf, i )
    {
        assert( pLeaf->nRefs > 0 );
        if ( --pLeaf->nRefs > 0 || !If_ObjIsAnd(pLeaf) )
            continue;
        Edge += If_CutEdgeDeref( p, If_ObjCutBest(pLeaf) );
    }
    return Edge;
}

/**Function*************************************************************

  Synopsis    [Computes area of the first level.]

  Description [The cut need to be derefed.]
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
float If_CutEdgeRef( If_Man_t * p, If_Cut_t * pCut )
{
    If_Obj_t * pLeaf;
    float Edge;
    int i;
    Edge = pCut->nLeaves;
    If_CutForEachLeaf( p, pCut, pLeaf, i )
    {
        assert( pLeaf->nRefs >= 0 );
        if ( pLeaf->nRefs++ > 0 || !If_ObjIsAnd(pLeaf) )
            continue;
        Edge += If_CutEdgeRef( p, If_ObjCutBest(pLeaf) );
    }
    return Edge;
}

/**Function*************************************************************

  Synopsis    [Computes edge of the first level.]

  Description [The cut need to be derefed.]
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
float If_CutEdgeDerefed( If_Man_t * p, If_Cut_t * pCut )
{
    float aResult, aResult2;
    if ( pCut->nLeaves < 2 )
        return pCut->nLeaves;
    aResult2 = If_CutEdgeRef( p, pCut );
    aResult  = If_CutEdgeDeref( p, pCut );
//    assert( aResult > aResult2 - 3*p->fEpsilon );
//    assert( aResult < aResult2 + 3*p->fEpsilon );
    return aResult;
}

/**Function*************************************************************

  Synopsis    [Computes area of the first level.]

  Description [The cut need to be derefed.]
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
float If_CutEdgeRefed( If_Man_t * p, If_Cut_t * pCut )
{
    float aResult, aResult2;
    if ( pCut->nLeaves < 2 )
        return pCut->nLeaves;
    aResult2 = If_CutEdgeDeref( p, pCut );
    aResult  = If_CutEdgeRef( p, pCut );
//    assert( aResult > aResult2 - p->fEpsilon );
//    assert( aResult < aResult2 + p->fEpsilon );
    return aResult;
}


/**Function*************************************************************

  Synopsis    [Computes area of the first level.]

  Description [The cut need to be derefed.]
               
  SideEffects []

  SeeAlso     []
 
***********************************************************************/
float If_CutPowerDeref( If_Man_t * p, If_Cut_t * pCut, If_Obj_t * pRoot )
{
    If_Obj_t * pLeaf;
    float * pSwitching = (float *)p->vSwitching->pArray;
    float Power = 0;
    int i;
    If_CutForEachLeaf( p, pCut, pLeaf, i )
    {
        Power += pSwitching[pLeaf->Id];
        assert( pLeaf->nRefs > 0 );
        if ( --pLeaf->nRefs > 0 || !If_ObjIsAnd(pLeaf) )
            continue;
        Power += If_CutPowerDeref( p, If_ObjCutBest(pLeaf), pRoot );
    }
    return Power;
}

/**Function*************************************************************

  Synopsis    [Computes area of the first level.]

  Description [The cut need to be derefed.]
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
float If_CutPowerRef( If_Man_t * p, If_Cut_t * pCut, If_Obj_t * pRoot )
{
    If_Obj_t * pLeaf;
    float * pSwitching = (float *)p->vSwitching->pArray;
    float Power = 0;
    int i;
    If_CutForEachLeaf( p, pCut, pLeaf, i )
    {
        Power += pSwitching[pLeaf->Id];
        assert( pLeaf->nRefs >= 0 );
        if ( pLeaf->nRefs++ > 0 || !If_ObjIsAnd(pLeaf) )
            continue;
        Power += If_CutPowerRef( p, If_ObjCutBest(pLeaf), pRoot );
    }
    return Power;
}

/**Function*************************************************************

  Synopsis    [Computes Power of the first level.]

  Description [The cut need to be derefed.]
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
float If_CutPowerDerefed( If_Man_t * p, If_Cut_t * pCut, If_Obj_t * pRoot )
{
    float aResult, aResult2;
    if ( pCut->nLeaves < 2 )
        return 0;
    aResult2 = If_CutPowerRef( p, pCut, pRoot );
    aResult  = If_CutPowerDeref( p, pCut, pRoot );
    assert( aResult > aResult2 - p->fEpsilon );
    assert( aResult < aResult2 + p->fEpsilon );
    return aResult;
}

/**Function*************************************************************

  Synopsis    [Computes area of the first level.]

  Description [The cut need to be derefed.]
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
float If_CutPowerRefed( If_Man_t * p, If_Cut_t * pCut, If_Obj_t * pRoot )
{
    float aResult, aResult2;
    if ( pCut->nLeaves < 2 )
        return 0;
    aResult2 = If_CutPowerDeref( p, pCut, pRoot );
    aResult  = If_CutPowerRef( p, pCut, pRoot );
    assert( aResult > aResult2 - p->fEpsilon );
    assert( aResult < aResult2 + p->fEpsilon );
    return aResult;
}

/**Function*************************************************************

  Synopsis    [Computes the cone of the cut in AIG with choices.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
int If_CutGetCutMinLevel( If_Man_t * p, If_Cut_t * pCut )
{
    If_Obj_t * pLeaf;
    int i, nMinLevel = IF_INFINITY;
    If_CutForEachLeaf( p, pCut, pLeaf, i )
        nMinLevel = IF_MIN( nMinLevel, (int)pLeaf->Level );
    return nMinLevel;
}

/**Function*************************************************************

  Synopsis    [Computes the cone of the cut in AIG with choices.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
int If_CutGetCone_rec( If_Man_t * p, If_Obj_t * pObj, If_Cut_t * pCut )
{
    If_Obj_t * pTemp;
    int i, RetValue;
    // check if the node is in the cut
    for ( i = 0; i < (int)pCut->nLeaves; i++ )
        if ( pCut->pLeaves[i] == pObj->Id )
            return 1;
        else if ( pCut->pLeaves[i] > pObj->Id )
            break;
    // return if we reached the boundary
    if ( If_ObjIsCi(pObj) )
        return 0;
    // check the choice node
    for ( pTemp = pObj; pTemp; pTemp = pTemp->pEquiv )
    {
        // check if the node itself is bound
        RetValue = If_CutGetCone_rec( p, If_ObjFanin0(pTemp), pCut );
        if ( RetValue )
            RetValue &= If_CutGetCone_rec( p, If_ObjFanin1(pTemp), pCut );
        if ( RetValue )
            return 1;
    }
    return 0;
}

/**Function*************************************************************

  Synopsis    [Computes the cone of the cut in AIG with choices.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
int If_CutGetCones( If_Man_t * p )
{
    If_Obj_t * pObj;
    int i, Counter = 0;
    abctime clk = Abc_Clock();
    If_ManForEachObj( p, pObj, i )
    {
        if ( If_ObjIsAnd(pObj) && pObj->nRefs )
        {
            Counter += !If_CutGetCone_rec( p, pObj, If_ObjCutBest(pObj) );
//            Abc_Print( 1, "%d ", If_CutGetCutMinLevel( p, If_ObjCutBest(pObj) ) );
        }
    }
    Abc_Print( 1, "Cound not find boundary for %d nodes.\n", Counter );
    Abc_PrintTime( 1, "Cones", Abc_Clock() - clk );
    return 1;
}


/**Function*************************************************************

  Synopsis    [Computes the cone of the cut in AIG with choices.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
void If_CutFoundFanins_rec( If_Obj_t * pObj, Vec_Int_t * vLeaves )
{
    if ( pObj->nRefs || If_ObjIsCi(pObj) )
    {
        Vec_IntPushUnique( vLeaves, pObj->Id );
        return;
    }
    If_CutFoundFanins_rec( If_ObjFanin0(pObj), vLeaves );
    If_CutFoundFanins_rec( If_ObjFanin1(pObj), vLeaves );
}

/**Function*************************************************************

  Synopsis    [Computes the cone of the cut in AIG with choices.]

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
int If_CutCountTotalFanins( If_Man_t * p )
{
    If_Obj_t * pObj;
    Vec_Int_t * vLeaves;
    int i, nFaninsTotal = 0, Counter = 0;
    abctime clk = Abc_Clock();
    vLeaves = Vec_IntAlloc( 100 );
    If_ManForEachObj( p, pObj, i )
    {
        if ( If_ObjIsAnd(pObj) && pObj->nRefs )
        {
            nFaninsTotal += If_ObjCutBest(pObj)->nLeaves;
            Vec_IntClear( vLeaves );
            If_CutFoundFanins_rec( If_ObjFanin0(pObj), vLeaves );
            If_CutFoundFanins_rec( If_ObjFanin1(pObj), vLeaves );
            Counter += Vec_IntSize(vLeaves);
        }
    }
    Abc_Print( 1, "Total cut inputs = %d. Total fanins incremental = %d.\n", nFaninsTotal, Counter );
    Abc_PrintTime( 1, "Fanins", Abc_Clock() - clk );
    Vec_IntFree( vLeaves );
    return 1;
}

/**Function*************************************************************

  Synopsis    []

  Description []
               
  SideEffects []

  SeeAlso     []

***********************************************************************/
int If_CutFilter2_rec( If_Man_t * p, If_Obj_t * pObj, int LevelMin )
{
    char * pVisited = Vec_StrEntryP(p->vMarks, pObj->Id);
    if ( *pVisited )
        return *pVisited;
    Vec_IntPush( p->vVisited2, pObj->Id );
    if ( (int)pObj->Level <= LevelMin )
        return (*pVisited = 1);
    if ( If_CutFilter2_rec( p, pObj->pFanin0, LevelMin ) == 1 )
        return (*pVisited = 1);
    if ( If_CutFilter2_rec( p, pObj->pFanin1, LevelMin ) == 1 )
        return (*pVisited = 1);
    return (*pVisited = 2);
}
int If_CutFilter2( If_Man_t * p, If_Obj_t * pNode, If_Cut_t * pCut )
{
    If_Obj_t * pLeaf, * pTemp;  int i, Count = 0; 
//    printf( "Considering node %d and cut {", pNode->Id );
//    If_CutForEachLeaf( p, pCut, pLeaf, i )
//        printf( " %d", pLeaf->Id );
//    printf( " }\n" );
    If_CutForEachLeaf( p, pCut, pLeaf, i )
    {
        int k, iObj, RetValue, nLevelMin = ABC_INFINITY;
        Vec_IntClear( p->vVisited2 );
        If_CutForEachLeaf( p, pCut, pTemp, k )
        {
            if ( pTemp == pLeaf )
                continue;
            nLevelMin = Abc_MinInt( nLevelMin, (int)pTemp->Level );
            assert( Vec_StrEntry(p->vMarks, pTemp->Id) == 0 );
            Vec_StrWriteEntry( p->vMarks, pTemp->Id, 2 );
            Vec_IntPush( p->vVisited2, pTemp->Id );
        }
        RetValue = If_CutFilter2_rec( p, pLeaf, nLevelMin );
        Vec_IntForEachEntry( p->vVisited2, iObj, k )
            Vec_StrWriteEntry( p->vMarks, iObj, 0 );
        if ( RetValue == 2 )
        {
            Count++;
            pCut->nLeaves--;
            for ( k = i; k < (int)pCut->nLeaves; k++ )
                pCut->pLeaves[k] = pCut->pLeaves[k+1];
            i--;
        }
    }
    //if ( Count )
    //    printf( "%d", Count );
    return 0;
}

/**function*************************************************************

  synopsis    [Prints and resets If_CutAreaDeref/Ref profiling statistics.]

  description []

  sideeffects []

  seealso     []

***********************************************************************/
void If_CutAreaRefDerefProfilePrint( char * pStageName )
{
    if ( s_IfRD_CallCount > 0 )
    {
        printf( "FPGA If_CutAreaDeref/Ref profiling [%s]:\n", pStageName );
        printf( "  Total time         : %10.2f sec\n",
            (double)s_IfRD_TotalTime / CLOCKS_PER_SEC );
        printf( "  Top-level calls    : %lld\n", (long long)s_IfRD_CallCount );
        printf( "  Max MFFC size      : %d  (nodes visited in single DFS)\n",
            s_IfRD_MaxSize );
        printf( "  Average MFFC size  : %.2f\n",
            (double)s_IfRD_TotalSize / s_IfRD_CallCount );
        printf( "  Total nodes visited: %lld\n", (long long)s_IfRD_TotalSize );
    }
}
void If_CutAreaRefDerefProfileReset()
{
    s_IfRD_Depth     = 0;
    s_IfRD_CurSize   = 0;
    s_IfRD_MaxSize   = 0;
    s_IfRD_TotalSize = 0;
    s_IfRD_CallCount = 0;
    s_IfRD_TotalTime = 0;
}

/**Function*************************************************************

  Synopsis    [Deref with MFFC size recording for exact area pruning.]

  Description [Works like If_CutAreaDeref but also records mffcSize for
  each AND node whose nRefs drops to 0. mffcSize[l] = |S(l)|, the size
  of l's activation set (including l itself). Also tracks total MFFC
  size and detects tree structure (no reconvergence).

  The p->pMffcSizes array is lazily allocated on first use.]

  SideEffects [Modifies p->pMffcSizes, p->nMffcTotal, p->fMffcIsTree]

  SeeAlso     [If_CutAreaDeref]

***********************************************************************/
static float If_CutAreaDerefAndRecord_rec( If_Man_t * p, If_Cut_t * pCut, int * pMffcNodeCount )
{
    If_Obj_t * pLeaf;
    float Area;
    int i;
    Area = If_CutLutArea(p, pCut);
    (*pMffcNodeCount)++;
    If_CutForEachLeaf( p, pCut, pLeaf, i )
    {
        assert( pLeaf->nRefs > 0 );
        if ( --pLeaf->nRefs > 0 || !If_ObjIsAnd(pLeaf) )
            continue;
        // pLeaf->nRefs dropped to 0: it's an MFFC node.
        // A node's nRefs can only drop to 0 once during a single deref
        // (reconvergent nodes have nRefs >= 2 and get decremented gradually),
        // so the stamp check below is a safety assertion, not a branch.
        assert( p->pMffcStamps[pLeaf->Id] != p->nMffcStamp );
        float subArea = If_CutAreaDerefAndRecord_rec( p, If_ObjCutBest(pLeaf), pMffcNodeCount );
        p->pMffcSizes[pLeaf->Id] = subArea;  // |S(l)| = area of l's subtree
        p->pMffcStamps[pLeaf->Id] = p->nMffcStamp;
        Area += subArea;
    }
    return Area;
}

float If_CutAreaDerefAndRecord( If_Man_t * p, If_Cut_t * pCut )
{
    If_Obj_t * pLeaf;
    float Area;
    int i, nMffcNodeCount = 0;
    int fTopLevel = (s_IfRD_Depth == 0);
    abctime clkStart = 0;

    // profiling: top-level call setup
    if ( fTopLevel )
    {
        s_IfRD_CurSize = 0;
        clkStart = Abc_Clock();
    }

    // lazy allocation of mffcSizes and stamps arrays
    if ( p->pMffcSizes == NULL )
    {
        int nObjs = If_ManObjNum(p);
        p->pMffcSizes  = ABC_CALLOC( float, nObjs );
        p->pMffcStamps = ABC_CALLOC( int, nObjs );
        p->nMffcStamp  = 0;
    }

    // initialize: new generation stamp invalidates all previous entries
    p->nMffcStamp++;
    p->nMffcTotal = 0;
    p->fMffcIsTree = 1;  // assume tree until reconvergence detected

    // perform deref with recording
    Area = If_CutLutArea(p, pCut);
    nMffcNodeCount = 1;  // count the root
    If_CutForEachLeaf( p, pCut, pLeaf, i )
    {
        assert( pLeaf->nRefs > 0 );
        if ( --pLeaf->nRefs > 0 || !If_ObjIsAnd(pLeaf) )
            continue;
        // pLeaf is an MFFC node (nRefs dropped to 0)
        float subArea = If_CutAreaDerefAndRecord_rec( p, If_ObjCutBest(pLeaf), &nMffcNodeCount );
        p->pMffcSizes[pLeaf->Id] = subArea;  // |S(l)|
        p->pMffcStamps[pLeaf->Id] = p->nMffcStamp;
        Area += subArea;
    }
    p->nMffcTotal = nMffcNodeCount - 1;  // exclude root (root is n itself, always nRefs>0)

    // profiling: top-level call teardown
    if ( fTopLevel )
    {
        s_IfRD_TotalTime += Abc_Clock() - clkStart;
        s_IfRD_CallCount++;
        s_IfRD_TotalSize += nMffcNodeCount;
        if ( nMffcNodeCount > s_IfRD_MaxSize )
            s_IfRD_MaxSize = nMffcNodeCount;
    }
    return Area;
}

/**Function*************************************************************

  Synopsis    [Exact area evaluation with submodular bound pruning.]

  Description [Tries O(K) bounds (LB1, LB2, UB1) and fast paths before
  falling back to full ref/deref. Returns the exact area of the cut.
  If the cut can be proven suboptimal (LB >= bestArea), returns -1
  to signal that the cut was pruned.

  Fast Path 1: |M| = 0 → ExactArea = LutArea
  Fast Path 2: |M| ≤ 1 → ExactArea computable in O(K)
  Fast Path 3: tree MFFC → UB1 = ExactArea (no overlap)]

  SideEffects []

  SeeAlso     [If_CutAreaDerefed]

***********************************************************************/
// Set to 1 to enable verification of pruning results against exact computation
#define IF_PRUNE_VERIFY  1

float If_CutAreaDerefedWithPruning( If_Man_t * p, If_Cut_t * pCut, float bestArea )
{
    If_Obj_t * pLeaf;
    float lutArea, lb1, ub1, maxMffc;
    int i, nMffcLeaves, fAllValidated;

    p->nExactPrune_Total++;

    if ( pCut->nLeaves < 2 )
    {
        p->nExactPrune_FastPath++;
        return 0;
    }

    // Bail out if mffcSizes not available (shouldn't happen in Mode=2)
    if ( p->pMffcSizes == NULL )
    {
        p->nExactPrune_Exact++;
        return If_CutAreaDerefed( p, pCut );
    }

    lutArea = If_CutLutArea(p, pCut);

    // ---- Compute LB1, UB1, LB2 in O(K) ----
    // For each leaf, check if it's an MFFC node with a valid stamp.
    // If any MFFC leaf has a stale stamp (not from current deref),
    // we can't trust bounds and must fall back to exact computation.
    lb1 = lutArea;          // LB1 = LutArea + |L(C) ∩ M|
    ub1 = lutArea;          // UB1 = LutArea + Σ mffcSize[l]
    maxMffc = 0;            // for LB2 = LutArea + max mffcSize[l]
    nMffcLeaves = 0;
    fAllValidated = 1;      // all MFFC leaves have valid stamps?

    If_CutForEachLeaf( p, pCut, pLeaf, i )
    {
        if ( pLeaf->nRefs == 0 && If_ObjIsAnd(pLeaf) )
        {
            nMffcLeaves++;
            lb1 += 1.0;
            // Check stamp: is this MFFC node from our current deref?
            if ( p->pMffcStamps[pLeaf->Id] == p->nMffcStamp )
            {
                float ms = p->pMffcSizes[pLeaf->Id];
                ub1 += ms;
                if ( ms > maxMffc )
                    maxMffc = ms;
            }
            else
            {
                // Stale entry: this nRefs=0 node was NOT part of current MFFC.
                // We don't know its activation set size → can't compute UB1.
                fAllValidated = 0;
            }
        }
    }

    // ---- Fast Path: nMffcLeaves == 0 ----
    // All leaves are external (nRefs > 0 or CIs) → ExactArea = LutArea
    if ( nMffcLeaves == 0 )
    {
#if IF_PRUNE_VERIFY
        { float exact = If_CutAreaDerefed(p, pCut);
          assert( exact < lutArea + 3*p->fEpsilon && exact > lutArea - 3*p->fEpsilon ); }
#endif
        p->nExactPrune_FastPath++;
        return lutArea;
    }

    // ---- LB1 pruning (always valid, even without stamps) ----
    // LB1 only uses the count of MFFC leaves, not their sizes
    if ( lb1 > bestArea - p->fEpsilon )
    {
#if IF_PRUNE_VERIFY
        { float exact = If_CutAreaDerefed(p, pCut);
          assert( exact > bestArea - 3*p->fEpsilon ); }
#endif
        p->nExactPrune_LBPruned++;
        return -1;
    }

    // If any MFFC leaf has stale stamp, fall back to exact
    if ( !fAllValidated )
    {
        p->nExactPrune_Exact++;
        return If_CutAreaDerefed( p, pCut );
    }

    // ---- From here: all MFFC leaves have valid stamps ----

    // ---- Fast Path: nMffcLeaves == 1 ----
    // Only one MFFC leaf → ExactArea = LutArea + mffcSize[that leaf]
    // (no overlap possible with a single set)
    if ( nMffcLeaves == 1 )
    {
        float result = lutArea + maxMffc;
#if IF_PRUNE_VERIFY
        { float exact = If_CutAreaDerefed(p, pCut);
          assert( exact < result + 3*p->fEpsilon && exact > result - 3*p->fEpsilon ); }
#endif
        p->nExactPrune_FastPath++;
        return result;
    }

    // LB2 = LutArea + max singleton
    {
        float lb2 = lutArea + maxMffc;
        float lb = (lb1 > lb2) ? lb1 : lb2;  // max(LB1, LB2)

        // ---- LB pruning: if lower bound >= bestArea, prune ----
        if ( lb > bestArea - p->fEpsilon )
        {
#if IF_PRUNE_VERIFY
            { float exact = If_CutAreaDerefed(p, pCut);
              assert( exact > bestArea - 3*p->fEpsilon ); }
#endif
            p->nExactPrune_LBPruned++;
            return -1;
        }
    }

    // ---- Tight bounds: if UB1 ≈ LB, we know the exact value ----
    {
        float lb2 = lutArea + maxMffc;
        float lb = (lb1 > lb2) ? lb1 : lb2;
        if ( ub1 < lb + p->fEpsilon )
        {
#if IF_PRUNE_VERIFY
            { float exact = If_CutAreaDerefed(p, pCut);
              assert( exact < ub1 + 3*p->fEpsilon && exact > ub1 - 3*p->fEpsilon ); }
#endif
            p->nExactPrune_FastPath++;
            return ub1;
        }
    }

    // ---- Fallback: full ref/deref ----
    p->nExactPrune_Exact++;
    return If_CutAreaDerefed( p, pCut );
}

/**Function*************************************************************

  Synopsis    [Print and reset exact area pruning statistics.]

  Description []

  SideEffects []

  SeeAlso     []

***********************************************************************/
void If_CutAreaPruningStatsPrint( If_Man_t * p )
{
    if ( p->nExactPrune_Total > 0 )
    {
        printf( "Exact area pruning statistics:\n" );
        printf( "  Total candidates   : %lld\n", (long long)p->nExactPrune_Total );
        printf( "  Fast path (no DFS) : %lld  (%5.1f%%)\n",
            (long long)p->nExactPrune_FastPath,
            100.0 * p->nExactPrune_FastPath / p->nExactPrune_Total );
        printf( "  LB pruned          : %lld  (%5.1f%%)\n",
            (long long)p->nExactPrune_LBPruned,
            100.0 * p->nExactPrune_LBPruned / p->nExactPrune_Total );
        printf( "  Full ref/deref     : %lld  (%5.1f%%)\n",
            (long long)p->nExactPrune_Exact,
            100.0 * p->nExactPrune_Exact / p->nExactPrune_Total );
        printf( "  Saved DFS calls    : %5.1f%%\n",
            100.0 * (1.0 - (double)p->nExactPrune_Exact / p->nExactPrune_Total) );
    }
}
void If_CutAreaPruningStatsReset( If_Man_t * p )
{
    p->nExactPrune_Total    = 0;
    p->nExactPrune_FastPath = 0;
    p->nExactPrune_LBPruned = 0;
    p->nExactPrune_UBAccept = 0;
    p->nExactPrune_Exact    = 0;
}

////////////////////////////////////////////////////////////////////////
///                       END OF FILE                                ///
////////////////////////////////////////////////////////////////////////


ABC_NAMESPACE_IMPL_END


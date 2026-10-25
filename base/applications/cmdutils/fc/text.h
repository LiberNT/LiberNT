/*
 * PROJECT:     ReactOS FC Command
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Comparing text files
 * COPYRIGHT:   Copyright 2021 Katayama Hirofumi MZ <katayama.hirofumi.mz@gmail.com>
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "fc.h"

#define IS_SPACE(ch) ((ch) == TEXT(' ') || (ch) == TEXT('\t'))

#ifdef UNICODE
    #define NODE NODE_W
    #define PrintLine PrintLineW
    #define TextCompare TextCompareW
#else
    #define NODE NODE_A
    #define PrintLine PrintLineA
    #define TextCompare TextCompareA
#endif

static LPTSTR AllocLine(LPCTSTR pch, DWORD cch)
{
    LPTSTR pszNew = malloc((cch + 1) * sizeof(TCHAR));
    if (!pszNew)
        return NULL;
    memcpy(pszNew, pch, cch * sizeof(TCHAR));
    pszNew[cch] = 0;
    return pszNew;
}

static NODE *AllocNode(LPTSTR psz, DWORD lineno)
{
    NODE *node;
    if (!psz)
        return NULL;
    node = calloc(1, sizeof(NODE));
    if (!node)
    {
        free(psz);
        return NULL;
    }
    node->pszLine = psz;
    node->lineno = lineno;
    return node;
}

static __inline VOID DeleteNode(NODE *node)
{
    if (node)
    {
        free(node->pszLine);
        free(node->pszComp);
        free(node);
    }
}

static VOID DeleteList(struct list *list)
{
    struct list *ptr;
    NODE *node;
    while ((ptr = list_head(list)) != NULL)
    {
        list_remove(ptr);
        node = LIST_ENTRY(ptr, NODE, entry);
        DeleteNode(node);
    }
}

static __inline LPCTSTR SkipSpace(LPCTSTR pch)
{
    while (IS_SPACE(*pch))
        ++pch;
    return pch;
}

static __inline LPCTSTR FindLastNonSpace(LPCTSTR pch)
{
    LPCTSTR pchLast = NULL;
    while (*pch)
    {
        if (!IS_SPACE(*pch))
            pchLast = pch;
        ++pch;
    }
    return pchLast;
}

static VOID DeleteDuplicateSpaces(LPTSTR psz)
{
    LPTSTR pch0, pch1;
    for (pch0 = pch1 = psz; *pch0; ++pch0)
    {
        *pch1++ = *pch0;
        if (IS_SPACE(*pch0))
        {
            do
            {
                ++pch0;
            } while (IS_SPACE(*pch0));
            --pch0;
        }
    }
    *pch1 = 0;
}

static LPTSTR CompressSpace(LPCTSTR line)
{
    LPTSTR pszNew;
    LPCTSTR pchLast;

    line = SkipSpace(line);
    pchLast = FindLastNonSpace(line);
    if (pchLast == NULL)
        return AllocLine(NULL, 0);

    pszNew = AllocLine(line, (DWORD)(pchLast - line) + 1);
    if (!pszNew)
        return NULL;

    DeleteDuplicateSpaces(pszNew);
    return pszNew;
}

#define TAB_WIDTH 8

static INT ExpandTabLength(LPCTSTR line)
{
    LPCTSTR pch;
    INT cch = 0;
    for (pch = line; *pch; ++pch)
    {
        if (*pch == TEXT('\t'))
            cch += TAB_WIDTH - (cch % TAB_WIDTH);
        else
            ++cch;
    }
    return cch;
}

static LPTSTR ExpandTab(LPCTSTR line)
{
    INT spaces, cch = ExpandTabLength(line), ich;
    LPTSTR pszNew = malloc((cch + 1) * sizeof(TCHAR));
    LPCTSTR pch;
    if (!pszNew)
        return NULL;
    ich = 0;
    for (pch = line; *pch; ++pch)
    {
        if (*pch == TEXT('\t'))
        {
            spaces = TAB_WIDTH - (ich % TAB_WIDTH);
            while (spaces-- > 0)
            {
                pszNew[ich++] = TEXT(' ');
            }
        }
        else
        {
            pszNew[ich++] = *pch;
        }
    }
    pszNew[ich] = 0;
    return pszNew;
}

#define HASH_EOF 0xFFFFFFFF
#define HASH_MASK 0x7FFFFFFF

static DWORD GetHash(LPCTSTR psz, BOOL bIgnoreCase)
{
    DWORD ret = 0xDEADFACE;
    while (*psz)
    {
        ret += (bIgnoreCase ? towupper(*psz) : *psz);
        ret <<= 2;
        ++psz;
    }
    return (ret & HASH_MASK);
}

static NODE *AllocEOFNode(DWORD lineno)
{
    NODE *node = AllocNode(AllocLine(NULL, 0), 0);
    if (node == NULL)
        return NULL;
    node->pszComp = AllocLine(NULL, 0);
    if (node->pszComp == NULL)
    {
        DeleteNode(node);
        return NULL;
    }
    node->lineno = lineno;
    node->hash = HASH_EOF;
    return node;
}

static __inline BOOL IsEOFNode(NODE *node)
{
    return !node || node->hash == HASH_EOF;
}

static BOOL ConvertNode(const FILECOMPARE *pFC, NODE *node)
{
    if (!(pFC->dwFlags & FLAG_T))
    {
        LPTSTR tmp = ExpandTab(node->pszLine);
        if (!tmp)
            return FALSE;
        free(node->pszLine);
        node->pszLine = tmp;
        if (!(pFC->dwFlags & FLAG_W))
            node->hash = GetHash(node->pszLine, !!(pFC->dwFlags & FLAG_C));
    }
    if (pFC->dwFlags & FLAG_W)
    {
        node->pszComp = CompressSpace(node->pszLine);
        if (!node->pszComp)
            return FALSE;
        node->hash = GetHash(node->pszComp, !!(pFC->dwFlags & FLAG_C));
    }
    return TRUE;
}

static FCRET CompareNode(const FILECOMPARE *pFC, const NODE *node0, const NODE *node1)
{
    DWORD dwCmpFlags;
    LPTSTR psz0, psz1;
    INT ret;
    if (node0->hash != node1->hash)
        return FCRET_DIFFERENT;

    psz0 = (pFC->dwFlags & FLAG_W) ? node0->pszComp : node0->pszLine;
    psz1 = (pFC->dwFlags & FLAG_W) ? node1->pszComp : node1->pszLine;
    dwCmpFlags = ((pFC->dwFlags & FLAG_C) ? NORM_IGNORECASE : 0);
    ret = CompareString(LOCALE_USER_DEFAULT, dwCmpFlags, psz0, -1, psz1, -1);
    return (ret == CSTR_EQUAL) ? FCRET_IDENTICAL : FCRET_DIFFERENT;
}

static BOOL FindNextLine(LPCTSTR pch, DWORD ich, DWORD cch, LPDWORD pich)
{
    while (ich < cch)
    {
        if (pch[ich] == TEXT('\n') || pch[ich] == TEXT('\0'))
        {
            *pich = ich;
            return TRUE;
        }
        ++ich;
    }
    *pich = cch;
    return FALSE;
}

static FCRET
ParseLines(const FILECOMPARE *pFC, HANDLE *phMapping,
           LARGE_INTEGER *pib, const LARGE_INTEGER *pcb, struct list *list)
{
    DWORD lineno = 1, ich, cch, ichNext = 0, cbView, cchNode;
    LPTSTR psz, pszLine;
    BOOL fLast, bCR;
    NODE *node;

    if (*phMapping == NULL)
        return FCRET_NO_MORE_DATA;

    if (pib->QuadPart >= pcb->QuadPart)
    {
        CloseHandle(*phMapping);
        *phMapping = NULL;
        return FCRET_NO_MORE_DATA;
    }

    cbView = (DWORD)min(pcb->QuadPart - pib->QuadPart, MAX_VIEW_SIZE);
    psz = MapViewOfFile(*phMapping, FILE_MAP_READ, pib->HighPart, pib->LowPart, cbView);
    if (!psz)
    {
        return OutOfMemory();
    }

    ich = 0;
    cch = cbView / sizeof(TCHAR);
    fLast = (pib->QuadPart + cbView >= pcb->QuadPart);
    while (ich < cch &&
           (FindNextLine(psz, ich, cch, &ichNext) ||
            (ichNext == cch && (fLast || ich == 0))))
    {
        bCR = (ichNext > 0) && (psz[ichNext - 1] == TEXT('\r'));
        cchNode = ichNext - ich - bCR;
        pszLine = AllocLine(&psz[ich], cchNode);
        node = AllocNode(pszLine, lineno++);
        if (!node || !ConvertNode(pFC, node))
        {
            DeleteNode(node);
            UnmapViewOfFile(psz);
            return OutOfMemory();
        }
        list_add_tail(list, &node->entry);
        ich = ichNext + 1;
    }

    UnmapViewOfFile(psz);

    pib->QuadPart += ichNext * sizeof(WCHAR);

    if (pib->QuadPart < pcb->QuadPart)
        return FCRET_IDENTICAL;

    // append EOF node
    node = AllocEOFNode(lineno);
    if (!node)
        return OutOfMemory();
    list_add_tail(list, &node->entry);

    return FCRET_NO_MORE_DATA;
}

typedef struct LINEBUF
{
    NODE **ppNodes;
    DWORD cNodes;
    HANDLE *phMapping;
    LARGE_INTEGER ib;
    const LARGE_INTEGER *pcb;
    FCRET ret;
} LINEBUF;

static FCRET
FillBuffer(FILECOMPARE *pFC, INT i, LINEBUF *pBuf)
{
    struct list *list = &pFC->list[i], *ptr;
    NODE *node;
    while (pBuf->cNodes < (DWORD)pFC->n)
    {
        if (pBuf->cNodes)
            ptr = list_next(list, &pBuf->ppNodes[pBuf->cNodes - 1]->entry);
        else
            ptr = list_head(list);
        if (!ptr)
        {
            if (pBuf->ret == FCRET_NO_MORE_DATA)
                break;
            pBuf->ret = ParseLines(pFC, pBuf->phMapping, &pBuf->ib, pBuf->pcb, list);
            if (pBuf->ret == FCRET_INVALID)
                return FCRET_INVALID;
            continue;
        }
        node = LIST_ENTRY(ptr, NODE, entry);
        if (IsEOFNode(node))
            break;
        pBuf->ppNodes[pBuf->cNodes++] = node;
    }
    return FCRET_IDENTICAL;
}

static VOID
DropLines(LINEBUF *pBuf, DWORD cDrop)
{
    DWORD k;
    for (k = 0; k < cDrop; ++k)
    {
        list_remove(&pBuf->ppNodes[k]->entry);
        DeleteNode(pBuf->ppNodes[k]);
    }
    pBuf->cNodes -= cDrop;
    memmove(pBuf->ppNodes, pBuf->ppNodes + cDrop, pBuf->cNodes * sizeof(NODE *));
}

static VOID
ShowLines(FILECOMPARE *pFC, INT i, const LINEBUF *pBuf, DWORD cLines)
{
    NODE *node;
    DWORD k;
    PrintCaption(pFC->file[i]);
    if (!(pFC->dwFlags & FLAG_A) || cLines <= 3)
    {
        for (k = 0; k < cLines; ++k)
        {
            node = pBuf->ppNodes[k];
            PrintLine(pFC, node->lineno, node->pszLine);
        }
        return;
    }
    node = pBuf->ppNodes[0];
    PrintLine(pFC, node->lineno, node->pszLine);
    PrintDots();
    node = pBuf->ppNodes[cLines - 1];
    PrintLine(pFC, node->lineno, node->pszLine);
}

static VOID
ShowBlock(FILECOMPARE *pFC, const LINEBUF *pBuf, DWORD cLines0, DWORD cLines1)
{
    ShowLines(pFC, 0, &pBuf[0], cLines0);
    ShowLines(pFC, 1, &pBuf[1], cLines1);
    PrintEndOfDiff();
}

static BOOL
IsResync(FILECOMPARE *pFC, const LINEBUF *pBuf, DWORD i0, DWORD i1)
{
    DWORD k;
    for (k = 0; k < (DWORD)pFC->nnnn; ++k)
    {
        if (i0 + k >= pBuf[0].cNodes || i1 + k >= pBuf[1].cNodes)
            break;
        if (CompareNode(pFC, pBuf[0].ppNodes[i0 + k], pBuf[1].ppNodes[i1 + k]) != FCRET_IDENTICAL)
            return FALSE;
    }
    return k > 0;
}

static BOOL
FindResync(FILECOMPARE *pFC, const LINEBUF *pBuf, DWORD *pi0, DWORD *pi1)
{
    DWORD d, k, cMax = max(pBuf[0].cNodes, pBuf[1].cNodes);
    for (d = 1; d < cMax; ++d)
    {
        for (k = 1; k < d; ++k)
        {
            if (IsResync(pFC, pBuf, d, k))
            {
                *pi0 = d;
                *pi1 = k;
                return TRUE;
            }
            if (IsResync(pFC, pBuf, k, d))
            {
                *pi0 = k;
                *pi1 = d;
                return TRUE;
            }
        }
        if (IsResync(pFC, pBuf, d, d))
        {
            *pi0 = *pi1 = d;
            return TRUE;
        }
    }
    return FALSE;
}

FCRET TextCompare(FILECOMPARE *pFC, HANDLE *phMapping0, const LARGE_INTEGER *pcb0,
                                    HANDLE *phMapping1, const LARGE_INTEGER *pcb1)
{
    FCRET ret;
    LINEBUF buf[2];
    DWORD m, i0, i1;
    BOOL fDifferent = FALSE;
    INT i;

    list_init(&pFC->list[0]);
    list_init(&pFC->list[1]);
    ZeroMemory(buf, sizeof(buf));
    buf[0].phMapping = phMapping0;
    buf[0].pcb = pcb0;
    buf[1].phMapping = phMapping1;
    buf[1].pcb = pcb1;
    for (i = 0; i < 2; ++i)
    {
        buf[i].ret = FCRET_IDENTICAL;
        if ((DWORD)pFC->n <= MAXLONG / sizeof(NODE *))
            buf[i].ppNodes = malloc(max((DWORD)pFC->n, 1) * sizeof(NODE *));
        if (!buf[i].ppNodes)
        {
            ret = OutOfMemory();
            goto cleanup;
        }
    }

    for (;;)
    {
        if (FillBuffer(pFC, 0, &buf[0]) == FCRET_INVALID ||
            FillBuffer(pFC, 1, &buf[1]) == FCRET_INVALID)
        {
            ret = FCRET_INVALID;
            goto cleanup;
        }
        if (!buf[0].cNodes && !buf[1].cNodes)
            break;

        for (m = 0; m < buf[0].cNodes && m < buf[1].cNodes; ++m)
        {
            if (CompareNode(pFC, buf[0].ppNodes[m], buf[1].ppNodes[m]) != FCRET_IDENTICAL)
                break;
        }
        if (m && (m == buf[0].cNodes || m == buf[1].cNodes))
        {
            DropLines(&buf[0], m);
            DropLines(&buf[1], m);
            continue;
        }

        fDifferent = TRUE;
        if (m > 0)
        {
            DropLines(&buf[0], m - 1);
            DropLines(&buf[1], m - 1);
            if (FillBuffer(pFC, 0, &buf[0]) == FCRET_INVALID ||
                FillBuffer(pFC, 1, &buf[1]) == FCRET_INVALID)
            {
                ret = FCRET_INVALID;
                goto cleanup;
            }
        }

        if (FindResync(pFC, buf, &i0, &i1))
        {
            ShowBlock(pFC, buf, i0 + 1, i1 + 1);
            DropLines(&buf[0], i0);
            DropLines(&buf[1], i1);
            continue;
        }

        if (buf[0].cNodes == (DWORD)pFC->n || buf[1].cNodes == (DWORD)pFC->n)
            ResyncFailed();
        ShowBlock(pFC, buf, buf[0].cNodes, buf[1].cNodes);
        break;
    }

    ret = fDifferent ? FCRET_DIFFERENT : NoDifference();
cleanup:
    free(buf[0].ppNodes);
    free(buf[1].ppNodes);
    DeleteList(&pFC->list[0]);
    DeleteList(&pFC->list[1]);
    return ret;
}

/*
 * PROJECT:     LiberNT user32
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Touch hit testing proximity evaluation
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <stdlib.h>
#include <windef.h>
#include <winbase.h>
#include <winuser.h>

#define PROXIMITY_NONE 0xFFE
#define PROXIMITY_CAP 0xFFD
#define PROXIMITY_BIAS_CAP 20
#define CONTACT_SHAPE_MAX 12

typedef struct _CONTACT
{
    RECT NonOccluded;
    RECT Box;
    POINT Point;
    ULONG Count;
    POINT Shape[CONTACT_SHAPE_MAX];
} CONTACT, *PCONTACT;

static
LONG
Sign(
    _In_ LONG Value)
{
    return Value > 0 ? 1 : (Value < 0 ? -1 : 0);
}

static
LONG
Percent(
    _In_ LONG Length,
    _In_ LONG Percentage)
{
    return (Length * Percentage) / 100;
}

static
BOOL
IsValidInput(
    _In_ const TOUCH_HIT_TESTING_INPUT *Input)
{
    const RECT *Box = &Input->boundingBox, *Visible = &Input->nonOccludedBoundingBox;

    if (!PtInRect(Box, Input->point) &&
        !(Box->left == Input->point.x && Box->right == Input->point.x &&
          Box->top == Input->point.y && Box->bottom == Input->point.y))
    {
        return FALSE;
    }
    return Box->left <= Visible->left && Box->top <= Visible->top &&
           Box->right >= Visible->right && Box->bottom >= Visible->bottom;
}

static
VOID
BuildContact(
    _In_ const TOUCH_HIT_TESTING_INPUT *Input,
    _Out_ PCONTACT Contact)
{
    LONG L = Input->boundingBox.left, T = Input->boundingBox.top;
    LONG R = Input->boundingBox.right, B = Input->boundingBox.bottom;
    LONG W = R - L, H = B - T;

    Contact->NonOccluded = Input->nonOccludedBoundingBox;
    Contact->Box = Input->boundingBox;
    Contact->Point = Input->point;

    if (W < 4 || H < 4)
    {
        Contact->Count = 4;
        Contact->Shape[0].x = L; Contact->Shape[0].y = T;
        Contact->Shape[1].x = L; Contact->Shape[1].y = B;
        Contact->Shape[2].x = R; Contact->Shape[2].y = B;
        Contact->Shape[3].x = R; Contact->Shape[3].y = T;
        return;
    }

    Contact->Count = 12;
    Contact->Shape[0].x = L;                     Contact->Shape[0].y = (T + B) / 2;
    Contact->Shape[1].x = L + Percent(W, 7);     Contact->Shape[1].y = B + Percent(T - B, 25);
    Contact->Shape[2].x = L + Percent(W, 25);    Contact->Shape[2].y = B + Percent(T - B, 7);
    Contact->Shape[3].x = (L + R) / 2;           Contact->Shape[3].y = B;
    Contact->Shape[4].x = L + R - Contact->Shape[2].x; Contact->Shape[4].y = Contact->Shape[2].y;
    Contact->Shape[5].x = L + R - Contact->Shape[1].x; Contact->Shape[5].y = Contact->Shape[1].y;
    Contact->Shape[6].x = R;                     Contact->Shape[6].y = Contact->Shape[0].y;
    Contact->Shape[7].x = Contact->Shape[5].x;   Contact->Shape[7].y = T + B - Contact->Shape[1].y;
    Contact->Shape[8].x = Contact->Shape[4].x;   Contact->Shape[8].y = T + B - Contact->Shape[2].y;
    Contact->Shape[9].x = Contact->Shape[3].x;   Contact->Shape[9].y = T;
    Contact->Shape[10].x = Contact->Shape[2].x;  Contact->Shape[10].y = Contact->Shape[8].y;
    Contact->Shape[11].x = Contact->Shape[1].x;  Contact->Shape[11].y = Contact->Shape[7].y;
}

static
LONG
RoundedQuotient(
    _In_ LONG Numerator,
    _In_ LONG Denominator)
{
    LONG Half = Denominator / 2;

    if (Half < 0)
        Half = -Half;
    return (Numerator + Sign(Numerator) * Half) / Denominator;
}

static
VOID
CrossElementLine(
    _In_ POINT P1,
    _In_ POINT P2,
    _In_ POINT C1,
    _In_ POINT C2,
    _Inout_updates_(2) POINT *Points,
    _Inout_ PULONG Count)
{
    LONG Ax = P1.x - C1.x, Ay = P1.y - C1.y, Bx = P2.x - C1.x, By = P2.y - C1.y;
    LONG Ex = C2.x - C1.x, Ey = C2.y - C1.y;
    LONG S1 = Ax * By - Ay * Bx;
    LONG S2 = (Ax - Ex) * (By - Ey) - (Bx - Ex) * (Ay - Ey);
    POINT Hit;

    if (S1 == 0)
    {
        if (S2 == 0)
            return;
        Hit = C1;
    }
    else if (S2 == 0)
    {
        Hit = C2;
    }
    else
    {
        LONG Denominator;

        if (Sign(S1) == Sign(S2))
            return;
        Denominator = (Ax - Bx) * Ey + (By - Ay) * Ex;
        Hit.x = C1.x + RoundedQuotient(S1 * Ex, Denominator);
        Hit.y = C1.y + RoundedQuotient(S1 * Ey, Denominator);
    }

    if (*Count != 0 && Points[0].x == Hit.x && Points[0].y == Hit.y)
        return;
    Points[(*Count)++] = Hit;
}

static
BOOL
CrossContact(
    _In_ POINT P1,
    _In_ POINT P2,
    _In_ const CONTACT *Contact,
    _Out_ POINT *A,
    _Out_ POINT *B)
{
    POINT Points[CONTACT_SHAPE_MAX + 1];
    ULONG Count = 0, Index;

    if (P1.x == P2.x && P1.y == P2.y)
        return FALSE;
    if (max(P1.x, P2.x) < Contact->NonOccluded.left || min(P1.x, P2.x) > Contact->NonOccluded.right ||
        max(P1.y, P2.y) < Contact->NonOccluded.top || min(P1.y, P2.y) > Contact->NonOccluded.bottom)
    {
        return FALSE;
    }

    for (Index = 0; Index < Contact->Count && Count < 2; Index++)
        CrossElementLine(P1, P2, Contact->Shape[Index], Contact->Shape[(Index + 1) % Contact->Count], Points, &Count);
    if (Count != 2)
        return FALSE;
    *A = Points[0];
    *B = Points[1];
    return TRUE;
}

static
BOOL
ClipEdge(
    _In_ POINT P1,
    _In_ POINT P2,
    _In_ const CONTACT *Contact,
    _Out_ POINT *Start,
    _Out_ POINT *End)
{
    POINT A, B;
    LONG Swap;

    if (!CrossContact(P1, P2, Contact, &A, &B))
        return FALSE;

    Start->x = max(min(A.x, B.x), min(P1.x, P2.x));
    End->x = min(max(A.x, B.x), max(P1.x, P2.x));
    if (Start->x > End->x)
        return FALSE;
    Start->y = max(min(A.y, B.y), min(P1.y, P2.y));
    End->y = min(max(A.y, B.y), max(P1.y, P2.y));
    if (Start->y > End->y)
        return FALSE;

    if (Sign(P1.x - P2.x) * Sign(P1.y - P2.y) != Sign(Start->x - End->x) * Sign(Start->y - End->y))
    {
        Swap = Start->x;
        Start->x = End->x;
        End->x = Swap;
    }
    return TRUE;
}

static
POINT
NearestPoint(
    _In_ POINT A,
    _In_ POINT B,
    _In_ POINT P)
{
    LONG Ab = (B.y - A.y) * (B.y - A.y) + (B.x - A.x) * (B.x - A.x);
    LONG Ap = (P.x - A.x) * (P.x - A.x) + (P.y - A.y) * (P.y - A.y);
    LONG Bp = (P.y - B.y) * (P.y - B.y) + (P.x - B.x) * (P.x - B.x);
    LONGLONG Cross;
    POINT Result;

    if (Bp >= Ap + Ab)
        return A;
    if (Ap >= Bp + Ab)
        return B;

    Cross = (LONGLONG)((P.x - A.x) * (B.y - A.y)) - (LONGLONG)((P.y - A.y) * (B.x - A.x));
    Result.x = (LONG)(((LONGLONG)P.x * Ab - (LONGLONG)(B.y - A.y) * Cross) / Ab);
    Result.y = (LONG)(((LONGLONG)P.y * Ab + (LONGLONG)(B.x - A.x) * Cross) / Ab);
    return Result;
}

static
USHORT
Score(
    _In_ POINT Touch,
    _In_ POINT Candidate,
    _In_ LONG MinX,
    _In_ LONG MaxX,
    _In_ POINT Center)
{
    LONG Distance = abs(Candidate.y - Touch.y) + abs(Candidate.x - Touch.x);
    LONG Bias;

    if (Touch.x > MinX && Touch.x < MaxX)
        Bias = abs(Candidate.y - Center.y);
    else
        Bias = abs(Center.x - Candidate.x);
    if (Bias > PROXIMITY_BIAS_CAP)
        Bias = PROXIMITY_BIAS_CAP;
    Distance += Bias;
    return (USHORT)(Distance < PROXIMITY_CAP ? Distance : PROXIMITY_CAP);
}

static
BOOL
InsidePolygon(
    _In_ UINT32 Count,
    _In_reads_(Count) const POINT *Polygon,
    _In_ POINT P,
    _In_ BOOL RejectOnEdge)
{
    UINT32 Index, Previous = Count - 1;
    BOOL Inside = FALSE;

    for (Index = 0; Index < Count; Previous = Index++)
    {
        POINT Left = Polygon[Previous], Right = Polygon[Index];

        if (Right.x <= Left.x)
        {
            Left = Polygon[Index];
            Right = Polygon[Previous];
        }

        if (!RejectOnEdge)
        {
            if ((Polygon[Index].x < P.x) == (P.x <= Polygon[Previous].x) &&
                (P.y - Left.y) * (Right.x - Left.x) < (P.x - Left.x) * (Right.y - Left.y))
            {
                Inside = !Inside;
            }
            continue;
        }

        if (P.x > Left.x && P.x <= Right.x)
        {
            LONG Below = (P.y - Left.y) * (Right.x - Left.x);
            LONG Line = (P.x - Left.x) * (Right.y - Left.y);

            if (Left.x == Right.x || Below == Line)
                return FALSE;
            if (Below < Line)
                Inside = !Inside;
        }
        else if (Left.x == Right.x && P.x == Left.x)
        {
            return FALSE;
        }
    }
    return Inside;
}

static
VOID
NudgeInside(
    _In_ UINT32 Count,
    _In_reads_(Count) const POINT *Polygon,
    _In_ const CONTACT *Contact,
    _Inout_ POINT *Adjusted)
{
    LONG Dx = 0, Dy = 0;
    ULONG Variant;

    if (Adjusted->x < Contact->Point.x)
        Dx = Adjusted->x > Contact->NonOccluded.left ? -1 : 0;
    else if (Adjusted->x > Contact->Point.x)
        Dx = Adjusted->x < Contact->NonOccluded.right - 1 ? 1 : 0;
    if (Adjusted->y < Contact->Point.y)
        Dy = Adjusted->y > Contact->NonOccluded.top ? -1 : 0;
    else if (Adjusted->y > Contact->Point.y)
        Dy = Adjusted->y < Contact->NonOccluded.bottom - 1 ? 1 : 0;

    for (Variant = 0; Variant < 4; Variant++)
    {
        POINT Candidate = *Adjusted;
        BOOL Moved = (Variant == 0);

        if ((Variant & 1) && Dx)
        {
            Candidate.x += Dx;
            Moved = TRUE;
        }
        if ((Variant & ~1UL) == 2 && Dy)
        {
            Candidate.y += Dy;
            Moved = TRUE;
        }
        if (!Moved)
            continue;
        if (PtInRect(&Contact->NonOccluded, Candidate) && InsidePolygon(Count, Polygon, Candidate, TRUE))
        {
            *Adjusted = Candidate;
            return;
        }
    }
}

BOOL
WINAPI
EvaluateProximityToPolygon(
    _In_ UINT32 numVertices,
    _In_reads_(numVertices) const POINT *controlPolygon,
    _In_ const TOUCH_HIT_TESTING_INPUT *pHitTestingInput,
    _Out_ TOUCH_HIT_TESTING_PROXIMITY_EVALUATION *pProximityEval)
{
    LONGLONG Area = 0, SumX = 0, SumY = 0;
    POINT Center, Touch, Start, End, Candidate;
    LONG MinX, MaxX;
    USHORT Best = PROXIMITY_NONE;
    CONTACT Contact;
    UINT32 Index, Previous;

    if (!pHitTestingInput || !pProximityEval || !controlPolygon || numVertices < 3 || !IsValidInput(pHitTestingInput))
    {
        SetLastError(ERROR_BAD_ARGUMENTS);
        return FALSE;
    }

    Touch = pHitTestingInput->point;
    MinX = MaxX = controlPolygon[0].x;
    for (Index = 0, Previous = numVertices - 1; Index < numVertices; Previous = Index++)
    {
        LONG Cross = controlPolygon[Previous].x * controlPolygon[Index].y - controlPolygon[Index].x * controlPolygon[Previous].y;

        SumX += (LONGLONG)(controlPolygon[Index].x + controlPolygon[Previous].x) * Cross;
        SumY += (LONGLONG)(controlPolygon[Index].y + controlPolygon[Previous].y) * Cross;
        Area += Cross;
        MinX = min(MinX, controlPolygon[Index].x);
        MaxX = max(MaxX, controlPolygon[Index].x);
    }

    if (InsidePolygon(numVertices, controlPolygon, Touch, FALSE))
    {
        pProximityEval->score = TOUCH_HIT_TESTING_PROXIMITY_CLOSEST;
        pProximityEval->adjustedPoint = Touch;
        return TRUE;
    }
    if (Area == 0)
    {
        pProximityEval->score = TOUCH_HIT_TESTING_PROXIMITY_FARTHEST;
        return TRUE;
    }

    Center.x = (LONG)(SumX / (3 * Area));
    Center.y = (LONG)(SumY / (3 * Area));
    pProximityEval->adjustedPoint = Touch;
    BuildContact(pHitTestingInput, &Contact);

    for (Index = 0, Previous = numVertices - 1; Index < numVertices; Previous = Index++)
    {
        USHORT Value;

        if (!ClipEdge(controlPolygon[Previous], controlPolygon[Index], &Contact, &Start, &End))
            continue;
        Candidate = NearestPoint(Start, End, Touch);
        if (Candidate.x == pProximityEval->adjustedPoint.x && Candidate.y == pProximityEval->adjustedPoint.y)
            continue;
        Value = Score(Touch, Candidate, MinX, MaxX, Center);
        if (Value < Best)
        {
            Best = Value;
            pProximityEval->adjustedPoint = Candidate;
        }
    }

    if (Best != PROXIMITY_NONE)
        NudgeInside(numVertices, controlPolygon, &Contact, &pProximityEval->adjustedPoint);
    pProximityEval->score = Best;
    return TRUE;
}

static
VOID
EvaluateRectEdge(
    _In_ POINT P1,
    _In_ POINT P2,
    _In_ const RECT *Box,
    _In_ const CONTACT *Contact,
    _Inout_ POINT *BestPoint,
    _Inout_ USHORT *Best)
{
    POINT Candidate = { 0, 0 }, A, B, Center;
    USHORT Value;

    if (max(P1.x, P2.x) >= Contact->NonOccluded.left && min(P1.x, P2.x) < Contact->NonOccluded.right &&
        max(P1.y, P2.y) >= Contact->NonOccluded.top && min(P1.y, P2.y) < Contact->NonOccluded.bottom &&
        ClipEdge(P1, P2, Contact, &A, &B))
    {
        POINT S = A, E = B;

        if (A.x == B.x)
        {
            S.y = max(min(A.y, B.y), Contact->NonOccluded.top);
            E.x = A.x;
            E.y = min(max(A.y, B.y), Contact->NonOccluded.bottom - 1);
        }
        else
        {
            S.x = max(min(A.x, B.x), Contact->NonOccluded.left);
            E.x = min(max(A.x, B.x), Contact->NonOccluded.right - 1);
            E.y = A.y;
        }
        Candidate = NearestPoint(S, E, Contact->Point);
    }

    if (*Best != PROXIMITY_NONE && Candidate.x == BestPoint->x && Candidate.y == BestPoint->y)
        return;
    Center.x = (Box->left + Box->right) / 2;
    Center.y = (Box->top + Box->bottom) / 2;
    Value = Score(Contact->Point, Candidate, Box->left, Box->right, Center);
    if (*Best == PROXIMITY_NONE || Value < *Best)
    {
        *Best = Value;
        *BestPoint = Candidate;
    }
}

BOOL
WINAPI
EvaluateProximityToRect(
    _In_ const RECT *controlBoundingBox,
    _In_ const TOUCH_HIT_TESTING_INPUT *pHitTestingInput,
    _Out_ TOUCH_HIT_TESTING_PROXIMITY_EVALUATION *pProximityEval)
{
    const RECT *Box = controlBoundingBox;
    USHORT Best = PROXIMITY_NONE;
    POINT BestPoint = { 0, 0 };
    CONTACT Contact;
    RECT Overlap;

    if (!Box || !pHitTestingInput || !pProximityEval || !IsValidInput(pHitTestingInput))
    {
        SetLastError(ERROR_BAD_ARGUMENTS);
        return FALSE;
    }
    if (Box->top == Box->bottom || Box->left == Box->right)
    {
        pProximityEval->score = TOUCH_HIT_TESTING_PROXIMITY_FARTHEST;
        return TRUE;
    }
    if (PtInRect(Box, pHitTestingInput->point))
    {
        pProximityEval->score = TOUCH_HIT_TESTING_PROXIMITY_CLOSEST;
        pProximityEval->adjustedPoint = pHitTestingInput->point;
        return TRUE;
    }

    BuildContact(pHitTestingInput, &Contact);
    if (IntersectRect(&Overlap, Box, &Contact.NonOccluded) && Box->left < Box->right && Box->top < Box->bottom)
    {
        LONG L = Box->left, T = Box->top, R = Box->right - 1, B = Box->bottom - 1;
        POINT TopLeft = { L, T }, BottomLeft = { L, B }, BottomRight = { R, B }, TopRight = { R, T };

        if (L == R)
        {
            EvaluateRectEdge(TopLeft, BottomLeft, Box, &Contact, &BestPoint, &Best);
        }
        else if (T == B)
        {
            EvaluateRectEdge(TopLeft, TopRight, Box, &Contact, &BestPoint, &Best);
        }
        else
        {
            EvaluateRectEdge(TopLeft, BottomLeft, Box, &Contact, &BestPoint, &Best);
            EvaluateRectEdge(BottomRight, TopRight, Box, &Contact, &BestPoint, &Best);
            EvaluateRectEdge(BottomLeft, BottomRight, Box, &Contact, &BestPoint, &Best);
            EvaluateRectEdge(TopRight, TopLeft, Box, &Contact, &BestPoint, &Best);
        }
    }

    pProximityEval->score = Best;
    pProximityEval->adjustedPoint = pHitTestingInput->point;
    if (Best != PROXIMITY_NONE)
    {
        pProximityEval->adjustedPoint.x += BestPoint.x - Contact.Point.x;
        pProximityEval->adjustedPoint.y += BestPoint.y - Contact.Point.y;
    }
    return TRUE;
}

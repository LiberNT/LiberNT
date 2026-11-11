/*
 * PROJECT:     LiberNT Text-to-Speech Engine
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     English letter-to-sound rules and number reading
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "synthcore.h"
#include <string.h>

/* Contexts: ' ' word edge, '#' vowels, ':' any consonants, '^' consonant, '.' voiced consonant, '+' E/I/Y, '&' sibilant, '@' T/S/R/D/L/Z/N/J/TH/CH/SH, '%' word-final E/ER/ES/ED/ING/ELY */
typedef struct _LTS_RULE
{
    const char *Left;
    const char *Match;
    const char *Right;
    const char *Out;
} LTS_RULE;

static const LTS_RULE RulesA[] =
{
    {" ", "A", " ", "AH"}, {" ", "ARE", " ", "AA R"}, {" ", "AR", "O", "AH R"},
    {"", "AR", "#", "EH R"}, {" ^", "AS", "#", "EY S"}, {"", "A", "WA", "AH"},
    {"", "AW", "", "AO"}, {" :", "ANY", "", "EH N IY"}, {"", "A", "^+#", "EY"},
    {"#:", "ALLY", "", "AH L IY"}, {" ", "AL", "#", "AH L"}, {"", "AGAIN", "", "AH G EH N"},
    {"#:", "AG", "E", "IH JH"}, {"", "A", "^+:#", "AE"}, {" :", "A", "^+ ", "EY"},
    {"", "A", "^%", "EY"}, {" ", "ARR", "", "AH R"}, {"", "ARR", "", "AE R"},
    {" :", "AR", " ", "AA R"}, {"", "AR", " ", "ER"}, {"", "AR", "", "AA R"},
    {"", "AIR", "", "EH R"}, {"", "AI", "", "EY"}, {"", "AY", "", "EY"},
    {"", "AU", "", "AO"}, {"#:", "AL", " ", "AH L"}, {"#:", "ALS", " ", "AH L Z"},
    {"", "ALK", "", "AO K"}, {"", "AL", "^", "AO L"}, {" :", "ABLE", "", "EY B AH L"},
    {"", "ABLE", "", "AH B AH L"}, {"", "ANG", "+", "EY N JH"}, {"", "A", "", "AE"},
    {NULL}
};

static const LTS_RULE RulesB[] =
{
    {" ", "BE", "^#", "B IH"}, {"", "BEING", "", "B IY IH NG"}, {" ", "BOTH", " ", "B OW TH"},
    {" ", "BUS", "#", "B IH Z"}, {"", "BUIL", "", "B IH L"}, {"", "B", "", "B"},
    {NULL}
};

static const LTS_RULE RulesC[] =
{
    {" ", "CH", "^", "K"}, {"^E", "CH", "", "K"}, {"", "CH", "", "CH"},
    {" S", "CI", "#", "S AY"}, {"", "CI", "A", "SH"}, {"", "CI", "O", "SH"},
    {"", "CI", "EN", "SH"}, {"", "C", "+", "S"}, {"", "CK", "", "K"},
    {"", "COM", "%", "K AH M"}, {"", "C", "", "K"},
    {NULL}
};

static const LTS_RULE RulesD[] =
{
    {"#:", "DED", " ", "D IH D"}, {".E", "D", " ", "D"}, {"#^:E", "D", " ", "T"},
    {" ", "DE", "^#", "D IH"}, {" ", "DO", " ", "D UW"}, {" ", "DOES", "", "D AH Z"},
    {" ", "DOING", "", "D UW IH NG"}, {" ", "DOW", "", "D AW"}, {"", "DU", "A", "JH UW"},
    {"", "D", "", "D"},
    {NULL}
};

static const LTS_RULE RulesE[] =
{
    {"#:", "E", " ", ""}, {" :", "E", " ", "IY"}, {"#", "ED", " ", "D"},
    {"#:", "E", "D ", ""}, {"", "EV", "ER", "EH V"}, {"", "E", "^%", "IY"},
    {"", "ERI", "#", "IY R IY"}, {"", "ERI", "", "EH R IH"}, {"#:", "ER", "#", "ER"},
    {"", "ER", "#", "EH R"}, {"", "ER", "", "ER"}, {" ", "EVEN", "", "IY V EH N"},
    {"#:", "E", "W", ""}, {"@", "EW", "", "UW"}, {"", "EW", "", "Y UW"},
    {"", "E", "O", "IY"}, {"#:&", "ES", " ", "IH Z"}, {"#:", "E", "S ", ""},
    {"#:", "ELY", " ", "L IY"}, {"#:", "EMENT", "", "M EH N T"}, {"", "EFUL", "", "F UH L"},
    {"", "EE", "", "IY"}, {"", "EARN", "", "ER N"}, {" ", "EAR", "^", "ER"},
    {"", "EAD", "", "EH D"}, {"#:", "EA", " ", "IY AH"}, {"", "EA", "SU", "EH"},
    {"", "EA", "", "IY"}, {"", "EIGH", "", "EY"}, {"", "EI", "", "IY"},
    {" ", "EYE", "", "AY"}, {"", "EY", "", "IY"}, {"", "EU", "", "Y UW"},
    {"", "E", "", "EH"},
    {NULL}
};

static const LTS_RULE RulesF[] =
{
    {"", "FUL", "", "F UH L"}, {"", "F", "", "F"},
    {NULL}
};

static const LTS_RULE RulesG[] =
{
    {"", "GIV", "", "G IH V"}, {" ", "G", "I^", "G"}, {"", "GE", "T", "G EH"},
    {"SU", "GGES", "", "G JH EH S"}, {"", "GG", "", "G"}, {" B#", "G", "", "G"},
    {"", "G", "+", "JH"}, {"", "GREAT", "", "G R EY T"}, {"#", "GH", "", ""},
    {"", "G", "", "G"},
    {NULL}
};

static const LTS_RULE RulesH[] =
{
    {" ", "HAV", "", "HH AE V"}, {" ", "HERE", "", "HH IY R"}, {" ", "HOUR", "", "AW ER"},
    {"", "HOW", "", "HH AW"}, {"", "H", "#", "HH"}, {"", "H", "", ""},
    {NULL}
};

static const LTS_RULE RulesI[] =
{
    {" ", "IN", "", "IH N"}, {" ", "I", " ", "AY"}, {"", "IN", "D", "AY N"},
    {"", "IER", "", "IY ER"}, {"#:R", "IED", "", "IY D"}, {"", "IED", " ", "AY D"},
    {"", "IEN", "", "IY EH N"}, {"", "IE", "T", "AY EH"}, {" :", "I", "%", "AY"},
    {"", "I", "%", "IY"}, {"", "IE", "", "IY"}, {"", "I", "^+:#", "IH"},
    {"", "IR", "#", "AY R"}, {"", "IZ", "%", "AY Z"}, {"", "IS", "%", "AY Z"},
    {"", "I", "D%", "AY"}, {"+^", "I", "^+", "IH"}, {"", "I", "T%", "AY"},
    {"#^:", "I", "^+", "IH"}, {"", "IR", "", "ER"}, {"", "IGH", "", "AY"},
    {"", "ILD", "", "AY L D"}, {"", "IGN", " ", "AY N"}, {"", "IGN", "^", "AY N"},
    {"", "IGN", "%", "AY N"}, {"", "IQUE", "", "IY K"}, {"", "I", "", "IH"},
    {NULL}
};

static const LTS_RULE RulesJ[] =
{
    {"", "J", "", "JH"},
    {NULL}
};

static const LTS_RULE RulesK[] =
{
    {" ", "K", "N", ""}, {"", "K", "", "K"},
    {NULL}
};

static const LTS_RULE RulesL[] =
{
    {"", "LO", "C#", "L OW"}, {"L", "L", "", ""}, {"#^:", "L", "%", "AH L"},
    {"", "LEAD", "", "L IY D"}, {"", "L", "", "L"},
    {NULL}
};

static const LTS_RULE RulesM[] =
{
    {"", "MOV", "", "M UW V"}, {"", "M", "", "M"},
    {NULL}
};

static const LTS_RULE RulesN[] =
{
    {"E", "NG", "+", "N JH"}, {"", "NG", "R", "NG G"}, {"", "NG", "#", "NG G"},
    {"", "NGL", "%", "NG G AH L"}, {"", "NG", "", "NG"}, {"", "NK", "", "NG K"},
    {" ", "NOW", " ", "N AW"}, {"", "N", "", "N"},
    {NULL}
};

static const LTS_RULE RulesO[] =
{
    {"", "OF", " ", "AH V"}, {"", "OROUGH", "", "ER OW"}, {"#:", "OR", " ", "ER"},
    {"#:", "ORS", " ", "ER Z"}, {"", "OR", "", "AO R"}, {" ", "ONE", "", "W AH N"},
    {"", "OW", "", "OW"}, {" ", "OVER", "", "OW V ER"}, {"", "OV", "", "AH V"},
    {"", "O", "^%", "OW"}, {"", "O", "^EN", "OW"}, {"", "O", "^I#", "OW"},
    {"", "OL", "D", "OW L"}, {"", "OUGHT", "", "AO T"}, {"", "OUGH", "", "AH F"},
    {" ", "OU", "", "AW"}, {"H", "OU", "S#", "AW"}, {"", "OUS", "", "AH S"},
    {"", "OUR", "", "AO R"}, {"", "OULD", "", "UH D"}, {"^", "OU", "^L", "AH"},
    {"", "OUP", "", "UW P"}, {"", "OU", "", "AW"}, {"", "OY", "", "OY"},
    {"", "OING", "", "OW IH NG"}, {"", "OI", "", "OY"}, {"", "OOR", "", "AO R"},
    {"", "OOK", "", "UH K"}, {"", "OOD", "", "UH D"}, {"", "OO", "", "UW"},
    {"", "O", "E", "OW"}, {"", "O", " ", "OW"}, {"", "OA", "", "OW"},
    {" ", "ONLY", "", "OW N L IY"}, {" ", "ONCE", "", "W AH N S"}, {"", "ON'T", "", "OW N T"},
    {"C", "O", "N", "AA"}, {"", "O", "NG", "AO"}, {" :^", "O", "N", "AH"},
    {"I", "ON", "", "AH N"}, {"#:", "ON", " ", "AH N"}, {"#^", "ON", "", "AH N"},
    {"", "O", "ST ", "OW"}, {"", "OF", "^", "AO F"}, {"", "OTHER", "", "AH DH ER"},
    {"", "OSS", " ", "AO S"}, {"#^:", "OM", "", "AH M"}, {"", "O", "", "AA"},
    {NULL}
};

static const LTS_RULE RulesP[] =
{
    {"", "PH", "", "F"}, {"", "PEOP", "", "P IY P"}, {"", "POW", "", "P AW"},
    {"", "PUT", " ", "P UH T"}, {"", "P", "", "P"},
    {NULL}
};

static const LTS_RULE RulesQ[] =
{
    {"", "QUAR", "", "K W AO R"}, {"", "QU", "", "K W"}, {"", "Q", "", "K"},
    {NULL}
};

static const LTS_RULE RulesR[] =
{
    {" ", "RE", "^#", "R IY"}, {"", "R", "", "R"},
    {NULL}
};

static const LTS_RULE RulesS[] =
{
    {"", "SH", "", "SH"}, {"#", "SION", "", "ZH AH N"}, {"", "SOME", "", "S AH M"},
    {"#", "SUR", "#", "ZH ER"}, {"", "SUR", "#", "SH ER"}, {"#", "SU", "#", "ZH UW"},
    {"#", "SSU", "#", "SH UW"}, {"#", "SED", " ", "Z D"}, {"#", "S", "#", "Z"},
    {"", "SAID", "", "S EH D"}, {"^", "SION", "", "SH AH N"}, {"", "S", "S", ""},
    {".", "S", " ", "Z"}, {"#:.E", "S", " ", "Z"}, {"#^:##", "S", " ", "Z"},
    {"#^:#", "S", " ", "S"}, {"U", "S", " ", "S"}, {" :#", "S", " ", "Z"},
    {" ", "SCH", "", "S K"}, {"", "S", "C+", ""}, {"#", "SM", "", "Z M"},
    {"", "S", "", "S"},
    {NULL}
};

static const LTS_RULE RulesT[] =
{
    {" ", "THE", " ", "DH AH"}, {"", "TO", " ", "T UW"}, {"", "THAT", " ", "DH AE T"},
    {" ", "THIS", " ", "DH IH S"}, {" ", "THEY", "", "DH EY"}, {" ", "THERE", "", "DH EH R"},
    {"", "THER", "", "DH ER"}, {"", "THEIR", "", "DH EH R"}, {" ", "THAN", " ", "DH AE N"},
    {" ", "THEM", " ", "DH EH M"}, {"", "THESE", " ", "DH IY Z"}, {" ", "THEN", "", "DH EH N"},
    {"", "THROUGH", "", "TH R UW"}, {"", "THOSE", "", "DH OW Z"}, {"", "THOUGH", " ", "DH OW"},
    {" ", "THUS", "", "DH AH S"}, {"", "TH", "", "TH"}, {"#:", "TED", " ", "T IH D"},
    {"S", "TI", "#N", "CH"}, {"", "TI", "O", "SH"}, {"", "TI", "A", "SH"},
    {"", "TIEN", "", "SH AH N"}, {"", "TUR", "#", "CH ER"}, {"", "TU", "A", "CH UW"},
    {" ", "TWO", "", "T UW"}, {"", "T", "", "T"},
    {NULL}
};

static const LTS_RULE RulesU[] =
{
    {" ", "UN", "I", "Y UW N"}, {" ", "UN", "", "AH N"}, {" ", "UPON", "", "AH P AO N"},
    {"@", "UR", "#", "UH R"}, {"", "UR", "#", "Y UH R"}, {"", "UR", "", "ER"},
    {"", "U", "^ ", "AH"}, {"", "U", "^^", "AH"}, {"", "UY", "", "AY"},
    {" G", "U", "#", ""}, {"G", "U", "%", ""}, {"G", "U", "#", "W"},
    {"#N", "U", "", "Y UW"}, {"@", "U", "", "UW"}, {"", "U", "", "Y UW"},
    {NULL}
};

static const LTS_RULE RulesV[] =
{
    {"", "VIEW", "", "V Y UW"}, {"", "V", "", "V"},
    {NULL}
};

static const LTS_RULE RulesW[] =
{
    {" ", "WERE", "", "W ER"}, {"", "WA", "S", "W AA"}, {"", "WA", "T", "W AA"},
    {"", "WHERE", "", "W EH R"}, {"", "WHAT", "", "W AH T"}, {"", "WHOL", "", "HH OW L"},
    {"", "WHO", "", "HH UW"}, {"", "WH", "", "W"}, {"", "WAR", "", "W AO R"},
    {"", "WOR", "^", "W ER"}, {"", "WR", "", "R"}, {"", "W", "", "W"},
    {NULL}
};

static const LTS_RULE RulesX[] =
{
    {" ", "X", "", "Z"}, {"", "X", "", "K S"},
    {NULL}
};

static const LTS_RULE RulesY[] =
{
    {"", "YOUNG", "", "Y AH NG"}, {" ", "YOU", "", "Y UW"}, {" ", "YES", "", "Y EH S"},
    {" ", "Y", "", "Y"}, {"#^:", "Y", " ", "IY"}, {"#^:", "Y", "I", "IY"},
    {" :", "Y", " ", "AY"}, {" :", "Y", "#", "AY"}, {" :", "Y", "^+:#", "IH"},
    {" :", "Y", "^#", "AY"}, {"", "Y", "", "IH"},
    {NULL}
};

static const LTS_RULE RulesZ[] =
{
    {"", "Z", "", "Z"},
    {NULL}
};

static const LTS_RULE *const RuleSets[26] =
{
    RulesA, RulesB, RulesC, RulesD, RulesE, RulesF, RulesG, RulesH, RulesI,
    RulesJ, RulesK, RulesL, RulesM, RulesN, RulesO, RulesP, RulesQ, RulesR,
    RulesS, RulesT, RulesU, RulesV, RulesW, RulesX, RulesY, RulesZ
};

static const char *const PhonemeNames[PH_COUNT] =
{
    "_",
    "AA", "AE", "AH", "AO", "AW", "AY", "EH", "ER", "EY",
    "IH", "IY", "OW", "OY", "UH", "UW",
    "B", "CH", "D", "DH", "F", "G", "HH", "JH", "K", "L",
    "M", "N", "NG", "P", "R", "S", "SH", "T", "TH", "V",
    "W", "Y", "Z", "ZH"
};

static const char *const LetterNames[26] =
{
    "EY", "B IY", "S IY", "D IY", "IY", "EH F", "JH IY", "EY CH", "AY",
    "JH EY", "K EY", "EH L", "EH M", "EH N", "OW", "P IY", "K Y UW", "AA R",
    "EH S", "T IY", "Y UW", "V IY", "D AH B AH L Y UW", "EH K S", "W AY", "Z IY"
};

static BOOL IsLetter(char c) { return c >= 'A' && c <= 'Z'; }
static BOOL IsVowel(char c) { return c && strchr("AEIOUY", c) != NULL; }
static BOOL IsConsonant(char c) { return IsLetter(c) && !IsVowel(c); }
static BOOL IsVoiced(char c) { return c && strchr("BDVGJLMNRWZ", c) != NULL; }
static BOOL IsFront(char c) { return c && strchr("EIY", c) != NULL; }

static ULONG
AppendPhonemes(
    _In_z_ const char *Names,
    _Inout_updates_(Capacity) UCHAR *Phonemes,
    _In_ ULONG Count,
    _In_ ULONG Capacity)
{
    char Name[4];
    ULONG Length, Index;

    while (*Names)
    {
        while (*Names == ' ')
            Names++;
        for (Length = 0; Names[Length] && Names[Length] != ' ' && Length < sizeof(Name) - 1; Length++)
            Name[Length] = Names[Length];
        Name[Length] = 0;
        Names += Length;
        if (!Length)
            break;
        for (Index = 1; Index < PH_COUNT; Index++)
        {
            if (!strcmp(PhonemeNames[Index], Name))
            {
                if (Count < Capacity)
                    Phonemes[Count++] = (UCHAR)Index;
                break;
            }
        }
    }

    return Count;
}

static BOOL
MatchLeft(
    _In_z_ const char *Word,
    _In_ LONG Position,
    _In_z_ const char *Context)
{
    LONG c = (LONG)strlen(Context) - 1;

    for (; c >= 0; c--)
    {
        char Want = Context[c];
        char Have = Position >= 0 ? Word[Position] : ' ';

        switch (Want)
        {
            case ' ':
                if (IsLetter(Have))
                    return FALSE;
                Position--;
                break;
            case '#':
                if (!IsVowel(Have))
                    return FALSE;
                while (Position >= 0 && IsVowel(Word[Position]))
                    Position--;
                break;
            case ':':
                while (Position >= 0 && IsConsonant(Word[Position]))
                    Position--;
                break;
            case '^':
                if (!IsConsonant(Have))
                    return FALSE;
                Position--;
                break;
            case '.':
                if (!IsVoiced(Have))
                    return FALSE;
                Position--;
                break;
            case '+':
                if (!IsFront(Have))
                    return FALSE;
                Position--;
                break;
            case '&':
                if (Position >= 1 && Have == 'H' && (Word[Position - 1] == 'C' || Word[Position - 1] == 'S'))
                    Position -= 2;
                else if (Have && strchr("SCGZXJ", Have))
                    Position--;
                else
                    return FALSE;
                break;
            case '@':
                if (Position >= 1 && Have == 'H' && strchr("TCS", Word[Position - 1]))
                    Position -= 2;
                else if (Have && strchr("TSRDLZNJ", Have))
                    Position--;
                else
                    return FALSE;
                break;
            default:
                if (Have != Want)
                    return FALSE;
                Position--;
                break;
        }
    }

    return TRUE;
}

static BOOL
MatchSuffix(
    _In_z_ const char *Rest)
{
    static const char *const Suffixes[] = { "ELY", "ING", "ER", "ES", "ED", "E" };
    ULONG i, Length;

    for (i = 0; i < ARRAYSIZE(Suffixes); i++)
    {
        Length = (ULONG)strlen(Suffixes[i]);
        if (!strncmp(Rest, Suffixes[i], Length) && !IsLetter(Rest[Length]))
            return TRUE;
    }

    return FALSE;
}

static BOOL
MatchRight(
    _In_z_ const char *Word,
    _In_ ULONG Position,
    _In_z_ const char *Context)
{
    for (; *Context; Context++)
    {
        char Have = Word[Position];

        switch (*Context)
        {
            case ' ':
                if (IsLetter(Have))
                    return FALSE;
                if (Have)
                    Position++;
                break;
            case '#':
                if (!IsVowel(Have))
                    return FALSE;
                while (IsVowel(Word[Position]))
                    Position++;
                break;
            case ':':
                while (IsConsonant(Word[Position]))
                    Position++;
                break;
            case '^':
                if (!IsConsonant(Have))
                    return FALSE;
                Position++;
                break;
            case '.':
                if (!IsVoiced(Have))
                    return FALSE;
                Position++;
                break;
            case '+':
                if (!IsFront(Have))
                    return FALSE;
                Position++;
                break;
            case '%':
                return MatchSuffix(Word + Position);
            case '&':
                if ((Have == 'C' || Have == 'S') && Word[Position + 1] == 'H')
                    Position += 2;
                else if (Have && strchr("SCGZXJ", Have))
                    Position++;
                else
                    return FALSE;
                break;
            case '@':
                if (Have && strchr("TCS", Have) && Word[Position + 1] == 'H')
                    Position += 2;
                else if (Have && strchr("TSRDLZNJ", Have))
                    Position++;
                else
                    return FALSE;
                break;
            default:
                if (Have != *Context)
                    return FALSE;
                Position++;
                break;
        }
    }

    return TRUE;
}

ULONG
LtsWordToPhonemes(
    _In_z_ const char *Word,
    _Out_writes_(Capacity) UCHAR *Phonemes,
    _In_ ULONG Capacity)
{
    ULONG Position = 0, Count = 0, Length;
    const LTS_RULE *Rule;

    while (Word[Position])
    {
        char Letter = Word[Position];

        if (!IsLetter(Letter))
        {
            Position++;
            continue;
        }

        for (Rule = RuleSets[Letter - 'A']; Rule->Match; Rule++)
        {
            Length = (ULONG)strlen(Rule->Match);
            if (strncmp(Word + Position, Rule->Match, Length))
                continue;
            if (!MatchLeft(Word, (LONG)Position - 1, Rule->Left))
                continue;
            if (!MatchRight(Word, Position + Length, Rule->Right))
                continue;
            Count = AppendPhonemes(Rule->Out, Phonemes, Count, Capacity);
            Position += Length;
            break;
        }

        if (!Rule->Match)
            Position++;
    }

    return Count;
}

ULONG
LtsSpellLetter(
    _In_ char Letter,
    _Out_writes_(Capacity) UCHAR *Phonemes,
    _In_ ULONG Capacity)
{
    if (!IsLetter(Letter))
        return 0;
    return AppendPhonemes(LetterNames[Letter - 'A'], Phonemes, 0, Capacity);
}

static void
AppendWord(
    _Inout_updates_z_(Capacity) char *Words,
    _In_ ULONG Capacity,
    _In_z_ const char *Word)
{
    size_t Used = strlen(Words);

    if (Used && Used + 1 < Capacity)
        Words[Used++] = ' ';
    while (*Word && Used + 1 < Capacity)
        Words[Used++] = *Word++;
    Words[Used] = 0;
}

static void
AppendBelowThousand(
    _In_ ULONG Value,
    _Inout_updates_z_(Capacity) char *Words,
    _In_ ULONG Capacity)
{
    static const char *const Ones[20] =
    {
        "ZERO", "ONE", "TWO", "THREE", "FOUR", "FIVE", "SIX", "SEVEN", "EIGHT", "NINE",
        "TEN", "ELEVEN", "TWELVE", "THIRTEEN", "FOURTEEN", "FIFTEEN", "SIXTEEN",
        "SEVENTEEN", "EIGHTEEN", "NINETEEN"
    };
    static const char *const Tens[10] =
    {
        "", "", "TWENTY", "THIRTY", "FORTY", "FIFTY", "SIXTY", "SEVENTY", "EIGHTY", "NINETY"
    };

    if (Value >= 100)
    {
        AppendWord(Words, Capacity, Ones[Value / 100]);
        AppendWord(Words, Capacity, "HUNDRED");
        Value %= 100;
        if (!Value)
            return;
    }
    if (Value >= 20)
    {
        AppendWord(Words, Capacity, Tens[Value / 10]);
        Value %= 10;
        if (!Value)
            return;
    }
    AppendWord(Words, Capacity, Ones[Value]);
}

ULONG
LtsNumberToWords(
    _In_ ULONGLONG Value,
    _Out_writes_(Capacity) char *Words,
    _In_ ULONG Capacity)
{
    static const struct { ULONGLONG Scale; const char *Name; } Scales[] =
    {
        { 1000000000ULL, "BILLION" }, { 1000000ULL, "MILLION" }, { 1000ULL, "THOUSAND" }
    };
    ULONG i;

    if (!Capacity)
        return 0;
    Words[0] = 0;

    if (Value >= 1000000000000ULL)
        return 0;
    if (Value == 0)
    {
        AppendWord(Words, Capacity, "ZERO");
        return (ULONG)strlen(Words);
    }

    for (i = 0; i < ARRAYSIZE(Scales); i++)
    {
        if (Value >= Scales[i].Scale)
        {
            AppendBelowThousand((ULONG)(Value / Scales[i].Scale), Words, Capacity);
            AppendWord(Words, Capacity, Scales[i].Name);
            Value %= Scales[i].Scale;
        }
    }
    if (Value)
        AppendBelowThousand((ULONG)Value, Words, Capacity);

    return (ULONG)strlen(Words);
}

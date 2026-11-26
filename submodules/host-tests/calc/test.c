/*
 * PROJECT:     LiberNT host-native tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Calculator arithmetic and key handling tests
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <calcengine.h>
#include <stdio.h>
#include <string.h>

static int Failures;
static CALC_ENGINE Engine;

typedef struct _KEY_NAME
{
    const char *Name;
    CALC_KEY Key;
} KEY_NAME;

static const KEY_NAME KeyNames[] =
{
    {"+", CALC_KEY_ADD}, {"-", CALC_KEY_SUBTRACT}, {"*", CALC_KEY_MULTIPLY}, {"/", CALC_KEY_DIVIDE},
    {"=", CALC_KEY_EQUALS}, {"%", CALC_KEY_PERCENT}, {"(", CALC_KEY_OPEN}, {")", CALC_KEY_CLOSE},
    {"^", CALC_KEY_POWER}, {"yroot", CALC_KEY_ROOT}, {"mod", CALC_KEY_MODULO}, {"neg", CALC_KEY_SIGN},
    {"inv", CALC_KEY_RECIPROCAL}, {"sqr", CALC_KEY_SQUARE}, {"sqrt", CALC_KEY_SQUARE_ROOT},
    {"cube", CALC_KEY_CUBE}, {"abs", CALC_KEY_ABSOLUTE}, {"fact", CALC_KEY_FACTORIAL},
    {"10^", CALC_KEY_TEN_POWER}, {"e^", CALC_KEY_E_POWER}, {"log", CALC_KEY_LOG}, {"ln", CALC_KEY_LN},
    {"sin", CALC_KEY_SIN}, {"cos", CALC_KEY_COS}, {"tan", CALC_KEY_TAN}, {"2nd", CALC_KEY_INVERSE},
    {"hyp", CALC_KEY_HYPERBOLIC}, {"angle", CALC_KEY_ANGLE}, {"fe", CALC_KEY_FORMAT}, {"pi", CALC_KEY_PI},
    {"e", CALC_KEY_EULER}, {"exp", CALC_KEY_EXPONENT}, {"back", CALC_KEY_BACK}, {"ce", CALC_KEY_CLEAR_ENTRY},
    {"c", CALC_KEY_CLEAR}, {"mc", CALC_KEY_MEMORY_CLEAR}, {"mr", CALC_KEY_MEMORY_RECALL},
    {"m+", CALC_KEY_MEMORY_ADD}, {"m-", CALC_KEY_MEMORY_SUBTRACT}, {"ms", CALC_KEY_MEMORY_STORE},
    {"and", CALC_KEY_AND}, {"or", CALC_KEY_OR}, {"xor", CALC_KEY_XOR}, {"not", CALC_KEY_NOT},
    {"lsh", CALC_KEY_SHIFT_LEFT}, {"rsh", CALC_KEY_SHIFT_RIGHT}, {"hex", CALC_KEY_HEX}, {"dec", CALC_KEY_DEC},
    {"oct", CALC_KEY_OCT}, {"bin", CALC_KEY_BIN}, {"word", CALC_KEY_WORD},
};

static void
Type(const char *Keys)
{
    char Token[64];

    while (*Keys != 0)
    {
        size_t Length = 0, Index;
        int Number = 1;

        while (*Keys == ' ')
            Keys++;
        while (*Keys != 0 && *Keys != ' ' && Length < sizeof(Token) - 1)
            Token[Length++] = *Keys++;
        Token[Length] = 0;
        if (Length == 0)
            break;
        for (Index = 0; Index < Length; Index++)
        {
            char Symbol = Token[Index];

            if (!((Symbol >= '0' && Symbol <= '9') || Symbol == '.' || (Symbol >= 'A' && Symbol <= 'F')))
                Number = 0;
        }
        if (Number)
        {
            for (Index = 0; Index < Length; Index++)
            {
                char Symbol = Token[Index];

                if (Symbol == '.')
                    CalcPress(&Engine, CALC_KEY_POINT);
                else if (Symbol >= 'A')
                    CalcPress(&Engine, (CALC_KEY)(CALC_KEY_A + (Symbol - 'A')));
                else
                    CalcPress(&Engine, (CALC_KEY)(CALC_KEY_0 + (Symbol - '0')));
            }
            continue;
        }
        for (Index = 0; Index < sizeof(KeyNames) / sizeof(KeyNames[0]); Index++)
        {
            if (strcmp(KeyNames[Index].Name, Token) == 0)
            {
                CalcPress(&Engine, KeyNames[Index].Key);
                break;
            }
        }
        if (Index == sizeof(KeyNames) / sizeof(KeyNames[0]))
        {
            printf("FAIL unknown key '%s'\n", Token);
            Failures++;
        }
    }
}

static void
Expect(int Line, CALC_MODE Mode, const char *Keys, const char *Display, CALC_STATUS Status)
{
    char Text[CALC_TEXT_LENGTH];

    CalcInit(&Engine);
    CalcSetMode(&Engine, Mode);
    Type(Keys);
    CalcDisplay(&Engine, Text, sizeof(Text));
    if (Engine.Error != Status || (Status == CALC_OK && strcmp(Text, Display) != 0))
    {
        printf("FAIL line %d: '%s' gave '%s' status %d, expected '%s' status %d\n", Line, Keys, Text,
               (int)Engine.Error, Display, (int)Status);
        Failures++;
    }
}

static void
ExpectExpression(int Line, CALC_MODE Mode, const char *Keys, const char *Expression)
{
    char Text[CALC_EXPRESSION_LENGTH];

    CalcInit(&Engine);
    CalcSetMode(&Engine, Mode);
    Type(Keys);
    CalcExpression(&Engine, Text, sizeof(Text));
    if (strcmp(Text, Expression) != 0)
    {
        printf("FAIL line %d: '%s' wrote '%s', expected '%s'\n", Line, Keys, Text, Expression);
        Failures++;
    }
}

static void
ExpectNumber(int Line, const char *Input, int Precision, int Exponential, const char *Expected)
{
    CALC_NUMBER Value;
    char Text[CALC_TEXT_LENGTH];

    if (NumFromString(Input, &Value) != CALC_OK)
    {
        printf("FAIL line %d: '%s' was not read\n", Line, Input);
        Failures++;
        return;
    }
    NumToString(&Value, Precision, Exponential, Text, sizeof(Text));
    if (strcmp(Text, Expected) != 0)
    {
        printf("FAIL line %d: '%s' printed '%s', expected '%s'\n", Line, Input, Text, Expected);
        Failures++;
    }
}

#define STD(keys, display) Expect(__LINE__, CALC_MODE_STANDARD, keys, display, CALC_OK)
#define SCI(keys, display) Expect(__LINE__, CALC_MODE_SCIENTIFIC, keys, display, CALC_OK)
#define PRG(keys, display) Expect(__LINE__, CALC_MODE_PROGRAMMER, keys, display, CALC_OK)
#define STD_ERROR(keys, status) Expect(__LINE__, CALC_MODE_STANDARD, keys, "", status)
#define SCI_ERROR(keys, status) Expect(__LINE__, CALC_MODE_SCIENTIFIC, keys, "", status)
#define PRG_ERROR(keys, status) Expect(__LINE__, CALC_MODE_PROGRAMMER, keys, "", status)
#define STD_TEXT(keys, text) ExpectExpression(__LINE__, CALC_MODE_STANDARD, keys, text)
#define SCI_TEXT(keys, text) ExpectExpression(__LINE__, CALC_MODE_SCIENTIFIC, keys, text)
#define NUMBER(input, precision, exponential, expected) ExpectNumber(__LINE__, input, precision, exponential, expected)

static void
TestStandard(void)
{
    STD("", "0");
    STD("2 + 3 =", "5");
    STD("2 + 3 * 4 =", "20");
    STD("2 + 3 = =", "8");
    STD("2 + 3 = 10 =", "13");
    STD("5 * =", "25");
    STD("2 + * 3 =", "6");
    STD("+ 5 =", "5");
    STD("0.1 + 0.2 =", "0.3");
    STD("1 / 3 =", "0.3333333333333333");
    STD("2 / 3 =", "0.6666666666666667");
    STD("1 / 3 * 3 =", "1");
    STD("1 / 3 * 3 - 1 =", "0");
    STD("10 / 3 =", "3.333333333333333");
    STD("1 / 30 =", "0.0333333333333333");
    STD("1 / 3000000 =", "3.333333333333333e-7");
    STD("9999999999999999 + 1 =", "1.e+16");
    STD("12345678901234567", "1234567890123456");
    STD("0.12345678901234567", "0.1234567890123456");
    STD("007", "7");
    STD("1.5.2", "1.52");
    STD("9 sqrt", "3");
    STD("2 sqrt", "1.414213562373095");
    STD("2 sqrt sqr - 2 =", "0");
    STD("4 inv", "0.25");
    STD("3 sqr", "9");
    STD("5 neg", "-5");
    STD("5 neg neg", "5");
    STD("5 = neg", "-5");
    STD("200 + 10 % =", "220");
    STD("200 - 10 % =", "180");
    STD("200 * 10 % =", "20");
    STD("200 / 10 % =", "2000");
    STD("50 %", "0");
    STD("12 back", "1");
    STD("1 back", "0");
    STD("1.5 back back", "1");
    STD("7 ce 8 + 1 =", "9");
    STD("2 + 3 ce 4 =", "6");
    STD("2 + 3 c", "0");
    STD("5 ms c mr", "5");
    STD("5 ms 3 m+ c mr", "8");
    STD("5 ms 3 m- c mr", "2");
    STD("5 ms mc mr 7", "7");
    STD("2 + 5 ms 3 mr =", "7");
    STD("1 / 0 = 4", "4");
    STD("2 * 3 = + 1 =", "7");
    STD_ERROR("1 / 0 =", CALC_DIVIDE_BY_ZERO);
    STD_ERROR("0 / 0 =", CALC_UNDEFINED);
    STD_ERROR("0 inv", CALC_DIVIDE_BY_ZERO);
    STD_ERROR("1 neg sqrt", CALC_INVALID_INPUT);
    STD_ERROR("1 / 0 = + sqrt =", CALC_DIVIDE_BY_ZERO);
    STD_TEXT("2 + 3", "2 + ");
    STD_TEXT("2 + 3 =", "2 + 3 =");
    STD_TEXT("2 + 3 * 4 =", "2 + 3 \xC3\x97 4 =");
    STD_TEXT("2 + * 3", "2 \xC3\x97 ");
    STD_TEXT("9 sqrt", "sqrt(9)");
    STD_TEXT("9 sqrt sqr + 1 =", "sqr(sqrt(9)) + 1 =");
    STD_TEXT("2 + 3 = =", "5 + 3 =");
    STD_TEXT("2 + 3 = 7", "");
    STD_TEXT("8 / 2 =", "8 \xC3\xB7 2 =");
}

static void
TestScientific(void)
{
    SCI("2 + 3 * 4 =", "14");
    SCI("2 * 3 + 4 =", "10");
    SCI("( 2 + 3 ) * 4 =", "20");
    SCI("2 * ( 3 + 4", "4");
    SCI("2 * ( 3 + 4 =", "14");
    SCI("( ( 1 + 2 ) * ( 3 + 4 ) ) =", "21");
    SCI("2 ^ 10 =", "1024");
    SCI("2 ^ 3 ^ 2 =", "64");
    SCI("2 + 2 ^ 3 * 2 =", "18");
    SCI("2 ^ 100 =", "1267650600228229401496703205376");
    SCI("2 ^ 0.5 =", "1.4142135623730950488016887242097");
    SCI("27 yroot 3 =", "3");
    SCI("8 neg yroot 3 =", "-2");
    SCI("16 yroot 4 =", "2");
    SCI("10 mod 3 =", "1");
    SCI("10 neg mod 3 =", "-1");
    SCI("5.5 mod 2 =", "1.5");
    SCI("1 / 3 =", "0.33333333333333333333333333333333");
    SCI("2 / 3 =", "0.66666666666666666666666666666667");
    SCI("pi", "3.1415926535897932384626433832795");
    SCI("e", "2.7182818284590452353602874713527");
    SCI("2 sqrt", "1.4142135623730950488016887242097");
    SCI("2 ln", "0.69314718055994530941723212145818");
    SCI("10 ln", "2.3025850929940456840179914546844");
    SCI("1 ln", "0");
    SCI("100 log", "2");
    SCI("0.001 log", "-3");
    SCI("2 log", "0.30102999566398119521373889472449");
    SCI("1 e^", "2.7182818284590452353602874713527");
    SCI("10 e^", "22026.465794806716516957900645284");
    SCI("3 10^", "1000");
    SCI("50 10^", "1.e+50");
    SCI("3 cube", "27");
    SCI("5 neg abs", "5");
    SCI("0 fact", "1");
    SCI("5 fact", "120");
    SCI("20 fact", "2432902008176640000");
    SCI("69 fact", "1.7112245242814131137246833888127e+98");
    SCI("30 sin", "0.5");
    SCI("60 cos", "0.5");
    SCI("45 tan", "1");
    SCI("180 sin", "0");
    SCI("90 cos", "0");
    SCI("360000030 sin", "0.5");
    SCI("1 sin", "0.01745240643728351281941897851632");
    SCI("angle 1 sin", "0.8414709848078965066525023216303");
    SCI("angle 1 cos", "0.54030230586813971740093660744298");
    SCI("angle 1 tan", "1.5574077246549022305069748074584");
    SCI("angle pi sin", "0");
    SCI("angle pi cos", "-1");
    SCI("angle angle 100 sin", "1");
    SCI("angle angle 50 tan", "1");
    SCI("2nd 0.5 sin", "30");
    SCI("2nd 1 sin", "90");
    SCI("2nd 0.5 cos", "60");
    SCI("2nd 1 tan", "45");
    SCI("2nd 1 cos", "0");
    SCI("angle 2nd 1 tan", "0.78539816339744830961566084581988");
    SCI("hyp 1 sin", "1.1752011936438014568823818505956");
    SCI("hyp 1 cos", "1.5430806348152437784779056207571");
    SCI("hyp 1 tan", "0.76159415595576488811945828260479");
    SCI("hyp 0 sin", "0");
    SCI("hyp 2nd 1 sin", "0.88137358701954302523260932497979");
    SCI("hyp 2nd 2 cos", "1.316957896924816708625046347308");
    SCI("hyp 2nd 0.5 tan", "0.54930614433405484569762261846126");
    SCI("2 exp 3 =", "2000");
    SCI("2 exp 3 neg =", "0.002");
    SCI("1.5 exp 10", "1.5e+10");
    SCI("1000 = fe", "1.e+3");
    SCI("1 exp 9999 * 10 = c 5", "5");
    SCI("2 sqrt sqr - 2 =", "0");
    SCI("3 sqrt sqr =", "3");
    SCI("pi - pi =", "0");
    SCI_ERROR("90 tan", CALC_INVALID_INPUT);
    SCI_ERROR("0 ln", CALC_INVALID_INPUT);
    SCI_ERROR("1 neg log", CALC_INVALID_INPUT);
    SCI_ERROR("2 2nd sin", CALC_INVALID_INPUT);
    SCI_ERROR("0.5 fact", CALC_INVALID_INPUT);
    SCI_ERROR("3 neg fact", CALC_INVALID_INPUT);
    SCI_ERROR("3249 fact", CALC_OVERFLOW);
    SCI_ERROR("1 exp 9999 * 10 =", CALC_OVERFLOW);
    SCI_ERROR("10000 10^", CALC_OVERFLOW);
    SCI_ERROR("8 neg yroot 2 =", CALC_INVALID_INPUT);
    SCI_ERROR("2 neg ^ 0.5 =", CALC_INVALID_INPUT);
    SCI_ERROR("0 ^ 1 neg =", CALC_DIVIDE_BY_ZERO);
    SCI_ERROR("5 mod 0 =", CALC_DIVIDE_BY_ZERO);
    SCI("3248 fact", "1.9736342530860425312047080034031e+9997");
    SCI("0 ^ 0 =", "1");
    SCI_TEXT("( 2 + 3 ) * 4 =", "(2 + 3) \xC3\x97 4 =");
    SCI_TEXT("2 * ( 3 + 4 =", "2 \xC3\x97 (3 + 4) =");
    SCI_TEXT("30 sin", "sin(30)");
    SCI_TEXT("2nd hyp 1 sin", "asinh(1)");
    SCI_TEXT("( 2 + 3 ) sqrt", "sqrt((2 + 3))");
    SCI_TEXT("2 ^ 3 =", "2 ^ 3 =");
}

static void
TestProgrammer(void)
{
    PRG("2 + 3 * 4 =", "14");
    PRG("7 / 2 =", "3");
    PRG("7 neg / 2 =", "-3");
    PRG("7 mod 3 =", "1");
    PRG("7 neg mod 3 =", "-1");
    PRG("5 and 3 =", "1");
    PRG("5 or 3 =", "7");
    PRG("5 xor 3 =", "6");
    PRG("1 or 2 and 3 =", "3");
    PRG("0 not", "-1");
    PRG("0 not hex", "FFFFFFFFFFFFFFFF");
    PRG("10 lsh 2 =", "40");
    PRG("1 lsh 64 =", "0");
    PRG("40 rsh 2 =", "10");
    PRG("1 neg rsh 1 =", "-1");
    PRG("hex FF + 1 =", "100");
    PRG("hex FF + 1 = dec", "256");
    PRG("255 hex", "FF");
    PRG("255 oct", "377");
    PRG("10 bin", "1010");
    PRG("bin 1010 dec", "10");
    PRG("bin 12", "1");
    PRG("oct 78", "7");
    PRG("9223372036854775807 + 1 =", "-9223372036854775808");
    PRG("9223372036854775808", "922337203685477580");
    PRG("hex FFFFFFFFFFFFFFFF dec", "-1");
    PRG("hex FFFFFFFFFFFFFFFFF", "FFFFFFFFFFFFFFFF");
    PRG("word hex FFFFFFFF dec", "-1");
    PRG("word 2147483647 + 1 =", "-2147483648");
    PRG("word word word 127 + 1 =", "-128");
    PRG("word word word 128", "12");
    PRG("word word word hex FF dec", "-1");
    PRG("hex 1FF word word word", "FF");
    PRG("word word word word 300", "300");
    PRG("1.5", "15");
    PRG("5 ms c mr + 1 =", "6");
    PRG("( 1 + 2 ) * 3 =", "9");
    PRG("12 back", "1");
    PRG_ERROR("1 / 0 =", CALC_DIVIDE_BY_ZERO);
    PRG_ERROR("1 mod 0 =", CALC_DIVIDE_BY_ZERO);
}

static void
TestModes(void)
{
    char Text[CALC_TEXT_LENGTH];

    CalcInit(&Engine);
    Type("2.75");
    CalcSetMode(&Engine, CALC_MODE_PROGRAMMER);
    CalcDisplay(&Engine, Text, sizeof(Text));
    if (strcmp(Text, "2") != 0)
    {
        printf("FAIL programmer mode kept '%s'\n", Text);
        Failures++;
    }
    Type("hex");
    CalcDisplayBase(&Engine, 2, Text, sizeof(Text));
    if (strcmp(Text, "10") != 0)
    {
        printf("FAIL binary view gave '%s'\n", Text);
        Failures++;
    }
    CalcSetMode(&Engine, CALC_MODE_STANDARD);
    Type("+ 1 =");
    CalcDisplay(&Engine, Text, sizeof(Text));
    if (strcmp(Text, "3") != 0)
    {
        printf("FAIL standard mode after programmer gave '%s'\n", Text);
        Failures++;
    }
    CalcInit(&Engine);
    if (!CalcPaste(&Engine, "-12.5e2") || CalcPaste(&Engine, "12x"))
    {
        printf("FAIL paste accepted or refused the wrong text\n");
        Failures++;
    }
    CalcDisplay(&Engine, Text, sizeof(Text));
    if (strcmp(Text, "-1250") != 0)
    {
        printf("FAIL paste gave '%s'\n", Text);
        Failures++;
    }
    CalcSetMode(&Engine, CALC_MODE_PROGRAMMER);
    Type("hex");
    if (!CalcPaste(&Engine, "ff") || CalcPaste(&Engine, "fg") || !CalcKeyEnabled(&Engine, CALC_KEY_F))
    {
        printf("FAIL hexadecimal paste\n");
        Failures++;
    }
    Type("oct");
    if (CalcKeyEnabled(&Engine, CALC_KEY_8) || !CalcKeyEnabled(&Engine, CALC_KEY_7) ||
        CalcKeyEnabled(&Engine, CALC_KEY_POINT))
    {
        printf("FAIL octal key set\n");
        Failures++;
    }
}

static void
TestNumbers(void)
{
    NUMBER("0", 16, 0, "0");
    NUMBER("-0", 16, 0, "0");
    NUMBER("123.456", 16, 0, "123.456");
    NUMBER("-123.456", 16, 0, "-123.456");
    NUMBER("0.0001", 16, 0, "0.0001");
    NUMBER("1e15", 16, 0, "1000000000000000");
    NUMBER("1e16", 16, 0, "1.e+16");
    NUMBER("12345678901234567", 16, 0, "1.234567890123457e+16");
    NUMBER("0.099999999999999999999", 16, 0, "0.1");
    NUMBER("0.00033333333333333333", 16, 0, "3.333333333333333e-4");
    NUMBER("1e-21", 16, 0, "1.e-21");
    NUMBER("1e9999", 16, 0, "1.e+9999");
    NUMBER("1234.5", 16, 1, "1.2345e+3");
    NUMBER("0.5", 32, 1, "5.e-1");
    NUMBER("99999999999999999999999999999999.5", 32, 0, "1.e+32");
    NUMBER(".5", 16, 0, "0.5");
    NUMBER("5.", 16, 0, "5");
    NUMBER("1e-10001", 16, 0, "1.e-10001");
    NUMBER("1e-10002", 16, 0, "0");
}

int
main(void)
{
    TestNumbers();
    TestStandard();
    TestScientific();
    TestProgrammer();
    TestModes();
    if (Failures)
    {
        printf("%d check(s) failed\n", Failures);
        return 1;
    }
    printf("calc.engine: all checks passed\n");
    return 0;
}

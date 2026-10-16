
#define STANDALONE
#include <apitest.h>

extern void func_PropertyDescriptionList(void);
extern void func_PropertySchemaDump(void);
extern void func_PSPropertyBag(void);

const struct test winetest_testlist[] =
{
    { "PropertyDescriptionList", func_PropertyDescriptionList },
    { "PropertySchemaDump", func_PropertySchemaDump },
    { "PSPropertyBag", func_PSPropertyBag },
    { 0, 0 }
};

#define STANDALONE
#include <apitest.h>

extern void func_BitmapSourceEffect(void);
extern void func_GeometryOps(void);
extern void func_Layer(void);
extern void func_Path(void);
extern void func_PrimitiveBlend(void);

const struct test winetest_testlist[] =
{
    { "BitmapSourceEffect", func_BitmapSourceEffect },
    { "GeometryOps", func_GeometryOps },
    { "Layer", func_Layer },
    { "Path", func_Path },
    { "PrimitiveBlend", func_PrimitiveBlend },

    { 0, 0 }
};

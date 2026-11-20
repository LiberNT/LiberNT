#define STANDALONE
#include <apitest.h>

extern void func_BitmapSourceEffect(void);
extern void func_FillMesh(void);
extern void func_GeometryOps(void);
extern void func_Layer(void);
extern void func_Path(void);
extern void func_PrimitiveBlend(void);
extern void func_SvgDocument(void);

const struct test winetest_testlist[] =
{
    { "BitmapSourceEffect", func_BitmapSourceEffect },
    { "FillMesh", func_FillMesh },
    { "GeometryOps", func_GeometryOps },
    { "Layer", func_Layer },
    { "Path", func_Path },
    { "PrimitiveBlend", func_PrimitiveBlend },
    { "SvgDocument", func_SvgDocument },

    { 0, 0 }
};

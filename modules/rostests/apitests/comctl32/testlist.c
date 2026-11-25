#define STANDALONE
#include <apitest.h>

extern void func_buttonv6(void);
extern void func_ClassModule(void);
extern void func_EditImeContext(void);
extern void func_ImageListApi(void);
extern void func_ListViewActivate(void);
extern void func_ListViewGroups(void);
extern void func_ListViewSnapToGrid(void);
extern void func_ListViewWorkAreas(void);
extern void func_LoadIconWithScaleDown(void);
extern void func_OwnerDrawButtonCues(void);
extern void func_propsheetv6(void);
extern void func_PropSheetRecalc(void);
extern void func_TabControlKeys(void);
extern void func_StatusBarResize(void);
extern void func_SysLinkIdealSize(void);
extern void func_TaskDialogTitle(void);
extern void func_themestate(void);
extern void func_toolbarv6(void);
extern void func_ToolbarExtras(void);
extern void func_tooltipv6(void);

const struct test winetest_testlist[] =
{
    { "buttonv6", func_buttonv6 },
    { "ClassModule", func_ClassModule },
    { "EditImeContext", func_EditImeContext },
    { "ImageListApi", func_ImageListApi },
    { "ListViewActivate", func_ListViewActivate },
    { "ListViewGroups", func_ListViewGroups },
    { "ListViewSnapToGrid", func_ListViewSnapToGrid },
    { "ListViewWorkAreas", func_ListViewWorkAreas },
    { "LoadIconWithScaleDown", func_LoadIconWithScaleDown },
    { "OwnerDrawButtonCues", func_OwnerDrawButtonCues },
    { "propsheetv6", func_propsheetv6 },
    { "PropSheetRecalc", func_PropSheetRecalc },
    { "TabControlKeys", func_TabControlKeys },
    { "StatusBarResize", func_StatusBarResize },
    { "SysLinkIdealSize", func_SysLinkIdealSize },
    { "TaskDialogTitle", func_TaskDialogTitle },
    { "themestate", func_themestate },
    { "toolbarv6", func_toolbarv6 },
    { "ToolbarExtras", func_ToolbarExtras },
    { "tooltipv6", func_tooltipv6 },
    { 0, 0 }
};

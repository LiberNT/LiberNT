#define STANDALONE
#include <apitest.h>

extern void func_basicui(void);
extern void func_exeaction(void);
extern void func_filesource(void);
extern void func_installend(void);
extern void func_msgbox(void);
extern void func_protectedfile(void);
extern void func_reboot(void);
extern void func_uiphase(void);

const struct test winetest_testlist[] =
{
    { "basicui", func_basicui },
    { "exeaction", func_exeaction },
    { "filesource", func_filesource },
    { "installend", func_installend },
    { "msgbox", func_msgbox },
    { "protectedfile", func_protectedfile },
    { "reboot", func_reboot },
    { "uiphase", func_uiphase },
    { 0, 0 }
};

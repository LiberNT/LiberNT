#define STANDALONE
#include <apitest.h>

extern void func_attrib(void);
extern void func_cd(void);
extern void func_confirm(void);
extern void func_dir(void);
extern void func_echo(void);
extern void func_exit(void);
extern void func_fc(void);
extern void func_pushd(void);
extern void func_script(void);
extern void func_tree(void);
extern void func_where(void);

const struct test winetest_testlist[] =
{
    { "attrib", func_attrib },
    { "cd", func_cd },
    { "confirm", func_confirm },
    { "dir", func_dir },
    { "echo", func_echo },
    { "exit", func_exit },
    { "fc", func_fc },
    { "pushd", func_pushd },
    { "script", func_script },
    { "tree", func_tree },
    { "where", func_where },
    { 0, 0 }
};

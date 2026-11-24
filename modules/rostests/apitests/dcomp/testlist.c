#define STANDALONE
#include <apitest.h>

extern void func_composition_modechange(void);
extern void func_composition_present(void);
extern void func_noredirection_style(void);
extern void func_noredirection_transparency(void);
extern void func_target_lifetime(void);
extern void func_target_commit(void);
extern void func_visual_reset(void);

const struct test winetest_testlist[] =
{
    { "composition_modechange", func_composition_modechange },
    { "composition_present", func_composition_present },
    { "noredirection_style", func_noredirection_style },
    { "noredirection_transparency", func_noredirection_transparency },
    { "target_lifetime", func_target_lifetime },
    { "target_commit", func_target_commit },
    { "visual_reset", func_visual_reset },
    { 0, 0 }
};

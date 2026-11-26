#define STANDALONE
#include <apitest.h>

extern void func_category(void);
extern void func_composition(void);
extern void func_editsession(void);
extern void func_inputmode(void);
extern void func_keyroute(void);
extern void func_langbar(void);
extern void func_langbarmgr(void);
extern void func_layouttip(void);
extern void func_mtaproxy(void);
extern void func_owner(void);
extern void func_profilemgr(void);
extern void func_profiles(void);
extern void func_property(void);
extern void func_range(void);
extern void func_textedit(void);
extern void func_threadfocus(void);
extern void func_threadmgr(void);
extern void func_tipthread(void);
extern void func_trackprop(void);

const struct test winetest_testlist[] =
{
    { "category", func_category },
    { "composition", func_composition },
    { "editsession", func_editsession },
    { "inputmode", func_inputmode },
    { "keyroute", func_keyroute },
    { "langbar", func_langbar },
    { "langbarmgr", func_langbarmgr },
    { "layouttip", func_layouttip },
    { "mtaproxy", func_mtaproxy },
    { "owner", func_owner },
    { "profilemgr", func_profilemgr },
    { "profiles", func_profiles },
    { "property", func_property },
    { "range", func_range },
    { "textedit", func_textedit },
    { "threadfocus", func_threadfocus },
    { "threadmgr", func_threadmgr },
    { "tipthread", func_tipthread },
    { "trackprop", func_trackprop },
    { 0, 0 }
};

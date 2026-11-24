#define STANDALONE
#include <apitest.h>

extern void func_agreements(void);
extern void func_dependency(void);
extern void func_dependencyagreements(void);
extern void func_exe(void);
extern void func_gui(void);
extern void func_guiagreements(void);
extern void func_guiuninstall(void);
extern void func_guiupdate(void);
extern void func_inno(void);
extern void func_nullsoft(void);
extern void func_nullsoftdirectory(void);
extern void func_options(void);
extern void func_portable(void);
extern void func_portablefiles(void);
extern void func_upgrade(void);
extern void func_upgradeall(void);
extern void func_wix(void);

const struct test winetest_testlist[] =
{
    { "agreements", func_agreements },
    { "dependency", func_dependency },
    { "dependencyagreements", func_dependencyagreements },
    { "exe", func_exe },
    { "gui", func_gui },
    { "guiagreements", func_guiagreements },
    { "guiuninstall", func_guiuninstall },
    { "guiupdate", func_guiupdate },
    { "inno", func_inno },
    { "nullsoft", func_nullsoft },
    { "nullsoftdirectory", func_nullsoftdirectory },
    { "options", func_options },
    { "portable", func_portable },
    { "portablefiles", func_portablefiles },
    { "upgrade", func_upgrade },
    { "upgradeall", func_upgradeall },
    { "wix", func_wix },
    { 0, 0 }
};

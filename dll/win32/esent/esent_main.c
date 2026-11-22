/*
 * PROJECT:     LiberNT Extensible Storage Engine
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Entry points of the database engine that report it as unavailable
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <windef.h>
#include <winbase.h>
#include <wine/debug.h>

WINE_DEFAULT_DEBUG_CHANNEL(esent);

typedef long JET_ERR;
typedef ULONG_PTR JET_API_PTR;
typedef JET_API_PTR JET_INSTANCE;
typedef JET_API_PTR JET_SESID;
typedef JET_API_PTR JET_TABLEID;
typedef unsigned long JET_COLUMNID;
typedef unsigned long JET_DBID;
typedef unsigned long JET_GRBIT;
typedef const WCHAR *JET_PCWSTR;

#define JET_errFeatureNotAvailable (-1001)

/* TODO: no database engine; every entry point reports JET_errFeatureNotAvailable */

JET_ERR WINAPI JetCreateInstanceW(JET_INSTANCE *pinstance, JET_PCWSTR szInstanceName)
{
    FIXME("(%p, %s): database engine not available\n", pinstance, debugstr_w(szInstanceName));
    return JET_errFeatureNotAvailable;
}

JET_ERR WINAPI JetSetSystemParameterW(JET_INSTANCE *pinstance, JET_SESID sesid, unsigned long paramid,
                                      JET_API_PTR lParam, JET_PCWSTR szParam)
{
    FIXME("(%p, %Ix, %lu, %Ix, %s): database engine not available\n", pinstance, sesid, paramid, lParam,
          debugstr_w(szParam));
    return JET_errFeatureNotAvailable;
}

JET_ERR WINAPI JetInit(JET_INSTANCE *pinstance)
{
    FIXME("(%p): database engine not available\n", pinstance);
    return JET_errFeatureNotAvailable;
}

JET_ERR WINAPI JetTerm(JET_INSTANCE instance)
{
    FIXME("(%Ix): database engine not available\n", instance);
    return JET_errFeatureNotAvailable;
}

JET_ERR WINAPI JetBeginSessionW(JET_INSTANCE instance, JET_SESID *psesid, JET_PCWSTR szUserName,
                                JET_PCWSTR szPassword)
{
    FIXME("(%Ix, %p): database engine not available\n", instance, psesid);
    return JET_errFeatureNotAvailable;
}

JET_ERR WINAPI JetAttachDatabase2W(JET_SESID sesid, JET_PCWSTR szFilename, const unsigned long cpgDatabaseSizeMax,
                                   JET_GRBIT grbit)
{
    FIXME("(%Ix, %s, %lu, %lx): database engine not available\n", sesid, debugstr_w(szFilename),
          cpgDatabaseSizeMax, grbit);
    return JET_errFeatureNotAvailable;
}

JET_ERR WINAPI JetOpenDatabaseW(JET_SESID sesid, JET_PCWSTR szFilename, JET_PCWSTR szConnect, JET_DBID *pdbid,
                                JET_GRBIT grbit)
{
    FIXME("(%Ix, %s, %p, %lx): database engine not available\n", sesid, debugstr_w(szFilename), pdbid, grbit);
    return JET_errFeatureNotAvailable;
}

JET_ERR WINAPI JetOpenTableW(JET_SESID sesid, JET_DBID dbid, JET_PCWSTR szTableName, const void *pvParameters,
                             unsigned long cbParameters, JET_GRBIT grbit, JET_TABLEID *ptableid)
{
    FIXME("(%Ix, %lu, %s, %lx, %p): database engine not available\n", sesid, dbid, debugstr_w(szTableName), grbit,
          ptableid);
    return JET_errFeatureNotAvailable;
}

JET_ERR WINAPI JetCloseTable(JET_SESID sesid, JET_TABLEID tableid)
{
    FIXME("(%Ix, %Ix): database engine not available\n", sesid, tableid);
    return JET_errFeatureNotAvailable;
}

JET_ERR WINAPI JetGetTableColumnInfoW(JET_SESID sesid, JET_TABLEID tableid, JET_PCWSTR szColumnName, void *pvResult,
                                      unsigned long cbMax, unsigned long InfoLevel)
{
    FIXME("(%Ix, %Ix, %s, %lu): database engine not available\n", sesid, tableid, debugstr_w(szColumnName),
          InfoLevel);
    return JET_errFeatureNotAvailable;
}

JET_ERR WINAPI JetMove(JET_SESID sesid, JET_TABLEID tableid, long cRow, JET_GRBIT grbit)
{
    FIXME("(%Ix, %Ix, %ld, %lx): database engine not available\n", sesid, tableid, cRow, grbit);
    return JET_errFeatureNotAvailable;
}

JET_ERR WINAPI JetRetrieveColumn(JET_SESID sesid, JET_TABLEID tableid, JET_COLUMNID columnid, void *pvData,
                                 unsigned long cbData, unsigned long *pcbActual, JET_GRBIT grbit, void *pretinfo)
{
    FIXME("(%Ix, %Ix, %lu): database engine not available\n", sesid, tableid, columnid);
    return JET_errFeatureNotAvailable;
}

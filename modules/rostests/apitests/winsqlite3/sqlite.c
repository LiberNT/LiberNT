/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests the SQLite API exported by winsqlite3.dll
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#include <apitest.h>
#include <string.h>
#include <sqlite3.h>

static int
ReverseCompare(void *Context, int Length1, const void *Data1, int Length2, const void *Data2)
{
    int Length = Length1 < Length2 ? Length1 : Length2;
    int Result = memcmp(Data1, Data2, Length);

    if (!Result)
        Result = Length1 - Length2;
    return -Result;
}

static int CollationDestroyed;

static void
DestroyCollation(void *Context)
{
    CollationDestroyed++;
}

static void
DoubleFunction(sqlite3_context *Context, int Argc, sqlite3_value **Argv)
{
    sqlite3_result_int64(Context, sqlite3_value_int64(Argv[0]) * 2);
}

static sqlite3_int64
QueryInt64(sqlite3 *Db, const char *Sql)
{
    sqlite3_stmt *Stmt = NULL;
    sqlite3_int64 Value = -1;
    int rc = sqlite3_prepare_v2(Db, Sql, -1, &Stmt, NULL);

    ok(rc == SQLITE_OK, "prepare(%s) returned %d: %s\n", Sql, rc, sqlite3_errmsg(Db));
    if (rc != SQLITE_OK)
        return -1;
    rc = sqlite3_step(Stmt);
    ok(rc == SQLITE_ROW, "step(%s) returned %d\n", Sql, rc);
    if (rc == SQLITE_ROW)
        Value = sqlite3_column_int64(Stmt, 0);
    ok(sqlite3_step(Stmt) == SQLITE_DONE, "%s returned more than one row\n", Sql);
    ok(sqlite3_finalize(Stmt) == SQLITE_OK, "finalize(%s) failed\n", Sql);
    return Value;
}

START_TEST(sqlite)
{
    sqlite3 *Db = NULL, *Copy = NULL;
    sqlite3_stmt *Stmt = NULL;
    sqlite3_backup *Backup;
    const unsigned char *Text;
    const char *Tail = NULL;
    int rc;

    ok(sqlite3_libversion_number() / 1000000 == 3, "version %d\n", sqlite3_libversion_number());
    ok(sqlite3_libversion() != NULL && !strncmp(sqlite3_libversion(), "3.", 2), "libversion %s\n", sqlite3_libversion());
    ok(sqlite3_threadsafe() != 0, "winsqlite3 is not thread safe\n");

    rc = sqlite3_open_v2(":memory:", &Db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL);
    ok(rc == SQLITE_OK, "open_v2 returned %d\n", rc);
    if (rc != SQLITE_OK)
        return;
    ok(sqlite3_extended_result_codes(Db, 1) == SQLITE_OK, "extended_result_codes failed\n");
    ok(sqlite3_busy_timeout(Db, 1000) == SQLITE_OK, "busy_timeout failed\n");

    rc = sqlite3_exec(Db, "CREATE TABLE t(id INTEGER PRIMARY KEY, name TEXT, size INTEGER, data BLOB)", NULL, NULL, NULL);
    ok(rc == SQLITE_OK, "CREATE TABLE returned %d: %s\n", rc, sqlite3_errmsg(Db));

    rc = sqlite3_prepare_v2(Db, "INSERT INTO t(name, size, data) VALUES(?1, ?2, ?3)", -1, &Stmt, &Tail);
    ok(rc == SQLITE_OK, "prepare INSERT returned %d\n", rc);
    ok(Tail && !*Tail, "INSERT left a tail\n");
    if (rc == SQLITE_OK)
    {
        ok(sqlite3_bind_text64(Stmt, 1, "beta", 4, SQLITE_TRANSIENT, SQLITE_UTF8) == SQLITE_OK, "bind_text64 failed\n");
        ok(sqlite3_bind_int64(Stmt, 2, 0x123456789aLL) == SQLITE_OK, "bind_int64 failed\n");
        ok(sqlite3_bind_blob64(Stmt, 3, "\x01\x02\x03", 3, SQLITE_STATIC) == SQLITE_OK, "bind_blob64 failed\n");
        ok(sqlite3_step(Stmt) == SQLITE_DONE, "INSERT step failed: %s\n", sqlite3_errmsg(Db));
        ok(sqlite3_changes(Db) == 1, "changes %d\n", sqlite3_changes(Db));
        ok(sqlite3_last_insert_rowid(Db) == 1, "last_insert_rowid %I64d\n", sqlite3_last_insert_rowid(Db));
        ok(sqlite3_reset(Stmt) == SQLITE_OK, "reset failed\n");
        ok(sqlite3_bind_text64(Stmt, 1, "alpha", 5, SQLITE_TRANSIENT, SQLITE_UTF8) == SQLITE_OK, "bind_text64 failed\n");
        ok(sqlite3_bind_int(Stmt, 2, 7) == SQLITE_OK, "bind_int failed\n");
        ok(sqlite3_bind_null(Stmt, 3) == SQLITE_OK, "bind_null failed\n");
        ok(sqlite3_step(Stmt) == SQLITE_DONE, "second INSERT step failed\n");
        ok(sqlite3_finalize(Stmt) == SQLITE_OK, "finalize failed\n");
    }

    rc = sqlite3_prepare_v2(Db, "SELECT name, size, data FROM t WHERE id = 1", -1, &Stmt, NULL);
    ok(rc == SQLITE_OK, "prepare SELECT returned %d\n", rc);
    if (rc == SQLITE_OK)
    {
        ok(sqlite3_step(Stmt) == SQLITE_ROW, "SELECT step failed\n");
        Text = sqlite3_column_text(Stmt, 0);
        ok(Text && !strcmp((const char *)Text, "beta"), "name %s\n", Text);
        ok(sqlite3_column_bytes(Stmt, 0) == 4, "name bytes %d\n", sqlite3_column_bytes(Stmt, 0));
        ok(sqlite3_column_int64(Stmt, 1) == 0x123456789aLL, "size %I64x\n", sqlite3_column_int64(Stmt, 1));
        ok(sqlite3_column_type(Stmt, 2) == SQLITE_BLOB, "data type %d\n", sqlite3_column_type(Stmt, 2));
        ok(sqlite3_column_bytes(Stmt, 2) == 3 && !memcmp(sqlite3_column_blob(Stmt, 2), "\x01\x02\x03", 3), "blob mismatch\n");
        ok(sqlite3_column_table_name(Stmt, 0) && !strcmp(sqlite3_column_table_name(Stmt, 0), "t"),
           "column_table_name %s\n", sqlite3_column_table_name(Stmt, 0));
        ok(sqlite3_column_origin_name(Stmt, 1) && !strcmp(sqlite3_column_origin_name(Stmt, 1), "size"),
           "column_origin_name %s\n", sqlite3_column_origin_name(Stmt, 1));
        ok(sqlite3_db_handle(Stmt) == Db, "db_handle mismatch\n");
        ok(sqlite3_finalize(Stmt) == SQLITE_OK, "finalize failed\n");
    }

    rc = sqlite3_create_collation_v2(Db, "reverse", SQLITE_UTF8, NULL, ReverseCompare, DestroyCollation);
    ok(rc == SQLITE_OK, "create_collation_v2 returned %d\n", rc);
    ok(QueryInt64(Db, "SELECT id FROM t ORDER BY name COLLATE reverse LIMIT 1") == 1, "reverse collation order\n");
    ok(QueryInt64(Db, "SELECT id FROM t ORDER BY name LIMIT 1") == 2, "binary collation order\n");

    rc = sqlite3_create_function(Db, "double_it", 1, SQLITE_UTF8 | SQLITE_DETERMINISTIC, NULL, DoubleFunction, NULL, NULL);
    ok(rc == SQLITE_OK, "create_function returned %d\n", rc);
    ok(QueryInt64(Db, "SELECT double_it(size) FROM t WHERE id = 2") == 14, "double_it(7)\n");

    rc = sqlite3_prepare_v2(Db, "SELEC 1", -1, &Stmt, NULL);
    ok(rc == SQLITE_ERROR, "bad SQL returned %d\n", rc);
    ok(Stmt == NULL, "bad SQL produced a statement\n");
    ok(sqlite3_errcode(Db) == SQLITE_ERROR, "errcode %d\n", sqlite3_errcode(Db));
    ok(sqlite3_errmsg(Db) && *sqlite3_errmsg(Db), "empty errmsg\n");
    ok(sqlite3_errstr(SQLITE_CONSTRAINT) && *sqlite3_errstr(SQLITE_CONSTRAINT), "empty errstr\n");

    rc = sqlite3_open_v2(":memory:", &Copy, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL);
    ok(rc == SQLITE_OK, "second open_v2 returned %d\n", rc);
    if (rc == SQLITE_OK)
    {
        Backup = sqlite3_backup_init(Copy, "main", Db, "main");
        ok(Backup != NULL, "backup_init failed: %s\n", sqlite3_errmsg(Copy));
        if (Backup)
        {
            ok(sqlite3_backup_step(Backup, -1) == SQLITE_DONE, "backup_step did not finish\n");
            ok(sqlite3_backup_finish(Backup) == SQLITE_OK, "backup_finish failed\n");
        }
        ok(QueryInt64(Copy, "SELECT count(*) FROM t") == 2, "backup row count\n");
        ok(sqlite3_close_v2(Copy) == SQLITE_OK, "close_v2 of the copy failed\n");
    }

    ok(CollationDestroyed == 0, "collation destroyed %d times before close\n", CollationDestroyed);
    ok(sqlite3_close_v2(Db) == SQLITE_OK, "close_v2 failed\n");
    ok(CollationDestroyed == 1, "collation destroyed %d times after close\n", CollationDestroyed);
}

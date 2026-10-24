/*
 * PROJECT:     ReactOS package manager
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Read-only WinGet SQLite v2 package index reader
 * COPYRIGHT:   Copyright 2026 Ahmed Arif
 */

#pragma once

#include "model.hpp"

namespace lntget
{

enum class SearchField
{
    Any,
    Id,
    Name,
    Moniker,
    Tag,
    Command,
};

struct PackageQuery
{
    std::string text;
    SearchField field = SearchField::Any;
    bool exact = false;
    std::size_t count = 20;
};

class SQLiteIndex
{
public:
    Status Open(const std::wstring &path);
    Status Search(const PackageQuery &query, std::vector<PackageRecord> &results) const;
    Status FindExactId(const std::string &id, PackageRecord &package) const;
    Status LoadCatalog(Catalog &catalog) const;
    void Swap(SQLiteIndex &other);

private:
    struct Value;

    Status FindRoots();
    Status ValidateSchema();
    uint32_t Root(const std::string &table) const;
    template <typename RowCallback>
    Status ScanTable(uint32_t rootPage, const RowCallback &callback) const;
    template <typename RowCallback>
    Status ScanTablePage(uint32_t pageNumber, const RowCallback &callback, std::vector<bool> &visited) const;
    template <typename RowCallback>
    Status ScanIndex(uint32_t rootPage, const RowCallback &callback) const;
    template <typename RowCallback>
    Status ScanIndexPage(uint32_t pageNumber, const RowCallback &callback, std::vector<bool> &visited) const;
    Status ParseRecord(const uint8_t *payload, std::size_t payloadSize, std::vector<Value> &values) const;
    Status ReadPayload(const uint8_t *page, std::size_t cursor, uint64_t payloadSize,
                       uint64_t localSize, std::vector<uint8_t> &payload) const;
    template <typename PackageCallback>
    Status ForEachPackage(const PackageCallback &callback) const;
    const uint8_t *Page(uint32_t pageNumber) const;

    std::vector<uint8_t> data_;
    std::map<std::string, uint32_t> roots_;
    std::map<std::string, std::string> schemas_;
    uint32_t pageSize_ = 0;
    uint32_t usableSize_ = 0;
    uint32_t pageCount_ = 0;
};

Status RunSQLiteIndexSelfTests();

} // namespace lntget

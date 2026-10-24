/*
 * PROJECT:     ReactOS package manager
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Maps WinGet tags onto browsable catalog categories
 * COPYRIGHT:   Copyright 2026 Ahmed Arif
 */

#pragma once

#include "model.hpp"

namespace lntget
{

struct CategoryDefinition
{
    const wchar_t *name;
    COLORREF accent;
    const char *tags;
    const char *roots;
};

const CategoryDefinition *CategoryTable();
std::size_t CategoryCount();
std::size_t OtherCategoryIndex();

class CategoryClassifier
{
public:
    CategoryClassifier();
    uint32_t Classify(const std::vector<uint32_t> &tags, const std::vector<std::string> &loweredNames) const;
    uint32_t ClassifyText(const std::string &loweredText) const;

private:
    std::map<std::string, uint32_t> exact_;
    std::vector<std::pair<std::string, uint32_t>> roots_;
};

} // namespace lntget

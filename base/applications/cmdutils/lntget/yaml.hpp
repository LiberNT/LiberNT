/*
 * PROJECT:     LiberNT package manager
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     YAML reader for WinGet manifests and version data
 * COPYRIGHT:   Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#pragma once

#include "model.hpp"

#include <utility>

namespace lntget
{

struct YamlNode
{
    enum class Kind
    {
        Null,
        Scalar,
        Sequence,
        Mapping,
    };

    Kind kind = Kind::Null;
    std::string scalar;
    std::vector<YamlNode> items;
    std::vector<std::pair<std::string, YamlNode>> members;

    bool IsNull() const { return kind == Kind::Null; }
    bool IsScalar() const { return kind == Kind::Scalar; }
    bool IsSequence() const { return kind == Kind::Sequence; }
    bool IsMapping() const { return kind == Kind::Mapping; }
    const YamlNode *Find(const std::string &key) const;
    std::string Text() const { return kind == Kind::Scalar ? scalar : std::string(); }
};

Status ParseYaml(const std::string &text, YamlNode &root);
Status RunYamlSelfTests();

} // namespace lntget

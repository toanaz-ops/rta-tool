// SPDX-License-Identifier: AGPL-3.0-or-later
// Test-only. Reads the vectors written by tools/gen_golden.py.
#pragma once

#include <cstddef>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace rta::test {

struct GoldenCase {
    std::string name;
    std::size_t size = 0;
    std::unordered_map<std::string, std::vector<double>> rows;

    [[nodiscard]] bool hasRow(const std::string& key) const {
        return rows.find(key) != rows.end();
    }

    [[nodiscard]] const std::vector<double>& row(const std::string& key) const {
        const auto it = rows.find(key);
        if (it == rows.end()) {
            throw std::runtime_error("golden case '" + name + "' has no row '" + key + "'");
        }
        return it->second;
    }

    /// The generator writes float32 inputs widened to float64, which is exact,
    /// so narrowing them back here recovers the identical samples. The cast is
    /// spelled out rather than left to the iterator-pair constructor: an
    /// implicit double-to-float narrowing is exactly the kind of thing that
    /// should be visible in a file whose job is to say what "correct" means.
    [[nodiscard]] std::vector<float> floatRow(const std::string& key) const {
        const auto& values = row(key);
        std::vector<float> narrowed;
        narrowed.reserve(values.size());
        for (const double v : values) narrowed.push_back(static_cast<float>(v));
        return narrowed;
    }
};

/// Parses the line format documented in tools/gen_golden.py. Throws rather than
/// returning empty on anything unexpected: a silently empty vector set would
/// make every assertion below it vacuously pass.
inline std::vector<GoldenCase> loadGolden(const std::string& path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("cannot open golden file: " + path);
    }

    std::vector<GoldenCase> cases;
    std::string line;

    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;

        std::istringstream tokens(line);
        std::string key;
        tokens >> key;

        if (key == "case") {
            cases.emplace_back();
            tokens >> cases.back().name;
        } else if (key == "end") {
            continue;
        } else if (cases.empty()) {
            throw std::runtime_error("golden row '" + key + "' before any case");
        } else if (key == "size") {
            tokens >> cases.back().size;
        } else {
            std::vector<double> values;
            for (double v; tokens >> v;) values.push_back(v);

            // Stopping early on a bad token would hand back a short row, and a
            // short row indexed to its expected length reads off the end of the
            // vector -- undefined behaviour in a release build, which is where
            // the tests actually run. Fail loudly here instead.
            if (!tokens.eof()) {
                throw std::runtime_error("golden row '" + key + "' in case '" +
                                         cases.back().name + "' has an unparseable token");
            }
            cases.back().rows[key] = std::move(values);
        }
    }

    if (cases.empty()) {
        throw std::runtime_error("golden file parsed to zero cases: " + path);
    }
    return cases;
}

}  // namespace rta::test

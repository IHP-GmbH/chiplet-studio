/**
 * GDSAnalyzer.cpp - Implementation
 */

#include "GDSAnalyzer.h"
#include <iostream>
#include <algorithm>
#include <set>

#ifdef HAVE_KLAYOUT
#include "dbLayout.h"
#include "dbReader.h"
#include "dbCell.h"
#include "tlStream.h"
#include "tlException.h"
#endif

namespace chiplet {

#ifdef HAVE_KLAYOUT

std::vector<GDSCellInfo> GDSAnalyzer::analyzeCells(const std::string& gdsPath)
{
    std::vector<GDSCellInfo> result;
    m_lastError.clear();

    try {
        tl::InputStream stream(gdsPath);
        db::Reader reader(stream);

        db::Layout layout;
        reader.read(layout);

        double dbu = layout.dbu();

        // First pass: count how many times each cell is instantiated
        std::map<db::cell_index_type, int> instanceCount;

        // Initialize all cells with 0 instances
        for (auto it = layout.begin(); it != layout.end(); ++it) {
            instanceCount[it->cell_index()] = 0;
        }

        // Count instances
        for (auto it = layout.begin(); it != layout.end(); ++it) {
            const db::Cell& cell = *it;
            // Iterate through all instances in this cell
            for (auto inst_it = cell.begin(); !inst_it.at_end(); ++inst_it) {
                db::cell_index_type child_index = inst_it->cell_index();
                instanceCount[child_index]++;
            }
        }

        // Second pass: collect info for all cells
        for (auto it = layout.begin(); it != layout.end(); ++it) {
            const db::Cell& cell = *it;

            GDSCellInfo info;
            info.name = layout.cell_name(cell.cell_index());
            info.instanceCount = instanceCount[cell.cell_index()];
            info.isTopCandidate = (info.instanceCount == 0);

            // Count shapes (approximate)
            info.shapeCount = 0;
            for (unsigned int li = 0; li < layout.layers(); ++li) {
                if (layout.is_valid_layer(li)) {
                    info.shapeCount += static_cast<int>(cell.shapes(li).size());
                }
            }

            // Calculate bounding box area
            db::Box bbox = cell.bbox();
            if (!bbox.empty()) {
                double width = bbox.width() * dbu;
                double height = bbox.height() * dbu;
                info.boundingBoxArea = width * height;
            }

            result.push_back(info);
        }

        // Sort by relevance (top candidates first, then by area)
        std::sort(result.begin(), result.end());

    } catch (const tl::Exception& e) {
        m_lastError = std::string("KLayout error: ") + e.msg();
        std::cerr << "GDSAnalyzer: " << m_lastError << std::endl;
    } catch (const std::exception& e) {
        m_lastError = std::string("Error: ") + e.what();
        std::cerr << "GDSAnalyzer: " << m_lastError << std::endl;
    }

    return result;
}

std::vector<std::string> GDSAnalyzer::listAllCells(const std::string& gdsPath)
{
    std::vector<std::string> result;
    m_lastError.clear();

    try {
        tl::InputStream stream(gdsPath);
        db::Reader reader(stream);

        db::Layout layout;
        reader.read(layout);

        for (auto it = layout.begin(); it != layout.end(); ++it) {
            result.push_back(layout.cell_name(it->cell_index()));
        }

    } catch (const tl::Exception& e) {
        m_lastError = std::string("KLayout error: ") + e.msg();
        std::cerr << "GDSAnalyzer: " << m_lastError << std::endl;
    } catch (const std::exception& e) {
        m_lastError = std::string("Error: ") + e.what();
        std::cerr << "GDSAnalyzer: " << m_lastError << std::endl;
    }

    return result;
}

bool GDSAnalyzer::isFlatGDS(const std::string& gdsPath)
{
    auto cells = analyzeCells(gdsPath);

    if (cells.empty()) {
        return false;
    }

    // GDS is flat if ALL cells are top candidates (no hierarchy)
    for (const auto& cell : cells) {
        if (!cell.isTopCandidate) {
            return false;
        }
    }

    return true;
}

std::vector<std::string> GDSAnalyzer::getTopCellCandidates(const std::string& gdsPath)
{
    std::vector<std::string> result;

    auto cells = analyzeCells(gdsPath);

    for (const auto& cell : cells) {
        if (cell.isTopCandidate) {
            result.push_back(cell.name);
        }
    }

    return result;
}

#else
// Stub implementations when KLayout is not available

std::vector<GDSCellInfo> GDSAnalyzer::analyzeCells(const std::string& /*gdsPath*/)
{
    m_lastError = "KLayout not available";
    std::cerr << "GDSAnalyzer: " << m_lastError << std::endl;
    return {};
}

std::vector<std::string> GDSAnalyzer::listAllCells(const std::string& /*gdsPath*/)
{
    m_lastError = "KLayout not available";
    std::cerr << "GDSAnalyzer: " << m_lastError << std::endl;
    return {};
}

bool GDSAnalyzer::isFlatGDS(const std::string& /*gdsPath*/)
{
    m_lastError = "KLayout not available";
    return false;
}

std::vector<std::string> GDSAnalyzer::getTopCellCandidates(const std::string& /*gdsPath*/)
{
    m_lastError = "KLayout not available";
    return {};
}

#endif // HAVE_KLAYOUT

} // namespace chiplet

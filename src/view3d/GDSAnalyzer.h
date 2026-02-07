/**
 * GDSAnalyzer.h - Analyze GDS files for cell information
 *
 * Provides fast metadata extraction from GDS files without
 * loading full geometry. Used for cell selection dialogs.
 */

#ifndef CHIPLET_VIEW3D_GDSANALYZER_H
#define CHIPLET_VIEW3D_GDSANALYZER_H

#include <string>
#include <vector>

namespace chiplet {

/**
 * Information about a cell in a GDS file
 */
struct GDSCellInfo {
    std::string name;
    int instanceCount = 0;      // How many times this cell is instantiated by other cells
    bool isTopCandidate = true; // Not instantiated by any other cell
    int shapeCount = 0;         // Approximate number of shapes
    double boundingBoxArea = 0.0; // Area of bounding box (for sorting by size)

    // Comparison for sorting (larger cells first)
    bool operator<(const GDSCellInfo& other) const {
        // Top candidates first, then by area (descending)
        if (isTopCandidate != other.isTopCandidate) {
            return isTopCandidate > other.isTopCandidate;
        }
        return boundingBoxArea > other.boundingBoxArea;
    }
};

/**
 * GDSAnalyzer extracts cell metadata from GDS files
 */
class GDSAnalyzer {
public:
    GDSAnalyzer() = default;
    ~GDSAnalyzer() = default;

    /**
     * Analyze GDS file and return information about ALL cells
     * @param gdsPath Path to GDS file
     * @return Vector of cell info, sorted by relevance
     */
    std::vector<GDSCellInfo> analyzeCells(const std::string& gdsPath);

    /**
     * Get names of all cells in GDS file (fast, names only)
     * @param gdsPath Path to GDS file
     * @return Vector of cell names
     */
    std::vector<std::string> listAllCells(const std::string& gdsPath);

    /**
     * Detect if GDS is "flat" (all cells are top candidates)
     * @param gdsPath Path to GDS file
     * @return true if all cells are top candidates (flat GDS)
     */
    bool isFlatGDS(const std::string& gdsPath);

    /**
     * Get top cell candidates (cells not instantiated by others)
     * @param gdsPath Path to GDS file
     * @return Vector of cell names that are top candidates
     */
    std::vector<std::string> getTopCellCandidates(const std::string& gdsPath);

    // Get last error message
    const std::string& lastError() const { return m_lastError; }

private:
    std::string m_lastError;
};

} // namespace chiplet

#endif // CHIPLET_VIEW3D_GDSANALYZER_H

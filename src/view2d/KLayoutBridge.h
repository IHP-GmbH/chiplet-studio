/**
 * KLayoutBridge.h - Bridge to KLayout for 2D layout visualization
 */

#ifndef CHIPLET_VIEW2D_KLAYOUTBRIDGE_H
#define CHIPLET_VIEW2D_KLAYOUTBRIDGE_H

#include <string>

namespace chiplet {

/**
 * KLayoutBridge provides access to KLayout's 2D layout viewing capabilities.
 */
class KLayoutBridge {
public:
    KLayoutBridge();
    ~KLayoutBridge();

    /**
     * Load a layout file (GDS/OASIS).
     * @param path Path to layout file
     * @return true on success
     */
    bool loadLayout(const std::string& path);

    /**
     * Set the top cell to display.
     */
    void setTopCell(const std::string& cellName);

private:
    // TODO: Add KLayout db::Layout member when integrated
};

} // namespace chiplet

#endif // CHIPLET_VIEW2D_KLAYOUTBRIDGE_H

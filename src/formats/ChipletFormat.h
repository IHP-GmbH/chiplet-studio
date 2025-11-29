/**
 * ChipletFormat.h - Parser/Writer for .chiplet YAML format
 */

#ifndef CHIPLET_FORMATS_CHIPLETFORMAT_H
#define CHIPLET_FORMATS_CHIPLETFORMAT_H

#include <string>
#include <memory>
#include "core/Assembly.h"

namespace chiplet {

/**
 * ChipletFormat handles reading and writing .chiplet files.
 */
class ChipletFormat {
public:
    ChipletFormat();
    ~ChipletFormat();

    /**
     * Load an assembly from a .chiplet file.
     * @param path Path to .chiplet file
     * @return Assembly or nullptr on error
     */
    std::unique_ptr<Assembly> load(const std::string& path);

    /**
     * Save an assembly to a .chiplet file.
     * @param assembly Assembly to save
     * @param path Path to .chiplet file
     * @return true on success
     */
    bool save(const Assembly& assembly, const std::string& path);

    /**
     * Get last error message.
     */
    const std::string& errorMessage() const;

private:
    std::string m_errorMessage;
};

} // namespace chiplet

#endif // CHIPLET_FORMATS_CHIPLETFORMAT_H

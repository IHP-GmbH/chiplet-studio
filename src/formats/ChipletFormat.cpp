/**
 * ChipletFormat.cpp - Implementation
 *
 * TODO: Implement YAML parsing using yaml-cpp
 */

#include "ChipletFormat.h"

namespace chiplet {

ChipletFormat::ChipletFormat() = default;
ChipletFormat::~ChipletFormat() = default;

std::unique_ptr<Assembly> ChipletFormat::load(const std::string& path)
{
    // TODO: Implement YAML parsing
    m_errorMessage = "Not implemented";
    return nullptr;
}

bool ChipletFormat::save(const Assembly& assembly, const std::string& path)
{
    // TODO: Implement YAML writing
    m_errorMessage = "Not implemented";
    return false;
}

const std::string& ChipletFormat::errorMessage() const
{
    return m_errorMessage;
}

} // namespace chiplet

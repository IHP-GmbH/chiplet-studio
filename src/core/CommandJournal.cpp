/**
 * CommandJournal.cpp - Implementation
 */

#include "CommandJournal.h"

#include <QDebug>

#include <chrono>
#include <iomanip>
#include <sstream>

namespace chiplet {

CommandJournal::CommandJournal(const std::filesystem::path& journal_path)
    : m_path(journal_path)
{
}

CommandJournal::~CommandJournal()
{
    close();
}

bool CommandJournal::ensure_open()
{
    if (m_open) {
        return true;
    }

    try {
        // Create parent directories if they don't exist
        auto parent = m_path.parent_path();
        if (!parent.empty() && !std::filesystem::exists(parent)) {
            std::filesystem::create_directories(parent);
        }

        m_file.open(m_path, std::ios::out | std::ios::app);
        if (m_file.is_open()) {
            m_open = true;
            return true;
        }
    } catch (const std::exception& e) {
        qWarning("Failed to open journal file: %s", e.what());
    }

    return false;
}

void CommandJournal::record(const Command& cmd)
{
    if (!ensure_open()) {
        return;
    }

    try {
        // Get current timestamp in ISO 8601 format
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        std::stringstream ss;
        ss << std::put_time(std::gmtime(&time_t), "%Y-%m-%dT%H:%M:%SZ");

        // Create journal entry
        nlohmann::json entry;
        entry["timestamp"] = ss.str();
        entry["type"] = cmd.type();
        entry["data"] = cmd.serialize()["data"];

        // Write as single line (JSON Lines format)
        m_file << entry.dump() << "\n";
        m_file.flush();  // Immediate flush for crash safety
    } catch (const std::exception& e) {
        qWarning("Failed to record command to journal: %s", e.what());
    }
}

void CommandJournal::flush()
{
    if (m_open && m_file.is_open()) {
        m_file.flush();
    }
}

void CommandJournal::close()
{
    if (m_open && m_file.is_open()) {
        m_file.close();
        m_open = false;
    }
}

void CommandJournal::discard()
{
    close();

    try {
        if (std::filesystem::exists(m_path)) {
            std::filesystem::remove(m_path);
        }
    } catch (const std::exception& e) {
        qWarning("Failed to discard journal file: %s", e.what());
    }
}

bool CommandJournal::has_orphaned_journal(const std::filesystem::path& directory,
                                          const std::string& journal_name)
{
    auto journal_path = directory / journal_name;
    return std::filesystem::exists(journal_path);
}

std::filesystem::path CommandJournal::find_orphaned_journal(
    const std::filesystem::path& directory,
    const std::string& journal_name)
{
    auto journal_path = directory / journal_name;
    if (std::filesystem::exists(journal_path)) {
        return journal_path;
    }
    return {};
}

std::vector<CommandPtr> CommandJournal::replay(const std::filesystem::path& journal_path,
                                               const CommandFactory& factory)
{
    std::vector<CommandPtr> commands;

    if (!std::filesystem::exists(journal_path)) {
        return commands;
    }

    std::ifstream file(journal_path);
    if (!file.is_open()) {
        qWarning("Failed to open journal file for replay");
        return commands;
    }

    std::string line;
    int line_number = 0;

    while (std::getline(file, line)) {
        line_number++;

        if (line.empty()) {
            continue;
        }

        try {
            auto entry = nlohmann::json::parse(line);
            std::string type = entry.at("type").get<std::string>();

            // Reconstruct the command data format expected by deserialize
            nlohmann::json cmd_json;
            cmd_json["type"] = type;
            cmd_json["data"] = entry.at("data");

            CommandPtr cmd = factory(type, cmd_json);
            if (cmd) {
                commands.push_back(std::move(cmd));
            } else {
                qWarning("Unknown command type in journal line %d: %s",
                         line_number, type.c_str());
            }
        } catch (const std::exception& e) {
            qWarning("Failed to parse journal line %d: %s", line_number, e.what());
        }
    }

    return commands;
}

} // namespace chiplet

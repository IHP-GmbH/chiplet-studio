/**
 * test_command_processor.cpp - Unit tests for Command system
 *
 * Tests CommandProcessor, CmdMoveComponent, CmdRenameComponent,
 * CommandJournal, and CommandFactory.
 */

#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include "core/Assembly.h"
#include "core/Component.h"
#include "core/CommandProcessor.h"
#include "core/CommandJournal.h"
#include "core/CommandFactory.h"
#include "core/commands/CmdMoveComponent.h"
#include "core/commands/CmdRenameComponent.h"

namespace chiplet {
namespace {

// =============================================================================
// Helper: Temporary directory for journal tests
// =============================================================================

class TempDir {
public:
    TempDir()
        : m_path(std::filesystem::temp_directory_path() / ("cmd_test_" + std::to_string(counter++)))
    {
        std::filesystem::create_directories(m_path);
    }

    ~TempDir()
    {
        std::filesystem::remove_all(m_path);
    }

    const std::filesystem::path& path() const { return m_path; }

private:
    std::filesystem::path m_path;
    static int counter;
};

int TempDir::counter = 0;

// =============================================================================
// Helper: Create test assembly
// =============================================================================

std::unique_ptr<Assembly> createTestAssembly()
{
    auto assembly = std::make_unique<Assembly>();
    assembly->set_name("TestAssembly");

    auto die = std::make_unique<Component>("die_a", ComponentType::Die);
    die->set_position({100.0, 200.0, 50.0});
    assembly->add_component(std::move(die));

    auto interposer = std::make_unique<Component>("interposer", ComponentType::Interposer);
    interposer->set_position({0.0, 0.0, 0.0});
    assembly->add_component(std::move(interposer));

    return assembly;
}

// =============================================================================
// CmdMoveComponent Tests
// =============================================================================

TEST(CmdMoveComponent, ExecuteChangesPosition)
{
    auto assembly = createTestAssembly();
    Component* die = assembly->component("die_a");
    ASSERT_NE(die, nullptr);

    Position3D oldPos = die->position();
    Position3D newPos = {200.0, 300.0, 100.0};

    CmdMoveComponent cmd("die_a", oldPos, newPos);
    EXPECT_TRUE(cmd.execute(*assembly));

    EXPECT_DOUBLE_EQ(die->position().x, 200.0);
    EXPECT_DOUBLE_EQ(die->position().y, 300.0);
    EXPECT_DOUBLE_EQ(die->position().z, 100.0);
}

TEST(CmdMoveComponent, UndoRestoresPosition)
{
    auto assembly = createTestAssembly();
    Component* die = assembly->component("die_a");
    ASSERT_NE(die, nullptr);

    Position3D oldPos = die->position();
    Position3D newPos = {200.0, 300.0, 100.0};

    CmdMoveComponent cmd("die_a", oldPos, newPos);
    cmd.execute(*assembly);
    cmd.undo(*assembly);

    EXPECT_DOUBLE_EQ(die->position().x, oldPos.x);
    EXPECT_DOUBLE_EQ(die->position().y, oldPos.y);
    EXPECT_DOUBLE_EQ(die->position().z, oldPos.z);
}

TEST(CmdMoveComponent, ExecuteFailsForInvalidComponent)
{
    auto assembly = createTestAssembly();
    Position3D oldPos = {0.0, 0.0, 0.0};
    Position3D newPos = {100.0, 100.0, 100.0};

    CmdMoveComponent cmd("nonexistent", oldPos, newPos);
    EXPECT_FALSE(cmd.execute(*assembly));
}

TEST(CmdMoveComponent, Description)
{
    Position3D oldPos = {100.0, 200.0, 50.0};
    Position3D newPos = {200.0, 300.0, 100.0};
    CmdMoveComponent cmd("die_a", oldPos, newPos);

    std::string desc = cmd.description();
    EXPECT_FALSE(desc.empty());
    EXPECT_NE(desc.find("die_a"), std::string::npos);
}

TEST(CmdMoveComponent, Type)
{
    Position3D pos = {0.0, 0.0, 0.0};
    CmdMoveComponent cmd("die", pos, pos);
    EXPECT_EQ(cmd.type(), "MoveComponent");
}

TEST(CmdMoveComponent, SerializeDeserialize)
{
    Position3D oldPos = {100.0, 200.0, 50.0};
    Position3D newPos = {200.0, 300.0, 100.0};
    CmdMoveComponent cmd("die_a", oldPos, newPos);

    nlohmann::json j = cmd.serialize();

    auto deserialized = CmdMoveComponent::deserialize(j);
    ASSERT_NE(deserialized, nullptr);

    auto* moveCmd = dynamic_cast<CmdMoveComponent*>(deserialized.get());
    ASSERT_NE(moveCmd, nullptr);

    EXPECT_EQ(moveCmd->component_id(), "die_a");
    EXPECT_DOUBLE_EQ(moveCmd->old_position().x, 100.0);
    EXPECT_DOUBLE_EQ(moveCmd->new_position().x, 200.0);
}

TEST(CmdMoveComponent, CanMerge)
{
    Position3D pos1 = {0.0, 0.0, 0.0};
    Position3D pos2 = {10.0, 0.0, 0.0};
    Position3D pos3 = {20.0, 0.0, 0.0};

    CmdMoveComponent cmd1("die", pos1, pos2);
    CmdMoveComponent cmd2("die", pos2, pos3);
    CmdMoveComponent cmd3("other_die", pos1, pos2);

    EXPECT_TRUE(cmd1.can_merge_with(cmd2));  // Same component
    EXPECT_FALSE(cmd1.can_merge_with(cmd3)); // Different component
}

TEST(CmdMoveComponent, Merge)
{
    Position3D pos1 = {0.0, 0.0, 0.0};
    Position3D pos2 = {10.0, 10.0, 10.0};
    Position3D pos3 = {20.0, 20.0, 20.0};

    CmdMoveComponent cmd1("die", pos1, pos2);
    CmdMoveComponent cmd2("die", pos2, pos3);

    cmd1.merge_with(cmd2);

    // After merge, cmd1 should have original old_pos but new_pos from cmd2
    EXPECT_DOUBLE_EQ(cmd1.old_position().x, 0.0);
    EXPECT_DOUBLE_EQ(cmd1.new_position().x, 20.0);
}

// =============================================================================
// CmdRenameComponent Tests
// =============================================================================

TEST(CmdRenameComponent, ExecuteChangesName)
{
    auto assembly = createTestAssembly();
    Component* die = assembly->component("die_a");
    ASSERT_NE(die, nullptr);

    std::string oldName = die->name();
    std::string newName = "renamed_die";

    CmdRenameComponent cmd("die_a", oldName, newName);
    EXPECT_TRUE(cmd.execute(*assembly));

    EXPECT_EQ(die->name(), "renamed_die");
}

TEST(CmdRenameComponent, UndoRestoresName)
{
    auto assembly = createTestAssembly();
    Component* die = assembly->component("die_a");
    ASSERT_NE(die, nullptr);

    std::string oldName = die->name();
    std::string newName = "renamed_die";

    CmdRenameComponent cmd("die_a", oldName, newName);
    cmd.execute(*assembly);
    cmd.undo(*assembly);

    EXPECT_EQ(die->name(), oldName);
}

TEST(CmdRenameComponent, ExecuteFailsForInvalidComponent)
{
    auto assembly = createTestAssembly();

    CmdRenameComponent cmd("nonexistent", "old", "new");
    EXPECT_FALSE(cmd.execute(*assembly));
}

TEST(CmdRenameComponent, SerializeDeserialize)
{
    CmdRenameComponent cmd("comp_id", "old_name", "new_name");

    nlohmann::json j = cmd.serialize();

    auto deserialized = CmdRenameComponent::deserialize(j);
    ASSERT_NE(deserialized, nullptr);

    auto* renameCmd = dynamic_cast<CmdRenameComponent*>(deserialized.get());
    ASSERT_NE(renameCmd, nullptr);

    EXPECT_EQ(renameCmd->component_id(), "comp_id");
    EXPECT_EQ(renameCmd->old_name(), "old_name");
    EXPECT_EQ(renameCmd->new_name(), "new_name");
}

// =============================================================================
// CommandProcessor Tests
// =============================================================================

TEST(CommandProcessor, ExecuteAddsToUndoStack)
{
    auto assembly = createTestAssembly();
    CommandProcessor processor(assembly.get());

    EXPECT_FALSE(processor.can_undo());
    EXPECT_EQ(processor.undo_stack_size(), 0u);

    Position3D oldPos = assembly->component("die_a")->position();
    Position3D newPos = {500.0, 500.0, 500.0};

    processor.execute(std::make_unique<CmdMoveComponent>("die_a", oldPos, newPos));

    EXPECT_TRUE(processor.can_undo());
    EXPECT_EQ(processor.undo_stack_size(), 1u);
}

TEST(CommandProcessor, Undo)
{
    auto assembly = createTestAssembly();
    CommandProcessor processor(assembly.get());

    Component* die = assembly->component("die_a");
    Position3D originalPos = die->position();
    Position3D newPos = {500.0, 500.0, 500.0};

    processor.execute(std::make_unique<CmdMoveComponent>("die_a", originalPos, newPos));
    EXPECT_DOUBLE_EQ(die->position().x, 500.0);

    processor.undo();
    EXPECT_DOUBLE_EQ(die->position().x, originalPos.x);
    EXPECT_FALSE(processor.can_undo());
    EXPECT_TRUE(processor.can_redo());
}

TEST(CommandProcessor, Redo)
{
    auto assembly = createTestAssembly();
    CommandProcessor processor(assembly.get());

    Component* die = assembly->component("die_a");
    Position3D originalPos = die->position();
    Position3D newPos = {500.0, 500.0, 500.0};

    processor.execute(std::make_unique<CmdMoveComponent>("die_a", originalPos, newPos));
    processor.undo();
    processor.redo();

    EXPECT_DOUBLE_EQ(die->position().x, 500.0);
    EXPECT_TRUE(processor.can_undo());
    EXPECT_FALSE(processor.can_redo());
}

TEST(CommandProcessor, ExecuteClearsRedoStack)
{
    auto assembly = createTestAssembly();
    CommandProcessor processor(assembly.get());

    Component* die = assembly->component("die_a");
    Position3D pos1 = die->position();
    Position3D pos2 = {500.0, 500.0, 500.0};
    Position3D pos3 = {600.0, 600.0, 600.0};

    processor.execute(std::make_unique<CmdMoveComponent>("die_a", pos1, pos2));
    processor.undo();
    EXPECT_TRUE(processor.can_redo());

    // New command should clear redo stack
    processor.execute(std::make_unique<CmdMoveComponent>("die_a", pos1, pos3));
    EXPECT_FALSE(processor.can_redo());
}

TEST(CommandProcessor, Clear)
{
    auto assembly = createTestAssembly();
    CommandProcessor processor(assembly.get());

    Position3D pos1 = assembly->component("die_a")->position();
    Position3D pos2 = {500.0, 500.0, 500.0};

    processor.execute(std::make_unique<CmdMoveComponent>("die_a", pos1, pos2));
    EXPECT_TRUE(processor.can_undo());

    processor.clear();
    EXPECT_FALSE(processor.can_undo());
    EXPECT_FALSE(processor.can_redo());
}

TEST(CommandProcessor, UndoDescription)
{
    auto assembly = createTestAssembly();
    CommandProcessor processor(assembly.get());

    Position3D pos1 = assembly->component("die_a")->position();
    Position3D pos2 = {500.0, 500.0, 500.0};

    processor.execute(std::make_unique<CmdMoveComponent>("die_a", pos1, pos2));

    QString desc = processor.undo_description();
    EXPECT_FALSE(desc.isEmpty());
}

TEST(CommandProcessor, StackLimit)
{
    auto assembly = createTestAssembly();
    CommandProcessor processor(assembly.get());

    // Execute more than MAX_UNDO_DEPTH commands
    for (size_t i = 0; i < CommandProcessor::MAX_UNDO_DEPTH + 10; ++i) {
        Position3D pos = {static_cast<double>(i), 0.0, 0.0};
        Position3D newPos = {static_cast<double>(i + 1), 0.0, 0.0};
        processor.execute(std::make_unique<CmdMoveComponent>("die_a", pos, newPos));
    }

    EXPECT_LE(processor.undo_stack_size(), CommandProcessor::MAX_UNDO_DEPTH);
}

// =============================================================================
// CommandJournal Tests
// =============================================================================

TEST(CommandJournal, RecordAndReplay)
{
    TempDir dir;
    auto journalPath = dir.path() / ".test.journal";

    // Register commands
    CommandFactory::register_builtin_commands();

    // Record commands
    {
        CommandJournal journal(journalPath);

        Position3D pos1 = {0.0, 0.0, 0.0};
        Position3D pos2 = {100.0, 100.0, 100.0};
        CmdMoveComponent moveCmd("die_a", pos1, pos2);
        journal.record(moveCmd);

        CmdRenameComponent renameCmd("die_a", "old_name", "new_name");
        journal.record(renameCmd);

        journal.flush();
    }

    // Replay commands
    auto commands = CommandJournal::replay(journalPath, CommandFactory::get_journal_factory());

    EXPECT_EQ(commands.size(), 2u);
    EXPECT_EQ(commands[0]->type(), "MoveComponent");
    EXPECT_EQ(commands[1]->type(), "RenameComponent");
}

TEST(CommandJournal, Discard)
{
    TempDir dir;
    auto journalPath = dir.path() / ".test.journal";

    {
        CommandJournal journal(journalPath);
        Position3D pos = {0.0, 0.0, 0.0};
        CmdMoveComponent cmd("die", pos, pos);
        journal.record(cmd);
        journal.discard();
    }

    EXPECT_FALSE(std::filesystem::exists(journalPath));
}

TEST(CommandJournal, HasOrphanedJournal)
{
    TempDir dir;
    auto journalPath = dir.path() / CommandJournal::DEFAULT_JOURNAL_NAME;

    // No journal exists initially
    EXPECT_FALSE(CommandJournal::has_orphaned_journal(dir.path()));

    // Create journal file
    {
        std::ofstream ofs(journalPath);
        ofs << "{}";
    }

    // Now orphan exists
    EXPECT_TRUE(CommandJournal::has_orphaned_journal(dir.path()));
}

TEST(CommandJournal, FindOrphanedJournal)
{
    TempDir dir;
    auto journalPath = dir.path() / CommandJournal::DEFAULT_JOURNAL_NAME;

    // No journal
    auto found = CommandJournal::find_orphaned_journal(dir.path());
    EXPECT_TRUE(found.empty());

    // Create journal
    {
        std::ofstream ofs(journalPath);
        ofs << "{}";
    }

    found = CommandJournal::find_orphaned_journal(dir.path());
    EXPECT_EQ(found, journalPath);
}

// =============================================================================
// CommandFactory Tests
// =============================================================================

TEST(CommandFactory, RegisterBuiltinCommands)
{
    CommandFactory::register_builtin_commands();

    EXPECT_TRUE(CommandFactory::is_registered("MoveComponent"));
    EXPECT_TRUE(CommandFactory::is_registered("RenameComponent"));
}

TEST(CommandFactory, CreateMoveCommand)
{
    CommandFactory::register_builtin_commands();

    // Match the format from CmdMoveComponent::serialize()
    nlohmann::json j = {
        {"data", {
            {"id", "die_a"},
            {"old", {0.0, 0.0, 0.0}},
            {"new", {100.0, 100.0, 100.0}}
        }}
    };

    auto cmd = CommandFactory::create("MoveComponent", j);
    ASSERT_NE(cmd, nullptr);
    EXPECT_EQ(cmd->type(), "MoveComponent");
}

TEST(CommandFactory, CreateRenameCommand)
{
    CommandFactory::register_builtin_commands();

    // Match the format from CmdRenameComponent::serialize()
    nlohmann::json j = {
        {"data", {
            {"id", "comp_id"},
            {"old", "old_name"},
            {"new", "new_name"}
        }}
    };

    auto cmd = CommandFactory::create("RenameComponent", j);
    ASSERT_NE(cmd, nullptr);
    EXPECT_EQ(cmd->type(), "RenameComponent");
}

TEST(CommandFactory, CreateUnknownTypeReturnsNull)
{
    auto cmd = CommandFactory::create("UnknownCommand", {});
    EXPECT_EQ(cmd, nullptr);
}

TEST(CommandFactory, GetJournalFactory)
{
    CommandFactory::register_builtin_commands();

    auto factory = CommandFactory::get_journal_factory();

    // Match the format from CmdMoveComponent::serialize()
    nlohmann::json j = {
        {"data", {
            {"id", "die"},
            {"old", {0.0, 0.0, 0.0}},
            {"new", {10.0, 10.0, 10.0}}
        }}
    };

    auto cmd = factory("MoveComponent", j);
    ASSERT_NE(cmd, nullptr);
    EXPECT_EQ(cmd->type(), "MoveComponent");
}

} // namespace
} // namespace chiplet

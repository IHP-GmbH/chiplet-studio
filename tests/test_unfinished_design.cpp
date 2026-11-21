// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * A design the flow wrote before its last step (_metadata.finalize_required)
 * opens read-only when the caller asks for that, says what finishes it, and
 * is never saved: not over itself, not to a new file, not as an autosave. By
 * default it is still refused (ChipletFormat.MetadataFinalizeRequiredRejected).
 */

#include <gtest/gtest.h>

#include "core/Assembly.h"
#include "core/DocumentPersistence.h"
#include "formats/ChipletFormat.h"
#include "ui/UnfinishedDesignBanner.h"

#include <QString>

#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>

using namespace chiplet;
namespace fs = std::filesystem;

namespace {

std::string readFile(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

void writeFile(const fs::path& path, const std::string& text)
{
    std::ofstream(path, std::ios::binary) << text;
}

const fs::path kFixture = fs::path(FIXTURES_DIR) / "intermediate_kicad.chiplet";

// A fresh directory per test, removed after it.
class OpenUnfinished : public ::testing::Test {
protected:
    void SetUp() override
    {
        const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
        dir = fs::temp_directory_path() / ("chiplet-unfinished-" + std::string(info->name()));
        fs::remove_all(dir);
        fs::create_directories(dir);
        ASSERT_TRUE(fs::exists(kFixture)) << kFixture;
    }
    void TearDown() override { fs::remove_all(dir); }

    // The fixture with its finalizer line replaced (or dropped, when empty).
    fs::path withFinalizerLine(const std::string& line)
    {
        const std::string text = std::regex_replace(readFile(kFixture),
            std::regex("  finalizer: [^\n]*\n"), line.empty() ? "" : "  " + line + "\n");
        EXPECT_NE(text, readFile(kFixture)) << "the fixture's finalizer line moved";
        const auto path = dir / "unfinished.chiplet";
        writeFile(path, text);
        return path;
    }

    fs::path dir;
};

} // namespace

TEST_F(OpenUnfinished, OpensReadOnlyWhenAskedAndNamesTheFinalizerTheFileGives)
{
    const auto path = withFinalizerLine("finalizer: finish_it.py --in-place");
    ChipletFormat format;
    auto assembly = format.load(path.string(), UnfinishedDesign::OpenReadOnly);
    ASSERT_NE(assembly, nullptr);
    EXPECT_FALSE(assembly->components().empty());
    EXPECT_TRUE(assembly->document_persistence().read_only());
    EXPECT_EQ(assembly->document_persistence().finalizer(), "finish_it.py --in-place");
}

TEST_F(OpenUnfinished, AFileThatNamesNoFinalizerGetsTheFormatsDefault)
{
    const auto path = withFinalizerLine("");
    ChipletFormat format;
    auto assembly = format.load(path.string(), UnfinishedDesign::OpenReadOnly);
    EXPECT_TRUE(assembly->document_persistence().read_only());
    EXPECT_EQ(assembly->document_persistence().finalizer(), "hyp_to_gds.py --update-chiplet-file");
}

TEST_F(OpenUnfinished, AFinishedDesignOpenedTheSameWayIsNotReadOnly)
{
    ChipletFormat format;
    auto assembly = format.load((fs::path(FIXTURES_DIR) / "minimal.chiplet").string(),
                                UnfinishedDesign::OpenReadOnly);
    EXPECT_FALSE(assembly->document_persistence().read_only());
    EXPECT_EQ(assembly->document_persistence().finalizer(), "");
}

TEST_F(OpenUnfinished, EverySaveIsRefusedAndNoFileIsWritten)
{
    const auto path = withFinalizerLine("finalizer: finish_it.py --in-place");
    const std::string before = readFile(path);
    ChipletFormat format;
    auto assembly = format.load(path.string(), UnfinishedDesign::OpenReadOnly);
    const fs::path elsewhere = dir / "saved_as.chiplet";

    const auto expectRefused = [&](auto&& save, const char* what) {
        try {
            save();
            ADD_FAILURE() << what << " was not refused";
        } catch (const ChipletFormatException& e) {
            const std::string msg = e.what();
            EXPECT_NE(msg.find("not finished"), std::string::npos) << what << ": " << msg;
            EXPECT_NE(msg.find("finish_it.py --in-place"), std::string::npos) << what << ": " << msg;
        }
    };
    expectRefused([&] { format.save(*assembly, path.string()); }, "Save");
    expectRefused([&] { format.save(*assembly, elsewhere.string()); }, "Save As");
    expectRefused([&] { format.save(*assembly, (dir / "auto.chiplet").string(),
                                    SavePurpose::Autosave); }, "Autosave");
    const Assembly& constAssembly = *assembly;
    expectRefused([&] { (void)format.save(constAssembly, elsewhere.string()); }, "const save");

    EXPECT_EQ(readFile(path), before);
    EXPECT_FALSE(fs::exists(elsewhere));
    EXPECT_FALSE(fs::exists(dir / "auto.chiplet"));
    EXPECT_TRUE(assembly->document_persistence().read_only());
}

TEST_F(OpenUnfinished, ARecoveryCheckpointOfItRecoversReadOnly)
{
    const auto path = withFinalizerLine("finalizer: finish_it.py --in-place");
    ChipletFormat format;
    auto assembly = format.load(path.string(), UnfinishedDesign::OpenReadOnly);
    auto recovered = format.decode_recovery(format.encode_recovery(*assembly));
    ASSERT_NE(recovered, nullptr);
    EXPECT_TRUE(recovered->document_persistence().read_only());
    EXPECT_EQ(recovered->document_persistence().finalizer(), "finish_it.py --in-place");
}

TEST_F(OpenUnfinished, TheBannerSaysSoInTheApprovedWordsAndIsHiddenOtherwise)
{
    const auto path = withFinalizerLine("finalizer: finish_it.py --in-place");
    ChipletFormat format;
    auto unfinished = format.load(path.string(), UnfinishedDesign::OpenReadOnly);
    auto finished = format.load((fs::path(FIXTURES_DIR) / "minimal.chiplet").string(),
                                UnfinishedDesign::OpenReadOnly);

    UnfinishedDesignBanner banner;
    EXPECT_TRUE(banner.isHidden());

    banner.showFor(unfinished->document_persistence());
    EXPECT_FALSE(banner.isHidden());
    EXPECT_EQ(banner.text(),
        QString("This design is not finished: the flow wrote it before its last step, so "
                "positions and connections may still change. It is opened read-only; editing "
                "and saving are off. To get the final design, run finish_it.py --in-place and "
                "open its result."));

    banner.showFor(finished->document_persistence());
    EXPECT_TRUE(banner.isHidden());
    EXPECT_TRUE(banner.text().isEmpty());
}

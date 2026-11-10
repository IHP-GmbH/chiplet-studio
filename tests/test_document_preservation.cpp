// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 IHP GmbH
#include <gtest/gtest.h>
#include "formats/ChipletFormat.h"
#include <chiplet_format_io/chiplet_format_io.hpp>
#include <filesystem>
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <type_traits>
#include <unistd.h>
#include <QJsonDocument>
#include <QJsonObject>

using namespace chiplet;
namespace {
class DocumentPreservation : public ::testing::Test {
protected:
    std::filesystem::path dir;
    void SetUp() override {
        std::string pattern = (std::filesystem::temp_directory_path() / "studio-preserve-XXXXXX").string();
        std::vector<char> bytes(pattern.begin(), pattern.end());
        bytes.push_back('\0');
        const char* created = ::mkdtemp(bytes.data());
        ASSERT_NE(created, nullptr);
        dir = created;
    }
    void TearDown() override { if (!dir.empty()) std::filesystem::remove_all(dir); }
    std::string path(const std::string& name) const { return (dir / name).string(); }
    void put(const std::string& name, const std::string& text) {
        std::ofstream stream(path(name), std::ios::binary);
        stream.write(text.data(), static_cast<std::streamsize>(text.size()));
        ASSERT_TRUE(stream.good());
    }
    std::string get(const std::string& name) {
        std::ifstream stream(path(name), std::ios::binary);
        return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    }
    const std::string basic = "format_version: '1.0'\nassembly:\n  name: original\n";
};

using ConstWorker = SaveReceipt (ChipletFormat::*)(const Assembly&, const std::string&, const SaveRequest&);
static_assert(std::is_same_v<decltype(&ChipletFormat::save_snapshot), ConstWorker>);
static_assert(std::is_same_v<decltype(std::declval<const Assembly&>().document_persistence()),
                             const DocumentPersistence&>);
template<class T, class = void> struct can_replace_persistence : std::false_type {};
template<class T> struct can_replace_persistence<T, std::void_t<decltype(
    std::declval<T&>().set_document_persistence(DocumentPersistence{}))>> : std::true_type {};
static_assert(!can_replace_persistence<Assembly>::value);
static_assert(!can_replace_persistence<const Assembly>::value);
static_assert(!std::is_aggregate_v<SaveReceipt>);
static_assert(!std::is_default_constructible_v<SaveReceipt>);
static_assert(std::is_copy_constructible_v<SaveReceipt>);
static_assert(!std::is_constructible_v<SaveReceipt, std::shared_ptr<const int>, std::string, std::string>);
}

TEST_F(DocumentPreservation, CarriesPreambleAndOpaqueBytesIncludingUnterminatedLastBlock)
{
    const std::string prefix = "# lead with UTF-8: é\r\n\r\n";
    const std::string opaque = "interposer: {adapter: intm4tm2}\r\n_metadata:\r\n  code: '001'\r\n  enabled: false";
    put("input.chiplet", prefix + basic + opaque);
    ChipletFormat format;
    auto assembly = format.load(path("input.chiplet"));
    assembly->set_name("edited");
    format.save(*assembly, path("output.chiplet"));
    const auto bytes = get("output.chiplet");
    EXPECT_EQ(bytes.substr(0, prefix.size()), prefix);
    EXPECT_EQ(bytes.substr(bytes.size() - opaque.size()), opaque);
    EXPECT_NE(bytes.find("edited"), std::string::npos);
    EXPECT_NE(bytes.back(), '\n');
    EXPECT_EQ(format.load(path("output.chiplet"))->name(), "edited");
}

TEST_F(DocumentPreservation, IntentionalOwnedDeletionDoesNotReviveSourceSections)
{
    put("input.chiplet", basic + "components:\n  - id: U1\n    type: die\n"
        "netlist:\n  nets:\n    - name: A\ninterposer: {adapter: intm4tm2}\n");
    ChipletFormat format;
    auto assembly = format.load(path("input.chiplet"));
    ASSERT_TRUE(assembly->remove_component("U1"));
    assembly->set_netlist(Netlist{});
    format.save(*assembly, path("output.chiplet"));
    const auto keys = chiplet_format_io::split_top_level_keys(get("output.chiplet")).keys;
    EXPECT_EQ(std::count(keys.begin(), keys.end(), "components"), 0);
    EXPECT_EQ(std::count(keys.begin(), keys.end(), "netlist"), 0);
    EXPECT_EQ(std::count(keys.begin(), keys.end(), "interposer"), 1);
}

TEST_F(DocumentPreservation, RefusesUnsafeCaptureWithoutTouchingDestinationOrModel)
{
    put("input.chiplet", "format_version: '1.0'\nassembly:\n  name: &label original\n");
    ChipletFormat format;
    auto assembly = format.load(path("input.chiplet"));
    ASSERT_FALSE(assembly->document_persistence().capture_safe);
    assembly->set_name("unsaved");
    EXPECT_THROW(format.save(*assembly, path("input.chiplet")), ChipletFormatException);
    EXPECT_NE(get("input.chiplet").find("&label"), std::string::npos);
    EXPECT_EQ(assembly->name(), "unsaved");
}

TEST_F(DocumentPreservation, RepeatedPrimaryAndAutosaveKeepSeparateReceipts)
{
    put("input.chiplet", basic);
    ChipletFormat format;
    auto assembly = format.load(path("input.chiplet"));
    assembly->set_name("first");
    format.save(*assembly, path("input.chiplet"));
    const auto primary = get("input.chiplet");
    assembly->set_name("second");
    format.save(*assembly, path("auto.chiplet"), SavePurpose::Autosave);
    assembly->set_name("third");
    format.save(*assembly, path("auto.chiplet"), SavePurpose::Autosave);
    EXPECT_EQ(get("input.chiplet"), primary);
    EXPECT_EQ(assembly->document_persistence().primary->bytes(), primary);
    EXPECT_EQ(assembly->document_persistence().autosave->bytes(), get("auto.chiplet"));
    format.save(*assembly, path("input.chiplet"));
    EXPECT_NE(get("input.chiplet").find("third"), std::string::npos);
    EXPECT_THROW(format.save(*assembly, path("input.chiplet"), SavePurpose::Autosave), ChipletFormatException);
}

TEST_F(DocumentPreservation, ExternalChangeAndUnrelatedDestinationRefuse)
{
    put("input.chiplet", basic);
    put("unrelated.chiplet", "unrelated bytes");
    ChipletFormat format;
    auto assembly = format.load(path("input.chiplet"));
    assembly->set_name("unsaved");
    put("input.chiplet", basic + "# external\n");
    EXPECT_THROW(format.save(*assembly, path("input.chiplet")), ChipletFormatException);
    EXPECT_THROW(format.save(*assembly, path("unrelated.chiplet")), ChipletFormatException);
    EXPECT_EQ(get("input.chiplet"), basic + "# external\n");
    EXPECT_EQ(get("unrelated.chiplet"), "unrelated bytes");
    EXPECT_EQ(assembly->name(), "unsaved");
}

TEST_F(DocumentPreservation, ConstWorkerReturnsReceiptWithoutAdvancingAssembly)
{
    put("input.chiplet", basic);
    ChipletFormat format;
    auto assembly = format.load(path("input.chiplet"));
    assembly->set_name("first");
    const Assembly& view = *assembly;
    const auto receipt = format.save(view, path("input.chiplet"));
    EXPECT_EQ(assembly->document_persistence().primary->bytes(), basic);
    EXPECT_EQ(assembly->name(), "first");
    assembly->set_name("second");
    EXPECT_THROW((void)format.save(view, path("input.chiplet")), ChipletFormatException);
    const auto next = format.save_snapshot(view, path("input.chiplet"), SaveRequest{receipt});
    EXPECT_EQ(next.bytes(), get("input.chiplet"));
    EXPECT_EQ(assembly->document_persistence().primary->bytes(), basic);
}

TEST_F(DocumentPreservation, RecoveryKeepsOriginalBaselineAndCurrentEditsSeparately)
{
    const auto original = "# source\n" + basic + "_metadata: {code: '001'}\n";
    put("input.chiplet", original);
    ChipletFormat format;
    auto assembly = format.load(path("input.chiplet"));
    assembly->set_name("recovered edit");
    format.save(*assembly, path("auto.chiplet"), SavePurpose::Autosave);
    const auto payload = format.encode_recovery(*assembly);
    auto recovered = format.decode_recovery(payload);
    EXPECT_EQ(recovered->name(), "recovered edit");
    EXPECT_EQ(recovered->document_persistence().source_bytes, original);
    EXPECT_EQ(recovered->document_persistence().primary->bytes(), original);
    EXPECT_EQ(recovered->document_persistence().autosave->bytes(), get("auto.chiplet"));
    format.save(*recovered, path("input.chiplet"));
    EXPECT_NE(get("input.chiplet").find("recovered edit"), std::string::npos);
    put("input.chiplet", original + "# changed since checkpoint\n");
    auto stale = format.decode_recovery(payload);
    EXPECT_THROW(format.save(*stale, path("input.chiplet")), ChipletFormatException);
    EXPECT_THROW((void)format.decode_recovery(payload.substr(0, payload.size() - 1)), ChipletFormatException);
}

TEST_F(DocumentPreservation, FreshModelRecoveryNeverAuthorizesExistingUnrelatedFile)
{
    Assembly assembly;
    assembly.set_name("fresh");
    ChipletFormat format;
    auto recovered = format.decode_recovery(format.encode_recovery(assembly));
    put("target.chiplet", basic);
    EXPECT_THROW(format.save(*recovered, path("target.chiplet")), ChipletFormatException);
    EXPECT_EQ(get("target.chiplet"), basic);
    format.save(*recovered, path("new.chiplet"));
    EXPECT_EQ(format.load(path("new.chiplet"))->name(), "fresh");
}

TEST_F(DocumentPreservation, CapturedFlowAndOpaqueOrderSurviveButDeletedFlowDoesNotReturn)
{
    const std::string flow = "flow:\n  steps: []\n  # retained flow comment\n";
    const std::string leading = "_leading: {kept: true}\n";
    const std::string opaque = "_metadata: {tag: '001'}";
    put("input.chiplet", leading + flow + basic + opaque);
    ChipletFormat format;
    auto assembly = format.load(path("input.chiplet"));
    ASSERT_EQ(assembly->flow_source_text(), flow);
    format.save(*assembly, path("saved.chiplet"));
    const auto saved = get("saved.chiplet");
    const auto carried = leading + flow + opaque;
    EXPECT_EQ(saved.substr(saved.size() - carried.size()), carried);
    // Owned roots are regenerated first; only the carried roots' mutual order,
    // not their original position relative to owned roots, is preserved.
    EXPECT_LT(saved.find("assembly:"), saved.find("_leading:"));
    assembly->set_flow_definition(FlowDefinition{});
    format.save(*assembly, path("saved.chiplet"));
    const auto keys = chiplet_format_io::split_top_level_keys(get("saved.chiplet")).keys;
    EXPECT_EQ(std::count(keys.begin(), keys.end(), "flow"), 0);
    EXPECT_EQ(std::count(keys.begin(), keys.end(), "_metadata"), 1);
}

TEST_F(DocumentPreservation, FailedWriteNeverAdvancesReceiptOrChangesModel)
{
    put("input.chiplet", basic);
    ChipletFormat format;
    auto assembly = format.load(path("input.chiplet"));
    const auto baseline = *assembly->document_persistence().primary;
    assembly->set_name("unsaved edit");
    EXPECT_THROW(format.save(*assembly, path("missing-parent/output.chiplet")), ChipletFormatException);
    EXPECT_EQ(assembly->document_persistence().primary->canonical_path(), baseline.canonical_path());
    EXPECT_EQ(assembly->document_persistence().primary->bytes(), baseline.bytes());
    EXPECT_EQ(get("input.chiplet"), basic);
    EXPECT_EQ(assembly->name(), "unsaved edit");
    EXPECT_FALSE(std::filesystem::exists(path("missing-parent/output.chiplet")));
}

TEST_F(DocumentPreservation, RecoveryRetainsOriginalRelativePathContextAfterSaveAs)
{
    put("input.chiplet", basic + "  assembly_gds: assets/layout.gds\n");
    ChipletFormat format;
    auto assembly = format.load(path("input.chiplet"));
    const auto resolved = assembly->assembly_gds();
    std::filesystem::create_directory(dir / "other");
    format.save(*assembly, path("other/output.chiplet"));
    auto recovered = format.decode_recovery(format.encode_recovery(*assembly));
    EXPECT_EQ(recovered->assembly_gds(), resolved);
    EXPECT_EQ(recovered->assembly_gds_source(), "assets/layout.gds");
    EXPECT_EQ(recovered->document_persistence().source_path, path("input.chiplet"));
    EXPECT_EQ(recovered->document_persistence().primary->canonical_path(), path("other/output.chiplet"));
}

TEST_F(DocumentPreservation, ReceiptForAnotherDocumentRefusesEvenIdenticalOpaqueContext)
{
    put("a.chiplet", basic + "_metadata: {tag: '001'}\n");
    put("b.chiplet", basic + "_metadata: {tag: '001'}\n");
    ChipletFormat format;
    auto a = format.load(path("a.chiplet"));
    auto b = format.load(path("b.chiplet"));
    const auto receipt = format.save(static_cast<const Assembly&>(*b), path("b.chiplet"));
    const auto baseline = get("b.chiplet");
    a->set_name("must not land");
    EXPECT_THROW((void)format.save_snapshot(*a, path("b.chiplet"), SaveRequest{receipt}), ChipletFormatException);
    EXPECT_EQ(get("b.chiplet"), baseline);
    b->set_name("allowed owner");
    const auto own = format.save_snapshot(*b, path("b.chiplet"), SaveRequest{receipt});
    EXPECT_EQ(own.bytes(), get("b.chiplet"));
}

TEST_F(DocumentPreservation, RecoveryRefusesUnrelatedReceiptOpaqueBytesForBothDestinations)
{
    put("a.chiplet", basic + "interposer: {adapter: intm4tm2}\n_metadata: {tag: '001'}\n");
    put("b.chiplet", basic + "interposer: {adapter: other}\n_metadata: {tag: '002'}\n");
    ChipletFormat format;
    auto a = format.load(path("a.chiplet"));
    auto b = format.load(path("b.chiplet"));
    const auto good = format.encode_recovery(*a);
    ASSERT_NO_THROW((void)format.decode_recovery(good));
    const auto foreignPayload = format.encode_recovery(*b);
    const auto foreign = QJsonDocument::fromJson(QByteArray::fromStdString(foreignPayload)).object().value("primary");
    for (const auto* field : {"primary", "autosave"}) {
        auto object = QJsonDocument::fromJson(QByteArray::fromStdString(good)).object();
        object.insert(field, foreign);
        const auto mutant = QJsonDocument(object).toJson(QJsonDocument::Compact).toStdString();
        EXPECT_THROW((void)format.decode_recovery(mutant), ChipletFormatException) << field;
    }
    EXPECT_EQ(get("b.chiplet"), basic + "interposer: {adapter: other}\n_metadata: {tag: '002'}\n");
}

// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Probe: assert the DESIRED behaviour so the failures name which flow
// round-trip defects are still live now that load preserves the raw flow
// slice.

#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>

#include "formats/ChipletFormat.h"
#include "core/Assembly.h"
#include "core/flow/FlowDefinition.h"

using namespace chiplet;

namespace {

std::string slurp(const std::string& p) {
    std::ifstream f(p);
    std::ostringstream ss; ss << f.rdbuf(); return ss.str();
}

std::string fixture(const char* name) {
    return std::string(FIXTURES_DIR) + "/" + name;
}

// Force model emission: replacing the definition clears the captured bytes,
// so save() must serialize from the model.
void forceModelEmission(Assembly& a) {
    a.set_flow_definition(FlowDefinition(a.flow_definition()));
}

} // namespace

// The loader injects its own absolute base path as
// working_directory when the document declares none (ChipletFormat.cpp:547).
// If the flow is then emitted from the model, that host path is written into a
// portable interchange document.
//
// The fixture declares `working_directory: /tmp/flow_test`, so loading it does
// NOT exercise the injection. The document under test must omit the key.
TEST(FlowFidelity, LoaderBasePathDoesNotLeakIntoTheDocument)
{
    const std::string src = slurp(fixture("with_flow.chiplet"));
    const std::string patched =
        src.substr(0, src.find("flow:")) +
        "flow:\n"
        "  steps:\n"
        "    - id: step_hello\n"
        "      tool: /bin/echo\n";

    // A directory whose absolute path is distinctive enough to grep for.
    auto dir = std::filesystem::temp_directory_path() / "flow_basepath_leak";
    std::filesystem::create_directories(dir);
    auto in = dir / "in.chiplet";
    { std::ofstream f(in); f << patched; }

    ChipletFormat fmt;
    auto a = fmt.load(in.string());
    ASSERT_TRUE(a != nullptr);

    // Guard on the INPUT, not on the behaviour under test: this document must
    // genuinely declare no working_directory, or the leak assert below is
    // vacuous. (The shipped fixture declares one, which is why this test
    // patches the block instead of loading the fixture directly.)
    ASSERT_EQ(patched.find("working_directory"), std::string::npos)
        << "the document under test declares a working_directory, so it cannot "
           "testify about what the loader injects when none is declared";

    // What the author declared is what the model carries: nothing.
    EXPECT_TRUE(a->flow_definition().working_directory.empty())
        << "the loader invented a working_directory the document never declared";

    forceModelEmission(*a);

    auto out = dir / "out.chiplet";
    fmt.save(*a, out.string());
    const std::string text = slurp(out.string());
    std::filesystem::remove_all(dir);

    EXPECT_EQ(text.find(dir.string()), std::string::npos)
        << "the loader's absolute base path was written into the document";
}

// has_flow() is !steps.empty(), so a flow carrying only an
// environment (a perfectly legal thing to author) is dropped on save.
TEST(FlowFidelity, AnEnvironmentOnlyFlowSurvivesSave)
{
    ChipletFormat fmt;
    auto a = fmt.load(fixture("with_flow.chiplet"));
    ASSERT_TRUE(a != nullptr);

    FlowDefinition def;
    def.environment["PDK_ROOT"] = "/opt/pdk";
    a->set_flow_definition(std::move(def));

    auto out = std::filesystem::temp_directory_path() / "flow_env_only.chiplet";
    fmt.save(*a, out.string());
    const std::string text = slurp(out.string());
    std::filesystem::remove(out);

    EXPECT_NE(text.find("PDK_ROOT"), std::string::npos)
        << "an environment-only flow was silently deleted on save";
}

// parse_step reads script: only inside `if (node["tool"])`, so a
// step with script: and no tool: loses the script entirely.
TEST(FlowFidelity, AStepWithScriptAndNoToolKeepsItsScript)
{
    const std::string src = slurp(fixture("with_flow.chiplet"));
    const std::string patched =
        src.substr(0, src.find("flow:")) +
        "flow:\n"
        "  steps:\n"
        "    - id: only_script\n"
        "      script: build.py\n";

    auto in =
        std::filesystem::temp_directory_path() / "flow_script_only_in.chiplet";
    { std::ofstream f(in); f << patched; }

    ChipletFormat fmt;
    auto a = fmt.load(in.string());
    ASSERT_TRUE(a != nullptr);
    forceModelEmission(*a);

    auto out =
        std::filesystem::temp_directory_path() / "flow_script_only.chiplet";
    fmt.save(*a, out.string());
    const std::string text = slurp(out.string());
    std::filesystem::remove(in); std::filesystem::remove(out);

    EXPECT_NE(text.find("build.py"), std::string::npos)
        << "a step's script: was silently discarded on the round trip";
}

// Script-only step, live half. Save is not the only consumer: the flow panel's
// Run button reaches build_program, which returned "" for a script-only step
// and started a process with no program. This asserts on the model that such a
// step names no program, which is the fact the engine now acts on.
TEST(FlowFidelity, AScriptOnlyStepNamesNoProgramToRun)
{
    const std::string src = slurp(fixture("with_flow.chiplet"));
    const std::string patched =
        src.substr(0, src.find("flow:")) +
        "flow:\n"
        "  steps:\n"
        "    - id: only_script\n"
        "      script: build.py\n"
        "    - id: with_tool\n"
        "      tool: python3\n"
        "      script: build.py\n";

    auto in =
        std::filesystem::temp_directory_path() / "flow_script_only_run.chiplet";
    { std::ofstream f(in); f << patched; }

    ChipletFormat fmt;
    auto a = fmt.load(in.string());
    ASSERT_TRUE(a != nullptr);
    std::filesystem::remove(in);

    const auto& steps = a->flow_definition().steps;
    ASSERT_EQ(steps.size(), 2u);

    // The script is carried even though no tool: was declared.
    ASSERT_TRUE(steps[0].declared_script.has_value());
    EXPECT_EQ(*steps[0].declared_script, "build.py");
    EXPECT_FALSE(steps[0].declared_tool.has_value());

    // ...and the step is honestly unrunnable rather than silently running "".
    EXPECT_FALSE(steps[0].program().has_value())
        << "a script with no tool resolved to a program, so Run would start it";

    // The ordinary shape is unaffected.
    EXPECT_TRUE(steps[1].program().has_value());
    EXPECT_EQ(*steps[1].program(), "python3");
}

// resolve_variables throws on an unresolvable reference, and
// parse_flow_definition is called from ChipletFormat::load with no catch of its
// own. A flow that names a component the assembly no longer has would therefore
// make the whole document unopenable, in the editor that would let you fix it.
// Unlike the flow defects above, this is on the LOAD path, which every user
// takes.
TEST(FlowFidelity, ADanglingFlowReferenceDoesNotMakeTheDocumentUnopenable)
{
    const std::string src = slurp(fixture("with_flow.chiplet"));
    const std::string patched =
        src.substr(0, src.find("flow:")) +
        "flow:\n"
        "  steps:\n"
        "    - id: uses_missing\n"
        "      tool: /bin/echo\n"
        "      args: [\"${component.no_such_die.id}\"]\n";

    auto in =
        std::filesystem::temp_directory_path() / "flow_unresolved_var.chiplet";
    { std::ofstream f(in); f << patched; }

    ChipletFormat fmt;
    std::unique_ptr<Assembly> a;
    std::string threw;
    try {
        a = fmt.load(in.string());
    } catch (const std::exception& e) {
        threw = e.what();
    }
    std::filesystem::remove(in);

    EXPECT_TRUE(threw.empty())
        << "loading threw, so a dangling flow reference makes the document "
           "impossible to open: " << threw;
    ASSERT_TRUE(a != nullptr) << "no assembly came back";

    // Opening is only half of it. The block the studio could not read must
    // still be there afterwards, or opening the file would quietly cost the
    // user the flow they wrote.
    auto out = std::filesystem::temp_directory_path()
               / "flow_unresolved_var_out.chiplet";
    fmt.save(*a, out.string());
    const std::string written = slurp(out.string());
    std::filesystem::remove(out);

    EXPECT_NE(written.find("${component.no_such_die.id}"), std::string::npos)
        << "saving dropped the flow block the loader could not parse";
    EXPECT_NE(written.find("uses_missing"), std::string::npos)
        << "the step survived neither";
}

// The flow block is OPAQUE and host-specific: the format's schema
// sets additionalProperties true and does not validate build steps, so a step
// without an `id` is a legal document even though this host cannot run it.
// FlowConfig::parse_step throws on a missing id, and that exception used to
// reject the whole document. A block this host cannot interpret must degrade to
// "no runnable flow", never to a document that will not open.
TEST(FlowFidelity, AStepWithoutAnIdDoesNotRejectTheDocument)
{
    const std::string src = slurp(fixture("with_flow.chiplet"));
    const std::string patched =
        src.substr(0, src.find("flow:")) +
        // The exact shape of chiplet-system-spec's own positive fixture
        // conformance/fixtures/v1_0_all_blocks.chiplet: flow-style mappings,
        // name and tool, no id anywhere. A host that rejects this rejects the
        // format's reference document. Flow style is also a different parse
        // path from block style, so it is the shape worth pinning.
        "flow:\n"
        "  steps:\n"
        "    - {name: export, tool: kicad}\n"
        "    - {name: assemble, tool: hyp_to_gds}\n";

    auto in = std::filesystem::temp_directory_path() / "flow_opaque.chiplet";
    { std::ofstream f(in); f << patched; }

    ChipletFormat fmt;
    std::unique_ptr<Assembly> a;
    std::string threw;
    try { a = fmt.load(in.string()); }
    catch (const std::exception& e) { threw = e.what(); }

    EXPECT_TRUE(threw.empty())
        << "a legal document was rejected because this host cannot run its "
           "flow: " << threw;
    ASSERT_TRUE(a != nullptr);

    // The assembly is intact...
    EXPECT_FALSE(a->components().empty()) << "the assembly did not survive";
    // ...the flow is not runnable...
    EXPECT_FALSE(a->has_flow())
        << "a flow this host could not parse is being offered as runnable";
    // ...and the block itself is preserved on the way back out.
    auto out =
        std::filesystem::temp_directory_path() / "flow_opaque_out.chiplet";
    fmt.save(*a, out.string());
    const std::string written = slurp(out.string());
    std::filesystem::remove(in); std::filesystem::remove(out);
    EXPECT_NE(written.find("hyp_to_gds"), std::string::npos)
        << "saving dropped the flow block this host could not interpret";
}

/**
 * test_flow_config.cpp - Unit tests for FlowConfig YAML parsing
 */

#include <gtest/gtest.h>
#include <yaml-cpp/yaml.h>
#include <sstream>

#include "core/flow/FlowConfig.h"
#include "core/flow/FlowEngine.h"
#include "core/flow/FlowStep.h"
#include "core/Assembly.h"
#include "core/Component.h"
#include "core/Technology.h"
#include "formats/ChipletFormat.h"  // ChipletFormatException

using namespace chiplet;

class FlowConfigTest : public ::testing::Test {
protected:
    std::unique_ptr<Assembly> make_test_assembly() {
        auto asm_ = std::make_unique<Assembly>();
        asm_->set_name("TestDesign");
        asm_->set_description("Test assembly");
        asm_->set_author("tester");
        asm_->set_units("um");

        auto die = std::make_unique<Component>("die_0", ComponentType::Die);
        die->set_layout_path("/path/to/die.gds");
        die->set_technology("ihp");
        die->set_name("MainDie");
        die->set_position({100.0, 200.0, 50.0});
        die->set_dimensions({5000.0, 5000.0, 100.0});
        asm_->add_component(std::move(die));

        auto tech = std::make_unique<Technology>("ihp");
        tech->set_description("IHP SG13G2");
        tech->set_layer_properties_path("/path/to/sg13g2.lyp");
        tech->set_dbu(0.001);
        asm_->add_technology(std::move(tech));

        return asm_;
    }

    // Parse YAML into a FlowEngine, populating it via FlowConfig
    void parse_yaml(const std::string& yaml_str, const Assembly& asm_,
                    FlowEngine& engine) {
        YAML::Node node = YAML::Load(yaml_str);
        FlowConfig config;
        config.parse_flow(node, asm_, engine);
    }
};

// -- Basic parsing ----------------------------------------------------------

TEST_F(FlowConfigTest, ParseBasicFlow) {
    auto asm_ = make_test_assembly();
    std::string yaml = R"(
steps:
  - id: step_a
    name: "Step A"
    tool: /bin/echo
    args: ["hello", "world"]
  - id: step_b
    name: "Step B"
    tool: python3
    script: run.py
    args: ["--flag"]
)";

    FlowEngine engine;
    parse_yaml(yaml, *asm_, engine);
    ASSERT_EQ(engine.step_count(), 2u);

    const FlowStep* a = engine.step("step_a");
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(a->name, "Step A");
    EXPECT_EQ(a->tool_path, "/bin/echo");
    EXPECT_TRUE(a->interpreter.empty());
    ASSERT_EQ(a->args.size(), 2u);
    EXPECT_EQ(a->args[0], "hello");
    EXPECT_EQ(a->args[1], "world");

    const FlowStep* b = engine.step("step_b");
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(b->interpreter, "python3");
    EXPECT_EQ(b->tool_path, "run.py");
    ASSERT_EQ(b->args.size(), 1u);
    EXPECT_EQ(b->args[0], "--flag");
}

TEST_F(FlowConfigTest, ParseWorkingDirectory) {
    auto asm_ = make_test_assembly();
    std::string yaml = R"(
working_directory: /tmp/workdir
steps: []
)";

    FlowEngine engine;
    parse_yaml(yaml, *asm_, engine);
    EXPECT_EQ(engine.working_directory(), "/tmp/workdir");
}

TEST_F(FlowConfigTest, ParseEnvironment) {
    auto asm_ = make_test_assembly();
    std::string yaml = R"(
environment:
  PDK_ROOT: /opt/pdk
  DEBUG: "1"
steps: []
)";

    FlowEngine engine;
    parse_yaml(yaml, *asm_, engine);
    auto env = engine.environment();
    ASSERT_EQ(env.size(), 2u);
    EXPECT_EQ(env.at("PDK_ROOT"), "/opt/pdk");
    EXPECT_EQ(env.at("DEBUG"), "1");
}

TEST_F(FlowConfigTest, ParseDependencies) {
    auto asm_ = make_test_assembly();
    std::string yaml = R"(
steps:
  - id: step_a
    tool: /bin/echo
  - id: step_b
    tool: /bin/echo
    depends_on: [step_a]
)";

    FlowEngine engine;
    parse_yaml(yaml, *asm_, engine);
    const FlowStep* b = engine.step("step_b");
    ASSERT_NE(b, nullptr);
    ASSERT_EQ(b->depends_on.size(), 1u);
    EXPECT_EQ(b->depends_on[0], "step_a");
}

TEST_F(FlowConfigTest, ParseInputOutputFiles) {
    auto asm_ = make_test_assembly();
    std::string yaml = R"(
steps:
  - id: convert
    tool: /bin/echo
    input_files: [input.gds, input.lyp]
    output_files: [output.gds]
)";

    FlowEngine engine;
    parse_yaml(yaml, *asm_, engine);
    const FlowStep* s = engine.step("convert");
    ASSERT_NE(s, nullptr);
    ASSERT_EQ(s->input_files.size(), 2u);
    EXPECT_EQ(s->input_files[0], "input.gds");
    ASSERT_EQ(s->output_files.size(), 1u);
    EXPECT_EQ(s->output_files[0], "output.gds");
}

TEST_F(FlowConfigTest, NameDefaultsToId) {
    auto asm_ = make_test_assembly();
    std::string yaml = R"(
steps:
  - id: my_step
    tool: /bin/echo
)";

    FlowEngine engine;
    parse_yaml(yaml, *asm_, engine);
    EXPECT_EQ(engine.step("my_step")->name, "my_step");
}

// -- Variable substitution --------------------------------------------------

TEST_F(FlowConfigTest, VarSubstitutionAssembly) {
    auto asm_ = make_test_assembly();
    std::string yaml = R"(
steps:
  - id: test
    tool: /bin/echo
    args: ["${assembly.name}", "${assembly.units}"]
)";

    FlowEngine engine;
    parse_yaml(yaml, *asm_, engine);
    const FlowStep* s = engine.step("test");
    ASSERT_EQ(s->args.size(), 2u);
    EXPECT_EQ(s->args[0], "TestDesign");
    EXPECT_EQ(s->args[1], "um");
}

TEST_F(FlowConfigTest, VarSubstitutionComponent) {
    auto asm_ = make_test_assembly();
    std::string yaml = R"(
steps:
  - id: test
    tool: /bin/echo
    args: ["${component.die_0.layout_path}", "${component.die_0.name}"]
)";

    FlowEngine engine;
    parse_yaml(yaml, *asm_, engine);
    const FlowStep* s = engine.step("test");
    ASSERT_EQ(s->args.size(), 2u);
    EXPECT_EQ(s->args[0], "/path/to/die.gds");
    EXPECT_EQ(s->args[1], "MainDie");
}

TEST_F(FlowConfigTest, VarSubstitutionTechnology) {
    auto asm_ = make_test_assembly();
    std::string yaml = R"(
steps:
  - id: test
    tool: /bin/echo
    args: ["${technology.ihp.layer_properties_path}"]
)";

    FlowEngine engine;
    parse_yaml(yaml, *asm_, engine);
    const FlowStep* s = engine.step("test");
    ASSERT_EQ(s->args.size(), 1u);
    EXPECT_EQ(s->args[0], "/path/to/sg13g2.lyp");
}

TEST_F(FlowConfigTest, VarSubstitutionComponentPosition) {
    auto asm_ = make_test_assembly();
    std::string yaml = R"(
steps:
  - id: test
    tool: /bin/echo
    args: ["${component.die_0.position.x}"]
)";

    FlowEngine engine;
    parse_yaml(yaml, *asm_, engine);
    const FlowStep* s = engine.step("test");
    ASSERT_EQ(s->args.size(), 1u);
    // std::to_string(100.0) produces "100.000000"
    EXPECT_EQ(s->args[0], "100.000000");
}

TEST_F(FlowConfigTest, VarSubstitutionInToolScript) {
    auto asm_ = make_test_assembly();
    std::string yaml = R"(
steps:
  - id: test
    tool: python3
    script: "${component.die_0.layout_path}"
)";

    FlowEngine engine;
    parse_yaml(yaml, *asm_, engine);
    const FlowStep* s = engine.step("test");
    EXPECT_EQ(s->tool_path, "/path/to/die.gds");
    EXPECT_EQ(s->interpreter, "python3");
}

TEST_F(FlowConfigTest, VarSubstitutionMixed) {
    auto asm_ = make_test_assembly();
    std::string yaml = R"(
steps:
  - id: test
    tool: /bin/echo
    args: ["prefix_${assembly.name}_suffix"]
)";

    FlowEngine engine;
    parse_yaml(yaml, *asm_, engine);
    EXPECT_EQ(engine.step("test")->args[0], "prefix_TestDesign_suffix");
}

TEST_F(FlowConfigTest, VarSubstitutionWorkingDir) {
    auto asm_ = make_test_assembly();
    std::string yaml = R"(
working_directory: "/work/${assembly.name}"
steps: []
)";

    FlowEngine engine;
    parse_yaml(yaml, *asm_, engine);
    EXPECT_EQ(engine.working_directory(), "/work/TestDesign");
}

// -- Error cases ------------------------------------------------------------

TEST_F(FlowConfigTest, UnresolvedComponentThrows) {
    auto asm_ = make_test_assembly();
    std::string yaml = R"(
steps:
  - id: test
    tool: /bin/echo
    args: ["${component.nonexistent.layout_path}"]
)";

    FlowEngine engine;
    EXPECT_THROW(parse_yaml(yaml, *asm_, engine), ChipletFormatException);
}

TEST_F(FlowConfigTest, UnresolvedTechnologyThrows) {
    auto asm_ = make_test_assembly();
    std::string yaml = R"(
steps:
  - id: test
    tool: /bin/echo
    args: ["${technology.unknown.dbu}"]
)";

    FlowEngine engine;
    EXPECT_THROW(parse_yaml(yaml, *asm_, engine), ChipletFormatException);
}

TEST_F(FlowConfigTest, UnknownCategoryThrows) {
    auto asm_ = make_test_assembly();
    std::string yaml = R"(
steps:
  - id: test
    tool: /bin/echo
    args: ["${badcategory.foo.bar}"]
)";

    FlowEngine engine;
    EXPECT_THROW(parse_yaml(yaml, *asm_, engine), ChipletFormatException);
}

TEST_F(FlowConfigTest, UnknownFieldThrows) {
    auto asm_ = make_test_assembly();
    std::string yaml = R"(
steps:
  - id: test
    tool: /bin/echo
    args: ["${component.die_0.nonexistent_field}"]
)";

    FlowEngine engine;
    EXPECT_THROW(parse_yaml(yaml, *asm_, engine), ChipletFormatException);
}

TEST_F(FlowConfigTest, MissingIdThrows) {
    auto asm_ = make_test_assembly();
    std::string yaml = R"(
steps:
  - name: "No ID"
    tool: /bin/echo
)";

    FlowEngine engine;
    EXPECT_THROW(parse_yaml(yaml, *asm_, engine), ChipletFormatException);
}

TEST_F(FlowConfigTest, StepsNotSequenceThrows) {
    auto asm_ = make_test_assembly();
    std::string yaml = R"(
steps:
  key: value
)";

    FlowEngine engine;
    EXPECT_THROW(parse_yaml(yaml, *asm_, engine), ChipletFormatException);
}

// -- Tool/script mapping ----------------------------------------------------

TEST_F(FlowConfigTest, ToolScriptMapping) {
    auto asm_ = make_test_assembly();
    std::string yaml = R"(
steps:
  - id: test
    tool: python3
    script: my_script.py
    args: ["--flag"]
)";

    FlowEngine engine;
    parse_yaml(yaml, *asm_, engine);
    const FlowStep* s = engine.step("test");
    EXPECT_EQ(s->interpreter, "python3");
    EXPECT_EQ(s->tool_path, "my_script.py");
}

TEST_F(FlowConfigTest, ToolOnlyMapping) {
    auto asm_ = make_test_assembly();
    std::string yaml = R"(
steps:
  - id: test
    tool: /usr/bin/mytool
    args: ["arg1"]
)";

    FlowEngine engine;
    parse_yaml(yaml, *asm_, engine);
    const FlowStep* s = engine.step("test");
    EXPECT_TRUE(s->interpreter.empty());
    EXPECT_EQ(s->tool_path, "/usr/bin/mytool");
}

// -- Round-trip test --------------------------------------------------------

TEST_F(FlowConfigTest, WriteFlowRoundtrip) {
    // Build engine manually
    FlowEngine original;
    original.set_working_directory("/tmp/test");
    original.set_environment("KEY", "value");

    FlowStep a;
    a.id = "step_a";
    a.name = "Step A";
    a.interpreter = "python3";
    a.tool_path = "script.py";
    a.args = {"--input", "file.gds"};
    a.depends_on = {};
    original.add_step(a);

    FlowStep b;
    b.id = "step_b";
    b.name = "step_b";  // same as id, should not be written
    b.tool_path = "/bin/echo";
    b.args = {"done"};
    b.depends_on = {"step_a"};
    original.add_step(b);

    // Write to YAML
    YAML::Emitter out;
    out << YAML::BeginMap;
    FlowConfig config;
    config.write_flow(original, out);
    out << YAML::EndMap;

    // Parse back
    YAML::Node root = YAML::Load(out.c_str());
    ASSERT_TRUE(root["flow"]);

    FlowEngine loaded;
    auto asm_ = make_test_assembly();
    config.parse_flow(root["flow"], *asm_, loaded);

    // Compare
    EXPECT_EQ(loaded.working_directory(), "/tmp/test");
    EXPECT_EQ(loaded.environment().at("KEY"), "value");
    ASSERT_EQ(loaded.step_count(), 2u);

    const FlowStep* la = loaded.step("step_a");
    ASSERT_NE(la, nullptr);
    EXPECT_EQ(la->name, "Step A");
    EXPECT_EQ(la->interpreter, "python3");
    EXPECT_EQ(la->tool_path, "script.py");
    ASSERT_EQ(la->args.size(), 2u);
    EXPECT_EQ(la->args[0], "--input");
    EXPECT_EQ(la->args[1], "file.gds");

    const FlowStep* lb = loaded.step("step_b");
    ASSERT_NE(lb, nullptr);
    EXPECT_EQ(lb->tool_path, "/bin/echo");
    ASSERT_EQ(lb->depends_on.size(), 1u);
    EXPECT_EQ(lb->depends_on[0], "step_a");
}

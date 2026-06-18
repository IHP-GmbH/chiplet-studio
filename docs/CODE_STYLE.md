# Code Style Guide

This document defines the coding patterns for chiplet-studio. The conventions follow KLayout's style, since the app links KLayout's `db`/`tl`/`lay` libraries directly.

**Reference:** Study `extern/klayout/src/` for examples (see the table at the end).

There is no `.clang-format` in the tree, so these rules are hand-applied. When in doubt, match the surrounding file.

---

## File Header (SPDX)

Every `.h` and `.cpp` file starts with the SPDX header, before the include guard or any code:

```cpp
// SPDX-FileCopyrightText: 2026 IHP GmbH
// SPDX-License-Identifier: GPL-3.0-or-later
```

This is mandatory and machine-checked by tooling; new files without it are non-conformant.

---

## CRITICAL: Robustness and Stability

A crash in a design tool can cost hours of unsaved layout work and destroys user trust, so a segfault, null deref, or unhandled exception is treated as a release-blocking bug, not a cosmetic one. The view layer in particular must survive running headless, without KLayout, or with a half-initialized widget. The patterns below are how we keep that guarantee.

### 1. Defensive, lazy initialization

Never construct a heavy resource (a KLayout view widget, a GL context) in a constructor that may run headless. Defer it and gate it on an environment check, so the object is always constructible.

```cpp
// WRONG: assumes a display is available; crashes headless
KLayout2DView::KLayout2DView() {
    m_viewWidget = new lay::LayoutViewWidget(...);
}

// CORRECT: lazy init, returns true only once the widget is ready
bool KLayout2DView::ensureViewWidget() {
    if (m_initAttempted && !m_viewAvailable) return false;
    m_initAttempted = true;
    if (!isDisplayAvailable()) {
        qWarning("No display available");
        return false;
    }
    try {
        m_viewWidget = new lay::LayoutViewWidget(...);
        m_viewAvailable = true;
        return true;
    } catch (...) {
        m_viewAvailable = false;
        return false;
    }
}
```

### 2. Null checks before every raw-pointer use

```cpp
void process(Component* comp) {
    if (!comp) return;       // never deref a pointer you did not just allocate
    use(comp->name());
}
```

### 3. Public methods safe in any object state

A method must not assume the object is fully initialized. It either no-ops or returns a safe default when its resources are missing.

```cpp
void KLayout2DView::zoomFit() {
    if (m_viewWidget && m_viewWidget->view()) {
        m_viewWidget->view()->zoom_fit();
    }
    // silent no-op if the widget was never created
}
```

### 4. Graceful degradation, not failure

When an optional dependency is absent, return a safe value rather than throwing. Guard KLayout-specific code with `HAVE_KLAYOUT`.

```cpp
bool KLayout2DView::hasLayout() const {
#ifdef HAVE_KLAYOUT
    if (m_viewWidget && m_viewWidget->view()) {
        return m_viewWidget->view()->cellviews() > 0;
    }
#endif
    return false;  // safe default when KLayout is compiled out
}
```

### 5. Wrap external library calls in try/catch

KLayout and yaml-cpp throw. Catch at the boundary and recover to a safe state.

```cpp
try {
    externalLibrary->riskyOperation();
} catch (const std::exception& e) {
    qWarning("Operation failed: %s", e.what());
} catch (...) {
    qWarning("Unknown error in operation");
}
```

### Testing

- Cover error paths, not just the happy path.
- Tests must pass with a display, headless, and with or without KLayout. Run headless with `QT_QPA_PLATFORM=offscreen`.
- No test may crash, even tests of error conditions.
- Skip environment-dependent tests with `GTEST_SKIP()`; do not disable them.

### Pre-merge checklist

- [ ] No raw-pointer deref without a null check.
- [ ] External library calls wrapped in try/catch.
- [ ] Public methods safe to call in any object state.
- [ ] Tests pass headless (`QT_QPA_PLATFORM=offscreen`).
- [ ] No crash when a display, file, or library is unavailable.
- [ ] Errors reported via `qWarning`, not swallowed silently.

---

## Member Variable Naming

Members use the `m_` prefix; statics use `s_`. This is one place where we diverge slightly from KLayout's older `mp_`/`ms_` forms.

```cpp
m_name        // member, any type (value, smart pointer, or raw pointer)
s_something   // static member or static local
```

```cpp
class Component {
private:
    std::string m_id;                       // value
    std::unique_ptr<Data> m_data;           // owning smart pointer, still m_
    Widget* m_widget = nullptr;             // raw pointer, still m_

    static const string_type s_emptyString; // static member
};
```

`m_` is the dominant prefix across `core/`, `ui/`, and `view3d/` for every kind of member, raw or smart pointer included. The older KLayout `mp_` prefix survives only inside one PIMPL, `src/view2d/KLayoutBridge.cpp` (e.g. `std::unique_ptr<db::Layout> mp_layout;`). Do not introduce `mp_` or `ms_` in new code; use `m_` and `s_`.

## Getter/Setter Style

The codebase deliberately follows two conventions, by layer:

**Core domain classes (`src/core/`)** use KLayout-style snake_case: a bare property name for the getter, a `set_` prefix for the setter.

```cpp
const std::string& name() const { return m_name; }   // getter: property name
void set_name(const std::string& name);              // setter: set_ prefix
```

This holds for `Component`, `Assembly`, `Technology`, `IOPad`, and the rest of `core/`.

**Qt-derived UI and view classes (`src/ui/`, `src/view3d/`)** follow Qt's own camelCase `setX` convention, because they sit alongside Qt's API.

```cpp
void setLightDirection(const VECTOR3D& dir);   // SceneManager
void setComponent(const ComponentID& id, Assembly* a);  // PropertiesPanel
void setRunningState(bool running);            // FlowPanel
```

Pick the convention that matches the file's layer. Do not "correct" a Qt widget to snake_case, and do not add camelCase setters to a core domain class. (`LayerStackup` is a core file that exposes a free function `setConfigsDir` in camelCase; treat that as a local exception, not a model to copy.)

Never use a `get` prefix on a getter in either layer:

```cpp
std::string getName() const;   // WRONG in every layer
```

## Type Definitions

Place type aliases in the public section at the top of the class. Core classes use `typedef`:

```cpp
class Assembly {
public:
    typedef std::string string_type;
    typedef std::vector<std::unique_ptr<Component>> component_list_type;

private:
    string_type m_name;
    component_list_type m_components;
};
```

`typedef` is the prevailing form in `core/`. A few newer command/value types use `using` aliases instead (e.g. `Command.h`, `ComponentID.h`); both are acceptable, but match the file you are editing.

## Error Handling

Use exceptions, not error codes or out-params.

The `.chiplet` parser raises `ChipletFormatException`, which carries a message plus an optional source line and context so callers can report where a file went wrong. The real signature lives in `src/formats/ChipletFormat.h`:

```cpp
class ChipletFormatException : public std::exception {
public:
    typedef std::string string_type;

    ChipletFormatException(const string_type& msg,
                           size_t line = 0,
                           const string_type& context = "");

    const char* what() const noexcept override;
    size_t line() const { return m_line; }
    const string_type& context() const { return m_context; }

private:
    string_type m_message;   // built in the constructor from msg + context + line
    size_t m_line;
    string_type m_context;
};
```

Callers catch at the load boundary:

```cpp
try {
    auto assembly = format.load("file.chiplet");
} catch (const ChipletFormatException& e) {
    std::cerr << "Parse error: " << e.what() << std::endl;
}
```

Do not return null-with-error-flag or expose a `hasError()` query:

```cpp
// WRONG
if (!parse_ok) { m_errorMessage = "Failed"; return nullptr; }
if (format.hasError()) { ... }
```

### Parsing is vendored; do not hand-write it

`.chiplet` field-level parsing no longer lives in this repo. It is delegated to the vendored, Apache-2.0 reference library under `src/formats/chiplet_format_io/` (see its `VENDORED.md`; the canonical spec is `IHP-GmbH/chiplet-spec`). That library depends only on yaml-cpp, with no Qt or KLayout.

`src/formats/ChipletFormat.*` is a thin consumer: it calls the library, maps the resulting `chiplet_format_io::ChipletDocument` into `core/Assembly`, and adds the studio-only concerns the library deliberately omits (path resolution, `qWarning` diagnostics, z auto-calculation, techfile auto-load, and flow parsing). The only raw `YAML::Node` use left in `ChipletFormat.cpp` is for the embedded flow YAML.

When you extend the format, change the vendored library and the spec, not a bespoke `YAML::Node` walk in the host. `ChipletFormat`, the contract docs (`coord_frame_contract`, `interconnect_render_contract`, `CHIPLET_FORMAT_SPEC`), and their tests (`tests/test_coord_frame_contract.cpp`, `tests/test_chiplet_format*.cpp`) are a downstream contract surface consumed by adk-tools as a submodule; keep them in sync.

## Class Structure

```cpp
class MyClass {
public:
    // 1. Type definitions
    typedef std::string string_type;

    // 2. Constructors and destructor
    MyClass();
    explicit MyClass(const string_type& name);
    ~MyClass();

    // 3. Public methods: getters, then setters, then operations
    const string_type& name() const;
    void set_name(const string_type& name);
    void process();

private:
    // 4. Friend declarations (if any)
    friend class OtherClass;

    // 5. Private methods
    void internal_helper();

    // 6. Member variables
    string_type m_name;
};
```

## Include Order

```cpp
// 1. Corresponding header (for .cpp files)
#include "MyClass.h"

// 2. Project headers
#include "core/Assembly.h"
#include "formats/ChipletFormat.h"

// 3. External library headers
#include <chiplet_format_io/chiplet_format_io.hpp>
#include <yaml-cpp/yaml.h>
#include <QDebug>

// 4. Standard library headers
#include <string>
#include <memory>
#include <filesystem>
```

This matches `src/core/Assembly.cpp` and `src/formats/ChipletFormat.cpp`.

## Spacing and Braces

Follow KLayout's style. There is no formatter enforcing this, so apply it by hand.

```cpp
// Brace on the same line
if (condition) {
    do_something();
}

// Space after a keyword
if (x)
for (auto& item : list)
while (running)

// No space before the parenthesis in a call
do_something(arg1, arg2);

// Space around binary operators
int x = a + b;
if (a == b)
```

## KLayout Reference Files

Study these for pattern examples:

| File | Pattern Example |
|------|-----------------|
| `extern/klayout/src/db/db/dbCell.h` | Class structure, member naming |
| `extern/klayout/src/db/db/dbLayout.h` | Data model patterns |
| `extern/klayout/src/db/db/dbTechnology.h` | Technology class |
| `extern/klayout/src/plugins/streamers/gds2/db_plugin/dbGDS2Reader.h` | Reader/parser pattern |
| `extern/klayout/src/tl/tl/tlException.h` | Exception base class |

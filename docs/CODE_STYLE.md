# Code Style Guide

This document defines the coding patterns for chiplet-studio, based on KLayout's conventions.

**Reference:** Study `extern/klayout/src/` for examples of these patterns.

---

## CRITICAL: Robustness and Stability Requirements

**This application is designed for complex system design. Robustness and stability are non-negotiable requirements.**

### Zero Tolerance for Crashes

Segmentation faults, null pointer dereferences, and unhandled exceptions are **absolutely unacceptable**. A crash in a design tool can result in hours of lost work and destroys user trust.

**Mandatory practices:**

1. **Defensive initialization** - Never assume resources are available
   ```cpp
   // WRONG: Assumes display is available
   KLayout2DView::KLayout2DView() {
       m_viewWidget = new lay::LayoutViewWidget(...);  // May crash headless
   }

   // CORRECT: Lazy initialization with environment checking
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

2. **Null pointer checks** - Always verify pointers before use
   ```cpp
   // WRONG
   void process(Component* comp) {
       return comp->name();  // Crash if null
   }

   // CORRECT
   void process(Component* comp) {
       if (!comp) return;
       return comp->name();
   }
   ```

3. **Safe method calls** - Methods must be safe to call in any state
   ```cpp
   // All public methods must be safe even without initialization
   void KLayout2DView::zoomFit() {
       if (m_viewWidget && m_viewWidget->view()) {
           m_viewWidget->view()->zoom_fit();
       }
       // No crash if widget not initialized
   }
   ```

4. **Graceful degradation** - Features should degrade, not crash
   ```cpp
   // If KLayout not available, return safe defaults
   bool KLayout2DView::hasLayout() const {
   #ifdef HAVE_KLAYOUT
       if (m_viewWidget && m_viewWidget->view()) {
           return m_viewWidget->view()->cellviews() > 0;
       }
   #endif
       return false;  // Safe default
   }
   ```

5. **Exception safety** - Wrap external library calls in try/catch
   ```cpp
   try {
       externalLibrary->riskyOperation();
   } catch (const std::exception& e) {
       qWarning("Operation failed: %s", e.what());
       // Recover to safe state
   } catch (...) {
       qWarning("Unknown error in operation");
       // Recover to safe state
   }
   ```

### Testing Requirements

- **All code paths must be tested**, including error cases
- **Tests must pass in all environments**: with display, headless, with/without KLayout
- **No test should crash** - even tests for error conditions must pass gracefully
- **Use GTEST_SKIP() for environment-dependent tests**, not disabled tests

### Code Review Checklist for Robustness

Before merging any code, verify:
- [ ] No raw pointer dereferences without null checks
- [ ] All external library calls wrapped in try/catch
- [ ] All public methods safe to call in any object state
- [ ] Tests pass in headless mode (QT_QPA_PLATFORM=offscreen)
- [ ] No crashes when resources unavailable (display, files, libraries)
- [ ] Graceful error messages instead of silent failures

---

## Member Variable Naming

```cpp
m_name      // member value
mp_pointer  // member pointer (raw)
ms_static   // static member
```

Examples:
```cpp
class Component {
private:
    std::string m_id;                           // value
    std::unique_ptr<Layout> mp_layout;          // owning pointer (use std:: not mp_ for smart ptrs)
    static int ms_instance_count;               // static

    // For std::unique_ptr and std::shared_ptr, use m_ prefix
    std::unique_ptr<Data> m_data;
};
```

## Getter/Setter Style

```cpp
// Getters: just the property name
const std::string& name() const { return m_name; }
double dbu() const { return m_dbu; }

// Setters: set_ prefix with underscore
void set_name(const std::string& name) { m_name = name; }
void set_dbu(double dbu) { m_dbu = dbu; }
```

**NOT:**
```cpp
void setName(const std::string& name);  // Wrong - camelCase
std::string getName() const;            // Wrong - get prefix
```

## Type Definitions

Place typedefs in the public section at the top of the class:

```cpp
class Assembly {
public:
    typedef std::string string_type;
    typedef std::vector<std::unique_ptr<Component>> component_list_type;

    // ... methods ...

private:
    string_type m_name;
    component_list_type m_components;
};
```

## Error Handling

Use exceptions, not error codes:

```cpp
// Exception class with context
class ChipletFormatException : public std::exception {
public:
    ChipletFormatException(const std::string& msg,
                           size_t line = 0,
                           const std::string& context = "")
        : m_msg(format_message(msg, line, context)) {}

    const char* what() const noexcept override { return m_msg.c_str(); }

private:
    std::string m_msg;

    static std::string format_message(const std::string& msg,
                                      size_t line,
                                      const std::string& context);
};

// Usage in parser
void parse_component(const YAML::Node& node) {
    if (!node["id"]) {
        throw ChipletFormatException("Missing required field 'id'",
                                     node.Mark().line,
                                     "component");
    }
    // ...
}

// Caller handles exceptions
try {
    auto assembly = format.load("file.chiplet");
} catch (const ChipletFormatException& e) {
    std::cerr << "Parse error: " << e.what() << std::endl;
}
```

**NOT:**
```cpp
// Wrong - error codes
if (!parse_ok) {
    m_errorMessage = "Failed";
    return nullptr;
}
if (format.hasError()) { ... }
```

## Class Structure

Follow this order in class definitions:

```cpp
class MyClass {
public:
    // 1. Type definitions
    typedef std::string string_type;

    // 2. Constructors and destructor
    MyClass();
    explicit MyClass(const string_type& name);
    ~MyClass();

    // 3. Public methods (getters first, then setters, then operations)
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
#include <yaml-cpp/yaml.h>

// 4. Standard library headers
#include <string>
#include <vector>
#include <memory>
```

## Spacing and Braces

Follow KLayout's style:

```cpp
// Braces on same line for short blocks
if (condition) {
    do_something();
}

// Space after keywords
if (x)
for (auto& item : list)
while (running)

// No space before parentheses in function calls
do_something(arg1, arg2);

// Space around operators
int x = a + b;
if (a == b)
```

## KLayout Reference Files

Study these files for pattern examples:

| File | Pattern Example |
|------|-----------------|
| `extern/klayout/src/db/db/dbCell.h` | Class structure, member naming |
| `extern/klayout/src/db/db/dbLayout.h` | Data model patterns |
| `extern/klayout/src/db/db/dbTechnology.h` | Technology class |
| `extern/klayout/src/plugins/streamers/gds2/db_plugin/dbGDS2Reader.h` | Reader/parser pattern |
| `extern/klayout/src/tl/tl/tlException.h` | Exception base class |

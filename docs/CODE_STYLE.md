# Code Style Guide

This document defines the coding patterns for chiplet-studio, based on KLayout's conventions.

**Reference:** Study `extern/klayout/src/` for examples of these patterns.

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

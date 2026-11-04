# PyDict++: Python-Inspired Dictionary Library for C++

A header-only C++17 library that brings Python dictionary usability and syntax to C++ using a custom dual-chain hash table (without `std::unordered_map` or `std::map`).

---

## 📁 Project Structure (Only 3 Files)

```
PyDictPlusPlus/
├── pydict.hpp    # Complete, self-contained single-header library
├── main.cpp      # Showcase application demonstrating all features
└── README.md     # Project documentation
```

---

## ⚡ How to Run

Open your VS Code terminal in this folder and run:

```powershell
# Compile and run in one command:
g++ -std=c++17 main.cpp -o main.exe && .\main.exe
```

Or just run the compiled binary:
```powershell
.\main.exe
```

---

## 💡 Quick Code Example

```cpp
#include <iostream>
#include <string>
#include "pydict.hpp" // Or #include <pydict>

using namespace std;

int main() {
    // 1. Natural Python syntax
    pydict<string, int> marks = {{"Alice", 95}, {"Bob", 88}};
    marks["Charlie"] = 92;

    // 2. Stream print (like iostream)
    cout << marks << endl; // Output: {'Alice': 95, 'Bob': 88, 'Charlie': 92}

    // 3. Dictionary merge
    pydict<string, int> bonus = {{"Diana", 99}};
    auto combined = marks | bonus;

    // 4. Undo support
    combined.insert("Test", 50);
    combined.undo(); // Reverts 'Test'

    // 5. Insertion-order iteration
    for (const auto& [name, score] : combined) {
        cout << name << " -> " << score << endl;
    }

    return 0;
}
```

---

## 🌟 Implemented Features

1. **All 16 Core Python Methods**: `insert`, `erase`, `get`, `contains`, `operator[]`, `size`, `empty`, `clear`, `keys()`, `values()`, `items()`, `update()`, `copy()`, `setdefault()`, `pop()`, `popitem()`.
2. **Advanced Features**:
   - Merge (`+`, `+=`, `|`, `|=`) & Difference (`-`, `-=`)
   - Operation History & Undo / Redo (`undo()`, `redo()`)
   - In-Place Sorting (`sortByKey()`, `sortByValue()`)
   - Value Search (`findValue()`)
   - JSON Export / Import (`saveToJSON()`, `loadFromJSON()`)
   - Stream I/O (`cin >> dict` and `cout << dict`)
   - Dynamic Nested Dictionaries (`PyAnyDict`)
   - Collision Tracking & Auto-Rehashing

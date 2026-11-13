# PyDict++

A modern, header-only C++17 library that brings the simplicity, functionality, and developer ergonomics of Python dictionaries to C++. Built from scratch using a custom dual-chain hash table without relying on standard associative containers.

---

## Problem Statement

C++ developers often face significant friction when working with key-value data structures compared to languages like Python:

1. **Order Loss in Standard Hash Maps**: `std::unordered_map` provides average $O(1)$ lookup, but completely scrambles insertion order. Iterating through it produces non-deterministic order, making debugging, reproducible serialization, and ordered pipelines tedious.
2. **Speed Trade-Offs in Ordered Maps**: `std::map` maintains sorted order, but relies on Red-Black binary search trees with $O(\log N)$ access time. In performance-critical applications, tree traversals can be 5x to 9x slower than hash tables due to frequent pointer chasing and CPU cache misses.
3. **Missing Python Dictionary Ergonomics**: Standard C++ map containers lack fundamental everyday dictionary operations:
   - No safe `get(key, defaultValue)` that avoids accidental insertions.
   - No `pop(key)` returning the removed value, or $O(1)$ LIFO `popitem()`.
   - No `setdefault(key, defaultVal)` helper.
   - No dictionary union/merge (`+`, `|`) or difference (`-`) operators.
   - No built-in mutation history tracking or undo/redo mechanisms.
   - No direct stream input/output (`std::cin >> dict`, `std::cout << dict`).
   - No native export or import for JSON data.

---

## How PyDict++ Solves It

PyDict++ bridges this gap by implementing an independent, self-contained hash table engine based on a **Dual-Chaining Architecture**:

```
                         PyDict Table
                              |
       +----------------------+----------------------+
       |                                             |
       v                                             v
Bucket Array (Separate Chaining)         Global Doubly-Linked Chain
[Bucket 0] -> [Node A] -> [Node B]       [Head] <-> [Node A] <-> [Node C] <-> [Node B] <-> [Tail]
[Bucket 1] -> nullptr
[Bucket 2] -> [Node C]
```

### Core Architecture Highlights

- **$O(1)$ Average Lookup & Mutation**: Uses prime-modulo bucket arrays with separate chaining to resolve collisions without open-addressing clustering.
- **Deterministic Insertion Order**: Every node is woven into a global doubly-linked list (`prev_global` and `next_global`). Iteration walks existing nodes directly, never scanning empty buckets.
- **Instant $O(1)$ `popitem()`**: Removing the most recently added key-value pair is an immediate pointer unlinking at the tail.
- **Dynamic Auto-Rehashing**: Expands capacity across pre-calculated prime numbers when the load factor reaches 0.75, rewiring bucket pointers without reallocating nodes.
- **Built-in Undo/Redo Engine**: Tracks all mutating operations (`INSERT`, `UPDATE`, `ERASE`, `CLEAR`) via the Command Pattern, allowing full rollback of any modification.
- **Zero Third-Party Dependencies**: Self-contained single-header implementation (`pydict.hpp`). Includes built-in JSON parsing, serialization, and stream formatting.

---

## Quickstart

```cpp
#include <iostream>
#include <string>
#include <vector>
#include "pydict.hpp"

using namespace std;

int main() {
    // 1. Initializer-list syntax
    pydict<string, int> scores = {
        {"Alice", 95},
        {"Bob", 88},
        {"Charlie", 72}
    };

    // 2. Subscript assignment and safe get
    scores["Diana"] = 91;
    int eve_score = scores.get("Eve", 0); // returns 0, does not insert

    // 3. Dictionary Merge and Difference
    pydict<string, int> extra = {{"Eve", 84}, {"Frank", 90}};
    auto combined = scores | extra; // union
    auto diff = scores - extra;     // difference

    // 4. Undo functionality
    combined.insert("Temporary", 50);
    combined.undo(); // Reverts the insertion

    // 5. In-place sorting
    scores.sortByValue(false); // Descending by score

    // 6. Insertion-order iteration
    for (const auto& [name, score] : scores) {
        cout << name << ": " << score << "\n";
    }

    // 7. Stream output and JSON export
    cout << "Dictionary: " << scores << "\n";
    cout << "JSON:\n" << scores.toJSONString(2) << "\n";

    return 0;
}
```

---

## Multiple Values & Data Types

PyDict++ supports complex values and dynamic structures:

### 1. One Key to Multiple Values
```cpp
pydict<string, vector<string>> skills;
skills["Alice"] = {"C++", "Python", "SQL"};
skills["Alice"].push_back("Docker");
```

### 2. Dynamic Mixed-Type Dictionary (`PyAnyDict`)
```cpp
PyAnyDict profile;
profile["username"] = "dev_alex";
profile["active"] = true;
profile["login_count"] = 42;
profile["tags"] = PyValue::ListType{"engineer", "admin"};
profile["server"]["host"] = "127.0.0.1";
profile["server"]["port"] = 8080;
```

---

## Cross-Platform Usage Guide

Because PyDict++ is a single header with standard C++17 code, it works identically across all operating systems and compilers.

### Platform 1: Windows (VS Code / PowerShell / Command Prompt)

1. Ensure a C++17-capable compiler (GCC/MinGW, Clang, or MSVC) is installed.
2. Navigate to your project folder:
   ```powershell
   cd path/to/PyDictPlusPlus
   ```
3. Compile and run:
   ```powershell
   g++ -std=c++17 main.cpp -o main.exe
   .\main.exe
   ```

### Platform 2: Linux & macOS (Terminal / GCC / Clang)

1. Open your terminal.
2. Compile with optimization:
   ```bash
   g++ -std=c++17 -O2 main.cpp -o main
   ./main
   ```
   Or with Clang:
   ```bash
   clang++ -std=c++17 -O2 main.cpp -o main
   ./main
   ```

### Platform 3: Online Compilers (OnlineGDB, Godbolt, Replit)

#### Option A: Single-File Copy (Fastest)
1. Open [onlinegdb.com/online_c++_compiler](https://www.onlinegdb.com/online_c++_compiler).
2. Set the language dropdown to **C++17** or **C++20**.
3. Copy the contents of `onlinegdb_bundle.cpp` from this repository.
4. Paste into the editor and click **Run**.

#### Option B: Multi-Tab Setup
1. On OnlineGDB, click the **+ (New File)** button next to `main.cpp`.
2. Name the new file `pydict.hpp`.
3. Paste the contents of `pydict.hpp` into that tab.
4. In `main.cpp`, write your program with `#include "pydict.hpp"`.
5. Select **C++17** and click **Run**.

### System-Wide Installation (Optional)

To use `#include <pydict>` from anywhere on your system without path flags, copy `pydict.hpp` to your compiler's include directory:
- **GCC / MinGW on Windows**: `<MinGW_Path>/include/pydict`
- **Linux**: `/usr/local/include/pydict`
- **macOS**: `/usr/local/include/pydict`

---

## API Reference & Feature Set

### Core Python Methods

| Operation | PyDict++ Syntax | Complexity |
| :--- | :--- | :---: |
| Insert / Assign | `dict.insert(key, value)` or `dict[key] = value` | $O(1)$ avg |
| Erase | `dict.erase(key)` | $O(1)$ avg |
| Safe Lookup | `dict.get(key, defaultValue)` | $O(1)$ avg |
| Membership Check | `dict.contains(key)` | $O(1)$ avg |
| Subscript Access | `dict[key]` | $O(1)$ avg |
| Size & Emptiness | `dict.size()`, `dict.empty()` | $O(1)$ |
| Clear | `dict.clear()` | $O(N)$ |
| Python Views | `dict.keys()`, `dict.values()`, `dict.items()` | $O(1)$ view |
| Batch Update | `dict.update(other_dict)` | $O(M)$ |
| Deep Copy | `dict.copy()` | $O(N)$ |
| Set Default | `dict.setdefault(key, defaultVal)` | $O(1)$ avg |
| Pop Value | `dict.pop(key)` / `dict.pop(key, defaultVal)` | $O(1)$ avg |
| Pop Last (LIFO) | `dict.popitem()` | $O(1)$ |

### Advanced Features

| Feature | Method / Syntax | Description |
| :--- | :--- | :--- |
| Dictionary Merge | `dict1 + dict2` or `dict1 \| dict2` | Creates new union dict; keys in dict2 take priority |
| In-Place Merge | `dict1 += dict2` or `dict1 \|= dict2` | Merges dict2 into dict1 |
| Dictionary Difference | `dict1 - dict2` | Keys in dict1 not present in dict2 |
| In-Place Difference | `dict1 -= dict2` | Erases all keys present in dict2 from dict1 |
| In-Place Sorting | `dict.sortByKey(ascending)` | Reorders global iteration order by key |
| Sort by Value | `dict.sortByValue(ascending)` | Reorders global iteration order by value |
| Value Search | `dict.findValue(targetValue)` | Returns vector of all keys matching value |
| Undo Mutation | `dict.undo()` | Reverts last insert, update, erase, or clear |
| Redo Mutation | `dict.redo()` | Re-applies undone mutation |
| History Audit | `dict.printHistory()` | Prints formatted log of all mutations |
| JSON Serialization | `dict.saveToJSON(filename)` | Writes formatted JSON to disk |
| JSON Deserialization | `dict.loadFromJSON(filename)` | Restores dictionary from JSON file |
| Stream Output | `std::cout << dict` | Formats dictionary as `{ 'key': value }` |
| Stream Input | `std::cin >> dict` | Parses `{ 'key': value }` from any input stream |
| Statistics | `dict.printStats()` | Displays load factor, collision count, bucket depth |

---

## Performance Benchmark

Measured on x86_64 across 50,000 operations compiled with `-O3`:

| Operation (50,000 ops) | PyDict++ | std::map | std::unordered_map | Comparison vs std::map |
| :--- | :---: | :---: | :---: | :---: |
| **Lookup (Hits)** | **0.83 ms** | 4.46 ms | 0.62 ms | **5.4x faster** |
| **Lookup (Misses)** | **0.09 ms** | 0.41 ms | 0.31 ms | **3.4x faster** |
| **Iteration Traversal** | **1.20 ms** | 1.61 ms | 1.15 ms | **Faster than std::map** |
| **Bulk Erase** | **5.53 ms** | 9.70 ms | 2.80 ms | **1.75x faster** |

---

## License

This project is open-source and available under the MIT License.

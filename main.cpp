#include <iostream>
#include <string>
#include "pydict.hpp"

using namespace std;

int main() {
    cout << "=========================================================\n";
    cout << "                  PYDICT++ SHOWCASE                      \n";
    cout << "=========================================================\n\n";

    // 1. Core Python Dictionary CRUD
    cout << "1. Core Operations:\n";
    pydict<string, double> grocery = {
        {"apples", 2.99},
        {"milk", 3.49},
        {"bread", 2.25}
    };
    grocery["eggs"] = 4.19;
    cout << "   Dictionary: " << grocery << "\n";
    cout << "   Contains 'milk': " << (grocery.contains("milk") ? "Yes" : "No") << "\n";
    cout << "   Safe get('cheese', 0.0): $" << grocery.get("cheese", 0.0) << "\n";
    cout << "   Popped last item: " << grocery.popitem().first << "\n\n";

    // 2. Views and Range-based Loop (Insertion Order)
    cout << "2. Views & Iteration in Insertion Order:\n";
    cout << "   Keys:   " << grocery.keys() << "\n";
    cout << "   Values: " << grocery.values() << "\n";
    cout << "   Items:  " << grocery.items() << "\n";
    for (const auto& [item, price] : grocery) {
        cout << "     * " << item << " -> $" << price << "\n";
    }
    cout << "\n";

    // 3. Dictionary Merge & Difference
    cout << "3. Merge (+) and Difference (-):\n";
    pydict<string, int> storeA = {{"laptops", 10}, {"keyboards", 25}};
    pydict<string, int> storeB = {{"keyboards", 50}, {"monitors", 15}};
    auto merged = storeA | storeB;
    auto diff = storeA - storeB;
    cout << "   Merged:     " << merged << "\n";
    cout << "   Difference: " << diff << "\n\n";

    // 4. Undo / Redo & Operation History
    cout << "4. Undo / Redo System:\n";
    pydict<string, int> game = {{"score", 100}, {"level", 1}};
    game.insert("score", 250); // Updated score to 250
    cout << "   Before undo: " << game << "\n";
    game.undo(); // Undoes to 100
    cout << "   After undo:  " << game << "\n";
    game.redo(); // Redoes to 250
    cout << "   After redo:  " << game << "\n\n";

    // 5. In-Place Sorting
    cout << "5. In-Place Sorting:\n";
    pydict<string, int> students = {{"Charlie", 82}, {"Alice", 98}, {"Bob", 74}};
    students.sortByKey(true); // Alphabetical
    cout << "   Sorted by key:   " << students << "\n";
    students.sortByValue(false); // Ranked
    cout << "   Sorted by value: " << students << "\n\n";

    // 6. JSON Export & Import
    cout << "6. JSON Round-Trip:\n";
    string json = students.toJSONString(2);
    cout << "   Exported JSON:\n" << json << "\n";
    pydict<string, int> from_json;
    from_json.fromJSONString(json);
    cout << "   Restored from JSON: " << from_json << "\n\n";

    // 7. Dynamic Heterogeneous Nested Dict
    cout << "7. Dynamic Nested Dictionary:\n";
    PyAnyDict config;
    config["server"]["host"] = "127.0.0.1";
    config["server"]["port"] = 8080;
    config["active"] = true;
    cout << "   Config: " << config << "\n";
    cout << "   Host: " << config["server"]["host"].as_string() << "\n\n";

    // 8. Stream I/O (like iostream)
    cout << "8. Stream I/O:\n";
    stringstream ss("{'book': 15, 'pen': 3}");
    pydict<string, int> stationery;
    ss >> stationery; // Stream extraction
    cout << "   Read from stream (cin >> dict): " << stationery << "\n";
    cout << "   Written to stream (cout << dict): " << stationery << "\n\n";

    cout << "=========================================================\n";
    cout << "                  ALL TESTS COMPLETED!                   \n";
    cout << "=========================================================\n";
    return 0;
}

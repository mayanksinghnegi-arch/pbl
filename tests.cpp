// =====================================================================
//  tests.cpp — unit + integration tests for the DSA and OOP engine
//  Build & run:  g++ -std=c++17 tests.cpp -o tests && ./tests
// =====================================================================
#include "managers.h"
#include <cassert>

static int passed = 0;
#define CHECK(cond, name)                                                     \
    do {                                                                      \
        if (!(cond)) { std::cerr << "FAIL: " << name << "\n"; return 1; }     \
        std::cout << "  PASS  " << name << "\n";                              \
        passed++;                                                             \
    } while (0)

template <typename F>
bool throwsInvalid(F f) {
    try { f(); } catch (const std::invalid_argument&) { return true; }
    return false;
}

int main() {
    std::cout << "\n[ Data structures ]\n";
    {
        LinkedList<int> l;
        l.pushBack(2); l.pushBack(3); l.pushFront(1);
        CHECK(l.size() == 3, "LinkedList insert front/back");
        CHECK(l.removeFirst([](int v) { return v == 2; }) && l.size() == 2, "LinkedList delete middle node");
        CHECK(l.find([](int v) { return v == 3; }) != nullptr, "LinkedList find");
        l.removeLast();
        CHECK(l.size() == 1, "LinkedList remove last");
    }
    {
        Stack<int> s;
        s.push(1); s.push(2); s.push(3);
        CHECK(s.pop() == 3 && s.top() == 2, "Stack LIFO order");
        bool threw = false;
        Stack<int> e;
        try { e.pop(); } catch (const std::underflow_error&) { threw = true; }
        CHECK(threw, "Stack underflow throws");
    }
    {
        Queue<int> q;
        q.enqueue(1); q.enqueue(2); q.enqueue(3);
        CHECK(q.dequeue() == 1 && q.dequeue() == 2 && q.size() == 1, "Queue FIFO order");
    }
    {
        struct IntLess { bool operator()(int a, int b) const { return a < b; } };
        MinHeap<int, IntLess> h(2);
        int vals[] = {9, 4, 7, 1, 8, 2};
        for (int v : vals) h.insert(v);
        bool sorted = true;
        int prev = -1;
        while (!h.empty()) { int v = h.extractMin(); if (v < prev) sorted = false; prev = v; }
        CHECK(sorted, "MinHeap extracts in ascending order (with growth)");
    }
    {
        HashTable<std::string> t(5);   // few buckets -> forces chaining
        for (int i = 0; i < 20; i++) t.insert("N" + std::to_string(100 + i), "ngo" + std::to_string(i));
        CHECK(t.size() == 20 && *t.find("N113") == "ngo13", "HashTable insert + find with chaining");
        CHECK(!t.insert("N113", "dup"), "HashTable rejects duplicate key");
        CHECK(t.remove("N105") && t.find("N105") == nullptr, "HashTable remove");
    }
    {
        int a[] = {5, 3, 9, 1, 7, 2, 8};
        mergeSort(a, 7, [](int x, int y) { return x < y; });
        bool ok = true;
        for (int i = 1; i < 7; i++) if (a[i - 1] > a[i]) ok = false;
        CHECK(ok, "Merge sort");
        int b[] = {5, 3, 9, 1, 7, 2, 8};
        quickSort(b, 7, [](int x, int y) { return x < y; });
        ok = true;
        for (int i = 1; i < 7; i++) if (b[i - 1] > b[i]) ok = false;
        CHECK(ok, "Quick sort");
        int ub = upperBound(a, 7, 5.0, [](int v) { return static_cast<double>(v); });
        CHECK(ub == 4, "Binary search upper bound (values <= 5 -> 4 items)");
        std::string ids[] = {"F101", "F102", "F105", "F109"};
        CHECK(binarySearch(ids, 4, "F105", [](const std::string& s) { return s; }) == 2 &&
              binarySearch(ids, 4, "F999", [](const std::string& s) { return s; }) == -1, "Binary search exact match");
    }
    {
        Graph g;
        g.addEdge("A", "B", 4); g.addEdge("A", "C", 1); g.addEdge("C", "B", 2); g.addEdge("B", "D", 5);
        auto r = g.dijkstra("A");
        CHECK(r.dist[g.indexOf("B")] == 3 && r.dist[g.indexOf("D")] == 8, "Dijkstra shortest distances");
        auto p = g.pathTo(r, g.indexOf("D"));
        CHECK(p.size() == 4 && p[1] == "C", "Dijkstra path reconstruction A-C-B-D");
    }

    std::cout << "\n[ OOP classes ]\n";
    time_t now = nowTs();
    {
        RawFood raw("F1", "Rice", 10, "kg", 30, "C01", now - 24 * 3600, "Room", 2);
        PreparedFood prep("F2", "Biryani", 5, "kg", 60, "C01", now, now - 3600, 6);
        FoodItem* base = &raw;
        CHECK(std::fabs(base->getUrgencyScore() - 24) < 0.1, "RawFood urgency = shelfLife - age (24 h)");
        base = &prep;
        CHECK(std::fabs(base->getUrgencyScore() - 5) < 0.1, "PreparedFood urgency = window - sinceCooked (5 h)");
        CHECK(prep < raw, "operator< : prepared (5 h) more urgent than raw (24 h)");
        CHECK(raw.getType() == "RAW" && prep.getType() == "PREPARED", "Runtime polymorphism via virtual getType()");
        std::ostringstream os;
        os << prep;
        CHECK(os.str().find("Biryani") != std::string::npos, "operator<< prints item");
    }
    CHECK(throwsInvalid([] { RawFood("F", "X", -2, "kg", 10, "C", nowTs(), "Room", 2); }), "Rejects negative quantity");
    CHECK(throwsInvalid([] { RawFood("F", "X", 2, "kg", -1, "C", nowTs(), "Room", 2); }), "Rejects negative price");
    CHECK(throwsInvalid([] { RawFood("F", "  ", 2, "kg", 1, "C", nowTs(), "Room", 2); }), "Rejects empty food name");
    CHECK(throwsInvalid([] { NGO("N1", "", "9876543210", "x", 10); }), "Rejects empty NGO name");
    CHECK(throwsInvalid([] { NGO("N1", "Help", "123", "x", 10); }), "Rejects bad contact number");

    std::cout << "\n[ Engine integration ]\n";
    {
        InventoryManager inv;
        NGOManager ngos;
        OrderQueue q;
        TransactionLog log;
        MatchingEngine eng(inv, ngos, q, log);

        inv.addFood(new RawFood("F101", "Tomatoes", 8, "kg", 20, "C01", now, "Room", 3));
        inv.addFood(new PreparedFood("F102", "Dal", 5, "kg", 40, "C01", now, now - 2 * 3600, 4));
        inv.addFood(new PreparedFood("F103", "Old Rice", 3, "kg", 40, "C01", now, now - 10 * 3600, 4)); // expired
        CHECK(inv.size() == 3 && inv.heapSize() == 3, "Adding food updates linked list AND heap");
        auto urgent = inv.getMostUrgent(1);
        CHECK(urgent.size() == 1 && urgent[0]->getId() == "F102", "Heap top = most urgent non-expired item");

        ngos.registerNGO(NGO("N101", "Help A", "9876543210", "x", 40));
        ngos.registerNGO(NGO("N102", "Help B", "9876543211", "y", 3));
        CHECK(ngos.findNGO("N102") != nullptr && ngos.findNGO("N999") == nullptr, "NGO hash lookup");

        // Two NGOs ask for the same Dal: FIFO -> first gets 4 kg, second (asks 3) is rejected
        q.enqueueOrder(ngos.findNGO("N101")->placeOrder(q.generateId(), *inv.findItem("F102"), 4));
        q.enqueueOrder(ngos.findNGO("N102")->placeOrder(q.generateId(), *inv.findItem("F102"), 3));
        q.enqueueOrder(Order("O9", "F103", "Old Rice", "PREPARED", "N101", "Help A", 1, "kg", 40));
        q.enqueueOrder(Order("O10", "F101", "Tomatoes", "RAW", "N999", "Ghost", 1, "kg", 20));
        q.enqueueOrder(ngos.findNGO("N102")->placeOrder(q.generateId(), *inv.findItem("F101"), 5));
        auto res = eng.processQueue();
        CHECK(res[0].getStatus() == "CONFIRMED" && res[1].getStatus() == "REJECTED", "Same item, two NGOs: FIFO fairness");
        CHECK(res[2].getStatus() == "REJECTED", "Expired item rejected");
        CHECK(res[3].getStatus() == "REJECTED", "Unknown NGO ID rejected");
        CHECK(res[4].getStatus() == "REJECTED", "Order above NGO daily capacity rejected");
        CHECK(std::fabs(inv.findItem("F102")->getQuantity() - 1) < 1e-9, "Stock reduced after confirmation");
        CHECK(log.size() == 5 && log.getRecent(1)[0].getOrderId() == res[4].getOrderId(), "Transaction stack: newest on top");

        q.enqueueOrder(ngos.findNGO("N101")->placeOrder(q.generateId(), *inv.findItem("F102"), 1));
        eng.processQueue();
        CHECK(inv.findItem("F102") == nullptr, "Fully claimed item removed from inventory");

        log.updateStatus(res[0].getOrderId(), "PICKED_UP");
        log.updateStatus(res[0].getOrderId(), "DISTRIBUTED");
        CHECK(log.find(res[0].getOrderId())->getStatus() == "DISTRIBUTED", "Order lifecycle CONFIRMED -> PICKED_UP -> DISTRIBUTED");
        CHECK(throwsInvalid([&] { log.updateStatus(res[1].getOrderId(), "PICKED_UP"); }), "Invalid status transition rejected");

        SortInfo info;
        auto cheap = inv.getSortedList("price", 25, "", true, info);
        CHECK(cheap.size() == 1 && cheap[0]->getId() == "F101", "Sorted listing + price filter via binary search");
    }
    {
        PredictionEngine p;
        double vals[] = {10, 20, 30, 40, 50, 60, 70, 80};
        for (int i = 0; i < 8; i++) p.recordDailyWaste("RAW", "2020-01-0" + std::to_string(i + 1), vals[i], true);
        p.recordDailyWaste("RAW", "2020-01-08", 5, true);   // same day -> accumulates
        CHECK(std::fabs(p.predictNextDay("RAW", 7) - (20 + 30 + 40 + 50 + 60 + 70 + 85) / 7.0) < 1e-9,
              "Moving average of last 7 days (same-day entries accumulate)");
        CHECK(p.predictNextDay("PREPARED", 7) == 0, "Prediction with no history returns 0");
    }

    std::cout << "\nAll " << passed << " tests passed.\n";
    return 0;
}

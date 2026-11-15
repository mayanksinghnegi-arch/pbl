// =====================================================================
//  managers.h  —  The engine: OOP manager classes built on the DSA layer
//
//  InventoryManager  LinkedList<FoodItem*> + MinHeap (urgency) + sort/search
//  NGOManager        HashTable<NGO>
//  OrderQueue        Queue<Order>  (FIFO fairness)
//  TransactionLog    Stack<Order>  (most-recent-first)
//  MatchingEngine    greedy matching of queued orders against stock
//  PredictionEngine  DynamicArray history + simple moving average
//  DataStore         fstream persistence (pipe-separated .txt files)
//  EngineLog         recent engine operations (shown on Admin page)
// =====================================================================
#pragma once
#include <fstream>
#include <sstream>
#include <functional>
#include "dsa.h"
#include "models.h"

// ---------------------------------------------------------------------
// EngineLog — last 60 operations, newest first (LinkedList pushFront)
// ---------------------------------------------------------------------
class EngineLog {
    LinkedList<std::string> entries;
    const int cap = 60;

public:
    void add(const std::string& structure, const std::string& msg) {
        time_t t = nowTs();
        std::tm tmv = *std::localtime(&t);
        char buf[12];
        std::strftime(buf, sizeof buf, "%H:%M:%S", &tmv);
        entries.pushFront(std::string(buf) + "|" + structure + "|" + msg);
        if (entries.size() > cap) entries.removeLast();
    }
    json toJson() const {
        json arr = json::array();
        entries.forEach([&](const std::string& e) {
            auto p = split(e);
            arr.push_back({{"time", p[0]}, {"structure", p[1]}, {"message", p.size() > 2 ? p[2] : ""}});
        });
        return arr;
    }
};
inline EngineLog& engineLog() { static EngineLog log; return log; }

inline std::string num(double v) {
    std::ostringstream o;
    o << round2(v);
    return o.str();
}

// ---------------------------------------------------------------------
// InventoryManager
// ---------------------------------------------------------------------
struct UrgencyLess {             // used by the min-heap: calls overloaded FoodItem::operator<
    bool operator()(FoodItem* a, FoodItem* b) const { return *a < *b; }
};

struct SortInfo {
    std::string algorithm;
    long long comparisons = 0;
    int searchSteps = 0;
    int beforeFilter = 0;
};

class InventoryManager {
    LinkedList<FoodItem*> items;
    MinHeap<FoodItem*, UrgencyLess> urgencyHeap;
    int nextId = 101;

public:
    ~InventoryManager() { items.forEach([](FoodItem* f) { delete f; }); }

    std::string generateId() { return "F" + std::to_string(nextId++); }
    void bumpId(const std::string& id) {
        try { int n = std::stoi(id.substr(1)); if (n >= nextId) nextId = n + 1; } catch (...) {}
    }

    void addFood(FoodItem* f, bool quiet = false) {
        items.pushBack(f);            // O(1) insert at tail of the linked list
        urgencyHeap.insert(f);        // O(log n) insert into the urgency min-heap
        bumpId(f->getId());
        if (!quiet)
            engineLog().add("LinkedList + MinHeap", "Inserted " + f->getId() + " (" + f->getName() +
                            "); list size " + std::to_string(items.size()) + ", heap top = " +
                            urgencyHeap.peek()->getId());
    }

    bool removeFood(std::string id) {   // by value: caller may pass a reference into the item being deleted
        FoodItem* removed = nullptr;
        bool ok = items.removeFirst([&](FoodItem* f) { return f->getId() == id; }, &removed);
        if (ok) {
            std::string nm = removed->getName();
            delete removed;
            rebuildHeap();
            engineLog().add("LinkedList", "Deleted node " + id + " (" + nm + "); list size " +
                            std::to_string(items.size()));
        }
        return ok;
    }

    FoodItem* findItem(const std::string& id) {
        FoodItem** p = items.find([&](FoodItem* f) { return f->getId() == id; });
        return p ? *p : nullptr;
    }

    // Urgency scores change with time, so the heap is rebuilt before it is read
    void rebuildHeap() {
        urgencyHeap.clear();
        items.forEach([&](FoodItem* f) { urgencyHeap.insert(f); });
    }

    // Top-k most urgent (non-expired) items using extractMin
    std::vector<FoodItem*> getMostUrgent(int k) {
        rebuildHeap();
        std::vector<FoodItem*> out;
        while (!urgencyHeap.empty() && static_cast<int>(out.size()) < k) {
            FoodItem* f = urgencyHeap.extractMin();
            if (!f->isExpired() && f->getQuantity() > 0) out.push_back(f);
        }
        rebuildHeap();
        return out;
    }

    // Copies the list into an array, applies filters, sorts (merge / quick sort)
    // and optionally keeps only items with price <= maxPrice (binary search).
    std::vector<FoodItem*> getSortedList(const std::string& sortBy, double maxPrice,
                                         const std::string& type, bool includeExpired, SortInfo& info) {
        int n = items.size();
        FoodItem** arr = new FoodItem*[n > 0 ? n : 1];
        int m = 0;
        items.forEach([&](FoodItem* f) {
            if (!type.empty() && f->getType() != type) return;
            if (!includeExpired && f->isExpired()) return;
            arr[m++] = f;
        });
        info.beforeFilter = m;

        auto byPrice = [](FoodItem* a, FoodItem* b) { return a->getPrice() < b->getPrice(); };
        auto byUrgency = [](FoodItem* a, FoodItem* b) { return *a < *b; };
        auto byName = [](FoodItem* a, FoodItem* b) { return a->getName() < b->getName(); };

        if (maxPrice >= 0) {
            info.comparisons += mergeSort(arr, m, byPrice);
            m = upperBound(arr, m, maxPrice, [](FoodItem* f) { return f->getPrice(); }, &info.searchSteps);
        }
        if (sortBy == "price") {
            if (maxPrice < 0) info.comparisons += mergeSort(arr, m, byPrice);
            info.algorithm = "Merge Sort (price)";
        } else if (sortBy == "name") {
            info.comparisons += quickSort(arr, m, byName);
            info.algorithm = "Quick Sort (name)";
        } else {
            info.comparisons += mergeSort(arr, m, byUrgency);
            info.algorithm = "Merge Sort (urgency)";
        }
        std::vector<FoodItem*> out(arr, arr + m);
        delete[] arr;
        return out;
    }

    // Binary search by ID on an ID-sorted copy of the list
    FoodItem* binaryFindById(const std::string& id, int& steps) {
        int n = items.size();
        if (n == 0) { steps = 0; return nullptr; }
        FoodItem** arr = new FoodItem*[n];
        int m = 0;
        items.forEach([&](FoodItem* f) { arr[m++] = f; });
        mergeSort(arr, m, [](FoodItem* a, FoodItem* b) { return a->getId() < b->getId(); });
        int idx = binarySearch(arr, m, id, [](FoodItem* f) { return f->getId(); }, &steps);
        FoodItem* res = idx >= 0 ? arr[idx] : nullptr;
        delete[] arr;
        return res;
    }

    template <typename F> void forEach(F f) { items.forEach(f); }
    int size() const { return items.size(); }
    int heapSize() const { return urgencyHeap.size(); }
};

// ---------------------------------------------------------------------
// NGOManager — hash table keyed by NGO ID
// ---------------------------------------------------------------------
class NGOManager {
    HashTable<NGO> table{31};
    int nextId = 101;

public:
    std::string generateId() { return "N" + std::to_string(nextId++); }

    NGO& registerNGO(const NGO& ngo, bool quiet = false) {
        if (!table.insert(ngo.getId(), ngo)) throw std::invalid_argument("NGO ID " + ngo.getId() + " already exists");
        try { int n = std::stoi(ngo.getId().substr(1)); if (n >= nextId) nextId = n + 1; } catch (...) {}
        if (!quiet)
            engineLog().add("HashTable", "Registered " + ngo.getId() + " -> bucket " +
                            std::to_string(table.bucketOf(ngo.getId())) + " (chain length " +
                            std::to_string(table.chainLength(table.bucketOf(ngo.getId()))) + ")");
        return *table.find(ngo.getId());
    }

    NGO* findNGO(const std::string& id) { return table.find(id); }
    int bucketOf(const std::string& id) const { return table.bucketOf(id); }

    std::vector<NGO> all() {
        std::vector<NGO> out;
        table.forEach([&](const std::string&, NGO& n) { out.push_back(n); });
        // sort by ID for a stable display order (quick sort on a raw array)
        int m = static_cast<int>(out.size());
        NGO* arr = new NGO[m > 0 ? m : 1];
        for (int i = 0; i < m; i++) arr[i] = out[i];
        quickSort(arr, m, [](const NGO& a, const NGO& b) { return a.getId() < b.getId(); });
        for (int i = 0; i < m; i++) out[i] = arr[i];
        delete[] arr;
        return out;
    }
    int size() const { return table.size(); }
    int buckets() const { return table.buckets_(); }
};

// ---------------------------------------------------------------------
// TransactionLog — stack: newest transaction on top
// ---------------------------------------------------------------------
class TransactionLog {
    Stack<Order> st;

public:
    void logTransaction(const Order& o, bool quiet = false) {
        st.push(o);
        if (!quiet)
            engineLog().add("Stack", "Pushed " + o.getOrderId() + " [" + o.getStatus() + "]; depth " +
                            std::to_string(st.size()));
    }

    std::vector<Order> getRecent(int n) const {
        std::vector<Order> out;
        st.forEach([&](const Order& o) { if (n < 0 || static_cast<int>(out.size()) < n) out.push_back(o); });
        return out;
    }

    Order* find(const std::string& orderId) {
        Order* found = nullptr;
        st.forEach([&](Order& o) { if (!found && o.getOrderId() == orderId) found = &o; });
        return found;
    }

    void updateStatus(const std::string& orderId, const std::string& next) {
        Order* o = find(orderId);
        if (!o) throw std::invalid_argument("Order " + orderId + " not found");
        o->advanceTo(next);
        engineLog().add("Stack", "Order " + orderId + " -> " + next);
    }

    std::vector<Order> historyFor(const std::string& ngoId) const {
        std::vector<Order> out;
        st.forEach([&](const Order& o) { if (o.getNgoId() == ngoId) out.push_back(o); });
        return out;
    }

    double confirmedQtyOn(const std::string& ngoId, const std::string& date) const {
        double total = 0;
        st.forEach([&](const Order& o) {
            if (o.getNgoId() == ngoId && o.countsAsSaved() && fmtDate(o.getTimestamp()) == date) total += o.getQuantity();
        });
        return total;
    }

    template <typename F> void forEach(F f) const { st.forEach(f); }
    int size() const { return st.size(); }
};

inline std::vector<Order> NGO::viewHistory(const TransactionLog& log) const { return log.historyFor(ngoId); }

// ---------------------------------------------------------------------
// OrderQueue — FIFO: first request in is first served
// ---------------------------------------------------------------------
class OrderQueue {
    Queue<Order> q;
    int nextId = 1001;

public:
    std::string generateId() { return "O" + std::to_string(nextId++); }
    void bumpId(const std::string& id) {
        try { int n = std::stoi(id.substr(1)); if (n >= nextId) nextId = n + 1; } catch (...) {}
    }

    void enqueueOrder(const Order& o, bool quiet = false) {
        q.enqueue(o);
        bumpId(o.getOrderId());
        if (!quiet)
            engineLog().add("Queue", "Enqueued " + o.getOrderId() + " from " + o.getNgoId() +
                            " at position " + std::to_string(q.size()));
    }
    Order dequeueOrder() { return q.dequeue(); }

    std::vector<Order> snapshot() const {
        std::vector<Order> out;
        q.forEach([&](const Order& o) { out.push_back(o); });
        return out;
    }
    bool empty() const { return q.empty(); }
    int size() const { return q.size(); }
};

// ---------------------------------------------------------------------
// MatchingEngine — processes the order queue in FIFO order
// ---------------------------------------------------------------------
class MatchingEngine {
    InventoryManager& inv;
    NGOManager& ngos;
    OrderQueue& queue;
    TransactionLog& log;

public:
    MatchingEngine(InventoryManager& i, NGOManager& n, OrderQueue& q, TransactionLog& l)
        : inv(i), ngos(n), queue(q), log(l) {}

    // Confirms the order if every rule passes, otherwise rejects with a reason
    void matchOrder(Order& o) {
        NGO* ngo = ngos.findNGO(o.getNgoId());                        // O(1) hash lookup
        FoodItem* item = inv.findItem(o.getFoodItemId());
        double qty = o.getQuantity();

        if (!ngo) { o.reject("Unknown NGO ID"); return; }
        if (!item) { o.reject("Item no longer available (fully claimed or removed)"); return; }
        if (item->isExpired()) { o.reject("Item has expired"); return; }
        if (item->getQuantity() + 1e-9 < qty) {
            o.reject("Insufficient stock: requested " + num(qty) + ", available " + num(item->getQuantity()));
            return;
        }
        double usedToday = log.confirmedQtyOn(ngo->getId(), fmtDate(nowTs()));
        if (usedToday + qty > ngo->getCapacity() + 1e-9) {
            o.reject("Exceeds NGO daily capacity (" + num(usedToday) + " of " + num(ngo->getCapacity()) + " used)");
            return;
        }

        item->setQuantity(item->getQuantity() - qty);
        o.setAmount(qty * item->getPrice());
        o.confirm();
        if (item->getQuantity() <= 1e-9) inv.removeFood(item->getId());   // fully claimed -> delete node
    }

    std::vector<Order> processQueue() {
        std::vector<Order> processed;
        while (!queue.empty()) {
            Order o = queue.dequeueOrder();
            matchOrder(o);
            engineLog().add("MatchingEngine", "Dequeued " + o.getOrderId() + " -> " + o.getStatus());
            log.logTransaction(o);
            processed.push_back(o);
        }
        return processed;
    }
};

// ---------------------------------------------------------------------
// PredictionEngine — simple moving average over the last N days
// ---------------------------------------------------------------------
struct DayRecord {
    std::string date;
    double qty = 0;
};

class PredictionEngine {
    DynamicArray<DayRecord> rawHistory;
    DynamicArray<DayRecord> preparedHistory;

    DynamicArray<DayRecord>& hist(const std::string& cat) {
        if (cat == "RAW") return rawHistory;
        if (cat == "PREPARED") return preparedHistory;
        throw std::invalid_argument("Unknown food category: " + cat);
    }

    // Index of the last fully completed day (today is still in progress)
    int completedCount(const DynamicArray<DayRecord>& h) const {
        int n = h.size();
        if (n > 0 && h[n - 1].date == fmtDate(nowTs())) n--;
        return n;
    }

public:
    void recordDailyWaste(const std::string& cat, const std::string& date, double qty, bool quiet = false) {
        DynamicArray<DayRecord>& h = hist(cat);
        if (h.size() > 0 && h[h.size() - 1].date == date) h[h.size() - 1].qty += qty;
        else h.push(DayRecord{date, qty});
        if (!quiet)
            engineLog().add("DynamicArray", cat + " history[" + date + "] += " + num(qty) + " (" +
                            std::to_string(h.size()) + " days stored)");
    }

    double predictNextDay(const std::string& cat, int N = 7) {
        DynamicArray<DayRecord>& h = hist(cat);
        int n = completedCount(h);
        if (n == 0) return 0;
        int start = n - N < 0 ? 0 : n - N;
        double sum = 0;
        for (int i = start; i < n; i++) sum += h[i].qty;
        return sum / (n - start);
    }

    // Back-test: for each past day, what would the moving average have predicted?
    json backtest(const std::string& cat, int N, double& accuracyOut) {
        DynamicArray<DayRecord>& h = hist(cat);
        int n = completedCount(h);
        json rows = json::array();
        double apeSum = 0;
        int count = 0;
        for (int i = N; i < n; i++) {
            double s = 0;
            for (int k = i - N; k < i; k++) s += h[k].qty;
            double pred = s / N;
            double actual = h[i].qty;
            rows.push_back({{"date", h[i].date}, {"actual", round2(actual)}, {"predicted", round2(pred)}});
            if (actual > 0) { apeSum += std::fabs(actual - pred) / actual; count++; }
        }
        accuracyOut = count ? std::max(0.0, 100.0 * (1.0 - apeSum / count)) : 0;
        return rows;
    }

    json toJson(int N = 7) {
        json out = json::object();
        out["today"] = fmtDate(nowTs());
        for (std::string cat : {"RAW", "PREPARED"}) {
            DynamicArray<DayRecord>& h = hist(cat);
            json histArr = json::array();
            int from = h.size() - 14 < 0 ? 0 : h.size() - 14;
            for (int i = from; i < h.size(); i++) histArr.push_back({{"date", h[i].date}, {"qty", round2(h[i].qty)}});
            double acc = 0;
            json bt = backtest(cat, N, acc);
            double today = (h.size() > 0 && h[h.size() - 1].date == fmtDate(nowTs())) ? h[h.size() - 1].qty : 0;
            out[cat] = {{"history", histArr}, {"predictedNextDay", round2(predictNextDay(cat, N))},
                        {"window", N}, {"accuracy", round2(acc)}, {"backtest", bt},
                        {"todaySoFar", round2(today)}};
        }
        return out;
    }

    template <typename F>
    void forEachRecord(F f) {
        for (int i = 0; i < rawHistory.size(); i++) f("RAW", rawHistory[i]);
        for (int i = 0; i < preparedHistory.size(); i++) f("PREPARED", preparedHistory[i]);
    }
    int daysStored() const { return rawHistory.size() + preparedHistory.size(); }
};

// ---------------------------------------------------------------------
// Canteen method definitions (need InventoryManager to be complete)
// ---------------------------------------------------------------------
inline void Canteen::addFoodItem(InventoryManager& inv, FoodItem* item) { inv.addFood(item); }
inline bool Canteen::removeFoodItem(InventoryManager& inv, const std::string& id) { return inv.removeFood(id); }

// ---------------------------------------------------------------------
// DataStore — file handling with fstream
//   foods.txt, ngos.txt, transactions.txt, pending_orders.txt,
//   waste_history.txt, graph.txt
// ---------------------------------------------------------------------
class DataStore {
    std::string dir;

    std::string path(const std::string& f) const { return dir + "/" + f; }

    template <typename F>
    bool readLines(const std::string& file, F f) const {
        std::ifstream in(path(file));
        if (!in) return false;
        std::string line;
        while (std::getline(in, line)) {
            line = trim(line);
            if (line.empty() || line[0] == '#') continue;
            try { f(line); }
            catch (const std::exception& e) { std::cerr << "  skipped bad line in " << file << ": " << e.what() << "\n"; }
        }
        return true;
    }

public:
    explicit DataStore(const std::string& dataDir) : dir(dataDir) {}

    bool exists(const std::string& file) const { std::ifstream in(path(file)); return static_cast<bool>(in); }

    // ---------- save ----------
    void saveFoods(InventoryManager& inv) const {
        std::ofstream out(path("foods.txt"));
        out << "# type|id|name|qty|unit|price|canteenId|dateAdded|(raw: storage|shelfLifeDays) (prepared: cookedTime|consumeWithinHours)\n";
        inv.forEach([&](FoodItem* f) { out << f->serialize() << "\n"; });
    }
    void saveNGOs(NGOManager& ngos) const {
        std::ofstream out(path("ngos.txt"));
        out << "# ngoId|name|contact|address|capacityPerDayKg\n";
        for (const NGO& n : ngos.all()) out << n.serialize() << "\n";
    }
    void saveTransactions(const TransactionLog& log) const {
        std::vector<Order> topFirst = log.getRecent(-1);
        std::ofstream out(path("transactions.txt"));
        out << "# oldest first (bottom of stack) -> newest last (top of stack)\n";
        for (auto it = topFirst.rbegin(); it != topFirst.rend(); ++it) out << it->serialize() << "\n";
    }
    void savePending(const OrderQueue& q) const {
        std::ofstream out(path("pending_orders.txt"));
        out << "# front of queue first\n";
        for (const Order& o : q.snapshot()) out << o.serialize() << "\n";
    }
    void saveHistory(PredictionEngine& pred) const {
        std::ofstream out(path("waste_history.txt"));
        out << "# category|date|leftoverQty\n";
        pred.forEachRecord([&](const std::string& cat, const DayRecord& r) { out << cat << '|' << r.date << '|' << r.qty << "\n"; });
    }
    void saveGraph(const Graph& g) const {
        std::ofstream out(path("graph.txt"));
        out << "# nodeA|nodeB|distanceKm\n";
        g.forEachEdge([&](const std::string& a, const std::string& b, double w) { out << a << '|' << b << '|' << w << "\n"; });
    }

    // ---------- load ----------
    void loadFoods(InventoryManager& inv) const {
        readLines("foods.txt", [&](const std::string& line) {
            auto p = split(line);
            if (p.size() < 10) throw std::runtime_error("too few fields");
            time_t added = static_cast<time_t>(std::stoll(p[7]));
            FoodItem* f = nullptr;
            if (p[0] == "R")
                f = new RawFood(p[1], p[2], std::stod(p[3]), p[4], std::stod(p[5]), p[6], added, p[8], std::stoi(p[9]));
            else
                f = new PreparedFood(p[1], p[2], std::stod(p[3]), p[4], std::stod(p[5]), p[6], added,
                                     static_cast<time_t>(std::stoll(p[8])), std::stod(p[9]));
            inv.addFood(f, true);
        });
    }
    void loadNGOs(NGOManager& ngos) const {
        readLines("ngos.txt", [&](const std::string& line) { ngos.registerNGO(NGO::deserialize(line), true); });
    }
    void loadTransactions(TransactionLog& log, OrderQueue& q) const {
        readLines("transactions.txt", [&](const std::string& line) {
            Order o = Order::deserialize(line);
            q.bumpId(o.getOrderId());
            log.logTransaction(o, true);
        });
    }
    void loadPending(OrderQueue& q) const {
        readLines("pending_orders.txt", [&](const std::string& line) { q.enqueueOrder(Order::deserialize(line), true); });
    }
    void loadHistory(PredictionEngine& pred) const {
        readLines("waste_history.txt", [&](const std::string& line) {
            auto p = split(line);
            if (p.size() < 3) throw std::runtime_error("too few fields");
            pred.recordDailyWaste(p[0], p[1], std::stod(p[2]), true);
        });
    }
    void loadGraph(Graph& g) const {
        readLines("graph.txt", [&](const std::string& line) {
            auto p = split(line);
            if (p.size() < 3) throw std::runtime_error("too few fields");
            g.addEdge(p[0], p[1], std::stod(p[2]));
        });
    }
};

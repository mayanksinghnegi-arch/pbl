// =====================================================================
//  main.cpp  —  FoodBridge server
//  Serves the website (frontend/) and exposes the C++ engine as a JSON API
//  using cpp-httplib + nlohmann/json.
//
//  Build:  g++ -std=c++17 -O2 main.cpp -o foodbridge -lpthread        (Linux / macOS)
//          g++ -std=c++17 -O2 main.cpp -o foodbridge.exe -lws2_32     (Windows, MinGW-w64)
//  Run  :  ./foodbridge            then open http://localhost:8080
//          ./foodbridge --demo     console demo of the engine (no server)
// =====================================================================
#include "httplib.h"
#include "managers.h"
#include <mutex>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#define MAKE_DIR(p) _mkdir(p)
#else
#define MAKE_DIR(p) mkdir(p, 0755)
#endif

// ---------------------------------------------------------------------
// Application state — one instance of every engine component
// ---------------------------------------------------------------------
struct App {
    Canteen canteen{"C01", "Main Campus Canteen", "Block A, Ground Floor"};
    InventoryManager inventory;
    NGOManager ngos;
    OrderQueue queue;
    TransactionLog log;
    PredictionEngine prediction;
    Graph graph;
    DataStore store;
    MatchingEngine engine{inventory, ngos, queue, log};

    explicit App(const std::string& dataDir) : store(dataDir) {}

    void saveAll() {
        store.saveFoods(inventory);
        store.saveNGOs(ngos);
        store.saveTransactions(log);
        store.savePending(queue);
        store.saveHistory(prediction);
        store.saveGraph(graph);
    }

    void loadAll() {
        store.loadNGOs(ngos);
        store.loadFoods(inventory);
        store.loadTransactions(log, queue);
        store.loadPending(queue);
        store.loadHistory(prediction);
        store.loadGraph(graph);
    }

    // First run: create realistic sample data relative to "now"
    void seed() {
        time_t now = nowTs();
        auto hrs = [&](double h) { return now - static_cast<time_t>(h * 3600); };

        ngos.registerNGO(NGO("N101", "Annapurna Seva Trust", "9810011122", "Sector 37, near Gate 2", 40), true);
        ngos.registerNGO(NGO("N102", "Roti Ghar Foundation", "9873345566", "Market Chowk, Old Town", 25), true);
        ngos.registerNGO(NGO("N103", "Asha Community Kitchen", "9999012345", "Behind Metro Station", 30), true);
        ngos.registerNGO(NGO("N104", "Saathi Welfare Society", "9650987654", "Shanti Nagar Colony", 15), true);

        // Raw food: id, name, qty, unit, price/unit, canteen, added, storage, shelfLifeDays
        inventory.addFood(new RawFood("F101", "Tomatoes", 8, "kg", 20, "C01", hrs(30), "Room", 3), true);
        inventory.addFood(new RawFood("F102", "Spinach", 4, "kg", 15, "C01", hrs(38), "Room", 2), true);
        inventory.addFood(new RawFood("F103", "Milk", 6, "litre", 40, "C01", hrs(20), "Refrigerated", 2), true);
        inventory.addFood(new RawFood("F104", "Paneer", 2, "kg", 180, "C01", hrs(10), "Refrigerated", 3), true);
        inventory.addFood(new RawFood("F105", "Basmati Rice (uncooked)", 15, "kg", 35, "C01", hrs(48), "Room", 30), true);
        inventory.addFood(new RawFood("F106", "Potatoes", 12, "kg", 18, "C01", hrs(24), "Room", 10), true);
        // Prepared food: ..., cookedTime, consumeWithinHours
        inventory.addFood(new PreparedFood("F107", "Veg Biryani", 10, "kg", 60, "C01", hrs(1.5), hrs(2), 6), true);
        inventory.addFood(new PreparedFood("F108", "Rajma Curry", 4, "kg", 50, "C01", hrs(3.5), hrs(4.5), 6), true);
        inventory.addFood(new PreparedFood("F109", "Dal Makhani", 6, "kg", 45, "C01", hrs(0.5), hrs(1), 8), true);
        inventory.addFood(new PreparedFood("F110", "Chapati", 5, "kg", 30, "C01", hrs(1), hrs(1.5), 10), true);

        // 28 days of history (weekday pattern: weekends cook less, so less waste)
        const double rawByDay[7] = {11, 24, 22, 26, 23, 27, 15};       // Sun..Sat
        const double prepByDay[7] = {14, 32, 29, 35, 31, 38, 20};
        for (int d = 28; d >= 1; d--) {
            time_t t = now - static_cast<time_t>(d) * 86400;
            int wd = std::localtime(&t)->tm_wday;
            double wobble = ((d * 7) % 5) - 2;                           // -2..+2 deterministic variation
            prediction.recordDailyWaste("RAW", fmtDate(t), rawByDay[wd] + wobble, true);
            prediction.recordDailyWaste("PREPARED", fmtDate(t), prepByDay[wd] - wobble * 1.5, true);
        }
        // Today's entries so far = what is currently in the inventory
        inventory.forEach([&](FoodItem* f) {
            if (fmtDate(f->getDateAdded()) == fmtDate(now))
                prediction.recordDailyWaste(f->getType(), fmtDate(now), f->getQuantity(), true);
        });

        // A few completed transactions from previous days (oldest first)
        auto past = [&](double days) { return std::to_string(static_cast<long long>(now - days * 86400)); };
        const std::string seedTx[] = {
            "O1001|F090|Veg Pulao|PREPARED|N101|Annapurna Seva Trust|8|kg|400|DISTRIBUTED||" + past(3) + "|" + past(2.9),
            "O1002|F091|Onions|RAW|N102|Roti Ghar Foundation|10|kg|200|DISTRIBUTED||" + past(2.5) + "|" + past(2.4),
            "O1003|F092|Chole|PREPARED|N104|Saathi Welfare Society|20|kg|0|REJECTED|Exceeds NGO daily capacity (0 of 15 used)|" + past(2) + "|" + past(2),
            "O1004|F093|Aloo Sabzi|PREPARED|N103|Asha Community Kitchen|7|kg|280|DISTRIBUTED||" + past(1.2) + "|" + past(1.1),
            "O1005|F094|Bananas|RAW|N101|Annapurna Seva Trust|6|kg|180|PICKED_UP||" + past(0.9) + "|" + past(0.8),
        };
        for (const std::string& line : seedTx) {
            Order o = Order::deserialize(line);
            queue.bumpId(o.getOrderId());
            log.logTransaction(o, true);
        }

        // Map: canteen, NGOs and landmarks with road distances (km)
        graph.addEdge("CANTEEN", "Gate-2", 1.2);
        graph.addEdge("CANTEEN", "Metro-Station", 3.5);
        graph.addEdge("Gate-2", "Market-Chowk", 2.8);
        graph.addEdge("Gate-2", "N101", 0.8);
        graph.addEdge("Market-Chowk", "N102", 0.6);
        graph.addEdge("Metro-Station", "Market-Chowk", 1.5);
        graph.addEdge("Metro-Station", "N103", 0.4);
        graph.addEdge("Metro-Station", "Shanti-Nagar", 2.6);
        graph.addEdge("Shanti-Nagar", "N104", 0.9);
        graph.addEdge("Gate-2", "Shanti-Nagar", 5.5);
    }
};

std::mutex appMutex;

// ---------------------------------------------------------------------
// HTTP helpers
// ---------------------------------------------------------------------
static void sendJson(httplib::Response& res, const json& j, int status = 200) {
    res.status = status;
    res.set_content(j.dump(), "application/json; charset=utf-8");
}
static void sendError(httplib::Response& res, const std::string& msg, int status = 400) {
    sendJson(res, json{{"error", msg}}, status);
}
static json body(const httplib::Request& req) {
    if (req.body.empty()) return json::object();
    return json::parse(req.body);
}
static double numField(const json& j, const char* key, double def = 0) {
    if (!j.contains(key) || j[key].is_null()) return def;
    if (j[key].is_number()) return j[key].get<double>();
    if (j[key].is_string()) {
        std::string s = trim(j[key].get<std::string>());
        if (s.empty()) return def;
        try { return std::stod(s); } catch (...) { throw std::invalid_argument(std::string("Field '") + key + "' must be a number"); }
    }
    throw std::invalid_argument(std::string("Field '") + key + "' must be a number");
}
static std::string strField(const json& j, const char* key, const std::string& def = "") {
    if (!j.contains(key) || j[key].is_null()) return def;
    if (j[key].is_string()) return j[key].get<std::string>();
    return j[key].dump();
}

// Every handler runs under one mutex and converts exceptions to JSON errors
template <typename F>
httplib::Server::Handler guarded(F f) {
    return [f](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(appMutex);
        try { f(req, res); }
        catch (const json::exception& e) { sendError(res, std::string("Invalid JSON: ") + e.what(), 400); }
        catch (const std::invalid_argument& e) { sendError(res, e.what(), 400); }
        catch (const std::exception& e) { sendError(res, e.what(), 500); }
    };
}

static bool fileExists(const std::string& p) { std::ifstream f(p); return static_cast<bool>(f); }

// ---------------------------------------------------------------------
// Console demo (for viva) — shows operator<< and the engine without the website
// ---------------------------------------------------------------------
static int runDemo(App& app) {
    std::cout << "\n=== Inventory (LinkedList, printed with overloaded operator<<) ===\n";
    app.inventory.forEach([](FoodItem* f) { std::cout << *f << "\n"; });

    std::cout << "\n=== Top 3 most urgent (MinHeap extractMin) ===\n";
    for (FoodItem* f : app.inventory.getMostUrgent(3)) std::cout << *f << "\n";

    std::cout << "\n=== Hash lookup N103 ===\n";
    if (NGO* n = app.ngos.findNGO("N103"))
        std::cout << n->getName() << "  (bucket " << app.ngos.bucketOf("N103") << ")\n";

    std::cout << "\n=== Nearest NGOs from canteen (Dijkstra) ===\n";
    auto r = app.graph.dijkstra("CANTEEN");
    for (const NGO& n : app.ngos.all()) {
        int idx = app.graph.indexOf(n.getId());
        if (idx < 0) continue;
        std::cout << n.getId() << " " << n.getName() << " : " << r.dist[idx] << " km  via ";
        for (auto& s : app.graph.pathTo(r, idx)) std::cout << s << " ";
        std::cout << "\n";
    }

    std::cout << "\n=== Prediction (7-day moving average) ===\n";
    std::cout << "RAW tomorrow      : " << round2(app.prediction.predictNextDay("RAW")) << " kg\n";
    std::cout << "PREPARED tomorrow : " << round2(app.prediction.predictNextDay("PREPARED")) << " kg\n";
    std::cout << "\n(demo mode does not save changes)\n";
    return 0;
}

// =====================================================================
int main(int argc, char* argv[]) {
    int port = 8080;
    bool demo = false;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--demo") demo = true;
        else if (a == "--port" && i + 1 < argc) port = std::stoi(argv[++i]);
    }

    // Works whether you run from the project root or from backend/
    std::string root = fileExists("frontend/index.html") ? "." : (fileExists("../frontend/index.html") ? ".." : ".");
    std::string frontendDir = root + "/frontend";
    std::string dataDir = root + "/data";
    MAKE_DIR(dataDir.c_str());

    App app(dataDir);
    if (app.store.exists("foods.txt")) {
        app.loadAll();
        std::cout << "Loaded data from " << dataDir << "\n";
    } else {
        app.seed();
        if (!demo) app.saveAll();
        std::cout << "First run: created sample data in " << dataDir << "\n";
    }
    if (demo) return runDemo(app);

    httplib::Server svr;
    svr.set_default_headers({{"Access-Control-Allow-Origin", "*"},
                             {"Access-Control-Allow-Headers", "Content-Type"},
                             {"Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS"}});
    svr.Options(R"(/api/.*)", [](const httplib::Request&, httplib::Response& res) { res.status = 204; });

    // ---------------- Canteen ----------------
    svr.Get("/api/canteen", guarded([&](const httplib::Request&, httplib::Response& res) {
        sendJson(res, app.canteen.toJson());
    }));

    // ---------------- Foods ----------------
    // GET /api/foods?sort=urgency|price|name&maxPrice=50&type=RAW|PREPARED&includeExpired=1
    svr.Get("/api/foods", guarded([&](const httplib::Request& req, httplib::Response& res) {
        std::string sort = req.has_param("sort") ? req.get_param_value("sort") : "urgency";
        std::string type = req.has_param("type") ? req.get_param_value("type") : "";
        double maxPrice = -1;
        if (req.has_param("maxPrice") && !req.get_param_value("maxPrice").empty())
            maxPrice = std::stod(req.get_param_value("maxPrice"));
        bool includeExpired = !req.has_param("includeExpired") || req.get_param_value("includeExpired") != "0";

        SortInfo info;
        auto list = app.inventory.getSortedList(sort, maxPrice, type, includeExpired, info);
        json items = json::array();
        for (FoodItem* f : list) items.push_back(f->toJson());

        json meta = {{"algorithm", info.algorithm}, {"comparisons", info.comparisons},
                     {"structure", "LinkedList -> array"}, {"total", info.beforeFilter}};
        if (maxPrice >= 0) {
            meta["binarySearch"] = {{"maxPrice", maxPrice}, {"steps", info.searchSteps}, {"kept", list.size()}};
            engineLog().add("BinarySearch", "upperBound(price <= " + num(maxPrice) + ") in " +
                            std::to_string(info.searchSteps) + " steps -> " + std::to_string(list.size()) + " items");
        }
        sendJson(res, json{{"items", items}, {"meta", meta}});
    }));

    svr.Get("/api/foods/urgent", guarded([&](const httplib::Request& req, httplib::Response& res) {
        int k = req.has_param("k") ? std::stoi(req.get_param_value("k")) : 3;
        json items = json::array();
        for (FoodItem* f : app.inventory.getMostUrgent(k)) items.push_back(f->toJson());
        sendJson(res, json{{"items", items}, {"heapSize", app.inventory.heapSize()}});
    }));

    svr.Get(R"(/api/foods/(\w+))", guarded([&](const httplib::Request& req, httplib::Response& res) {
        int steps = 0;
        FoodItem* f = app.inventory.binaryFindById(req.matches[1], steps);
        if (!f) return sendError(res, "Food item not found", 404);
        sendJson(res, json{{"item", f->toJson()}, {"binarySearchSteps", steps}});
    }));

    // POST /api/foods  { type, name, quantity, unit, price, storageType, shelfLifeDays, cookedHoursAgo, consumeWithinHours }
    svr.Post("/api/foods", guarded([&](const httplib::Request& req, httplib::Response& res) {
        json b = body(req);
        std::string type = strField(b, "type", "RAW");
        std::string id = app.inventory.generateId();
        time_t now = nowTs();
        FoodItem* f = nullptr;
        if (type == "RAW") {
            f = new RawFood(id, strField(b, "name"), numField(b, "quantity"), strField(b, "unit", "kg"),
                            numField(b, "price"), app.canteen.getId(), now, strField(b, "storageType", "Room"),
                            static_cast<int>(numField(b, "shelfLifeDays", 0)));
        } else if (type == "PREPARED") {
            double ago = numField(b, "cookedHoursAgo", 0);
            if (ago < 0) throw std::invalid_argument("Cooked hours ago cannot be negative");
            f = new PreparedFood(id, strField(b, "name"), numField(b, "quantity"), strField(b, "unit", "kg"),
                                 numField(b, "price"), app.canteen.getId(), now,
                                 now - static_cast<time_t>(ago * 3600), numField(b, "consumeWithinHours", 0));
        } else {
            throw std::invalid_argument("Type must be RAW or PREPARED");
        }
        app.canteen.addFoodItem(app.inventory, f);
        app.prediction.recordDailyWaste(f->getType(), fmtDate(now), f->getQuantity());
        app.saveAll();
        sendJson(res, json{{"item", f->toJson()}}, 201);
    }));

    svr.Delete(R"(/api/foods/(\w+))", guarded([&](const httplib::Request& req, httplib::Response& res) {
        if (!app.canteen.removeFoodItem(app.inventory, req.matches[1])) return sendError(res, "Food item not found", 404);
        app.saveAll();
        sendJson(res, json{{"removed", std::string(req.matches[1])}});
    }));

    // ---------------- NGOs ----------------
    svr.Get("/api/ngos", guarded([&](const httplib::Request&, httplib::Response& res) {
        json arr = json::array();
        std::string today = fmtDate(nowTs());
        for (const NGO& n : app.ngos.all()) {
            json j = n.toJson();
            j["usedToday"] = round2(app.log.confirmedQtyOn(n.getId(), today));
            j["bucket"] = app.ngos.bucketOf(n.getId());
            arr.push_back(j);
        }
        sendJson(res, json{{"items", arr}});
    }));

    svr.Get(R"(/api/ngos/(\w+))", guarded([&](const httplib::Request& req, httplib::Response& res) {
        std::string id = req.matches[1];
        NGO* n = app.ngos.findNGO(id);   // O(1) hash lookup
        if (!n) return sendError(res, "NGO not found", 404);
        json hist = json::array();
        for (const Order& o : n->viewHistory(app.log)) hist.push_back(o.toJson());
        json pending = json::array();
        for (const Order& o : app.queue.snapshot()) if (o.getNgoId() == id) pending.push_back(o.toJson());
        json j = n->toJson();
        j["usedToday"] = round2(app.log.confirmedQtyOn(id, fmtDate(nowTs())));
        j["bucket"] = app.ngos.bucketOf(id);
        sendJson(res, json{{"ngo", j}, {"history", hist}, {"pending", pending}});
    }));

    // POST /api/ngos { name, contactNumber, address, capacityPerDay, landmark, distanceKm }
    svr.Post("/api/ngos", guarded([&](const httplib::Request& req, httplib::Response& res) {
        json b = body(req);
        std::string id = app.ngos.generateId();
        NGO n(id, strField(b, "name"), strField(b, "contactNumber"), strField(b, "address"), numField(b, "capacityPerDay"));
        std::string landmark = strField(b, "landmark");
        double dist = numField(b, "distanceKm", 0);
        if (!landmark.empty() && dist <= 0) throw std::invalid_argument("Distance from landmark must be greater than zero");
        app.ngos.registerNGO(n);
        if (!landmark.empty()) {
            app.graph.addEdge(id, landmark, dist);
            engineLog().add("Graph", "Added edge " + id + " <-> " + landmark + " (" + num(dist) + " km)");
        }
        app.saveAll();
        sendJson(res, json{{"ngo", n.toJson()}, {"bucket", app.ngos.bucketOf(id)}}, 201);
    }));

    // ---------------- Orders / Queue ----------------
    // POST /api/order { ngoId, foodItemId, quantity }  -> goes into the FIFO queue
    svr.Post("/api/order", guarded([&](const httplib::Request& req, httplib::Response& res) {
        json b = body(req);
        NGO* ngo = app.ngos.findNGO(strField(b, "ngoId"));
        if (!ngo) throw std::invalid_argument("Unknown NGO ID");
        FoodItem* item = app.inventory.findItem(strField(b, "foodItemId"));
        if (!item) throw std::invalid_argument("Food item not found");
        Order o = ngo->placeOrder(app.queue.generateId(), *item, numField(b, "quantity"));
        app.queue.enqueueOrder(o);
        app.saveAll();
        sendJson(res, json{{"order", o.toJson()}, {"queuePosition", app.queue.size()}}, 201);
    }));

    svr.Get("/api/queue", guarded([&](const httplib::Request&, httplib::Response& res) {
        json arr = json::array();
        int pos = 1;
        for (const Order& o : app.queue.snapshot()) { json j = o.toJson(); j["position"] = pos++; arr.push_back(j); }
        sendJson(res, json{{"items", arr}});
    }));

    svr.Post("/api/queue/process", guarded([&](const httplib::Request&, httplib::Response& res) {
        json arr = json::array();
        for (const Order& o : app.engine.processQueue()) arr.push_back(o.toJson());
        app.saveAll();
        sendJson(res, json{{"processed", arr}});
    }));

    // ---------------- Transactions ----------------
    svr.Get("/api/transactions", guarded([&](const httplib::Request& req, httplib::Response& res) {
        int n = req.has_param("n") ? std::stoi(req.get_param_value("n")) : -1;
        json arr = json::array();
        for (const Order& o : app.log.getRecent(n)) arr.push_back(o.toJson());
        sendJson(res, json{{"items", arr}, {"stackDepth", app.log.size()}});
    }));

    // POST /api/transactions/O1006/status { status: PICKED_UP | DISTRIBUTED }
    svr.Post(R"(/api/transactions/(\w+)/status)", guarded([&](const httplib::Request& req, httplib::Response& res) {
        json b = body(req);
        app.log.updateStatus(req.matches[1], strField(b, "status"));
        app.saveAll();
        sendJson(res, app.log.find(req.matches[1])->toJson());
    }));

    // ---------------- Prediction ----------------
    svr.Get("/api/prediction", guarded([&](const httplib::Request& req, httplib::Response& res) {
        int N = req.has_param("window") ? std::stoi(req.get_param_value("window")) : 7;
        if (N < 1 || N > 14) throw std::invalid_argument("Window must be between 1 and 14 days");
        sendJson(res, app.prediction.toJson(N));
    }));

    // ---------------- Graph / nearest NGO ----------------
    svr.Get("/api/nearest", guarded([&](const httplib::Request&, httplib::Response& res) {
        auto r = app.graph.dijkstra("CANTEEN");
        struct Row { std::string id, name; double dist; std::vector<std::string> path; };
        std::vector<NGO> all = app.ngos.all();
        int m = static_cast<int>(all.size());
        Row* rows = new Row[m > 0 ? m : 1];
        int k = 0;
        for (const NGO& n : all) {
            int idx = app.graph.indexOf(n.getId());
            double d = (idx >= 0) ? r.dist[idx] : std::numeric_limits<double>::infinity();
            rows[k++] = Row{n.getId(), n.getName(), d, idx >= 0 && std::isfinite(d) ? app.graph.pathTo(r, idx) : std::vector<std::string>{}};
        }
        mergeSort(rows, k, [](const Row& a, const Row& b) { return a.dist < b.dist; });
        json arr = json::array();
        for (int i = 0; i < k; i++) {
            json j = {{"ngoId", rows[i].id}, {"name", rows[i].name}, {"path", rows[i].path}};
            j["distanceKm"] = std::isfinite(rows[i].dist) ? json(round2(rows[i].dist)) : json(nullptr);
            arr.push_back(j);
        }
        delete[] rows;
        engineLog().add("Graph", "Dijkstra from CANTEEN over " + std::to_string(app.graph.nodeCount()) + " nodes");
        sendJson(res, json{{"items", arr}});
    }));

    svr.Get("/api/landmarks", guarded([&](const httplib::Request&, httplib::Response& res) {
        json arr = json::array();
        for (int i = 0; i < app.graph.nodeCount(); i++) {
            const std::string& nm = app.graph.nameAt(i);
            if (nm != "CANTEEN" && !app.ngos.findNGO(nm)) arr.push_back(nm);
        }
        sendJson(res, json{{"items", arr}});
    }));

    // ---------------- Stats / engine log ----------------
    svr.Get("/api/stats", guarded([&](const httplib::Request&, httplib::Response& res) {
        double availQty = 0, availValue = 0;
        int urgent = 0, expired = 0, items = 0;
        app.inventory.forEach([&](FoodItem* f) {
            if (f->isExpired()) { expired++; return; }
            items++;
            availQty += f->getQuantity();
            availValue += f->getQuantity() * f->getPrice();
            std::string lv = f->urgencyLevel();
            if (lv == "CRITICAL" || lv == "HIGH") urgent++;
        });
        int confirmed = 0, rejected = 0, distributed = 0;
        double savedQty = 0, revenue = 0;
        app.log.forEach([&](const Order& o) {
            if (o.countsAsSaved()) { confirmed++; savedQty += o.getQuantity(); revenue += o.getAmount(); }
            if (o.getStatus() == "REJECTED") rejected++;
            if (o.getStatus() == "DISTRIBUTED") distributed++;
        });
        double accRaw = 0, accPrep = 0;
        app.prediction.backtest("RAW", 7, accRaw);
        app.prediction.backtest("PREPARED", 7, accPrep);

        sendJson(res, json{
            {"availableItems", items}, {"availableQty", round2(availQty)}, {"availableValue", round2(availValue)},
            {"urgentItems", urgent}, {"expiredItems", expired},
            {"pendingOrders", app.queue.size()}, {"transactions", app.log.size()},
            {"confirmed", confirmed}, {"rejected", rejected}, {"distributed", distributed},
            {"foodSavedKg", round2(savedQty)}, {"mealsEquivalent", static_cast<int>(savedQty / 0.4)},
            {"revenue", round2(revenue)}, {"ngoCount", app.ngos.size()},
            {"predictionAccuracy", round2((accRaw + accPrep) / 2)},
            {"structures", {
                {"linkedList", app.inventory.size()}, {"minHeap", app.inventory.heapSize()},
                {"queue", app.queue.size()}, {"stack", app.log.size()},
                {"hashTable", app.ngos.size()}, {"hashBuckets", app.ngos.buckets()},
                {"historyDays", app.prediction.daysStored()}, {"graphNodes", app.graph.nodeCount()}
            }}
        });
    }));

    svr.Get("/api/log", guarded([&](const httplib::Request&, httplib::Response& res) {
        sendJson(res, json{{"items", engineLog().toJson()}});
    }));

    // ---------------- Website ----------------
    if (!svr.set_mount_point("/", frontendDir)) {
        std::cerr << "Could not find the frontend folder at " << frontendDir << "\n";
        return 1;
    }

    std::cout << "\n  FoodBridge is running ->  http://localhost:" << port << "\n"
              << "  API base               ->  http://localhost:" << port << "/api\n"
              << "  Press Ctrl+C to stop.\n\n";
    if (!svr.listen("0.0.0.0", port)) {
        std::cerr << "Could not start server on port " << port << " (is it already in use? try --port 9090)\n";
        return 1;
    }
    return 0;
}

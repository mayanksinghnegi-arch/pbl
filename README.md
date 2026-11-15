# FoodBridge — Food Waste Prediction and Redistribution System

A web platform that connects a college canteen with NGOs. Canteen staff record leftover **Raw Food** and **Prepared Food**; the engine ranks it by how soon it spoils, NGOs request it, every hand-over is logged, and a moving-average model predicts tomorrow's surplus.

The core engine is **C++ (OOP)** built on **hand-written data structures** (linked list, stack, queue, min-heap, hash table, merge/quick sort, binary search, graph + Dijkstra). The website is plain **HTML + CSS + JavaScript** and talks to the engine through a small HTTP/JSON API (`cpp-httplib` + `nlohmann/json`, both header-only and already included).

---

## 1. Run it

You need a C++17 compiler (g++ 9 or newer). Nothing else to install.

### Windows (MinGW-w64 / MSYS2)
```
run.bat
```
or manually, from the project folder:
```
g++ -std=c++17 -O2 -D_WIN32_WINNT=0x0A00 backend/main.cpp -o backend/foodbridge.exe -lws2_32
backend\foodbridge.exe
```
> Use a recent MinGW-w64 (MSYS2 "UCRT64" g++ works). The old MinGW from Code::Blocks 17 is too old for cpp-httplib.

### Linux / macOS
```
chmod +x run.sh && ./run.sh
```
or: `make run`

Then open **http://localhost:8080** in your browser.

* The first build takes ~30 s (httplib is a big header). Later runs start instantly.
* First run creates sample data in `data/` (4 NGOs, 10 food items, 28 days of waste history, a few past transactions). Delete the `data/` folder to reset to fresh sample data.
* Port busy? `backend/foodbridge --port 9090`

### Other modes
| Command | What it does |
|---|---|
| `backend/foodbridge --demo` | Console demo: prints inventory with `operator<<`, heap top-3, hash lookup, Dijkstra, prediction. Good for viva. |
| `make test` (or `g++ -std=c++17 backend/tests.cpp -o tests && ./tests`) | 42 unit + integration tests of every structure and rule. |

---

## 2. The website (4 pages)

| Page | Who | What it shows |
|---|---|---|
| **Overview** | Everyone | KPIs (surplus available, food saved, ≈ meals, requests waiting), top 3 most urgent items (min-heap), tomorrow's forecast, the system flow. |
| **Canteen Staff** | Canteen | Form to record Raw/Prepared food (fields change by type) · inventory with urgency badges and time-left bars · FIFO request queue + **Process queue** button with confirm/reject results · orders awaiting pick-up · forecast bar charts · nearest NGOs by road distance (Dijkstra). |
| **NGO Portal** | NGOs | Pick which NGO you are acting as, or register a new one · daily capacity used · food cards with filter (Raw/Prepared), sort (spoils soonest / price / name), max price (binary search) · request dialog with live amount · "My requests" with **Mark distributed**. |
| **Admin** | Admin | Transaction history (stack, newest first) with status filter · prediction back-test chart (actual vs predicted) with selectable window N · live sizes of every data structure · engine activity log showing which structure handled each operation. |

### Demo script for evaluation (≈ 3 minutes)
1. **Canteen** → add a Prepared item (e.g. *Poha, 3 kg, ₹25, cooked 1 h ago, safe for 5 h*). It appears in the inventory sorted by urgency; Overview's "Claim these first" updates.
2. **NGO Portal** → acting as *Annapurna Seva Trust*, request 3 kg of *Rajma Curry* (4 kg available).
3. Switch to *Asha Community Kitchen* and request 2 kg of the same Rajma Curry.
4. **Canteen** → both requests sit in the queue as #1 and #2. Click **Process queue**: #1 is confirmed, #2 is rejected ("Insufficient stock: requested 2, available 1") — FIFO fairness.
5. Click **Mark picked up**; then in the NGO portal click **Mark distributed** — the full lifecycle.
6. **Admin** → transaction appears at the top of the stack; engine log shows Queue → MatchingEngine → Stack; structure sizes changed.
7. Try invalid input (negative quantity, 5-digit phone number) to show exception handling.

---

## 3. Where each concept from the report lives

### Data structures (`backend/dsa.h`, used in `backend/managers.h`)
| Structure | Implementation | Used for |
|---|---|---|
| Generic `Node<T>` (template) | `dsa.h` | one node type for list, stack, queue, hash chains |
| Singly linked list | `LinkedList<T>` | `InventoryManager` — food items |
| Min-heap | `MinHeap<T, Less>` (array) | `InventoryManager` — most urgent item at the top |
| Queue (FIFO) | `Queue<T>` (front/rear pointers) | `OrderQueue` — NGO requests |
| Stack (LIFO) | `Stack<T>` | `TransactionLog` — newest first |
| Hash table (chaining, djb2) | `HashTable<V>` | `NGOManager` — O(1) lookup by NGO ID |
| Dynamic array | `DynamicArray<T>` (doubling) | `PredictionEngine` — daily waste history |
| Merge sort / Quick sort | `mergeSort`, `quickSort` | food listing by urgency/price (merge), by name (quick) |
| Binary search | `upperBound`, `binarySearch` | "price ≤ ₹X" filter; `GET /api/foods/F105` lookup by ID |
| Graph + Dijkstra | `Graph` (adjacency lists, O(V²)) | nearest NGO from the canteen |
| Moving average | `PredictionEngine::predictNextDay` | tomorrow's surplus |

### OOP (`backend/models.h`)
| Concept | Where |
|---|---|
| Classes & objects | `FoodItem`, `RawFood`, `PreparedFood`, `NGO`, `Canteen`, `Order`, all managers |
| Encapsulation | private members + getters/validated setters |
| Inheritance | `FoodItem → RawFood, PreparedFood` |
| Abstraction | `FoodItem` is abstract (`getUrgencyScore()`, `display()` are pure virtual) |
| Runtime polymorphism | `virtual getUrgencyScore()`, `display()`, `toJson()`, `serialize()` |
| Constructors / destructors | parameterised constructors; destructors free list/heap/hash memory |
| Operator overloading | `operator<` (heap ordering by urgency), `operator<<` (printing) |
| File handling | `DataStore` (fstream) → `data/*.txt` |
| Exception handling | setters/constructors `throw invalid_argument`; server catches and returns `400 {"error": ...}` |
| Templates | `Node<T>`, `LinkedList<T>`, `Stack<T>`, `Queue<T>`, `MinHeap<T,Less>`, `HashTable<V>` |

### Core algorithms (report §7)
* **Urgency** — `RawFood`: `shelfLifeDays×24 − hoursSinceAdded`; `PreparedFood`: `consumeWithinHours − hoursSinceCooked`. Both are in **hours** so raw and prepared items can be compared in the same heap. Lower = more urgent. Levels: ≤0 Expired, ≤6 h Critical, ≤24 h High, ≤72 h Medium, else Low.
* **Matching** (`MatchingEngine::processQueue`) — dequeue in FIFO order; reject if NGO unknown, item gone, item expired, stock insufficient, or NGO's daily capacity exceeded; otherwise reduce stock, confirm, and delete the item when it reaches 0. Every result is pushed onto the transaction stack.
* **Prediction** — every food entry adds its quantity to today's total for its category; tomorrow = average of the last N completed days (N = 7). Admin shows a back-test: for each past day, the prediction from the N days before it vs. the actual, and accuracy = 100 − MAPE.

---

## 4. API (test with Postman)

| Method | Endpoint | Body / params |
|---|---|---|
| GET | `/api/foods` | `?sort=urgency\|price\|name&type=RAW\|PREPARED&maxPrice=40&includeExpired=0` |
| GET | `/api/foods/urgent?k=3` | heap top-k |
| GET | `/api/foods/{id}` | binary search by ID |
| POST | `/api/foods` | `{type:"RAW", name, quantity, unit, price, storageType, shelfLifeDays}` or `{type:"PREPARED", name, quantity, unit, price, cookedHoursAgo, consumeWithinHours}` |
| DELETE | `/api/foods/{id}` | |
| GET | `/api/ngos`, `/api/ngos/{id}` | `{id}` returns profile + history + pending |
| POST | `/api/ngos` | `{name, contactNumber, address, capacityPerDay, landmark, distanceKm}` |
| POST | `/api/order` | `{ngoId, foodItemId, quantity}` → enqueued |
| GET | `/api/queue` | |
| POST | `/api/queue/process` | runs the matching engine |
| GET | `/api/transactions?n=10` | newest first |
| POST | `/api/transactions/{id}/status` | `{status:"PICKED_UP"\|"DISTRIBUTED"}` |
| GET | `/api/prediction?window=7` | history, prediction, back-test, accuracy |
| GET | `/api/nearest` | Dijkstra distances + paths |
| GET | `/api/landmarks`, `/api/stats`, `/api/log`, `/api/canteen` | |

---

## 5. Project structure
```
foodbridge/
├── backend/
│   ├── dsa.h          hand-written data structures & algorithms
│   ├── models.h       OOP classes: FoodItem, RawFood, PreparedFood, Order, NGO, Canteen
│   ├── managers.h     InventoryManager, NGOManager, OrderQueue, MatchingEngine,
│   │                  TransactionLog, PredictionEngine, DataStore
│   ├── main.cpp       HTTP server + API endpoints + sample-data seeding + --demo
│   ├── tests.cpp      42 unit / integration tests
│   ├── httplib.h      cpp-httplib (MIT licence, third-party)
│   └── json.hpp       nlohmann/json (MIT licence, third-party)
├── frontend/
│   ├── index.html     4 pages: Overview, Canteen Staff, NGO Portal, Admin
│   ├── style.css
│   └── app.js         fetch() calls, rendering, SVG charts (no external libraries)
├── data/              created on first run (foods, ngos, transactions, pending_orders,
│                      waste_history, graph — pipe-separated .txt)
├── Makefile, run.sh, run.bat
└── README.md
```

## 6. Team split (matches report §10)
* **Member 1 — Data & documentation:** replace the sample data in `main.cpp → App::seed()` with the real canteen readings; this README + report.
* **Member 2 — DSA:** `dsa.h`, `tests.cpp` (data-structure section).
* **Member 3 — OOP backend:** `models.h`, `managers.h`, `main.cpp` (API + file handling).
* **Member 4 — Frontend & integration:** `frontend/*`, end-to-end demo script above.

## 7. Notes & limits
* No login: the NGO picker stands in for authentication (fine for a PBL prototype; add passwords in Future Scope).
* Moving average ignores the weekly pattern (weekends cook less), so accuracy is ~65–78% on the sample data. A weekday-aware average is an easy upgrade.
* Prices are per unit (₹/kg or ₹/litre) as set by the canteen.

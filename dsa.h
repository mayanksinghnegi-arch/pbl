// =====================================================================
//  dsa.h  —  Hand-written Data Structures & Algorithms
//  Food Waste Prediction and Redistribution System (PBL)
//
//  Every structure here is implemented manually with pointers / arrays
//  (no STL containers inside the structures) so the DSA logic is visible.
//
//   Node<T>            generic node (template) reused by list/stack/queue
//   LinkedList<T>      singly linked list         -> food inventory
//   Stack<T>           LIFO                       -> transaction log
//   Queue<T>           FIFO                       -> NGO order requests
//   MinHeap<T,Less>    binary min-heap            -> urgency priority
//   DynamicArray<T>    growable array             -> waste history
//   HashTable<V>       chaining, djb2 hash        -> NGO lookup by ID
//   mergeSort/quickSort, upperBound/binarySearch  -> food listing
//   Graph + Dijkstra                              -> nearest NGO
// =====================================================================
#pragma once
#include <string>
#include <stdexcept>
#include <vector>
#include <limits>

// ---------------------------------------------------------------------
// Generic node (template) — one structure for FoodItem*, Order, string...
// ---------------------------------------------------------------------
template <typename T>
struct Node {
    T data;
    Node<T>* next;
    explicit Node(const T& d) : data(d), next(nullptr) {}
};

// ---------------------------------------------------------------------
// Singly Linked List
//   pushBack O(1) (tail pointer), pushFront O(1), find/remove O(n)
// ---------------------------------------------------------------------
template <typename T>
class LinkedList {
    Node<T>* head = nullptr;
    Node<T>* tail = nullptr;
    int count = 0;

public:
    LinkedList() = default;
    ~LinkedList() { clear(); }
    LinkedList(const LinkedList&) = delete;
    LinkedList& operator=(const LinkedList&) = delete;

    void pushBack(const T& value) {
        Node<T>* n = new Node<T>(value);
        if (!head) head = tail = n;
        else { tail->next = n; tail = n; }
        count++;
    }

    void pushFront(const T& value) {
        Node<T>* n = new Node<T>(value);
        n->next = head;
        head = n;
        if (!tail) tail = n;
        count++;
    }

    // Removes the first node whose data matches the predicate.
    template <typename Pred>
    bool removeFirst(Pred match, T* removedOut = nullptr) {
        Node<T>* prev = nullptr;
        Node<T>* cur = head;
        while (cur) {
            if (match(cur->data)) {
                if (prev) prev->next = cur->next;
                else head = cur->next;
                if (cur == tail) tail = prev;
                if (removedOut) *removedOut = cur->data;
                delete cur;
                count--;
                return true;
            }
            prev = cur;
            cur = cur->next;
        }
        return false;
    }

    void removeLast() {
        if (!head) return;
        if (head == tail) { delete head; head = tail = nullptr; count = 0; return; }
        Node<T>* cur = head;
        while (cur->next != tail) cur = cur->next;
        delete tail;
        tail = cur;
        tail->next = nullptr;
        count--;
    }

    template <typename Pred>
    T* find(Pred match) {
        for (Node<T>* c = head; c; c = c->next)
            if (match(c->data)) return &c->data;
        return nullptr;
    }

    template <typename F>
    void forEach(F f) { for (Node<T>* c = head; c; c = c->next) f(c->data); }
    template <typename F>
    void forEach(F f) const { for (Node<T>* c = head; c; c = c->next) f(c->data); }

    void clear() {
        while (head) { Node<T>* n = head; head = head->next; delete n; }
        tail = nullptr;
        count = 0;
    }

    int size() const { return count; }
    bool empty() const { return count == 0; }
};

// ---------------------------------------------------------------------
// Stack (LIFO) — linked implementation, push/pop O(1)
// ---------------------------------------------------------------------
template <typename T>
class Stack {
    Node<T>* topNode = nullptr;
    int count = 0;

public:
    Stack() = default;
    ~Stack() { while (!empty()) pop(); }
    Stack(const Stack&) = delete;
    Stack& operator=(const Stack&) = delete;

    void push(const T& value) {
        Node<T>* n = new Node<T>(value);
        n->next = topNode;
        topNode = n;
        count++;
    }

    T pop() {
        if (empty()) throw std::underflow_error("Stack underflow");
        Node<T>* n = topNode;
        T value = n->data;
        topNode = n->next;
        delete n;
        count--;
        return value;
    }

    T& top() {
        if (empty()) throw std::underflow_error("Stack is empty");
        return topNode->data;
    }

    // Traverses from TOP (most recent) to BOTTOM (oldest)
    template <typename F>
    void forEach(F f) { for (Node<T>* c = topNode; c; c = c->next) f(c->data); }
    template <typename F>
    void forEach(F f) const { for (Node<T>* c = topNode; c; c = c->next) f(c->data); }

    bool empty() const { return count == 0; }
    int size() const { return count; }
};

// ---------------------------------------------------------------------
// Queue (FIFO) — linked implementation with front & rear, O(1) ops
// ---------------------------------------------------------------------
template <typename T>
class Queue {
    Node<T>* frontNode = nullptr;
    Node<T>* rearNode = nullptr;
    int count = 0;

public:
    Queue() = default;
    ~Queue() { while (!empty()) dequeue(); }
    Queue(const Queue&) = delete;
    Queue& operator=(const Queue&) = delete;

    void enqueue(const T& value) {
        Node<T>* n = new Node<T>(value);
        if (!rearNode) frontNode = rearNode = n;
        else { rearNode->next = n; rearNode = n; }
        count++;
    }

    T dequeue() {
        if (empty()) throw std::underflow_error("Queue underflow");
        Node<T>* n = frontNode;
        T value = n->data;
        frontNode = n->next;
        if (!frontNode) rearNode = nullptr;
        delete n;
        count--;
        return value;
    }

    T& front() {
        if (empty()) throw std::underflow_error("Queue is empty");
        return frontNode->data;
    }

    // Traverses front -> rear (arrival order)
    template <typename F>
    void forEach(F f) const { for (Node<T>* c = frontNode; c; c = c->next) f(c->data); }

    bool empty() const { return count == 0; }
    int size() const { return count; }
};

// ---------------------------------------------------------------------
// Dynamic Array — doubles capacity when full (amortised O(1) push)
// ---------------------------------------------------------------------
template <typename T>
class DynamicArray {
    T* arr;
    int cap;
    int n = 0;

    void grow() {
        int newCap = cap * 2;
        T* bigger = new T[newCap];
        for (int i = 0; i < n; i++) bigger[i] = arr[i];
        delete[] arr;
        arr = bigger;
        cap = newCap;
    }

public:
    explicit DynamicArray(int initialCap = 8) : arr(new T[initialCap]), cap(initialCap) {}
    ~DynamicArray() { delete[] arr; }
    DynamicArray(const DynamicArray&) = delete;
    DynamicArray& operator=(const DynamicArray&) = delete;

    void push(const T& v) { if (n == cap) grow(); arr[n++] = v; }

    T& operator[](int i) {
        if (i < 0 || i >= n) throw std::out_of_range("DynamicArray index out of range");
        return arr[i];
    }
    const T& operator[](int i) const {
        if (i < 0 || i >= n) throw std::out_of_range("DynamicArray index out of range");
        return arr[i];
    }

    int size() const { return n; }
    int capacity() const { return cap; }
    void clear() { n = 0; }
};

// ---------------------------------------------------------------------
// Min-Heap (array based) — smallest element (by Less) always at index 0
//   insert O(log n), extractMin O(log n), peek O(1)
// ---------------------------------------------------------------------
template <typename T, typename Less>
class MinHeap {
    T* arr;
    int cap;
    int n = 0;
    Less less;

    void swapAt(int i, int j) { T t = arr[i]; arr[i] = arr[j]; arr[j] = t; }

    void grow() {
        int newCap = cap * 2;
        T* bigger = new T[newCap];
        for (int i = 0; i < n; i++) bigger[i] = arr[i];
        delete[] arr;
        arr = bigger;
        cap = newCap;
    }

    void siftUp(int i) {
        while (i > 0) {
            int parent = (i - 1) / 2;
            if (less(arr[i], arr[parent])) { swapAt(i, parent); i = parent; }
            else break;
        }
    }

    void siftDown(int i) {
        while (true) {
            int l = 2 * i + 1, r = 2 * i + 2, smallest = i;
            if (l < n && less(arr[l], arr[smallest])) smallest = l;
            if (r < n && less(arr[r], arr[smallest])) smallest = r;
            if (smallest == i) break;
            swapAt(i, smallest);
            i = smallest;
        }
    }

public:
    explicit MinHeap(int initialCap = 16) : arr(new T[initialCap]), cap(initialCap) {}
    ~MinHeap() { delete[] arr; }
    MinHeap(const MinHeap&) = delete;
    MinHeap& operator=(const MinHeap&) = delete;

    void insert(const T& v) {
        if (n == cap) grow();
        arr[n] = v;
        siftUp(n);
        n++;
    }

    T extractMin() {
        if (n == 0) throw std::underflow_error("Heap is empty");
        T top = arr[0];
        arr[0] = arr[--n];
        if (n > 0) siftDown(0);
        return top;
    }

    const T& peek() const {
        if (n == 0) throw std::underflow_error("Heap is empty");
        return arr[0];
    }

    void clear() { n = 0; }
    int size() const { return n; }
    bool empty() const { return n == 0; }
};

// ---------------------------------------------------------------------
// Hash Table with separate chaining (string key -> V)
//   djb2 hash, average O(1) insert / find / remove
// ---------------------------------------------------------------------
template <typename V>
class HashTable {
    struct Entry { std::string key; V value; };
    Node<Entry>** buckets;
    int bucketCount;
    int count = 0;

public:
    explicit HashTable(int nBuckets = 31) : buckets(new Node<Entry>*[nBuckets]), bucketCount(nBuckets) {
        for (int i = 0; i < bucketCount; i++) buckets[i] = nullptr;
    }
    ~HashTable() {
        for (int i = 0; i < bucketCount; i++) {
            Node<Entry>* c = buckets[i];
            while (c) { Node<Entry>* nx = c->next; delete c; c = nx; }
        }
        delete[] buckets;
    }
    HashTable(const HashTable&) = delete;
    HashTable& operator=(const HashTable&) = delete;

    int bucketOf(const std::string& key) const {
        unsigned long h = 5381;
        for (char ch : key) h = ((h << 5) + h) + static_cast<unsigned char>(ch);   // h*33 + c
        return static_cast<int>(h % bucketCount);
    }

    // Returns false if the key already exists
    bool insert(const std::string& key, const V& value) {
        int idx = bucketOf(key);
        for (Node<Entry>* c = buckets[idx]; c; c = c->next)
            if (c->data.key == key) return false;
        Node<Entry>* n = new Node<Entry>(Entry{key, value});
        n->next = buckets[idx];          // insert at head of the chain
        buckets[idx] = n;
        count++;
        return true;
    }

    V* find(const std::string& key) {
        int idx = bucketOf(key);
        for (Node<Entry>* c = buckets[idx]; c; c = c->next)
            if (c->data.key == key) return &c->data.value;
        return nullptr;
    }

    bool remove(const std::string& key) {
        int idx = bucketOf(key);
        Node<Entry>* prev = nullptr;
        for (Node<Entry>* c = buckets[idx]; c; prev = c, c = c->next) {
            if (c->data.key == key) {
                if (prev) prev->next = c->next; else buckets[idx] = c->next;
                delete c;
                count--;
                return true;
            }
        }
        return false;
    }

    int chainLength(int idx) const {
        int len = 0;
        for (Node<Entry>* c = buckets[idx]; c; c = c->next) len++;
        return len;
    }

    template <typename F>
    void forEach(F f) {
        for (int i = 0; i < bucketCount; i++)
            for (Node<Entry>* c = buckets[i]; c; c = c->next) f(c->data.key, c->data.value);
    }

    int size() const { return count; }
    int buckets_() const { return bucketCount; }
};

// ---------------------------------------------------------------------
// Merge Sort (stable)  O(n log n)  — returns number of comparisons made
// ---------------------------------------------------------------------
template <typename T, typename Less>
void mergeSortRec(T* a, T* tmp, int lo, int hi, Less& less, long long& comps) {
    if (lo >= hi) return;
    int mid = lo + (hi - lo) / 2;
    mergeSortRec(a, tmp, lo, mid, less, comps);
    mergeSortRec(a, tmp, mid + 1, hi, less, comps);
    int i = lo, j = mid + 1, k = lo;
    while (i <= mid && j <= hi) {
        comps++;
        if (less(a[j], a[i])) tmp[k++] = a[j++];   // take right only if strictly smaller -> stable
        else tmp[k++] = a[i++];
    }
    while (i <= mid) tmp[k++] = a[i++];
    while (j <= hi) tmp[k++] = a[j++];
    for (int x = lo; x <= hi; x++) a[x] = tmp[x];
}

template <typename T, typename Less>
long long mergeSort(T* a, int n, Less less) {
    long long comps = 0;
    if (n < 2) return 0;
    T* tmp = new T[n];
    mergeSortRec(a, tmp, 0, n - 1, less, comps);
    delete[] tmp;
    return comps;
}

// ---------------------------------------------------------------------
// Quick Sort (Lomuto partition)  avg O(n log n)
// ---------------------------------------------------------------------
template <typename T, typename Less>
void quickSortRec(T* a, int lo, int hi, Less& less, long long& comps) {
    if (lo >= hi) return;
    T pivot = a[hi];
    int i = lo - 1;
    for (int j = lo; j < hi; j++) {
        comps++;
        if (less(a[j], pivot)) { i++; T t = a[i]; a[i] = a[j]; a[j] = t; }
    }
    T t = a[i + 1]; a[i + 1] = a[hi]; a[hi] = t;
    quickSortRec(a, lo, i, less, comps);
    quickSortRec(a, i + 2, hi, less, comps);
}

template <typename T, typename Less>
long long quickSort(T* a, int n, Less less) {
    long long comps = 0;
    if (n > 1) quickSortRec(a, 0, n - 1, less, comps);
    return comps;
}

// ---------------------------------------------------------------------
// Binary Search helpers (array must be sorted ascending by key)
// ---------------------------------------------------------------------
// First index whose key is > target  (=> items [0, idx) have key <= target)
template <typename T, typename KeyFn>
int upperBound(T* a, int n, double target, KeyFn key, int* stepsOut = nullptr) {
    int lo = 0, hi = n, steps = 0;
    while (lo < hi) {
        steps++;
        int mid = lo + (hi - lo) / 2;
        if (key(a[mid]) <= target) lo = mid + 1;
        else hi = mid;
    }
    if (stepsOut) *stepsOut = steps;
    return lo;
}

// Exact-match search on a string key; returns index or -1
template <typename T, typename KeyFn>
int binarySearch(T* a, int n, const std::string& target, KeyFn key, int* stepsOut = nullptr) {
    int lo = 0, hi = n - 1, steps = 0;
    while (lo <= hi) {
        steps++;
        int mid = lo + (hi - lo) / 2;
        const std::string k = key(a[mid]);
        if (k == target) { if (stepsOut) *stepsOut = steps; return mid; }
        if (k < target) lo = mid + 1;
        else hi = mid - 1;
    }
    if (stepsOut) *stepsOut = steps;
    return -1;
}

// ---------------------------------------------------------------------
// Weighted undirected Graph (adjacency lists) + Dijkstra  O(V^2)
// ---------------------------------------------------------------------
class Graph {
public:
    struct Edge { int to; double w; };
    struct Result { std::vector<double> dist; std::vector<int> prev; int source = -1; };

private:
    DynamicArray<std::string> names;
    DynamicArray<LinkedList<Edge>*> adj;

public:
    Graph() = default;
    ~Graph() { for (int i = 0; i < adj.size(); i++) delete adj[i]; }

    int indexOf(const std::string& name) const {
        for (int i = 0; i < names.size(); i++) if (names[i] == name) return i;
        return -1;
    }

    int addNode(const std::string& name) {
        int i = indexOf(name);
        if (i >= 0) return i;
        names.push(name);
        adj.push(new LinkedList<Edge>());
        return names.size() - 1;
    }

    void addEdge(const std::string& a, const std::string& b, double w) {
        if (w <= 0) throw std::invalid_argument("Distance must be positive");
        int u = addNode(a), v = addNode(b);
        adj[u]->pushBack(Edge{v, w});
        adj[v]->pushBack(Edge{u, w});
    }

    Result dijkstra(const std::string& src) const {
        const double INF = std::numeric_limits<double>::infinity();
        int V = names.size();
        Result r;
        r.dist.assign(V, INF);
        r.prev.assign(V, -1);
        r.source = indexOf(src);
        if (r.source < 0) return r;
        std::vector<bool> done(V, false);
        r.dist[r.source] = 0;
        for (int iter = 0; iter < V; iter++) {
            int u = -1;                               // pick closest unvisited node
            for (int i = 0; i < V; i++)
                if (!done[i] && (u == -1 || r.dist[i] < r.dist[u])) u = i;
            if (u == -1 || r.dist[u] == INF) break;
            done[u] = true;
            adj[u]->forEach([&](const Edge& e) {      // relax neighbours
                if (r.dist[u] + e.w < r.dist[e.to]) {
                    r.dist[e.to] = r.dist[u] + e.w;
                    r.prev[e.to] = u;
                }
            });
        }
        return r;
    }

    std::vector<std::string> pathTo(const Result& r, int target) const {
        std::vector<std::string> rev;
        for (int v = target; v != -1; v = r.prev[v]) rev.push_back(names[v]);
        return std::vector<std::string>(rev.rbegin(), rev.rend());
    }

    // Each undirected edge once (u < v) — used for saving to file
    template <typename F>
    void forEachEdge(F f) const {
        for (int u = 0; u < adj.size(); u++)
            adj[u]->forEach([&](const Edge& e) { if (u < e.to) f(names[u], names[e.to], e.w); });
    }

    const std::string& nameAt(int i) const { return names[i]; }
    int nodeCount() const { return names.size(); }
};

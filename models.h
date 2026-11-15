// =====================================================================
//  models.h  —  OOP entity classes
//
//  FoodItem (abstract)  <- RawFood, PreparedFood     (inheritance)
//  getUrgencyScore(), display()  pure virtual        (abstraction / runtime polymorphism)
//  operator<  (heap ordering), operator<< (printing) (operator overloading)
//  private members + validated setters that throw    (encapsulation + exceptions)
//  Order, NGO, Canteen
// =====================================================================
#pragma once
#include <string>
#include <ctime>
#include <cmath>
#include <cctype>
#include <algorithm>
#include <sstream>
#include <iostream>
#include <stdexcept>
#include <vector>
#include "json.hpp"

using json = nlohmann::json;

// ---------------- small helpers ----------------
inline time_t nowTs() { return std::time(nullptr); }
inline double hoursBetween(time_t from, time_t to) { return std::difftime(to, from) / 3600.0; }
inline double round2(double v) { return std::round(v * 100.0) / 100.0; }

inline std::string fmtDate(time_t t) {
    std::tm tmv = *std::localtime(&t);
    char buf[16];
    std::strftime(buf, sizeof buf, "%Y-%m-%d", &tmv);
    return buf;
}
inline std::string fmtTime(time_t t) {
    std::tm tmv = *std::localtime(&t);
    char buf[32];
    std::strftime(buf, sizeof buf, "%d %b %Y, %H:%M", &tmv);
    return buf;
}

inline std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}
// '|' is the field separator in our data files, so keep it out of text fields
inline std::string clean(const std::string& s) {
    std::string out = trim(s);
    for (char& c : out) if (c == '|' || c == '\n' || c == '\r') c = '/';
    return out;
}
inline std::vector<std::string> split(const std::string& s, char sep = '|') {
    std::vector<std::string> parts;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, sep)) parts.push_back(item);
    if (!s.empty() && s.back() == sep) parts.push_back("");
    return parts;
}

class TransactionLog;   // forward declarations (defined in managers.h)
class InventoryManager;

// =====================================================================
//  FoodItem — abstract base class
// =====================================================================
class FoodItem {
private:
    std::string id, name, unit, canteenId;
    double quantity = 0;
    double price = 0;        // price per unit (₹) charged to the NGO
    time_t dateAdded;

public:
    FoodItem(const std::string& id, const std::string& name, double qty, const std::string& unit,
             double price, const std::string& canteenId, time_t dateAdded)
        : id(id), unit(clean(unit)), canteenId(canteenId), dateAdded(dateAdded) {
        if (qty <= 0) throw std::invalid_argument("Quantity must be greater than zero");
        setName(name);
        setQuantity(qty);
        setPrice(price);
        if (this->unit.empty()) this->unit = "kg";
    }
    virtual ~FoodItem() = default;

    // ---- pure virtual: every derived class MUST define these ----
    virtual double getUrgencyScore() const = 0;     // hours left before it spoils (lower = more urgent)
    virtual std::string display() const = 0;
    virtual std::string getType() const = 0;        // "RAW" | "PREPARED"
    virtual std::string serialize() const = 0;

    virtual json toJson() const {
        double h = getUrgencyScore();
        return json{
            {"id", id}, {"name", name}, {"type", getType()},
            {"quantity", round2(quantity)}, {"unit", unit}, {"price", round2(price)},
            {"value", round2(quantity * price)}, {"canteenId", canteenId},
            {"dateAdded", fmtTime(dateAdded)},
            {"urgencyHours", round2(h)}, {"urgencyLevel", urgencyLevel()}
        };
    }

    std::string urgencyLevel() const {
        double h = getUrgencyScore();
        if (h <= 0) return "EXPIRED";
        if (h <= 6) return "CRITICAL";
        if (h <= 24) return "HIGH";
        if (h <= 72) return "MEDIUM";
        return "LOW";
    }
    bool isExpired() const { return getUrgencyScore() <= 0; }

    // ---- getters ----
    const std::string& getId() const { return id; }
    const std::string& getName() const { return name; }
    const std::string& getUnit() const { return unit; }
    const std::string& getCanteenId() const { return canteenId; }
    double getQuantity() const { return quantity; }
    double getPrice() const { return price; }
    time_t getDateAdded() const { return dateAdded; }

    // ---- validated setters (throw on bad data) ----
    void setName(const std::string& n) {
        if (trim(n).empty()) throw std::invalid_argument("Food name cannot be empty");
        name = clean(n);
    }
    void setQuantity(double q) {
        if (q < 0 || std::isnan(q)) throw std::invalid_argument("Quantity cannot be negative");
        quantity = q;
    }
    void setPrice(double p) {
        if (p < 0 || std::isnan(p)) throw std::invalid_argument("Price cannot be negative");
        price = p;
    }

    // ---- operator overloading ----
    bool operator<(const FoodItem& other) const { return getUrgencyScore() < other.getUrgencyScore(); }
    friend std::ostream& operator<<(std::ostream& os, const FoodItem& f) { return os << f.display(); }

protected:
    std::string baseSerialize() const {
        std::ostringstream o;
        o << id << '|' << name << '|' << quantity << '|' << unit << '|' << price << '|'
          << canteenId << '|' << static_cast<long long>(dateAdded);
        return o.str();
    }
};

// =====================================================================
//  RawFood — unused ingredient stock (vegetables, grains, dairy...)
// =====================================================================
class RawFood : public FoodItem {
private:
    std::string storageType;   // Room / Refrigerated / Frozen
    int shelfLifeDays;

public:
    RawFood(const std::string& id, const std::string& name, double qty, const std::string& unit,
            double price, const std::string& canteenId, time_t added,
            const std::string& storageType, int shelfLifeDays)
        : FoodItem(id, name, qty, unit, price, canteenId, added),
          storageType(clean(storageType).empty() ? "Room" : clean(storageType)) {
        if (shelfLifeDays <= 0) throw std::invalid_argument("Shelf life must be at least 1 day");
        this->shelfLifeDays = shelfLifeDays;
    }

    // urgency = shelfLifeDays - daysSinceAdded   (computed in hours so it is comparable with PreparedFood)
    double getUrgencyScore() const override {
        return shelfLifeDays * 24.0 - hoursBetween(getDateAdded(), nowTs());
    }
    std::string getType() const override { return "RAW"; }

    std::string display() const override {
        std::ostringstream o;
        o << "[RAW]      " << getId() << "  " << getName() << "  " << getQuantity() << " " << getUnit()
          << "  @ Rs." << getPrice() << "/" << getUnit() << "  | " << storageType
          << "  | " << round2(getUrgencyScore()) << " h left (" << urgencyLevel() << ")";
        return o.str();
    }

    std::string serialize() const override {
        return "R|" + baseSerialize() + "|" + storageType + "|" + std::to_string(shelfLifeDays);
    }

    json toJson() const override {
        json j = FoodItem::toJson();
        j["storageType"] = storageType;
        j["shelfLifeDays"] = shelfLifeDays;
        return j;
    }
};

// =====================================================================
//  PreparedFood — cooked surplus left after serving hours
// =====================================================================
class PreparedFood : public FoodItem {
private:
    time_t cookedTime;
    double consumeWithinHours;

public:
    PreparedFood(const std::string& id, const std::string& name, double qty, const std::string& unit,
                 double price, const std::string& canteenId, time_t added,
                 time_t cookedTime, double consumeWithinHours)
        : FoodItem(id, name, qty, unit, price, canteenId, added), cookedTime(cookedTime) {
        if (consumeWithinHours <= 0) throw std::invalid_argument("Consume-within hours must be greater than zero");
        if (std::difftime(cookedTime, nowTs()) > 300) throw std::invalid_argument("Cooked time cannot be in the future");
        this->consumeWithinHours = consumeWithinHours;
    }

    // urgency = consumeWithinHours - hoursSinceCooked
    double getUrgencyScore() const override {
        return consumeWithinHours - hoursBetween(cookedTime, nowTs());
    }
    std::string getType() const override { return "PREPARED"; }

    std::string display() const override {
        std::ostringstream o;
        o << "[PREPARED] " << getId() << "  " << getName() << "  " << getQuantity() << " " << getUnit()
          << "  @ Rs." << getPrice() << "/" << getUnit() << "  | cooked " << fmtTime(cookedTime)
          << "  | " << round2(getUrgencyScore()) << " h left (" << urgencyLevel() << ")";
        return o.str();
    }

    std::string serialize() const override {
        std::ostringstream o;
        o << "P|" << baseSerialize() << '|' << static_cast<long long>(cookedTime) << '|' << consumeWithinHours;
        return o.str();
    }

    json toJson() const override {
        json j = FoodItem::toJson();
        j["cookedTime"] = fmtTime(cookedTime);
        j["consumeWithinHours"] = consumeWithinHours;
        return j;
    }
};

// =====================================================================
//  Order — an NGO's purchase request and its lifecycle
//  PENDING -> CONFIRMED -> PICKED_UP -> DISTRIBUTED   (or REJECTED)
// =====================================================================
class Order {
private:
    std::string orderId, foodItemId, foodName, foodType, ngoId, ngoName, unit;
    std::string status = "PENDING";
    std::string reason;
    double quantity = 0, amount = 0;
    time_t timestamp = 0, updatedAt = 0;

public:
    Order() = default;
    Order(const std::string& orderId, const std::string& foodItemId, const std::string& foodName,
          const std::string& foodType, const std::string& ngoId, const std::string& ngoName,
          double quantity, const std::string& unit, double amount)
        : orderId(orderId), foodItemId(foodItemId), foodName(foodName), foodType(foodType),
          ngoId(ngoId), ngoName(ngoName), unit(unit), quantity(quantity), amount(amount),
          timestamp(nowTs()), updatedAt(nowTs()) {
        if (quantity <= 0) throw std::invalid_argument("Order quantity must be greater than zero");
    }

    void confirm() { status = "CONFIRMED"; reason = ""; updatedAt = nowTs(); }
    void reject(const std::string& why) { status = "REJECTED"; reason = clean(why); amount = 0; updatedAt = nowTs(); }
    void cancel() { status = "CANCELLED"; updatedAt = nowTs(); }

    // Allowed transitions after confirmation
    void advanceTo(const std::string& next) {
        if (next == "PICKED_UP" && status == "CONFIRMED") status = next;
        else if (next == "DISTRIBUTED" && status == "PICKED_UP") status = next;
        else throw std::invalid_argument("Cannot move order from " + status + " to " + next);
        updatedAt = nowTs();
    }

    bool countsAsSaved() const { return status == "CONFIRMED" || status == "PICKED_UP" || status == "DISTRIBUTED"; }

    // getters
    const std::string& getOrderId() const { return orderId; }
    const std::string& getFoodItemId() const { return foodItemId; }
    const std::string& getFoodName() const { return foodName; }
    const std::string& getNgoId() const { return ngoId; }
    const std::string& getStatus() const { return status; }
    double getQuantity() const { return quantity; }
    double getAmount() const { return amount; }
    time_t getTimestamp() const { return timestamp; }
    void setAmount(double a) { if (a < 0) throw std::invalid_argument("Amount cannot be negative"); amount = a; }

    json toJson() const {
        return json{
            {"orderId", orderId}, {"foodItemId", foodItemId}, {"foodName", foodName}, {"foodType", foodType},
            {"ngoId", ngoId}, {"ngoName", ngoName}, {"quantity", round2(quantity)}, {"unit", unit},
            {"amount", round2(amount)}, {"status", status}, {"reason", reason},
            {"timestamp", fmtTime(timestamp)}, {"updatedAt", fmtTime(updatedAt)},
            {"date", fmtDate(timestamp)}
        };
    }

    std::string serialize() const {
        std::ostringstream o;
        o << orderId << '|' << foodItemId << '|' << clean(foodName) << '|' << foodType << '|' << ngoId << '|'
          << clean(ngoName) << '|' << quantity << '|' << unit << '|' << amount << '|' << status << '|'
          << clean(reason) << '|' << static_cast<long long>(timestamp) << '|' << static_cast<long long>(updatedAt);
        return o.str();
    }

    static Order deserialize(const std::string& line) {
        auto p = split(line);
        if (p.size() < 13) throw std::runtime_error("Corrupt order record: " + line);
        Order o(p[0], p[1], p[2], p[3], p[4], p[5], std::stod(p[6]), p[7], std::stod(p[8]));
        o.status = p[9];
        o.reason = p[10];
        o.timestamp = static_cast<time_t>(std::stoll(p[11]));
        o.updatedAt = static_cast<time_t>(std::stoll(p[12]));
        return o;
    }
};

// =====================================================================
//  NGO
// =====================================================================
class NGO {
private:
    std::string ngoId, name, contactNumber, address;
    double capacityPerDay = 0;   // kg the NGO can collect & distribute per day

public:
    NGO() = default;
    NGO(const std::string& id, const std::string& name, const std::string& contact,
        const std::string& address, double capacityPerDay)
        : ngoId(id) {
        if (trim(name).empty()) throw std::invalid_argument("NGO name cannot be empty");
        std::string digits;
        for (char c : contact) if (isdigit(static_cast<unsigned char>(c))) digits += c;
        if (digits.size() != 10) throw std::invalid_argument("Contact number must have 10 digits");
        if (capacityPerDay <= 0) throw std::invalid_argument("Daily capacity must be greater than zero");
        this->name = clean(name);
        this->contactNumber = digits;
        this->address = clean(address);
        this->capacityPerDay = capacityPerDay;
    }

    // Creates a purchase request for an item (validated, not yet confirmed)
    Order placeOrder(const std::string& orderId, const FoodItem& item, double qty) const {
        if (qty <= 0) throw std::invalid_argument("Requested quantity must be greater than zero");
        return Order(orderId, item.getId(), item.getName(), item.getType(), ngoId, name,
                     qty, item.getUnit(), qty * item.getPrice());
    }

    std::vector<Order> viewHistory(const TransactionLog& log) const;   // defined in managers.h

    const std::string& getId() const { return ngoId; }
    const std::string& getName() const { return name; }
    const std::string& getContact() const { return contactNumber; }
    const std::string& getAddress() const { return address; }
    double getCapacity() const { return capacityPerDay; }

    json toJson() const {
        return json{{"ngoId", ngoId}, {"name", name}, {"contactNumber", contactNumber},
                    {"address", address}, {"capacityPerDay", capacityPerDay}};
    }
    std::string serialize() const {
        std::ostringstream o;
        o << ngoId << '|' << name << '|' << contactNumber << '|' << address << '|' << capacityPerDay;
        return o.str();
    }
    static NGO deserialize(const std::string& line) {
        auto p = split(line);
        if (p.size() < 5) throw std::runtime_error("Corrupt NGO record: " + line);
        return NGO(p[0], p[1], p[2], p[3], std::stod(p[4]));
    }
};

// =====================================================================
//  Canteen
// =====================================================================
class Canteen {
private:
    std::string canteenId, name, location;

public:
    Canteen(const std::string& id, const std::string& name, const std::string& location)
        : canteenId(id), name(name), location(location) {}

    void addFoodItem(InventoryManager& inv, FoodItem* item);            // defined in managers.h
    bool removeFoodItem(InventoryManager& inv, const std::string& id);   // defined in managers.h

    const std::string& getId() const { return canteenId; }
    json toJson() const { return json{{"canteenId", canteenId}, {"name", name}, {"location", location}}; }
};

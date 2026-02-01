
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <map>
#include <memory>
#include <queue>
#include <vector>

// FIX underflow error for volume
using namespace std;

enum OrderType { marketOrder, limitOrder };
enum Side { Buy, Ask };

using ID = u_int64_t;
using Volume = int;
using Price = double;

istream &operator>>(istream &is, OrderType &type) {
  int a;
  is >> a;
  type = static_cast<OrderType>(a);
  return is;
}

struct Order {
  Price price;
  ID id;
  Volume volume;
  Price stop;
  Side side;
  OrderType type;
  bool isCancelled;

  Order(Price p = 0.0, ID i = 0, Volume v = 0, Price s = 0.0, Side si = Buy,
        OrderType t = marketOrder, bool cancel = false)
      :

        price(p), id(i), volume(v), stop(s), side(si), type(t),
        isCancelled(cancel) {}
  void reset() {
    price = 0;
    id = 0;
    volume = 0;
    stop = 0;
    side = Buy;
    type = marketOrder;
    isCancelled = false;
  }

  ~Order() { return; }
};

struct PriceLevel {
  queue<shared_ptr<Order>> orders;
  unsigned int totalVolume;
};

class OrderBook {
private:
  map<ID, shared_ptr<Order>> orders_map;
  map<double, unique_ptr<PriceLevel>> Buy_Side;
  map<double, unique_ptr<PriceLevel>> Ask_Side;
  atomic<ID> next_id{1};
  vector<shared_ptr<Order>> available;

  Order default_order();

  double find_highest_priority(shared_ptr<Order> order) {
    auto &side = (order->side == Buy) ? Ask_Side : Buy_Side;
    if (side.empty())
      return 0.0;

    if (order->side == Buy) {
      return side.begin()->first;
    } else {
      return side.rbegin()->first;
    }
  }

  void addOrdertoBook(shared_ptr<Order> order) {

    orders_map[order->id] = order;
    auto &side = (order->side == Buy) ? Buy_Side : Ask_Side;
    auto it = side.find(orders_map[order->id]->price);
    if (it != side.end()) {
      it->second->orders.push(order);
    } else {
      auto level = std::make_unique<PriceLevel>();
      level->totalVolume = order->volume;
      level->orders = queue<shared_ptr<Order>>();
      level->orders.push(order);
      side[order->price] = std::move(level);
    }
  }
  void fillOrder(shared_ptr<Order> order) {
    auto &side = (order->side == Buy) ? Ask_Side : Buy_Side;
    while (!side.empty() && order->volume > 0) {
      double price = find_highest_priority(order);

      if (order->price < price && order->side == Buy &&
          order->type == limitOrder) {
        return;
      };
      if (order->price > price && order->side == Ask &&
          order->type == limitOrder) {
        return;
      };

      ID order_id = side[price]->orders.front()->id;

      if (orders_map[order_id]->volume != 0 &&
          orders_map[order_id]->isCancelled != true) {

        if (orders_map[order_id]->volume > order->volume) {
          orders_map[order_id]->volume -= order->volume;
          side[price]->totalVolume -= order->volume;
          order->volume = 0;
        } else {
          order->volume -= orders_map[order_id]->volume;
          side[price]->totalVolume -= orders_map[order_id]->volume;
          orders_map[order_id]->volume = 0;
          orders_map[order->id]->reset();
          available.push_back(orders_map[order->id]);
          side[price].reset();
          orders_map.erase(order->id);
          side[price]->orders.pop();
        }

        if (side[price]->orders.empty()) {
          side.erase(price);
        }
        if (order->volume == 0) {
          return;
        }
      } else {
        side[order->price]->orders.pop();
      }
    }
  }

  shared_ptr<Order> grabOrder() {
    auto order = available.back();
    available.pop_back();
    return order;
  }

public:
  OrderBook() {
    available.reserve(10000);
    for (auto i = 1; i < 10000; ++i) {
      available.push_back(make_shared<Order>());
    }
  };
  ID addOrder(Price price, Volume volume, Price stop, Side side, OrderType type,
              bool isCancelled) {
    ID id = next_id.fetch_add(1);
    shared_ptr<Order> order = grabOrder();

    order->price = price;
    order->volume = volume;
    order->side = side;
    order->stop = stop;
    order->id = id;
    fillOrder(order);
    if (order->volume > 0) {
      addOrdertoBook(order);
      return order->id;
    }
    return 0;
  }

  void killOrder(ID id) {
    auto it = orders_map.find(id);
    if (it == orders_map.end())
      return;

    auto &order = it->second;
    order->isCancelled = true;
    order->volume = 0;

    double price = orders_map[id]->price;
    auto &side = (order->side == Buy) ? Buy_Side : Ask_Side;
    auto price_level = side.find(price);
    if (price_level != side.end()) {
      auto &level = price_level->second;
      if (!level->orders.empty() && id == level->orders.front()->id) {
        level->orders.pop();
      }
      if (level->orders.empty()) {
        side.erase(price_level);
      }
    }
  }

  void editOrder(ID id) {
    if (auto search = orders_map.find(id); search != orders_map.end()) {
      Price newPrice;
      Price newStop;
      Volume newVolume;
      OrderType newType;

      cout << "\nEnter new price. If no change type 0.0: ";
      cin >> newPrice;
      cout << "\n Enter new stop. If no change type 0.0: ";
      cin >> newStop;
      cout << "\n Enter new volume. If no change type 0: ";
      cin >> newVolume;
      cout << "\n Enter a different orderType. If no change type your original "
              "order type: ";
      cin >> newType;

      if (newPrice != search->second->price && newPrice > 0.0) {
        shared_ptr<Order> old = orders_map[id];
        auto &side = (old->side == Buy) ? Buy_Side : Ask_Side;
        side[old->price]->totalVolume -= old->volume;
        old->price = newPrice;
        old->stop = newStop;
        old->volume = newVolume;
        old->type = newType;
        shared_ptr<Order> newOrder = old;
        addOrdertoBook(newOrder);
        old->isCancelled = true;
      }

      if (newStop != search->second->stop && newStop > 0.0) {
        search->second->stop = newStop;
      };
      if (newVolume != search->second->volume && newVolume > 0) {
        search->second->volume = newVolume;
      };
      if (newType != search->second->type) {
        search->second->type = newType;
      };

    } else {
      cout << id << " not found.";
    }
  }

  void cleanup() {
    auto it = Buy_Side.begin();
    while (it != Buy_Side.end()) {
      if (it->second->totalVolume == 0) {
        it = Buy_Side.erase(it);
      } else {
        ++it;
      }
    }

    it = Ask_Side.begin();
    while (it != Ask_Side.end()) {
      if (it->second->totalVolume == 0) {
        it = Ask_Side.erase(it);
      } else {
        ++it;
      }
    }
  }

  void printBook() {
    cout << "=== OrderBook === \n";
    cout << ("Asks: \n");
    for (auto const &[price, level] : Ask_Side) {

      cout << "Price: " << price << " Volume: " << level->totalVolume << "\n";
    }
    cout << "Buys: \n";
    for (auto const &[price, level] : Buy_Side) {
      cout << "Price: " << price << " Volume: " << level->totalVolume << "\n";
    }
  }
};

int main() {
  OrderBook book;
  ID id1 = book.addOrder(100.00, 500, 75.00, Buy, marketOrder, false);
  book.killOrder(id1);
  ID id2 = book.addOrder(90.00, 250, 82.00, Ask, limitOrder, false);
  book.editOrder(id2);
  book.cleanup();
  book.printBook();
}

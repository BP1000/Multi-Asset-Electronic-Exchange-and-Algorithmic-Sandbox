#include <algorithm>
#include <cstdlib>
#include <limits>
#include <map>
#include <queue>
#include <stdio.h>
#include <string>
#include <vector>

using namespace std;

enum OrderType { marketOrder, limitOrder };
enum Side { Buy, Ask };

struct Order {
  double price;
  u_int64_t id;
  int volume;
  double stop;
  Side side;
  OrderType type;
};

struct PriceLevel {
  queue<Order *> orders;
  unsigned int totalVolume;
};

class OrderBook {
private:
  map<u_int64_t, Order *> orders_map;
  map<double, PriceLevel *> Buy_Side;
  map<double, PriceLevel *> Ask_Side;

  double find_highest_priority(Order *order) {
    auto &side = (order->side == Buy) ? Ask_Side : Buy_Side;
    if (side.empty())
      return 0.0;

    if (order->side == Buy) {
      // Buyer wants the LOWEST Ask price
      return side.begin()->first;
    } else {
      // Seller wants the HIGHEST Buy price
      // In a default map, the highest key is at the end
      return side.rbegin()->first;
    }
  }

  void addOrdertoBook(Order *order) {
    auto &side = (order->side == Buy) ? Buy_Side : Ask_Side;
    if (side.find(order->price) == side.end()) {
      side[order->price] = new PriceLevel();
    }
    side[order->price]->orders.push(order);
    side[order->price]->totalVolume += order->volume;
    orders_map[order->id] = order;
  }

public:
  void fillLimitOrder(Order *order) {
    auto &side = (order->side == Buy) ? Ask_Side : Buy_Side;
    while (!side.empty() && order->volume > 0) {
      double price = find_highest_priority(order);

      if (order->price < price && order->side == Buy) {
        return;
      };
      if (order->price > price && order->side == Ask) {
        return;
      };

      PriceLevel *level = side[price];

      if (level->orders.front()->volume > order->volume) {
        level->orders.front()->volume -= order->volume;
        level->totalVolume -= order->volume;
        order->volume = 0;
      } else {
        order->volume -= level->orders.front()->volume;
        level->totalVolume -= level->orders.front()->volume;
        level->orders.front()->volume = 0;
        orders_map.erase(level->orders.front()->id);
        level->orders.pop();
      }

      if (level->orders.empty()) {
        side.erase(price);
      }
      if (order->volume == 0) {
        return;
      };
    }
  }

  OrderBook() {};
  void addOrder(Order *order) {
    fillLimitOrder(order);
    if (order->volume > 0) {
      addOrdertoBook(order);
    };
  }
  void printBook() {
    printf("=== OrderBook === \n");
    printf("Asks: \n");
    for (auto const &[price, level] : Ask_Side) {
      printf("Price: %.2f, Volume: %u\n", price, level->totalVolume);
    }
    printf("Buys: \n");
    for (auto const &[price, level] : Buy_Side) {
      printf("Price: %.2f, Volume: %u\n", price, level->totalVolume);
    }
  }
};

int main() {
  OrderBook book;
  Order *order = new Order();
  order->id = 12439023849123;
  order->price = 44.49;
  order->volume = 50;
  order->side = Buy;
  order->type = limitOrder;
  order->stop = 40;
  book.addOrder(order);
  book.printBook();
  Order *order2 = new Order();
  order2->id = 12341234213421342;
  order2->price = 44.49;
  order2->volume = 50;
  order2->side = Ask;
  order2->type = limitOrder;
  order2->stop = 40;
  book.addOrder(order2);
  Order *order3 = new Order();
  order3->id = 12341239048123094;
  order3->price = 50.00;
  order3->volume = 100;
  order3->side = Buy;
  order3->type = limitOrder;
  order3->stop = 50;
  book.addOrder(order3);

  book.printBook();
}

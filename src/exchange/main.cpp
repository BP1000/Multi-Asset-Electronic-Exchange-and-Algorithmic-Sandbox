#include <algorithm>
#include <cstdlib>
#include <map>
#include <queue>
#include <stdio.h>
#include <string>
#include <vector>

using namespace std;

enum OrderType { marketOrder, limitOrder };
enum Side { Buy, Sell };

class Order {
public:
  OrderType type;
  Side side;
  string symbol;
  double price;
  unsigned int volume;
  long orderId = rand() % 10000000000000000;
  double limit = 0.0;
  bool isFilled = false;

  Order(double price = 0.0, unsigned int volume = 0,
        OrderType type = marketOrder, Side side = Buy, string symbol = "",
        double limit = 0.0) {
    this->price = price;
    this->volume = volume;
    this->type = type;
    this->side = side;
    this->symbol = symbol;
    this->limit = limit;
  };

  void toString() {
    printf("Symbol: %s\n", symbol.c_str());
    printf("Price: %f\n", price);
    printf("Volume: %d\n", volume);
    printf("Type: %d\n", type);
    printf("Side: %d\n", side);
    printf("Limit: %f\n", limit);
  };
  void update_price(double newPrice) { price = newPrice; }
  void update_volume(unsigned int volume) { volume = volume; }

  // Create a destructor of some sorts to deallocate memory
};

class OrderBook {
private:
  map<double, queue<Order>> Buy_Side, Ask_Side;

public:
  double find_highest_priority(enum Side type) {
    if (type == Buy) {
      double max = 0.0;
      for (auto value : Buy_Side) {
        if (value.first > max) {
          max = value.first;
        }
      }
      return max;
    } else {
      double min = 10000.0;
      for (auto value : Ask_Side) {
        if (value.first < min) {
          min = value.first;
        }
      }
      return min;
    }
  }
  void add_limitOrder(Order order) {
    if (order.side == Buy && order.type == limitOrder) {
      for (auto price : Buy_Side) {
        if (price.first == order.price) {
          Buy_Side[price.first].push(order);
          return;
        }
      }
      // if there isn't any orders on the Buy Side at that price create a new
      // queue and add that order at the first offical queue;
      queue<Order> order_at_price;
      order_at_price.push(order);
      Buy_Side.insert({order.price, order_at_price});
    } else if (order.side == Sell && order.type == limitOrder) {
      for (auto price : Buy_Side) {
        if (price.first == order.price) {
          Ask_Side[price.first].push(order);
          return;
        }
      }
      queue<Order> order_at_price;
      order_at_price.push(order);
      Ask_Side.insert({order.price, order_at_price});
    }
  }
};

int main() { return 0; };

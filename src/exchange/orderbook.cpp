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
public:
  map<double, queue<Order>> Buy_Side, Ask_Side;
  OrderBook() {}

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
  // set add an order to the orderbook
  // if it's a market order set it to the find_highest_priority value on the
  // other side
  void addOrder(Order order) {
    if (order.type == limitOrder) {
      if (order.side == Buy) { // limit order on the buy side
        double bestPrice = this->find_highest_priority(Sell);

        if (bestPrice < order.price) {
          order.price = bestPrice;
        }
        if (Buy_Side.count(order.price) > 0) {
          Buy_Side[order.price].push(order);
        } else {
          queue<Order> order_at_price;
          order_at_price.push(order);
          Buy_Side.insert({order.price, order_at_price});
        }
      } else { // limit order on the sell side
        double bestPrice = this->find_highest_priority(Buy);
        if (bestPrice > order.price) {
          order.price = bestPrice;
        }
        if (Ask_Side.count(order.price) > 0) {
          Ask_Side[order.price].push(order);
        } else {
          queue<Order> order_at_price;
          order_at_price.push(order);
          Ask_Side.insert({order.price, order_at_price});
        }
      }
    } else {
      if (order.side ==
          Buy) { // market order on the buy side
                 // check if there are order(s) being placed for the same price
                 // as the lowest price on the sell side. If so, then add it to
                 // the back of the queue. If not, create a new entry into the
                 // hashmap
        order.price = this->find_highest_priority(Sell);
        if (Buy_Side.count(order.price) > 0) {
          Buy_Side[order.price].push(order);
        } else {
          queue<Order> order_at_price;
          order_at_price.push(order);
          Buy_Side.insert({order.price, order_at_price});
        }
      } else { // market order on the sell side
               // check if there are order(s) being placed for the same price as
               // the highest price on the buy side. If so, then add it to the
               // back of the queue. If not, create a new entry into the hashmap
        order.price = this->find_highest_priority(Buy);
        if (Ask_Side.count(order.price) > 0) {
          Ask_Side[order.price].push(order);
        } else {
          queue<Order> order_at_price;
          order_at_price.push(order);
          Ask_Side.insert({order.price, order_at_price});
        }
      }
    }
  }

  void toString() { queue<Order> new_Buy = Buy_Side; }

  // void fillOrder(Order order) {
  // if (order.type == limitOrder) {
  //  if (order.side == Buy) {
  //  ()
  // }
  //}
  //}
};

int main() {
  OrderBook book;
  Order *order = new Order();
  book.addOrder(*order);
  Order *order2 = new Order();
  order2->price = 44.49;
  order2->volume = 100;
  order2->side = Sell;
  order2->symbol = "AAPL";
  book.addOrder(*order2);
};

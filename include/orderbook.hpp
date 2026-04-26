#ifndef ORDERBOOK_H
#define ORDERBOOK_H

#include <array>
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <map>
#include <memory>
#include <queue>
#include <string>
#include <unordered_map>
#include <vector>
using Price = double;
using Volume = std::size_t;
using ID = u_int64_t;
using Control = float;

enum OrderType { marketOrder, limitOrder };
enum Side { Buy, Ask };

struct Order {
  std::string name_;
  Price price_;
  ID id_;
  Volume volume_;
  Price stop_;
  Side side_;
  OrderType type_;
  bool isCancelled_;

  Order(Price price_ = 0.0, ID id_ = 0, Volume volume_ = 0, Price stop_ = 0.0,
        Side side_ = Buy, OrderType type_ = marketOrder,
        bool isCancelled_ = false);
  void reset();
};

struct PriceLevel {
  double price_;
  std::queue<ID> orders_;
  unsigned int totalVolume_;
};

class OrderBook {
private:
  std::unordered_map<ID, std::shared_ptr<Order>> orders_map;
  static std::map<Price, std::shared_ptr<PriceLevel>, std::greater<Price>>
      Buy_Side;
  static std::map<Price, std::shared_ptr<PriceLevel>, std::greater<Price>>
      Ask_Side;
  std::atomic<ID> next_id = std::atomic<ID>(0);
  template <Side s> double find_highest_priority();

  void addOrdertoBook(std::shared_ptr<Order> order);
  Control controlofSector_;
  Price stockPrice_;

public:
  std::string name_;
  static const std::map<Price, std::shared_ptr<PriceLevel>, std::greater<Price>>
  getBuy() {
    return Buy_Side;
  }
  static const std::map<double, std::shared_ptr<PriceLevel>,
                        std::greater<Price>>
  getAsk() {
    return Ask_Side;
  }

  void fillOrder(std::shared_ptr<Order> order);
  Price getStockPrice();
  bool isEmpty();
  OrderBook(Price initialPrice, std::string name);
  OrderBook(const OrderBook &) = delete;
  void operator=(const OrderBook &) = delete;
  OrderBook(OrderBook &&) = delete;
  auto getControlOfSector();
  ID addOrder(Price price, Volume volume, Price stop, Side side,
              OrderType type);
  void killOrder(ID id);
  void editOrder(ID id);
  void cleanup();
  void printBook();
};

template <std::size_t length> class FlatBook {
private:
  std::array<std::shared_ptr<PriceLevel>, length> flatBuy_;
  std::array<std::shared_ptr<PriceLevel>, length> flatAsk_;
  template <Side s> Price getLow() {
    auto &side = (s == Buy) ? flatBuy_ : flatAsk_;
    Price minPrice = side[0];
    for (auto i{1}; i < length; ++i) {
      if (minPrice > side[i]) {
        minPrice = side[i];
      }
    }
    return minPrice;
  }

public:
  FlatBook() {
    for (int i = 0; i < length; i++) {
      flatBuy_[i] = OrderBook::getBuy().at(i);
      flatAsk_[i] = OrderBook::getAsk().at(i);
    }
  };
  FlatBook(const FlatBook &) = delete;
  void operator=(FlatBook &) = delete;

  template <std::array<std::shared_ptr<PriceLevel>, length> s>
  void getflatInfo() {
    for (int i = 0; i < length; ++i) {
      std::cout << "Price: " << s[i]->price << " Volume: " << s[i]->totalVolume_
                << "\n";
    }
  }

  template <Side s> void addLevel(std::shared_ptr<PriceLevel> level) {
    auto &side = (s == Buy) ? flatBuy_ : flatAsk_;
    if (side.getLow() < level->price_) {
      get<getLow<s>()>(s) = level;
    }
  }
};

#endif

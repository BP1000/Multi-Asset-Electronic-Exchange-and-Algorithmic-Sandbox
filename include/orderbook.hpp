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
using Price = double;
using Volume = std::size_t;
using ID = u_int64_t;
using Control = float;

enum OrderType { marketOrder, limitOrder };
enum Side { Buy, Ask };

template <std::size_t length> class FlatBook;

struct Order {
  std::string name_;
  Price price_;
  ID id_;
  Volume volume_;
  Price stop_;
  Side side_;
  OrderType type_;
  bool isCancelled_;
  Order() = default;
  Order(Price price_, ID id_ = 0, Volume volume_ = 0, Price stop_ = 0.0,
        Side side_ = Buy, OrderType type_ = marketOrder,
        bool isCancelled_ = false);
  void reset();
  void info();
};

struct PriceLevel {
  double price_;
  std::queue<ID> orders_;
  Volume totalVolume_ = 50;
  PriceLevel() = default;
  PriceLevel(double price, Volume totalVolume)
      : price_(price), totalVolume_(totalVolume) {};
};

template <std::size_t flatSize> class OrderBook {
private:
  std::unordered_map<ID, std::shared_ptr<Order>> orders_map;
  static inline std::map<Price, std::shared_ptr<PriceLevel>,
                         std::greater<Price>>
      Buy_Side{};
  static inline std::map<Price, std::shared_ptr<PriceLevel>,
                         std::greater<Price>>
      Ask_Side{};
  std::atomic<ID> next_id = std::atomic<ID>(0);
  template <Side s> double find_highest_priority();

  void addOrdertoBook(std::shared_ptr<Order> order);
  Control controlofSector_;
  Price stockPrice_;
  std::string name_;
  std::size_t flatLength_;

public:
  OrderBook(Price initialPrice, std::string name);
  OrderBook(const OrderBook &) = delete;
  void operator=(const OrderBook &) = delete;
  OrderBook(OrderBook &&) = delete;
  FlatBook<flatSize> flatBook_;
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
  static inline std::array<std::shared_ptr<PriceLevel>, length> flatBuy_{};
  static inline std::array<std::shared_ptr<PriceLevel>, length> flatAsk_{};

  template <Side s> int getWorst() {
    if (s == Buy) {
      int minIndex = 0;
      for (auto i{1}; i < length; ++i) {
        if (flatBuy_[i] < flatBuy_[minIndex]) {
          minIndex = i;
        }
      }
      return minIndex;
    } else {
      int maxIndex = 0;
      for (auto i{1}; i < length; ++i) {
        if (flatAsk_[maxIndex] > flatAsk_[i]) {
          maxIndex = i;
        }
      }
      return maxIndex;
    }
  }

  int checkFull(Side s) {
    auto &side = (s == Buy) ? flatBuy_ : flatAsk_;
    for (auto i{0}; i < length; ++i) {
      if (side[i] == nullptr) {
        return i;
      }
    }
    return -1;
  }

public:
  FlatBook() {};
  FlatBook(const FlatBook &) = delete;
  void operator=(FlatBook &) = delete;

  void getflatInfo() {
    std::cout << "Buy Info: \n";
    for (int i = 0; i < length; ++i) {
      std::cout << "Price: " << flatBuy_[i]->price_
                << " Volume: " << flatBuy_[i]->totalVolume_ << "\n";
    }
    std::cout << "Ask Info: \n";
    for (int i = 0; i < length; ++i) {
      std::cout << "Price " << flatAsk_[i]->price_
                << " Volume: " << flatAsk_[i]->totalVolume_ << "\n";
    }
  }

  template <Side s>
  void addLevel(const std::shared_ptr<PriceLevel> &level, Price price) {
    if (s == Buy) {
      int index = checkFull(Buy);
      if (index > -1) {
        flatBuy_[index] = level;
        return;
      }
      index = getWorst<Side::Buy>();
      if (flatBuy_[index]->price_ < price || !flatBuy_[index]) {
        flatBuy_[index] = level;
      }
    } else {
      int index = checkFull(Ask);
      if (index > -1) {
        flatAsk_[index] = level;
        return;
      }
      index = getWorst<Side::Ask>();
      if (flatAsk_[index]->price_ > price || !flatAsk_[index]) {
        flatAsk_[index] = level;
      }
    }
  }
};

#endif

#ifndef ORDERBOOK_H
#define ORDERBOOK_H

#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
using Price = int64_t;
using Volume = u_int32_t;
using ID = u_int64_t;
using Control = float;

enum OrderType { marketOrder, limitOrder, last1 };
enum Side { Buy, Ask, last2 };

template <std::size_t length> struct FlatBook;

struct Order {
  Price price_;
  Price stop_;
  ID id_;
  Volume volume_;
  Order *next_;
  Order *prev_;
  Side side_;
  OrderType type_;
  uint8_t book_id_;
  Order() = default;
  Order(Price price_, ID id_ = 0, Volume volume_ = 0, Price stop_ = 0.0,
        Side side_ = Buy, OrderType type_ = marketOrder);
  void reset();
  void info();
};

struct PriceLevel {
  Price price_;
  Volume totalVolume_;
  Order *head_;
  Order *tail_;
  PriceLevel() = default;
  PriceLevel(Price price, Volume totalVolume)
      : price_(price), totalVolume_(totalVolume) {};
  Order pop();
};

namespace OrderBookEngine {
template <std::size_t size> class MemoryPool {
  std::unique_ptr<Order[]> buffer_;
  std::unique_ptr<Order *[]> free_;
  std::size_t free_head{0};

public:
  MemoryPool();
  Order *acquire() noexcept;
  void release(Order *order) noexcept;
  ~MemoryPool() = default;
};

template <std::size_t length> struct FlatBook {
  PriceLevel *Buy_Levels_[length];
  PriceLevel *Ask_Levels_[length];

  FlatBook()
      : Buy_Levels_(new PriceLevel[length]{}),
        Ask_Levels_(new PriceLevel[length]{}) {};

  bool isBetter(Side side, Price p1, Price p2) {
    if (side == Buy) {
      if (p1 > p2) {
        return true;
      }
      return false;
    } else {
      if (p1 < p2) {
        return true;
      }
      return false;
    }
  }
  std::size_t getBestIndex(Side side) {
    auto &sidelevels = (side == Buy) ? Buy_Levels_ : Ask_Levels_;
    std::size_t idx = 0;

    if (sidelevels[idx] == nullptr) {
      return 0;
    }

    for (auto i{1}; i < length; ++i) {
      if (sidelevels[i] != nullptr) {
        if (sidelevels[i]->totalVolume_ > sidelevels[idx]->totalVolume_) {
          idx = i;
        }
      }
    }
    return idx;
  }

  void addLevel(Side side, PriceLevel *level) {
    auto &sidelevels = (side == Buy) ? Buy_Levels_ : Ask_Levels_;
    if (sidelevels[0] == nullptr || sidelevels[0]->totalVolume_ == 0) {
      sidelevels[0] = level;
      return;
    }
    std::size_t idx = 0;

    for (auto i{1}; i < length; ++i) {
      if (sidelevels[i] == nullptr) {
        sidelevels[i] = level;
        return;
      } else if (sidelevels[i]->price_ == level->price_) {
        return;
      }
      if (sidelevels[idx]->price_ > sidelevels[i]->price_ ||
          sidelevels[i]->totalVolume_ <= 0) {
        idx = i;
      }
    }
    if (level->price_ > sidelevels[idx]->price_) {
      sidelevels[idx] = level;
    }
  }

  void getLevels() {
    std::cerr << "=== Best Buy Bids === \n";
    for (auto i{0}; i < length; ++i) {
      if (Buy_Levels_[i] != nullptr && Buy_Levels_[i]->totalVolume_ > 0) {
        std::cerr << "Price: " << Buy_Levels_[i]->price_
                  << " Volume: " << Buy_Levels_[i]->totalVolume_ << "\n";
      }
    }
    std::cerr << "=== Best Ask Bids === \n" << std::endl;
    for (auto i{0}; i < length; ++i) {
      if (Ask_Levels_[i] != nullptr && Ask_Levels_[i]->totalVolume_ > 0) {
        std::cerr << "Price: " << Ask_Levels_[i]->price_
                  << " Volume: " << Ask_Levels_[i]->totalVolume_ << "\n";
      }
    }
  }
};

template <std::size_t poolSize, std::size_t flatSize> class OrderBook {
private:
  std::unordered_map<ID, Order *> orders_map;
  std::map<Price, PriceLevel *, std::greater<Price>> Buy_Side{};
  std::map<Price, PriceLevel *, std::greater<Price>> Ask_Side{};
  std::atomic<ID> next_id = std::atomic<ID>(0);
  template <Side s> double find_highest_priority();
  MemoryPool<poolSize> pool_;
  void addOrdertoBook(Order *order);
  Control controlofSector_;
  Price stockPrice_;
  std::string name_;
  std::size_t flatLength_;
  void runBook();

public:
  OrderBook(Price initialPrice, std::string name);
  OrderBook(const OrderBook &) = delete;
  void operator=(const OrderBook &) = delete;
  OrderBook(OrderBook &&) = delete;
  FlatBook<flatSize> flatBook_;
  std::map<Price, PriceLevel *, std::greater<Price>> &getBuy() {
    return Buy_Side;
  }
  std::map<Price, PriceLevel *, std::greater<Price>> &getAsk() {
    return Ask_Side;
  }
  void callRun() { runBook(); };
  std::unordered_map<ID, Order *> getOrdersMap() { return orders_map; }
  bool fillOrder(Order *order);
  Price getStockPrice() { return stockPrice_; };
  bool isEmpty();
  auto getControlOfSector();

  ID addOrder(Price price, Volume volume, Price stop, Side side,
              OrderType type);
  void killOrder(ID id);
  ID editOrder(ID id, Price price, Volume volume, Price stop, OrderType type);
  void getBookInfo();
};

}; // namespace OrderBookEngine
#endif

#ifndef ORDERBOOK_H
#define ORDERBOOK_H

#include <atomic>
#include <cstdlib>
#include <map>
#include <memory>
#include <queue>
#include <string>
#include <unordered_map>
using Price = double;
using Volume = int;
using ID = u_int64_t;
using Control = float;

enum OrderType { marketOrder, limitOrder };
enum Side { Buy, Ask };

struct Order {
  Price price;
  ID id;
  Volume volume;
  Price stop;
  Side side;
  OrderType type;
  bool isCancelled;

  Order(Price price_ = 0.0, ID id_ = 0, Volume volume_ = 0, Price stop_ = 0.0,
        Side side_ = Buy, OrderType type_ = marketOrder,
        bool isCancelled_ = false);
  void reset();
};

struct PriceLevel {
  std::queue<ID> orders;
  unsigned int totalVolume;
};

class OrderBook {
private:
  std::unordered_map<ID, std::shared_ptr<Order>> orders_map;
  std::map<double, std::unique_ptr<PriceLevel>> Buy_Side;
  std::map<double, std::unique_ptr<PriceLevel>> Ask_Side;
  std::atomic<ID> next_id = std::atomic<ID>(0);
  double find_highest_priority(std::shared_ptr<Order> order);
  void addOrdertoBook(std::shared_ptr<Order> order);
  void fillOrder(std::shared_ptr<Order> order);
  Control controlofSector;
  Price stockPrice;
  std::string name;

public:
  Price getStockPrice();
  OrderBook(Price stockPrice_, std::string name_);
  auto getControlOfSector();
  ID addOrder(Price price, Volume volume, Price stop, Side side,
              OrderType type);
  void killOrder(ID id);
  void editOrder(ID id);
  void cleanup();
  void printBook();
};

#endif

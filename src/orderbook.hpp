#ifndef PLAYER_H
#define PLAYER_H

#include <atomic>
#include <cstdlib>
#include <map>
#include <memory>
#include <queue>
#include <unordered_map>
#include <vector>
using Price = double;
using Volume = int;
using ID = u_int64_t;

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
  std::queue<std::shared_ptr<Order>> orders;
  unsigned int totalVolume;
};

class OrderBook {
private:
  std::unordered_map<ID, std::shared_ptr<Order>> orders_map;
  std::map<double, std::unique_ptr<PriceLevel>> Buy_Side;
  std::map<double, std::unique_ptr<PriceLevel>> Ask_Side;
  std::atomic<ID> next_id{1};
  std::vector<std::shared_ptr<Order>> available;
  double find_highest_priority(std::shared_ptr<Order> order);
  void addOrdertoBook(std::shared_ptr<Order> order);
  void fillOrder(std::shared_ptr<Order> order);
  std::shared_ptr<Order> grabOrder();

public:
  OrderBook(Volume totalShares);
  ID addOrder(Price price, Volume volume, Price stop, Side side,
              OrderType type);
  void killOrder(ID id);
  void editOrder(ID id);
  void cleanup();
  void printBook();
};

#endif

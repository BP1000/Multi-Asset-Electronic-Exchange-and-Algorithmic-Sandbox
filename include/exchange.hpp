#ifndef EXCHANGE
#define EXCHANGE

#include "orderbook.hpp"
#include <atomic>
#include <string>
#include <unordered_map>
#include <vector>

using TraderID = u_int64_t;

struct Trader;

class Exchange {
private:
  std::atomic<TraderID> traderID = std::atomic<TraderID>(1000);
  std::unordered_map<ID, Trader> traders_map;
  std::atomic<TraderID> next_traderID{10000};
  // std::unordered_map<std::string, OrderBook> books;
  double highest_price;
  void createNewBook(std::string name, std::vector<Trader> marketMakers,
                     Price IPO, Volume initialVolume);
  void manageBooks();

public:
  std::unordered_map<std::string, OrderBook *> books;
  void placeOrder(std::string name);
  void destroyOrder(ID id);
  void modifyOrder(ID id);
  void getPctChange(std::string name);
  void destroyStock(std::string);
};

struct Trader {
  Exchange &exchange;
  std::string traderName;
  TraderID traderID;
  std::unordered_map<ID, Order> user_orders;
  std::unordered_map<std::string, Volume> owned;
  Volume totalVolumeOwned;
  Price totalMoney;
  Price totalProft;
  Price totalLoss;
  Volume volumeOnMarket;

  Trader(Exchange &exhange_, std::string traderName_, TraderID traderID_);

  void placeOrder(std::string name, Price price, Price stop, Volume volume,
                  Side side, OrderType type);
  void killOrder(ID id);
  void editOrder(ID id);
};

#endif

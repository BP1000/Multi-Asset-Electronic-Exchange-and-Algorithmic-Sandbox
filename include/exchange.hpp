#ifndef EXCHANGE
#define EXCHANGE

#include "orderbook.hpp"
#include <string>
#include <unordered_map>
#include <vector>

using TraderID = u_int64_t;

struct Trader {
  std::string traderName;
  TraderID traderID;
  std::unordered_map<ID, Order> user_orders;
  Volume totalVolumeOwned;
  Price totalMoney;
  Price totalProft;
  Price totalLoss;
  Volume volumeOnMarket;

  void placeOrder(Price price, Price stop, Volume volume, OrderType type);
  void killOrder(ID id);
};

class Exchange {
private:
  std::unordered_map<ID, Trader> traders_map;
  std::atomic<TraderID> next_traderID{10000};
  std::unordered_map<std::string, std::vector<std::string>> sectors;
  double highest_price;
  void createNewBook(std::string name, std::string sector,
                     Volume initialVolume);

public:
  void placeOrder(std::string name);
  void destroyOrder(ID id);
  void modifyOrder(ID id);
  void getPctChange(std::string name);
  void destroyStock(std::string);
};
#endif

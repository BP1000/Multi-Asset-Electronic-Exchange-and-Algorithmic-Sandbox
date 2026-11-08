#ifndef EXCHANGE
#define EXCHANGE

#include "../json.hpp"
#include "orderbook.hpp"
#include <atomic>
#include <cstddef>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <unordered_map>
#include <vector>

#define PORT 8080

using Trader_ID = u_int64_t;
using JSON = nlohmann::json;
using Capital = u_int32_t;

struct Trader {
private:
  int traderID_;
  std::unordered_map<ID, Order *> orders_;
  Capital totalCapital_;
  std::string name_;
  int clientSocket_;
  sockaddr_in serverAddress_;

public:
  Trader(Capital initialCapital, std::string userName);
  void addCapital(Capital capital);
  void placeOrder(std::string name, Price price, Volume volume, Price stop,
                  Side side, OrderType type);
  void insertOrder(Trader_ID, Order *);
  void killOrder(ID id);
  void modifyOrder(ID id);
  void getPositions();
  void clearPositions();
  void acceptMessage();
};

namespace Exchange {

struct TraderRegistry {
  TraderRegistry() = default;
  std::unordered_map<Trader_ID, Trader *> traders_map;
  std::atomic<Trader_ID> getTraderID = std::atomic<Trader_ID>(1000);
  JSON addTrader(Trader *trader);
  JSON removeTrader(Trader_ID);
};

class SPSC_Queue {
private:
  std::vector<JSON> buffer_;
  const std::size_t capacity_;
  alignas(64) std::atomic<std::size_t> head_;
  alignas(64) std::atomic<std::size_t> tail_;

public:
  SPSC_Queue(std::size_t size);
  SPSC_Queue(const SPSC_Queue &) = delete;
  SPSC_Queue &operator=(const SPSC_Queue &) = delete;
  bool push(const JSON order);
  bool pop(JSON &order);
};

template <std::size_t bookPoolSize, std::size_t bookflatSize>
class BookManager {
private:
  std::string ticker_;
  TraderRegistry &traders_;
  OrderBookEngine::OrderBook<bookPoolSize, bookflatSize> &book_;

public:
  BookManager(std::string ticker, Price price, TraderRegistry &traders);
  void acceptMsg(Trader_ID traderID, Price price, Volume volume, Price stop,
                 Side side, OrderType type);
  void updateTraderVolume(Trader_ID traderID, Price price, Volume volume);
};

template <std::size_t bookPoolSize, std::size_t bookflatSize> class Exchange {
  std::string exchangeName_;
  TraderRegistry traders_;
  std::unordered_map<std::string, BookManager<bookPoolSize, bookflatSize> *>
      books_;
  SPSC_Queue orders_queue_;
  int m_port;
  int m_serverFd;

  void createNewBook(std::string &name, Price initialPrice);
  void removeBook(std::string &name);
  std::string modifyOrder(JSON order);
  std::string killOrder(JSON order);

public:
  Exchange(std::string name_, int port, std::size_t queueSize);
  ~Exchange() = default;
  bool startAndRunSocket();
  void stop();
  void processMessages();
  void sendMessage(std::string);
  void acceptOrder(Trader_ID traderID, std::string name, Price price,
                   Volume volume, Price stop, Side side, OrderType type);
};
} // namespace Exchange
#endif

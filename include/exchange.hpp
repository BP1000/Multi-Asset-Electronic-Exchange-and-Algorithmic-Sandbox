#ifndef EXCHANGE
#define EXCHANGE

#include "../json.hpp"
#include "orderbook.hpp"
#include <atomic>
#include <cstddef>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
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

public:
  Trader(Capital initialCapital, std::string userName);
  ID placeOrder(JSON order);
  void killOrder(ID id);
  void modifyOrder(ID id);
  void getPositions();
  void clearPositions();
};

namespace Exchange {
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

template <std::size_t bookPoolSize, std::size_t bookflatSize> class Exchange {
  std::string exchangeName_;
  std::unordered_map<std::string,
                     OrderBookEngine::OrderBook<bookPoolSize, bookflatSize> *>
      books_;
  std::unordered_map<int, Trader *> traders_;
  SPSC_Queue orders_queue_;
  int m_port;
  int m_serverFd;

  void createNewBook(std::string &name, Price initialPrice);
  void removeBook(std::string &name);
  std::string modifyOrder(JSON order);
  std::string killOrder(JSON order);

public:
  Exchange(std::string name_, int port);
  // ~Exchange();
  bool startAndRunSocket();
  void stop();
  void sendMessage(std::string);
  void acceptMessage(JSON json);
};
} // namespace Exchange
#endif

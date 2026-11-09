#ifndef EXCHANGE
#define EXCHANGE

#include "../json.hpp"
#include "orderbook.hpp"
#include <arpa/inet.h>
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
  Trader() = delete;
  Trader(Capital initialCapital, std::string userName) {
    totalCapital_ = initialCapital;
    name_ = userName;
    clientSocket_ = socket(AF_INET, SOCK_STREAM, 0);
    if (clientSocket_ < 0) {
      perror("Failed to create socket");
      return;
    }
    std::cerr << "Client Socket fd: " << clientSocket_ << "\n";

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(PORT);
    inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
    serverAddress_ = address;

    if (connect(clientSocket_, (const sockaddr *)&address, sizeof(address)) <
        0) {
      perror("ERROR! Failed to establish connection with server");
      return;
    }
    try {
      JSON initialMsg;
      initialMsg["name"] = name_;
      initialMsg["capital"] = totalCapital_;
      const std::string wire = initialMsg.dump() + '\n';
      if (send(clientSocket_, wire.data(), wire.size(), 0) < 0) {
        perror("ERROR! Failed to send data to server");
        return;
      }
    } catch (const JSON::exception &e) {
      std::cerr << "bad message: " << e.what() << "\n";
    }
  }
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
  std::mutex mtx_;
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
  std::mutex mutex_;
  void createNewBook(std::string &name, Price initialPrice);
  void removeBook(std::string &name);
  std::string modifyOrder(JSON order);
  std::string killOrder(JSON order);
  void handleClient(int fd);

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

#include "../../include/exchange.hpp"
#include <sys/socket.h>

Exchange::SPSC_Queue::SPSC_Queue(std::size_t size)
    : buffer_(size + 1), capacity_(size + 1), head_(0), tail_(0) {};

bool Exchange::SPSC_Queue::push(const JSON order) {
  const std::size_t cur_tail = tail_.load(std::memory_order_relaxed);
  const std::size_t next_tail = (cur_tail + 1) % capacity_;

  if (next_tail == head_.load(std::memory_order_acquire)) {
    return false;
  }
  buffer_[cur_tail] = order;
  tail_.store(next_tail, std::memory_order_release);
  return true;
}

bool Exchange::SPSC_Queue::pop(JSON &order) {
  const std::size_t cur_head = head_.load(std::memory_order_relaxed);
  if (cur_head == tail_.load(std::memory_order_acquire)) {
    return false;
  }
  auto result = buffer_[cur_head];
  const std::size_t next_head = (cur_head + 1) % capacity_;
  head_.store(next_head, std::memory_order_relaxed);
  return true;
}

template <std::size_t bookPoolSize, std::size_t bookflatSize>
Exchange::Exchange<bookPoolSize, bookflatSize>::Exchange(std::string name_,
                                                         int port)
    : m_port(port), m_serverFd(-1), exchangeName_(std::move(name_)), {
  bool val = startAndRunSocket();
};
template <std::size_t bookPoolSize, std::size_t bookflatSize>
void Exchange::Exchange<bookPoolSize, bookflatSize>::createNewBook(
    std::string &name, Price initialPrice) {
  OrderBookEngine::OrderBook<bookPoolSize, bookflatSize> book(name,
                                                              initialPrice);
  books_.insert({name, book});
}

template <std::size_t bookPoolSize, std::size_t bookflatSize>
bool Exchange::Exchange<bookPoolSize, bookflatSize>::startAndRunSocket() {
  int listenSocket = socket(AF_INET, SOCK_STREAM, 0);

  if (listenSocket < 0) {
    std::cerr << "Error: Failed to create listening socket\n";
    return false;
  }

  // assign family and address to the socket
  sockaddr_in address;
  address.sin_family = AF_INET;
  address.sin_port = htons(8080);
  address.sin_addr.s_addr = htonl(INADDR_ANY);

  // assigns the address and the port to the socket
  if (bind(listenSocket, (const struct sockaddr *)&address, sizeof(address)) <
      0) {
    std::cerr << "Bind failed\n";
    std::cerr << errno;
    return false;
  }

  // allows the socket to accept new connections from clients
  if (listen(listenSocket, SOMAXCONN) < 0) {
    std::cerr << "Failed to establish listening connection!\n";
    std::cerr << errno;
    return false;
  }

  std::cerr << "Listening on port 8080.....\n";

  while (true) {
    socklen_t size = sizeof(address);
    auto fd = accept(listenSocket, (struct sockaddr *)&address, &size);
    if (fd < 0) {
      std::cout << "Failed to establish connected with client or client has "
                   "clossed connection.\n";
      close(fd);
      continue;
    } else {
      std::cerr << "Done!";
      traders_.insert({fd, nullptr});
    }

    JSON *buffer;
    if (recv(listenSocket, &buffer, buffer->size(), MSG_PEEK)) {
      try {
        JSON &msg = buffer->front();
        orders_queue_.push(msg);
      } catch (const JSON::parse_error &) {
        std::cout << "ERROR NOT JSON!";
      }
    }
  }
}

template <std::size_t bookPoolSize, std::size_t bookflatSize>
void Exchange::Exchange<bookPoolSize, bookflatSize>::processMessages() {
  JSON message;
  while (true) {
    try {
      orders_queue_.pop(message);
      if (message.value("action", "") == "place order") {
        acceptOrder(message.at("trader id").get<Trader_ID>(),
                    message.at("name").get<std::string>(),
                    message.at("price").get<Price>(),
                    message.at("volume").get<Volume>(),
                    message.at("stop").get<Price>(),
                    message.at("side").get<Side>(),
                    message.at("type").get<OrderType>());
      }
    } catch (Exception &e) {
      continue;
    }
  }
}

template <std::size_t bookPoolSize, std::size_t bookflatSize>
void Exchange::Exchange<bookPoolSize, bookflatSize>::acceptOrder(
    Trader_ID traderID, std::string name, Price price, Volume volume,
    Price stop, Side side, OrderType type) {
  auto &book = books_.find(name)->second;
  auto result = book.addOrder(price, volume, stop, side, type);
  auto trader = traders_.find(traderID)->second;
  if (result.first > 0) {
    if (side == Ask) {
      trader->addCapital(result.second);
    }
  } else {
    auto book = books_.find(name)->second;
    auto order = book->orders_map.find(result.second);
    trader->insertOrder({result.first, order});
  }
}

int main() {
  Exchange::Exchange<1000, 10> exchange("BPSE", 8080);
  return 0;
}

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
Exchange::BookManager<bookPoolSize, bookflatSize>::BookManager(
    std::string ticker, Price price, TraderRegistry &traders)
    : ticker_(ticker), traders_(traders),
      book_(price, ticker,
            [this](Trader_ID traderID, Price price, Volume volume, Side side) {
              auto trader = traders_.traders_map.find(traderID)->second;
              if (side == Buy) {
                trader->addCapital(price * volume);
              } else {
                trader->removeCapital(price * volume);
              }
            }

      ){};

template <std::size_t bookPoolSize, std::size_t bookflatSize>
Exchange::Exchange<bookPoolSize, bookflatSize>::Exchange(std::string name_,
                                                         int port,
                                                         std::size_t queueSize)
    : m_port(port), m_serverFd(-1), exchangeName_(std::move(name_)),
      orders_queue_(queueSize){};

template <std::size_t bookPoolSize, std::size_t bookflatSize>
void Exchange::Exchange<bookPoolSize, bookflatSize>::createNewBook(
    std::string name, Price initialPrice) {

  BookManager<bookPoolSize, bookflatSize> book(name, initialPrice, traders_);
  books_.insert({name, &book});
}

template <std::size_t bookPoolSize, std::size_t bookflatSize>
bool Exchange::Exchange<bookPoolSize, bookflatSize>::startAndRunSocket() {
  int listenSocket = socket(AF_INET, SOCK_STREAM, 0);
  if (listenSocket < 0) {
    perror("socket");
    return false;
  }

  int opt = 1;
  setsockopt(listenSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(m_port);
  address.sin_addr.s_addr = htonl(INADDR_ANY);

  if (bind(listenSocket, (const sockaddr *)&address, sizeof(address)) < 0) {
    perror("bind");
    return false;
  }
  if (listen(listenSocket, SOMAXCONN) < 0) {
    perror("listen");
    return false;
  }
  std::cerr << "Listening on port " << m_port << "\n";

  while (true) {
    int fd = accept(listenSocket, nullptr, nullptr);
    if (fd < 0) {
      perror("accept");
      continue;
    }
    std::thread(&Exchange::handleClient, this, fd).detach();
  }
}

template <std::size_t P, std::size_t F>
void Exchange::Exchange<P, F>::handleClient(int fd) {
  std::string pending;
  char buf[1024];
  Trader_ID myId = 0;

  while (true) {
    ssize_t n = recv(fd, buf, sizeof(buf), 0);
    if (n <= 0)
      break; // disconnect or error
    pending.append(buf, n);

    size_t pos;
    while ((pos = pending.find('\n')) != std::string::npos) {
      std::string line = pending.substr(0, pos);
      pending.erase(0, pos + 1);
      try {
        JSON msg = JSON::parse(line);
        if (msg.contains("capital")) { // login message
          std::lock_guard<std::mutex> lk(mutex_);
          myId = traders_.getTraderID.fetch_add(1);
          // register an account here (see below)
          std::cerr << "Trader " << msg.value("name", "") << " logged in as "
                    << myId << "\n";
        } else {
          msg["trader_id"] = myId;
          std::lock_guard<std::mutex> lk(
              orders_queue_.mtx_); // push needs one producer at a time
          orders_queue_.push(msg);
        }
      } catch (const JSON::exception &e) {
        std::cerr << "bad JSON: " << e.what() << "\n";
      }
    }
  }
  close(fd);
}

int main() {
  Exchange::Exchange<1000, 10> exchange("BPSE", 8080, 1000);
  bool val = exchange.startAndRunSocket();
  exchange.createNewBook("AAPL", 120);
  return 0;
}

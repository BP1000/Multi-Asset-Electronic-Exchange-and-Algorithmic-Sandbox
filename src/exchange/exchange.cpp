#include "../../include/exchange.hpp"
#include "orderbook.cpp"
#include <sys/socket.h>

Exchange::SPSC_Queue::SPSC_Queue(std::size_t size)
    : buffer_(size + 1), capacity_(size + 1), head_(0), tail_(0) {};

bool Exchange::SPSC_Queue::push(const JSON order) {
  const std::size_t cur_tail = tail_.load(std::memory_order_relaxed);
  const std::size_t next_tail = (cur_tail + 1) % capacity_;
  if (next_tail == head_.load(std::memory_order_acquire))
    return false;
  buffer_[cur_tail] = order;
  tail_.store(next_tail, std::memory_order_release);
  std::cerr << "Order queued!\n";
  return true;
}

bool Exchange::SPSC_Queue::pop(JSON &order) {
  const std::size_t cur_head = head_.load(std::memory_order_relaxed);
  if (cur_head == tail_.load(std::memory_order_acquire))
    return false;
  order = std::move(buffer_[cur_head]);
  head_.store((cur_head + 1) % capacity_, std::memory_order_release);
  return true;
}

template <std::size_t bookPoolSize, std::size_t bookflatSize>
Exchange::BookManager<bookPoolSize, bookflatSize>::BookManager(
    std::string ticker, Price price, TraderRegistry &traders)
    : ticker_(ticker), traders_(traders),
      book_(
          price, ticker, [this](Trader_ID id, Price px, Volume vol, Side side) {
            std::lock_guard<std::mutex> lk(traders_.mtx);
            auto it = traders_.traders_map.find(id);
            if (it == traders_.traders_map.end())
              return;
            Trader *trader = it->second;
            Capital value = static_cast<Capital>(px) * vol;
            if (side == Buy) {
              trader->removeCapital(value); // buyer pays
            } else {
              trader->addCapital(value); // seller receives
            }
            std::cerr << "FILL trader=" << id << " px=" << px << " vol=" << vol
                      << " side=" << (side == Buy ? "BUY" : "SELL") << "\n";
          }) {}

template <std::size_t bookPoolSize, std::size_t bookflatSize>
ID Exchange::BookManager<bookPoolSize, bookflatSize>::acceptMsg(
    Trader_ID traderID, Price price, Volume volume, Price stop, Side side,
    OrderType type) {
  return book_.addOrder(traderID, price, volume, stop, side, type);
}

// Members listed in declaration order to match the class
template <std::size_t bookPoolSize, std::size_t bookflatSize>
Exchange::Exchange<bookPoolSize, bookflatSize>::Exchange(std::string name_,
                                                         int port,
                                                         std::size_t queueSize)
    : exchangeName_(std::move(name_)), orders_queue_(queueSize), m_port(port),
      m_serverFd(-1) {}

template <std::size_t bookPoolSize, std::size_t bookflatSize>
void Exchange::Exchange<bookPoolSize, bookflatSize>::createNewBook(
    std::string name, Price initialPrice) {
  // heap allocated so the pointer stays valid
  books_[name] =
      new BookManager<bookPoolSize, bookflatSize>(name, initialPrice, traders_);
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

  // The accept loop ONLY accepts. Each client gets its own thread, which
  // handles that client's login and orders for the life of the connection.
  while (true) {
    int fd = accept(listenSocket, nullptr, nullptr);
    if (fd < 0) {
      perror("accept");
      continue;
    }
    std::thread(&Exchange::handleClient, this, fd).detach();
  }
  return true;
}

template <std::size_t P, std::size_t F>
void Exchange::Exchange<P, F>::handleClient(int fd) {
#ifdef SO_NOSIGPIPE
  int one = 1; // don't die if we write to a closed client (macOS)
  setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif

  std::string pending;
  char buf[1024];
  Trader_ID myId = 0; // 0 until this client logs in

  while (true) {
    ssize_t n = recv(fd, buf, sizeof(buf), 0);
    if (n <= 0)
      break; // client closed or error
    pending.append(buf, n);

    size_t pos;
    while ((pos = pending.find('\n')) != std::string::npos) {
      std::string line = pending.substr(0, pos);
      pending.erase(0, pos + 1);
      if (line.empty())
        continue;

      try {
        JSON msg = JSON::parse(line);

        if (msg.contains("capital")) { // login
          Trader_ID id = traders_.getTraderID.fetch_add(1);
          Trader *trader = new Trader(msg.at("capital").get<Capital>(),
                                      msg.at("name").get<std::string>());
          trader->clientSocket_ = fd;
          trader->traderID_ = static_cast<int>(id);
          {
            std::lock_guard<std::mutex> lk(traders_.mtx);
            traders_.traders_map.insert({id, trader});
          }
          {
            std::lock_guard<std::mutex> lk(mutex_);
            fd_to_id_.insert({fd, id});
          }
          myId = id;
          std::cerr << "Login: id=" << id
                    << " name=" << msg.at("name").get<std::string>() << "\n";

        } else if (msg.contains("ticker")) { // order
          if (myId == 0) {
            std::cerr << "Order before login, ignored\n";
            continue;
          }
          msg["trader_id"] = myId; // server decides identity
          std::lock_guard<std::mutex> lk(orders_queue_.mtx_);
          if (!orders_queue_.push(msg))
            std::cerr << "Queue full, order dropped\n";
        }
      } catch (const JSON::exception &e) {
        std::cerr << "bad JSON: " << e.what() << "\n";
      }
    }
  }

  // disconnect cleanup
  {
    std::lock_guard<std::mutex> lk(mutex_);
    fd_to_id_.erase(fd);
  }
  if (myId != 0) {
    std::lock_guard<std::mutex> lk(traders_.mtx);
    auto it = traders_.traders_map.find(myId);
    if (it != traders_.traders_map.end())
      it->second->clientSocket_ = -1;
  }
  close(fd);
}

template <std::size_t P, std::size_t F>
void Exchange::Exchange<P, F>::processMessages() {
  JSON msg;
  while (true) {
    if (!orders_queue_.pop(msg)) {
      std::this_thread::sleep_for(std::chrono::microseconds(50));
      continue;
    }
    try {
      std::string ticker = msg.at("ticker").get<std::string>();
      auto it = books_.find(ticker);
      if (it == books_.end()) {
        std::cerr << "Unknown ticker: " << ticker << "\n";
        continue;
      }

      Trader_ID traderId = msg.at("trader_id").get<Trader_ID>();
      ID orderId = it->second->acceptMsg(
          traderId, msg.at("price").get<Price>(),
          msg.at("volume").get<Volume>(), msg.at("stop").get<Price>(),
          static_cast<Side>(msg.at("side").get<int>()),
          static_cast<OrderType>(msg.at("type").get<int>()));
      std::cerr << "Order added, id=" << orderId << "\n";

      // acknowledge back to the trader's socket
      int fd = -1;
      {
        std::lock_guard<std::mutex> lk(traders_.mtx);
        auto t = traders_.traders_map.find(traderId);
        if (t != traders_.traders_map.end())
          fd = t->second->clientSocket_;
      }
      if (fd >= 0) {
        JSON ack{{"type", "ack"}, {"id", orderId}};
        std::string wire = ack.dump() + '\n';
        send(fd, wire.data(), wire.size(), 0);
      }
    } catch (const JSON::exception &e) {
      std::cerr << "Bad order: " << e.what() << "\n";
    }
  }
}

int main() {
  Exchange::Exchange<1000, 10> exchange("BPSE", PORT, 100000);
  exchange.createNewBook("AAPL", 120);

  // consumer must start BEFORE the blocking accept loop
  std::thread consumer(&Exchange::Exchange<1000, 10>::processMessages,
                       &exchange);
  consumer.detach();

  if (!exchange.startAndRunSocket())
    return 1;
  return 0;
}

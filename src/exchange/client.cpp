#include "../../include/exchange.hpp"
#include <thread>

bool Trader::connectToServer() {
  clientSocket_ = socket(AF_INET, SOCK_STREAM, 0);
  if (clientSocket_ < 0) {
    perror("socket");
    return false;
  }

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(PORT);
  inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
  if (connect(clientSocket_, (const sockaddr *)&address, sizeof(address)) < 0) {
    perror("connect");
    return false;
  }

  JSON login;
  login["name"] = name_;
  login["capital"] = totalCapital_;
  const std::string wire = login.dump() + '\n';
  return send(clientSocket_, wire.data(), wire.size(), 0) >= 0;
}

void Trader::placeOrder(std::string name, Price price, Volume volume,
                        Price stop, Side side, OrderType type) {
  JSON order;
  order["ticker"] = name; // server reads "ticker"
  order["price"] = price;
  order["volume"] = volume;
  order["stop"] = stop;
  order["side"] = static_cast<int>(side); // server casts back to Side
  order["type"] = static_cast<int>(type);

  const std::string wire = order.dump() + '\n';
  if (send(clientSocket_, wire.data(), wire.size(), 0) < 0) {
    perror("send");
  } else {
    std::cerr << "order sent\n";
  }
}

int main() {
  Trader trader(1000, "Bhavik");
  if (!trader.connectToServer()) {
    std::cerr << "Error\n";
    return 1;
  }
  trader.placeOrder("AAPL", 150, 100, 0, Buy, marketOrder);
  trader.placeOrder("AAPL", 150, 100, 0, Buy, marketOrder);

  // give the server time to reply before we exit
  std::this_thread::sleep_for(std::chrono::seconds(1));
}

#include "../../include/exchange.hpp"
#include <format>

Trader::Trader(Capital initialCapital, std::string userName) {
  totalCapital_ = initialCapital;
  name_ = userName;

  int clientSocket = socket(AF_INET, SOCK_STREAM, 0);
  clientSocket_ = clientSocket;

  if (clientSocket < 0) {
    std::cerr << "Failed to establish connection with server";
    return;
  }

  sockaddr_in address;
  address.sin_family = AF_INET;
  address.sin_port = htons(8080);
  address.sin_addr.s_addr = INADDR_ANY;
  serverAddress_ = address;
  if (connect(clientSocket, (const struct sockaddr *)&address,
              sizeof(address)) < 0) {
    std::cerr << "ERROR! Failed to establish connection with server";
    return;
  } else {
    JSON initialMsg;
    initialMsg["name"] = userName;
    initialMsg["capital"] = initialCapital;
    const std::string wire = initialMsg.dump() + '\n';
    if (send(clientSocket, (const std::string *)&wire, size(wire), MSG_EOR) <
        0) {
      std::cerr << "ERROR! Failed to establish connection with server";
      return;
    }
  }

  send(clientSocket, (const std::string *)&name_, size(name_), MSG_EOR);
}

void Trader::placeOrder(std::string name, Price price, Volume volume,
                        Price stop, Side side, OrderType type) {
  JSON order;
  order["action"] = "place order";
  order["name"] = name;
  order["price"] = price;
  order["volume"] = volume;
  order["stop"] = stop;

  send(clientSocket_, (const JSON *)&order, size(order), MSG_EOR);

  // setup server code to send acknowledgement back as the id of the order
};

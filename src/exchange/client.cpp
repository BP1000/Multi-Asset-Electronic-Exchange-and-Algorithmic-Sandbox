#include "../../include/exchange.hpp"
#include <format>

void Trader::placeOrder(std::string name, Price price, Volume volume,
                        Price stop, Side side, OrderType type) {
  JSON order;
  order["action"] = "place order";
  order["name"] = name;
  order["price"] = price;
  order["volume"] = volume;
  order["stop"] = stop;

  if (send(clientSocket_, (const JSON *)&order, size(order), MSG_EOR) > 0) {
    std::cerr << "data sent";
  }

  // setup server code to send acknowledgement back as the id of the order
};

int main() { Trader trader(1000, "Bhavik"); }

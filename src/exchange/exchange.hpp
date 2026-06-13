#include "../../include/exchange.hpp"
#include <thread>
template <std::size_t flatSize>
void Exchange<flatSize>::addBook(Price initialPrice, std::string name) {
  books_[name] = OrderBook<flatSize>(initialPrice, name);
}

template <std::size_t flatSize>
void Exchange<flatSize>::deleteBook(std::string_view name) {
  if (Exchange<flatSize>::getBooks()[name] != nullptr) {
    auto &book = Exchange<flatSize>::getBooks()[name];
    for (Order order : book.getBuy()) {
      book.killOrder(order);
    }
    for (Order order : book.getAsk()) {
      book.killOrder(order);
    }
  }
}

template <std::size_t flatSize>
void Exchange<flatSize>::Trader::placeOrder(std::string name, Price price,
                                            Volume volume, Price stop,
                                            Side side, OrderType type) {

  auto &exchange = Exchange<flatSize>();
  if (exchange.getBooks()[name] != nullptr)
    exchange.getBooks()[name].addOrder(price, volume, stop, side, type);
  t1.join();
}
}
template <std::size_t flatSize>
void Exchange<flatSize>::Trader::killOrder(ID id) {
  auto &exchange = Exchange<flatSize>();
  if (exchange.Trader.orders_[id] != nullptr) {
    exchange.getBooks()[name_].killOrder(id);
  }
}

template <std::size_t flatSize>
void Exchange<flatSize>::Trader::modifyOrder(ID id) {
  auto &exchange = Exchange<flatSize>();
  if (exchange.getBooks()[exchange.Trader.orders_[id]] != nullptr) {
    exchange.getBooks()[exchange.Trader.orders_[id]].modifyOrder(id);
  }
}

#include "orderbook.hpp"
#include <iostream>
// FIX underflow error for volume

std::istream &operator>>(std::istream &is, OrderType &type) {
  int a;
  is >> a;
  type = static_cast<OrderType>(a);
  return is;
}

Order::Order(Price price_, ID id_, Volume volume_, Price stop_, Side side_,
             OrderType type_, bool isCancelled_)
    : price(price_), id(id_), volume(volume_), stop(stop_), side(side_),
      type(type_), isCancelled(isCancelled_) {};

void Order::reset() {
  price = 0;
  id = 0;
  volume = 0;
  stop = 0;
  side = Buy;
  type = marketOrder;
  isCancelled = false;
};

double OrderBook::find_highest_priority(std::shared_ptr<Order> order) {
  auto &side = (order->side == Buy) ? Ask_Side : Buy_Side;
  if (side.empty())
    return 0.0;

  if (order->side == Buy) {
    return side.begin()->first;
  } else {
    return side.rbegin()->first;
  }
};

void OrderBook::addOrdertoBook(std::shared_ptr<Order> order) {
  orders_map[order->id] = order;
  auto &side = (order->side == Buy) ? Buy_Side : Ask_Side;
  auto it = side.find(orders_map[order->id]->price);
  if (it != side.end()) {
    it->second->orders.push(order);
  } else {
    auto level = std::make_unique<PriceLevel>();
    level->totalVolume = order->volume;
    level->orders = std::queue<std::shared_ptr<Order>>();
    level->orders.push(order);
    side[order->price] = std::move(level);
  }
}

void OrderBook::fillOrder(std::shared_ptr<Order> order) {
  auto &side = (order->side == Buy) ? Ask_Side : Buy_Side;
  while (!side.empty() && order->volume > 0) {
    double price = find_highest_priority(order);
    if (order->price < price && (order->side == Buy || order->side == Ask) &&
        order->type == limitOrder) {
      return;
    }
    ID order_id = side[price]->orders.front()->id;
    if (orders_map[order_id]->volume != 0 &&
        orders_map[order_id]->isCancelled != true) {
      if (orders_map[order_id]->volume > order->volume) {
        orders_map[order_id]->volume -= order->volume;
        side[price]->totalVolume -= order->volume;
        order->volume = 0;
      } else {
        order->volume -= orders_map[order_id]->volume;
        side[price]->totalVolume -= orders_map[order_id]->volume;
        orders_map[order->id]->reset();
        available.push_back(orders_map[order->id]);
        orders_map.erase(order->id);
        side[price]->orders.pop();
      }
    }
  }
}

std::shared_ptr<Order> OrderBook::grabOrder() {
  auto order = available.back();
  available.pop_back();
  return order;
}

OrderBook::OrderBook(Volume totalShares) {
  available.reserve(totalShares);
  for (auto i = 1; i < totalShares; ++i) {
    available.push_back(std::make_shared<Order>());
  }
}

ID OrderBook::addOrder(Price price, Volume volume, Price stop, Side side,
                       OrderType type) {
  ID id = next_id.fetch_add(1);
  std::shared_ptr<Order> order = grabOrder();
  order->price = price;
  order->volume = volume;
  order->stop = stop;
  order->side = side;
  order->type = type;
  fillOrder(order);
  if (order->volume > 0) {
    addOrdertoBook(order);
    return order->id;
  }
  return 0;
}

void OrderBook::killOrder(ID id) {
  auto it = orders_map.find(id);
  if (it == orders_map.end())
    return;

  auto &order = it->second;
  order->isCancelled = true;
  order->volume = 0;
  double price = orders_map[id]->price;
  auto &side = (order->side == Buy) ? Buy_Side : Ask_Side;
  auto price_level = side.find(price);
  if (price_level != side.end()) {
    auto &level = price_level->second;
    if (!level->orders.empty() && id == level->orders.front()->id) {
      level->orders.pop();
    }
    if (level->orders.empty()) {
      side.erase(price_level);
    };
  }
}

void OrderBook::editOrder(ID id) {
  if (auto search = orders_map.find(id); search != orders_map.end()) {
    Price newPrice;
    Price newStop;
    Volume newVolume;
    OrderType newType;

    std::cout << "\nEnter new price. If no change type 0.0: ";
    std::cin >> newPrice;
    std::cout << "\n Enter new stop. If no change type 0.0: ";
    std::cin >> newStop;
    std::cout << "\n Enter new volume. If no change type 0: ";
    std::cin >> newVolume;
    std::cout << "\n Enter new type. If no change type your original order";
    std::cin >> newType;

    if (newPrice != search->second->price && newPrice > 0.0) {
      std::shared_ptr<Order> old = orders_map[id];
      auto &side = (old->side == Buy) ? Buy_Side : Ask_Side;
      side[old->price]->totalVolume -= old->volume;
      old->price = newPrice;
      old->stop = newStop;
      old->volume = newVolume;
      old->type = newType;
      std::shared_ptr<Order> newOrder = old;
      addOrdertoBook(newOrder);
      old->isCancelled = true;
    }
    if (newStop != search->second->stop && newStop > 0.0) {
      search->second->stop = newStop;
    }
    if (newVolume != search->second->volume && newVolume > 0) {
      search->second->volume = newVolume;
    }
    if (newType != search->second->type) {
      search->second->type = newType;
    }
  } else {
    std::cout << id << " not found.";
  }
}

void OrderBook::cleanup() {
  auto it = Buy_Side.begin();
  while (it != Buy_Side.end()) {
    if (it->second->totalVolume == 0) {
      it = Buy_Side.erase(it);
    } else
      ++it;
  }

  it = Ask_Side.begin();
  while (it != Ask_Side.end()) {
    if (it->second->totalVolume == 0) {
      it = Ask_Side.erase(it);
    } else {
      ++it;
    }
  }
}

void OrderBook::printBook() {
  std::cout << "=== OrderBook === \n";
  std::cout << "Asks: \n";
  for (auto const &[price, level] : Ask_Side) {
    std::cout << "Price: " << price << "Volume: " << level->totalVolume << "\n";
  }
  std::cout << "Buys: \n";
  for (auto const &[price, level] : Buy_Side) {
    std::cout << "Price: " << price << "Volume: " << level->totalVolume << "\n";
  }
};

int main() {
  OrderBook book(10000);
  ID id1 = book.addOrder(100.00, 500, 75.00, Buy, marketOrder);
  book.killOrder(id1);
  ID id2 = book.addOrder(90.00, 250, 82.00, Ask, limitOrder);
  book.printBook();
  return 0;
}

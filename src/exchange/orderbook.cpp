#include "../../include/orderbook.hpp"
#include <chrono>
#include <iostream>
#include <random>

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
    if (side.begin()->second != nullptr &&
        side.rbegin()->second->orders.empty() == false) {
      return side.begin()->first;
    }
    side.erase(side.begin()->first);
    return find_highest_priority(order);
  } else {
    if (side.rbegin()->second != nullptr &&
        side.rbegin()->second->orders.empty() == false) {
      return side.rbegin()->first;
    }
    side.erase(side.rbegin()->first);
    return find_highest_priority(order);
  }
};

void OrderBook::addOrdertoBook(std::shared_ptr<Order> order) {
  orders_map[order->id] = order;
  auto &side = (order->side == Buy) ? Buy_Side : Ask_Side;
  auto it = side.find(orders_map[order->id]->price);
  if (it != side.end()) {
    it->second->orders.push(order->id);
  } else {
    auto level = std::make_unique<PriceLevel>();
    level->totalVolume = order->volume;
    level->orders = std::queue<ID>();
    level->orders.push(order->id);
    side[order->price] = std::move(level);
  }
}

void OrderBook::fillOrder(std::shared_ptr<Order> order) {
  auto &side = (order->side == Buy) ? Ask_Side : Buy_Side;

  if (side.empty()) {
    return;
  }

  auto startTime = std::chrono::steady_clock::now();
  while (!(side.empty()) && order->volume > 0) {
    double price = find_highest_priority(order);

    if (price < order->price && order->type == limitOrder &&
        order->side == Ask) {
      return;
    }
    if (price > order->price && order->type == limitOrder &&
        order->side == Buy) {
      return;
    }
    // cleanup();
    ID front_id = side[price]->orders.front();
    if (orders_map[front_id] != nullptr) {
      if (orders_map[front_id]->volume > order->volume) {
        orders_map[front_id]->volume -= order->volume;
        order->reset();
        orders_map[order->id] = nullptr;
      } else if (orders_map[front_id]->volume == order->volume) {
        side[price]->orders.pop();
        orders_map[front_id] = nullptr;
        orders_map[order->id] = nullptr;
        orders_map[front_id]->reset();
        order->reset();
        orders_map.erase(front_id);
      } else {
        side[order->price]->orders.pop();
        orders_map[front_id] = nullptr;
        order->volume -= orders_map[front_id]->volume;
        orders_map[front_id]->reset();
        orders_map.erase(front_id);
      }
    } else {
      side[price]->orders.pop();
    }
  }
}

ID OrderBook::addOrder(Price price, Volume volume, Price stop, Side side,
                       OrderType type) {
  ID id = next_id.fetch_add(1);
  std::shared_ptr<Order> order = std::make_shared<Order>();
  order->price = price;
  order->volume = volume;
  order->stop = stop;
  order->side = side;
  order->type = type;
  // fillOrder(order);
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
    if (!level->orders.empty() && id == level->orders.front()) {
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
    if (it->second->orders.empty() || it->second == nullptr) {
      it = Buy_Side.erase(it);
    } else
      ++it;
  }

  it = Ask_Side.begin();
  while (it != Ask_Side.end()) {
    if (it->second->orders.empty()) {
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
    std::cout << "Price: " << price << " Volume: " << level->totalVolume
              << "\n";
  }
  std::cout << "Buys: \n";
  for (auto const &[price, level] : Buy_Side) {
    std::cout << "Price: " << price << " Volume: " << level->totalVolume
              << "\n";
  }
};
// fill order1
int main() {
  OrderBook book;
  // book.addOrder(100, 100, 0, Buy, marketOrder);
  // book.addOrder(100, 100, 0, Ask, marketOrder);
  // book.printBook();

  static std::random_device rd;
  static std::mt19937 gen(rd());
  static std::uniform_int_distribution<int> sideDist(0, 1);
  std::normal_distribution<Price> priceDist(150.0, 5.0);
  std::uniform_int_distribution<> volDist(1, 100);

  std::cout << "Starting simulation with " << 10000 << " orders..."
            << std::endl;
  auto start = std::chrono::high_resolution_clock::now();
  for (auto i = 0; i < 10000; ++i) {
    Side side = static_cast<Side>(sideDist(gen));
    double price = priceDist(gen);
    int volume = volDist(gen);
    OrderType type = static_cast<OrderType>(sideDist(gen));
    book.addOrder(price, volume, 0.0, side, type);
    if (i % 100 == 0) {
      book.cleanup();
      book.printBook();
    }
  }
  auto end = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double> diff = end - start;

  std::cout << "Simulation complete." << std::endl;
  std::cout << "Total time: " << diff.count() << "s" << std::endl;
  std::cout << "Throughput: " << 10000 / diff.count() << " orders/sec"
            << std::endl;
}

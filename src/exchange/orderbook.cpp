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

Order::Order(Price price, ID id, Volume volume, Price stop, Side side,
             OrderType type, bool isCancelled)
    : price_(price), id_(id), volume_(volume), stop_(stop), side_(side),
      type_(type), isCancelled_(isCancelled) {};

void Order::reset() {
  price_ = 0;
  id_ = 0;
  volume_ = 0;
  stop_ = 0;
  side_ = Buy;
  type_ = marketOrder;
  isCancelled_ = false;
};

void Order::info() {
  std::cout << "Name: " << name_;
  std::cout << "Price: " << price_;
  std::cout << "Volume: " << volume_;
  std::cout << "Stop: " << stop_;
}

template <std::size_t flatSize>
template <Side s>
Price OrderBook<flatSize>::find_highest_priority() {
  auto &side = (s == Buy) ? Ask_Side : Buy_Side;
  if (side.empty())
    return 0.0;

  if (s == Buy) {
    if (side.begin()->second != nullptr &&
        side.rbegin()->second->orders_.empty() == false) {
      return side.begin()->first;
    }
    side.erase(side.begin()->first);
    return find_highest_priority<s>();
  } else {
    if (side.rbegin()->second != nullptr &&
        side.rbegin()->second->orders_.empty() == false) {
      return side.rbegin()->first;
    }
    side.erase(side.rbegin()->first);
    return find_highest_priority<s>();
  }
};

template <std::size_t flatSize>
void OrderBook<flatSize>::addOrdertoBook(std::shared_ptr<Order> order) {
  stockPrice_ = order->price_;
  orders_map[order->id_] = order;
  auto &side = (order->side_ == Buy) ? Buy_Side : Ask_Side;
  auto it = side.find(orders_map[order->id_]->price_);
  if (it != side.end()) {
    it->second->orders_.push(order->id_);
  } else {
    auto level = std::make_shared<PriceLevel>();
    level->price_ = order->price_;
    level->totalVolume_ = order->volume_;
    level->orders_ = std::queue<ID>();
    level->orders_.push(order->id_);
    side[order->price_] = std::move(level);
  }
}

template <std::size_t flatSize>
void OrderBook<flatSize>::fillOrder(std::shared_ptr<Order> order) {
  auto &side = (order->side_ == Buy) ? Ask_Side : Buy_Side;

  if (side.empty()) {
    return;
  }

  auto startTime = std::chrono::steady_clock::now();
  while (!(side.empty()) && order->volume_ > 0) {
    double price;
    if (order->side_ == Buy) {
      price = find_highest_priority<Ask>();
    } else {
      price = find_highest_priority<Buy>();
    }
    if (price < order->price_ && order->type_ == limitOrder &&
        order->side_ == Ask) {
      continue;
    }
    if (price > order->price_ && order->type_ == limitOrder &&
        order->side_ == Buy) {
      continue;
    }
    // cleanup();
    ID front_id = side[price]->orders_.front();
    if (orders_map[front_id] != nullptr &&
        orders_map[front_id]->isCancelled_ != true) {
      if (orders_map[front_id]->volume_ > order->volume_) {
        orders_map[front_id]->volume_ -= order->volume_;
        order->reset();
        orders_map[order->id_] = nullptr;
      } else if (orders_map[front_id]->volume_ == order->volume_) {
        side[price]->orders_.pop();
        orders_map[front_id] = nullptr;
        orders_map[order->id_] = nullptr;
        orders_map[front_id]->reset();
        order->reset();
        orders_map.erase(front_id);
      } else {
        side[order->price_]->orders_.pop();
        orders_map[front_id] = nullptr;
        order->volume_ -= orders_map[front_id]->volume_;
        orders_map[front_id]->reset();
        orders_map.erase(front_id);
      }
    } else {
      side[price]->orders_.pop();
    }
  }
}

template <std::size_t flatSize>
ID OrderBook<flatSize>::addOrder(Price price, Volume volume, Price stop,
                                 Side side, OrderType type) {
  ID id = next_id.fetch_add(1);
  std::shared_ptr<Order> order = std::make_shared<Order>();
  order->id_ = id;
  order->price_ = price;
  order->volume_ = volume;
  order->stop_ = stop;
  order->side_ = side;
  order->type_ = type;
  addOrdertoBook(order);
  if (side == Buy) {
    this->flatBook_.template addLevel<Side::Buy>(Buy_Side[price], price);
    return order->id_;
  } else {
    this->flatBook_.template addLevel<Side::Ask>(Ask_Side[price], price);
    return order->id_;
  }
}

template <std::size_t flatSize> void OrderBook<flatSize>::killOrder(ID id) {
  auto it = orders_map.find(id);
  if (it == orders_map.end())
    return;

  auto &order = it->second;
  order->isCancelled_ = true;
  order->volume_ = 0;
  double price = orders_map[id]->price_;
  auto &side = (order->side_ == Buy) ? Buy_Side : Ask_Side;
  auto price_level = side.find(price);
  if (price_level != side.end()) {
    auto &level = price_level->second;
    if (!level->orders_.empty() && id == level->orders_.front()) {
      level->orders_.pop();
    }
    if (level->orders_.empty()) {
      side.erase(price_level);
    }
  }
}

template <std::size_t flatSize> void OrderBook<flatSize>::editOrder(ID id) {
  auto search = orders_map.find(id);
  if (search != orders_map.end()) {
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

    if (newPrice != search->second->price_ && newPrice > 0.0) {
      std::shared_ptr<Order> old = orders_map[id];
      auto &side = (old->side_ == Buy) ? Buy_Side : Ask_Side;
      side[old->price_]->totalVolume_ -= old->volume_;
      old->price_ = newPrice;
      old->stop_ = newStop;
      old->volume_ = newVolume;
      old->type_ = newType;
      std::shared_ptr<Order> newOrder = old;
      addOrdertoBook(newOrder);
      old->isCancelled_ = true;
    }
    if (newStop != search->second->stop_ && newStop > 0.0) {
      search->second->stop_ = newStop;
    }
    if (newVolume != search->second->volume_ && newVolume > 0) {
      search->second->volume_ = newVolume;
    }
    if (newType != search->second->type_) {
      search->second->type_ = newType;
    }
  } else {
    std::cout << id << " not found.";
  }
}

template <std::size_t flatSize> void OrderBook<flatSize>::cleanup() {
  auto it = Buy_Side.begin();
  while (it != Buy_Side.end()) {
    if (it->second->orders_.empty() || it->second == nullptr) {
      it = Buy_Side.erase(it);
    } else
      ++it;
  }

  it = Ask_Side.begin();
  while (it != Ask_Side.end()) {
    if (it->second->orders_.empty()) {
      it = Ask_Side.erase(it);
    } else {
      ++it;
    }
  }
}

template <std::size_t flatSize>
OrderBook<flatSize>::OrderBook(Price initialPrice, std::string name) {
  name_ = name;
  stockPrice_ = initialPrice;
}

template <std::size_t flatSize> Price OrderBook<flatSize>::getStockPrice() {
  return stockPrice_;
}

template <std::size_t flatSize> bool OrderBook<flatSize>::isEmpty() {
  if (orders_map.empty() == true) {
    return true;
  }
  return false;
}

int main() {
  OrderBook<5> book(20.00, "AAPL");
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<int> type_and_side(0, 1);
  std::uniform_real_distribution<Price> randPrice(10.00, 250.00);
  std::uniform_int_distribution<Volume> randVolume(10, 1000);

  for (int i = 0; i < 10000; ++i) {
    double price = randPrice(gen);
    Volume volume = randVolume(gen);
    OrderType type = static_cast<OrderType>(type_and_side(gen));
    Side side = static_cast<Side>(type_and_side(gen));
    book.addOrder(price, volume, 0, side, type);
  }
  book.flatBook_.getflatInfo();
}

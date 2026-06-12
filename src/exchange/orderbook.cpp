#include "../../include/orderbook.hpp"
#include <chrono>
#include <iostream>
#include <random>
#include <thread>

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
Price OrderBook<flatSize>::find_highest_priority(Side s) {
  // ERROR: side.empty keep returning true even when the other side has an order
  // on it
  auto &side = (s == Side::Buy) ? Ask_Side : Buy_Side;

  if (side.empty()) {
    return this->stockPrice_;
  }

  if (s == Buy) {
    if (side.begin()->second != nullptr &&
        side.begin()->second->orders_.empty() == false) {
      return side.begin()->first;
    }
    side.erase(side.begin()->first);
    return find_highest_priority(Buy);
  } else {
    if (side.rbegin()->second != nullptr &&
        side.rbegin()->second->orders_.empty() == false) {
      return side.rbegin()->first;
    }
    side.erase(side.rbegin()->first);
    return find_highest_priority(Ask);
  }
};

template <std::size_t flatSize>
void OrderBook<flatSize>::addOrdertoBook(std::shared_ptr<Order> order) {
  this->stockPrice_ = order->price_;
  std::shared_ptr<Order> mapOrder(order);
  this->orders_map.insert({order->id_, mapOrder});
  auto &side = (order->side_ == Buy) ? this->getBuy() : this->getAsk();
  auto it = side.find(orders_map.find(order->id_)->second->price_);
  if (it != side.end()) {
    it->second->orders_.push(order->id_);
  } else {
    std::shared_ptr<PriceLevel> level = std::make_shared<PriceLevel>();
    level->price_ = order->price_;
    level->totalVolume_ = order->volume_;
    level->orders_ = std::queue<ID>();
    level->orders_.push(order->id_);
    std::shared_ptr<PriceLevel> sideLevel(level);
    side.emplace(std::make_pair(order->price_, sideLevel));
  }
}

template <std::size_t flatSize>
void OrderBook<flatSize>::fillOrder(std::shared_ptr<Order> order) {
  auto &side = (order->side_ == Buy) ? this->getAsk() : this->getBuy();

  if (side.empty() || order->volume_ <= 0)
    return;
  int count = 0;
  while (!side.empty() && order->volume_ > 0 && order != nullptr && count < 5) {
    count++;
    double price{};
    if (order->side_ == Buy) {
      if (order->type_ == limitOrder) {
        price = find_highest_priority(Side::Ask);
        if (this->getAsk().find(price) == this->getBuy().end())
          return;
        if (price > order->price_)
          price = order->price_;
        auto it = side.find(order->price_);
        if (it != side.end() && it->second != nullptr) {
          if (it->second->orders_.empty() || it->second->totalVolume_ <= 0) {
            it++;
          }
        }
      } else {
        order->price_ = find_highest_priority(Side::Buy);
      }
    } else {
      if (order->type_ == limitOrder) {
        price = find_highest_priority(Side::Buy);
        if (side.find(price) == side.end())
          return;
        if (price < order->price_)
          price = order->price_;
        auto it = side.find(order->price_);
        if (it != side.end() && it->second != nullptr) {
          if (it->second->orders_.empty() || it->second->totalVolume_ <= 0) {
            it++;
          }
        }
      } else {
        order->price_ = find_highest_priority(Side::Ask);
      }
    }
    if (order == nullptr)
      return;

    auto it = side.find(order->price_);
    if (it == side.end())
      continue;
    auto item = orders_map.find(it->second->orders_.front());
    if (item == orders_map.end()) {
      if (!it->second->orders_.empty())
        it->second->orders_.pop();
      continue;
    }
    auto front = orders_map.find(it->second->orders_.front())->second;
    if (front == nullptr) {
      continue;
    }

    if (front->volume_ > order->volume_) {
      front->volume_ -= order->volume_;
      order->volume_ = 0;
      order->isCancelled_ = true;
      orders_map.erase(order->id_);
      return;
    } else if (order->volume_ > front->volume_) {
      order->volume_ -= front->volume_;
      front->volume_ = 0;
      front->isCancelled_ = true;
      if (!it->second->orders_.empty())
        it->second->orders_.pop();
      orders_map.erase(front->id_);
    } else if (order->volume_ == front->volume_) {
      order->volume_ -= front->volume_;
      front->volume_ -= front->volume_;
      order->isCancelled_ = true;
      front->isCancelled_ = true;
      if (!it->second->orders_.empty())
        it->second->orders_.pop();
      orders_map.erase(front->id_);
      orders_map.erase(order->id_);
      return;
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
  orders_map.insert({order->id_, order});
  if (order->type_ == limitOrder) {
    double price;
    if (order->side_ == Buy) {
      price = find_highest_priority(Buy);
      if (order->price_ > price) {
        order->price_ = price;
      }
    } else {
      price = find_highest_priority(Ask);
      if (order->price_ < price) {
        order->price_ = price;
      }
    }
  }

  this->fillOrder(order);
  if (order->volume_ == 0) {
    return id;
  }
  this->addOrdertoBook(order);
  auto &bookSide = (order->side_ == Buy) ? Buy_Side : Ask_Side;
  auto it = bookSide.find(order->price_);
  if (it != bookSide.end() && it->second != nullptr) {
    if (side == Buy) {
      this->flatBook_.template addLevel<Side::Buy>(bookSide[order->price_],
                                                   order->price_);
    } else {
      this->flatBook_.template addLevel<Side::Ask>(bookSide[order->price_],
                                                   order->price_);
    }
  }
  return id;
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
OrderBook<flatSize>::OrderBook(Price initialPrice, std::string name,
                               std::size_t flatSizes) {
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

template <std::size_t flatSize> void OrderBook<flatSize>::runBook() {
  std::vector<std::thread> work;

  while (!this->isClosed && !this->getBuy().empty() &&
         !this->getAsk().empty()) {
    work.clear();

    for (const auto &level : this->getBuy()) {
      if (level.second == nullptr || level.second->orders_.empty())
        continue;
      auto id = level.second->orders_.front();
      auto it = orders_map.find(id);
      if (it != orders_map.end() && it->second != nullptr) {
        work.push_back(std::thread(&OrderBook::fillOrder, this, it->second));
      }
    }

    for (const auto &level : this->getAsk()) {
      if (level.second == nullptr || level.second->orders_.empty())
        continue;
      auto id = level.second->orders_.front();
      auto it = orders_map.find(id);
      if (it != orders_map.end() && it->second != nullptr) {
        work.push_back(std::thread(&OrderBook::fillOrder, this, it->second));
      }
    }

    for (auto &order : work) {
      order.join();
    }
    this->flatBook_.getflatInfo();
  }
}
int main() {
  std::size_t size = 10;
  OrderBook<10> book(20.00, "AAPL", size);
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<int> type_and_side(0, 1);
  std::uniform_real_distribution<Price> randPrice(book.getStockPrice() - 5,
                                                  book.getStockPrice() + 5);
  std::uniform_int_distribution<Volume> randVolume(10, 1000);
  for (int i = 0; i <= 100; ++i) {
    double price = randPrice(gen);
    Volume volume = randVolume(gen);
    OrderType type = static_cast<OrderType>(type_and_side(gen));
    Side side = static_cast<Side>(type_and_side(gen));
    book.addOrder(price, volume, 0, side, type);
  }
  book.runBook();
  return 0;
}

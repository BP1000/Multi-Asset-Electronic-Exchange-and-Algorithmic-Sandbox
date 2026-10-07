#include "../../include/orderbook.hpp"
#include <assert.h>
#include <iostream>
#include <numeric>
#include <random>
#include <thread>

std::istream &operator>>(std::istream &is, OrderType &type) {
  int a;
  is >> a;
  type = static_cast<OrderType>(a);
  return is;
}

Order::Order(Price price, ID id, Volume volume, Price stop, Side side,
             OrderType type)
    : price_(price), id_(id), volume_(volume), stop_(stop), side_(side),
      type_(type) {};

void Order::reset() {
  price_ = 0;
  id_ = 0;
  volume_ = 0;
  stop_ = 0;
  side_ = Buy;
  type_ = marketOrder;
  next_ = nullptr;
  prev_ = nullptr;
};

void Order::info() {
  std::cout << "Price: " << price_;
  std::cout << "Volume: " << volume_;
  std::cout << "Stop: " << stop_;
}

Order *PriceLevel::pop() {
  Order *ref = head_;
  head_ = head_->next_;
  return ref;
}

template <std::size_t size>
OrderBookEngine::MemoryPool<size>::MemoryPool()
    : buffer_(new Order[size]), free_(new Order *[size]) {
  std::iota(free_.get(), free_.get() + size, buffer_.get());
  free_head = size;
}

template <std::size_t size>
Order *OrderBookEngine::MemoryPool<size>::acquire() noexcept {
  if (free_head == 0)
    return nullptr;
  return free_[--free_head];
}
template <std::size_t size>
void OrderBookEngine::MemoryPool<size>::release(Order *order) noexcept {
  free_[free_head++] = order;
}

template <std::size_t poolSize, std::size_t flatSize>
OrderBookEngine::OrderBook<poolSize, flatSize>::OrderBook(Price initialPrice,
                                                          std::string name)
    : stockPrice_(initialPrice), name_(std::move(name)){};

template <std::size_t poolSize, std::size_t flatSize>
ID OrderBookEngine::OrderBook<poolSize, flatSize>::addOrder(
    Price price, Volume volume, Price stop, Side side, OrderType type) {
  Order *order = pool_.acquire();
  if (order == nullptr) {
    return 0;
  }
  if (type == marketOrder) {
    order->price_ = stockPrice_;
  } else {
    order->price_ = price;
  }

  assert(order->price_ != 0);
  order->volume_ = volume;
  order->side_ = side;
  order->type_ = type;
  order->stop_ = stop;
  auto &bookside = (order->side_ == Buy) ? getAsk() : getBuy();
  if (bookside.empty()) {
    addOrdertoBook(order);
    return order->id_;
  }

  fillOrder(order);
  if (order->volume_ > 0) {
    addOrdertoBook(order);
    return order->id_;
  }
  return 0;
}

template <std::size_t poolSize, std::size_t flatSize>
void OrderBookEngine::OrderBook<poolSize, flatSize>::fillOrder(Order *order) {
  auto &side = (order->side_ == Buy) ? getAsk() : getBuy();
  if (side.empty())
    return;
  auto &bestlevels =
      (order->side_ == Buy) ? flatBook_.Ask_Levels_ : flatBook_.Buy_Levels_;
  Side opposite = (order->side_ == Buy) ? Ask : Buy;
  while (order->volume_ > 0 && side.empty() == false) {

    auto it = side.find(order->price_);
    if (it == side.end()) {
      auto best = bestlevels[flatBook_.getBestIndex(opposite)];
      if (best == nullptr)
        return;

      if (order->type_ != marketOrder &&
          flatBook_.isBetter(order->side_, order->price_, best->price_)) {
        return;
      }
      order->price_ = best->price_;
      it = side.find(order->price_);
      if (it == side.end()) {
        return;
      }
    }
    PriceLevel *level = it->second;
    while (level != nullptr && level->totalVolume_ > 0 &&
           level->head_ != nullptr) {
      auto head = level->head_;
      if (head->volume_ > order->volume_) {
        head->volume_ -= order->volume_;
        level->totalVolume_ -= order->volume_;
        order->volume_ = 0;
        pool_.release(order);
        return;
      } else if (order->volume_ > head->volume_) {
        order->volume_ -= head->volume_;
        level->totalVolume_ -= head->volume_;
        head->volume_ = 0;
        level->head_ = level->head_->next_;
        pool_.release(head);
        if (level->head_ == nullptr) {
          side.erase(level->price_);
          bestlevels[flatBook_.getBestIndex(opposite)] = nullptr;
        }
      } else {
        level->totalVolume_ -= head->volume_;
        head->volume_ = 0;
        order->volume_ = 0;
        level->head_ = level->head_->next_;
        pool_.release(order);
        pool_.release(head);
        if (level->head_ == nullptr) {
          level->tail_ = nullptr;
          level->totalVolume_ = 0;
          side.erase(level->price_);
          bestlevels[flatBook_.getBestIndex(opposite)] = nullptr;
        }
        return;
      }
    }
  }
}

template <std::size_t poolSize, std::size_t flatSize>
void OrderBookEngine::OrderBook<poolSize, flatSize>::addOrdertoBook(
    Order *order) {
  order->next_ = nullptr;
  order->prev_ = nullptr;
  order->id_ = next_id.fetch_add(1);

  auto &side = (order->side_ == Buy) ? getBuy() : getAsk();
  auto it = side.find(order->price_);
  if (it != side.end()) {
    auto level = it->second;
    if (level->head_ == nullptr) {
      level->head_ = order;
      level->tail_ = order;
    } else {
      order->prev_ = level->tail_;
      level->tail_->next_ = order;
      level->tail_ = order;
    }
    level->totalVolume_ += order->volume_;
    orders_map.insert({order->id_, order});
    flatBook_.addLevel(order->side_, level);
  } else {
    PriceLevel *level = new PriceLevel();
    level->head_ = order;
    level->tail_ = order;
    level->totalVolume_ = order->volume_;
    level->price_ = order->price_;
    side.insert({level->price_, level});
    orders_map.insert({order->id_, order});
    flatBook_.addLevel(order->side_, level);
  }
}

template <std::size_t poolSize, std::size_t flatSize>
void OrderBookEngine::OrderBook<poolSize, flatSize>::killOrder(ID id) {
  auto it = orders_map.find(id);
  if (it == orders_map.end())
    return;
  auto order = it->second;
  auto &side = (order->side_ == Buy) ? getBuy() : getAsk();
  auto &flatSide =
      (order->side_ == Buy) ? flatBook_.Buy_Levels_ : flatBook_.Ask_Levels_;
  auto it2 = side.find(order->price_);
  if (it2 == side.end()) {
    orders_map.erase(order->id_);
    pool_.release(order);
    return;
  }
  auto level = it2->second;
  if (order->prev_ != nullptr)
    order->prev_->next_ = order->next_;
  else
    level->head_ = order->next_;

  if (order->next_ != nullptr)
    order->next_->prev_ = order->prev_;
  else
    level->tail_ = order->prev_;

  level->totalVolume_ -= order->volume_;

  if (level->head_ == nullptr) {
    for (auto i{0}; i < flatSize; ++i) {
      if (flatSide[i] != nullptr) {
        if (flatSide[i]->price_ == order->price_) {
          flatSide[i] = nullptr;
          side.erase(order->price_);
        }
      }
    }
  }

  orders_map.erase(id);
  pool_.release(order);
};

template <std::size_t poolSize, std::size_t flatSize>
ID OrderBookEngine::OrderBook<poolSize, flatSize>::editOrder(ID id, Price price,
                                                             Volume volume,
                                                             Price stop,
                                                             OrderType type) {
  auto it = orders_map.find(id);
  if (it == orders_map.end())
    return id;
  auto order = it->second;
  order->volume_ = volume;
  order->stop_ = stop;
  order->type_ = type;
  if (order->price_ != price) {
    killOrder(id);
    return addOrder(price, order->volume_, order->stop_, order->side_,
                    order->type_);
  }
  return id;
}

// integrate pop function
template <std::size_t poolSize, std::size_t flatSize>
void OrderBookEngine::OrderBook<poolSize, flatSize>::runBook() {
  while (!Buy_Side.empty() && !Ask_Side.empty()) {
    auto bid_it = Buy_Side.begin();
    auto ask_it = Ask_Side.begin();

    // No crossing — book is resting
    if (bid_it->first < ask_it->first)
      break;

    PriceLevel *level = bid_it->second;
    if (level == nullptr || level->head_ == nullptr) {
      Buy_Side.erase(bid_it);
      continue;
    }

    Order *order = level->head_;

    if (level->head_->next_ != nullptr) {
      level->head_ = level->head_->next_;
    } else {
      if (level->totalVolume_ <= 0)
        level = nullptr;
    }
    orders_map.erase(order->id_);

    // assure that the order has been detached
    order->next_ = nullptr;
    order->prev_ = nullptr;
    fillOrder(order);
  }
}

template <std::size_t poolSize, std::size_t flatSize>
void OrderBookEngine::OrderBook<poolSize, flatSize>::getBookInfo() {
  std::cerr << "=== Buy Side === " << std::endl;
  for (auto it = getBuy().begin(); it != getBuy().end(); it++) {
    if (it->second->totalVolume_ > 0)
      std::cerr << "Price: " << it->first
                << " Volume: " << it->second->totalVolume_ << "\n";
  }
  std::cerr << "=== Ask Side === " << std::endl;
  for (auto it = getAsk().begin(); it != getAsk().end(); it++) {
    if (it->second->totalVolume_ > 0)
      std::cerr << "Price: " << it->first
                << " Volume: " << it->second->totalVolume_ << "\n";
  }
}

int main() {
  // Fixed seed for deterministic results
  std::mt19937 gen(rand());
  std::uniform_int_distribution<Price> randPrice(9500,
                                                 10500); // $95.00 - $105.00
  std::uniform_int_distribution<Volume> randVolume(1, 100);
  std::uniform_int_distribution<int> randType(0, 1);

  OrderBookEngine::OrderBook<50000, 15> book(9550, "BPSE");

  std::cerr << "=== TESTING BOOK ==" << std::endl;

  for (auto i{0}; i < 10000; ++i) {
    Price price = randPrice(gen);
    Volume volume = randVolume(gen);
    Side side = static_cast<Side>(randType(gen));
    OrderType type = static_cast<OrderType>(randType(gen));
    book.addOrder(price, volume, 0, side, type);
  }
  book.callRun();
  book.getBookInfo();
  return 0;
}

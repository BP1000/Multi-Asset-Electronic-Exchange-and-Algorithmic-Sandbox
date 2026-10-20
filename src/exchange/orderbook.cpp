#include "../include/orderbook.hpp"
#include <assert.h>
#include <iostream>
#include <numeric>
#include <random>

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

template <std::size_t size>
OrderBookEngine::MemoryPool<size>::MemoryPool()
    : buffer_(new Order[size]), free_(new Order *[size]) {
  std::iota(
      free_.get(), free_.get() + size,
      buffer_
          .get()); // fills free_ with pointers to the Order objects in buffer_
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
  order->reset();
  free_[free_head++] = order;
}

template <std::size_t poolSize, std::size_t flatSize>
OrderBookEngine::OrderBook<poolSize, flatSize>::OrderBook(Price initialPrice,
                                                          std::string name)
    : stockPrice_(initialPrice), name_(name){};

template <std::size_t poolSize, std::size_t flatSize>
ID OrderBookEngine::OrderBook<poolSize, flatSize>::addOrder(
    Price price, Volume volume, Price stop, Side side, OrderType type) {
  Order *order = pool_.acquire();
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
  auto &bookside = (order->side_ == Buy) ? getBuy() : getAsk();
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
bool OrderBookEngine::OrderBook<poolSize, flatSize>::fillOrder(Order *order) {
  auto &side = (order->side_ == Buy) ? getAsk() : getBuy();
  if (side.empty())
    return false;
  auto &bestlevels =
      (order->side_ == Buy) ? flatBook_.Ask_Levels_ : flatBook_.Buy_Levels_;
  Side opposite = (order->side_ == Buy) ? Ask : Buy;
  while (order->volume_ > 0 && side.empty() == false) {

    auto it = side.find(order->price_);
    if (it == side.end()) {
      auto best = bestlevels[flatBook_.getBestIndex(opposite)];
      if (best == nullptr)
        return false;

      if (order->type_ != marketOrder &&
          flatBook_.isBetter(order->side_, order->price_, best->price_)) {
        return false;
      }
      order->price_ = best->price_;
      it = side.find(order->price_);
      if (it == side.end()) {
        return false;
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
        return true;
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
        return true;
      }
    }
  }
  return false;
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
    stockPrice_ = level->price_;
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
    stockPrice_ = level->price_;
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
    return 0;
  auto order = it->second;
  order->volume_ = volume;
  order->stop_ = stop;
  order->type_ = type;
  if (order->price_ != price) {
    killOrder(id);
    return addOrder(price, order->volume_, order->stop_, order->side_,
                    order->type_);
  }
  return 0;
}

int main() {
  // Fixed seed for deterministic results
  std::mt19937 gen(rand());
  std::uniform_int_distribution<Price> randPrice(9500,
                                                 10500); // $95.00 - $105.00
  std::uniform_int_distribution<Volume> randVolume(1, 1000);
  std::uniform_int_distribution<int> randSide(0, 1);
  std::uniform_int_distribution<int> randType(0, 1);

  OrderBookEngine::OrderBook<5000, 15> book(9550, "BPSE");

  std::cerr << "=== Adding Orders ===\n";

  // Add 20 limit orders to build up the book
  for (int i = 0; i < 1000; ++i) {
    Price price = randPrice(gen);
    Volume vol = randVolume(gen);
    Side side = (randSide(gen) == 0) ? Buy : Ask;

    std::cerr << "Adding limit order — price: " << price << " volume: " << vol
              << " side: " << (side == Buy ? "Buy" : "Ask") << "\n";

    if (book.addOrder(price, vol, 0, side, limitOrder) == 0) {
      std::cout << "Order filled!";
    }
  }

  std::cerr << "\n=== Sending Market Orders ===\n";

  // Send 5 market orders to trigger matching
  for (int i = 0; i < 1000; ++i) {
    Volume vol = randVolume(gen);
    Side side = (randSide(gen) == 0) ? Buy : Ask;

    std::cerr << "Market order — volume: " << vol
              << " side: " << (side == Buy ? "Buy" : "Ask") << "\n";

    if (book.addOrder(0, vol, 0, side, marketOrder) == 0) {
      std::cout << "Order filled!\n";
    }
  }

  std::cerr << "\n=== Cancelling Orders ===\n";

  // Cancel a few orders by ID
  for (ID id = 1; id <= 3; ++id) {
    std::cerr << "Cancelling order ID: " << id << "\n";
    book.killOrder(id);
  }

  std::cerr << "\n === FILLING REMAINING ORDERS ==\n";
  for (auto &order : book.getOrdersMap()) {
    if (book.fillOrder(order.second))
      std::cout << "Order filled!\n";
  }

  std::cerr << "\n=== Done ===\n";
  return 0;
}

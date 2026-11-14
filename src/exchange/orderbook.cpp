#include "../../include/orderbook.hpp"
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
ID OrderBookEngine::OrderBook<poolSize, flatSize>::addOrder(
    Trader_ID traderID, Price price, Volume volume, Price stop, Side side,
    OrderType type) {
  Order *order = pool_.acquire();
  if (!order)
    return 0;
  if (type == marketOrder) {
    order->price_ = stockPrice_;
  } else {
    order->price_ = price;
  }

  assert(order->price_ != 0);
  order->traderID_ = traderID;
  order->volume_ = volume;
  order->side_ = side;
  order->type_ = type;
  order->stop_ = stop;
  auto &bookside = (order->side_ == Buy) ? getBuy() : getAsk();
  if (!fillOrder(order)) {
    addOrdertoBook(order);
    return order->id_;
  }
  pool_.release(order);
  return 0;
}
template <std::size_t poolSize, std::size_t flatSize>
bool OrderBookEngine::OrderBook<poolSize, flatSize>::fillOrder(Order *order) {
  const bool buying = (order->side_ == Buy);
  auto &side =
      buying ? Ask_Side : Buy_Side; // both maps are std::greater<Price>

  while (order->volume_ > 0 && !side.empty()) {
    // greater<> ordering: begin() = highest price, last = lowest price
    auto it =
        buying ? std::prev(side.end()) : side.begin(); // best opposite level
    PriceLevel *level = it->second;
    Price bestPx = level->price_;

    // limit orders only match if the price crosses
    if (order->type_ != marketOrder) {
      if (buying ? (bestPx > order->price_) : (bestPx < order->price_))
        break;
    }

    Order *cur = level->head_;
    while (cur != nullptr && order->volume_ > 0) {
      Order *next = cur->next_;
      if (cur->traderID_ != order->traderID_) { // skip own orders
        Volume qty = std::min(order->volume_, cur->volume_);

        filler_(order->traderID_, bestPx, qty,
                order->side_); // both sides settle
        filler_(cur->traderID_, bestPx, qty, cur->side_);

        order->volume_ -= qty;
        cur->volume_ -= qty;
        level->totalVolume_ -= qty;
        stockPrice_ = bestPx;

        if (cur->volume_ == 0) { // resting order fully filled
          if (cur->prev_)
            cur->prev_->next_ = cur->next_;
          else
            level->head_ = cur->next_;
          if (cur->next_)
            cur->next_->prev_ = cur->prev_;
          else
            level->tail_ = cur->prev_;
          orders_map.erase(cur->id_);
          pool_.release(cur);
        }
      }
      cur = next;
    }

    if (level->head_ == nullptr) { // level emptied: remove it
      for (std::size_t i = 0; i < flatSize; ++i) {
        if (flatBook_.Buy_Levels_[i] == level)
          flatBook_.Buy_Levels_[i] = nullptr;
        if (flatBook_.Ask_Levels_[i] == level)
          flatBook_.Ask_Levels_[i] = nullptr;
      }
      side.erase(it);
      delete level;
    } else {
      break; // incoming is done, or only our own orders remain at this level
    }
  }
  return order->volume_ == 0;
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

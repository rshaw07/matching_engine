#pragma once

#include <drogon/HttpController.h>
#include <iostream>
#include <atomic>
using namespace drogon;
using namespace std;

struct Order {
  uint64_t orderId;
  string symbol; // BTCUSD, ETHUSD, etc.
  double price;
  double quantity;
  string side; // "buy" or "sell"
  string orderType; // "limit" or "market"
  string timestamp;
};

struct Trade {
  uint64_t tradeId;
  uint64_t makerOrderId;
  uint64_t takerOrderId;
  string aggressor;
  string symbol;
  double price;
  double quantity;
  string timestamp;
};

struct PriceLevel{
  deque<shared_ptr<Order>> orders;
  double totalQuantity = 0.0;
};

struct OrderBook{
  map<double, PriceLevel, greater<double>> bids;
  map<double, PriceLevel> asks;
  mutex bookMutex;
};

struct OrderResult{
    uint64_t orderId;
    string status;
    double executedQuantity;
    double remainingQuantity;
    double averagePrice;
};

string getTime();
class matching_engine : public drogon::HttpController<matching_engine>
{
  public:
  static atomic<uint64_t> orderCounter;
  static atomic<uint64_t> tradeCounter;
  void recordTrades(const Order &buyOrder, const Order &sellOrder, double tradeQuantity, double tradePrice, string aggressorSide);
  OrderResult submitOrder(Order parsedOrder);
  void updateRecords(shared_ptr<OrderBook> currentBook, const string& symbol);
  
  METHOD_LIST_BEGIN
  METHOD_ADD(matching_engine::order, "/order", Post);
  
  METHOD_LIST_END
  void order(const HttpRequestPtr& req, std::function<void (const HttpResponsePtr &)> &&callback);
  static const auto& getPendingOrders() {
    return pendingOrders;
  }

  static uint64_t getNextOrderId(){
    return ++orderCounter;
  }

  static uint64_t getNextTradeId(){
    return ++tradeCounter;
  }

  private:



  static inline unordered_map<string, shared_ptr<OrderBook>> pendingOrders;
  mutex engineMutex;
};

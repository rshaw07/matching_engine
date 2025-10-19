#pragma once

#include <drogon/HttpController.h>
#include <iostream>
using namespace drogon;
using namespace std;

struct Order {
  string orderId;
  string symbol; // BTCUSD, ETHUSD, etc.
  string price;
  string quantity;
  string side; // "buy" or "sell"
  string orderType; // "limit" or "market"
  string timestamp;
};

struct Trade {
  string tradeId;
  string makerOrderId;
  string takerOrderId;
  string aggressor;
  string symbol;
  string price;
  string quantity;
  string timestamp;
};

class matching_engine : public drogon::HttpController<matching_engine>
{
  public:
    void recordTrades(const Order &buyOrder, const Order &sellOrder, double tradeQuantity, double tradePrice, string aggressorSide);
    void submitOrder(Order parsedOrder);

    METHOD_LIST_BEGIN
    METHOD_ADD(matching_engine::order, "/order", Post);
    METHOD_ADD(matching_engine::tradeBook, "/book/{symbol}", Get);
    METHOD_ADD(matching_engine::orderDepth, "/depth/{symbol}", Get);

    METHOD_LIST_END
    void order(const HttpRequestPtr& req, std::function<void (const HttpResponsePtr &)> &&callback);
    void tradeBook(const HttpRequestPtr& req, std::function<void (const HttpResponsePtr &)> &&callback, const string& symbol);
    void orderDepth(const HttpRequestPtr& req, std::function<void (const HttpResponsePtr &)> &&callback, const string& symbol);
    
  private:


  struct PriceLevel{
    deque<shared_ptr<Order>> orders;
    double totalQuantity = 0.0;
  };

  struct OrderBook{
    map<double, PriceLevel, greater<double>> bids;
    map<double, PriceLevel> asks;
    unordered_map<string, shared_ptr<Order>> ordersById;
    mutex bookMutex;
  };

  unordered_map<string, shared_ptr<OrderBook>> pendingOrders;
  mutex engineMutex;
  unordered_map<string, vector<Trade>> tradeHistory;
};

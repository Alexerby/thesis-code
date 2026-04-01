#include "feature_engineering/order_tracker.hpp"

class OrderAnalyser {
public:
  OrderAnalyser(OrderTracker tracker) : order_tracker_(std::move(tracker)) {}

private:
  OrderTracker order_tracker_;
};

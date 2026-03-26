#pragma once
#include <databento/record.hpp>

struct ImbalanceLevels {
  double imb_1;
  double imb_3;
  double imb_5;
  double imb_10;
};

struct FeatureVector {
  ImbalanceLevels imbalance_levels;
  int64_t mid_price;
  int64_t sprad;
};

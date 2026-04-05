# Recording of Feature Records flow

  ```mermaid
--- 
config:
  layout: elk
  look: handDrawn
  theme: dark
---
sequenceDiagram
  participant F as DBN File
  participant RE as ReplayEngine
  participant M as Market
  participant OT as OrderTracker
  participant FR as feature_records_

  F->>RE: MboMsg stream
  loop every message
      RE->>M: market.Apply(mbo)
      RE->>OT: Router(mbo)
      alt action == Add
          OT->>OT: order_map.insert(Order) <br> snapshot imbalance, dist_touch, ts
      else action == Cancel
          OT->>OT: Reconcile(mbo)
          OT->>FR: EmitFeatureRecord(Order, mbo)
      end
  end
  ```

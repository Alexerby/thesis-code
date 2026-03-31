# Order (Lifetime) Tracking

This document contains guidelines and information about the algorithmic order tracking for the XNAS.ITCH market data feed.

## Routing of MBO Messages for XNAS.ITCH

The [Databento MBO Schema](https://databento.com/docs/schemas-and-data-formats/mbo#fields-mbo?historical=cpp&live=cpp&reference=python) contains multiple fields which are not utilized for the XNAS.ITCH data.
This is clearly out of necessity from Databento's side, but is not immediately clear to the end user of their historical data.

This document serves as a basis for understanding the implemented algorithm for order lifetime tracking, which should relatively easy to consume for individuals without prior experience in CPP.

### Introduction

Every MBO Message contains a field  `Action`.
The following actions are defined in the [Databento MBO Schema](https://databento.com/docs/schemas-and-data-formats/mbo#fields-mbo?historical=cpp&live=cpp&reference=python):


```mermaid
---
config:
  layout: elk
  look: handDrawn
  theme: dark
---
flowchart TD
    Root([MBOMsg::Action])
    
    subgraph Operations [" "]
        direction LR
        A[Add]
        C[Cancel]
        M[Modify]
        T[Trade]
        F[Fill]
        N[None]
        CLR[Clear]
    end

    Root --> A
    Root --> C
    Root --> M
    Root --> T
    Root --> F
    Root --> N
    Root --> CLR

    style Operations fill:none,stroke-dasharray: 5 5
```

By inspecting the data, one will immediately arrive a couple of insights in how the messages are treated by the exchanges, and therefore Databento.

### Simplifying the MBO Schema by remapping events
* **Modifications** are not streamed as `Action::Modify`, but rather as either an `Action::Add` or an `Action::Cancel` (partial) on an existing `order_id`.
* For tracking the state of the order book ($\mathcal L_t$) `Action::Trade` is not relevant for us.
* Fill is represented by an `Action::Fill` followed by an `Action::Cancel` sharing `sequence` (and `ts_recv`).
The models in this project will use these two messages in conjunction and map this to a `Fill`.
We can therefore ignore these events when tracking the state of the order and treat an `Action::Fill` + `Action::Cancel` followed by a bitfield `F_LAST` (128) as an `Action::Fill`.


---

### Algorithm
This project implements the following algorithm for (i) routing and (ii) tracking the life duration of an order.

This algorithm requires three data structures
| Structure | Type | Key/Value | Purpose |
| :--- | :--- | :--- | :--- |
| **Order Map** | `std::unordered_map` | `order_id` $\rightarrow$ `Order` | **Persistent Store:** Quick lookup when a Cancel/Fill arrives. |
| **Staging Map** | `std::unordered_map` | `sequence_id` $\rightarrow$ `Fill` | **Staging Area:** Matches asynchronous fills to orders. |
| **Expiry Queue** | `std::deque` | `pair<order_id, ts>` | **TTL Tracker:** Keeps track of who is the "oldest" for pruning. |


```mermaid
---
config:
  layout: elk
  look: handDrawn
  theme: dark
---
flowchart TB
    A([MBOMsg]) --> Prune[Prune Expired Orders]
    Prune --> Action{Action Routing}

    %% EXPIRE LOGIC
    Prune -.->|O-1 Check| ExpiryQueue
    ExpiryQueue -.->|Erase| OrderMap

    %% FILL BRANCH
    Action -->|Fill| Accumulate[Add Fill Size to<br/>PendingVolumeMap]
    Accumulate --> IsFillLast{Flg == 128?}
    IsFillLast -->|Yes| CheckStaged
    IsFillLast -->|No| End([State Updated])

    %% CANCEL BRANCH
    Action -->|Cancel| IsCancelLast{Flg == 128?}
    IsCancelLast -->|No| PureCancel[Reduce OrderMap Size]
    IsCancelLast -->|Yes| CheckStaged{Pending Vol?}
    
    %% CLEAR BRANCH
    Action -->|Clear| Reset[Erase: OrderMap,<br/>PendingVolumeMap,<br/>ExpiryQueue]
    Reset --> End

    %% RECONCILIATION
    CheckStaged -->|Yes| Reconcile[OrderMap.Vol -=<br/>Staged + Msg.Size]
    CheckStaged -->|No| Reconcile[OrderMap.Vol -=<br/>Msg.Size]
    
    Reconcile --> ClearStaged[Erase from<br/>PendingVolumeMap]
    ClearStaged --> VolCheck

    %% POST-RECONCILE & ADD
    Action -->|Add| NewOrder[Insert to Order Map<br/>& Expiry Queue]
    NewOrder --> End
    
    VolCheck{Volume <= 0?}
    VolCheck -->|Yes| EraseOrder[Erase from Order Map]
    VolCheck -->|No| UpdateState[Update Order Map State]
    
    EraseOrder --> End
    UpdateState --> End
    PureCancel --> End

    %% OTHER BRANCHES
    Action -->|Trade/Modify| End

    subgraph Storage [Memory Management]
        OrderMap[(Order Map<br/>Key: order_id)]
        PendingVolumeMap[(Pending Volume<br/>Key: order_id)]
        ExpiryQueue[(Expiry Queue<br/>std::deque)]
    end

    %% --- CLASS DEFINITIONS ---
    classDef decision fill:#8e44ad,stroke:#9b59b6,color:#fff
    classDef storage fill:#2d3436,stroke:#00cec9,color:#00cec9
    classDef staging fill:#6c5ce7,stroke:#a29bfe,color:#a29bfe
    classDef expiry fill:#e67e22,stroke:#d35400,color:#fff
    classDef destructive fill:#c0392b,stroke:#ff0000,color:#fff

    %% --- APPLYING CLASSES ---
    class Action,IsFillLast,IsCancelLast,CheckStaged,VolCheck decision
    
    %% Storage & Linked Logic
    class OrderMap,NewOrder,EraseOrder,UpdateState,Reconcile,PureCancel storage
    class PendingVolumeMap,Accumulate,ClearStaged staging
    class ExpiryQueue,Prune expiry
    class EraseOrder,Reset destructive
```

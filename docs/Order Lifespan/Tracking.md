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
    %% START & CLEANUP
    A([MBOMsg]) --> Prune[Prune Expired Orders<br/>Check Expiry Queue Front]
    Prune --> Action{Action Routing}

    %% EXPIRE LOGIC
    Prune -.->|O-1 Check| ExpiryQueue
    ExpiryQueue -.->|Erase| OrderMap

    %% ADD BRANCH
    Action -->|Add| PersistentLookupAdd{Order ID in<br/>Order Map?}
    
    PersistentLookupAdd -->|No| NewOrder[Insert New Record into<br/>Order Map & Expiry Queue]
    PersistentLookupAdd -->|Yes| IncrementVol[Update Order Map Record:<br/>Volume += New Volume]

    %% FILL BRANCH
    Action -->|Fill| StageFill[Insert into Staging Map<br/>Key: sequence_id]

    %% CANCEL BRANCH
    Action -->|Cancel| StagingLookup{sequence_id in<br/>Staging Map?}
    
    StagingLookup -->|Yes| ProcessFill[Reconcile:<br/>Update Order Map Volume]
    StagingLookup -->|No| PureCancel[Reduce Order Map Size<br/>Using Order ID]

    %% EAGER CLEANUP CHECK
    ProcessFill --> VolCheck{Volume <= 0?}
    PureCancel --> VolCheck

    VolCheck -->|Yes| EraseOrder[Erase from Order Map]
    VolCheck -->|No| UpdateState[Keep in Order Map]

    %% MODIFY BRANCH
    Action -->|Modify| ModStep[Kill Program <br> Should not exist in ITCH]

    %% CLEAR BRANCH
    Action -->|Clear| ClearBook[Erase: Order Map, Staging Map, Expiry Queue]



    %% DATA STORES
    subgraph Storage [Memory Management]
        OrderMap[(Order Map<br/>unordered_map<br/>Key: order_id)]
        StagingMap[(Staging Map<br/>unordered_map<br/>Key: sequence_id)]
        ExpiryQueue[(Expiry Queue<br/>deque<br/>Value: id + ts)]
    end

    %% FINAL STATES
    NewOrder --> End([State Updated])
    IncrementVol --> End
    StageFill --> End
    EraseOrder --> End
    UpdateState --> End

    %% --- CLASS DEFINITIONS ---
    classDef decision fill:#8e44ad,stroke:#9b59b6
    classDef destructive fill:#d63031,stroke:#ff7675
    classDef success fill:#1b4d3e,stroke:#2ecc71
    classDef staging fill:#6c5ce7,stroke:#a29bfe
    classDef alert fill:#c0392b,stroke:#ff0000,color:#fff,stroke-width:4px
    classDef storage fill:#2d3436,stroke:#00cec9,stroke-dasharray: 5 5

    %% --- APPLYING CLASSES ---
    class PersistentLookupAdd,StagingLookup,VolCheck,Action decision
    class EraseOrder,ModStep,ClearBook destructive
    class NewOrder success
    class StageFill,StagingLookup,StagingMap staging
    class Storage,OrderMap,ExpiryQueue storage
    
    %% Specific overrides
    style Prune fill:#e67e22,stroke:#d35400
    style IncrementVol fill:#2980b9,stroke:#3498db
    style PureCancel fill:#e17055,stroke:#fab1a0
    style ProcessFill fill:#e17055,stroke:#fab1a0

    %% Link Styling
    linkStyle 15 stroke:#ff4757,color:#ff4757
    linkStyle 4,5,6,7,8,9,10,11,12,13,14,16,17,18,19,20 stroke:#2ed573,color:#2ed573
```

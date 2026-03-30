# Routing of MBO Messages
This document contains information about how the models in this project simplifies the order message routing when tracking individual orders. 

## Introduction

Every MBO Message contains a field  `Action`.
The following actions are defined in the [Databento MBO Schema](https://databento.com/docs/schemas-and-data-formats/mbo#fields-mbo?historical=cpp&live=cpp&reference=python):


```mermaid
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

## Simplifying the MBO Schema by remapping events
* **Modifications** are not streamed as `Action::Modify`, but rather as either an `Action::Add` or an `Action::Cancel` (partial) on an existing `order_id`.
* For tracking the state of the order book ($\mathcal L_t$) `Action::Trade` is not relevant for us.


---
## New mapping

### Add (`Action::Add`)
Nothing fancy with `Action::Add`, it is a standalone message which adds liquidity to the book.

### Cancel (`Action::Cancel`)
An `Action::Cancel` which is not followed by an `Action::Fill` (with the same `sequence` number) will be treated as a pure order cancellation.

### Trade (`Action::Trade`)
Ignored -- can reconstruct entire order book without these entries.

### Clear (`Action::Clear`)
Will be found at the start of each trading session and whenever the exchange manually flushes the books.

### Fill (`Action::Fill` && `Action::Cancel`)
Fill is represented by an `Action::Fill` followed by an `Action::Cancel` sharing `sequence` (and `ts_recv`).
The models in this project will use these two messages in conjunction and map this to a `Fill`.

We can therefore ignore these events when tracking the state of the order and treat an `Action::Fill` + `Action::Cancel` followed by a bitfield `F_LAST` (128) as an `Action::Fill`.


## The new mapping is therefore 

```mermaid
flowchart LR
    %% Source Nodes
    Add(Add)
    Cancel_pure(Cancel)
    Modify(Modify)
    Trade(Trade)
    Fill(Fill)
    Cancel_fill(Cancel <br/>same sequence)
    Clear(Clear)

    %% Destination Nodes
    ADD_OUT[Add <br/>Adds liquidity]
    CXL_OUT[Cancel <br/>Pure cancellation]
    MOD_A[Add <br/>Price improved]
    MOD_C[Cancel <br/>Qty reduced]
    IGN_OUT[Ignored <br/>Not needed for state]
    FILL_OUT[Fill <br/>Merged on F_LAST 128]
    CLR_OUT[Clear <br/>Session/Flush]

    %% Connections
    Add --> ADD_OUT
    Cancel_pure --> CXL_OUT
    Modify --> MOD_A
    Modify --> MOD_C
    Trade -.-> IGN_OUT
    Fill --> FILL_OUT
    Cancel_fill --> FILL_OUT
    Clear --> CLR_OUT

    %% Styling for "Ignored" path
    style IGN_OUT stroke-dasharray: 5 5
```



# Routing of MBO Messages

Every MBO Message comes with an `action`.
The following actions are defined in the [Databento MBO Schema](https://databento.com/docs/schemas-and-data-formats/mbo#fields-mbo?historical=cpp&live=cpp&reference=python):


```mermaid
flowchart TD
    classDef theme fill:none,stroke:currentColor,stroke-width:1px

    Root([MBOMsg::Action]):::theme
    
    subgraph Operations [" "]
        direction LR
        A[Add]:::theme
        C[Cancel]:::theme
        M[Modify]:::theme
        T[Trade]:::theme
        F[Fill]:::theme
        N[None]:::theme
    end

    Root --> A
    Root --> C
    Root --> M
    Root --> T
    Root --> F
    Root --> N

    style Operations fill:none,stroke:currentColor,stroke-dasharray: 5 5
```

By inspecting the data, one will immediately see a couple of insights.

* Modifications are not streamed as `Action::Modify`, but rather as either an `Action::Add` or an `Action::Cancel` (partial) on an existing `order_id`.

## Add
Nothing fancy with `Action::Add`, it 


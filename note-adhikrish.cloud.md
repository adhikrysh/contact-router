# contact-router

links in a space network exist only during scheduled contact windows, many of them share a transmitter, and data has a deadline. routing therefore can't assume a path exists right now; it has to plan across a schedule, and two routes that look feasible independently can both need the same radio at the same time.

contact-router computes the earliest arrival for each bundle over a contact plan and reserves transmitter time along the chosen route, so overlapping bookings on a shared radio are impossible by construction.

```text
bundle -> stored at node -> first free slot that fits -> shared radio calendar -> arrival
```

![Contact reservations](docs/reservations.png)

## design

the search is an earliest-arrival label search over contacts, each with open and close times, data rate, propagation delay and an optional shared resource. one label per node is sufficient because storage is unlimited, waiting is free, and each resource calendar is filled first-fit in time order, so an earlier arrival never removes an option a later one would have had. buffer limits or energy costs would break that property and require a different search.

time is integer microseconds with an overflow check on every addition. the search never mutates a calendar: once a complete route is found, the router copies the calendars, books every hop, and swaps the copies in. a failed search, a missed deadline or an allocation failure partway through leaves the existing plan untouched. tie-breaking is deterministic, so the result doesn't depend on input row order.

## example

in the lunar scenario, health telemetry has priority, takes the direct link and arrives at 6.3 s. science data doesn't fit in the remainder of the first relay window, waits for the next one and arrives at 44.3 s. the image bundles have no feasible route left.

```text
health,reserved,6300000,6300000,3
science,reserved,44300000,44300000,4;5
image,no_route,,,
```

batch scheduling is greedy: each bundle gets its earliest arrival given existing bookings. that's deterministic and easy to reason about, but it doesn't optimise the batch as a whole, and one bundle's choice can block a cheaper alternative for a later one. a joint objective would be the next step.

## verification

2,800 reservations checked against an independent exhaustive path search and a microsecond-by-microsecond slot scan, plus shared-transmitter conflicts, expiry and delay boundaries, route release, overflow, search limits and permuted inputs.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build
ctest --test-dir build --output-on-failure
./build/contact-route examples/contacts.csv examples/bundles.csv
```

not modelled: fragmentation, buffer limits, retransmission, uncertain contact times. it's a planning simulator rather than a bundle protocol implementation; nasa's [dtn overview](https://www.nasa.gov/communicating-with-missions/delay-disruption-tolerant-networking/) covers the wider context.

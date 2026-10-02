# contact-router

contact-router plans routes over links that are available only at scheduled times. each contact has a transmission rate, closing time, and propagation delay. a bundle, a unit of data, can wait at a relay, but each full transmission must fit inside its contact window.

the planner reserves actual transmission intervals. contacts can share a transmitter, so two otherwise valid routes cannot use the same radio at once.

![Contact reservations](docs/reservations.png)

## build and run

requires C++20 and CMake 3.20+. it has no runtime dependencies.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 3
ctest --test-dir build --output-on-failure
./build/contact-route examples/contacts.csv examples/bundles.csv
./build/contact-route examples/contacts.csv examples/bundles.csv --bookings
```

the first command prints one result per bundle. the second prints reserved transmission intervals. the CSV files use integer microseconds, bytes, and bytes per second. names accept letters, digits, underscores, hyphens, and dots. quoted CSV fields are not supported.

## the example

a lunar node can send directly to Earth or through a relay. health data has higher priority than science data, so it uses the direct link and arrives at 6.3 seconds.

that transmission occupies the lunar transmitter for five seconds. the science bundle cannot fit in the remaining time of the first relay contact. it waits for the later contact and arrives at 44.3 seconds. the image bundle has no feasible route under the remaining reservations.

counting link capacity alone is not enough. the free time must exist when both the bundle and transmitter are available.

## routing rule

the library uses an earliest-arrival search. at each node, it finds the first free transmission slot in each outgoing contact, adds propagation time, and keeps the earliest arrival at the next node. that is sufficient here because storage is unlimited and a bundle may always wait.

capacity is reserved only after the planner finds a complete route. a failed search consumes nothing. the library can release a planned route and disable a contact before replanning. releasing a reservation does not model undoing a transmission that already happened.

the CLI processes bundles by release time, descending priority, earliest expiry, then bundle ID. each route is earliest-arrival for that bundle given existing reservations. the complete set of routes is not necessarily optimal for total throughput or fairness. the example exposes that tradeoff.

## tests and limits

tests compare 2,800 routing attempts with exhaustive simple-path search and a microsecond-by-microsecond slot scan. they also cover shared-radio conflicts, expiry boundaries, propagation delay, rollback, disabled contacts, integer overflow, search budgets, and input-order determinism.

there is no bundle fragmentation, receiver-conflict model, buffer limit, retransmission, or uncertain contact time. a shared resource represents one transmitter calendar, not a complete half-duplex radio model. this is a planning simulator, not an implementation of the Bundle Protocol.

each successful reservation copies the calendars before committing. this makes rollback simple but costs time proportional to existing bookings. for larger plans, a transaction over only the affected intervals would avoid copying every calendar.

the API is [router.hpp](include/contact/router.hpp). reproduce the plot with Matplotlib and `python examples/plot.py`. enable sanitizers with `-DCONTACT_SANITIZERS=ON -DCMAKE_BUILD_TYPE=Debug`. NASA's [delay/disruption tolerant networking overview](https://www.nasa.gov/communicating-with-missions/delay-disruption-tolerant-networking/) gives the broader store-and-forward context.

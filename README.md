# contact-router

Route data through links that are available only at scheduled times. Each contact has a transmission rate, a closing time, and a propagation delay. A bundle can wait at a relay, but its entire transmission must fit inside each contact window.

The planner reserves actual time intervals. Contacts can share a transmitter, so two otherwise valid routes cannot silently use the same radio at once.

![Contact reservations](docs/reservations.png)

## Build and run

Requires C++20 and CMake 3.20+. No runtime dependencies.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 3
ctest --test-dir build --output-on-failure
./build/contact-route examples/contacts.csv examples/bundles.csv
./build/contact-route examples/contacts.csv examples/bundles.csv --bookings
```

The first command prints one result per bundle. The second prints the reserved transmission intervals. The CSV files use integer microseconds, bytes, and bytes per second. Names accept letters, digits, underscore, hyphen, and dot; quoted CSV fields are not supported.

## The example

A lunar node can send directly to Earth or through a relay. Health data has higher priority than science data. It takes the direct link and arrives at 6.3 seconds.

That direct transmission occupies the lunar transmitter for five seconds. The science bundle then cannot fit into the remainder of the first relay contact. It waits for the later contact and arrives at 44.3 seconds. The image bundle has no feasible route under the remaining reservations.

This is why counting link capacity alone is insufficient. The free time must occur when the bundle and the transmitter are both available.

## Routing rule

The library uses an earliest-arrival search. At each node it finds the first free transmission slot in every outgoing contact, adds propagation time, and keeps the best arrival at the next node. Earlier arrival is sufficient here because storage is unlimited and a bundle may always wait.

Capacity is reserved only after a complete route is found. A failed search consumes nothing. The library can release a planned route and disable a contact before replanning. Releasing a reservation is not a model of undoing a transmission that already happened.

The CLI processes bundles by release time, then descending priority, then earliest expiry and bundle ID. Each route is optimal for that bundle given existing reservations. The whole set of routes is not necessarily optimal for total throughput or fairness. The example intentionally exposes that tradeoff.

## Tests and limits

Tests compare 2,800 routing attempts with exhaustive simple-path search and a microsecond-by-microsecond slot scan. They also check shared-radio conflicts, expiry boundaries, propagation delay, rollback, disabled contacts, integer overflow, search budgets, and input-order determinism.

There is no bundle fragmentation, receiver conflict model, buffer limit, retransmission, or uncertain contact time. A shared resource represents one transmitter calendar; it is not a complete half-duplex radio model. This is a planning simulator, not an implementation of the Bundle Protocol.

Successful reservation currently copies the calendars before committing the change. That makes rollback easy to reason about but costs time proportional to existing bookings. For large plans I would replace that copy with a transaction over only the affected interval sets.

The API is [router.hpp](include/contact/router.hpp). The plot can be reproduced with Matplotlib and `python examples/plot.py`. Sanitizers are enabled with `-DCONTACT_SANITIZERS=ON -DCMAKE_BUILD_TYPE=Debug`. NASA's [delay/disruption tolerant networking overview](https://www.nasa.gov/communicating-with-missions/delay-disruption-tolerant-networking/) gives the broader store-and-forward context.

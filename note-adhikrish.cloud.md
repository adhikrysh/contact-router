# contact-router

routing in space is routing with a bus timetable. the relay is reachable from 2:00 to 2:20, the downlink opens at 3:10, two links share one radio, and your data has an expiry date. there's no "path right now", there's a calendar, and someone's already booked half of it.

this finds the earliest arrival for each bundle across that calendar and reserves the radio time, so two routes never fight over one transmitter.

```text
bundle -> stored at node -> first free slot that fits -> shared radio calendar -> arrival
```

![Contact reservations](docs/reservations.png)

## how

it's an earliest-arrival search over contacts, where each contact has open and close times, a rate, a delay, and maybe a shared radio. one label per node is enough because storage is unlimited, waiting is free and calendars fill first-fit in time order, so showing up earlier never costs you an option. add buffers or energy costs and that stops being true.

everything is integer microseconds with overflow checks on every add, because floats and deadlines don't mix. search never touches the calendars. only once a full route exists does it copy them, book every hop and swap the copies in, so a failed search, a missed deadline or an allocation failure halfway books nothing. ties are deterministic, so shuffling the input rows doesn't change the answer.

## example

in the lunar scenario, health data has priority, grabs the direct link and lands at 6.3 s. science can't fit in what's left of the first relay window, waits for the next one and lands at 44.3 s. the images get nothing.

```text
health,reserved,6300000,6300000,3
science,reserved,44300000,44300000,4;5
image,no_route,,,
```

it's greedy on purpose. each bundle takes its best route given what's already booked, which means one bundle's perfect route can ruin a later one's day. fixing that needs a joint objective, which is a different project.

## tests

2,800 reservations checked against a brute-force path search and a microsecond-by-microsecond slot scan, plus shared radios, expiry edges, release, overflow, search limits and shuffled inputs.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build
ctest --test-dir build --output-on-failure
./build/contact-route examples/contacts.csv examples/bundles.csv
```

no fragmentation, buffers, retransmission or fuzzy contact times. a planning sim, not a Bundle Protocol implementation. NASA's [DTN overview](https://www.nasa.gov/communicating-with-missions/delay-disruption-tolerant-networking/) has the bigger picture.

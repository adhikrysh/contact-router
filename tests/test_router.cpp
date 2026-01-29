#include "contact/router.hpp"

#include <algorithm>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <random>
#include <set>
#include <stdexcept>

namespace {
using namespace contact;
void require(bool condition, const char *why) {
    if (!condition)
        throw std::runtime_error(why);
}
template <class F> void rejects(F f) {
    bool threw = false;
    try {
        f();
    } catch (const std::exception &) {
        threw = true;
    }
    require(threw, "invalid input accepted");
}
std::string resource(const Contact &c) {
    return c.resource.empty() ? "link:" + std::to_string(c.id) : "shared:" + c.resource;
}

// Tiny-workload oracle: enumerate simple paths, and try every microsecond of
// every contact. It deliberately shares neither Dijkstra nor first-fit code.
std::optional<Time> exhaustive(const std::vector<Contact> &contacts,
                               const std::vector<Booking> &bookings, const Bundle &bundle) {
    std::optional<Time> best;
    std::set<std::string> visited{bundle.from};
    std::function<void(const std::string &, Time)> visit = [&](const std::string &node,
                                                               Time ready) {
        if (node == bundle.to) {
            if (!best || ready < *best)
                best = ready;
            return;
        }
        for (const auto &c : contacts) {
            if (c.from != node || visited.contains(c.to))
                continue;
            const auto duration = static_cast<Time>(
                (bundle.bytes * 1'000'000 + c.bytes_per_second - 1) / c.bytes_per_second);
            for (Time start = std::max(ready, c.opens); start + duration <= c.closes; ++start) {
                const Time finish = start + duration;
                bool conflict = false;
                for (const auto &b : bookings) {
                    if (b.resource == resource(c) && start < b.finishes && b.starts < finish)
                        conflict = true;
                }
                if (conflict)
                    continue;
                if (finish + c.propagation_delay <= bundle.expires) {
                    visited.insert(c.to);
                    visit(c.to, finish + c.propagation_delay);
                    visited.erase(c.to);
                }
                break; // Earlier arrival can always wait for a later departure.
            }
        }
    };
    visit(bundle.from, bundle.released);
    return best;
}

void validate(const Router &router, const Bundle &bundle, const Route &route) {
    Time time = bundle.released;
    std::string node = bundle.from;
    for (const auto &hop : route.hops) {
        const auto found = std::find_if(router.contacts().begin(), router.contacts().end(),
                                        [&](const Contact &c) { return c.id == hop.contact; });
        require(found != router.contacts().end(), "unknown contact in route");
        const auto &c = *found;
        require(c.from == node, "route is disconnected");
        require(hop.starts >= time && hop.starts >= c.opens && hop.finishes <= c.closes,
                "transmission outside contact");
        const auto expected = static_cast<Time>(
            (bundle.bytes * 1'000'000 + c.bytes_per_second - 1) / c.bytes_per_second);
        require(hop.finishes - hop.starts == expected, "wrong transmission duration");
        require(hop.arrives == hop.finishes + c.propagation_delay, "wrong light-time delay");
        time = hop.arrives;
        node = c.to;
    }
    require(node == bundle.to && time == route.arrives && time <= bundle.expires,
            "wrong destination or expiry");
    const auto bookings = router.bookings();
    for (std::size_t i = 0; i < bookings.size(); ++i) {
        for (std::size_t j = i + 1; j < bookings.size(); ++j) {
            const auto &a = bookings[i];
            const auto &b = bookings[j];
            if (a.resource == b.resource) {
                require(a.finishes <= b.starts || b.finishes <= a.starts,
                        "resource calendar overlaps");
            }
        }
    }
}

void generated_graphs() {
    std::mt19937_64 random(7319);
    for (int trial = 0; trial < 350; ++trial) {
        std::vector<Contact> contacts;
        for (Id id = 1; id <= 16; ++id) {
            const auto from = random() % 5;
            const auto to = (from + 1 + random() % 4) % 5;
            const Time opens = static_cast<Time>(random() % 40);
            contacts.push_back({id, "n" + std::to_string(from), "n" + std::to_string(to), opens,
                                opens + static_cast<Time>(5 + random() % 30),
                                1'000'000 * (1 + random() % 3), static_cast<Time>(random() % 6),
                                "radio" + std::to_string(random() % 4)});
        }
        Router router(contacts);
        for (int index = 0; index < 8; ++index) {
            const Bundle bundle{
                "b" + std::to_string(index), "n0", "n4", index, 80, 1 + random() % 7, 0};
            const auto before = router.bookings();
            const auto expected = exhaustive(contacts, before, bundle);
            const auto result = router.reserve(bundle);
            require(expected.has_value() == result.route.has_value(),
                    "Dijkstra disagrees with exhaustive path feasibility");
            if (result.route) {
                require(result.route->arrives == *expected,
                        "Dijkstra route is not earliest-arrival");
                validate(router, bundle, *result.route);
            } else
                require(before == router.bookings(), "failed routing consumed capacity");
        }
    }
}

void boundary_and_waiting() {
    Router router({{1, "a", "b", 10, 20, 1'000'000, 3, ""},
                   {2, "b", "c", 25, 30, 1'000'000, 2, ""},
                   {3, "a", "c", 0, 100, 1'000'000, 100, ""}});
    const Bundle first{"first", "a", "c", 0, 32, 5, 0};
    const auto result = router.reserve(first);
    require(result.route && result.route->arrives == 32, "inclusive completion/expiry boundaries");
    require(result.route->hops[0].starts == 10 && result.route->hops[1].starts == 25,
            "waiting at relay omitted");
    validate(router, first, *result.route);
    const auto old = router.bookings();
    require(router.reserve({"too-late", "a", "c", 0, 31, 5, 0}).status == Status::no_route,
            "deadline was ignored");
    require(old == router.bookings(), "failure reserved an intermediate hop");
    const auto local = router.reserve({"local", "a", "a", 7, 7, 1, 0});
    require(local.route && local.route->hops.empty() && local.route->arrives == 7,
            "local delivery failed");
}

void shared_radio_and_rollback() {
    Router router(
        {{1, "a", "b", 0, 20, 1'000'000, 0, "tx"}, {2, "a", "c", 0, 20, 1'000'000, 0, "tx"}});
    require(router.reserve({"one", "a", "b", 0, 20, 8, 0}).route->arrives == 8, "first radio slot");
    require(router.reserve({"two", "a", "c", 0, 20, 8, 0}).route->arrives == 16,
            "shared transmitter double-booked");
    rejects([&] { router.set_enabled(1, false); });
    require(router.release("one"), "release did not find reservation");
    require(!router.release("one"), "release must be idempotent");
    router.set_enabled(1, false);
    require(!router.reserve({"no-link", "a", "b", 0, 20, 1, 0}).route, "disabled link used");
    const auto hole = router.reserve({"three", "a", "c", 0, 20, 8, 0});
    require(hole.route && hole.route->arrives == 8, "released interval was not reused");
    validate(router, {"three", "a", "c", 0, 20, 8, 0}, *hole.route);
}

void overflow_and_limits() {
    require(transmission_time(1, 3) == 333334, "duration rounded down");
    require(transmission_time(1, 1'000'000'000'000ULL) == 1,
            "sub-microsecond bundle got zero duration");
    require(!transmission_time(std::numeric_limits<std::uint64_t>::max(), 1),
            "duration overflow not detected");
    const Time last = std::numeric_limits<Time>::max();
    Router router({{1, "a", "b", last - 10, last, 1'000'000, 3, ""}});
    require(!router.reserve({"overflow", "a", "b", last - 10, last, 10, 0}).route,
            "light time overflow accepted");
    Router capped({{1, "a", "b", 0, 10, 1'000'000, 0, ""}, {2, "b", "c", 0, 10, 1'000'000, 0, ""}},
                  1);
    require(capped.reserve({"x", "a", "c", 0, 20, 1, 0}).status == Status::search_limit,
            "search limit not reported");
    require(capped.bookings().empty(), "search limit consumed capacity");
    rejects([] { (void)Router({{1, "a", "b", 0, 1, 0, 0, ""}}); });
    rejects([] { (void)Router({{1, "a", "b", 0, 1, 1, 0, ""}, {1, "b", "c", 0, 1, 1, 0, ""}}); });
    rejects([&] { (void)router.reserve({"bad", "a", "b", 10, 9, 1, 0}); });
}

void deterministic_order() {
    std::vector<Contact> contacts{{3, "a", "c", 0, 30, 1'000'000, 0, ""},
                                  {1, "a", "b", 0, 30, 1'000'000, 0, ""},
                                  {2, "b", "c", 0, 30, 1'000'000, 0, ""}};
    Router first(contacts);
    std::reverse(contacts.begin(), contacts.end());
    Router second(contacts);
    for (int i = 0; i < 10; ++i) {
        const Bundle b{"b" + std::to_string(i), "a", "c", 0, 100, 4, 0};
        const auto a = first.reserve(b), c = second.reserve(b);
        require(a.status == c.status, "input row ordering changed feasibility");
        if (a.route)
            require(a.route->hops == c.route->hops, "input row ordering changed route");
    }
}
} // namespace

int main() {
    unsigned failed = 0;
    auto test = [&](const char *name, const std::function<void()> &body) {
        try {
            body();
            std::cout << "PASS " << name << '\n';
        } catch (const std::exception &e) {
            ++failed;
            std::cerr << "FAIL " << name << ": " << e.what() << '\n';
        }
    };
    test("2800 reservations against exhaustive routing", generated_graphs);
    test("contact boundaries, propagation, and waiting", boundary_and_waiting);
    test("shared transmitter, rollback, and outage", shared_radio_and_rollback);
    test("overflow, malformed inputs, and search limits", overflow_and_limits);
    test("deterministic input permutation", deterministic_order);
    return failed == 0 ? 0 : 1;
}

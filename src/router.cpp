#include "contact/router.hpp"

#include <algorithm>
#include <functional>
#include <limits>
#include <queue>
#include <stdexcept>
#include <utility>

namespace contact {
namespace {
constexpr auto maximum = std::numeric_limits<Time>::max();
std::string resource_key(const Contact &c) {
    // Separate namespaces prevent an explicit resource name from colliding with
    // the implicit calendar belonging to a contact that has no shared resource.
    return c.resource.empty() ? "link:" + std::to_string(c.id) : "shared:" + c.resource;
}
void valid_name(const std::string &name) {
    if (name.empty() || name.size() > 128)
        throw std::invalid_argument("name length must be 1..128 bytes");
    for (char raw : name) {
        const auto ch = static_cast<unsigned char>(raw);
        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
              ch == '_' || ch == '-' || ch == '.')) {
            throw std::invalid_argument(
                "names accept only letters, digits, underscore, hyphen, and dot");
        }
    }
}

std::optional<Hop> first_slot(const Contact &c, Time ready, Time duration,
                              const std::vector<Booking> &bookings) {
    Time start = std::max(ready, c.opens);
    if (start > c.closes || duration > c.closes - start)
        return std::nullopt;
    for (const auto &booked : bookings) {
        if (booked.finishes <= start)
            continue;
        if (duration <= booked.starts - start)
            break;
        start = booked.finishes;
        if (start > c.closes || duration > c.closes - start)
            return std::nullopt;
    }
    const Time finish = start + duration;
    if (c.propagation_delay > maximum - finish)
        return std::nullopt;
    return Hop{c.id, resource_key(c), start, finish, finish + c.propagation_delay};
}
} // namespace

std::optional<Time> transmission_time(std::uint64_t bytes, std::uint64_t rate) {
    if (bytes == 0 || rate == 0 || rate > 1'000'000'000'000ULL) {
        throw std::invalid_argument("positive bytes required; rate must be in (0, 10^12] bytes/s");
    }
    constexpr std::uint64_t micros = 1'000'000;
    const std::uint64_t seconds = bytes / rate;
    if (seconds > static_cast<std::uint64_t>(maximum) / micros)
        return std::nullopt;
    const std::uint64_t remainder = bytes % rate;
    // The rate limit bounds remainder*10^6 below 10^18, so this multiplication
    // cannot overflow. Dividing first avoids multiplying the full bundle size.
    const std::uint64_t fractional =
        (remainder * micros) / rate + ((remainder * micros) % rate != 0);
    const std::uint64_t result = seconds * micros + fractional;
    if (result > static_cast<std::uint64_t>(maximum))
        return std::nullopt;
    return static_cast<Time>(result);
}

Router::Router(std::vector<Contact> contacts, std::size_t search_budget)
    : contacts_(std::move(contacts)), search_budget_(search_budget) {
    if (search_budget == 0)
        throw std::invalid_argument("search budget must be positive");
    // Stable order makes equal-arrival choices independent of input row order.
    std::sort(contacts_.begin(), contacts_.end(),
              [](const Contact &a, const Contact &b) { return a.id < b.id; });
    for (std::size_t i = 0; i < contacts_.size(); ++i) {
        const auto &c = contacts_[i];
        valid_name(c.from);
        valid_name(c.to);
        if (!c.resource.empty())
            valid_name(c.resource);
        if (c.from == c.to || c.opens < 0 || c.closes <= c.opens || c.propagation_delay < 0) {
            throw std::invalid_argument("invalid contact endpoints or time interval");
        }
        (void)transmission_time(1, c.bytes_per_second);
        if (!by_id_.emplace(c.id, i).second)
            throw std::invalid_argument("duplicate contact ID");
        outgoing_[c.from].push_back(i);
        calendars_.try_emplace(resource_key(c));
    }
    enabled_.assign(contacts_.size(), true);
}

Result Router::reserve(const Bundle &bundle) {
    valid_name(bundle.id);
    valid_name(bundle.from);
    valid_name(bundle.to);
    if (bundle.released < 0 || bundle.expires < bundle.released || bundle.bytes == 0) {
        throw std::invalid_argument("invalid bundle lifetime or size");
    }
    if (routes_.contains(bundle.id))
        throw std::invalid_argument("bundle already has a reservation");
    struct Label {
        Time arrival;
        std::optional<Hop> previous;
    };
    std::map<std::string, Label> labels;
    using Entry = std::pair<Time, std::string>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<>> frontier;
    labels.emplace(bundle.from, Label{bundle.released, std::nullopt});
    frontier.emplace(bundle.released, bundle.from);
    std::size_t examined = 0;
    bool found = false;
    while (!frontier.empty()) {
        const auto [time, node] = frontier.top();
        frontier.pop();
        if (labels.at(node).arrival != time)
            continue;
        if (node == bundle.to) {
            found = true;
            break;
        }
        const auto outgoing = outgoing_.find(node);
        if (outgoing == outgoing_.end())
            continue;
        for (std::size_t index : outgoing->second) {
            if (examined == search_budget_)
                return {Status::search_limit, std::nullopt, examined};
            ++examined;
            if (!enabled_[index])
                continue;
            const auto &c = contacts_[index];
            const auto duration = transmission_time(bundle.bytes, c.bytes_per_second);
            if (!duration)
                continue;
            const auto hop = first_slot(c, time, *duration, calendars_.at(resource_key(c)));
            if (!hop || hop->arrives > bundle.expires)
                continue;
            const auto old = labels.find(c.to);
            if (old == labels.end() || hop->arrives < old->second.arrival) {
                labels.insert_or_assign(c.to, Label{hop->arrives, hop});
                frontier.emplace(hop->arrives, c.to);
            }
        }
    }
    if (!found)
        return {Status::no_route, std::nullopt, examined};
    Route route{labels.at(bundle.to).arrival, {}};
    for (std::string node = bundle.to; node != bundle.from;) {
        const Hop hop = *labels.at(node).previous;
        route.hops.push_back(hop);
        node = contacts_[by_id_.at(hop.contact)].from;
    }
    std::reverse(route.hops.begin(), route.hops.end());

    // Search never changes calendars. Copy then swap gives the whole reservation
    // a strong exception guarantee, including allocation failure on a later hop.
    // This costs O(existing bookings) per successful reservation; it is a clear
    // correctness-first tradeoff, and would need replacing for very large plans.
    auto next_calendars = calendars_;
    auto next_routes = routes_;
    for (const auto &hop : route.hops) {
        auto &calendar = next_calendars.at(hop.resource);
        calendar.push_back({hop.resource, bundle.id, hop.contact, hop.starts, hop.finishes});
        std::sort(calendar.begin(), calendar.end(),
                  [](const Booking &a, const Booking &b) { return a.starts < b.starts; });
    }
    next_routes.emplace(bundle.id, route);
    Result success{Status::reserved, route, examined};
    calendars_.swap(next_calendars);
    routes_.swap(next_routes);
    return success;
}

bool Router::release(const std::string &id) {
    if (routes_.erase(id) == 0)
        return false;
    for (auto &[resource, calendar] : calendars_) {
        (void)resource;
        std::erase_if(calendar, [&](const Booking &booking) { return booking.bundle == id; });
    }
    return true;
}

void Router::set_enabled(Id id, bool enabled) {
    const auto found = by_id_.find(id);
    if (found == by_id_.end())
        throw std::invalid_argument("unknown contact ID");
    if (!enabled) {
        for (const auto &[resource, calendar] : calendars_) {
            (void)resource;
            if (std::any_of(calendar.begin(), calendar.end(),
                            [&](const Booking &b) { return b.contact == id; })) {
                throw std::logic_error("release reservations before disabling this contact");
            }
        }
    }
    enabled_[found->second] = enabled;
}

std::vector<Booking> Router::bookings() const {
    std::vector<Booking> result;
    for (const auto &[resource, calendar] : calendars_) {
        (void)resource;
        result.insert(result.end(), calendar.begin(), calendar.end());
    }
    return result;
}
} // namespace contact

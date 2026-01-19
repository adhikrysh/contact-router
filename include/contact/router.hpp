#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace contact {
using Time = std::int64_t; // integer microseconds, nonnegative
using Id = std::uint64_t;

struct Contact {
    Id id{};
    std::string from;
    std::string to;
    Time opens{};
    Time closes{};
    std::uint64_t bytes_per_second{}; // (0, 10^12]
    Time propagation_delay{};
    // Contacts with the same nonempty resource share a transmitter calendar.
    // Empty means an independent link. Receiver constraints are not modelled.
    std::string resource;
};
struct Bundle {
    std::string id;
    std::string from;
    std::string to;
    Time released{};
    Time expires{};        // final arrival at this time is allowed
    std::uint64_t bytes{}; // positive; bundles are not fragmented
    unsigned priority{};
};
struct Hop {
    Id contact{};
    std::string resource;
    Time starts{};
    Time finishes{}; // transmission completion, must be <= contact.closes
    Time arrives{};  // finishes + propagation delay
    bool operator==(const Hop &) const = default;
};
struct Route {
    Time arrives{};
    std::vector<Hop> hops;
};
enum class Status { reserved, no_route, search_limit };
struct Result {
    Status status;
    std::optional<Route> route;
    std::size_t contacts_examined{};
};
struct Booking {
    std::string resource;
    std::string bundle;
    Id contact{};
    Time starts{};
    Time finishes{};
    bool operator==(const Booking &) const = default;
};

// ceil(bytes / rate) in microseconds, without floating-point rounding.
// nullopt means the duration is outside the representable time domain.
std::optional<Time> transmission_time(std::uint64_t bytes, std::uint64_t rate);

// Single-threaded reservation planner. Waiting/storage at nodes is unlimited.
// Each route is earliest-arrival for this bundle and the existing reservations;
// priority ordering across bundles is a caller policy, not a global optimum.
class Router {
  public:
    explicit Router(std::vector<Contact> contacts, std::size_t search_budget = 1'000'000);
    // The calendar changes only after a complete route is found. On failure or
    // budget exhaustion no capacity is consumed, including on intermediate hops.
    Result reserve(const Bundle &bundle);
    // Planning rollback only: the caller must not release already executed work.
    bool release(const std::string &bundle);
    // Refuses to disable a contact with reservations. Release affected plans
    // first, disable it, and route those bundles again from their actual source.
    void set_enabled(Id contact, bool enabled);
    [[nodiscard]] std::vector<Booking> bookings() const;
    [[nodiscard]] const std::vector<Contact> &contacts() const {
        return contacts_;
    }

  private:
    std::vector<Contact> contacts_;
    std::map<std::string, std::vector<std::size_t>> outgoing_;
    std::map<Id, std::size_t> by_id_;
    std::vector<bool> enabled_;
    std::map<std::string, std::vector<Booking>> calendars_;
    std::map<std::string, Route> routes_;
    std::size_t search_budget_;
};
} // namespace contact

#include "contact/router.hpp"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

namespace {
template <class T> T integer(const std::string &text) {
    T result{};
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), result);
    if (error != std::errc{} || end != text.data() + text.size())
        throw std::invalid_argument("invalid integer: " + text);
    return result;
}
std::vector<std::vector<std::string>> read_csv(const char *path, const std::string &expected,
                                               std::size_t columns) {
    std::ifstream file(path);
    if (!file)
        throw std::runtime_error(std::string("cannot open ") + path);
    std::string line;
    if (!std::getline(file, line))
        throw std::invalid_argument("missing CSV header");
    if (!line.empty() && line.back() == '\r')
        line.pop_back();
    if (line != expected)
        throw std::invalid_argument("expected CSV header: " + expected);
    std::vector<std::vector<std::string>> rows;
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty() || line.front() == '#')
            continue;
        std::vector<std::string> fields;
        std::size_t begin = 0;
        while (true) {
            const auto end = line.find(',', begin);
            fields.push_back(line.substr(begin, end == std::string::npos ? end : end - begin));
            if (end == std::string::npos)
                break;
            begin = end + 1;
        }
        if (fields.size() != columns)
            throw std::invalid_argument("wrong number of CSV columns");
        rows.push_back(std::move(fields));
        if (rows.size() > 1'000'000)
            throw std::invalid_argument("CSV row limit exceeded");
    }
    if (file.bad())
        throw std::runtime_error("failed reading CSV");
    return rows;
}
} // namespace

int main(int argc, char **argv) {
    using namespace contact;
    if (argc == 2 && std::string(argv[1]) == "--help") {
        std::cout << "Usage: contact-route CONTACTS.csv BUNDLES.csv [--bookings]\n"
                     "Integer microseconds and bytes. Reservations are made by release time,\n"
                     "then descending priority, then earliest expiry, then bundle ID.\n"
                     "Quoted CSV fields are not supported. See examples/ for column names.\n";
        return 0;
    }
    if (argc != 3 && !(argc == 4 && std::string(argv[3]) == "--bookings")) {
        std::cerr << "Usage: contact-route CONTACTS.csv BUNDLES.csv [--bookings]\n";
        return 2;
    }
    try {
        std::vector<Contact> contacts;
        for (const auto &row : read_csv(
                 argv[1], "id,from,to,opens_us,closes_us,bytes_per_second,delay_us,resource", 8)) {
            contacts.push_back({integer<Id>(row[0]), row[1], row[2], integer<Time>(row[3]),
                                integer<Time>(row[4]), integer<std::uint64_t>(row[5]),
                                integer<Time>(row[6]), row[7]});
        }
        std::vector<Bundle> bundles;
        for (const auto &row :
             read_csv(argv[2], "id,from,to,released_us,expires_us,bytes,priority", 7)) {
            bundles.push_back({row[0], row[1], row[2], integer<Time>(row[3]), integer<Time>(row[4]),
                               integer<std::uint64_t>(row[5]), integer<unsigned>(row[6])});
        }
        std::sort(bundles.begin(), bundles.end(), [](const Bundle &a, const Bundle &b) {
            if (a.released != b.released)
                return a.released < b.released;
            if (a.priority != b.priority)
                return a.priority > b.priority;
            return std::tie(a.expires, a.id) < std::tie(b.expires, b.id);
        });
        Router router(std::move(contacts));
        // Buffer output so malformed later input cannot masquerade as a complete
        // successful run with only a prefix of the requested bundles processed.
        std::ostringstream output;
        output << "bundle,status,arrival_us,latency_us,contacts\n";
        std::map<std::string, bool> seen;
        for (const auto &bundle : bundles) {
            if (!seen.emplace(bundle.id, true).second)
                throw std::invalid_argument("duplicate bundle ID");
            const auto result = router.reserve(bundle);
            output << bundle.id << ',';
            if (!result.route)
                output << (result.status == Status::search_limit ? "search_limit" : "no_route")
                       << ",,,\n";
            else {
                output << "reserved," << result.route->arrives << ','
                       << result.route->arrives - bundle.released << ',';
                bool first = true;
                for (const auto &hop : result.route->hops) {
                    if (!first)
                        output << ';';
                    output << hop.contact;
                    first = false;
                }
                output << '\n';
            }
        }
        if (argc == 4) {
            std::cout << "resource,bundle,contact,starts_us,finishes_us\n";
            for (const auto &booking : router.bookings()) {
                std::cout << booking.resource << ',' << booking.bundle << ',' << booking.contact
                          << ',' << booking.starts << ',' << booking.finishes << '\n';
            }
        } else {
            std::cout << output.str();
        }
        if (!std::cout)
            throw std::runtime_error("failed writing output");
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "contact-route: " << e.what() << '\n';
        return 1;
    }
}

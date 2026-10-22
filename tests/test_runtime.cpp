#include "threadpool.h"
#include "resourcebudget.h"
#include <atomic>
#include <array>
#include <chrono>
#include <iostream>
#include <memory>
#include <string>
using namespace imagebatch;
using namespace std::chrono_literals;
static void require(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
int main() {
    const std::vector<std::pair<std::string, std::function<void()>>> tests = {
        {"invalid constructor arguments", [] {
            bool invalidWorkers = false, invalidQueue = false, invalidBudget = false;
            try { ThreadPool pool(0, 1); } catch (const std::invalid_argument &) { invalidWorkers = true; }
            try { ThreadPool pool(1, 0); } catch (const std::invalid_argument &) { invalidQueue = true; }
            try { ResourceBudget budget(0); } catch (const std::invalid_argument &) { invalidBudget = true; }
            require(invalidWorkers && invalidQueue && invalidBudget, "Invalid settings were accepted");
        }},
        {"future results, move-only jobs and exception transport", [] {
            ThreadPool pool(2, 2);
            auto value = pool.submit([p = std::make_unique<int>(42)] { return *p; });
            auto failure = pool.submit([]() -> int { throw std::runtime_error("expected"); });
            require(value.get() == 42, "Move-only job lost its value");
            bool caught = false;
            try { failure.get(); } catch (const std::runtime_error &) { caught = true; }
            require(caught && pool.submit([] { return 7; }).get() == 7, "Job exception killed a worker");
        }},
        {"four workers really overlap", [] {
            ThreadPool pool(4, 4);
            std::promise<void> gate, allStarted;
            auto ready = gate.get_future().share(); auto started = allStarted.get_future();
            std::atomic_int count{0}; std::vector<std::future<void>> jobs;
            for (int i = 0; i < 4; ++i) jobs.push_back(pool.submit([&] {
                if (count.fetch_add(1) + 1 == 4) allStarted.set_value();
                ready.wait();
            }));
            const bool overlap = started.wait_for(2s) == std::future_status::ready;
            gate.set_value(); for (auto &job : jobs) job.get();
            require(overlap && pool.stats().peakActive == 4, "Jobs were not concurrent");
        }},
        {"bounded queue applies producer backpressure", [] {
            ThreadPool pool(1, 1); std::promise<void> gate, started;
            auto ready = gate.get_future().share(); auto active = started.get_future();
            auto first = pool.submit([&] { started.set_value(); ready.wait(); });
            active.wait(); auto second = pool.submit([] { return 2; });
            auto producer = std::async(std::launch::async, [&] { return pool.submit([] { return 3; }); });
            const bool blocked = producer.wait_for(40ms) == std::future_status::timeout;
            gate.set_value(); first.get();
            require(blocked && second.get() == 2 && producer.get().get() == 3, "Queue failed to block the producer");
            require(pool.stats().peakQueued == 1, "Queue capacity was exceeded");
        }},
        {"draining destructor joins accepted jobs", [] {
            std::atomic_int completed{0};
            { ThreadPool pool(3, 2); for (int i = 0; i < 50; ++i) pool.submit([&] { ++completed; }); }
            require(completed == 50, "Destructor dropped work");
        }},
        {"multiple producers, exactly-once completion", [] {
            ThreadPool pool(4, 3); std::atomic_int total{0}; std::vector<std::thread> producers;
            std::array<std::atomic_int, 1000> visits;
            for (auto &visit : visits) visit.store(0);
            for (int p = 0; p < 4; ++p) producers.emplace_back([&, p] {
                std::vector<std::future<void>> jobs;
                for (int i = 0; i < 250; ++i) {
                    const int id = p * 250 + i;
                    jobs.push_back(pool.submit([&, id] { ++visits[id]; ++total; }));
                }
                for (auto &job : jobs) job.get();
            });
            for (auto &producer : producers) producer.join();
            require(total == 1000 && pool.stats().peakQueued <= 3 && pool.stats().peakActive <= 4, "Concurrency invariant failed");
            for (const auto &visit : visits) require(visit.load() == 1, "A job was lost or executed twice");
        }},
        {"budget waits, releases and cancels", [] {
            ResourceBudget budget(100); std::atomic_bool cancelled{false};
            auto first = budget.acquire(80, cancelled);
            auto waiter = std::async(std::launch::async, [&] { return budget.acquire(50, cancelled).has_value(); });
            const bool waiting = waiter.wait_for(40ms) == std::future_status::timeout;
            cancelled.store(true);
            require(waiter.wait_for(1s) == std::future_status::ready && !waiter.get(), "Budget waiter ignored cancellation");
            first.reset(); cancelled.store(false);
            auto full = budget.acquire(100, cancelled);
            require(waiting && full.has_value() && budget.peakReserved() == 100, "Budget reservation leaked");
        }},
        {"RAII lease moves and exception safety", [] {
            ResourceBudget budget(100); std::atomic_bool cancelled{false};
            auto first = budget.acquire(40, cancelled); auto second = budget.acquire(60, cancelled);
            second = std::move(first); second.reset();
            try { auto full = budget.acquire(100, cancelled); throw std::runtime_error("release on unwind"); }
            catch (const std::runtime_error &) {}
            require(budget.acquire(100, cancelled).has_value(), "Moved or unwound lease leaked capacity");
            bool rejected = false;
            try { budget.acquire(101, cancelled); } catch (const std::invalid_argument &) { rejected = true; }
            require(rejected, "Over-budget reservation was accepted");
        }}
    };
    int failures = 0;
    for (const auto &test : tests) {
        try { test.second(); std::cout << "PASS " << test.first << '\n'; }
        catch (const std::exception &e) { ++failures; std::cerr << "FAIL " << test.first << ": " << e.what() << '\n'; }
    }
    std::cout << tests.size() - failures << " passed, " << failures << " failed\n";
    return failures ? 1 : 0;
}

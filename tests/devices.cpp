#include <iiCameraIntegration/Devices.h>

#include <atomic>
#include <barrier>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <type_traits>

using namespace iiCameraIntegration;

namespace {
#define CHECK(expression) do { if (!(expression)) throw std::runtime_error( \
    std::string("Check failed: ") + #expression + " at line " + std::to_string(__LINE__)); } while (false)

template<class T> void expectError(const Result<T>& result, ErrorCode code) {
    CHECK(!result);
    CHECK(result.error().code == code);
    CHECK(!result.error().message.empty());
}
template<class T> void expectFailure(const Result<T>& result) {
    CHECK(!result);
    CHECK(!result.error().message.empty());
}

CameraInfo camera(std::string id = "fixture:usb-camera") {
    CameraInfo info;
    info.id = std::move(id);
    info.name = "Artificial USB camera";
    info.backend = "test-provider";
    info.transport = Transport::Usb;
    info.additional["test-provider.serial"] = "fixture-only";
    ControlCapability zoom;
    zoom.id = ControlId::ZoomFactor;
    zoom.readable = zoom.writable = true;
    zoom.range = NumericRange{1, 8, 0, {}};
    info.controls.push_back(zoom);
    return info;
}

// These callbacks model ownership and operation order only. They do not open
// hardware, authenticate a camera, or establish an operating-system device bond.
struct Fixture {
    CameraInfo device{camera()};
    std::atomic<PairingState> state{PairingState::Ready};
    std::atomic<double> zoom{2};
    std::atomic<int> statusCalls{}, controlCalls{}, queryCalls{}, applyCalls{}, releases{};
    std::atomic<int> active{}, peak{};
    std::atomic<int> releaseFailures{};
    bool throwStatus{}, throwControls{}, throwQuery{}, throwApply{}, throwRelease{};
    std::mutex eventsMutex;
    std::vector<std::string> events;

    void record(std::string value) {
        std::lock_guard lock(eventsMutex);
        events.push_back(std::move(value));
    }
    std::vector<std::string> recorded() {
        std::lock_guard lock(eventsMutex);
        return events;
    }
    struct Call {
        Fixture& fixture;
        explicit Call(Fixture& value) : fixture(value) {
            const int entered = ++fixture.active;
            int old = fixture.peak.load();
            while (entered > old && !fixture.peak.compare_exchange_weak(old, entered)) {}
            std::this_thread::yield();
        }
        ~Call() { --fixture.active; }
    };
};

PairingCallbacks callbacks(const std::shared_ptr<Fixture>& fixture) {
    return {
        [fixture]() -> Result<PairingState> {
            Fixture::Call call(*fixture);
            ++fixture->statusCalls;
            fixture->record("status");
            if (fixture->throwStatus) throw std::runtime_error("fixture status exception");
            return fixture->state.load();
        },
        [fixture]() -> Result<ControlPort> {
            Fixture::Call call(*fixture);
            ++fixture->controlCalls;
            fixture->record("controls");
            if (fixture->throwControls) throw std::runtime_error("fixture controls exception");
            return ControlPort(fixture->device.controls, {
                [fixture](ControlId) -> Result<ControlValue> {
                    Fixture::Call query(*fixture);
                    ++fixture->queryCalls;
                    fixture->record("query");
                    if (fixture->throwQuery) throw std::runtime_error("fixture query exception");
                    return ControlValue{fixture->zoom.load()};
                },
                [fixture](const ControlCommand& command) -> Result<ControlValue> {
                    Fixture::Call apply(*fixture);
                    ++fixture->applyCalls;
                    fixture->record("apply");
                    if (fixture->throwApply) throw std::runtime_error("fixture apply exception");
                    fixture->zoom = std::get<double>(command.value);
                    return ControlValue{fixture->zoom.load()};
                }
            });
        },
        [fixture]() -> Result<void> {
            Fixture::Call call(*fixture);
            ++fixture->releases;
            fixture->record("unpair");
            if (fixture->throwRelease) throw std::runtime_error("fixture release exception");
            if (fixture->releaseFailures.load() > 0) {
                --fixture->releaseFailures;
                return std::unexpected(Error{ErrorCode::Busy, "Fixture still owns camera input"});
            }
            fixture->state = PairingState::Unpaired;
            return {};
        }
    };
}

Result<CameraPairing> pairing(const std::shared_ptr<Fixture>& fixture,
                            PairingMethod method = PairingMethod::Session) {
    PairingInfo info;
    info.device = fixture->device;
    info.method = method;
    info.additional["test-provider.session"] = "prepared-fixture";
    return CameraPairing::create(std::move(info), callbacks(fixture));
}

void discovery() {
    int discovered = 0, paired = 0;
    auto fixture = std::make_shared<Fixture>();
    CameraDeviceSystem system({
        [&]() -> Result<std::vector<CameraInfo>> {
            ++discovered;
            return std::vector{fixture->device, camera("fixture:network-camera")};
        },
        [&](const std::string&, const PairingOptions&) -> Result<CameraPairing> {
            ++paired;
            return pairing(fixture);
        }
    });
    auto first = system.discover();
    CHECK(first && first->size() == 2);
    CHECK(first->at(0).id == fixture->device.id);
    CHECK(discovered == 1 && paired == 0 && fixture->releases == 0);
    first->at(0).additional.clear();
    auto second = system.discover();
    CHECK(second && second->at(0).additional.at("test-provider.serial") == "fixture-only");
    CHECK(discovered == 2 && paired == 0);
    CameraDeviceSystem empty({[]() -> Result<std::vector<CameraInfo>> { return std::vector<CameraInfo>{}; }, {}});
    CHECK(empty.discover() && empty.discover()->empty());
    CameraDeviceSystem duplicate({[&]() -> Result<std::vector<CameraInfo>> {
        return std::vector{fixture->device, fixture->device};
    }, {}});
    expectFailure(duplicate.discover());
    auto malformed = camera();
    malformed.name.clear();
    CameraDeviceSystem invalid({[&]() -> Result<std::vector<CameraInfo>> { return std::vector{malformed}; }, {}});
    expectFailure(invalid.discover());
    CameraDeviceSystem absent({});
    expectError(absent.discover(), ErrorCode::Unavailable);
}

void pairDevice() {
    auto fixture = std::make_shared<Fixture>();
    std::vector<std::string> order;
    int paired = 0;
    CameraDeviceSystem system({
        [&]() -> Result<std::vector<CameraInfo>> {
            order.push_back("discover");
            return std::vector{fixture->device};
        },
        [&](const std::string& id, const PairingOptions& options) -> Result<CameraPairing> {
            order.push_back("pair");
            ++paired;
            CHECK(id == fixture->device.id);
            CHECK(options.method == PairingMethod::Session);
            CHECK(options.parameters.at("test-provider.token") == "fixture-secret");
            return pairing(fixture);
        }
    });
    PairingOptions options;
    options.parameters["test-provider.token"] = "fixture-secret";
    auto session = system.pair(fixture->device.id, options);
    CHECK(session && paired == 1);
    CHECK(order == std::vector<std::string>({"discover", "pair"}));
    CHECK(session->status() && *session->status() == PairingState::Ready);
    auto snapshot = session->info();
    CHECK(snapshot && snapshot->device.id == fixture->device.id);
    CHECK(snapshot->method == PairingMethod::Session);
    CHECK(snapshot->additional.at("test-provider.session") == "prepared-fixture");
    CHECK(!snapshot->additional.contains("test-provider.token"));
    snapshot->device.name = "Caller mutation";
    CHECK(session->info()->device.name == "Artificial USB camera");
    expectError(system.pair("fixture:unknown", options), ErrorCode::NotFound);
    CHECK(paired == 1);
    fixture->device.connected = false;
    expectError(system.pair(fixture->device.id, options), ErrorCode::NotFound);
    CHECK(paired == 1);
    CHECK(session->unpair());
}

void lifecycle() {
    static_assert(!std::is_copy_constructible_v<CameraPairing>);
    static_assert(!std::is_copy_assignable_v<CameraPairing>);
    static_assert(std::is_nothrow_move_constructible_v<CameraPairing>);
    static_assert(std::is_nothrow_move_assignable_v<CameraPairing>);
    auto fixture = std::make_shared<Fixture>();
    std::optional<ControlPort> escaped;
    {
        auto result = pairing(fixture);
        CHECK(result);
        CameraPairing original = std::move(*result);
        escaped = *original.controls();
        CameraPairing moved = std::move(original);
        expectError(original.info(), ErrorCode::InvalidArgument);
        expectError(original.status(), ErrorCode::InvalidArgument);
        expectError(original.controls(), ErrorCode::InvalidArgument);
        expectError(original.unpair(), ErrorCode::InvalidArgument);
        CHECK(moved.info()->device.id == fixture->device.id);
        CHECK(moved.unpair());
        CHECK(moved.unpair());
        CHECK(moved.status() && *moved.status() == PairingState::Unpaired);
        CHECK(fixture->releases == 1);
        expectError(escaped->query(ControlId::ZoomFactor), ErrorCode::Cancelled);
        expectError(escaped->apply({ControlId::ZoomFactor, 3.0}), ErrorCode::Cancelled);
    }
    CHECK(fixture->releases == 1);
    auto source = std::make_shared<Fixture>();
    auto destination = std::make_shared<Fixture>();
    {
        auto a = pairing(source);
        auto b = pairing(destination);
        CHECK(a && b);
        auto previousPort = b->controls();
        CHECK(previousPort);
        *b = std::move(*a);
        CHECK(destination->releases == 1);
        expectError(previousPort->query(ControlId::ZoomFactor), ErrorCode::Cancelled);
        CHECK(b->info()->device.id == source->device.id);
        escaped = *b->controls();
    }
    CHECK(source->releases == 1 && destination->releases == 1);
    expectError(escaped->query(ControlId::ZoomFactor), ErrorCode::Cancelled);
    auto weakFixture = std::weak_ptr(source);
    source.reset();
    CHECK(weakFixture.expired()); // Escaped port does not retain the provider.
}

void controls() {
    auto fixture = std::make_shared<Fixture>();
    auto session = pairing(fixture);
    CHECK(session);
    auto port = session->controls();
    CHECK(port);
    const int beforeQueryStatus = fixture->statusCalls;
    auto value = port->query(ControlId::ZoomFactor);
    CHECK(value && std::get<double>(*value) == 2);
    CHECK(fixture->statusCalls == beforeQueryStatus + 1 && fixture->queryCalls == 1);
    auto applied = port->apply({ControlId::ZoomFactor, 3.5});
    CHECK(applied && std::get<double>(*applied) == 3.5 && fixture->zoom == 3.5);
    CHECK(fixture->applyCalls == 1);
    expectError(port->apply({ControlId::ZoomFactor, 9.0}), ErrorCode::InvalidArgument);
    CHECK(fixture->applyCalls == 1);
    expectError(port->query(ControlId::Lens), ErrorCode::Unsupported);
    CHECK(fixture->queryCalls == 1);
    auto events = fixture->recorded();
    CHECK(events.size() >= 6);
    CHECK(events[events.size() - 2] == "status" && events.back() == "apply");
    fixture->state = PairingState::Disconnected;
    auto status = session->status();
    CHECK(status && *status == PairingState::Disconnected);
    expectError(port->query(ControlId::ZoomFactor), ErrorCode::NotFound);
    expectError(session->controls(), ErrorCode::NotFound);
    CHECK(fixture->queryCalls == 1);
    fixture->state = PairingState::Ready;
    CHECK(*session->status() == PairingState::Disconnected); // Disconnection latches.
    expectError(port->apply({ControlId::ZoomFactor, 4.0}), ErrorCode::NotFound);
    CHECK(fixture->applyCalls == 1);
    CHECK(session->unpair());
    expectError(port->query(ControlId::ZoomFactor), ErrorCode::Cancelled);
}

void failures() {
    CameraDeviceSystem throwingDiscovery({[]() -> Result<std::vector<CameraInfo>> {
        throw std::runtime_error("fixture discovery exception");
    }, {}});
    expectError(throwingDiscovery.discover(), ErrorCode::BackendFailure);
    auto fixture = std::make_shared<Fixture>();
    CameraDeviceSystem denied({[fixture]() -> Result<std::vector<CameraInfo>> {
        return std::vector{fixture->device};
    }, [](const std::string&, const PairingOptions&) -> Result<CameraPairing> {
        return std::unexpected(Error{ErrorCode::PermissionDenied, "Fixture permission denied", 71});
    }});
    auto permission = denied.pair(fixture->device.id);
    expectError(permission, ErrorCode::PermissionDenied);
    CHECK(permission.error().nativeCode == 71);
    CameraDeviceSystem throwingPair({[fixture]() -> Result<std::vector<CameraInfo>> {
        return std::vector{fixture->device};
    }, [](const std::string&, const PairingOptions&) -> Result<CameraPairing> {
        throw std::runtime_error("fixture pairing exception");
    }});
    expectError(throwingPair.pair(fixture->device.id), ErrorCode::BackendFailure);
    auto session = pairing(fixture);
    CHECK(session);
    fixture->throwStatus = true;
    expectError(session->status(), ErrorCode::BackendFailure);
    fixture->throwStatus = false;
    fixture->throwControls = true;
    expectError(session->controls(), ErrorCode::BackendFailure);
    fixture->throwControls = false;
    auto port = session->controls();
    CHECK(port);
    fixture->throwQuery = true;
    expectError(port->query(ControlId::ZoomFactor), ErrorCode::BackendFailure);
    fixture->throwQuery = false;
    fixture->throwApply = true;
    expectError(port->apply({ControlId::ZoomFactor, 3.0}), ErrorCode::BackendFailure);
    fixture->throwApply = false;
    fixture->releaseFailures = 1;
    expectError(session->unpair(), ErrorCode::Busy);
    CHECK(fixture->releases == 1);
    CHECK(*session->status() == PairingState::Ready);
    CHECK(port->query(ControlId::ZoomFactor)); // Failed release retains ownership.
    CHECK(session->unpair());
    CHECK(fixture->releases == 2);
    expectError(port->query(ControlId::ZoomFactor), ErrorCode::Cancelled);
    auto retryOnDestruction = std::make_shared<Fixture>();
    {
        auto owned = pairing(retryOnDestruction);
        CHECK(owned);
        retryOnDestruction->releaseFailures = 1;
        expectError(owned->unpair(), ErrorCode::Busy);
    }
    CHECK(retryOnDestruction->releases == 2);
    CHECK(retryOnDestruction->state == PairingState::Unpaired);
    auto releaseThrows = std::make_shared<Fixture>();
    std::optional<ControlPort> revoked;
    {
        auto owned = pairing(releaseThrows);
        CHECK(owned);
        revoked = *owned->controls();
        releaseThrows->throwRelease = true;
        expectError(owned->unpair(), ErrorCode::BackendFailure);
    }
    CHECK(releaseThrows->releases == 2); // Destructor catches its final exception.
    expectError(revoked->query(ControlId::ZoomFactor), ErrorCode::Cancelled);
    auto failedDestination = std::make_shared<Fixture>();
    auto replacement = std::make_shared<Fixture>();
    {
        auto destination = pairing(failedDestination);
        auto source = pairing(replacement);
        CHECK(destination && source);
        revoked = *destination->controls();
        failedDestination->throwRelease = true;
        *destination = std::move(*source);
        CHECK(failedDestination->releases == 1);
        expectError(revoked->query(ControlId::ZoomFactor), ErrorCode::Cancelled);
        CHECK(destination->status() && *destination->status() == PairingState::Ready);
    }
    CHECK(replacement->releases == 1);
}

void concurrency() {
    auto fixture = std::make_shared<Fixture>();
    auto session = pairing(fixture);
    CHECK(session);
    auto port = session->controls();
    CHECK(port);
    constexpr int workers = 8, iterations = 64;
    std::barrier start(workers);
    std::atomic<int> errors{};
    std::vector<std::thread> threads;
    for (int worker = 0; worker < workers; ++worker) {
        threads.emplace_back([&, worker] {
            start.arrive_and_wait();
            for (int index = 0; index < iterations; ++index) {
                if (worker % 2) {
                    if (!port->apply({ControlId::ZoomFactor, 2.0})) ++errors;
                } else if (!port->query(ControlId::ZoomFactor)) ++errors;
            }
        });
    }
    for (auto& thread : threads) thread.join();
    CHECK(errors == 0);
    CHECK(fixture->queryCalls == workers / 2 * iterations);
    CHECK(fixture->applyCalls == workers / 2 * iterations);
    CHECK(fixture->peak == 1); // Status and actual controls share the session gate.
    CHECK(session->unpair());

    // Pause an actual provider query while unpair races against that operation.
    // All fixture waits are bounded; no assertion can abandon a live worker.
    struct Race {
        std::mutex mutex;
        std::condition_variable changed;
        bool queryEntered{}, allowQueryExit{}, queryExited{}, queryTimedOut{};
        bool unpairStarted{}, releaseEntered{}, releasedBeforeQueryExit{};
        int queries{};
        std::vector<std::string> events;
    };
    auto race = std::make_shared<Race>();
    auto racingFixture = std::make_shared<Fixture>();
    auto racingCallbacks = callbacks(racingFixture);
    racingCallbacks.controls = [race, racingFixture]() -> Result<ControlPort> {
        return ControlPort(racingFixture->device.controls, {
            [race](ControlId) -> Result<ControlValue> {
                std::unique_lock lock(race->mutex);
                ++race->queries;
                race->queryEntered = true;
                race->changed.notify_all();
                if (!race->changed.wait_for(lock, std::chrono::seconds(3),
                        [&] { return race->allowQueryExit; })) {
                    race->queryTimedOut = true;
                }
                race->queryExited = true;
                race->events.push_back("queryexit");
                race->changed.notify_all();
                return ControlValue{2.0};
            }, {}
        });
    };
    racingCallbacks.unpair = [race, racingFixture]() -> Result<void> {
        std::lock_guard lock(race->mutex);
        race->releaseEntered = true;
        race->releasedBeforeQueryExit = !race->queryExited;
        race->events.push_back("unpair");
        racingFixture->state = PairingState::Unpaired;
        ++racingFixture->releases;
        race->changed.notify_all();
        return {};
    };
    auto racingSession = CameraPairing::create(
        PairingInfo{racingFixture->device, PairingMethod::Session, {}},
        std::move(racingCallbacks));
    CHECK(racingSession);
    auto escaped = racingSession->controls();
    CHECK(escaped);
    std::atomic<bool> querySucceeded{}, releaseSucceeded{};
    std::jthread queryWorker([&] {
        querySucceeded = escaped->query(ControlId::ZoomFactor).has_value();
    });
    bool observedQuery = false;
    {
        std::unique_lock lock(race->mutex);
        observedQuery = race->changed.wait_for(lock, std::chrono::seconds(1),
            [&] { return race->queryEntered; });
    }
    std::jthread releaseWorker([&] {
        {
            std::lock_guard lock(race->mutex);
            race->unpairStarted = true;
            race->changed.notify_all();
        }
        releaseSucceeded = racingSession->unpair().has_value();
    });
    bool observedUnpair = false, enteredTooEarly = false;
    {
        std::unique_lock lock(race->mutex);
        observedUnpair = race->changed.wait_for(lock, std::chrono::seconds(1),
            [&] { return race->unpairStarted; });
        enteredTooEarly = race->changed.wait_for(lock, std::chrono::milliseconds(100),
            [&] { return race->releaseEntered; });
        race->allowQueryExit = true;
        race->changed.notify_all();
    }
    queryWorker.join();
    releaseWorker.join();
    CHECK(observedQuery && observedUnpair);
    CHECK(!enteredTooEarly && !race->releasedBeforeQueryExit && !race->queryTimedOut);
    CHECK(querySucceeded && releaseSucceeded);
    CHECK(race->events == std::vector<std::string>({"queryexit", "unpair"}));
    CHECK(racingFixture->releases == 1 && race->queries == 1);
    expectError(escaped->query(ControlId::ZoomFactor), ErrorCode::Cancelled);
    CHECK(race->queries == 1); // Revoked ports cannot reenter the provider.
}

void validation() {
    int discoveries = 0, providerPairs = 0;
    auto fixture = std::make_shared<Fixture>();
    CameraDeviceSystem system({[&]() -> Result<std::vector<CameraInfo>> {
        ++discoveries;
        return std::vector{fixture->device};
    }, [&](const std::string&, const PairingOptions&) -> Result<CameraPairing> {
        ++providerPairs;
        return pairing(fixture);
    }});
    for (const auto& invalid : std::vector<std::string>{"", std::string("a\0b", 3),
             std::string("\xc0\xaf", 2), std::string("\xed\xa0\x80", 3), std::string("\xf4\x90\x80\x80", 4)}) {
        expectError(system.pair(invalid), ErrorCode::InvalidArgument);
    }
    PairingOptions invalidMethod;
    invalidMethod.method = static_cast<PairingMethod>(-1);
    expectError(system.pair(fixture->device.id, invalidMethod), ErrorCode::InvalidArgument);
    CHECK(discoveries == 0 && providerPairs == 0);
    PairingInfo bad;
    bad.device = camera();
    auto missing = callbacks(fixture);
    missing.status = {};
    expectError(CameraPairing::create(bad, std::move(missing)), ErrorCode::InvalidArgument);
    bad.method = static_cast<PairingMethod>(99);
    expectError(CameraPairing::create(bad, callbacks(fixture)), ErrorCode::InvalidArgument);
    bad.method = PairingMethod::Session;
    bad.device.id.clear();
    expectError(CameraPairing::create(bad, callbacks(fixture)), ErrorCode::InvalidArgument);
    auto wrongDevice = std::make_shared<Fixture>();
    wrongDevice->device.id = "fixture:wrong-device";
    CameraDeviceSystem mismatch({[fixture]() -> Result<std::vector<CameraInfo>> {
        return std::vector{fixture->device};
    }, [wrongDevice](const std::string&, const PairingOptions&) -> Result<CameraPairing> {
        return pairing(wrongDevice);
    }});
    expectError(mismatch.pair(fixture->device.id), ErrorCode::BackendFailure);
    CHECK(wrongDevice->releases == 1); // Invalid provider ownership still releases.
    auto wrongMethod = std::make_shared<Fixture>();
    CameraDeviceSystem wrong({[wrongMethod]() -> Result<std::vector<CameraInfo>> {
        return std::vector{wrongMethod->device};
    }, [wrongMethod](const std::string&, const PairingOptions&) -> Result<CameraPairing> {
        return pairing(wrongMethod, PairingMethod::DeviceBond);
    }});
    expectError(wrong.pair(wrongMethod->device.id), ErrorCode::BackendFailure);
    CHECK(wrongMethod->releases == 1);
    auto unready = std::make_shared<Fixture>();
    unready->state = PairingState::Disconnected;
    CameraDeviceSystem notReady({[unready]() -> Result<std::vector<CameraInfo>> {
        return std::vector{unready->device};
    }, [unready](const std::string&, const PairingOptions&) -> Result<CameraPairing> {
        return pairing(unready);
    }});
    expectFailure(notReady.pair(unready->device.id));
    CHECK(unready->releases == 1);
    auto bonded = std::make_shared<Fixture>();
    CameraDeviceSystem authenticatingProvider({[bonded]() -> Result<std::vector<CameraInfo>> {
        return std::vector{bonded->device};
    }, [bonded](const std::string&, const PairingOptions& options) -> Result<CameraPairing> {
        CHECK(options.method == PairingMethod::DeviceBond);
        return pairing(bonded, options.method);
    }});
    PairingOptions bond;
    bond.method = PairingMethod::DeviceBond;
    auto authenticatedFixture = authenticatingProvider.pair(bonded->device.id, bond);
    CHECK(authenticatedFixture && authenticatedFixture->info()->method == PairingMethod::DeviceBond);
    CHECK(authenticatedFixture->unpair());
    auto unicode = std::make_shared<Fixture>();
    unicode->device.id = "fixture:\xec\xb9\xb4\xeb\xa9\x94\xeb\x9d\xbc";
    CameraDeviceSystem international({[unicode]() -> Result<std::vector<CameraInfo>> {
        return std::vector{unicode->device};
    }, [unicode](const std::string&, const PairingOptions&) -> Result<CameraPairing> {
        return pairing(unicode);
    }});
    CHECK(international.pair(unicode->device.id));
}
} // namespace

int main(int argc, char** argv) {
    if (argc != 2) { std::cerr << "Expected a device test case name\n"; return 2; }
    const std::string_view test = argv[1];
    try {
        if (test == "discovery") discovery();
        else if (test == "pairing") pairDevice();
        else if (test == "lifecycle") lifecycle();
        else if (test == "controls") controls();
        else if (test == "failures") failures();
        else if (test == "concurrency") concurrency();
        else if (test == "validation") validation();
        else throw std::runtime_error("Unknown device test case");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

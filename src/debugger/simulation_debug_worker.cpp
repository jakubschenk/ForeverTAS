#include <forevervalidator/experimental/physics_sandbox.h>
#include <forevervalidator/native.h>

#include "mutations/input_event_formatter.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

using forevervalidator::AssetBytes;
using forevervalidator::AssetSource;
using forevervalidator::DiscriminatedResult;
using forevervalidator::ReplayIdentity;
using forevervalidator::experimental::PhysicsSandbox;
using forevervalidator::experimental::PhysicsSandboxStateView;

template <typename T, typename Error>
T Require(DiscriminatedResult<T, Error> result, const char *operation) {
    if (!result) {
        std::string diagnostic(operation);
        if (!result.Error().diagnostic.empty()) {
            diagnostic += ": ";
            diagnostic += result.Error().diagnostic;
        }
        throw std::runtime_error(std::move(diagnostic));
    }
    return std::move(result).Value();
}

void PrintJsonString(const std::string &value) {
    std::putchar('"');
    for (const unsigned char character : value) {
        switch (character) {
        case '"':
            std::fputs("\\\"", stdout);
            break;
        case '\\':
            std::fputs("\\\\", stdout);
            break;
        case '\b':
            std::fputs("\\b", stdout);
            break;
        case '\f':
            std::fputs("\\f", stdout);
            break;
        case '\n':
            std::fputs("\\n", stdout);
            break;
        case '\r':
            std::fputs("\\r", stdout);
            break;
        case '\t':
            std::fputs("\\t", stdout);
            break;
        default:
            if (character < 0x20u) {
                std::printf("\\u%04x", static_cast<unsigned>(character));
            } else {
                std::putchar(character);
            }
            break;
        }
    }
    std::putchar('"');
}

void PrintState(const PhysicsSandboxStateView &state,
                std::uint32_t simulationHorizonMs) {
    const auto number = [](double value) {
        return std::isfinite(value) ? value : 0.0;
    };
    std::fputs("@FOREVERTAS_STATE {", stdout);
    std::printf(
            "\"tick\":%llu,\"timeMs\":%llu,\"horizonMs\":%u,"
            "\"position\":[%.17g,%.17g,%.17g],"
            "\"rotation\":[%.17g,%.17g,%.17g,%.17g],"
            "\"linearSpeed\":[%.17g,%.17g,%.17g],"
            "\"angularSpeed\":[%.17g,%.17g,%.17g],"
            "\"force\":[%.17g,%.17g,%.17g],"
            "\"torque\":[%.17g,%.17g,%.17g],"
            "\"accelerate\":%.17g,\"brake\":%.17g,\"steering\":%.17g,"
            "\"checkpointsCollected\":%u,\"checkpointsTotal\":%u,"
            "\"completedLaps\":%u,\"totalLaps\":%u,"
            "\"raceCompleted\":%s,\"respawnCount\":%u",
            static_cast<unsigned long long>(state.tick),
            static_cast<unsigned long long>(state.timeMs),
            simulationHorizonMs,
            number(state.car.position.x),
            number(state.car.position.y),
            number(state.car.position.z),
            number(state.car.rotationX),
            number(state.car.rotationY),
            number(state.car.rotationZ),
            number(state.car.rotationW),
            number(state.car.linearSpeed.x),
            number(state.car.linearSpeed.y),
            number(state.car.linearSpeed.z),
            number(state.car.angularSpeed.x),
            number(state.car.angularSpeed.y),
            number(state.car.angularSpeed.z),
            number(state.car.force.x),
            number(state.car.force.y),
            number(state.car.force.z),
            number(state.car.torque.x),
            number(state.car.torque.y),
            number(state.car.torque.z),
            number(state.accelerate),
            number(state.brake),
            number(state.steering),
            state.checkpointsCollected,
            state.checkpointsTotal,
            state.completedLaps,
            state.totalLaps,
            state.raceCompleted ? "true" : "false",
            state.respawnCount);
    std::fputs(",\"wheelGroundPosition\":[", stdout);
    for (std::size_t wheel = 0u;
         wheel < state.car.wheelGroundPosition.size(); ++wheel) {
        const auto &position = state.car.wheelGroundPosition[wheel];
        std::printf("%s[%.17g,%.17g,%.17g]", wheel == 0u ? "" : ",",
                    number(position.x), number(position.y),
                    number(position.z));
    }
    std::fputs("],\"wheelContact\":[", stdout);
    for (std::size_t wheel = 0u; wheel < state.car.wheelContact.size();
         ++wheel) {
        std::printf("%s%s", wheel == 0u ? "" : ",",
                    state.car.wheelContact[wheel] ? "true" : "false");
    }
    std::fputs("],\"wheelHasSurface\":[", stdout);
    for (std::size_t wheel = 0u; wheel < state.car.wheelHasSurface.size();
         ++wheel) {
        std::printf("%s%s", wheel == 0u ? "" : ",",
                    state.car.wheelHasSurface[wheel] ? "true" : "false");
    }
    std::fputs("],\"wheelSliding\":[", stdout);
    for (std::size_t wheel = 0u; wheel < state.car.wheelSliding.size();
         ++wheel) {
        std::printf("%s%s", wheel == 0u ? "" : ",",
                    state.car.wheelSliding[wheel] ? "true" : "false");
    }
    std::fputs("],\"wheelSurface\":[", stdout);
    for (std::size_t wheel = 0u; wheel < state.car.wheelSurface.size();
         ++wheel) {
        std::printf("%s%u", wheel == 0u ? "" : ",",
                    static_cast<unsigned>(state.car.wheelSurface[wheel]));
    }
    std::putchar(']');
    if (state.finishTimeMs.has_value()) {
        std::printf(",\"finishTimeMs\":%u", *state.finishTimeMs);
    }
    if (state.stuntsScore.has_value()) {
        std::printf(",\"stuntsScore\":%u", *state.stuntsScore);
    }
    std::fputs("}\n", stdout);
    std::fflush(stdout);
}

void PrintError(const std::string &message) {
    std::fputs("@FOREVERTAS_ERROR {\"message\":", stdout);
    PrintJsonString(message);
    std::fputs("}\n", stdout);
    std::fflush(stdout);
}

#if defined(_MSC_VER)
#define FOREVERTAS_NOINLINE __declspec(noinline)
#else
#define FOREVERTAS_NOINLINE __attribute__((noinline))
#endif

} // namespace

extern "C" FOREVERTAS_NOINLINE void forevertas_debugger_tick_boundary() {
}

int main(int argc, char **argv) {
    using namespace forevervalidator;
    using namespace forevervalidator::experimental;

    if (argc != 5) {
        PrintError("worker requires a Packs directory, replay path, Simulation horizon, and input script");
        return 2;
    }

    try {
        const std::string packsDirectory(argv[1]);
        const std::string replayPath(argv[2]);
        const unsigned long parsedHorizon = std::stoul(argv[3]);
        if (parsedHorizon < 10ul || parsedHorizon > 2147481040ul ||
            parsedHorizon % 10ul != 0ul) {
            throw std::invalid_argument("invalid Simulation horizon");
        }
        const std::uint32_t simulationHorizonMs =
                static_cast<std::uint32_t>(parsedHorizon);
        std::ifstream scriptFile(argv[4], std::ios::binary);
        if (!scriptFile) {
            throw std::runtime_error("opening debugger input script failed");
        }
        const std::string inputScript{
                std::istreambuf_iterator<char>(scriptFile),
                std::istreambuf_iterator<char>()};
        const forevertas::InputScriptParseResult parsedInputs =
                forevertas::ParseInputScript(inputScript);
        if (!parsedInputs) {
            throw std::runtime_error(*parsedInputs.error);
        }
        const ReplayIdentity identity{replayPath};
        AssetSource source =
                Require(OpenInstalledPackDirectory(packsDirectory),
                        "opening Packs directory failed");
        AssetBytes replay =
                Require(ReadNativeReplayFile(replayPath, identity),
                        "reading replay failed");
        PhysicsSandboxOptions options;
        options.backend = SimulationBackend::Reference;
        options.tickDurationMs = 10u;
        options.timelineMode = PhysicsSandboxTimelineMode::Canonical;
        options.simulationHorizonMs = simulationHorizonMs;
        PhysicsSandbox sandbox =
                Require(CreatePhysicsSandbox(std::move(source), options),
                        "creating reference sandbox failed");
        PhysicsSandboxStateView state = Require(
                sandbox.LoadScenario({replay.data(), replay.size()}, identity),
                "loading scenario failed");
        const forevertas::InputScriptBaselineResult baseline =
                forevertas::BuildInputScriptBaseline(
                        Require(sandbox.ReadInputs(),
                                "reading canonical debugger inputs failed"),
                        parsedInputs.commands,
                        options.tickDurationMs);
        if (!baseline) {
            throw std::runtime_error(*baseline.error);
        }
        Require(sandbox.ReplaceInputs(baseline.events),
                "applying debugger input script failed");
        state = Require(sandbox.ReadState(),
                        "reading debugger initial state failed");
        PrintState(state, simulationHorizonMs);
        forevertas_debugger_tick_boundary();

        while (state.timeMs < simulationHorizonMs && !state.raceCompleted) {
            state =
                    Require(sandbox.AdvanceTicks(1u),
                            "advancing reference simulation failed");
            PrintState(state, simulationHorizonMs);
            forevertas_debugger_tick_boundary();
        }
        return 0;
    } catch (const std::exception &error) {
        PrintError(error.what());
    } catch (...) {
        PrintError("unexpected reference debugger worker failure");
    }
    return 1;
}

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <limits>
#include <queue>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

enum class ApplianceState { Off, Starting, On, CoolingDown };
enum class RequestedState { None, RequestOff, RequestOn };
enum class OperatingMode { Eco, Balanced, Performance, Emergency };

std::string toString(ApplianceState state) {
    switch (state) {
        case ApplianceState::Off: return "OFF";
        case ApplianceState::Starting: return "STARTING";
        case ApplianceState::On: return "ON";
        case ApplianceState::CoolingDown: return "COOLDOWN";
    }
    return "UNKNOWN";
}

std::string toString(OperatingMode mode) {
    switch (mode) {
        case OperatingMode::Eco: return "Eco";
        case OperatingMode::Balanced: return "Balanced";
        case OperatingMode::Performance: return "Performance";
        case OperatingMode::Emergency: return "Emergency";
    }
    return "Unknown";
}

struct ModeWeights {
    double energy = 1.0;
    double battery = 1.0;
    double health = 1.0;
    double solar = 1.0;
    double grid = 1.0;
    double priority = 1.0;
    double usefulness = 1.0;
    double preference = 1.0;
    double balance = 1.0;
    double reserve = 1.0;
};

ModeWeights weightsFor(OperatingMode mode) {
    switch (mode) {
        case OperatingMode::Eco:
            return {1.4, 1.5, 1.5, 1.4, 1.8, 0.9, 0.9, 0.9, 1.2, 1.6};
        case OperatingMode::Performance:
            return {0.8, 0.9, 0.9, 1.1, 0.8, 1.4, 1.4, 1.3, 0.9, 0.7};
        case OperatingMode::Emergency:
            return {1.8, 2.5, 2.3, 1.0, 2.0, 2.0, 1.5, 1.2, 1.4, 2.5};
        case OperatingMode::Balanced:
        default:
            return {};
    }
}

class Appliance {
public:
    Appliance(int id, std::string name, double voltage, double powerWatts, int priority,
              double usefulnessScore, bool running, bool mandatory, bool userLocked = false)
        : id(id), name(std::move(name)), voltage(voltage), powerWatts(powerWatts), priority(priority),
          usefulnessScore(usefulnessScore), running(running), mandatory(mandatory), userLocked(userLocked),
          currentState(running ? ApplianceState::On : ApplianceState::Off) {}

    int id;
    std::string name;
    double voltage;
    double powerWatts;
    int priority;
    double usefulnessScore;
    double userPreferenceScore = 5.0;
    bool running;
    bool mandatory;
    bool userLocked;

    ApplianceState currentState = ApplianceState::Off;
    RequestedState requestedState = RequestedState::None;
    bool startupState = false;
    bool cooldownState = false;
    double runtimeMinutes = 0.0;
    double accumulatedEnergyWh = 0.0;
    double estimatedCompletionMinutes = 0.0;

    double minimumOnMinutes = 0.0;
    double minimumOffMinutes = 0.0;
    double minutesInCurrentState = 60.0;
    double startupSurgeWatts = 0.0;
    double startupDelaySeconds = 0.0;
    std::vector<int> dependencyIds;
    std::vector<int> conflictIds;
    double maximumDailyRuntimeMinutes = 24.0 * 60.0;
    double dailyRuntimeMinutes = 0.0;
};

struct SourceSnapshot {
    double solarVoltage = 24.0;
    double solarCurrent = 8.0;
    double solarPowerProductionWatts = 320.0;
    double solarEnergyProductionWh = 1200.0;
    double renewableForecastNextHourWatts = 300.0;

    double batteryVoltage = 24.0;
    double batteryCurrent = 3.0;
    double batteryEnergyRemainingWh = 900.0;
    double batteryCapacityWh = 1200.0;
    double batteryHealth = 0.92;
    double maxBatteryDischargePower = 180.0;
    double criticalSocPercent = 20.0;

    bool gridAvailable = false;
    double gridPowerLimit = 0.0;
    double gridPricePerKWh = 0.28;
    double lowGridPriceThreshold = 0.16;

    bool generatorAvailable = false;
    double generatorPowerLimit = 0.0;
    double generatorCostPerKWh = 0.75;

    double inverterLimitWatts = 400.0;
    double wiringLimitWatts = 350.0;
    double systemVoltage = 24.0;
    double currentLimitAmps = 25.0;
    double planningRuntimeHours = 1.0;

    double batterySocPercent() const {
        return (batteryEnergyRemainingWh / std::max(1.0, batteryCapacityWh)) * 100.0;
    }

    double predictedBatterySocPercent(double loadWatts) const {
        double solarOffset = std::min(loadWatts, renewableForecastNextHourWatts);
        double batteryWatts = std::max(0.0, loadWatts - solarOffset);
        double predictedWh = batteryEnergyRemainingWh - batteryWatts * planningRuntimeHours;
        return (std::max(0.0, predictedWh) / std::max(1.0, batteryCapacityWh)) * 100.0;
    }

    double safePowerLimit() const {
        double sourceCapacity = solarPowerProductionWatts + maxBatteryDischargePower;
        if (gridAvailable) {
            sourceCapacity += gridPowerLimit;
        }
        if (generatorAvailable) {
            sourceCapacity += generatorPowerLimit;
        }
        return std::max(0.0, std::min(sourceCapacity, std::min(inverterLimitWatts, wiringLimitWatts)));
    }

    bool gridEconomicallyBeneficial() const {
        return gridAvailable && gridPricePerKWh <= lowGridPriceThreshold;
    }
};

struct ConstraintResult {
    bool feasible = true;
    std::string reason = "accepted";
};

class DemandTracker {
public:
    double currentUsage(const std::vector<Appliance>& devices) const {
        double total = 0.0;
        for (const Appliance& device : devices) {
            if (device.running) {
                total += device.powerWatts;
            }
        }
        return total;
    }
};

class ConstraintGuard {
public:
    bool canAccept(const Appliance& candidate, const SourceSnapshot& source, double remainingPower) const {
        return evaluate(candidate, source, remainingPower, {}, true).feasible;
    }

    ConstraintResult evaluate(const Appliance& candidate, const SourceSnapshot& source, double remainingPower,
                              const std::vector<int>& selectedIds, bool startingFromOff) const {
        if (candidate.voltage > source.systemVoltage * 1.1) {
            return {false, "voltage mismatch"};
        }
        double runningCurrent = candidate.powerWatts / std::max(1.0, source.systemVoltage);
        if (runningCurrent > source.currentLimitAmps) {
            return {false, "current limit"};
        }
        double requiredPower = candidate.powerWatts + (startingFromOff ? candidate.startupSurgeWatts : 0.0);
        if (requiredPower > remainingPower) {
            return {false, "insufficient remaining power including startup surge"};
        }
        if (candidate.powerWatts > source.maxBatteryDischargePower + source.solarPowerProductionWatts +
                                      (source.gridAvailable ? source.gridPowerLimit : 0.0) +
                                      (source.generatorAvailable ? source.generatorPowerLimit : 0.0)) {
            return {false, "source path capacity"};
        }
        if (candidate.currentState == ApplianceState::Off && candidate.minutesInCurrentState < candidate.minimumOffMinutes) {
            return {false, "minimum OFF time"};
        }
        if (candidate.dailyRuntimeMinutes >= candidate.maximumDailyRuntimeMinutes) {
            return {false, "maximum daily runtime"};
        }
        for (int dependency : candidate.dependencyIds) {
            if (std::find(selectedIds.begin(), selectedIds.end(), dependency) == selectedIds.end()) {
                return {false, "missing dependency"};
            }
        }
        for (int conflict : candidate.conflictIds) {
            if (std::find(selectedIds.begin(), selectedIds.end(), conflict) != selectedIds.end()) {
                return {false, "conflicting appliance"};
            }
        }
        return {};
    }

    ConstraintResult canTurnOff(const Appliance& candidate) const {
        if (candidate.userLocked || candidate.mandatory) {
            return {false, "mandatory or user locked"};
        }
        if (candidate.currentState == ApplianceState::On && candidate.minutesInCurrentState < candidate.minimumOnMinutes) {
            return {false, "minimum ON time"};
        }
        return {};
    }
};

struct SearchNode {
    std::vector<int> assignment;
    std::vector<int> selectedIds;
    std::vector<std::string> explanations;
    double load = 0.0;
    double surgeLoad = 0.0;
    double gCost = 0.0;
    double hCost = 0.0;
    double fCost = 0.0;
    int nextIndex = 0;
    int depth = 0;
};

struct SearchNodeCompare {
    bool operator()(const SearchNode& lhs, const SearchNode& rhs) const {
        if (lhs.fCost != rhs.fCost) {
            return lhs.fCost > rhs.fCost;
        }
        if (lhs.hCost != rhs.hCost) {
            return lhs.hCost > rhs.hCost;
        }
        return lhs.depth < rhs.depth;
    }
};

struct PlannerDiagnostics {
    int nodesExpanded = 0;
    int nodesGenerated = 0;
    int nodesPruned = 0;
    std::size_t maxFrontierSize = 0;
    int searchDepth = 0;
    double finalPathCost = 0.0;
    double heuristicValue = 0.0;
    double executionTimeMs = 0.0;
};

struct PlanResult {
    std::vector<int> selectedIndexes;
    PlannerDiagnostics diagnostics;
    std::vector<std::string> explanations;
};

class AStarPlanner {
public:
    PlanResult planDetailed(const std::vector<Appliance>& candidates, const SourceSnapshot& source,
                            double remainingPower, const ConstraintGuard& ruleSet,
                            OperatingMode mode) {
        auto started = std::chrono::steady_clock::now();
        PlanResult result;
        if (candidates.empty()) {
            return result;
        }

        const ModeWeights weights = weightsFor(mode);
        std::priority_queue<SearchNode, std::vector<SearchNode>, SearchNodeCompare> frontier;
        std::unordered_map<std::string, double> bestSeen;
        std::vector<std::string> closed;
        SearchNode bestGoal;
        bool haveGoal = false;
        double bestCost = std::numeric_limits<double>::infinity();

        SearchNode start;
        start.assignment.assign(candidates.size(), -1);
        start.hCost = estimateFutureCost(start, candidates, source, remainingPower, weights);
        start.fCost = start.gCost + start.hCost;
        frontier.push(start);
        bestSeen[stateKey(start)] = start.gCost;

        while (!frontier.empty()) {
            result.diagnostics.maxFrontierSize = std::max(result.diagnostics.maxFrontierSize, frontier.size());
            SearchNode node = frontier.top();
            frontier.pop();
            const std::string key = stateKey(node);
            if (std::find(closed.begin(), closed.end(), key) != closed.end()) {
                ++result.diagnostics.nodesPruned;
                continue;
            }
            closed.push_back(key);
            ++result.diagnostics.nodesExpanded;
            result.diagnostics.searchDepth = std::max(result.diagnostics.searchDepth, node.depth);

            if (node.nextIndex >= static_cast<int>(candidates.size())) {
                printFinalCandidate(node, candidates, source, remainingPower);
                if (node.gCost < bestCost) {
                    bestCost = node.gCost;
                    bestGoal = node;
                    haveGoal = true;
                }
                continue;
            }

            const Appliance& device = candidates[node.nextIndex];
            for (int choice : {0, 1}) {
                SearchNode child = node;
                child.assignment[node.nextIndex] = choice;
                child.nextIndex = node.nextIndex + 1;
                child.depth = node.depth + 1;
                std::ostringstream explanation;

                if (choice == 1) {
                    ConstraintResult constraint = ruleSet.evaluate(device, source, remainingPower - node.load,
                                                                    node.selectedIds, !device.running);
                    if (!constraint.feasible) {
                        ++result.diagnostics.nodesPruned;
                        std::cout << "Rejected: " << device.name << " ON because " << constraint.reason
                                  << " | remaining battery=" << source.predictedBatterySocPercent(node.load) << "%"
                                  << " | remaining solar=" << std::max(0.0, source.renewableForecastNextHourWatts - node.load) << " W"
                                  << " | headroom=" << std::max(0.0, remainingPower - node.load) << " W" << std::endl;
                        continue;
                    }
                    child.load = node.load + device.powerWatts;
                    child.surgeLoad = node.surgeLoad + (!device.running ? device.startupSurgeWatts : 0.0);
                    child.selectedIds.push_back(device.id);
                    child.gCost = node.gCost + decisionCost(device, true, source, child.load, remainingPower, weights);
                    explanation << "selected " << device.name << ": priority=" << device.priority
                                << ", usefulness=" << device.usefulnessScore
                                << ", preference=" << device.userPreferenceScore
                                << ", predicted SOC=" << source.predictedBatterySocPercent(child.load) << "%"
                                << ", solar headroom=" << std::max(0.0, source.renewableForecastNextHourWatts - child.load) << " W"
                                << ", power headroom=" << std::max(0.0, remainingPower - child.load) << " W";
                } else {
                    child.load = node.load;
                    child.gCost = node.gCost + decisionCost(device, false, source, child.load, remainingPower, weights);
                    explanation << "rejected " << device.name << ": missed priority/usefulness/preference benefit"
                                << " | remaining battery=" << source.predictedBatterySocPercent(child.load) << "%"
                                << " | remaining solar=" << std::max(0.0, source.renewableForecastNextHourWatts - child.load) << " W"
                                << " | power headroom=" << std::max(0.0, remainingPower - child.load) << " W";
                }

                child.explanations.push_back(explanation.str());
                child.hCost = estimateFutureCost(child, candidates, source,
                                                 std::max(0.0, remainingPower - child.load), weights);
                child.fCost = child.gCost + child.hCost;
                ++result.diagnostics.nodesGenerated;

                std::cout << "State: " << device.name << (choice == 1 ? " ON" : " OFF") << std::endl;
                std::cout << "- total selected power: " << child.load << "W" << std::endl;
                std::cout << "- g(n): " << child.gCost << std::endl;
                std::cout << "- h(n): " << child.hCost << std::endl;
                std::cout << "- f(n): " << child.fCost << std::endl;
                std::cout << "- why: " << explanation.str() << std::endl;

                const std::string childKey = stateKey(child);
                auto existing = bestSeen.find(childKey);
                if (existing != bestSeen.end() && child.gCost >= existing->second) {
                    ++result.diagnostics.nodesPruned;
                    continue;
                }
                bestSeen[childKey] = child.gCost;
                frontier.push(child);
            }
        }

        if (haveGoal) {
            result.selectedIndexes = decodeSelection(bestGoal.assignment);
            result.explanations = bestGoal.explanations;
            result.diagnostics.finalPathCost = bestGoal.gCost;
            result.diagnostics.heuristicValue = bestGoal.hCost;
        }
        auto finished = std::chrono::steady_clock::now();
        result.diagnostics.executionTimeMs = std::chrono::duration<double, std::milli>(finished - started).count();
        printDiagnostics(result.diagnostics);
        return result;
    }

    std::vector<int> plan(const std::vector<Appliance>& candidates, const SourceSnapshot& source,
                          double remainingPower, const ConstraintGuard& ruleSet) {
        return planDetailed(candidates, source, remainingPower, ruleSet, OperatingMode::Balanced).selectedIndexes;
    }

private:
    double decisionCost(const Appliance& device, bool selected, const SourceSnapshot& source, double totalLoad,
                        double remainingPower, const ModeWeights& weights) const {
        const double normalizedPower = device.powerWatts / std::max(1.0, source.safePowerLimit());
        const double predictedSoc = source.predictedBatterySocPercent(totalLoad);
        const double socPenalty = std::max(0.0, 35.0 - predictedSoc) / 35.0;
        const double batteryHealthPenalty = (1.0 - source.batteryHealth) * normalizedPower;
        const double solarUtilization = std::min(device.powerWatts, source.renewableForecastNextHourWatts) /
                                        std::max(1.0, device.powerWatts);
        const double gridCost = (!source.gridEconomicallyBeneficial() && totalLoad > source.solarPowerProductionWatts)
                                    ? (device.powerWatts / 1000.0) * source.gridPricePerKWh
                                    : 0.0;
        const double loadBalancePenalty = std::abs(0.70 - (totalLoad / std::max(1.0, source.safePowerLimit())));
        const double reservePenalty = std::max(0.0, 0.15 - ((remainingPower - totalLoad) / std::max(1.0, source.safePowerLimit())));
        const double priorityBenefit = static_cast<double>(device.priority) / 100.0;
        const double usefulnessBenefit = device.usefulnessScore / 10.0;
        const double preferenceBenefit = device.userPreferenceScore / 10.0;
        const double startupPenalty = device.startupDelaySeconds / 120.0 + device.startupSurgeWatts / std::max(1.0, source.safePowerLimit());

        const double maximumBenefit = weights.priority + weights.usefulness + weights.preference + (weights.solar * 0.45);
        const double earnedBenefit = weights.priority * priorityBenefit + weights.usefulness * usefulnessBenefit +
                                     weights.preference * preferenceBenefit + weights.solar * solarUtilization * 0.45;
        const double stressCost = weights.energy * normalizedPower + weights.battery * socPenalty +
                                  weights.health * batteryHealthPenalty + weights.grid * gridCost +
                                  weights.balance * loadBalancePenalty + weights.reserve * reservePenalty + startupPenalty;

        if (selected) {
            return std::max(0.0, stressCost + (maximumBenefit - earnedBenefit));
        }

        return std::max(0.0, earnedBenefit + (device.mandatory ? 10.0 : 0.0));
    }

    double estimateFutureCost(const SearchNode& node, const std::vector<Appliance>& candidates,
                              const SourceSnapshot& source, double remainingPower, const ModeWeights& weights) const {
        // Admissible lower bound: each future appliance gets the cheaper relaxed decision cost. This ignores pairwise
        // conflicts and therefore never overestimates the true remaining cost of the constrained search.
        double lowerBound = 0.0;
        for (std::size_t i = node.nextIndex; i < candidates.size(); ++i) {
            const Appliance& device = candidates[i];
            double relaxedOnLoad = node.load + std::min(device.powerWatts, remainingPower);
            double onCost = decisionCost(device, true, source, relaxedOnLoad, remainingPower, weights);
            double offCost = decisionCost(device, false, source, node.load, remainingPower, weights);
            lowerBound += std::min(onCost, offCost);
        }
        return lowerBound;
    }

    void printFinalCandidate(const SearchNode& node, const std::vector<Appliance>& candidates,
                             const SourceSnapshot& source, double remainingPower) const {
        std::vector<int> finalSelection = decodeSelection(node.assignment);
        std::cout << "Final candidate state:" << std::endl;
        std::cout << "- selected appliances: ";
        if (finalSelection.empty()) {
            std::cout << "none";
        }
        for (int index : finalSelection) {
            std::cout << candidates[index].name << " ";
        }
        std::cout << std::endl;
        std::cout << "- total power: " << node.load << "W" << std::endl;
        std::cout << "- remaining battery: " << source.predictedBatterySocPercent(node.load) << "%" << std::endl;
        std::cout << "- remaining solar: " << std::max(0.0, source.renewableForecastNextHourWatts - node.load) << " W" << std::endl;
        std::cout << "- remaining power headroom: " << std::max(0.0, remainingPower - node.load) << " W" << std::endl;
        std::cout << "- g(n): " << node.gCost << std::endl;
        std::cout << "- h(n): " << node.hCost << std::endl;
        std::cout << "- f(n): " << node.fCost << std::endl;
    }

    void printDiagnostics(const PlannerDiagnostics& diagnostics) const {
        std::cout << "A* diagnostics:" << std::endl;
        std::cout << "- nodes expanded: " << diagnostics.nodesExpanded << std::endl;
        std::cout << "- nodes generated: " << diagnostics.nodesGenerated << std::endl;
        std::cout << "- nodes pruned: " << diagnostics.nodesPruned << std::endl;
        std::cout << "- maximum frontier size: " << diagnostics.maxFrontierSize << std::endl;
        std::cout << "- search depth: " << diagnostics.searchDepth << std::endl;
        std::cout << "- final path cost: " << diagnostics.finalPathCost << std::endl;
        std::cout << "- heuristic value: " << diagnostics.heuristicValue << std::endl;
        std::cout << "- execution time: " << diagnostics.executionTimeMs << " ms" << std::endl;
    }

    std::string stateKey(const SearchNode& node) const {
        std::ostringstream stream;
        stream << node.nextIndex << ":";
        for (int value : node.assignment) {
            stream << value << ",";
        }
        return stream.str();
    }

    std::vector<int> decodeSelection(const std::vector<int>& assignment) const {
        std::vector<int> selection;
        selection.reserve(assignment.size());
        for (std::size_t i = 0; i < assignment.size(); ++i) {
            if (assignment[i] == 1) {
                selection.push_back(static_cast<int>(i));
            }
        }
        return selection;
    }
};

class EnergyManagementSystem {
public:
    EnergyManagementSystem() {
        seedDevices();
        seedSource();
    }

    void run() {
        OperatingMode mode = OperatingMode::Balanced;
        printSection("1. INITIAL SYSTEM STATE");
        std::cout << "- operating mode: " << toString(mode) << std::endl;
        for (const Appliance& device : devices) {
            std::cout << "- " << device.name << " | power=" << device.powerWatts << " W"
                      << " | state=" << toString(device.currentState)
                      << " | requested=" << static_cast<int>(device.requestedState)
                      << " | runtime=" << device.runtimeMinutes << " min"
                      << " | energy=" << device.accumulatedEnergyWh << " Wh"
                      << " | mandatory=" << (device.mandatory ? "true" : "false") << std::endl;
        }

        printSection("2. SYSTEM MEASUREMENTS");
        updateForecastAndPricing();
        std::cout << "- solar production: " << source.solarPowerProductionWatts << " W" << std::endl;
        std::cout << "- renewable forecast next hour: " << source.renewableForecastNextHourWatts << " W" << std::endl;
        std::cout << "- battery energy remaining: " << source.batteryEnergyRemainingWh << " Wh" << std::endl;
        std::cout << "- battery SOC: " << source.batterySocPercent() << "%" << std::endl;
        std::cout << "- battery health: " << source.batteryHealth * 100.0 << "%" << std::endl;
        std::cout << "- grid: " << (source.gridAvailable ? "available" : "unavailable")
                  << " | price=$" << source.gridPricePerKWh << "/kWh"
                  << " | economical=" << (source.gridEconomicallyBeneficial() ? "true" : "false") << std::endl;
        std::cout << "- generator: " << (source.generatorAvailable ? "available" : "unavailable") << std::endl;
        std::cout << "- system power limit: " << source.safePowerLimit() << " W" << std::endl;

        double currentUsage = demandTracker.currentUsage(devices);
        std::cout << "\n3. CURRENT USAGE" << std::endl;
        std::cout << "- current watt usage: " << currentUsage << " W" << std::endl;
        std::cout << "- remaining power: " << (source.safePowerLimit() - currentUsage) << " W" << std::endl;
        shedLoadsIfNeeded(currentUsage);

        printSection("4. USER REQUEST PROCESSING");
        std::cout << "Before user commands:" << std::endl;
        for (const Appliance& device : devices) {
            std::cout << "- " << device.name << " | state=" << toString(device.currentState)
                      << " | mandatory=" << (device.mandatory ? "true" : "false")
                      << " | userLocked=" << (device.userLocked ? "true" : "false") << std::endl;
        }

        std::vector<std::string> acceptedOnRequests;
        std::vector<std::string> acceptedOffRequests;
        handleUserRequest("ON", "Pump", acceptedOnRequests, acceptedOffRequests);
        handleUserRequest("OFF", "Lights", acceptedOnRequests, acceptedOffRequests);

        currentUsage = demandTracker.currentUsage(devices);
        double remainingPower = source.safePowerLimit() - currentUsage;

        std::cout << "\nAfter user commands:" << std::endl;
        for (const std::string& request : acceptedOnRequests) {
            std::cout << "- accepted ON request: " << request << std::endl;
        }
        for (const std::string& request : acceptedOffRequests) {
            std::cout << "- accepted OFF request: " << request << std::endl;
        }
        std::cout << "- current watt usage: " << currentUsage << " W" << std::endl;
        std::cout << "- remaining power: " << remainingPower << " W" << std::endl;

        std::cout << "\n5. A* INPUT" << std::endl;
        std::vector<Appliance> optionalPool;
        optionalPool.reserve(devices.size());
        for (Appliance& device : devices) {
            if (device.running || device.mandatory || device.userLocked) {
                continue;
            }
            ConstraintResult constraint = constraintGuard.evaluate(device, source, remainingPower, {}, !device.running);
            if (constraint.feasible) {
                optionalPool.push_back(device);
                std::cout << "- " << device.name << " (" << device.powerWatts << " W, surge=" << device.startupSurgeWatts
                          << " W, priority=" << device.priority << ", usefulness=" << device.usefulnessScore
                          << ", preference=" << device.userPreferenceScore << ")" << std::endl;
            } else {
                std::cout << "- candidate rejected before A*: " << device.name << " because " << constraint.reason << std::endl;
            }
        }

        std::cout << "\n6. A* RESULT" << std::endl;
        PlanResult plan = planner.planDetailed(optionalPool, source, remainingPower, constraintGuard, mode);
        for (const std::string& explanation : plan.explanations) {
            std::cout << "- decision: " << explanation << std::endl;
        }
        for (int index : plan.selectedIndexes) {
            std::cout << "- selected: " << optionalPool[index].name << std::endl;
        }

        for (int index : plan.selectedIndexes) {
            for (Appliance& device : devices) {
                if (device.id == optionalPool[index].id) {
                    device.running = true;
                    device.currentState = device.startupDelaySeconds > 0.0 ? ApplianceState::Starting : ApplianceState::On;
                    device.startupState = device.currentState == ApplianceState::Starting;
                    device.estimatedCompletionMinutes = device.startupDelaySeconds / 60.0;
                    break;
                }
            }
        }

        printSection("7. FINAL SYSTEM STATE");
        for (const Appliance& device : devices) {
            std::cout << "- " << device.name << " => " << toString(device.currentState)
                      << " | running=" << (device.running ? "true" : "false")
                      << " | mandatory=" << (device.mandatory ? "true" : "false")
                      << " | userLocked=" << (device.userLocked ? "true" : "false")
                      << " | runtime=" << device.runtimeMinutes << " min"
                      << " | accumulatedEnergy=" << device.accumulatedEnergyWh << " Wh"
                      << " | completion=" << device.estimatedCompletionMinutes << " min" << std::endl;
        }
    }

private:
    void seedSource() {
        source.solarVoltage = 24.0;
        source.solarCurrent = 8.0;
        source.solarPowerProductionWatts = 320.0;
        source.solarEnergyProductionWh = 1200.0;
        source.renewableForecastNextHourWatts = 280.0;
        source.batteryVoltage = 24.0;
        source.batteryCurrent = 3.0;
        source.batteryEnergyRemainingWh = 900.0;
        source.batteryCapacityWh = 1200.0;
        source.batteryHealth = 0.92;
        source.maxBatteryDischargePower = 180.0;
        source.gridAvailable = true;
        source.gridPowerLimit = 250.0;
        source.gridPricePerKWh = 0.22;
        source.lowGridPriceThreshold = 0.18;
        source.generatorAvailable = true;
        source.generatorPowerLimit = 220.0;
        source.generatorCostPerKWh = 0.70;
        source.inverterLimitWatts = 400.0;
        source.wiringLimitWatts = 350.0;
        source.systemVoltage = 24.0;
        source.currentLimitAmps = 25.0;
        source.planningRuntimeHours = 1.0;
    }

    void seedDevices() {
        devices.emplace_back(1, "Fridge", 24.0, 120.0, 100, 9.5, true, true);
        devices.back().minimumOnMinutes = 20.0;
        devices.back().userPreferenceScore = 10.0;
        devices.back().runtimeMinutes = 45.0;
        devices.back().minutesInCurrentState = 45.0;

        devices.emplace_back(2, "Pump", 24.0, 80.0, 95, 8.7, false, false);
        devices.back().startupSurgeWatts = 40.0;
        devices.back().startupDelaySeconds = 5.0;
        devices.back().minimumOffMinutes = 2.0;
        devices.back().userPreferenceScore = 8.5;
        devices.back().maximumDailyRuntimeMinutes = 240.0;

        devices.emplace_back(3, "Lights", 24.0, 30.0, 40, 6.2, false, false);
        devices.back().userPreferenceScore = 7.5;
        devices.back().dependencyIds.push_back(1);

        devices.emplace_back(4, "TV", 24.0, 140.0, 30, 7.8, false, false);
        devices.back().startupSurgeWatts = 0.0;
        devices.back().startupDelaySeconds = 2.0;
        devices.back().userPreferenceScore = 7.0;
        devices.back().conflictIds.push_back(5);

        devices.emplace_back(5, "Fan", 24.0, 60.0, 20, 5.4, false, false);
        devices.back().userPreferenceScore = 6.0;
        devices.back().conflictIds.push_back(4);
    }

    void updateForecastAndPricing() {
        source.renewableForecastNextHourWatts = std::max(0.0, source.solarPowerProductionWatts * 0.88);
        if (source.renewableForecastNextHourWatts > 300.0) {
            source.gridPricePerKWh = 0.30;
        }
    }

    void shedLoadsIfNeeded(double& currentUsage) {
        bool solarDrop = source.renewableForecastNextHourWatts < source.solarPowerProductionWatts * 0.65;
        bool criticalBattery = source.batterySocPercent() <= source.criticalSocPercent;
        if (!solarDrop && !criticalBattery) {
            return;
        }
        std::cout << "- load shedding triggered: " << (solarDrop ? "solar forecast drop " : "")
                  << (criticalBattery ? "critical battery SOC" : "") << std::endl;
        std::vector<Appliance*> shedCandidates;
        for (Appliance& device : devices) {
            if (device.running && constraintGuard.canTurnOff(device).feasible) {
                shedCandidates.push_back(&device);
            }
        }
        std::sort(shedCandidates.begin(), shedCandidates.end(), [](const Appliance* lhs, const Appliance* rhs) {
            return lhs->priority < rhs->priority;
        });
        for (Appliance* device : shedCandidates) {
            if (currentUsage <= source.safePowerLimit() && source.predictedBatterySocPercent(currentUsage) > source.criticalSocPercent) {
                break;
            }
            device->running = false;
            device->currentState = ApplianceState::CoolingDown;
            device->cooldownState = true;
            currentUsage -= device->powerWatts;
            std::cout << "- shed " << device->name << " to protect battery/solar reserve" << std::endl;
        }
    }

    void handleUserRequest(const std::string& action, const std::string& targetName,
                           std::vector<std::string>& acceptedOnRequests,
                           std::vector<std::string>& acceptedOffRequests) {
        Appliance* target = nullptr;
        for (Appliance& device : devices) {
            if (device.name == targetName) {
                target = &device;
                break;
            }
        }

        if (target == nullptr) {
            std::cout << "- request rejected: appliance " << targetName << " not found" << std::endl;
            return;
        }

        if (action == "ON") {
            target->requestedState = RequestedState::RequestOn;
            double currentUsage = demandTracker.currentUsage(devices);
            double remainingPower = source.safePowerLimit() - currentUsage;
            if (constraintGuard.canAccept(*target, source, remainingPower)) {
                target->running = true;
                target->mandatory = true;
                target->userLocked = true;
                target->currentState = target->startupDelaySeconds > 0.0 ? ApplianceState::Starting : ApplianceState::On;
                acceptedOnRequests.push_back(target->name);
                std::cout << "- ON accepted: " << target->name << std::endl;
            } else {
                std::vector<Appliance*> removals;
                for (Appliance& device : devices) {
                    if (!device.running || device.userLocked || device.mandatory) {
                        continue;
                    }
                    if (device.priority < target->priority && constraintGuard.canTurnOff(device).feasible) {
                        removals.push_back(&device);
                    }
                }
                std::sort(removals.begin(), removals.end(), [](const Appliance* lhs, const Appliance* rhs) {
                    return lhs->priority < rhs->priority;
                });

                for (Appliance* device : removals) {
                    device->running = false;
                    device->mandatory = false;
                    device->userLocked = false;
                    device->currentState = ApplianceState::CoolingDown;
                    std::cout << "- auto-off: " << device->name << " to satisfy user ON request" << std::endl;
                    currentUsage = demandTracker.currentUsage(devices);
                    remainingPower = source.safePowerLimit() - currentUsage;
                    if (constraintGuard.canAccept(*target, source, remainingPower)) {
                        break;
                    }
                }

                currentUsage = demandTracker.currentUsage(devices);
                remainingPower = source.safePowerLimit() - currentUsage;
                if (constraintGuard.canAccept(*target, source, remainingPower)) {
                    target->running = true;
                    target->mandatory = true;
                    target->userLocked = true;
                    target->currentState = target->startupDelaySeconds > 0.0 ? ApplianceState::Starting : ApplianceState::On;
                    acceptedOnRequests.push_back(target->name);
                    std::cout << "- ON accepted after power reallocation: " << target->name << std::endl;
                } else {
                    std::cout << "- ON rejected: " << target->name << " because constraints still cannot be satisfied" << std::endl;
                }
            }
        } else if (action == "OFF") {
            target->requestedState = RequestedState::RequestOff;
            ConstraintResult turnOff = constraintGuard.canTurnOff(*target);
            if (!turnOff.feasible && target->running) {
                std::cout << "- OFF rejected: " << target->name << " because " << turnOff.reason << std::endl;
                return;
            }
            target->running = false;
            target->mandatory = false;
            target->userLocked = true;
            target->currentState = ApplianceState::CoolingDown;
            target->cooldownState = true;
            acceptedOffRequests.push_back(target->name);
            std::cout << "- OFF accepted: " << target->name << std::endl;
        }
    }

    void printSection(const std::string& heading) const {
        std::cout << "\n" << heading << std::endl;
    }

private:
    SourceSnapshot source;
    DemandTracker demandTracker;
    ConstraintGuard constraintGuard;
    AStarPlanner planner;
    std::vector<Appliance> devices;
};

int main() {
    EnergyManagementSystem system;
    system.run();
    return 0;
}

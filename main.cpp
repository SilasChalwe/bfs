#include <algorithm>
#include <array>
#include <cstdint>
#include <cmath>
#include <iostream>
#include <limits>
#include <queue>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace firmware {

constexpr std::size_t kMaxAppliances = 12;
constexpr std::size_t kMaxRelations = 4;
constexpr double kEpsilon = 0.0001;

enum class RequestedState : uint8_t {
    None,
    UserOn,
    UserOff,
};

struct RelationList {
    std::array<int, kMaxRelations> ids{};
    uint8_t count = 0;

    void add(int applianceId) {
        if (count < ids.size()) {
            ids[count++] = applianceId;
        }
    }

    bool contains(int applianceId) const {
        for (uint8_t i = 0; i < count; ++i) {
            if (ids[i] == applianceId) {
                return true;
            }
        }
        return false;
    }
};

class Appliance {
public:
    Appliance(int id, const char* name, int relayPin, bool relayActiveHigh, uint8_t sensorIndex,
              double voltage, double runPowerWatts, double startupSurgeWatts, uint16_t startupDelayMs,
              uint32_t runtimeSeconds, double energyConsumedWh, uint8_t priority,
              double userPreferenceScore, double usefulnessScore, bool currentState,
              bool mandatory, bool userLocked)
        : id(id), name(name), relayPin(relayPin), relayActiveHigh(relayActiveHigh),
          sensorIndex(sensorIndex), voltage(voltage), runPowerWatts(runPowerWatts),
          startupSurgeWatts(startupSurgeWatts), startupDelayMs(startupDelayMs),
          runtimeSeconds(runtimeSeconds), energyConsumedWh(energyConsumedWh),
          priority(priority), userPreferenceScore(userPreferenceScore),
          usefulnessScore(usefulnessScore), mandatory(mandatory), userLocked(userLocked),
          currentState(currentState), requestedState(RequestedState::None) {}

    double turnOnPowerWatts() const {
        return currentState ? runPowerWatts : std::max(runPowerWatts, startupSurgeWatts);
    }

    double currentDrawAmps(double systemVoltage) const {
        return runPowerWatts / std::max(1.0, systemVoltage);
    }

    int id;
    const char* name;
    int relayPin;
    bool relayActiveHigh;
    uint8_t sensorIndex;
    double voltage;
    double runPowerWatts;
    double startupSurgeWatts;
    uint16_t startupDelayMs;
    uint32_t runtimeSeconds;
    double energyConsumedWh;
    uint8_t priority;
    double userPreferenceScore;
    double usefulnessScore;
    bool mandatory;
    bool userLocked;
    bool currentState;
    RequestedState requestedState;
    RelationList dependencies;
    RelationList conflicts;
};

class SolarSource {
public:
    double voltage = 24.0;
    double current = 0.0;
    double powerWatts = 0.0;
    double energyTodayWh = 0.0;
    double forecastWh = 0.0;

    void readSolarSensors() {
        // ESP32 placeholder: read PV voltage/current from ADC/INA219/MPPT telemetry here.
    }
};

class Battery {
public:
    double voltage = 24.0;
    double current = 0.0;
    double capacityWh = 1200.0;
    double remainingEnergyWh = 0.0;
    double socPercent = 0.0;
    double healthPercent = 100.0;
    double maxDischargePowerWatts = 0.0;
    double criticalSocPercent = 20.0;

    void readBatterySensors() {
        // ESP32 placeholder: read pack voltage/current/SOC from BMS or INA219 here.
    }

    double reserveEnergyWh() const {
        return capacityWh * (criticalSocPercent / 100.0);
    }

    double usableEnergyWh() const {
        return std::max(0.0, remainingEnergyWh - reserveEnergyWh());
    }

    double allowableDischargePowerWatts() const {
        if (socPercent <= criticalSocPercent || usableEnergyWh() <= 0.0) {
            return 0.0;
        }
        const double healthLimitedPower = maxDischargePowerWatts * std::max(0.0, healthPercent / 100.0);
        return std::max(0.0, healthLimitedPower);
    }

    bool critical() const {
        return socPercent <= criticalSocPercent;
    }
};

struct SystemLimits {
    double inverterLimitWatts = 0.0;
    double wiringLimitWatts = 0.0;
    double currentLimitAmps = 0.0;
    double systemVoltage = 24.0;

    double currentPowerLimitWatts() const {
        return currentLimitAmps * systemVoltage;
    }
};

class ConstraintGuard {
public:
    double availablePowerWatts(const SolarSource& solar, const Battery& battery,
                               const SystemLimits& limits) const {
        double sourcePower = solar.powerWatts + battery.allowableDischargePowerWatts();
        sourcePower = std::min(sourcePower, limits.inverterLimitWatts);
        sourcePower = std::min(sourcePower, limits.wiringLimitWatts);
        sourcePower = std::min(sourcePower, limits.currentPowerLimitWatts());
        return std::max(0.0, sourcePower);
    }

    bool canAccept(const Appliance& candidate, const SolarSource& solar, const Battery& battery,
                   const SystemLimits& limits, double remainingPower, double selectedLoad = 0.0) const {
        return voltageMatches(candidate, limits) && currentFits(candidate, limits) &&
               powerHeadroomExists(candidate, remainingPower) &&
               sourcePathExists(candidate, solar, battery) &&
               aggregateCurrentFits(candidate, limits, selectedLoad);
    }

private:
    bool voltageMatches(const Appliance& candidate, const SystemLimits& limits) const {
        return candidate.voltage <= limits.systemVoltage * 1.10;
    }

    bool currentFits(const Appliance& candidate, const SystemLimits& limits) const {
        return candidate.currentDrawAmps(limits.systemVoltage) <= limits.currentLimitAmps;
    }

    bool aggregateCurrentFits(const Appliance& candidate, const SystemLimits& limits,
                              double selectedLoad) const {
        const double draw = (selectedLoad + candidate.turnOnPowerWatts()) / std::max(1.0, limits.systemVoltage);
        return draw <= limits.currentLimitAmps;
    }

    bool powerHeadroomExists(const Appliance& candidate, double remainingPower) const {
        return candidate.turnOnPowerWatts() <= remainingPower + kEpsilon;
    }

    bool sourcePathExists(const Appliance& candidate, const SolarSource& solar, const Battery& battery) const {
        return candidate.turnOnPowerWatts() <= solar.powerWatts + battery.allowableDischargePowerWatts() + kEpsilon;
    }
};

class DemandTracker {
public:
    double currentUsage(const std::vector<Appliance>& appliances) const {
        double total = 0.0;
        for (const Appliance& appliance : appliances) {
            if (appliance.currentState) {
                total += appliance.runPowerWatts;
            }
        }
        return total;
    }
};

struct SearchNode {
    std::array<int8_t, kMaxAppliances> assignment{};
    double load = 0.0;
    double gCost = 0.0;
    double hCost = 0.0;
    double fCost = 0.0;
    uint8_t nextIndex = 0;
    uint8_t depth = 0;
};

struct SearchNodeCompare {
    bool operator()(const SearchNode& lhs, const SearchNode& rhs) const {
        if (std::abs(lhs.fCost - rhs.fCost) > kEpsilon) {
            return lhs.fCost > rhs.fCost;
        }
        if (std::abs(lhs.hCost - rhs.hCost) > kEpsilon) {
            return lhs.hCost > rhs.hCost;
        }
        return lhs.depth < rhs.depth;
    }
};

class AStarPlanner {
public:
    std::vector<int> plan(const std::vector<Appliance>& candidates, const SolarSource& solar,
                          const Battery& battery, const SystemLimits& limits,
                          double remainingPower, const ConstraintGuard& guard) {
        std::vector<int> bestSelection;
        bestSelection.reserve(candidates.size());
        if (candidates.empty()) {
            return bestSelection;
        }

        std::priority_queue<SearchNode, std::vector<SearchNode>, SearchNodeCompare> open;
        std::unordered_map<uint32_t, double> bestGCost;
        std::unordered_set<uint32_t> closed;
        bestGCost.reserve(1U << std::min<std::size_t>(candidates.size(), 10));
        closed.reserve(bestGCost.bucket_count());

        SearchNode start;
        start.assignment.fill(-1);
        start.hCost = estimateFutureCost(start, candidates, solar, battery, remainingPower);
        start.fCost = start.gCost + start.hCost;
        open.push(start);
        bestGCost[stateKey(start)] = start.gCost;

        double bestGoalCost = std::numeric_limits<double>::infinity();
        bool hasBestGoal = false;
        std::size_t expanded = 0;
        std::size_t pruned = 0;

        while (!open.empty()) {
            SearchNode node = open.top();
            open.pop();

            const uint32_t key = stateKey(node);
            const auto known = bestGCost.find(key);
            if (known != bestGCost.end() && node.gCost > known->second + kEpsilon) {
                ++pruned;
                continue;
            }
            if (closed.find(key) != closed.end()) {
                ++pruned;
                continue;
            }
            closed.insert(key);
            ++expanded;

            if (hasBestGoal && node.fCost >= bestGoalCost - kEpsilon) {
                ++pruned;
                break;
            }

            if (node.nextIndex >= candidates.size()) {
                if (node.gCost < bestGoalCost) {
                    bestGoalCost = node.gCost;
                    hasBestGoal = true;
                    bestSelection = decodeSelection(node, candidates.size());
                }
                continue;
            }

            expandChoice(node, 0, candidates, solar, battery, limits, remainingPower, guard,
                         open, bestGCost, pruned);
            expandChoice(node, 1, candidates, solar, battery, limits, remainingPower, guard,
                         open, bestGCost, pruned);
        }

        std::cout << "- OPEN exhausted/pruned with CLOSED states: " << closed.size() << '\n';
        std::cout << "- A* expanded states: " << expanded << '\n';
        std::cout << "- A* pruned states: " << pruned << '\n';
        if (hasBestGoal) {
            std::cout << "- best goal g(n): " << bestGoalCost << '\n';
        }
        return bestSelection;
    }

private:
    void expandChoice(const SearchNode& node, int8_t choice, const std::vector<Appliance>& candidates,
                      const SolarSource& solar, const Battery& battery, const SystemLimits& limits,
                      double remainingPower, const ConstraintGuard& guard,
                      std::priority_queue<SearchNode, std::vector<SearchNode>, SearchNodeCompare>& open,
                      std::unordered_map<uint32_t, double>& bestGCost, std::size_t& pruned) const {
        const Appliance& device = candidates[node.nextIndex];
        SearchNode child = node;
        child.assignment[node.nextIndex] = choice;
        child.nextIndex = node.nextIndex + 1;
        child.depth = node.depth + 1;

        if (choice == 1) {
            if (!relationsSatisfied(child, candidates, node.nextIndex)) {
                ++pruned;
                return;
            }
            const double availableForDevice = remainingPower - node.load;
            if (!guard.canAccept(device, solar, battery, limits, availableForDevice, node.load)) {
                ++pruned;
                return;
            }
            child.load += device.turnOnPowerWatts();
        }

        child.gCost += decisionCost(device, choice, solar, battery, remainingPower - child.load);
        child.hCost = estimateFutureCost(child, candidates, solar, battery, remainingPower - child.load);
        child.fCost = child.gCost + child.hCost;

        const uint32_t key = stateKey(child);
        const auto existing = bestGCost.find(key);
        if (existing != bestGCost.end() && child.gCost >= existing->second - kEpsilon) {
            ++pruned;
            return;
        }
        bestGCost[key] = child.gCost;
        open.push(child);
    }

    bool relationsSatisfied(const SearchNode& node, const std::vector<Appliance>& candidates,
                            std::size_t selectedIndex) const {
        const Appliance& selected = candidates[selectedIndex];
        for (std::size_t i = 0; i <= selectedIndex; ++i) {
            if (node.assignment[i] != 1) {
                continue;
            }
            if (i != selectedIndex &&
                (selected.conflicts.contains(candidates[i].id) || candidates[i].conflicts.contains(selected.id))) {
                return false;
            }
        }
        for (uint8_t i = 0; i < selected.dependencies.count; ++i) {
            const int requiredId = selected.dependencies.ids[i];
            bool foundAndOn = false;
            for (std::size_t j = 0; j < candidates.size(); ++j) {
                if (candidates[j].id == requiredId) {
                    foundAndOn = j < node.nextIndex && node.assignment[j] == 1;
                    break;
                }
            }
            if (!foundAndOn) {
                return false;
            }
        }
        return true;
    }

    double decisionCost(const Appliance& device, int8_t choice, const SolarSource& solar,
                        const Battery& battery, double remainingPowerAfterChoice) const {
        if (choice == 0) {
            return missedServiceCost(device);
        }

        const double solarShare = std::min(device.runPowerWatts, solar.powerWatts);
        const double batteryShare = std::max(0.0, device.runPowerWatts - solarShare);
        const double batteryPressure = batteryShare / std::max(1.0, battery.allowableDischargePowerWatts());
        const double reservePressure = battery.usableEnergyWh() <= 0.0
                                           ? 3.0
                                           : batteryShare / std::max(1.0, battery.usableEnergyWh());
        const double healthPenalty = (100.0 - battery.healthPercent) / 100.0;
        const double startupPenalty = std::max(0.0, device.startupSurgeWatts - device.runPowerWatts) /
                                      std::max(1.0, solar.powerWatts + battery.allowableDischargePowerWatts());
        const double remainingPowerReward = std::max(0.0, remainingPowerAfterChoice) /
                                            std::max(1.0, solar.powerWatts + battery.allowableDischargePowerWatts());
        const double solarUtilizationReward = solarShare / std::max(1.0, device.runPowerWatts);
        const double preferenceReward = device.userPreferenceScore / 10.0;
        const double priorityReward = static_cast<double>(device.priority) / 100.0;
        const double usefulnessReward = device.usefulnessScore / 10.0;

        return (batteryPressure * 0.80) + (reservePressure * 1.10) + (healthPenalty * 0.45) +
               (startupPenalty * 0.60) - (solarUtilizationReward * 1.10) -
               (preferenceReward * 0.55) - (priorityReward * 0.80) -
               (usefulnessReward * 0.70) - (remainingPowerReward * 0.20);
    }

    double missedServiceCost(const Appliance& device) const {
        return 0.05 + (static_cast<double>(device.priority) / 100.0) * 0.10 +
               (device.userPreferenceScore / 10.0) * 0.10 + (device.usefulnessScore / 10.0) * 0.10;
    }

    double estimateFutureCost(const SearchNode& node, const std::vector<Appliance>& candidates,
                              const SolarSource& solar, const Battery& battery,
                              double remainingPower) const {
        double lowerBound = 0.0;
        for (std::size_t i = node.nextIndex; i < candidates.size(); ++i) {
            const Appliance& device = candidates[i];
            if (device.turnOnPowerWatts() <= remainingPower + kEpsilon) {
                lowerBound += std::min(0.0, decisionCost(device, 1, solar, battery,
                                                         remainingPower - device.turnOnPowerWatts()));
            }
        }
        return lowerBound;
    }

    uint32_t stateKey(const SearchNode& node) const {
        uint32_t key = node.nextIndex;
        for (uint8_t i = 0; i < node.nextIndex; ++i) {
            key <<= 1U;
            key |= node.assignment[i] == 1 ? 1U : 0U;
        }
        return key;
    }

    std::vector<int> decodeSelection(const SearchNode& node, std::size_t candidateCount) const {
        std::vector<int> selection;
        selection.reserve(candidateCount);
        for (std::size_t i = 0; i < candidateCount; ++i) {
            if (node.assignment[i] == 1) {
                selection.push_back(static_cast<int>(i));
            }
        }
        return selection;
    }
};

class EnergyManagementSystem {
public:
    EnergyManagementSystem() {
        appliances.reserve(kMaxAppliances);
        seedHardwareConfiguration();
        updateMeasurements();
    }

    void run() {
        printInitialState();
        updateMeasurements();
        printMeasurements();
        shedLoadsIfBatteryCritical();
        printCurrentUsage();
        processUserRequests();
        const double remainingPower = calculateRemainingPower();
        std::vector<Appliance> optional = buildAStarSearchSpace(remainingPower);
        runPlanner(optional, remainingPower);
        applyRelayStates();
        publishTelemetry();
        printFinalState();
    }

    void readSolarSensors() { solar.readSolarSensors(); }
    void readBatterySensors() { battery.readBatterySensors(); }

    void updateMeasurements() {
        readSolarSensors();
        readBatterySensors();
        solar.voltage = 24.8;
        solar.current = 13.2;
        solar.powerWatts = solar.voltage * solar.current;
        solar.energyTodayWh = 1450.0;
        solar.forecastWh = 2300.0;

        battery.voltage = 25.1;
        battery.current = -3.8;
        battery.capacityWh = 1200.0;
        battery.remainingEnergyWh = 780.0;
        battery.socPercent = 65.0;
        battery.healthPercent = 94.0;
        battery.maxDischargePowerWatts = 220.0;
        battery.criticalSocPercent = 22.0;
    }

    void applyRelayStates() {
        for (const Appliance& appliance : appliances) {
            const bool relayLevel = appliance.relayActiveHigh ? appliance.currentState : !appliance.currentState;
            (void)relayLevel; // ESP32 placeholder: digitalWrite(appliance.relayPin, relayLevel).
        }
    }

    void publishTelemetry() {
        // ESP32 placeholder: publish telemetry over Serial, MQTT, BLE, or ESP-NOW.
    }

private:
    void seedHardwareConfiguration() {
        limits.inverterLimitWatts = 420.0;
        limits.wiringLimitWatts = 380.0;
        limits.currentLimitAmps = 18.0;
        limits.systemVoltage = 24.0;

        appliances.emplace_back(1, "Fridge", 16, true, 0, 24.0, 90.0, 150.0, 500, 1800, 310.0, 100, 9.5, 9.8, true, true, false);
        appliances.emplace_back(2, "WaterPump", 17, true, 1, 24.0, 95.0, 210.0, 1200, 0, 0.0, 95, 8.8, 9.0, false, false, false);
        appliances.emplace_back(3, "Lights", 18, true, 2, 24.0, 28.0, 28.0, 50, 0, 0.0, 60, 7.4, 7.1, false, false, false);
        appliances.emplace_back(4, "VentFan", 19, true, 3, 24.0, 55.0, 80.0, 300, 0, 0.0, 45, 6.6, 6.4, false, false, false);
        appliances.emplace_back(5, "Router", 21, true, 4, 24.0, 18.0, 18.0, 20, 0, 0.0, 80, 8.0, 8.6, false, false, false);
        appliances.emplace_back(6, "WorkshopOutlet", 22, true, 5, 24.0, 130.0, 180.0, 800, 0, 0.0, 25, 4.2, 5.0, false, false, false);

        appliances[5].conflicts.add(2);
    }

    void printInitialState() const {
        std::cout << "\n1. INITIAL EMBEDDED APPLIANCE STATE\n";
        for (const Appliance& appliance : appliances) {
            std::cout << "- " << appliance.name << " | relayPin=" << appliance.relayPin
                      << " | sensor=" << static_cast<int>(appliance.sensorIndex)
                      << " | power=" << appliance.runPowerWatts << " W | state="
                      << (appliance.currentState ? "ON" : "OFF")
                      << " | mandatory=" << (appliance.mandatory ? "true" : "false")
                      << " | locked=" << (appliance.userLocked ? "true" : "false") << '\n';
        }
    }

    void printMeasurements() const {
        std::cout << "\n2. SOLAR PV + BATTERY MEASUREMENTS\n";
        std::cout << "- solar voltage/current/power: " << solar.voltage << " V / " << solar.current
                  << " A / " << solar.powerWatts << " W\n";
        std::cout << "- solar energy today/forecast: " << solar.energyTodayWh << " Wh / "
                  << solar.forecastWh << " Wh\n";
        std::cout << "- battery voltage/current/SOC: " << battery.voltage << " V / "
                  << battery.current << " A / " << battery.socPercent << "%\n";
        std::cout << "- battery remaining/capacity/health: " << battery.remainingEnergyWh << " Wh / "
                  << battery.capacityWh << " Wh / " << battery.healthPercent << "%\n";
        std::cout << "- safe available PV+battery power: " << systemPowerLimit() << " W\n";
    }

    void printCurrentUsage() const {
        std::cout << "\n3. CURRENT USAGE\n";
        std::cout << "- current watt usage: " << demandTracker.currentUsage(appliances) << " W\n";
        std::cout << "- remaining power: " << calculateRemainingPower() << " W\n";
    }

    void processUserRequests() {
        std::cout << "\n4. USER REQUEST PROCESSING\n";
        requestOn(2);
        requestOff(3);
    }

    void requestOn(int applianceId) {
        Appliance* target = findAppliance(applianceId);
        if (target == nullptr) {
            std::cout << "- ON rejected: id " << applianceId << " not found\n";
            return;
        }
        target->requestedState = RequestedState::UserOn;
        if (guard.canAccept(*target, solar, battery, limits, calculateRemainingPower())) {
            target->currentState = true;
            target->mandatory = true;
            target->userLocked = true;
            std::cout << "- user ON accepted: " << target->name << '\n';
            return;
        }
        std::cout << "- user ON rejected by PV/battery constraints: " << target->name << '\n';
    }

    void requestOff(int applianceId) {
        Appliance* target = findAppliance(applianceId);
        if (target == nullptr) {
            std::cout << "- OFF rejected: id " << applianceId << " not found\n";
            return;
        }
        target->requestedState = RequestedState::UserOff;
        target->currentState = false;
        target->mandatory = false;
        target->userLocked = true;
        std::cout << "- user OFF accepted: " << target->name << '\n';
    }

    void shedLoadsIfBatteryCritical() {
        if (!battery.critical()) {
            return;
        }
        std::cout << "\nAutomatic load shedding: battery critical\n";
        for (Appliance& appliance : appliances) {
            if (appliance.currentState && !appliance.mandatory && !appliance.userLocked) {
                appliance.currentState = false;
                std::cout << "- shed: " << appliance.name << '\n';
            }
        }
    }

    std::vector<Appliance> buildAStarSearchSpace(double remainingPower) const {
        std::cout << "\n5. A* INPUT\n";
        std::vector<Appliance> optional;
        optional.reserve(appliances.size());
        for (const Appliance& appliance : appliances) {
            if (appliance.currentState || appliance.mandatory || appliance.userLocked) {
                continue;
            }
            if (guard.canAccept(appliance, solar, battery, limits, remainingPower)) {
                optional.push_back(appliance);
                std::cout << "- " << appliance.name << " | run=" << appliance.runPowerWatts
                          << " W | surge=" << appliance.startupSurgeWatts
                          << " W | priority=" << static_cast<int>(appliance.priority)
                          << " | pref=" << appliance.userPreferenceScore << '\n';
            }
        }
        if (optional.empty()) {
            std::cout << "- none\n";
        }
        return optional;
    }

    void runPlanner(const std::vector<Appliance>& optional, double remainingPower) {
        std::cout << "\n6. A* RESULT\n";
        const std::vector<int> chosen = planner.plan(optional, solar, battery, limits, remainingPower, guard);
        if (chosen.empty()) {
            std::cout << "- no optional loads selected\n";
        }
        for (int index : chosen) {
            Appliance* appliance = findAppliance(optional[static_cast<std::size_t>(index)].id);
            if (appliance != nullptr) {
                appliance->currentState = true;
                std::cout << "- selected by A*: " << appliance->name << '\n';
            }
        }
    }

    void printFinalState() const {
        std::cout << "\n7. FINAL RELAY STATE\n";
        for (const Appliance& appliance : appliances) {
            std::cout << "- " << appliance.name << " => " << (appliance.currentState ? "ON" : "OFF")
                      << " | GPIO=" << appliance.relayPin
                      << " | mandatory=" << (appliance.mandatory ? "true" : "false")
                      << " | locked=" << (appliance.userLocked ? "true" : "false") << '\n';
        }
    }

    Appliance* findAppliance(int applianceId) {
        for (Appliance& appliance : appliances) {
            if (appliance.id == applianceId) {
                return &appliance;
            }
        }
        return nullptr;
    }

    double systemPowerLimit() const {
        return guard.availablePowerWatts(solar, battery, limits);
    }

    double calculateRemainingPower() const {
        return std::max(0.0, systemPowerLimit() - demandTracker.currentUsage(appliances));
    }

    SolarSource solar;
    Battery battery;
    SystemLimits limits;
    ConstraintGuard guard;
    DemandTracker demandTracker;
    AStarPlanner planner;
    std::vector<Appliance> appliances;
};

} // namespace firmware

int main() {
    firmware::EnergyManagementSystem ems;
    ems.run();
    return 0;
}

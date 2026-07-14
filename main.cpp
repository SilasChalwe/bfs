#include <cmath>
#include <cstddef>
#include <iostream>
#include <queue>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

class Appliance {
public:
    Appliance(int id, std::string name, double voltage, double powerWatts, int priority,
              double usefulnessScore, bool running, bool mandatory, bool userLocked = false)
        : id(id), name(std::move(name)), voltage(voltage), powerWatts(powerWatts), priority(priority),
          usefulnessScore(usefulnessScore), running(running), mandatory(mandatory), userLocked(userLocked) {}

    int id;
    std::string name;
    double voltage;
    double powerWatts;
    int priority;
    double usefulnessScore;
    bool running;
    bool mandatory;
    bool userLocked;
};

struct SourceSnapshot {
    double solarVoltage = 24.0;
    double solarCurrent = 8.0;
    double solarPowerProductionWatts = 320.0;
    double solarEnergyProductionWh = 1200.0;

    double batteryVoltage = 24.0;
    double batteryCurrent = 3.0;
    double batteryEnergyRemainingWh = 900.0;
    double maxBatteryDischargePower = 180.0;

    bool gridAvailable = false;
    double gridPowerLimit = 0.0;

    double inverterLimitWatts = 400.0;
    double wiringLimitWatts = 350.0;
    double systemVoltage = 24.0;
    double currentLimitAmps = 25.0;

    double safePowerLimit() const {
        double sourceCapacity = solarPowerProductionWatts + maxBatteryDischargePower;
        double limit = std::min(sourceCapacity, inverterLimitWatts);
        limit = std::min(limit, wiringLimitWatts);
        if (gridAvailable) {
            limit = std::min(limit, gridPowerLimit);
        }
        return std::max(0.0, limit);
    }
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
        if (!voltageMatches(candidate, source)) {
            return false;
        }
        if (!currentFits(candidate, source)) {
            return false;
        }
        if (!powerHeadroomExists(candidate, remainingPower)) {
            return false;
        }
        if (!batteryPathExists(candidate, source)) {
            return false;
        }
        return true;
    }

private:
    bool voltageMatches(const Appliance& candidate, const SourceSnapshot& source) const {
        return candidate.voltage <= source.systemVoltage * 1.1;
    }

    bool currentFits(const Appliance& candidate, const SourceSnapshot& source) const {
        double draw = candidate.powerWatts / std::max(1.0, source.systemVoltage);
        return draw <= source.currentLimitAmps;
    }

    bool powerHeadroomExists(const Appliance& candidate, double remainingPower) const {
        return candidate.powerWatts <= remainingPower;
    }

    bool batteryPathExists(const Appliance& candidate, const SourceSnapshot& source) const {
        return candidate.powerWatts <= source.maxBatteryDischargePower + source.solarPowerProductionWatts;
    }
};

struct SearchNode {
    std::vector<int> assignment;
    double load;
    double gCost;
    double hCost;
    double fCost;
    int nextIndex;
    int depth;
};

struct SearchNodeCompare {
    bool operator()(const SearchNode& lhs, const SearchNode& rhs) const {
        if (lhs.fCost != rhs.fCost) {
            return lhs.fCost > rhs.fCost;
        }
        if (lhs.hCost != rhs.hCost) {
            return lhs.hCost > rhs.hCost;
        }
        return lhs.depth > rhs.depth;
    }
};

class AStarPlanner {
public:
    std::vector<int> plan(const std::vector<Appliance>& candidates, const SourceSnapshot& source,
                          double remainingPower, const ConstraintGuard& ruleSet) {
        if (candidates.empty()) {
            return {};
        }

        std::priority_queue<SearchNode, std::vector<SearchNode>, SearchNodeCompare> frontier;
        std::unordered_map<std::string, double> bestSeen;
        std::vector<SearchNode> closed;
        std::vector<int> bestSelection;
        double bestCost = 1e18;
        int evaluatedStateCount = 0;

        SearchNode start;
        start.assignment.assign(candidates.size(), -1);
        start.load = 0.0;
        start.gCost = 0.0;
        start.hCost = estimateFutureCost(start, candidates, source, remainingPower);
        start.fCost = start.gCost + start.hCost;
        start.nextIndex = 0;
        start.depth = 0;

        frontier.push(start);
        bestSeen[stateKey(start)] = start.gCost;

        while (!frontier.empty()) {
            SearchNode node = frontier.top();
            frontier.pop();

            const std::string key = stateKey(node);
            if (isClosed(key, closed)) {
                continue;
            }
            closed.push_back(node);

            if (node.nextIndex >= static_cast<int>(candidates.size())) {
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
                std::cout << "- g(n): " << node.gCost << std::endl;
                std::cout << "- h(n): " << node.hCost << std::endl;
                std::cout << "- f(n): " << node.fCost << std::endl;

                if (node.gCost < bestCost) {
                    bestCost = node.gCost;
                    bestSelection = finalSelection;
                }
                continue;
            }

            for (int choice : {0, 1}) {
                SearchNode child = node;
                const Appliance& device = candidates[node.nextIndex];
                child.assignment[node.nextIndex] = choice;
                child.nextIndex = node.nextIndex + 1;
                child.depth = node.depth + 1;

                if (choice == 1) {
                    double availablePower = remainingPower - node.load;
                    if (!ruleSet.canAccept(device, source, availablePower)) {
                        continue;
                    }
                    child.load = node.load + device.powerWatts;
                } else {
                    child.load = node.load;
                }

                child.gCost = node.gCost + transitionCost(device, choice, source);
                child.hCost = estimateFutureCost(child, candidates, source, std::max(0.0, remainingPower - child.load));
                child.fCost = child.gCost + child.hCost;

                if (choice == 1) {
                    std::cout << "State: " << device.name << " ON" << std::endl;
                    std::cout << "- Power: " << device.powerWatts << "W" << std::endl;
                    std::cout << "- g(n): " << child.gCost << std::endl;
                    std::cout << "- h(n): " << child.hCost << std::endl;
                    std::cout << "- f(n): " << child.fCost << std::endl;
                    ++evaluatedStateCount;
                }

                const std::string childKey = stateKey(child);
                auto existing = bestSeen.find(childKey);
                if (existing != bestSeen.end() && child.gCost >= existing->second) {
                    continue;
                }
                bestSeen[childKey] = child.gCost;
                frontier.push(child);
            }
        }

        return bestSelection;
    }

private:
    double transitionCost(const Appliance& device, int choice, const SourceSnapshot& source) const {
        if (choice == 0) {
            return 0.0;
        }

        double energyUsageCost = (device.powerWatts / std::max(1.0, source.safePowerLimit())) * 0.45;
        double batteryUsageCost = (device.powerWatts / std::max(1.0, source.maxBatteryDischargePower)) * 0.25;
        double electricityCost = source.gridAvailable ? (device.powerWatts / 1000.0) * 0.18 : 0.0;
        double sourceStressCost = sourceStress(device, source) * 0.30;

        double priorityReward = (static_cast<double>(device.priority) / 100.0) * 2.0;
        double usefulnessReward = (device.usefulnessScore / 10.0) * 2.5;
        double serviceReward = 0.25;
        double utilizationReward = (device.powerWatts / std::max(1.0, source.safePowerLimit())) * 0.70;

        return energyUsageCost + batteryUsageCost + electricityCost + sourceStressCost -
               (priorityReward + usefulnessReward + serviceReward + utilizationReward);
    }

    double estimateFutureCost(const SearchNode& node, const std::vector<Appliance>& candidates,
                              const SourceSnapshot& source, double remainingPower) const {
        double estimate = 0.0;
        for (std::size_t i = node.nextIndex; i < candidates.size(); ++i) {
            const Appliance& device = candidates[i];
            double fitRatio = remainingPower > 0.0 ? std::min(device.powerWatts, remainingPower) / std::max(1.0, remainingPower) : 0.0;
            double remainingEnergyCost = (device.powerWatts / std::max(1.0, source.safePowerLimit())) * 0.20;
            double expectedBatteryImpact = (device.powerWatts / std::max(1.0, source.maxBatteryDischargePower)) * 0.12;
            double expectedSolarRelief = (source.solarPowerProductionWatts / std::max(1.0, source.safePowerLimit())) * 0.08;
            double futureUsefulnessReward = ((static_cast<double>(device.priority) / 100.0) +
                                             (device.usefulnessScore / 10.0)) * fitRatio * 0.18;
            double remainingPenalty = remainingEnergyCost + expectedBatteryImpact - expectedSolarRelief - futureUsefulnessReward;
            estimate += std::max(0.0, remainingPenalty);
        }
        return std::max(0.0, estimate);
    }

    double sourceStress(const Appliance& device, const SourceSnapshot& source) const {
        double solarShortfall = std::max(0.0, device.powerWatts - source.solarPowerProductionWatts);
        double batteryPressure = solarShortfall / std::max(1.0, source.maxBatteryDischargePower);
        double currentPressure = (device.powerWatts / std::max(1.0, source.systemVoltage)) /
                                 std::max(1.0, source.currentLimitAmps);
        return batteryPressure + currentPressure;
    }

    bool isClosed(const std::string& key, const std::vector<SearchNode>& closed) const {
        for (const SearchNode& seen : closed) {
            if (stateKey(seen) == key) {
                return true;
            }
        }
        return false;
    }

    std::string stateKey(const SearchNode& node) const {
        std::ostringstream stream;
        stream << node.nextIndex << ":";
        for (int value : node.assignment) {
            stream << value << ",";
        }
        stream << "|" << static_cast<int>(std::round(node.load * 100.0));
        return stream.str();
    }

    std::vector<int> decodeSelection(const std::vector<int>& assignment) const {
        std::vector<int> selection;
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
        printSection("1. INITIAL SYSTEM STATE");
        for (const Appliance& device : devices) {
            std::cout << "- " << device.name << " | power=" << device.powerWatts << " W"
                      << " | running=" << (device.running ? "true" : "false")
                      << " | mandatory=" << (device.mandatory ? "true" : "false") << std::endl;
        }

        printSection("2. SYSTEM MEASUREMENTS");
        std::cout << "- solar production: " << source.solarPowerProductionWatts << " W" << std::endl;
        std::cout << "- battery energy remaining: " << source.batteryEnergyRemainingWh << " Wh" << std::endl;
        std::cout << "- system power limit: " << source.safePowerLimit() << " W" << std::endl;

        double currentUsage = demandTracker.currentUsage(devices);
        std::cout << "\n3. CURRENT USAGE" << std::endl;
        std::cout << "- current watt usage: " << currentUsage << " W" << std::endl;
        std::cout << "- remaining power: " << (source.safePowerLimit() - currentUsage) << " W" << std::endl;

        printSection("4. USER REQUEST PROCESSING");
        std::cout << "Before user commands:" << std::endl;
        for (const Appliance& device : devices) {
            std::cout << "- " << device.name << " | running=" << (device.running ? "true" : "false")
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
        for (Appliance& device : devices) {
            if (device.running || device.mandatory || device.userLocked) {
                continue;
            }
            if (constraintGuard.canAccept(device, source, remainingPower)) {
                optionalPool.push_back(device);
                std::cout << "- " << device.name << " (" << device.powerWatts << " W, priority="
                          << device.priority << ", usefulness=" << device.usefulnessScore << ")" << std::endl;
            }
        }

        std::cout << "\n6. A* RESULT" << std::endl;
        std::vector<int> chosen = planner.plan(optionalPool, source, remainingPower, constraintGuard);
        for (int index : chosen) {
            std::cout << "- selected: " << optionalPool[index].name << std::endl;
        }

        for (int index : chosen) {
            for (Appliance& device : devices) {
                if (device.id == optionalPool[index].id) {
                    device.running = true;
                    break;
                }
            }
        }

        printSection("7. FINAL SYSTEM STATE");
        for (const Appliance& device : devices) {
            std::cout << "- " << device.name << " => " << (device.running ? "ON" : "OFF")
                      << " | mandatory=" << (device.mandatory ? "true" : "false")
                      << " | userLocked=" << (device.userLocked ? "true" : "false") << std::endl;
        }
    }

private:
    void seedSource() {
        source.solarVoltage = 24.0;
        source.solarCurrent = 8.0;
        source.solarPowerProductionWatts = 320.0;
        source.solarEnergyProductionWh = 1200.0;
        source.batteryVoltage = 24.0;
        source.batteryCurrent = 3.0;
        source.batteryEnergyRemainingWh = 900.0;
        source.maxBatteryDischargePower = 180.0;
        source.gridAvailable = false;
        source.gridPowerLimit = 0.0;
        source.inverterLimitWatts = 400.0;
        source.wiringLimitWatts = 350.0;
        source.systemVoltage = 24.0;
        source.currentLimitAmps = 25.0;
    }

    void seedDevices() {
        devices.emplace_back(1, "Fridge", 24.0, 120.0, 100, 9.5, true, true);
        devices.emplace_back(2, "Pump", 24.0, 80.0, 95, 8.7, false, false);
        devices.emplace_back(3, "Lights", 24.0, 30.0, 40, 6.2, false, false);
        devices.emplace_back(4, "TV", 24.0, 140.0, 30, 7.8, false, false);
        devices.emplace_back(5, "Fan", 24.0, 60.0, 20, 5.4, false, false);
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
            double currentUsage = demandTracker.currentUsage(devices);
            double remainingPower = source.safePowerLimit() - currentUsage;
            if (constraintGuard.canAccept(*target, source, remainingPower)) {
                target->running = true;
                target->mandatory = true;
                target->userLocked = true;
                acceptedOnRequests.push_back(target->name);
                std::cout << "- ON accepted: " << target->name << std::endl;
            } else {
                std::vector<Appliance*> removals;
                for (Appliance& device : devices) {
                    if (!device.running || device.userLocked || device.mandatory) {
                        continue;
                    }
                    if (device.priority < target->priority) {
                        removals.push_back(&device);
                    }
                }

                for (Appliance* device : removals) {
                    device->running = false;
                    device->mandatory = false;
                    device->userLocked = false;
                    std::cout << "- auto-off: " << device->name << std::endl;
                }

                currentUsage = demandTracker.currentUsage(devices);
                remainingPower = source.safePowerLimit() - currentUsage;
                if (constraintGuard.canAccept(*target, source, remainingPower)) {
                    target->running = true;
                    target->mandatory = true;
                    target->userLocked = true;
                    acceptedOnRequests.push_back(target->name);
                    std::cout << "- ON accepted after power reallocation: " << target->name << std::endl;
                } else {
                    std::cout << "- ON rejected: " << target->name << std::endl;
                }
            }
        } else if (action == "OFF") {
            target->running = false;
            target->mandatory = false;
            target->userLocked = true;
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

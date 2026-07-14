#pragma once
class EnergyManagementSystem;
class ApiServer{ public: explicit ApiServer(EnergyManagementSystem& ems):ems_(ems){} void begin(); void loop(); private: EnergyManagementSystem& ems_; };

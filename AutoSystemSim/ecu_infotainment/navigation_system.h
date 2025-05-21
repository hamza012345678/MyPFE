// AutoSystemSim/ecu_infotainment/navigation_system.h
#ifndef NAVIGATION_SYSTEM_H
#define NAVIGATION_SYSTEM_H

#include "../common/logger.h"
#include "../common/datatypes.h" // For VehicleState (current speed, conceptually current location if we had it)
#include <string>
#include <thread>
#include <cmath>
#include <vector>
#include <chrono> // For ETA calculation

namespace ecu_infotainment {

// Simplified representation of a map point / coordinate
struct MapCoordinate {
    double latitude;
    double longitude;
    std::string name; // Optional name (e.g., "Home", "Work")

    bool isValid() const {
        return (latitude >= -90.0 && latitude <= 90.0 &&
                longitude >= -180.0 && longitude <= 180.0);
    }
    // Basic distance calculation (approximation, not Haversine)
    double distanceTo(const MapCoordinate& other) const {
        if (!isValid() || !other.isValid()) return -1.0; // Invalid input
        double lat_diff = latitude - other.latitude;
        double lon_diff = longitude - other.longitude;
        return std::sqrt(lat_diff * lat_diff + lon_diff * lon_diff) * 111.0; // Rough km, good enough for sim
    }
};

struct RouteSegment {
    std::string instruction; // e.g., "Turn left onto Main St"
    double distance_km;
    MapCoordinate end_point;
};

enum class GPSSignalStatus {
    NO_FIX,
    FIX_2D,
    FIX_3D,
    LOST_TEMPORARILY, // e.g., in a tunnel
    FAULTY
};

enum class NavigationStatus {
    IDLE,                   // No destination set
    ROUTE_CALCULATING,
    GUIDANCE_ACTIVE,
    RECALCULATING_ROUTE,    // Off-route
    DESTINATION_REACHED,
    ERROR_NO_GPS,
    ERROR_ROUTE_FAILED
};

class NavigationSystem {
public:
    NavigationSystem();
    ~NavigationSystem();

    // --- Destination & Routing ---
    bool setDestination(const MapCoordinate& dest, const std::string& dest_name = "");
    bool setDestinationByAddress(const std::string& address); // Simulates address lookup
    bool cancelNavigation();

    // --- Guidance ---
    NavigationStatus getCurrentNavigationStatus() const;
    RouteSegment getCurrentGuidanceInstruction() const; // Next maneuver
    double getDistanceToNextManeuverKm() const;
    double getDistanceToDestinationKm() const;
    std::chrono::seconds getEstimatedTimeOfArrivalSeconds() const;

    // --- GPS & Map ---
    GPSSignalStatus getGPSSignalStatus() const;
    MapCoordinate getCurrentLocation() const; // Simulated current location
    bool isMapDataAvailable() const; // Simulates map data presence

    // --- Periodic Update ---
    // vehicle_state for current speed and to simulate location changes
    void updateNavigationState(const VehicleState& vehicle_state);

private:
    NavigationStatus nav_status_;
    GPSSignalStatus gps_status_;
    bool map_data_loaded_; // Simulates if map data (e.g. from SD card/HDD) is available

    MapCoordinate current_location_; // Simulated current GPS location
    MapCoordinate destination_;
    std::string destination_name_;

    std::vector<RouteSegment> current_route_;
    int current_route_segment_index_;

    std::chrono::steady_clock::time_point route_start_time_;
    std::chrono::seconds initial_eta_seconds_;

    // Internal helpers
    void simulateGPSFix();
    bool calculateRoute(); // Simplified route calculation
    void provideGuidanceUpdate(double speed_kmh);
    void checkOffRoute(double speed_kmh);
    void simulateLocationUpdate(double speed_kmh, double heading_degrees, std::chrono::seconds time_delta);
    MapCoordinate findAddressCoordinates(const std::string& address); // Dummy address lookup
    void reportNavigationError(NavigationStatus error_status, const std::string& details);
};

} // namespace ecu_infotainment

#endif // NAVIGATION_SYSTEM_H
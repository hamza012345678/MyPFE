// AutoSystemSim/ecu_infotainment/navigation_system.cpp
#include "navigation_system.h"
#include <cmath>     // For fabs, sin, cos, M_PI
#include <random>    // For simulating GPS issues, route calculation time
#include <thread>    // For std::this_thread::sleep_for

namespace ecu_infotainment {
     static std::random_device rd_nav_static_generator; // Renamed to avoid conflict if one is member
     static std::mt19937 gen_nav_static_generator(rd_nav_static_generator());

// Helper to convert GPSSignalStatus enum to string for logging
const char* gpsStatusToString(GPSSignalStatus status) {
    switch (status) {
        case GPSSignalStatus::NO_FIX: return "NO_FIX";
        case GPSSignalStatus::FIX_2D: return "FIX_2D";
        case GPSSignalStatus::FIX_3D: return "FIX_3D";
        case GPSSignalStatus::LOST_TEMPORARILY: return "LOST_TEMPORARILY";
        case GPSSignalStatus::FAULTY: return "FAULTY";
        default: return "UNKNOWN_GPS_STATUS";
    }
}

// Helper to convert NavigationStatus enum to string for logging
const char* navStatusToString(NavigationStatus status) {
    switch (status) {
        case NavigationStatus::IDLE: return "IDLE";
        case NavigationStatus::ROUTE_CALCULATING: return "ROUTE_CALCULATING";
        case NavigationStatus::GUIDANCE_ACTIVE: return "GUIDANCE_ACTIVE";
        case NavigationStatus::RECALCULATING_ROUTE: return "RECALCULATING_ROUTE";
        case NavigationStatus::DESTINATION_REACHED: return "DESTINATION_REACHED";
        case NavigationStatus::ERROR_NO_GPS: return "ERROR_NO_GPS";
        case NavigationStatus::ERROR_ROUTE_FAILED: return "ERROR_ROUTE_FAILED";
        default: return "UNKNOWN_NAVIGATION_STATUS";
    }
}


NavigationSystem::NavigationSystem() :
    nav_status_(NavigationStatus::IDLE),
    gps_status_(GPSSignalStatus::NO_FIX),
    map_data_loaded_(false), // Assume map data needs to be "loaded"
    current_route_segment_index_(-1),
    initial_eta_seconds_(0)
{
    LOG_INFO("NavigationSystem: Initializing...");
    current_location_ = {0.0, 0.0, "Initial Position (No GPS)"}; // Default start
    destination_ = {0.0, 0.0, ""};

    // Simulate map data loading
    std::random_device rd_local_ctor; // Local to constructor
    std::mt19937 gen_local_ctor(rd_local_ctor());
    if (std::uniform_int_distribution<>(1, 10)(gen_local_ctor) > 1) { // 90% chance map data loads ok
        map_data_loaded_ = true;
        LOG_INFO("NavigationSystem: Map data loaded successfully.");
    } else {
        map_data_loaded_ = false;
        LOG_ERROR("NavigationSystem: Failed to load map data! Navigation will be unavailable.");
        nav_status_ = NavigationStatus::ERROR_ROUTE_FAILED; // Or a specific "NO_MAP" error
    }
    simulateGPSFix(); // Attempt initial GPS fix
}

NavigationSystem::~NavigationSystem() {
    LOG_INFO("NavigationSystem: Shutting down. Final NavStatus: %s, GPS: %s",
             navStatusToString(nav_status_), gpsStatusToString(gps_status_));
}

void NavigationSystem::simulateGPSFix() {
    GPSSignalStatus old_status = gps_status_;
    std::random_device rd_local_gps; // Use local generator if preferred for this function
    std::mt19937 gen_local_gps(rd_local_gps());
    int gps_chance = std::uniform_int_distribution<>(1, 100)(gen_local_gps);

    if (gps_status_ == GPSSignalStatus::FAULTY) {
        LOG_WARNING("NavigationSystem: GPS module is FAULTY. Attempting reset (simulated)...");
        if (gps_chance > 90) { // 10% chance faulty GPS recovers after "reset"
            gps_status_ = GPSSignalStatus::NO_FIX;
            LOG_INFO("NavigationSystem: GPS module fault cleared after reset (simulated). Now NO_FIX.");
        } else {
             LOG_ERROR("NavigationSystem: GPS module remains FAULTY after reset attempt.");
             return; // No change if still faulty
        }
    }


    if (gps_chance <= 5) { // 5% chance of becoming FAULTY
        gps_status_ = GPSSignalStatus::FAULTY;
        LOG_ERROR("NavigationSystem: GPS module became FAULTY (simulated hardware issue).");
    } else if (gps_chance <= 15) { // 10% chance of NO_FIX (includes previous 5%)
        gps_status_ = GPSSignalStatus::NO_FIX;
    } else if (gps_chance <= 30) { // 15% chance of 2D_FIX
        gps_status_ = GPSSignalStatus::FIX_2D;
        // Simulate getting a plausible location if we had no fix before
        if (old_status == GPSSignalStatus::NO_FIX || old_status == GPSSignalStatus::LOST_TEMPORARILY) {
            current_location_ = {48.8584, 2.2945, "Eiffel Tower Vicinity (Simulated Fix)"}; // Example: Paris
        }
    } else { // 70% chance of 3D_FIX (good signal)
        gps_status_ = GPSSignalStatus::FIX_3D;
        if (old_status == GPSSignalStatus::NO_FIX || old_status == GPSSignalStatus::LOST_TEMPORARILY || old_status == GPSSignalStatus::FIX_2D) {
            current_location_ = {34.0522, -118.2437, "Los Angeles Downtown (Simulated Fix)"}; // Example: LA
        }
    }

    if (old_status != gps_status_) {
        LOG_INFO("NavigationSystem: GPS status changed from %s to %s. Current Location (if fix): %.4f, %.4f",
                 gpsStatusToString(old_status), gpsStatusToString(gps_status_), current_location_.latitude, current_location_.longitude);
    } else {
        LOG_VERBOSE("NavigationSystem: GPS status remains %s.", gpsStatusToString(gps_status_));
    }

    if (gps_status_ == GPSSignalStatus::NO_FIX || gps_status_ == GPSSignalStatus::FAULTY) {
        if (nav_status_ == NavigationStatus::GUIDANCE_ACTIVE || nav_status_ == NavigationStatus::ROUTE_CALCULATING) {
            reportNavigationError(NavigationStatus::ERROR_NO_GPS, "Lost GPS signal during active guidance or route calculation.");
        }
    }
}

MapCoordinate NavigationSystem::findAddressCoordinates(const std::string& address) {
    LOG_INFO("NavigationSystem: Looking up address: '%s' (simulated).", address.c_str());
    // Dummy implementation
    if (address.find("Home") != std::string::npos) return {34.0522, -118.2437, "Home"}; // LA
    if (address.find("Work") != std::string::npos) return {40.7128, -74.0060, "Work"};   // NYC
    if (address.find("Paris") != std::string::npos) return {48.8566, 2.3522, "Paris Center"};
    LOG_WARNING("NavigationSystem: Address '%s' not found in dummy database.", address.c_str());
    return {0.0, 0.0, ""}; // Invalid/not found
}

bool NavigationSystem::setDestination(const MapCoordinate& dest, const std::string& dest_name) {
    LOG_INFO("NavigationSystem: Set destination request. Name: '%s', Lat: %.4f, Lon: %.4f.",
             dest_name.empty() ? "N/A" : dest_name.c_str(), dest.latitude, dest.longitude);

    if (!map_data_loaded_) {
        reportNavigationError(NavigationStatus::ERROR_ROUTE_FAILED, "Cannot set destination. Map data not available.");
        return false;
    }
    if (!dest.isValid()) {
        reportNavigationError(NavigationStatus::ERROR_ROUTE_FAILED, "Invalid destination coordinates.");
        return false;
    }
    if (gps_status_ == GPSSignalStatus::NO_FIX || gps_status_ == GPSSignalStatus::FAULTY) {
        reportNavigationError(NavigationStatus::ERROR_NO_GPS, "Cannot set destination. No valid GPS signal for current location.");
        return false;
    }

    destination_ = dest;
    destination_name_ = dest_name.empty() ? ("Destination (" + std::to_string(dest.latitude).substr(0,6) + "," + std::to_string(dest.longitude).substr(0,7) + ")") : dest_name;
    nav_status_ = NavigationStatus::ROUTE_CALCULATING;
    LOG_INFO("NavigationSystem: Destination set to '%s'. Calculating route...", destination_name_.c_str());

    // Route calculation happens in updateNavigationState or a separate thread.
    // For this simulation, calculateRoute will be called from update.
    return true;
}

bool NavigationSystem::setDestinationByAddress(const std::string& address) {
    LOG_INFO("NavigationSystem: Set destination by address request: '%s'.", address.c_str());
    MapCoordinate dest_coord = findAddressCoordinates(address); // Internal call
    if (!dest_coord.isValid()) {
        reportNavigationError(NavigationStatus::ERROR_ROUTE_FAILED, "Address lookup failed for: " + address);
        return false;
    }
    return setDestination(dest_coord, address); // Use the address as the name
}

bool NavigationSystem::cancelNavigation() {
    LOG_INFO("NavigationSystem: Cancel navigation request.");
    if (nav_status_ == NavigationStatus::IDLE || nav_status_ == NavigationStatus::DESTINATION_REACHED) {
        LOG_DEBUG("NavigationSystem: No active navigation to cancel.");
        return true;
    }
    nav_status_ = NavigationStatus::IDLE;
    current_route_.clear();
    current_route_segment_index_ = -1;
    destination_ = {0.0, 0.0, ""};
    destination_name_ = "";
    initial_eta_seconds_ = std::chrono::seconds(0);
    LOG_INFO("NavigationSystem: Navigation cancelled. System is IDLE.");
    return true;
}


bool NavigationSystem::calculateRoute() {
    LOG_INFO("NavigationSystem: Calculating route from (%.4f, %.4f) to '%s' (%.4f, %.4f)...",
             current_location_.latitude, current_location_.longitude,
             destination_name_.c_str(), destination_.latitude, destination_.longitude);

    nav_status_ = NavigationStatus::ROUTE_CALCULATING; // Ensure state
    current_route_.clear();
    current_route_segment_index_ = -1;

    // Simulate calculation time
    std::this_thread::sleep_for(std::chrono::milliseconds(500 + (rand() % 1500))); // 0.5 to 2 seconds

    if (gps_status_ == GPSSignalStatus::NO_FIX || gps_status_ == GPSSignalStatus::FAULTY) {
        reportNavigationError(NavigationStatus::ERROR_NO_GPS, "Route calculation failed: No GPS fix.");
        return false;
    }
    if (current_location_.distanceTo(destination_) < 0.1) { // Already at destination (within 100m)
        LOG_INFO("NavigationSystem: Already at destination '%s'. No route calculated.", destination_name_.c_str());
        nav_status_ = NavigationStatus::DESTINATION_REACHED;
        return true;
    }


    // Dummy route generation
    current_route_.push_back({"Drive straight for 2.5 km on Current Rd", 2.5, {current_location_.latitude + 0.01, current_location_.longitude + 0.01}});
    current_route_.push_back({"Turn left onto Cross Ave, proceed 1.8 km", 1.8, {current_location_.latitude + 0.02, current_location_.longitude - 0.005}});
    current_route_.push_back({"Turn right onto Destination Blvd, proceed 0.5 km", 0.5, destination_});
    current_route_.back().end_point.name = destination_name_; // Set name for last point

    if (current_route_.empty()) {
        reportNavigationError(NavigationStatus::ERROR_ROUTE_FAILED, "Failed to calculate a route to " + destination_name_);
        return false;
    }

    current_route_segment_index_ = 0;
    nav_status_ = NavigationStatus::GUIDANCE_ACTIVE;
    route_start_time_ = std::chrono::steady_clock::now();

    double total_dist = 0;
    for(const auto& seg : current_route_) total_dist += seg.distance_km;
    initial_eta_seconds_ = std::chrono::seconds(static_cast<long>((total_dist / 40.0) * 3600)); // Avg 40km/h

    LOG_INFO("NavigationSystem: Route calculated successfully to '%s'. %zu segments. Initial ETA: %llds. Guidance ACTIVE.",
             destination_name_.c_str(), current_route_.size(), initial_eta_seconds_.count());
    return true;
}

void NavigationSystem::provideGuidanceUpdate(double speed_kmh) {
    if (current_route_segment_index_ < 0 || current_route_segment_index_ >= static_cast<int>(current_route_.size())) {
        LOG_WARNING("NavigationSystem: Guidance update requested but no valid route segment. Index: %d", current_route_segment_index_);
        return;
    }

    const RouteSegment& current_segment = current_route_[current_route_segment_index_];
    double dist_to_maneuver = getDistanceToNextManeuverKm(); // This is approximated in updateLocation

    LOG_INFO("NavigationSystem: Guidance: %s. Next maneuver in %.1f km. Dist to Dest: %.1f km. ETA: %llds.",
             current_segment.instruction.c_str(),
             dist_to_maneuver,
             getDistanceToDestinationKm(),
             getEstimatedTimeOfArrivalSeconds().count());

    // Simulate advancing to next segment if close enough (handled more accurately in simulateLocationUpdate)
    if (dist_to_maneuver < 0.05 && speed_kmh > 1.0) { // Within 50m and moving
        LOG_INFO("NavigationSystem: Approaching maneuver for segment: '%s'.", current_segment.instruction.c_str());
        // Actual advance to next segment is done in simulateLocationUpdate where distance is reduced.
    }
}

void NavigationSystem::checkOffRoute(double speed_kmh) {
    if (current_route_.empty() || current_route_segment_index_ < 0) return;

    // Simplified: if current location is "too far" from the expected path (e.g., current segment's end point)
    // A real system compares against the polyline of the route.
    const RouteSegment& current_segment = current_route_[current_route_segment_index_];
    double dist_from_expected_track = current_location_.distanceTo(current_segment.end_point) - getDistanceToNextManeuverKm();

    if (dist_from_expected_track > 0.5 && speed_kmh > 5.0) { // More than 500m off track and moving
        LOG_WARNING("NavigationSystem: OFF ROUTE detected! Distance from track: %.2f km. Recalculating...", dist_from_expected_track);
        nav_status_ = NavigationStatus::RECALCULATING_ROUTE;
        // calculateRoute(); // Recalculate immediately or on next update cycle
    }
}

void NavigationSystem::simulateLocationUpdate(double speed_kmh, double heading_degrees, std::chrono::seconds time_delta_s) {
    if (gps_status_ != GPSSignalStatus::FIX_2D && gps_status_ != GPSSignalStatus::FIX_3D) {
        LOG_DEBUG("NavigationSystem: No GPS fix, cannot simulate location update meaningfully.");
        // Could simulate drift if using dead reckoning
        if(gps_status_ != GPSSignalStatus::FAULTY && gps_status_ != GPSSignalStatus::NO_FIX) {
            gps_status_ = GPSSignalStatus::LOST_TEMPORARILY; // e.g. tunnel
            LOG_WARNING("NavigationSystem: GPS signal temporarily lost. Location updates based on dead reckoning (simulated).");
        }
        return;
    }

    if (time_delta_s.count() == 0) return;

    double distance_moved_km = (speed_kmh * time_delta_s.count()) / 3600.0;
    double heading_rad = heading_degrees * (M_PI / 180.0); // Convert to radians

    // Simplistic update of lat/lon. 1 deg lat ~= 111km. 1 deg lon ~= 111km * cos(lat)
    current_location_.latitude += (distance_moved_km / 111.0) * std::cos(heading_rad);
    if (std::fabs(std::cos(current_location_.latitude * (M_PI / 180.0))) > 0.001) { // Avoid division by zero at poles
         current_location_.longitude += (distance_moved_km / (111.0 * std::cos(current_location_.latitude * (M_PI / 180.0)))) * std::sin(heading_rad);
    }

    // Clamp to valid lat/lon
    current_location_.latitude = std::max(-90.0, std::min(90.0, current_location_.latitude));
    current_location_.longitude = std::max(-180.0, std::min(180.0, current_location_.longitude));

    LOG_VERBOSE("NavigationSystem: Simulated new location: %.4f, %.4f (moved %.3f km, heading %.0f deg)",
               current_location_.latitude, current_location_.longitude, distance_moved_km, heading_degrees);

    if (nav_status_ == NavigationStatus::GUIDANCE_ACTIVE && !current_route_.empty()) {
        // Update distance to next maneuver and segment
        RouteSegment& current_segment = current_route_[current_route_segment_index_];
        current_segment.distance_km -= distance_moved_km; // Reduce remaining distance of current segment

        if (current_segment.distance_km <= 0.0) {
            LOG_INFO("NavigationSystem: Maneuver '%s' completed.", current_segment.instruction.c_str());
            current_route_segment_index_++;
            if (current_route_segment_index_ >= static_cast<int>(current_route_.size())) {
                LOG_INFO("NavigationSystem: DESTINATION '%s' REACHED!", destination_name_.c_str());
                nav_status_ = NavigationStatus::DESTINATION_REACHED;
                current_route_.clear();
                current_route_segment_index_ = -1;
            } else {
                LOG_INFO("NavigationSystem: Proceeding to next segment: '%s'.", current_route_[current_route_segment_index_].instruction.c_str());
                 // current_route_[current_route_segment_index_].distance_km is its original distance,
                 // we'd start reducing it from next cycle.
            }
        }
    }
}


void NavigationSystem::updateNavigationState(const VehicleState& vehicle_state) {
    LOG_DEBUG("NavigationSystem: Updating navigation state. NavStatus: %s, GPS: %s, Speed: %.1f km/h",
              navStatusToString(nav_status_), gpsStatusToString(gps_status_), vehicle_state.speed_kmh);

    static std::chrono::steady_clock::time_point last_update_time = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    auto time_delta = std::chrono::duration_cast<std::chrono::seconds>(now - last_update_time);
    last_update_time = now;

    // Simulate GPS status changes periodically
    if (std::uniform_int_distribution<>(1, 20)(gen_nav_static_generator) == 1) { 
        simulateGPSFix(); 
    }

    // Simulate current location update based on speed and heading (dummy heading)
    // A real system gets heading from GPS or compass/IMU.
    static double current_heading_deg = 45.0; // Start with a dummy heading
    if (vehicle_state.speed_kmh > 1.0) { // Only update location if moving
         // Simulate slight heading change
        current_heading_deg += std::uniform_real_distribution<>(-5.0, 5.0)(gen_nav_static_generator);
        if (current_heading_deg > 360.0) current_heading_deg -= 360.0;
        if (current_heading_deg < 0.0) current_heading_deg += 360.0;

        simulateLocationUpdate(vehicle_state.speed_kmh, current_heading_deg, time_delta); // Internal call
    }


    // Handle route calculation if pending
    if (nav_status_ == NavigationStatus::ROUTE_CALCULATING) {
        if (!map_data_loaded_) {
            reportNavigationError(NavigationStatus::ERROR_ROUTE_FAILED, "Route calculation aborted: Map data unavailable.");
        } else if (gps_status_ == GPSSignalStatus::NO_FIX || gps_status_ == GPSSignalStatus::FAULTY) {
            reportNavigationError(NavigationStatus::ERROR_NO_GPS, "Route calculation aborted: No GPS fix.");
        } else {
            calculateRoute(); // This will change nav_status_ to GUIDANCE_ACTIVE or ERROR
        }
    }
    // Handle recalculation if pending
    else if (nav_status_ == NavigationStatus::RECALCULATING_ROUTE) {
        LOG_INFO("NavigationSystem: Attempting to recalculate route due to off-route condition.");
        if (calculateRoute()) { // Will set to GUIDANCE_ACTIVE if successful
            LOG_INFO("NavigationSystem: Route successfully recalculated.");
        } else {
            LOG_ERROR("NavigationSystem: Failed to recalculate route. Check GPS and map data.");
            // nav_status_ might be ERROR_ROUTE_FAILED or ERROR_NO_GPS from calculateRoute()
        }
    }
    // Provide guidance if active
    else if (nav_status_ == NavigationStatus::GUIDANCE_ACTIVE) {
        provideGuidanceUpdate(vehicle_state.speed_kmh); // Internal call
        checkOffRoute(vehicle_state.speed_kmh);         // Internal call, might change state to RECALCULATING_ROUTE
    }
    else if (nav_status_ == NavigationStatus::DESTINATION_REACHED) {
        // Potentially clear destination after a while or offer new route options
        static int cycles_at_dest = 0;
        cycles_at_dest++;
        if(cycles_at_dest > 10) { // After 10 cycles
            LOG_INFO("NavigationSystem: Destination reached for some time. Clearing route.");
            cancelNavigation(); // Or set to IDLE.
            cycles_at_dest = 0;
        }
    }

    LOG_DEBUG("NavigationSystem: Navigation state update cycle complete. NavStatus: %s", navStatusToString(nav_status_));
}


NavigationStatus NavigationSystem::getCurrentNavigationStatus() const {
    LOG_DEBUG("NavigationSystem: getCurrentNavigationStatus() -> %s", navStatusToString(nav_status_));
    return nav_status_;
}

RouteSegment NavigationSystem::getCurrentGuidanceInstruction() const {
    if (nav_status_ == NavigationStatus::GUIDANCE_ACTIVE &&
        current_route_segment_index_ >= 0 &&
        current_route_segment_index_ < static_cast<int>(current_route_.size())) {
        const auto& seg = current_route_[current_route_segment_index_];
        LOG_DEBUG("NavigationSystem: getCurrentGuidanceInstruction() -> '%s' (%.1f km)", seg.instruction.c_str(), seg.distance_km);
        return seg;
    }
    LOG_DEBUG("NavigationSystem: getCurrentGuidanceInstruction() -> No active guidance.");
    return {"No active guidance", 0.0, {0,0,""}};
}

double NavigationSystem::getDistanceToNextManeuverKm() const {
    if (nav_status_ == NavigationStatus::GUIDANCE_ACTIVE &&
        current_route_segment_index_ >= 0 &&
        current_route_segment_index_ < static_cast<int>(current_route_.size())) {
        // This is the remaining distance of the current segment being traversed
        double dist = current_route_[current_route_segment_index_].distance_km;
        LOG_VERBOSE("NavigationSystem: getDistanceToNextManeuverKm() -> %.2f km", dist);
        return std::max(0.0, dist); // Don't return negative if overshot slightly in sim
    }
    return 0.0;
}

double NavigationSystem::getDistanceToDestinationKm() const {
    if (nav_status_ == NavigationStatus::GUIDANCE_ACTIVE && !current_route_.empty()) {
        double total_remaining_dist = 0.0;
        // Sum remaining distance of current segment + all subsequent segments
        if (current_route_segment_index_ >= 0 && current_route_segment_index_ < static_cast<int>(current_route_.size())) {
            total_remaining_dist += std::max(0.0, current_route_[current_route_segment_index_].distance_km);
            for (size_t i = current_route_segment_index_ + 1; i < current_route_.size(); ++i) {
                total_remaining_dist += current_route_[i].distance_km;
            }
        }
        LOG_VERBOSE("NavigationSystem: getDistanceToDestinationKm() -> %.2f km", total_remaining_dist);
        return total_remaining_dist;
    }
    // Could also calculate direct distance if no route: current_location_.distanceTo(destination_)
    if (destination_.isValid() && current_location_.isValid()) {
        return current_location_.distanceTo(destination_);
    }
    return 0.0;
}

std::chrono::seconds NavigationSystem::getEstimatedTimeOfArrivalSeconds() const {
    if (nav_status_ == NavigationStatus::GUIDANCE_ACTIVE) {
        auto time_elapsed_on_route = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - route_start_time_
        );
        auto current_eta = initial_eta_seconds_ - time_elapsed_on_route;
        if (current_eta.count() < 0) current_eta = std::chrono::seconds(0);
        LOG_VERBOSE("NavigationSystem: getEstimatedTimeOfArrivalSeconds() -> %llds", current_eta.count());
        return current_eta;
    }
    return std::chrono::seconds(0);
}

GPSSignalStatus NavigationSystem::getGPSSignalStatus() const {
    LOG_DEBUG("NavigationSystem: getGPSSignalStatus() -> %s", gpsStatusToString(gps_status_));
    return gps_status_;
}

MapCoordinate NavigationSystem::getCurrentLocation() const {
    LOG_DEBUG("NavigationSystem: getCurrentLocation() -> Lat: %.4f, Lon: %.4f (%s)",
              current_location_.latitude, current_location_.longitude, gpsStatusToString(gps_status_));
    return current_location_;
}

bool NavigationSystem::isMapDataAvailable() const {
    LOG_DEBUG("NavigationSystem: isMapDataAvailable() -> %s", map_data_loaded_ ? "YES" : "NO");
    return map_data_loaded_;
}

void NavigationSystem::reportNavigationError(NavigationStatus error_status, const std::string& details) {
    nav_status_ = error_status; // Set the error state
    LOG_ERROR("NavigationSystem: Navigation Error (%s): %s",
              navStatusToString(error_status), details.c_str());
    // Could clear route or take other actions based on error type.
    current_route_.clear();
    current_route_segment_index_ = -1;
}

// Define a global random generator for use within this file if needed by multiple functions
// Or pass it around / make it a member if you prefer stricter encapsulation.
static std::random_device rd_nav; // Static to be initialized once
static std::mt19937 gen(rd_nav()); // Static to be initialized once


} // namespace ecu_infotainment
#include "main_vehicle_controller.h"
#include <thread>   // For std::this_thread::sleep_for
#include <chrono>   // For std::chrono::milliseconds
#include <algorithm> // for std::generate, std::fill
#include <random>
#include <logger.h>
#include <airbag_control.h>

namespace main_application {

MainVehicleController::MainVehicleController() :
    ignition_on_(false),
    main_loop_cycles_(0),
    current_brake_pedal_pressure_(0.0)
{
    LOG_INFO("MainVehicleController: Initializing...");

    // --- Instantiate ECUs ---
    // Order might matter for dependencies (e.g., PowerMonitor first)
    power_monitor_ = new ecu_power_management::PowerMonitor();

    // Powertrain
    engine_manager_ = new ecu_powertrain_control::EngineManager(); // EngineManager instantiates its own PowerMonitor for now
                                                                  // A better design would pass power_monitor_ to EngineManager's constructor.
                                                                  // Let's assume for now EngineManager uses its own internal or a global one.
                                                                  // Or let's adjust EngineManager to take PowerMonitor*
                                                                  // For simplicity, we'll keep EngineManager's current constructor for now.
    transmission_manager_ = new ecu_powertrain_control::TransmissionManager(engine_manager_);

    // Body Control Module components
    // These BCM components also take PowerMonitor*.
    climate_control_ = new ecu_body_control_module::ClimateControl(power_monitor_);
    lighting_control_ = new ecu_body_control_module::LightingControl(power_monitor_);
    window_control_ = new ecu_body_control_module::WindowControl(power_monitor_);

    // Infotainment components
    media_player_ = new ecu_infotainment::MediaPlayer();
    navigation_system_ = new ecu_infotainment::NavigationSystem();

    // Safety Systems components
    abs_control_ = new ecu_safety_systems::ABSControl(/* no brake actuator for now */);
    airbag_control_ = new ecu_safety_systems::AirbagControl();


    // Initialize sensor data structures
    current_wheel_speed_sensors_.resize(4); // FL, FR, RL, RR
    for(int i=0; i<4; ++i) {
        current_wheel_speed_sensors_[i] = {i, 0.0, "km/h", 0};
    }

    LOG_INFO("MainVehicleController: All core ECU instances created.");
}

MainVehicleController::~MainVehicleController() {
    LOG_INFO("MainVehicleController: Shutting down...");
    if (ignition_on_) {
        handleIgnitionOff(); // Ensure graceful shutdown of systems if ignition was on
    }
    shutdownAllSystems(); // Call explicit shutdown for ECUs if they have one

    // Delete ECU instances in reverse order of creation (generally good practice)
    delete airbag_control_;
    delete abs_control_;
    delete navigation_system_;
    delete media_player_;
    delete window_control_;
    delete lighting_control_;
    delete climate_control_;
    delete transmission_manager_;
    delete engine_manager_;
    delete power_monitor_;

    LOG_INFO("MainVehicleController: Shutdown complete. All ECU instances deleted.");
}

void MainVehicleController::initializeAllSystems() {
    LOG_INFO("MainVehicleController: Initializing all vehicle systems...");
    // Power monitor usually has no explicit init beyond constructor
    // Engine manager is initialized by its constructor
    // Transmission manager is initialized by its constructor

    // BCM components are initialized by their constructors
    // Infotainment components are initialized by their constructors

    // Safety systems might have specific init/check routines
    if (abs_control_) abs_control_->runDiagnostics(); // ABS has diagnostics
    if (airbag_control_) airbag_control_->runSystemCheck(); // Airbag has system check

    std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Simulate overall system init time
    LOG_INFO("MainVehicleController: All vehicle systems initialized.");
}

void MainVehicleController::updateVehicleStateInputs() {
    LOG_VERBOSE("MainVehicleController: Updating simulated vehicle state inputs...");

    // Simulate changes to current_vehicle_state_ based on engine, etc.
    if (engine_manager_) {
        current_vehicle_state_ = engine_manager_->getEngineState(); // Get RPM
        // Simulate speed based on RPM and gear (very, very simplified)
        if (transmission_manager_ && transmission_manager_->getCurrentGear() > 0 && current_vehicle_state_.engine_rpm > 0) {
            current_vehicle_state_.speed_kmh = static_cast<double>(current_vehicle_state_.engine_rpm / 100) * transmission_manager_->getCurrentGear() * 0.5;
        } else if (transmission_manager_ && transmission_manager_->getCurrentGear() < 0 && current_vehicle_state_.engine_rpm > 0) { // Reverse
             current_vehicle_state_.speed_kmh = static_cast<double>(current_vehicle_state_.engine_rpm / 100) * -0.3;
        }
         else {
            current_vehicle_state_.speed_kmh = 0;
        }
        // Max speed clamp
        current_vehicle_state_.speed_kmh = std::min(current_vehicle_state_.speed_kmh, 180.0);
        if (current_vehicle_state_.speed_kmh < -30.0) current_vehicle_state_.speed_kmh = -30.0;

    } else {
        current_vehicle_state_.engine_rpm = 0;
        current_vehicle_state_.speed_kmh = 0;
    }
    current_vehicle_state_.lights_on = lighting_control_ ? (lighting_control_->getLightStatus(ecu_body_control_module::LightType::HEADLIGHT_LOW) == ecu_body_control_module::LightStatus::ON) : false;
    current_vehicle_state_.battery_voltage = power_monitor_ ? power_monitor_->getBatteryVoltage() : 12.0;


    // Simulate wheel speed sensors (can be noisy or slightly different from vehicle speed)
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<> speed_noise(-0.5, 0.5);
    for(size_t i=0; i < current_wheel_speed_sensors_.size(); ++i) {
        current_wheel_speed_sensors_[i].value = std::max(0.0, current_vehicle_state_.speed_kmh + speed_noise(gen));
        current_wheel_speed_sensors_[i].timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    }

    // Simulate crash sensor data (normally low, spikes during an event)
    std::uniform_real_distribution<> g_noise(-0.2, 0.2);
    current_crash_sensors_input_.longitudinal_g = g_noise(gen);
    current_crash_sensors_input_.lateral_g = g_noise(gen);
    current_crash_sensors_input_.vertical_g = 1.0 + g_noise(gen); // Centered around 1G for gravity
    current_crash_sensors_input_.roll_rate_deg_s = g_noise(gen) * 10;
    current_crash_sensors_input_.pitch_rate_deg_s = g_noise(gen) * 5;
    current_crash_sensors_input_.seatbelt_fastened_driver = true; // Assume for simplicity
    current_crash_sensors_input_.seatbelt_fastened_passenger = (rand() % 2 == 0);
    current_crash_sensors_input_.passenger_seat_occupied = current_crash_sensors_input_.seatbelt_fastened_passenger || (rand() % 3 == 0) ;


    // Simulate brake pedal (normally 0, increases when user brakes)
    // This would be set by a specific event usually. For periodic, assume no braking unless specified.
    // current_brake_pedal_pressure_ is set by events in simulateDrivingCycle for now.

    LOG_VERBOSE("MainVehicleController: Updated VehicleState: Speed=%.1f, RPM=%d. WheelFL=%.1f",
               current_vehicle_state_.speed_kmh, current_vehicle_state_.engine_rpm, current_wheel_speed_sensors_[0].value);
}


void MainVehicleController::periodicECUUpdates() {
    LOG_DEBUG("MainVehicleController: Performing periodic ECU updates...");

    if (power_monitor_) power_monitor_->updatePowerStatus(); // Update power status first

    // Update powertrain based on current state (e.g., accelerator pedal input, not modeled yet)
    if (engine_manager_) engine_manager_->updateEngineParameters(); // Simulates temp changes, etc.
    if (transmission_manager_ && engine_manager_) {
        transmission_manager_->updateState(current_vehicle_state_, engine_manager_->getCurrentRPM());
    }

    // BCM components
    if (climate_control_) {
        SensorData int_temp = {1, 22.0 + (rand()%3-1), "C", 0}; // Dummy interior temp sensor
        SensorData ext_temp = {2, 18.0 + (rand()%5-2), "C", 0}; // Dummy exterior temp sensor
        climate_control_->updateClimateState(current_vehicle_state_, int_temp, ext_temp);
    }
    if (lighting_control_) lighting_control_->updateLighting(current_vehicle_state_);
    if (window_control_) window_control_->updateWindowStates();

    // Infotainment
    if (media_player_) media_player_->updatePlaybackState();
    if (navigation_system_) navigation_system_->updateNavigationState(current_vehicle_state_);

    // Safety Systems
    if (abs_control_) {
        abs_control_->processBraking(current_vehicle_state_, current_wheel_speed_sensors_, current_brake_pedal_pressure_);
    }
    if (airbag_control_) {
        // Only process impact data if there's a significant G event (simulated more directly in crash scenario)
        // For normal loop, airbag just monitors.
        // If current_crash_sensors_input_ had a spike, we'd call it.
        // For regular updates, it might just do internal checks.
        airbag_control_->processImpactData(current_crash_sensors_input_, current_vehicle_state_);
    }
    LOG_DEBUG("MainVehicleController: Periodic ECU updates complete.");
}

void MainVehicleController::checkSystemHealth() {
    LOG_DEBUG("MainVehicleController: Performing periodic system health checks...");
    if (engine_manager_) {
        // EngineManager has its own fault reporting, this could be a higher level check
        if (engine_manager_->getCurrentRPM() == 0 && ignition_on_ && engine_manager_->getEngineState().status_message.find("FAULT") != std::string::npos) {
             LOG_WARNING("MainVehicleController: Health Check: Engine is off but reported FAULT while ignition is ON.");
        }
    }
    if (abs_control_) {
        if (abs_control_->getCurrentState() == ecu_safety_systems::ABSState::FAULT_DETECTED) {
             LOG_WARNING("MainVehicleController: Health Check: ABS System reports FAULT_DETECTED.");
        }
    }
    if (airbag_control_) {
        auto airbag_state = airbag_control_->getSystemState();
        if (airbag_state == ecu_safety_systems::AirbagSystemState::FAULT_SYSTEM_INOPERATIVE ||
            airbag_state == ecu_safety_systems::AirbagSystemState::FAULT_SENSOR_ISSUE ||
            airbag_state == ecu_safety_systems::AirbagSystemState::FAULT_DEPLOYMENT_CIRCUIT ) {
             LOG_WARNING("MainVehicleController: Health Check: Airbag System reports FAULT (%s).", ecu_safety_systems::airbagSysStateToString(airbag_state));
        }
    }
    // Add more checks for other systems as needed
    LOG_INFO("MainVehicleController: System health checks complete.");
}


void MainVehicleController::runMainLoop() {
    LOG_INFO("MainVehicleController: Starting main vehicle operation loop...");
    if (!ignition_on_) {
        LOG_WARNING("MainVehicleController: Cannot run main loop. Ignition is OFF.");
        return;
    }

    main_loop_cycles_ = 0;
    // Simulate a few cycles of operation
    // In a real car, this loop runs continuously at a fixed rate (e.g., 10ms, 20ms, 100ms tasks)
    const int max_cycles = 20; // Simulate for 20 cycles for this example

    while(ignition_on_ && main_loop_cycles_ < max_cycles) {
        main_loop_cycles_++;
        LOG_INFO("MainVehicleController: Main Loop Cycle %d/%d", main_loop_cycles_, max_cycles);

        auto cycle_start_time = std::chrono::steady_clock::now();

        // 1. Update Sensor Inputs / Vehicle State
        updateVehicleStateInputs();

        // 2. Run Periodic Updates for all ECUs
        periodicECUUpdates();

        // 3. Perform System Health Checks (less frequently)
        if (main_loop_cycles_ % 5 == 0) {
            checkSystemHealth();
        }

        // Simulate cycle time
        std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Simulate 100ms cycle
        LOG_INFO("MainVehicleController: End of Main Loop Cycle %d", main_loop_cycles_);

        // Reset brake pedal after ABS processing for this cycle unless an event sets it again
        current_brake_pedal_pressure_ = 0.0;
    }

    if (main_loop_cycles_ >= max_cycles) {
        LOG_INFO("MainVehicleController: Reached max simulation cycles for main loop.");
    }
    LOG_INFO("MainVehicleController: Exiting main vehicle operation loop.");
}

void MainVehicleController::handleIgnitionOn() {
    LOG_FATAL("MainVehicleController: IGNITION ON sequence started."); // FATAL to highlight
    ignition_on_ = true;
    // Power up sequence: BCM, Infotainment basic, then enable Engine start
    if (power_monitor_) power_monitor_->updatePowerStatus(); // Initial power check

    // Prime systems
    if (engine_manager_) {
        // engine_manager_->primeFuelPump(); // Or this is part of startEngine
    }
    if (airbag_control_) airbag_control_->runSystemCheck(); // Arm airbags
    if (abs_control_) abs_control_->runDiagnostics();       // Check ABS

    // Start some default services
    if (media_player_) media_player_->selectSource(ecu_infotainment::MediaSource::RADIO_FM);
    if (climate_control_) climate_control_->setAutoMode(true);


    LOG_INFO("MainVehicleController: Ignition ON sequence complete. Vehicle systems ready.");
}

void MainVehicleController::handleIgnitionOff() {
    LOG_FATAL("MainVehicleController: IGNITION OFF sequence started."); // FATAL to highlight
    ignition_on_ = false;

    // Graceful shutdown of systems
    if (engine_manager_ && engine_manager_->getCurrentRPM() > 0) {
        LOG_INFO("MainVehicleController: Engine is running. Requesting engine stop.");
        engine_manager_->stopEngine();
    }
    if (media_player_) media_player_->stop();
    if (navigation_system_) navigation_system_->cancelNavigation();
    if (climate_control_) climate_control_->setFanSpeed(0); // Turn off climate

    // Log final states or perform cleanup
    LOG_INFO("MainVehicleController: Ignition OFF sequence complete. Systems shutting down.");
    // Actual ECU shutdown might happen in their destructors or dedicated shutdown methods
}

void MainVehicleController::simulateDrivingCycle() {
    LOG_INFO("MainVehicleController: Starting SIMULATED DRIVING CYCLE.");
    if (!ignition_on_) {
        LOG_WARNING("MainVehicleController: Cannot start driving cycle, ignition is OFF. Turning ignition ON.");
        handleIgnitionOn();
    }
    if (!engine_manager_) {
        LOG_ERROR("MainVehicleController: EngineManager not available. Cannot simulate driving cycle.");
        return;
    }

    // Sequence of events
    LOG_INFO("MainVehicleController: DRIVING_CYCLE: Starting engine...");
    if (engine_manager_->startEngine()) {
        LOG_INFO("MainVehicleController: DRIVING_CYCLE: Engine started.");
        std::this_thread::sleep_for(std::chrono::seconds(1));

        if (transmission_manager_) {
            LOG_INFO("MainVehicleController: DRIVING_CYCLE: Setting transmission to DRIVE.");
            transmission_manager_->setTransmissionMode(ecu_powertrain_control::TransmissionMode::DRIVE);
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));


        LOG_INFO("MainVehicleController: DRIVING_CYCLE: Accelerating...");
        engine_manager_->setTargetRPM(2500);
        runMainLoop(); // Run a few cycles with this RPM

        LOG_INFO("MainVehicleController: DRIVING_CYCLE: Cruising...");
        engine_manager_->setTargetRPM(2000);
        runMainLoop();

        LOG_INFO("MainVehicleController: DRIVING_CYCLE: Hard braking simulation (for ABS test)...");
        current_brake_pedal_pressure_ = 80.0; // Simulate hard brake pedal
        // ABS will be processed in the next runMainLoop cycle
        runMainLoop(); // Run a few cycles with braking
        current_brake_pedal_pressure_ = 0.0; // Release brake

        // Simulate a CRASH SCENARIO for Airbag test
        LOG_FATAL("MainVehicleController: DRIVING_CYCLE: !!! SIMULATING CRASH EVENT !!!");
        current_crash_sensors_input_ = {-30.0, 2.0, 1.5, true, true, true, 10.0, 5.0}; // Severe frontal impact data
        // Airbag processing will happen in the next runMainLoop() via periodicECUUpdates()
        runMainLoop();
        // Reset crash sensors after event processed
        current_crash_sensors_input_ = {0,0,1,true,true,true,0,0};
        LOG_INFO("MainVehicleController: DRIVING_CYCLE: Crash event processed. System may be in post-crash mode.");


        LOG_INFO("MainVehicleController: DRIVING_CYCLE: Coming to a stop...");
        engine_manager_->setTargetRPM(800); // Idle
        if (transmission_manager_) {
             // Slowing down, transmission would downshift automatically, then select Neutral/Park
        }
        runMainLoop();


        if (transmission_manager_) {
            LOG_INFO("MainVehicleController: DRIVING_CYCLE: Setting transmission to PARK.");
            transmission_manager_->setTransmissionMode(ecu_powertrain_control::TransmissionMode::PARK);
        }
        LOG_INFO("MainVehicleController: DRIVING_CYCLE: Stopping engine...");
        engine_manager_->stopEngine();

    } else {
        LOG_ERROR("MainVehicleController: DRIVING_CYCLE: Engine failed to start. Aborting cycle.");
    }

    LOG_INFO("MainVehicleController: SIMULATED DRIVING CYCLE complete.");
    handleIgnitionOff();
}


void MainVehicleController::shutdownAllSystems() {
    LOG_INFO("MainVehicleController: Explicitly shutting down all ECU functionalities if applicable...");
    // Some ECUs might have explicit shutdown methods if their destructors aren't enough
    // e.g., to save state to non-volatile memory.
    // For our simulation, most cleanup is in destructors.
    if (media_player_) media_player_->stop(); // Example of an explicit stop command
    // if (navigation_system_) navigation_system_->saveCurrentSettings(); // Hypothetical
    LOG_DEBUG("MainVehicleController: System shutdown commands issued.");
}


} // namespace main_application
#include <Geode/Geode.hpp>
#include <Geode/utils/general.hpp>
#include <Geode/utils/async.hpp>
#include <Geode/utils/VersionInfo.hpp>
#include <Geode/utils/web.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <Geode/utils/Keyboard.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/ui/Notification.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/modify/EffectGameObject.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/GameObject.hpp>
#include <Geode/modify/HardStreak.hpp>
#include <Geode/modify/LevelEditorLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/modify/RingObject.hpp>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cmath>
#include <cwctype>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

#ifdef GEODE_IS_MACOS
#include <signal.h>
#include <sys/types.h>
#include <unistd.h>
#endif

using namespace geode::prelude;

namespace {

    constexpr int MOD_HOOK_PRIORITY = -1000000;
    constexpr float TICK_DURATION = 0.25f;
    constexpr char const* KEYBIND_STAGE = "keybind-stage";
    constexpr char const* KEYBIND_TERMINAL = "keybind-terminal";
    constexpr char const* KEYBIND_INTRO_DONE = "keybind-intro-done";

#ifdef _WIN32
    HANDLE g_terminalLogHandle = INVALID_HANDLE_VALUE;
    std::string g_windowsTerminalLogFileName = "afterflow-terminal.log";
#endif

    constexpr uint64_t typeBit(GameObjectType objectType) {
        return 1ull << static_cast<int>(objectType);
    }

    constexpr uint64_t COLLIDING_OBJECT_TYPES = typeBit(GameObjectType::Solid) | typeBit(GameObjectType::Hazard) | typeBit(GameObjectType::Slope) |
                                         typeBit(GameObjectType::CollisionObject) | typeBit(GameObjectType::AnimatedHazard);

    constexpr uint64_t HAZARD_OBJECT_TYPES = typeBit(GameObjectType::Hazard) | typeBit(GameObjectType::AnimatedHazard);

    constexpr uint64_t INTERACTABLE_OBJECT_TYPES =
        typeBit(GameObjectType::YellowJumpPad) | typeBit(GameObjectType::PinkJumpPad) | typeBit(GameObjectType::RedJumpPad) |
        typeBit(GameObjectType::GravityPad) | typeBit(GameObjectType::SpiderPad) | typeBit(GameObjectType::YellowJumpRing) |
        typeBit(GameObjectType::PinkJumpRing) | typeBit(GameObjectType::RedJumpRing) | typeBit(GameObjectType::GravityRing) |
        typeBit(GameObjectType::GreenRing) | typeBit(GameObjectType::DropRing) | typeBit(GameObjectType::CustomRing) |
        typeBit(GameObjectType::DashRing) | typeBit(GameObjectType::GravityDashRing) | typeBit(GameObjectType::SpiderOrb) |
        typeBit(GameObjectType::InverseGravityPortal) | typeBit(GameObjectType::NormalGravityPortal) |
        typeBit(GameObjectType::GravityTogglePortal);

    constexpr uint64_t MODE_PORTAL_OBJECT_TYPES =
        typeBit(GameObjectType::ShipPortal) | typeBit(GameObjectType::CubePortal) | typeBit(GameObjectType::BallPortal) |
        typeBit(GameObjectType::UfoPortal) | typeBit(GameObjectType::WavePortal) | typeBit(GameObjectType::RobotPortal) |
        typeBit(GameObjectType::SpiderPortal) | typeBit(GameObjectType::SwingPortal) | typeBit(GameObjectType::DualPortal) |
        typeBit(GameObjectType::SoloPortal) | typeBit(GameObjectType::TeleportPortal) | typeBit(GameObjectType::TeleportOrb);

    constexpr uint64_t SIZE_PORTAL_OBJECT_TYPES = typeBit(GameObjectType::RegularSizePortal) | typeBit(GameObjectType::MiniSizePortal);

    constexpr int HOLD_SAMPLE_COUNT = 8;
    constexpr int SPIDER_LAND_TICKS = 12;
    constexpr int SPAM_STREAK_COUNT = 3;
    constexpr int WAVE_TURN_MARGIN_TICKS[] = { 4, 8, 12 };
    constexpr int WAVE_MIN_TURN_TICKS = 6;
    constexpr int WAVE_ESCAPE_TICKS = 16;
    constexpr int ROBOT_HOLD_TICKS[] = { 6, 14, 26, 45 };
    constexpr int SPIDER_RETAP_TICKS[] = { 4, 8, 12 };
    constexpr int SPIDER_MIN_RETAP_TICKS = 4;
    constexpr int SAME_CRASH_TRY_COUNT = 4;
    constexpr int SHIP_RECOVERY_TICKS[] = { 8, 16, 24 };
    constexpr int UFO_REFLAP_TICKS[] = { 6, 12, 18 };

    bool hasType(uint64_t typeMask, GameObject* gameObject) {
        auto objectType = static_cast<unsigned>(gameObject->m_objectType);
        return objectType < 64 && ((typeMask >> objectType) & 1);
    }

    float speedPortalValue(int objectId) {
        switch (objectId) {
            case 200: return 0.7f;
            case 201: return 0.9f;
            case 202: return 1.1f;
            case 203: return 1.3f;
            case 1334: return 1.6f;
            default: return 0.f;
        }
    }

    struct Stage {
        bool isEnabled;
        int ticksAllModes;
        int ticksWaveMode;
        bool isCustom;
    };

    constexpr Stage STAGE_TABLE[] = {
        { true, 2, 2, false },
        { true, 4, 4, false },
        { true, 8, 8, false },
        { true, 16, 8, false },
        { true, 24, 12, false },
        { true, 32, 16, false },
        { true, 40, 20, false },
        { false, 0, 0, false },
    };
    constexpr int STAGE_TABLE_SIZE = sizeof(STAGE_TABLE) / sizeof(STAGE_TABLE[0]);

    struct QueuedInput {
        int ticksRemaining;
        bool isDown;
    };

    struct ObjSnapshot {
        GameObject* gameObject;
        bool isObjectActivated;
        EffectGameObject* effectObject;
        bool isActivatedByEffect;
        bool byPlayer1;
        bool byPlayer2;
    };

    struct ProbeOrb {
        RingObject* ringObject = nullptr;
        PlayerObject* playerObject = nullptr;
        float touchDistanceSquared = 0.f;
        float closeDistanceSquared = 0.f;
        int touchSampleTick = -1;
        int closeSampleTick = -1;
    };

    struct HoldStats {
        int holdSamples[HOLD_SAMPLE_COUNT] = {};
        int sampleCount = 0;
        int nextSampleIndex = 0;

        void add(int tickCount) {
            holdSamples[nextSampleIndex] = std::clamp(tickCount, 1, 240);
            nextSampleIndex = (nextSampleIndex + 1) % HOLD_SAMPLE_COUNT;
            sampleCount = std::min(sampleCount + 1, HOLD_SAMPLE_COUNT);
        }

        int pick(int fallbackValue, int percentile) const {
            if (sampleCount == 0) {
                return fallbackValue;
            }
            int scratchSamples[HOLD_SAMPLE_COUNT];
            std::copy(holdSamples, holdSamples + sampleCount, scratchSamples);
            int sampleIndex = std::min(sampleCount - 1, sampleCount * percentile / 100);
            std::nth_element(scratchSamples, scratchSamples + sampleIndex, scratchSamples + sampleCount);
            return scratchSamples[sampleIndex];
        }
    };

    enum class InputKind { Tap, Wave, Robot, Other };

    struct Buffer {

        PlayerObject* ghostPlayer1 = nullptr;
        PlayerObject* ghostPlayer2 = nullptr;
        CCLayer* ghostRenderLayer = nullptr;
        unsigned ghostLayerBaseIndex = 0;
        bool performanceMode = false;
        double userBudgetMilliseconds = 6.0;
        bool automaticBudget = true;
        int keyStates[6] = { 0, 0, 0, 0, 0, 0 };
        std::unordered_set<GameObject*> simulationDisabledObjects;

        bool isSimulating = false;
        bool isInjecting = false;
        bool hasGhostDied = false;
        bool hasGhostHitPortal = false;
        bool hasRealInput = false;
        bool hasAppliedInput = false;
        bool isFirstPlayer = true;

        bool isEnabled = true;
        int stageIndex = 0;
        int ticksAllModes = 2;
        int ticksWaveMode = 2;

        std::deque<QueuedInput> queuedInputs;
        int inputShiftTicks = 0;

        float tickLengthSeconds = TICK_DURATION;
        int ticksSinceInput = 999;
        int rapidInputTicks = 0;

        bool smartEnabled = true;
        bool consoleEnabled = false;
        double budgetMilliseconds = 6.0;
        bool practiceModeOnly = true;
        int groundHorizonTicks = 130;
        int releaseShiftTicks = 0;
        int waveHorizonTicks = 65;
        bool assumeOrbHits = true;
        bool isHeld1 = false, isHeld2 = false;
        bool isAuto1 = false, isAuto2 = false;
        bool orbTimingEnabled = true;
        int orbClickDelayTicks = 0;
        int autoReleaseTick1 = -1, autoReleaseTick2 = -1;
        bool wasOrbTiming = false;
        bool tryBothOrbCandidates = true;
        bool orbClickEnabled = true;
        bool hasTouchedOrb = false;
        bool skipPressHandling = false;
        bool avoidMovingObjects = true;
        bool hasSeenMover = false;
        bool trackMovingObjects = false;
        bool autoReleaseEnabled = false;
        int autoReleaseWindowTicks = 6;
        bool wasAutoReleased = false;
        int spamGapTicks = 16;
        bool waveMarginEnabled = true;
        bool waveVersion46Mode = false;
        bool orbAlignEnabled = true;
        bool orbAlignToGround = false;
        bool ignoreTouchingObjects = true;
        bool hasRealRectangle = false;
        CCRect realRectanglePlayer1, realRectanglePlayer2;
        std::unordered_set<GameObject*> harmlessObjects;
        int baseEscapeTicks = -1;
        int baseCrashTicks = -1;

        bool lowLatencyMode = false;
        bool explainEnabled = false;
        bool deathLogEnabled = false;
        bool captureKillerObject = false;
        bool hasBaseTouchedDown = false;
        bool hasSeenKiller = false;
        int killerCollisionTick = -1;
        int killerObjectId = 0, killerObjectType = -1;
        bool killerWithoutTouch = false, isKillerDisabled = false;
        CCRect killerRectangle, ghostDeathRectangle;
        bool robustSaveMode = true;
        float ghostHorizontalShift = 0.f;
        int lateCrashTicks = 90;
        bool predictMovingObjects = false;
        bool shipBufferEnabled = false;
        int shipSpamGapTicks = 6;
        int shipMaxDelayTicks = 2;

        bool performanceLogEnabled = false;
        int simulationRunCount = 0;
        long simulatedTickCount = 0;
        bool clickBetweenFramesSync = true;
        float firstStepScale = 1.f;
        bool isInRealUpdate = false;
        float tickStartPositionX = 0.f;
        float tickDeltaX = 0.f;
        bool dashAutoReleaseEnabled = false;
        bool autoDashEnabled = false;
        std::unordered_set<GameObject*> usedOrbs;
        bool moreTimeEnabled = true;
        bool ballSmartEnabled = true;
        bool swingSmartEnabled = true;
        bool trainingMode = false;
        bool perLevelStats = true;
        bool heatmapEnabled = true;

        CCLabelBMFont* hintTextLabel = nullptr;
        CCDrawNode* heatmapNode = nullptr;
        bool ufoSmartEnabled = true;
        bool spiderSmartEnabled = true;
        int retapTick = -1;
        bool robotSmartEnabled = true;
        bool robotAutoReleaseEnabled = false;
        bool autoNormalRunsEnabled = false;
        CCLabelBMFont* autoTagLabel = nullptr;
        CCLabelBMFont* watermarkLabel = nullptr;
        bool wasUsedThisAttempt = false;
        bool learningEnabled = true;
        bool fullSimulationState = true;

        int orbWindowTicks = 6;
        int simulationTick = 0;
        int firstOrbTouchTick = -1;
        int orbUseTick = -1;
        RingObject* firstOrbRingObject = nullptr;
        PlayerObject* firstOrbPlayerObject = nullptr;
        int orbCloseSampleTick = -1;
        float orbTouchDistanceSquared = 0.f;
        float orbCloseDistanceSquared = 0.f;
        RingObject* intendedOrbObject = nullptr;
        RingObject* pressOrbRingObject = nullptr;
        int inputTickIndex = 0;
        bool isInputDown = false;
        std::vector<ProbeOrb> probeOrbCandidates;
        bool isProbing = false;
        bool driftCheckEnabled = false;
        std::vector<CCPoint>* recordedPath = nullptr;
        bool showPathEnabled = false;
        CCDrawNode* pathDrawNode = nullptr;
        int pathHoldUntilTick = 0;

        std::unordered_set<GameObject*> simulationUsedObjects;
        std::vector<ObjSnapshot> objectSnapshots;
    } state;

    HoldStats tapHoldHistory, robotHoldHistory;
    int currentTick = 0;
    int lastPressTick = -1;
    InputKind lastPressKind = InputKind::Other;
    std::chrono::steady_clock::time_point clickStartTime;
    int debugBestDelay = 0;
    int debugBestResult = -1;
    int robotUsedHoldTicks = -1;
    bool isQuickScore = false;
    bool isCrashFixed = false;
    bool isMarginUsed = false;

    void cycleStage(int direction);
    void toggleTerminal();
    std::string stageText();

    float stepScale() {
        float deltaTime = state.tickLengthSeconds;
        if (!(deltaTime > 0.001f && deltaTime < 2.f)) {
            return 1.f;
        }
        return std::clamp(TICK_DURATION / deltaTime, 0.25f, 8.f);
    }

    int st(int ticksAt240Hz) {
        if (ticksAt240Hz <= 0) {
            return ticksAt240Hz;
        }
        return std::max(1, static_cast<int>(std::lround(ticksAt240Hz * stepScale())));
    }

    std::string wineUser() {
        if (auto* userName = std::getenv("USER"); userName && *userName) {
            return userName;
        }
        if (auto* logName = std::getenv("LOGNAME"); logName && *logName) {
            return logName;
        }
        return "unknown";
    }

    std::filesystem::path wineRuntimeDir() {
        static std::filesystem::path const runtimeDirectory = [] {
            std::string user = wineUser();
            std::error_code errorCode;
            auto sharedMemoryRoot = std::filesystem::path("Z:\\dev\\shm");
            auto sharedMemoryDirectory = sharedMemoryRoot / ("afterflow-" + user);
            if (std::filesystem::is_directory(sharedMemoryRoot, errorCode)) {
                std::filesystem::create_directories(sharedMemoryDirectory, errorCode);
                if (!errorCode) {
                    return sharedMemoryDirectory;
                }
            }
            auto fallbackDirectory = std::filesystem::path("Z:\\tmp") / ("afterflow-" + user);
            std::filesystem::create_directories(fallbackDirectory, errorCode);
            return fallbackDirectory;
        }();
        return runtimeDirectory;
    }

    enum class HostOS {
        Windows,
        Linux,
        Mac,
    };

    HostOS hostOS() {
#ifdef GEODE_IS_MACOS
        return HostOS::Mac;
#else
        static HostOS const hostOperatingSystem = geode::utils::platform::isWine() ? HostOS::Linux : HostOS::Windows;
        return hostOperatingSystem;
#endif
    }

    char const* hostOSName() {
        switch (hostOS()) {
            case HostOS::Linux: return "linux";
            case HostOS::Mac: return "macos";
            default: return "windows";
        }
    }

    char const* hostOSDescription() {
        switch (hostOS()) {
            case HostOS::Linux: return "proton/wine";
            case HostOS::Mac: return "native";
            default: return "native";
        }
    }

    void ensureWineRuntimeDir() {
        if (hostOS() != HostOS::Linux) {
            return;
        }
        auto runtimeDirectory = wineRuntimeDir();
        std::error_code errorCode;
        std::filesystem::create_directories(runtimeDirectory, errorCode);
        std::filesystem::permissions(
            runtimeDirectory,
            std::filesystem::perms::owner_all,
            std::filesystem::perm_options::replace,
            errorCode
        );
    }

    std::filesystem::path tempDirectory() {
        std::error_code errorCode;
        auto directory = std::filesystem::temp_directory_path(errorCode);
        if (errorCode || directory.empty()) {
            return {};
        }
        return directory;
    }

    std::filesystem::path macRuntimeDir() {
        static std::filesystem::path const runtimeDirectory = [] {
            auto directory = tempDirectory();
            auto name = "afterflow-" + wineUser();
            return directory.empty() ? std::filesystem::path(name) : directory / name;
        }();
        return runtimeDirectory;
    }

    void ensureMacRuntimeDir() {
        if (hostOS() != HostOS::Mac) {
            return;
        }
        auto runtimeDirectory = macRuntimeDir();
        std::error_code errorCode;
        std::filesystem::create_directories(runtimeDirectory, errorCode);
        std::filesystem::permissions(
            runtimeDirectory,
            std::filesystem::perms::owner_all,
            std::filesystem::perm_options::replace,
            errorCode
        );
    }

    std::string shellQuote(std::string const& value) {
        std::string result = "'";
        for (char character : value) {
            if (character == '\'') {
                result += "'\\''";
            }
            else {
                result += character;
            }
        }
        result += '\'';
        return result;
    }

    bool isTerminalEnabled() {
        return Mod::get()->getSettingValue<bool>("terminal-enabled");
    }

    std::filesystem::path macTerminalCommandPath() {
        return macRuntimeDir() / "afterflow-terminal.command";
    }

    std::filesystem::path macTerminalPidPath() {
        return macRuntimeDir() / "terminal.pid";
    }

    std::filesystem::path macTerminalClosePath() {
        return macRuntimeDir() / "close";
    }

    std::filesystem::path terminalEnabledFlagPath() {
        return wineRuntimeDir() / "terminal-enabled";
    }

    void writeTerminalFlags() {
        if (hostOS() != HostOS::Linux) {
            return;
        }
        ensureWineRuntimeDir();
        std::ofstream terminalEnabledFile(terminalEnabledFlagPath(), std::ios::trunc);
        if (terminalEnabledFile) {
            terminalEnabledFile << (isTerminalEnabled() ? "1\n" : "0\n");
        }
    }

    std::filesystem::path terminalLogPath() {
        if (hostOS() == HostOS::Linux) {
            return wineRuntimeDir() / "terminal.log";
        }
        if (hostOS() == HostOS::Mac) {
            return macRuntimeDir() / "terminal.log";
        }
        auto directory = tempDirectory();
#ifdef _WIN32
        auto fileName = g_windowsTerminalLogFileName;
#else
        std::string fileName = "afterflow-terminal.log";
#endif
        return directory.empty() ? std::filesystem::path(fileName) : directory / fileName;
    }

    std::filesystem::path windowsTerminalScriptPath() {
        auto directory = tempDirectory();
        return directory.empty() ? std::filesystem::path("afterflow-terminal.vbs") : directory / "afterflow-terminal.vbs";
    }

    std::filesystem::path windowsTerminalLegacyBatchPath() {
        auto directory = tempDirectory();
        return directory.empty() ? std::filesystem::path("afterflow-terminal.bat") : directory / "afterflow-terminal.bat";
    }

    std::filesystem::path windowsTerminalPowerShellPath() {
        auto directory = tempDirectory();
        return directory.empty() ? std::filesystem::path("afterflow-terminal.ps1") : directory / "afterflow-terminal.ps1";
    }

    std::filesystem::path windowsTerminalPidPath() {
        auto directory = tempDirectory();
        return directory.empty() ? std::filesystem::path("afterflow-terminal.pid") : directory / "afterflow-terminal.pid";
    }

    std::filesystem::path windowsTerminalClosePath() {
        auto directory = tempDirectory();
        return directory.empty() ? std::filesystem::path("afterflow-terminal.close") : directory / "afterflow-terminal.close";
    }

#ifdef _WIN32
    std::filesystem::path windowsTerminalErrorPath() {
        auto directory = tempDirectory();
        return directory.empty() ? std::filesystem::path("afterflow-terminal.error") : directory / "afterflow-terminal.error";
    }
#endif

    std::filesystem::path terminalScriptPath() {
        if (hostOS() == HostOS::Linux) {
            return wineRuntimeDir() / "terminal.sh";
        }
        if (hostOS() == HostOS::Mac) {
            return macTerminalCommandPath();
        }
        return windowsTerminalScriptPath();
    }

    std::filesystem::path cycleTriggerPath() {
        if (hostOS() == HostOS::Linux) {
            return wineRuntimeDir() / "cycle-stage";
        }
        if (hostOS() == HostOS::Mac) {
            return macRuntimeDir() / "cycle-stage";
        }
        auto directory = tempDirectory();
        return directory.empty() ? std::filesystem::path("afterflow-cycle-stage") : directory / "afterflow-cycle-stage";
    }

    void writeWineTerminalScript() {
        ensureWineRuntimeDir();
        std::ofstream outputFile(terminalScriptPath(), std::ios::trunc | std::ios::binary);
        if (!outputFile) {
            return;
        }
        outputFile << R"SH(#!/usr/bin/env bash
set -u
export PATH="/usr/local/bin:/usr/bin:/bin:${PATH:-}"
RUNDIR="/dev/shm/afterflow-$(id -un)"
if [ ! -d /dev/shm ] || [ ! -w /dev/shm ]; then
    RUNDIR="/tmp/afterflow-$(id -un)"
fi
mkdir -p "$RUNDIR" 2>/dev/null || true
chmod 700 "$RUNDIR" 2>/dev/null || true
LOG="$RUNDIR/terminal.log"
CLOSE="$RUNDIR/close"
PIDFILE="$RUNDIR/kitty.pid"
CLASS="afterflow"

if [ "${1:-}" = "--inside" ]; then
    clear
    rm -f "$CLOSE"
    printf '\n\033[1;38;5;141mAfterflow\033[0m  predictive input buffer\n\n'
    CYCLE="$RUNDIR/cycle-stage"
    stty -echo -icanon min 1 time 0 2>/dev/null || true
    tail -n 12 -F "$LOG" &
    TAIL_PID=$!
    cleanup() {
        kill "$TAIL_PID" 2>/dev/null || true
        wait "$TAIL_PID" 2>/dev/null || true
        rm -f "$CLOSE"
        stty echo icanon 2>/dev/null || true
    }
    trap 'exit 0' INT TERM HUP
    trap cleanup EXIT
    while true; do
        if [ -e "$CLOSE" ]; then
            rm -f "$CLOSE"
            exit 0
        fi
        if ! IFS= read -rsn1 -t 0.25 ch; then
            continue
        fi
        if [ "$ch" = $'\e' ]; then
            seq="$ch"
            while IFS= read -rsn1 -t 0.05 c; do
                seq+="$c"
                case "$seq" in
                    $'\e[17~'|$'\e[17;'*'~'|$'\e[17;'*'u')
                        printf 'cycle\n' > "$CYCLE"
                        break
                        ;;
                    $'\e[20~'|$'\e[20;'*'~'|$'\e[20u'|$'\e[20;'*'u')
                        exit 0
                        ;;
                esac
                [ "${#seq}" -ge 16 ] && break
            done
        fi
    done
fi

cd "$HOME" 2>/dev/null || cd /tmp

raise_window() {
    command -v hyprctl >/dev/null 2>&1 || return 0
    local ws
    if command -v jq >/dev/null 2>&1; then
        ws="$(hyprctl activeworkspace -j 2>/dev/null | jq -r '.id // empty' | head -n 1)"
    fi
    if [ -z "${ws:-}" ]; then
        ws="$(hyprctl activeworkspace -j 2>/dev/null | sed -n 's/.*"id": *\([0-9][0-9]*\).*/\1/p' | head -n 1)"
    fi
    [ -n "${ws:-}" ] || ws=1
    local cls
    for cls in afterflow afterflow-nocapture; do
        hyprctl dispatch "hl.dsp.window.move({ workspace = $ws, window = \"class:$cls\" })" >/dev/null 2>&1 || true
        hyprctl dispatch "hl.dsp.window.center({ window = \"class:$cls\" })" >/dev/null 2>&1 || true
        hyprctl dispatch "hl.dsp.window.bring_to_top({ window = \"class:$cls\" })" >/dev/null 2>&1 || true
        hyprctl dispatch "hl.dsp.window.alter_zorder({ mode = \"top\", window = \"class:$cls\" })" >/dev/null 2>&1 || true
        hyprctl dispatch "hl.dsp.focus({ window = \"class:$cls\" })" >/dev/null 2>&1 || true
    done
}

terminal_running() {
    if command -v hyprctl >/dev/null 2>&1; then
        if hyprctl clients -j 2>/dev/null | grep -q '"class": "afterflow'; then
            return 0
        fi
    fi
    if [ -r "$PIDFILE" ]; then
        local pid
        pid="$(cat "$PIDFILE" 2>/dev/null || true)"
        if [ -n "${pid:-}" ] && kill -0 "$pid" 2>/dev/null; then
            local args
            args="$(ps -p "$pid" -o args= 2>/dev/null || true)"
            case "$args" in
                *--class*afterflow*|*afterflow-terminal.sh*) return 0 ;;
            esac
        fi
        rm -f "$PIDFILE"
    fi
    return 1
}

if terminal_running; then
    if command -v jq >/dev/null 2>&1; then
        active_ws="$(hyprctl activeworkspace -j 2>/dev/null | jq -r '.id // empty' | head -n 1)"
        active_addr="$(hyprctl activewindow -j 2>/dev/null | jq -r '.address // empty' | head -n 1)"
        terminal_info="$(hyprctl clients -j 2>/dev/null | jq -r '.[] | select(.class == "afterflow" or .class == "afterflow-nocapture") | "\(.address) \(.workspace.id)"' | head -n 1)"
        terminal_addr="${terminal_info%% *}"
        terminal_ws="${terminal_info##* }"
        if [ -n "${terminal_addr:-}" ] && { [ "$terminal_addr" != "${active_addr:-}" ] || [ "$terminal_ws" != "${active_ws:-}" ]; }; then
            raise_window
            exit 0
        fi
    fi
    : > "$CLOSE"
    exit 0
fi

KITTY="$(command -v kitty || true)"
if [ -z "$KITTY" ]; then
    echo "kitty not found on path" >"$RUNDIR/kitty.log"
    exit 1
fi
nohup "$KITTY" --title "Afterflow" --class "$CLASS" --instance-group afterflow \
    -o confirm_os_window_close=0 -o close_on_child_death=yes \
    -o initial_window_width=100c -o initial_window_height=30c -o remember_window_size=no \
    /bin/bash "$0" --inside >"$RUNDIR/kitty.log" 2>&1 &
KITTY_PID=$!
printf '%s\n' "$KITTY_PID" > "$PIDFILE"
sleep 0.4

if command -v hyprctl >/dev/null 2>&1; then
    ( sleep 0.4; raise_window; sleep 0.7; raise_window ) &
fi
)SH";
    }

    void writeMacTerminalScript() {
        ensureMacRuntimeDir();
        std::ofstream outputFile(macTerminalCommandPath(), std::ios::trunc | std::ios::binary);
        if (!outputFile) {
            return;
        }

        std::string script = R"SH(#!/usr/bin/env bash
set -u

export PATH="/opt/homebrew/bin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin:${PATH:-}"

RUNDIR="__AFTERFLOW_RUNDIR__"
LOG="$RUNDIR/terminal.log"
CLOSE="$RUNDIR/close"
PIDFILE="$RUNDIR/terminal.pid"
CYCLE="$RUNDIR/cycle-stage"

mkdir -p "$RUNDIR" 2>/dev/null || true
chmod 700 "$RUNDIR" 2>/dev/null || true
printf '%s\n' "$$" > "$PIDFILE"

clear
rm -f "$CLOSE"
printf '\033]0;Afterflow\007'
printf '\n\033[1;38;5;141mAfterflow\033[0m  predictive input buffer\n\n'

stty -echo -icanon min 1 time 0 2>/dev/null || true
tail -n 12 -F "$LOG" &
TAIL_PID=$!

cleanup() {
    kill "$TAIL_PID" 2>/dev/null || true
    wait "$TAIL_PID" 2>/dev/null || true
    rm -f "$CLOSE" "$PIDFILE"
    stty echo icanon 2>/dev/null || true
    osascript -e 'tell application "Terminal" to close (every window whose name contains "Afterflow")' >/dev/null 2>&1 || true
}
trap 'exit 0' INT TERM HUP
trap cleanup EXIT

while true; do
    if [ -e "$CLOSE" ]; then
        rm -f "$CLOSE"
        exit 0
    fi
    if ! IFS= read -rsn1 -t 1 ch; then
        continue
    fi
    if [ "$ch" = $'\e' ]; then
        seq="$ch"
        while IFS= read -rsn1 -t 1 c; do
            seq+="$c"
            case "$seq" in
                $'\e[17~'|$'\e[17;'*'~'|$'\e[17;'*'u')
                    printf 'cycle\n' > "$CYCLE"
                    break
                    ;;
                $'\e[20~'|$'\e[20;'*'~'|$'\e[20u'|$'\e[20;'*'u')
                    exit 0
                    ;;
            esac
            [ "${#seq}" -ge 16 ] && break
        done
    fi
done
)SH";

        std::string marker = "RUNDIR=\"__AFTERFLOW_RUNDIR__\"";
        auto replacement = "RUNDIR=" + shellQuote(macRuntimeDir().string());
        auto markerPosition = script.find(marker);
        if (markerPosition != std::string::npos) {
            script.replace(markerPosition, marker.size(), replacement);
        }
        outputFile << script;
        outputFile.close();

        std::error_code errorCode;
        std::filesystem::permissions(
            macTerminalCommandPath(),
            std::filesystem::perms::owner_all,
            std::filesystem::perm_options::replace,
            errorCode
        );
    }

    void writeWindowsTerminalPowerShell() {
        std::ofstream outputFile(windowsTerminalPowerShellPath(), std::ios::trunc | std::ios::binary);
        if (!outputFile) {
            return;
        }
        std::string script = R"PS($ErrorActionPreference = 'Stop'

$log = Join-Path $PSScriptRoot 'afterflow-terminal.log'
$trigger = Join-Path $PSScriptRoot 'afterflow-cycle-stage'
$pidFile = Join-Path $PSScriptRoot 'afterflow-terminal.pid'
$closeFile = Join-Path $PSScriptRoot 'afterflow-terminal.close'
$errorFile = Join-Path $PSScriptRoot 'afterflow-terminal.error'

Remove-Item -LiteralPath $closeFile -Force -ErrorAction SilentlyContinue
Remove-Item -LiteralPath $errorFile -Force -ErrorAction SilentlyContinue
Set-Content -LiteralPath $pidFile -Value $PID -Encoding ASCII -NoNewline

$timer = $null
$form = $null
try {
    Add-Type -AssemblyName System.Windows.Forms
    Add-Type -AssemblyName System.Drawing

    [System.Windows.Forms.Application]::EnableVisualStyles()

    $form = New-Object System.Windows.Forms.Form
    $form.Text = 'Afterflow'
    $form.StartPosition = [System.Windows.Forms.FormStartPosition]::CenterScreen
    $form.Size = New-Object System.Drawing.Size(1000, 700)
    $form.MinimumSize = New-Object System.Drawing.Size(480, 280)
    $form.BackColor = [System.Drawing.Color]::FromArgb(16, 17, 22)
    $form.ForeColor = [System.Drawing.Color]::FromArgb(230, 230, 235)
    $form.Font = New-Object System.Drawing.Font('Consolas', 11)
    $form.KeyPreview = $true
    $form.TopMost = $true
    $form.ShowInTaskbar = $true
    $form.WindowState = [System.Windows.Forms.FormWindowState]::Normal

    $text = New-Object System.Windows.Forms.RichTextBox
    $text.Dock = [System.Windows.Forms.DockStyle]::Fill
    $text.ReadOnly = $true
    $text.BorderStyle = [System.Windows.Forms.BorderStyle]::None
    $text.BackColor = [System.Drawing.Color]::FromArgb(16, 17, 22)
    $text.ForeColor = [System.Drawing.Color]::FromArgb(210, 235, 215)
    $text.Font = New-Object System.Drawing.Font('Consolas', 11)
    $text.WordWrap = $false
    $text.DetectUrls = $false
    $text.ScrollBars = [System.Windows.Forms.RichTextBoxScrollBars]::Vertical
    $form.Controls.Add($text)

    $text.Text = "Afterflow  predictive input buffer`r`n`r`n"

    $script:lastLength = 0
    $script:share = [System.IO.FileShare]::ReadWrite -bor [System.IO.FileShare]::Delete

    $form.Add_KeyDown({
        param($sender, $eventArgs)
        if ($eventArgs.KeyCode -eq [System.Windows.Forms.Keys]::F6) {
            Set-Content -LiteralPath $trigger -Value 'cycle' -NoNewline -Encoding ASCII
            $eventArgs.SuppressKeyPress = $true
            $eventArgs.Handled = $true
        }
        elseif ($eventArgs.KeyCode -eq [System.Windows.Forms.Keys]::F9) {
            $eventArgs.SuppressKeyPress = $true
            $eventArgs.Handled = $true
            $form.Close()
        }
    })

    $timer = New-Object System.Windows.Forms.Timer
    $timer.Interval = 100
    $timer.Add_Tick({
        if (Test-Path -LiteralPath $closeFile) {
            Remove-Item -LiteralPath $closeFile -Force -ErrorAction SilentlyContinue
            $form.Close()
            return
        }

        if (-not (Test-Path -LiteralPath $log)) {
            return
        }

        $item = Get-Item -LiteralPath $log -ErrorAction SilentlyContinue
        if (-not $item) {
            return
        }
        if ($item.Length -lt $script:lastLength) {
            $script:lastLength = 0
        }
        if ($item.Length -eq $script:lastLength) {
            return
        }

        try {
            $stream = [System.IO.File]::Open($log, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, $script:share)
        }
        catch {
            return
        }
        try {
            [void]$stream.Seek($script:lastLength, [System.IO.SeekOrigin]::Begin)
            $count = [int][Math]::Min([int64]($item.Length - $script:lastLength), [int64]65536)
            if ($count -le 0) {
                return
            }
            $buffer = New-Object byte[] $count
            $read = $stream.Read($buffer, 0, $count)
            if ($read -le 0) {
                return
            }
            $script:lastLength += $read
            $chunk = [System.Text.Encoding]::UTF8.GetString($buffer, 0, $read)
            if ($chunk.Length -gt 0) {
                $text.AppendText($chunk)
                $text.SelectionStart = $text.TextLength
                $text.ScrollToCaret()
            }
        }
        catch {
            return
        }
        finally {
            $stream.Dispose()
        }
    })
    $timer.Start()

    $form.Add_Shown({
        $form.Activate()
        $form.BringToFront()
    })

    [System.Windows.Forms.Application]::Run($form)
}
catch {
    $_ | Out-String | Set-Content -LiteralPath $errorFile -Encoding UTF8 -ErrorAction SilentlyContinue
}
finally {
    if ($timer) {
        $timer.Stop()
    }
    Remove-Item -LiteralPath $pidFile -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $closeFile -Force -ErrorAction SilentlyContinue
}
)PS";

#ifdef _WIN32
        auto logName = "'" + g_windowsTerminalLogFileName + "'";
        auto logPosition = script.find("'afterflow-terminal.log'");
        if (logPosition != std::string::npos) {
            script.replace(logPosition, std::string("'afterflow-terminal.log'").size(), logName);
        }
#endif
        outputFile << script;
    }

    void writeWindowsTerminalScript() {
        writeWindowsTerminalPowerShell();

        std::error_code legacyErrorCode;
        std::filesystem::remove(windowsTerminalLegacyBatchPath(), legacyErrorCode);

        std::ofstream outputFile(windowsTerminalScriptPath(), std::ios::trunc | std::ios::binary);
        if (!outputFile) {
            return;
        }

        outputFile << "Set fso = CreateObject(\"Scripting.FileSystemObject\")\r\n"
                      "scriptDir = fso.GetParentFolderName(WScript.ScriptFullName)\r\n"
                      "ps = fso.BuildPath(fso.GetSpecialFolder(1).Path, \"WindowsPowerShell\\v1.0\\powershell.exe\")\r\n"
                      "q = Chr(34)\r\n"
                      "command = q & ps & q & \" -NoLogo -NoProfile -STA -ExecutionPolicy Bypass -File \" & q & scriptDir & \"\\afterflow-terminal.ps1\" & q\r\n"
                      "CreateObject(\"WScript.Shell\").Run command, 0, False\r\n";
    }

    void writeTerminalScript() {
        if (hostOS() == HostOS::Linux) {
            writeWineTerminalScript();
        }
        else if (hostOS() == HostOS::Mac) {
            writeMacTerminalScript();
        }
        else {
            writeWindowsTerminalScript();
        }
    }

#ifdef _WIN32
    void closeTerminalLog() {
        if (g_terminalLogHandle != INVALID_HANDLE_VALUE) {
            CloseHandle(g_terminalLogHandle);
            g_terminalLogHandle = INVALID_HANDLE_VALUE;
        }
    }
#endif

    void removeLegacyTerminalLogs() {
        if (hostOS() == HostOS::Mac) {
            return;
        }
        if (hostOS() == HostOS::Windows) {
            auto directory = tempDirectory();
            if (!directory.empty()) {
                std::error_code errorCode;
                std::filesystem::remove(directory / "afterflow-terminal.log", errorCode);
            }
            return;
        }
        auto legacyRuntimeDirectory = std::filesystem::path("Z:\\tmp") / ("afterflow-" + wineUser());
        if (wineRuntimeDir() == legacyRuntimeDirectory) {
            return;
        }
        for (auto const* fileName : { "terminal.log", "watch.log", "kitty.log" }) {
            std::error_code errorCode;
            std::filesystem::remove(legacyRuntimeDirectory / fileName, errorCode);
        }
    }

#ifdef _WIN32
    void resetWindowsTerminalState() {
        std::error_code errorCode;
        std::filesystem::remove(windowsTerminalPidPath(), errorCode);
        std::filesystem::remove(windowsTerminalClosePath(), errorCode);
        std::filesystem::remove(windowsTerminalErrorPath(), errorCode);
    }
#endif

    void resetTerminalLog() {
        writeTerminalFlags();
        if (hostOS() == HostOS::Linux) {
            ensureWineRuntimeDir();
            auto logPath = terminalLogPath();
            std::ofstream outputFile(logPath, std::ios::trunc);
            outputFile.close();
            std::error_code errorCode;
            std::filesystem::permissions(
                logPath,
                std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
                std::filesystem::perm_options::replace,
                errorCode
            );
        }
        else if (hostOS() == HostOS::Mac) {
            ensureMacRuntimeDir();
            auto logPath = terminalLogPath();
            std::ofstream outputFile(logPath, std::ios::trunc);
            outputFile.close();
            std::error_code errorCode;
            std::filesystem::permissions(
                logPath,
                std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
                std::filesystem::perm_options::replace,
                errorCode
            );
        }
        else {
#ifdef _WIN32
            g_windowsTerminalLogFileName = fmt::format("afterflow-terminal-{}.log", GetCurrentProcessId());
            closeTerminalLog();
            resetWindowsTerminalState();
            auto logPath = terminalLogPath().wstring();
            g_terminalLogHandle = CreateFileW(
                logPath.c_str(),
                GENERIC_WRITE | DELETE,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                nullptr,
                CREATE_ALWAYS,
                FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE,
                nullptr
            );
            if (g_terminalLogHandle == INVALID_HANDLE_VALUE) {
                log::warn("could not create volatile terminal log (error {})", GetLastError());
            }
#endif
        }

        writeTerminalScript();
    }

    void appendTerminalLog(std::string const& message) {
        if (hostOS() == HostOS::Linux || hostOS() == HostOS::Mac) {
            std::ofstream outputFile(terminalLogPath(), std::ios::app);
            if (outputFile) {
                outputFile << "[afterflow] " << message << '\n' << std::flush;
            }
            return;
        }

#ifdef _WIN32
        if (g_terminalLogHandle == INVALID_HANDLE_VALUE) {
            return;
        }
        auto line = "[afterflow] " + message + "\n";
        DWORD bytesWritten = 0;
        WriteFile(g_terminalLogHandle, line.data(), static_cast<DWORD>(line.size()), &bytesWritten, nullptr);
#endif
    }

    template <class... Args>
    void say(fmt::format_string<Args...> formatString, Args&&... arguments) {
        auto message = fmt::format(formatString, std::forward<Args>(arguments)...);
        appendTerminalLog(message);
    }

    std::string keybindList(std::string_view settingName) {
        auto keybinds = Mod::get()->getSettingValue<std::vector<Keybind>>(settingName);
        if (keybinds.empty()) {
            return "unbound";
        }
        std::string outputText;
        for (auto const& keybind : keybinds) {
            if (!outputText.empty()) {
                outputText += " / ";
            }
            outputText += keybind.toString();
        }
        return outputText;
    }

    std::string stageKey() {
        return keybindList(KEYBIND_STAGE);
    }

    std::string terminalKey() {
        return keybindList(KEYBIND_TERMINAL);
    }

    void finishKeybindIntro() {
        Mod::get()->setSavedValue(KEYBIND_INTRO_DONE, true);
        (void)Mod::get()->saveData();
    }

    bool showKeybindIntro() {
#ifdef GEODE_IS_ANDROID
        // no physical keyboard on phones: the pause-menu buttons are the controls
        return false;
#endif
        if (Mod::get()->getSavedValue<bool>(KEYBIND_INTRO_DONE, false)) {
            return false;
        }
        auto* menuScene = CCScene::get();
        if (!menuScene || !menuScene->getChildByType<MenuLayer>(0)) {
            return false;
        }

        if (menuScene->getChildByType<FLAlertLayer>(0)) {
            static bool isWaiting = false;
            static int attemptCount = 0;
            if (!isWaiting && attemptCount < 20) {
                isWaiting = true;
                ++attemptCount;
                menuScene->runAction(CCSequence::create(
                    CCDelayTime::create(0.75f),
                    CallFuncExt::create([] {
                        isWaiting = false;
                        showKeybindIntro();
                    }),
                    nullptr
                ));
            }
            return false;
        }

        auto contentText = fmt::format(
            "Afterflow uses Geode keybinds, so both controls can be changed.\n\n"
            "{}  cycle stage\n{}  terminal\n\n"
            "You can set these in Geode's mod settings or keybind popup.",
            stageKey(),
            terminalKey()
        );
        createQuickPopup(
            "Afterflow keybinds",
            contentText,
            "Defaults are fine",
            "Open settings",
            [](FLAlertLayer*, bool openSettingsOnStart) {
                finishKeybindIntro();
                if (openSettingsOnStart) {
                    Loader::get()->queueInMainThread([] {
                        openSettingsPopup(Mod::get());
                    });
                }
            }
        );
        return true;
    }

    void setupKeybinds() {
        listenForKeybindSettingPresses(KEYBIND_STAGE, [](Keybind const&, bool isDown, bool isRepeat, double) {
            if (isDown && !isRepeat && GameManager::get()->m_playLayer) {
                cycleStage(1);
            }
            return true;
        });
        listenForKeybindSettingPresses(KEYBIND_TERMINAL, [](Keybind const&, bool isDown, bool isRepeat, double) {
            if (isDown && !isRepeat) {
                toggleTerminal();
            }
            return true;
        });
    }

    constexpr char const* VERSION_URL =
        "https://raw.githubusercontent.com/afterflow-gd/afterflow/refs/heads/main/version.txt";
    constexpr char const* RELEASE_URL_PREFIX =
        "https://github.com/afterflow-gd/afterflow/releases/tag/";

    std::string cleanVersion(std::string versionText) {
        auto isWhitespace = [](char character) {
            return std::isspace(static_cast<unsigned char>(character)) != 0;
        };
        while (!versionText.empty() && isWhitespace(versionText.front())) {
            versionText.erase(versionText.begin());
        }
        while (!versionText.empty() && isWhitespace(versionText.back())) {
            versionText.pop_back();
        }
        versionText = versionText.substr(0, versionText.find_first_of(" \t\r\n"));
        if (!versionText.empty() && (versionText.front() == 'v' || versionText.front() == 'V')) {
            versionText.erase(versionText.begin());
        }
        for (auto& character : versionText) {
            character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
        }
        return versionText;
    }

    bool isNewerVersion(std::string const& latestVersion, std::string const& installedVersion) {
        auto parsedLatestVersion = VersionInfo::parse(latestVersion);
        auto parsedInstalledVersion = VersionInfo::parse(installedVersion);
        if (!parsedLatestVersion.isOk() || !parsedInstalledVersion.isOk()) {
            return latestVersion != installedVersion;
        }
        return parsedInstalledVersion.unwrap() < parsedLatestVersion.unwrap();
    }

    struct UpdateNotice {
        bool isShown = false;
        std::string urlText;
        std::string installedVersion;
        std::string latestVersion;
    } pendingUpdateNotice;

    bool showUpdatePopup() {
        if (pendingUpdateNotice.isShown || pendingUpdateNotice.urlText.empty()) {
            return false;
        }
        auto* menuScene = CCScene::get();
        if (!menuScene || !menuScene->getChildByType<MenuLayer>(0)) {
            return false;
        }
        pendingUpdateNotice.isShown = true;
        auto urlText = pendingUpdateNotice.urlText;
        createQuickPopup(
            "afterflow out of date",
            fmt::format("v{} installed, latest is {}", pendingUpdateNotice.installedVersion, pendingUpdateNotice.latestVersion),
            "dismiss", "update",
            [urlText](FLAlertLayer*, bool isUpdate) {
                if (isUpdate) {

                    web::openLinkInBrowser(urlText);
                }
                if (auto* menuScene = CCScene::get()) {
                    menuScene->runAction(CCSequence::create(
                        CCDelayTime::create(0.3f),
                        CallFuncExt::create([] { showKeybindIntro(); }),
                        nullptr
                    ));
                }
            }
        );
        return true;
    }

    void checkForUpdates(bool isLoud) {
        static bool hasChecked = false;
        static auto lastCheckTime = std::chrono::steady_clock::now() - std::chrono::minutes(11);
        auto currentTime = std::chrono::steady_clock::now();
        if (hasChecked && currentTime - lastCheckTime < std::chrono::minutes(10)) {
            return;
        }
        hasChecked = true;
        lastCheckTime = currentTime;

        auto installedVersion = cleanVersion(Mod::get()->getVersion().toVString());
        async::spawn(
            web::WebRequest().get(VERSION_URL),
            [isLoud, installedVersion](web::WebResponse webResponse) {
                if (!webResponse.ok()) {
                    if (isLoud) {
                        say("version check failed (http {}), installed v{}", webResponse.code(), installedVersion);
                    }
                    return;
                }

                auto responseBody = webResponse.string();
                if (!responseBody.isOk()) {
                    if (isLoud) {
                        say("version check failed: empty response");
                    }
                    return;
                }

                auto latestVersion = cleanVersion(responseBody.unwrap());
                if (latestVersion.empty()) {
                    if (isLoud) {
                        say("version check failed: version.txt is empty");
                    }
                    return;
                }

                if (!isNewerVersion(latestVersion, installedVersion)) {
                    if (isLoud) {
                        say("version check: up to date (v{})", installedVersion);
                    }
                    return;
                }

                auto updateUrlText = fmt::format("{}{}", RELEASE_URL_PREFIX, latestVersion);
                say("out of date: v{} installed, latest is {}", installedVersion, latestVersion);
                say("update at {}", updateUrlText);
                log::warn("afterflow is out of date: v{} installed, latest is {}", installedVersion, latestVersion);
                log::warn("update at {}", updateUrlText);

                pendingUpdateNotice.installedVersion = installedVersion;
                pendingUpdateNotice.latestVersion = latestVersion;
                pendingUpdateNotice.urlText = updateUrlText;
                showUpdatePopup();
            }
        );
    }

#ifdef _WIN32

    std::filesystem::path windowsPowerShellExecutablePath() {
        wchar_t systemDirectory[MAX_PATH] = {};
        UINT systemDirectoryLength = GetSystemDirectoryW(systemDirectory, MAX_PATH);
        if (systemDirectoryLength > 0 && systemDirectoryLength < MAX_PATH) {
            auto powerShellPath = std::filesystem::path(systemDirectory) / "WindowsPowerShell" / "v1.0" / "powershell.exe";
            std::error_code errorCode;
            if (std::filesystem::exists(powerShellPath, errorCode)) {
                return powerShellPath;
            }
        }

        for (auto const* executableName : { L"powershell.exe", L"pwsh.exe" }) {
            wchar_t foundPath[MAX_PATH] = {};
            DWORD foundLength = SearchPathW(nullptr, executableName, nullptr, MAX_PATH, foundPath, nullptr);
            if (foundLength > 0 && foundLength < MAX_PATH) {
                return std::filesystem::path(foundPath);
            }
        }

        return {};
    }

    void bringWindowsTerminalToFront(HWND terminalWindow) {
        if (!terminalWindow) {
            return;
        }
        if (IsIconic(terminalWindow)) {
            ShowWindow(terminalWindow, SW_RESTORE);
        }
        else {
            ShowWindow(terminalWindow, SW_SHOW);
        }
        SetWindowPos(
            terminalWindow,
            HWND_TOPMOST,
            0,
            0,
            0,
            0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW
        );
        SetForegroundWindow(terminalWindow);
    }

    void focusWindowsTerminalWhenReady(DWORD processId) {
        std::thread([processId] {
            for (int attempt = 0; attempt < 50; ++attempt) {
                Sleep(100);
                HWND terminalWindow = FindWindowW(nullptr, L"Afterflow");
                if (!terminalWindow) {
                    continue;
                }
                DWORD windowProcessId = 0;
                GetWindowThreadProcessId(terminalWindow, &windowProcessId);
                if (windowProcessId == processId) {
                    bringWindowsTerminalToFront(terminalWindow);
                    return;
                }
            }
        }).detach();
    }

    void monitorWindowsTerminalProcess(DWORD processId) {
        std::thread([processId] {
            HANDLE processHandle = OpenProcess(
                SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId
            );
            if (!processHandle) {
                return;
            }
            DWORD waitResult = WaitForSingleObject(processHandle, 5000);
            CloseHandle(processHandle);
            if (waitResult != WAIT_OBJECT_0) {
                return;
            }

            std::ifstream errorFile(windowsTerminalErrorPath());
            std::string errorLine;
            if (!std::getline(errorFile, errorLine) || errorLine.empty()) {
                return;
            }
            log::warn("terminal process exited: {}", errorLine);
            geode::queueInMainThread([] {
                Notification::create("Afterflow terminal failed to open", NotificationIcon::Error)->show();
            });
        }).detach();
    }

    bool launchWindowsTerminal() {
        writeWindowsTerminalPowerShell();

        auto powerShellPath = windowsPowerShellExecutablePath();
        if (powerShellPath.empty()) {
            log::warn("could not find powershell.exe or pwsh.exe");
            return false;
        }

        auto scriptPath = windowsTerminalPowerShellPath();
        std::error_code errorCode;
        if (!std::filesystem::exists(scriptPath, errorCode)) {
            log::warn("terminal script was not created: {}", scriptPath.string());
            return false;
        }

        auto commandLine = L"\"" + powerShellPath.wstring()
            + L"\" -NoLogo -NoProfile -STA -ExecutionPolicy Bypass -File \""
            + scriptPath.wstring() + L"\"";
        auto workingDirectory = scriptPath.parent_path().wstring();

        STARTUPINFOW startupInfo = {};
        startupInfo.cb = sizeof(startupInfo);
        PROCESS_INFORMATION processInfo = {};
        BOOL created = CreateProcessW(
            powerShellPath.c_str(),
            commandLine.data(),
            nullptr,
            nullptr,
            FALSE,
            CREATE_NO_WINDOW,
            nullptr,
            workingDirectory.empty() ? nullptr : workingDirectory.c_str(),
            &startupInfo,
            &processInfo
        );
        if (!created) {
            log::warn("could not start powershell terminal (error {})", GetLastError());
            return false;
        }

        {
            std::ofstream pidFile(windowsTerminalPidPath(), std::ios::trunc);
            if (pidFile) {
                pidFile << processInfo.dwProcessId;
            }
        }

        log::info("started terminal process {}", processInfo.dwProcessId);
        focusWindowsTerminalWhenReady(processInfo.dwProcessId);
        monitorWindowsTerminalProcess(processInfo.dwProcessId);
        CloseHandle(processInfo.hThread);
        CloseHandle(processInfo.hProcess);
        return true;
    }

    bool launchWindowsTerminalWithWScript() {
        writeWindowsTerminalScript();

        auto scriptPath = windowsTerminalScriptPath();
        std::error_code errorCode;
        if (!std::filesystem::exists(scriptPath, errorCode)) {
            log::warn("terminal launcher was not created: {}", scriptPath.string());
            return false;
        }

        auto workingDirectory = scriptPath.parent_path().wstring();
        auto parameters = L"\"" + scriptPath.wstring() + L"\"";
        auto shellResult = ShellExecuteW(
            nullptr,
            L"open",
            L"wscript.exe",
            parameters.c_str(),
            workingDirectory.empty() ? nullptr : workingDirectory.c_str(),
            SW_HIDE
        );
        auto shellResultCode = reinterpret_cast<INT_PTR>(shellResult);
        if (shellResultCode <= 32) {
            log::warn("could not open terminal launcher (shell execute returned {})", shellResultCode);
            return false;
        }
        return true;
    }

    bool windowsTerminalRunning() {
        std::ifstream inputFile(windowsTerminalPidPath());
        unsigned long processId = 0;
        if (!(inputFile >> processId) || processId == 0) {
            return false;
        }

        HANDLE processHandle = OpenProcess(
            PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, static_cast<DWORD>(processId)
        );
        if (!processHandle) {
            std::error_code processErrorCode;
            std::filesystem::remove(windowsTerminalPidPath(), processErrorCode);
            return false;
        }

        DWORD waitResult = WaitForSingleObject(processHandle, 0);
        wchar_t imagePath[MAX_PATH] = {};
        DWORD imagePathLength = MAX_PATH;
        bool hasImagePath = QueryFullProcessImageNameW(processHandle, 0, imagePath, &imagePathLength) != 0;
        CloseHandle(processHandle);

        std::wstring processName(imagePath, hasImagePath ? imagePathLength : 0);
        for (auto& character : processName) {
            character = static_cast<wchar_t>(std::towlower(character));
        }
        bool isPowerShellProcess = processName.find(L"powershell.exe") != std::wstring::npos
            || processName.find(L"pwsh.exe") != std::wstring::npos;
        if (waitResult == WAIT_TIMEOUT && isPowerShellProcess) {
            return true;
        }

        std::error_code cleanupErrorCode;
        std::filesystem::remove(windowsTerminalPidPath(), cleanupErrorCode);
        return false;
    }
#endif

#ifdef GEODE_IS_MACOS
    bool macTerminalRunning() {
        std::ifstream inputFile(macTerminalPidPath());
        long processId = 0;
        if (!(inputFile >> processId) || processId <= 0) {
            return false;
        }
        if (kill(static_cast<pid_t>(processId), 0) == 0) {
            return true;
        }

        std::error_code errorCode;
        std::filesystem::remove(macTerminalPidPath(), errorCode);
        return false;
    }
#endif

    void closeTerminal() {
        if (hostOS() == HostOS::Linux) {
            ensureWineRuntimeDir();
            auto closePath = wineRuntimeDir() / "close";
            std::ofstream closeFile(closePath, std::ios::trunc);
            if (closeFile) {
                closeFile << "close\n";
            }
            else {
                log::warn("could not write terminal close trigger {}", closePath.string());
            }

            std::error_code errorCode;
            std::filesystem::remove(wineRuntimeDir() / "open-kitty", errorCode);
            return;
        }

        if (hostOS() == HostOS::Mac) {
#ifdef GEODE_IS_MACOS
            if (!macTerminalRunning()) {
                return;
            }
            std::ofstream closeFile(macTerminalClosePath(), std::ios::trunc);
            if (closeFile) {
                closeFile << "close\n";
            }
            else {
                log::warn("could not write terminal close trigger");
            }
#endif
            return;
        }

#ifdef _WIN32
        if (windowsTerminalRunning()) {
            std::ofstream closeFile(windowsTerminalClosePath(), std::ios::trunc);
            if (closeFile) {
                closeFile << "close\n";
            }
            else {
                log::warn("could not write terminal close trigger");
            }
        }

        if (HWND terminalWindow = FindWindowW(nullptr, L"Afterflow")) {
            PostMessageW(terminalWindow, WM_CLOSE, 0, 0);
        }
#endif
    }

    void toggleTerminal() {
        if (!isTerminalEnabled()) {
            log::info("terminal toggle ignored (disabled)");
            return;
        }

        std::error_code errorCode;
        if (!std::filesystem::exists(terminalScriptPath(), errorCode)) {
            writeTerminalScript();
        }

        if (hostOS() == HostOS::Linux) {
            ensureWineRuntimeDir();
            std::error_code cleanupErrorCode;
            std::filesystem::remove(wineRuntimeDir() / "close", cleanupErrorCode);

            auto openTriggerPath = wineRuntimeDir() / "open-kitty";
            std::ofstream triggerFile(openTriggerPath, std::ios::trunc);
            if (triggerFile) {
                triggerFile << "toggle\n";
                log::info("wrote kitty trigger {}", openTriggerPath.string());
            }
            else {
                log::warn("could not write kitty trigger {}", openTriggerPath.string());
            }
            return;
        }

        if (hostOS() == HostOS::Mac) {
#ifdef GEODE_IS_MACOS
            ensureMacRuntimeDir();
            if (macTerminalRunning()) {
                std::ofstream closeFile(macTerminalClosePath(), std::ios::trunc);
                if (closeFile) {
                    closeFile << "close\n";
                }
                else {
                    log::warn("could not write terminal close trigger");
                }
                return;
            }

            auto command = "/usr/bin/open -a Terminal " + shellQuote(terminalScriptPath().string());
            int openResult = std::system(command.c_str());
            if (openResult != 0) {
                log::warn("could not open terminal launcher (open returned {})", openResult);
            }
#else
            log::warn("mac terminal is only available on macos");
#endif
            return;
        }

#ifdef _WIN32
        if (HWND terminalWindow = FindWindowW(nullptr, L"Afterflow")) {
            if (GetForegroundWindow() != terminalWindow) {
                bringWindowsTerminalToFront(terminalWindow);
                return;
            }
            closeTerminal();
            return;
        }

        if (windowsTerminalRunning()) {
            closeTerminal();
            return;
        }

        if (!launchWindowsTerminal() && !launchWindowsTerminalWithWScript()) {
            Notification::create("Afterflow terminal failed to open", NotificationIcon::Error)->show();
        }
#else
        log::warn("terminal is not available on this platform");
#endif
    }

    bool isGhost(PlayerObject* playerObject) {
        return playerObject && (playerObject == state.ghostPlayer1 || playerObject == state.ghostPlayer2);
    }

    void resetPending() {
        state.queuedInputs.clear();
        state.inputShiftTicks = 0;
        state.releaseShiftTicks = 0;
    }

    void readSettings() {
        state.smartEnabled = true;
        state.budgetMilliseconds = 8.0;
        state.automaticBudget = false;
        state.practiceModeOnly = false;
        state.groundHorizonTicks = 130;
        state.waveHorizonTicks = 65;
        state.assumeOrbHits = true;
        state.orbClickEnabled = true;
        state.tryBothOrbCandidates = true;
        state.avoidMovingObjects = false;
        state.autoReleaseEnabled = false;
        state.autoReleaseWindowTicks = 6;
        state.driftCheckEnabled = false;
        state.spamGapTicks = 16;
        state.orbAlignEnabled = true;
        state.orbTimingEnabled = true;
        state.orbAlignToGround = false;
        state.fullSimulationState = true;
        state.learningEnabled = false;
        state.robotSmartEnabled = true;
        state.spiderSmartEnabled = true;
        state.ufoSmartEnabled = true;
        state.ballSmartEnabled = true;
        state.swingSmartEnabled = true;
        state.trainingMode = false;
        state.perLevelStats = false;
        state.heatmapEnabled = false;
        state.moreTimeEnabled = false;
        state.dashAutoReleaseEnabled = false;
        state.performanceLogEnabled = false;
        state.shipBufferEnabled = true;
        state.lateCrashTicks = 90;
        state.robustSaveMode = false;
        state.ignoreTouchingObjects = true;
        state.deathLogEnabled = false;
        state.explainEnabled = false;
        state.lowLatencyMode = false;
        state.predictMovingObjects = false;
        state.shipMaxDelayTicks = 4;
        state.shipSpamGapTicks = 6;
        state.clickBetweenFramesSync = true;
        state.robotAutoReleaseEnabled = false;
        state.autoNormalRunsEnabled = false;
        state.waveVersion46Mode = false;
        state.waveMarginEnabled = true;
        state.orbWindowTicks = 6;
        state.performanceMode = false;
        state.userBudgetMilliseconds = 8.0;
        state.wasAutoReleased = false;
        state.consoleEnabled = false;
        state.showPathEnabled = false;
    }

    void clickBudget() {
        double budgetMilliseconds = state.userBudgetMilliseconds;
        if (state.automaticBudget) {
            auto* cocosDirector = CCDirector::get();
            double frameInterval = cocosDirector->getAnimationInterval() * 1000.0;
            double frameDuration = cocosDirector->getDeltaTime() * 1000.0;
            if (frameInterval > 0.0 && frameDuration > std::max(frameInterval * 2.0, 20.0)) {
                budgetMilliseconds = std::min(budgetMilliseconds, 4.0);
            }
        }
        state.budgetMilliseconds = budgetMilliseconds;
    }

    void applyStage() {
        auto const& stageDefinition = STAGE_TABLE[state.stageIndex];
        state.isEnabled = stageDefinition.isEnabled;
        state.ticksAllModes = stageDefinition.ticksAllModes;
        state.ticksWaveMode = stageDefinition.ticksWaveMode;
    }

    std::string stageText() {
        if (!state.isEnabled) {
            return "off (raw input)";
        }
        return fmt::format("{}t all / {}t wave", state.ticksAllModes, state.ticksWaveMode);
    }

    std::string stageBanner() {
        if (!state.isEnabled) {
            return "stage off (100% raw input)";
        }
        return fmt::format("stage {}: {} ticks all | {} ticks wave ({:g}f all | {:g}f wave @ 60fps)",
            state.stageIndex + 1, state.ticksAllModes, state.ticksWaveMode, state.ticksAllModes / 4.f, state.ticksWaveMode / 4.f);
    }

#ifdef GEODE_IS_ANDROID
    class AfterflowOverlay : public CCLayer {
    protected:
        CCLayerColor* m_dim = nullptr;
        CCScale9Sprite* m_panel = nullptr;
        CCLabelBMFont* m_stageLabel = nullptr;
        ButtonSprite* m_powerSprite = nullptr;
        CCMenuItemSpriteExtra* m_fab = nullptr;
        bool m_open = false;
        std::chrono::steady_clock::time_point m_openedAt{};

        void refresh() {
            if (!m_stageLabel) {
                return;
            }
            if (state.isEnabled) {
                m_stageLabel->setString(fmt::format("stage {} - {}t all / {}t wave", state.stageIndex + 1, state.ticksAllModes, state.ticksWaveMode).c_str());
            }
            else {
                m_stageLabel->setString("stage off - raw input");
            }
            if (m_powerSprite) {
                m_powerSprite->setString(state.isEnabled ? "on" : "off");
            }
        }

        void setOpen(bool open) {
            m_open = open;
            m_openedAt = std::chrono::steady_clock::now();
            if (m_dim) {
                m_dim->setVisible(open);
            }
            refresh();
            log::info("[afterflow] menu {}", open ? "open" : "closed");
        }

    public:
        static AfterflowOverlay* create() {
            auto* overlay = new AfterflowOverlay();
            if (overlay && overlay->init()) {
                overlay->autorelease();
                return overlay;
            }
            CC_SAFE_DELETE(overlay);
            return nullptr;
        }

        bool init() override {
            if (!CCLayer::init()) {
                return false;
            }
            this->setID("afterflow-overlay"_spr);

            auto winSize = CCDirector::get()->getWinSize();
            log::info("[afterflow] overlay init winSize {}x{}", winSize.width, winSize.height);

            m_dim = CCLayerColor::create({ 0, 0, 0, 110 }, winSize.width, winSize.height);
            m_dim->setVisible(false);
            m_dim->setID("afterflow-dim"_spr);
            this->addChild(m_dim, 1);

            m_panel = CCScale9Sprite::create("GJ_square01.png");
            m_panel->setContentSize({ 280.f, 150.f });
            m_panel->setPosition({ winSize.width / 2.f, winSize.height / 2.f });
            m_panel->setID("afterflow-panel"_spr);
            m_dim->addChild(m_panel, 2);

            auto title = CCLabelBMFont::create("AFTERFLOW", "goldFont.fnt");
            title->setScale(0.75f);
            title->setPosition({ 140.f, 124.f });
            m_panel->addChild(title);

            m_stageLabel = CCLabelBMFont::create("", "bigFont.fnt");
            m_stageLabel->setScale(0.4f);
            m_stageLabel->setPosition({ 140.f, 96.f });
            m_panel->addChild(m_stageLabel);

            auto menu = CCMenu::create();
            menu->setPosition({ 0.f, 0.f });
            m_panel->addChild(menu);

            auto prevButton = CCMenuItemSpriteExtra::create(
                ButtonSprite::create("<", "goldFont.fnt", "GJ_button_04.png", .75f),
                this,
                menu_selector(AfterflowOverlay::onPrev)
            );
            prevButton->setPosition({ 45.f, 45.f });
            menu->addChild(prevButton);

            auto powerButton = CCMenuItemSpriteExtra::create(
                ButtonSprite::create("on", "goldFont.fnt", "GJ_button_01.png", .85f),
                this,
                menu_selector(AfterflowOverlay::onPower)
            );
            powerButton->setPosition({ 140.f, 45.f });
            menu->addChild(powerButton);
            m_powerSprite = typeinfo_cast<ButtonSprite*>(powerButton->getNormalImage());

            auto nextButton = CCMenuItemSpriteExtra::create(
                ButtonSprite::create(">", "goldFont.fnt", "GJ_button_04.png", .75f),
                this,
                menu_selector(AfterflowOverlay::onNext)
            );
            nextButton->setPosition({ 235.f, 45.f });
            menu->addChild(nextButton);

            auto closeButton = CCMenuItemSpriteExtra::create(
                ButtonSprite::create("x", "goldFont.fnt", "GJ_button_06.png", .55f),
                this,
                menu_selector(AfterflowOverlay::onClose)
            );
            closeButton->setPosition({ 252.f, 124.f });
            menu->addChild(closeButton);

            auto fabMenu = CCMenu::create();
            fabMenu->setPosition({ 0.f, 0.f });
            this->addChild(fabMenu, 3);

            m_fab = CCMenuItemSpriteExtra::create(
                ButtonSprite::create("A", "goldFont.fnt", "GJ_button_01.png", .9f),
                this,
                menu_selector(AfterflowOverlay::onFab)
            );
            m_fab->setPosition({ winSize.width - 34.f, 34.f });
            m_fab->setID("afterflow-fab"_spr);
            fabMenu->addChild(m_fab);

            this->setTouchEnabled(true);
            refresh();
            log::info("[afterflow] overlay ready: fab at {}x{}", winSize.width - 34.f, 34.f);
            return true;
        }

        void onFab(CCObject*) {
            setOpen(!m_open);
            Notification::create(fmt::format("afterflow menu {}", m_open ? "on" : "off"), NotificationIcon::Info, 0.8f)->show();
        }

        void onClose(CCObject*) {
            setOpen(false);
        }

        void onPrev(CCObject*) {
            cycleStage(-1);
            refresh();
        }

        void onNext(CCObject*) {
            cycleStage(1);
            refresh();
        }

        void onPower(CCObject*) {
            state.stageIndex = state.isEnabled ? (STAGE_TABLE_SIZE - 1) : 0;
            Mod::get()->setSavedValue("stage", state.stageIndex);
            resetPending();
            applyStage();
            refresh();
            Notification::create(stageText(), NotificationIcon::Info, 1.2f)->show();
        }

        bool ccTouchBegan(CCTouch* touch, CCEvent*) override {
            if (!m_open || !m_panel) {
                return false;
            }
            auto point = touch->getLocation();
            log::info("[afterflow] touch at {:.0f},{:.0f} (menu open)", point.x, point.y);
            return true;
        }

        void registerWithTouchDispatcher() override {
            CCTouchDispatcher::get()->addTargetedDelegate(this, -500, false);
        }
    };
#endif

    void dropGhosts() {
        for (auto** ghostSlot : { &state.ghostPlayer1, &state.ghostPlayer2 }) {
            if (*ghostSlot) {
                (*ghostSlot)->removeFromParentAndCleanup(true);
                (*ghostSlot)->release();
                *ghostSlot = nullptr;
            }
        }
        if (state.ghostRenderLayer) {
            state.ghostRenderLayer->removeAllChildrenWithCleanup(true);
            state.ghostRenderLayer->release();
            state.ghostRenderLayer = nullptr;
        }
        state.isSimulating = false;
        state.isInjecting = false;
        state.rapidInputTicks = 0;
        state.wasAutoReleased = false;
        state.pathDrawNode = nullptr;
        resetPending();
    }

    PlayerObject* makeGhost(PlayLayer* gameLayer) {
        if (!state.ghostRenderLayer) {
            state.ghostRenderLayer = CCLayer::create();
            state.ghostRenderLayer->retain();
        }
        auto* ghostPlayer = PlayerObject::create(1, 1, gameLayer, state.ghostRenderLayer, true);
        ghostPlayer->retain();
        ghostPlayer->setPosition(CCPoint(0.f, 105.f));
        ghostPlayer->setVisible(false);
        ghostPlayer->m_playEffects = false;
        ghostPlayer->unscheduleUpdate();
        ghostPlayer->stopAllActions();
        state.ghostRenderLayer->addChild(ghostPlayer);
        return ghostPlayer;
    }

    void sweepGhostLayer() {
        if (!state.ghostRenderLayer) {
            return;
        }
        auto* childArray = state.ghostRenderLayer->getChildren();
        while (childArray && childArray->count() > state.ghostLayerBaseIndex) {
            auto* childNode = static_cast<CCNode*>(childArray->lastObject());
            state.ghostRenderLayer->removeChild(childNode, true);
        }
    }

    void sendButton(PlayLayer* gameLayer, bool isDown) {
        state.isInjecting = true;
        state.hasAppliedInput = isDown;
        gameLayer->handleButton(isDown, 1, state.isFirstPlayer);
        state.isInjecting = false;
    }

    void pressGhost(PlayerObject* ghostPlayer, bool isDown) {
        (ghostPlayer == state.ghostPlayer1 ? state.isHeld1 : state.isHeld2) = isDown;
        if (isDown) {
            ghostPlayer->pushButton(PlayerButton::Jump);
        }
        else {
            ghostPlayer->releaseButton(PlayerButton::Jump);
        }
    }

    void copyState(PlayerObject* ghostPlayer, PlayerObject* realPlayer) {
        ghostPlayer->setVisible(false);
        ghostPlayer->resetTouchedRings(true);
        ghostPlayer->copyAttributes(realPlayer);
        ghostPlayer->m_playEffects = false;
        ghostPlayer->setScale(realPlayer->getScale());
        ghostPlayer->setContentSize(realPlayer->getContentSize());
        ghostPlayer->m_width = realPlayer->m_width;
        ghostPlayer->m_height = realPlayer->m_height;
        ghostPlayer->m_gravityMod = realPlayer->m_gravityMod;
        ghostPlayer->m_isOnGround = realPlayer->m_isOnGround;
        ghostPlayer->m_isUpsideDown = realPlayer->m_isUpsideDown;
        ghostPlayer->m_vehicleSize = realPlayer->m_vehicleSize;
        ghostPlayer->m_playerSpeed = realPlayer->m_playerSpeed;
        ghostPlayer->m_isDashing = realPlayer->m_isDashing;
        ghostPlayer->m_lastActivatedPortal = realPlayer->m_lastActivatedPortal;
        if (state.ghostHorizontalShift != 0.f) {

            CCPoint position = realPlayer->getPosition();
            position.x += state.ghostHorizontalShift;
            ghostPlayer->setPosition(position);
            ghostPlayer->m_position = position;
        }

        ghostPlayer->m_holdingButtons = realPlayer->m_holdingButtons;
        ghostPlayer->m_jumpBuffered = realPlayer->m_jumpBuffered;
        ghostPlayer->m_wasJumpBuffered = realPlayer->m_wasJumpBuffered;
        ghostPlayer->m_stateJumpBuffered = realPlayer->m_stateJumpBuffered;
        ghostPlayer->m_stateRingJump = realPlayer->m_stateRingJump;
        ghostPlayer->m_stateRingJump2 = realPlayer->m_stateRingJump2;
        ghostPlayer->m_wasRobotJump = realPlayer->m_wasRobotJump;
        ghostPlayer->m_touchedRing = realPlayer->m_touchedRing;
        ghostPlayer->m_touchedPad = realPlayer->m_touchedPad;
        ghostPlayer->m_touchedGravityPortal = realPlayer->m_touchedGravityPortal;
        if (!state.fullSimulationState) {
            return;
        }

        ghostPlayer->m_isShip = realPlayer->m_isShip;
        ghostPlayer->m_isBird = realPlayer->m_isBird;
        ghostPlayer->m_isBall = realPlayer->m_isBall;
        ghostPlayer->m_isDart = realPlayer->m_isDart;
        ghostPlayer->m_isRobot = realPlayer->m_isRobot;
        ghostPlayer->m_isSpider = realPlayer->m_isSpider;
        ghostPlayer->m_isSwing = realPlayer->m_isSwing;
        ghostPlayer->m_isGoingLeft = realPlayer->m_isGoingLeft;
        ghostPlayer->m_isSideways = realPlayer->m_isSideways;
        ghostPlayer->m_reverseSync = realPlayer->m_reverseSync;
        ghostPlayer->m_reverseRelated = realPlayer->m_reverseRelated;
        ghostPlayer->m_maybeReverseSpeed = realPlayer->m_maybeReverseSpeed;
        ghostPlayer->m_maybeReverseAcceleration = realPlayer->m_maybeReverseAcceleration;
        ghostPlayer->m_speedMultiplier = realPlayer->m_speedMultiplier;
        ghostPlayer->m_gravity = realPlayer->m_gravity;
        ghostPlayer->m_yStart = realPlayer->m_yStart;
        ghostPlayer->m_fixGravityBug = realPlayer->m_fixGravityBug;
        ghostPlayer->m_fixRobotJump = realPlayer->m_fixRobotJump;
        ghostPlayer->m_enable22Changes = realPlayer->m_enable22Changes;
        ghostPlayer->m_enableImpulseFix = realPlayer->m_enableImpulseFix;
        ghostPlayer->m_totalTime = realPlayer->m_totalTime;

        ghostPlayer->m_lastPosition = realPlayer->m_lastPosition;
        ghostPlayer->m_yVelocity = realPlayer->m_yVelocity;
        ghostPlayer->m_fallSpeed = realPlayer->m_fallSpeed;
        ghostPlayer->m_yVelocityBeforeSlope = realPlayer->m_yVelocityBeforeSlope;
        ghostPlayer->m_yVelocityRelated = realPlayer->m_yVelocityRelated;
        ghostPlayer->m_yVelocityRelated3 = realPlayer->m_yVelocityRelated3;
        ghostPlayer->m_groundYVelocity = realPlayer->m_groundYVelocity;
        ghostPlayer->m_accelerationOrSpeed = realPlayer->m_accelerationOrSpeed;
        ghostPlayer->m_isAccelerating = realPlayer->m_isAccelerating;
        ghostPlayer->m_maybeIsFalling = realPlayer->m_maybeIsFalling;
        ghostPlayer->m_isOnGround2 = realPlayer->m_isOnGround2;
        ghostPlayer->m_isOnGround3 = realPlayer->m_isOnGround3;
        ghostPlayer->m_isOnGround4 = realPlayer->m_isOnGround4;
        ghostPlayer->m_stateOnGround = realPlayer->m_stateOnGround;
        ghostPlayer->m_lastLandTime = realPlayer->m_lastLandTime;
        ghostPlayer->m_lastJumpTime = realPlayer->m_lastJumpTime;
        ghostPlayer->m_lastFlipTime = realPlayer->m_lastFlipTime;
        ghostPlayer->m_lastSpiderFlipTime = realPlayer->m_lastSpiderFlipTime;
        ghostPlayer->m_lastGroundedPos = realPlayer->m_lastGroundedPos;
        ghostPlayer->m_blackOrbRelated = realPlayer->m_blackOrbRelated;
        ghostPlayer->m_padRingRelated = realPlayer->m_padRingRelated;
        ghostPlayer->m_ringJumpRelated = realPlayer->m_ringJumpRelated;

        ghostPlayer->m_isOnSlope = realPlayer->m_isOnSlope;
        ghostPlayer->m_wasOnSlope = realPlayer->m_wasOnSlope;
        ghostPlayer->m_slopeVelocity = realPlayer->m_slopeVelocity;
        ghostPlayer->m_maybeUpsideDownSlope = realPlayer->m_maybeUpsideDownSlope;
        ghostPlayer->m_currentSlope = realPlayer->m_currentSlope;
        ghostPlayer->m_currentSlope2 = realPlayer->m_currentSlope2;
        ghostPlayer->m_currentPotentialSlope = realPlayer->m_currentPotentialSlope;
        ghostPlayer->m_isCollidingWithSlope = realPlayer->m_isCollidingWithSlope;
        ghostPlayer->m_collidingWithSlopeId = realPlayer->m_collidingWithSlopeId;
        ghostPlayer->m_slopeFlipGravityRelated = realPlayer->m_slopeFlipGravityRelated;
        ghostPlayer->m_slopeAngle = realPlayer->m_slopeAngle;
        ghostPlayer->m_slopeAngleRadians = realPlayer->m_slopeAngleRadians;
        ghostPlayer->m_slopeRotation = realPlayer->m_slopeRotation;
        ghostPlayer->m_currentSlopeYVelocity = realPlayer->m_currentSlopeYVelocity;
        ghostPlayer->m_isCurrentSlopeTop = realPlayer->m_isCurrentSlopeTop;
        ghostPlayer->m_slopeStartTime = realPlayer->m_slopeStartTime;
        ghostPlayer->m_slopeEndTime = realPlayer->m_slopeEndTime;
        ghostPlayer->m_slopeSlidingMaybeRotated = realPlayer->m_slopeSlidingMaybeRotated;
        ghostPlayer->m_stateDartSlide = realPlayer->m_stateDartSlide;

        ghostPlayer->m_lastGroundObject = realPlayer->m_lastGroundObject;
        ghostPlayer->m_preLastGroundObject = realPlayer->m_preLastGroundObject;
        ghostPlayer->m_maybeLastGroundObject = realPlayer->m_maybeLastGroundObject;
        ghostPlayer->m_collidedObject = realPlayer->m_collidedObject;
        ghostPlayer->m_objectSnappedTo = realPlayer->m_objectSnappedTo;
        ghostPlayer->m_snapDistance = realPlayer->m_snapDistance;
        ghostPlayer->m_lastCollisionBottom = realPlayer->m_lastCollisionBottom;
        ghostPlayer->m_lastCollisionTop = realPlayer->m_lastCollisionTop;
        ghostPlayer->m_lastCollisionLeft = realPlayer->m_lastCollisionLeft;
        ghostPlayer->m_lastCollisionRight = realPlayer->m_lastCollisionRight;
        ghostPlayer->m_collidedTopMinY = realPlayer->m_collidedTopMinY;
        ghostPlayer->m_collidedBottomMaxY = realPlayer->m_collidedBottomMaxY;
        ghostPlayer->m_collidedLeftMaxX = realPlayer->m_collidedLeftMaxX;
        ghostPlayer->m_collidedRightMinX = realPlayer->m_collidedRightMinX;

        ghostPlayer->m_dashX = realPlayer->m_dashX;
        ghostPlayer->m_dashY = realPlayer->m_dashY;
        ghostPlayer->m_dashAngle = realPlayer->m_dashAngle;
        ghostPlayer->m_dashStartTime = realPlayer->m_dashStartTime;
        ghostPlayer->m_dashRing = realPlayer->m_dashRing;
        ghostPlayer->m_dashFireFrame = realPlayer->m_dashFireFrame;

        ghostPlayer->m_stateForce = realPlayer->m_stateForce;
        ghostPlayer->m_stateForceVector = realPlayer->m_stateForceVector;
        ghostPlayer->m_affectedByForces = realPlayer->m_affectedByForces;
        ghostPlayer->m_stateBoostX = realPlayer->m_stateBoostX;
        ghostPlayer->m_stateBoostY = realPlayer->m_stateBoostY;
        ghostPlayer->m_stateNoStickX = realPlayer->m_stateNoStickX;
        ghostPlayer->m_stateNoStickY = realPlayer->m_stateNoStickY;
        ghostPlayer->m_stateHitHead = realPlayer->m_stateHitHead;
        ghostPlayer->m_stateFlipGravity = realPlayer->m_stateFlipGravity;
        ghostPlayer->m_stateNoAutoJump = realPlayer->m_stateNoAutoJump;
        ghostPlayer->m_stateScale = realPlayer->m_stateScale;
        ghostPlayer->m_maybeStateForce2 = realPlayer->m_maybeStateForce2;
    }

    InputKind kindOf(PlayerObject* playerObject) {
        if (!playerObject || playerObject->m_isShip) {
            return InputKind::Other;
        }
        if (playerObject->m_isDart) {
            return InputKind::Wave;
        }
        return playerObject->m_isRobot ? InputKind::Robot : InputKind::Tap;
    }

    void recordRealInput(bool isDown, PlayerObject* player1) {
        if (isDown) {
            lastPressTick = currentTick;
            lastPressKind = kindOf(player1);
            return;
        }
        if (lastPressTick < 0) {
            return;
        }
        int heldTicks = currentTick - lastPressTick;
        lastPressTick = -1;
        if (state.wasAutoReleased) {
            return;
        }
        switch (lastPressKind) {
            case InputKind::Tap: tapHoldHistory.add(heldTicks); break;
            case InputKind::Robot: robotHoldHistory.add(heldTicks); break;
            default: break;
        }
    }

    void forgetPress() {
        lastPressTick = -1;
    }

    int holdEstimate() {
        return tapHoldHistory.pick(st(12), 50);
    }

    void snapshotObject(GameObject* gameObject) {
        if (!gameObject || state.simulationUsedObjects.contains(gameObject)) {
            return;
        }
        state.simulationUsedObjects.insert(gameObject);
        ObjSnapshot snapshot { gameObject, gameObject->m_isActivated, nullptr, false, false, false };
        if (auto* effectObject = typeinfo_cast<EffectGameObject*>(gameObject)) {
            snapshot.effectObject = effectObject;
            snapshot.isActivatedByEffect = effectObject->m_activated;
            snapshot.byPlayer1 = effectObject->m_activatedByPlayer1;
            snapshot.byPlayer2 = effectObject->m_activatedByPlayer2;
        }
        state.objectSnapshots.push_back(snapshot);
    }

    void restoreObjects() {
        for (auto const& snapshot : state.objectSnapshots) {
            snapshot.gameObject->m_isActivated = snapshot.isObjectActivated;
            if (snapshot.effectObject) {
                snapshot.effectObject->m_activated = snapshot.isActivatedByEffect;
                snapshot.effectObject->m_activatedByPlayer1 = snapshot.byPlayer1;
                snapshot.effectObject->m_activatedByPlayer2 = snapshot.byPlayer2;
            }
        }
        state.objectSnapshots.clear();
        state.simulationUsedObjects.clear();
        state.usedOrbs.clear();
        state.simulationDisabledObjects.clear();
    }

    float orbDistanceSq(PlayerObject* playerObject, RingObject* ringObject) {
        if (!playerObject || !ringObject) {
            return 0.f;
        }
        CCPoint pointA = playerObject->getPosition();
        CCPoint pointB = ringObject->getPosition();
        float deltaX = pointA.x - pointB.x;
        float deltaY = pointA.y - pointB.y;
        return deltaX * deltaX + deltaY * deltaY;
    }

    void advanceGhost(PlayerObject* ghostPlayer, float deltaTime) {
        ghostPlayer->update(deltaTime);
        ghostPlayer->updateRotation(deltaTime);
    }

    void checkGhost(PlayLayer* gameLayer, PlayerObject* ghostPlayer, float deltaTime) {
        ghostPlayer->m_collisionLogTop->removeAllObjects();
        ghostPlayer->m_collisionLogBottom->removeAllObjects();
        ghostPlayer->m_collisionLogLeft->removeAllObjects();
        ghostPlayer->m_collisionLogRight->removeAllObjects();
        gameLayer->checkCollisions(ghostPlayer, deltaTime, false);
    }

    bool isFlying(PlayerObject* playerObject) {
        return playerObject->m_isDart || playerObject->m_isBird || playerObject->m_isSwing || playerObject->m_isShip;
    }

    void releaseAutoClicks() {
        if (state.isAuto1) {
            if (!state.isHeld1 && state.ghostPlayer1) {
                state.ghostPlayer1->releaseButton(PlayerButton::Jump);
            }
            state.isAuto1 = false;
        }
        if (state.isAuto2) {
            if (!state.isHeld2 && state.ghostPlayer2) {
                state.ghostPlayer2->releaseButton(PlayerButton::Jump);
            }
            state.isAuto2 = false;
        }
    }

    enum class SimEnd { Ok, StopSearch };

    int simulate(PlayLayer* gameLayer, PlayerObject* player1, PlayerObject* player2, bool isDual, int delayTicks, bool isDown, int horizonTicks, int releaseAfterTicks,
        int stopAtTick, SimEnd* simulationEnd) {
        *simulationEnd = SimEnd::Ok;
        if (!state.ghostPlayer1) {
            return horizonTicks;
        }
        state.isSimulating = true;
        state.hasGhostDied = false;
        state.hasGhostHitPortal = false;
        state.firstOrbTouchTick = -1;
        state.orbUseTick = -1;
        state.firstOrbRingObject = nullptr;
        state.firstOrbPlayerObject = nullptr;
        state.orbCloseSampleTick = -1;
        state.orbTouchDistanceSquared = 0.f;
        state.orbCloseDistanceSquared = 0.f;
        state.pressOrbRingObject = nullptr;
        state.inputTickIndex = delayTicks;
        state.isInputDown = isDown;

        bool isSecondPlayer = isDual && state.ghostPlayer2 && player2;
        state.hasRealRectangle = state.ignoreTouchingObjects;
        state.harmlessObjects.clear();
        if (state.hasRealRectangle) {
            state.realRectanglePlayer1 = player1->getObjectRect();
            state.realRectanglePlayer2 = isSecondPlayer ? player2->getObjectRect() : state.realRectanglePlayer1;
        }
        copyState(state.ghostPlayer1, player1);
        if (isSecondPlayer) {
            copyState(state.ghostPlayer2, player2);
        }
        state.isHeld1 = state.isHeld2 = state.hasAppliedInput;
        state.isAuto1 = state.isAuto2 = false;
        state.autoReleaseTick1 = state.autoReleaseTick2 = -1;

        bool isWaveRun = player1->m_isDart || (isSecondPlayer && player2->m_isDart);
        if (state.waveVersion46Mode && isWaveRun && delayTicks > 0 && !state.skipPressHandling) {
            pressGhost(state.ghostPlayer1, !isDown);
            if (isSecondPlayer) {
                pressGhost(state.ghostPlayer2, !isDown);
            }
        }

        float deltaTime = state.tickLengthSeconds;
        if (!(deltaTime > 0.001f && deltaTime < 2.0f)) {
            deltaTime = TICK_DURATION;
        }

        bool isFlyingRun = isFlying(player1) || (isDual && player2 && isFlying(player2));
        bool isSpiderRun = state.spiderSmartEnabled && (player1->m_isSpider || (isSecondPlayer && player2->m_isSpider));
        bool isUfoRun = state.ufoSmartEnabled && (player1->m_isBird || (isSecondPlayer && player2->m_isBird));

        bool isBallRun = state.ballSmartEnabled && (player1->m_isBall || (isSecondPlayer && player2->m_isBall));
        int totalTicks = delayTicks + horizonTicks;
        int survivedTicks = 0;
        int airborneTicks = 0;
        int groundedTicks = 0;
        int currentTick = 0;
        bool hasDied = false;
        bool isSafe = false;

        for (; currentTick < totalTicks; ++currentTick) {
            state.simulationTick = currentTick;

            if (state.autoReleaseTick1 >= 0 && currentTick >= state.autoReleaseTick1) {
                if (!state.isHeld1 && state.ghostPlayer1->m_touchingRings->count() > 0) {
                    state.ghostPlayer1->pushButton(PlayerButton::Jump);
                    state.isAuto1 = true;
                }
                state.autoReleaseTick1 = -2;
            }
            if (isSecondPlayer && state.autoReleaseTick2 >= 0 && currentTick >= state.autoReleaseTick2) {
                if (!state.isHeld2 && state.ghostPlayer2->m_touchingRings->count() > 0) {
                    state.ghostPlayer2->pushButton(PlayerButton::Jump);
                    state.isAuto2 = true;
                }
                state.autoReleaseTick2 = -2;
            }
            if (currentTick == delayTicks && !state.skipPressHandling) {
                pressGhost(state.ghostPlayer1, isDown);
                if (isSecondPlayer) {
                    pressGhost(state.ghostPlayer2, isDown);
                }
            }
            if (state.retapTick >= 0) {
                int tick = currentTick - delayTicks - state.retapTick;
                if (tick == 0 || tick == 1 || tick == 5) {
                    pressGhost(state.ghostPlayer1, tick == 1);
                    if (isSecondPlayer) {
                        pressGhost(state.ghostPlayer2, tick == 1);
                    }
                }
            }
            if (isDown && releaseAfterTicks >= 0 && currentTick == delayTicks + releaseAfterTicks && state.ghostPlayer1->m_isDashing) {
                ++releaseAfterTicks;
            }
            if (releaseAfterTicks >= 0 && currentTick == delayTicks + releaseAfterTicks) {
                pressGhost(state.ghostPlayer1, !isDown);
                if (isSecondPlayer) {
                    pressGhost(state.ghostPlayer2, !isDown);
                }
            }
            float stepDeltaTime = currentTick == 0 ? deltaTime * state.firstStepScale : deltaTime;
            advanceGhost(state.ghostPlayer1, stepDeltaTime);
            if (isSecondPlayer) {
                advanceGhost(state.ghostPlayer2, stepDeltaTime);
            }
            releaseAutoClicks();
            if (state.hasGhostDied) {
                hasDied = true;
                break;
            }

            checkGhost(gameLayer, state.ghostPlayer1, deltaTime);
            if (!state.hasGhostDied && isSecondPlayer) {
                checkGhost(gameLayer, state.ghostPlayer2, deltaTime);
            }

            if (state.firstOrbRingObject && state.firstOrbPlayerObject && state.orbCloseSampleTick >= 0) {
                float orbDistanceSquared = orbDistanceSq(state.firstOrbPlayerObject, state.firstOrbRingObject);
                if (orbDistanceSquared < state.orbCloseDistanceSquared) {
                    state.orbCloseDistanceSquared = orbDistanceSquared;
                    state.orbCloseSampleTick = state.simulationTick;
                }
            }
            if (state.isProbing) {
                for (auto& probeOrb : state.probeOrbCandidates) {
                    float probeDistanceSquared = orbDistanceSq(probeOrb.playerObject, probeOrb.ringObject);
                    if (probeDistanceSquared < probeOrb.closeDistanceSquared) {
                        probeOrb.closeDistanceSquared = probeDistanceSquared;
                        probeOrb.closeSampleTick = state.simulationTick;
                    }
                }
            }
            if (state.hasGhostDied) {
                hasDied = true;
                break;
            }
            if (state.hasGhostHitPortal) {
                isSafe = currentTick >= delayTicks;
                break;
            }

            if (currentTick < delayTicks) {
                continue;
            }
            ++survivedTicks;
            if (survivedTicks >= stopAtTick) {
                break;
            }
            if (isFlyingRun && !isUfoRun) {
                continue;
            }

            bool isGrounded = state.ghostPlayer1->m_isOnGround && (!isSecondPlayer || state.ghostPlayer2->m_isOnGround);
            if (isGrounded && airborneTicks > 0) {
                state.hasBaseTouchedDown = true;
            }
            if (!isGrounded) {
                ++airborneTicks;
                groundedTicks = 0;
                continue;
            }

            if (isSpiderRun) {
                if (++groundedTicks >= st(SPIDER_LAND_TICKS)) {
                    isSafe = true;
                    break;
                }
                continue;
            }
            if (airborneTicks < st(isBallRun ? 2 : 6)) {
                continue;
            }
            if (groundedTicks > st(isBallRun ? 8 : 2)) {
                isSafe = true;
                break;
            }
            ++groundedTicks;
        }

        if ((hasDied || state.hasGhostHitPortal) && currentTick < delayTicks) {
            *simulationEnd = SimEnd::StopSearch;
        }
        restoreObjects();
        state.ghostPlayer1->stopAllActions();
        if (state.ghostPlayer2) {
            state.ghostPlayer2->stopAllActions();
        }
        state.isSimulating = false;
        return isSafe ? horizonTicks : survivedTicks;
    }

    int simulateOrbs(PlayLayer* gameLayer, PlayerObject* player1, PlayerObject* player2, bool isDual, int delayTicks, bool isDown, int horizonTicks, int releaseAfterTicks,
        int stopAtTick, SimEnd* simulationEnd) {
        state.orbClickEnabled = state.assumeOrbHits;
        state.hasTouchedOrb = false;
        int isFirst = simulate(gameLayer, player1, player2, isDual, delayTicks, isDown, horizonTicks, releaseAfterTicks, stopAtTick, simulationEnd);
        if (!state.tryBothOrbCandidates || !state.smartEnabled || !state.hasTouchedOrb || isFirst >= stopAtTick) {
            return isFirst;
        }
        SimEnd firstSimulationEnd = *simulationEnd;
        state.orbClickEnabled = !state.assumeOrbHits;
        int isSecondPlayer = simulate(gameLayer, player1, player2, isDual, delayTicks, isDown, horizonTicks, releaseAfterTicks, stopAtTick, simulationEnd);
        state.orbClickEnabled = state.assumeOrbHits;
        if (firstSimulationEnd != SimEnd::StopSearch) {
            *simulationEnd = SimEnd::Ok;
        }
        int bestResult = std::max(isFirst, isSecondPlayer);

        if (state.orbTimingEnabled && bestResult < stopAtTick) {
            for (int lateTick : { 3, 6 }) {
                state.orbClickEnabled = true;
                state.orbClickDelayTicks = st(lateTick);
                SimEnd lateSimulationEnd;
                int candidateResult = simulate(gameLayer, player1, player2, isDual, delayTicks, isDown, horizonTicks, releaseAfterTicks, stopAtTick, &lateSimulationEnd);
                state.orbClickDelayTicks = 0;
                state.orbClickEnabled = state.assumeOrbHits;
                bestResult = std::max(bestResult, candidateResult);
                if (bestResult >= stopAtTick) {
                    break;
                }
            }
        }
        return bestResult;
    }

    double clickMs() {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - clickStartTime).count();
    }

    int fullWaveHorizon(bool isMini) {
        int waveHorizonTicks = std::clamp(state.waveHorizonTicks, 20, 160);
        return st(isMini ? std::max(20, waveHorizonTicks - 15) : waveHorizonTicks);
    }

    float orbSpriteRadius(RingObject* ringObject) {
        if (!ringObject) {
            return 16.f;
        }
        CCSize spriteSize = ringObject->getScaledContentSize();
        float orbRadius = std::max(spriteSize.width, spriteSize.height) * 0.5f;
        return std::max(orbRadius, 12.f);
    }

    int alignOrb(PlayLayer* gameLayer, PlayerObject* player1, PlayerObject* player2, bool isDual, int horizonTicks, int releaseAfterTicks, int learnedDelay, int* landingTick) {
        int searchWindowTicks = st(std::clamp(state.orbWindowTicks, 1, 64));
        int foundTick = 0;
        SimEnd simulationEnd;
        state.orbClickEnabled = false;
        state.wasOrbTiming = false;
        state.intendedOrbObject = nullptr;

        if (learnedDelay > 0 && learnedDelay <= searchWindowTicks) {
            int learnedResult = simulate(gameLayer, player1, player2, isDual, learnedDelay, true, horizonTicks, releaseAfterTicks, horizonTicks, &simulationEnd);
            if (state.pressOrbRingObject && learnedResult >= horizonTicks) {
                *landingTick = learnedResult;
                state.intendedOrbObject = state.pressOrbRingObject;
                state.orbClickEnabled = state.assumeOrbHits;
                return learnedDelay;
            }
        }

        state.probeOrbCandidates.clear();
        state.isProbing = true;
        state.skipPressHandling = true;
        simulate(gameLayer, player1, player2, isDual, 0, true, searchWindowTicks + 2, -1, searchWindowTicks + 2, &simulationEnd);
        state.skipPressHandling = false;
        state.isProbing = false;
        std::string traceText;

        int currentResult = simulate(gameLayer, player1, player2, isDual, 0, true, horizonTicks, releaseAfterTicks, horizonTicks, &simulationEnd);
        RingObject* currentRing = state.pressOrbRingObject;
        bool currentUsesOrb = currentRing != nullptr;
        int maxDelayTicks = searchWindowTicks + st(4);

        constexpr float CLOSE_DISTANCE_FRACTION = 0.60f;
        ProbeOrb* targetProbe = nullptr;
        float targetDistanceSquared = 0.f;
        int targetCloseTick = -1;
        if (!currentUsesOrb) {
            for (auto& candidateProbe : state.probeOrbCandidates) {
                if (!candidateProbe.ringObject || candidateProbe.touchDistanceSquared <= 1e-6f) {
                    continue;
                }
                bool isAlreadyTouching = candidateProbe.touchSampleTick == 0;
                if (!isAlreadyTouching && candidateProbe.closeSampleTick > searchWindowTicks) {
                    continue;
                }
                if (candidateProbe.closeDistanceSquared > candidateProbe.touchDistanceSquared * (CLOSE_DISTANCE_FRACTION * CLOSE_DISTANCE_FRACTION)) {
                    continue;
                }

                if (typeinfo_cast<DashRingObject*>(candidateProbe.ringObject)) {
                    float orbRadius = orbSpriteRadius(candidateProbe.ringObject);
                    if (candidateProbe.closeDistanceSquared > orbRadius * orbRadius * 2.25f) {
                        continue;
                    }
                }
                if (!targetProbe || candidateProbe.closeDistanceSquared < targetDistanceSquared) {
                    targetProbe = &candidateProbe;
                    targetDistanceSquared = candidateProbe.closeDistanceSquared;
                    targetCloseTick = candidateProbe.closeSampleTick;
                }
            }
        }

        if (currentUsesOrb && currentResult < horizonTicks && state.orbTimingEnabled) {
            int closeTick = 0;
            for (auto const& timingProbe : state.probeOrbCandidates) {
                if (timingProbe.ringObject == currentRing) {
                    closeTick = timingProbe.closeSampleTick;
                    break;
                }
            }
            int timingLow = std::max(1, closeTick - 1);
            int timingHigh = std::min(closeTick + st(4), maxDelayTicks);
            int timingBestResult = -1;
            for (int timingDelay = timingLow; timingDelay <= timingHigh; ++timingDelay) {
                int timingResult = simulate(gameLayer, player1, player2, isDual, timingDelay, true, horizonTicks, releaseAfterTicks, horizonTicks, &simulationEnd);
                bool isTimingOrbUsed = state.pressOrbRingObject == currentRing;
                if (state.explainEnabled) {
                    traceText += fmt::format(" +{}:{}{}", timingDelay, timingResult, isTimingOrbUsed ? "o" : "-");
                }
                if (simulationEnd == SimEnd::StopSearch) {
                    break;
                }
                if (!isTimingOrbUsed || timingResult < std::min(horizonTicks, currentResult + st(12)) || timingResult <= timingBestResult) {
                    continue;
                }
                timingBestResult = timingResult;
                foundTick = timingDelay;
                *landingTick = timingResult;
                state.wasOrbTiming = true;
                if (timingResult >= horizonTicks) {
                    break;
                }
            }
            if (foundTick > 0) {
                state.intendedOrbObject = currentRing;
            }
        }
        else if (!currentUsesOrb && targetProbe && targetCloseTick >= 0 && targetCloseTick <= searchWindowTicks) {

            int nearBestResult = -1;
            int nearLow = std::max(1, targetCloseTick);
            int nearHigh = std::min(targetCloseTick + st(4), maxDelayTicks);
            for (int nearDelay = nearLow; nearDelay <= nearHigh; ++nearDelay) {
                int nearResult = simulate(gameLayer, player1, player2, isDual, nearDelay, true, horizonTicks, releaseAfterTicks, horizonTicks, &simulationEnd);
                bool isNearOrbUsed = state.pressOrbRingObject == targetProbe->ringObject;
                if (state.explainEnabled) {
                    traceText += fmt::format(" +{}:{}{}", nearDelay, nearResult, isNearOrbUsed ? "o" : "-");
                }
                if (simulationEnd == SimEnd::StopSearch) {
                    break;
                }
                if (!isNearOrbUsed || nearResult < std::min(currentResult, horizonTicks)) {
                    continue;
                }
                if (nearResult > nearBestResult) {
                    nearBestResult = nearResult;
                    foundTick = nearDelay;
                    *landingTick = nearResult;
                }
                if (nearResult >= horizonTicks) {
                    break;
                }
            }
            if (foundTick > 0) {
                state.intendedOrbObject = targetProbe->ringObject;
            }
        }

        if (currentUsesOrb) {
            state.intendedOrbObject = currentRing;
        }
        else if (targetProbe) {
            state.intendedOrbObject = targetProbe->ringObject;
        }
        if (state.explainEnabled) {
            say("orb: closest {} ({}), press now {} -> {}t |{} => {}", targetProbe ? targetCloseTick : -1,
                targetProbe && targetProbe->touchDistanceSquared > 1e-6f
                    ? std::sqrt(targetProbe->closeDistanceSquared / targetProbe->touchDistanceSquared) * 100.f
                    : 0.f,
                currentUsesOrb ? "uses it" : "misses it", currentResult, traceText.empty() ? " (nothing to try)" : traceText,
                foundTick > 0 ? fmt::format("+{}t", foundTick) : std::string("no pick"));
        }
        state.orbClickEnabled = state.assumeOrbHits;
        return foundTick;
    }

    int waveScore(PlayLayer* gameLayer, PlayerObject* player1, PlayerObject* player2, bool isDual, int delayTicks, bool isDown, int horizonTicks, int targetTick,
        SimEnd* simulationEnd) {
        int candidateResult = simulateOrbs(gameLayer, player1, player2, isDual, delayTicks, isDown, horizonTicks, -1, targetTick, simulationEnd);
        if (candidateResult >= targetTick || *simulationEnd == SimEnd::StopSearch) {
            return candidateResult;
        }
        for (int marginTicks : WAVE_TURN_MARGIN_TICKS) {
            int turnTick = candidateResult - st(marginTicks);
            if (turnTick < st(WAVE_MIN_TURN_TICKS)) {
                break;
            }
            int runLengthTicks = std::min(horizonTicks, turnTick + st(WAVE_ESCAPE_TICKS));
            SimEnd turnEndReason;
            int turnedTicks = simulateOrbs(gameLayer, player1, player2, isDual, delayTicks, isDown, runLengthTicks, turnTick, runLengthTicks, &turnEndReason);
            if (turnedTicks >= runLengthTicks) {
                if (state.captureKillerObject && state.baseEscapeTicks < 0) {
                    state.baseEscapeTicks = turnTick;
                    state.baseCrashTicks = candidateResult;
                }
                return horizonTicks;
            }
        }
        return candidateResult;
    }

    int robotScore(PlayLayer* gameLayer, PlayerObject* player1, PlayerObject* player2, bool isDual, int delayTicks, int horizonTicks, int targetTick, SimEnd* simulationEnd) {
        int usualHoldTicks = robotHoldHistory.pick(st(20), 50);
        int bestResult = simulateOrbs(gameLayer, player1, player2, isDual, delayTicks, true, horizonTicks, usualHoldTicks, targetTick, simulationEnd);
        robotUsedHoldTicks = usualHoldTicks;
        if (bestResult >= targetTick || *simulationEnd == SimEnd::StopSearch || isQuickScore) {
            return bestResult;
        }
        for (int holdTicks : ROBOT_HOLD_TICKS) {
            if (std::abs(st(holdTicks) - usualHoldTicks) < st(3)) {
                continue;
            }
            SimEnd trialSimulationEnd;
            int trialResult = simulateOrbs(gameLayer, player1, player2, isDual, delayTicks, true, horizonTicks, st(holdTicks), targetTick, &trialSimulationEnd);
            if (trialResult > bestResult) {
                bestResult = trialResult;
                robotUsedHoldTicks = st(holdTicks);
            }
            if (bestResult >= targetTick) {
                break;
            }
        }
        return bestResult;
    }

    int spiderScore(PlayLayer* gameLayer, PlayerObject* player1, PlayerObject* player2, bool isDual, int delayTicks, int horizonTicks, int releaseAfterTicks, int targetTick,
        SimEnd* simulationEnd) {
        int baseResult = simulateOrbs(gameLayer, player1, player2, isDual, delayTicks, true, horizonTicks, releaseAfterTicks, targetTick, simulationEnd);
        if (baseResult >= targetTick || *simulationEnd == SimEnd::StopSearch || isQuickScore) {
            return baseResult;
        }
        for (int marginTicks : SPIDER_RETAP_TICKS) {
            int retapTick = baseResult - st(marginTicks);
            if (retapTick < st(SPIDER_MIN_RETAP_TICKS)) {
                break;
            }
            int runLengthTicks = std::min(horizonTicks, retapTick + st(16));
            state.retapTick = retapTick;
            SimEnd retapSimulationEnd;
            int retappedTicks = simulateOrbs(gameLayer, player1, player2, isDual, delayTicks, true, runLengthTicks, releaseAfterTicks, runLengthTicks, &retapSimulationEnd);
            state.retapTick = -1;
            if (retappedTicks >= runLengthTicks) {
                if (state.captureKillerObject && state.baseEscapeTicks < 0) {
                    state.baseEscapeTicks = retapTick;
                    state.baseCrashTicks = baseResult;
                }
                return horizonTicks;
            }
        }
        return baseResult;
    }

    bool sameCrash(int candidateDelay, int candidateResult, int baseDelay, int& sameCrashStreak) {
        if (sameCrashStreak >= 0 && std::abs(candidateDelay + candidateResult - baseDelay) <= 1) {
            ++sameCrashStreak;
        }
        else {
            sameCrashStreak = -1;
        }
        if (sameCrashStreak >= SAME_CRASH_TRY_COUNT) {
            isCrashFixed = true;
            return true;
        }
        return false;
    }

    int searchSmallest(int horizonTicks, int maxDelayTicks, int baseDelay, int* landingTick, std::function<int(int, int, SimEnd*)> const& scoreFunction,
        double budgetMilliseconds = -1.0, bool isQuiet = false, bool needsFullSearch = false) {
        if (budgetMilliseconds < 0.0) {
            budgetMilliseconds = state.budgetMilliseconds;
        }
        using clock = std::chrono::steady_clock;
        auto startTime = clock::now();
        int targetTick = needsFullSearch ? horizonTicks : std::min(horizonTicks, baseDelay + st(12));
        debugBestDelay = 0;
        debugBestResult = -1;
        int sameCrashStreak = 0;
        for (int candidateDelay = 1; candidateDelay <= maxDelayTicks; ++candidateDelay) {
            SimEnd simulationEnd;
            int candidateResult = scoreFunction(candidateDelay, targetTick, &simulationEnd);
            if (candidateResult > debugBestResult) {
                debugBestResult = candidateResult;
                debugBestDelay = candidateDelay;
            }
            if (simulationEnd == SimEnd::StopSearch) {
                return 0;
            }
            if (candidateResult >= targetTick) {
                *landingTick = candidateResult;
                return candidateDelay;
            }
            if (sameCrash(candidateDelay, candidateResult, baseDelay, sameCrashStreak)) {
                return 0;
            }
            if (std::chrono::duration<double, std::milli>(clock::now() - startTime).count() > budgetMilliseconds) {
                if (!isQuiet) {
                    say("prediction budget hit at +{} ticks", candidateDelay);
                }
                return 0;
            }
        }
        return 0;
    }

    int searchTwoPass(int horizonTicks, int maxDelayTicks, int baseDelay, int* landingTick, std::function<int(int, int, SimEnd*)> const& scoreFunction,
        bool needsFullSearch = false) {
        isQuickScore = true;
        int candidateDelay = searchSmallest(horizonTicks, maxDelayTicks, baseDelay, landingTick, scoreFunction, state.budgetMilliseconds * 0.5, true, needsFullSearch);
        isQuickScore = false;
        if (candidateDelay > 0) {
            return candidateDelay;
        }
        isCrashFixed = false;
        double remainingBudget = state.budgetMilliseconds - clickMs();
        if (remainingBudget < 0.5) {
            say("prediction budget hit (quick pass only)");
            return 0;
        }
        return searchSmallest(horizonTicks, maxDelayTicks, baseDelay, landingTick, scoreFunction, remainingBudget, false, needsFullSearch);
    }

    int shipScore(PlayLayer* gameLayer, PlayerObject* player1, PlayerObject* player2, bool isDual, int delayTicks, bool isDown, int horizonTicks, int targetTick,
        SimEnd* simulationEnd) {
        int candidateResult = simulateOrbs(gameLayer, player1, player2, isDual, delayTicks, isDown, horizonTicks, -1, targetTick, simulationEnd);
        if (candidateResult >= targetTick || *simulationEnd == SimEnd::StopSearch) {
            return candidateResult;
        }
        for (int marginTicks : SHIP_RECOVERY_TICKS) {
            int turnTick = candidateResult - st(marginTicks);
            if (turnTick < st(WAVE_MIN_TURN_TICKS)) {
                break;
            }
            int runLengthTicks = std::min(horizonTicks, turnTick + st(WAVE_ESCAPE_TICKS));
            SimEnd turnEndReason;
            int turnedTicks = simulateOrbs(gameLayer, player1, player2, isDual, delayTicks, isDown, runLengthTicks, turnTick, runLengthTicks, &turnEndReason);
            if (turnedTicks >= runLengthTicks) {
                if (state.captureKillerObject && state.baseEscapeTicks < 0) {
                    state.baseEscapeTicks = turnTick;
                    state.baseCrashTicks = candidateResult;
                }
                return horizonTicks;
            }
        }
        return candidateResult;
    }

    int ufoScore(PlayLayer* gameLayer, PlayerObject* player1, PlayerObject* player2, bool isDual, int delayTicks, int horizonTicks, int releaseAfterTicks, int targetTick,
        SimEnd* simulationEnd) {
        int baseResult = simulateOrbs(gameLayer, player1, player2, isDual, delayTicks, true, horizonTicks, releaseAfterTicks, targetTick, simulationEnd);
        if (baseResult >= targetTick || *simulationEnd == SimEnd::StopSearch || isQuickScore) {
            return baseResult;
        }
        for (int marginTicks : UFO_REFLAP_TICKS) {
            int reflapTick = baseResult - st(marginTicks);
            if (reflapTick < st(4)) {
                break;
            }
            int runLengthTicks = std::min(horizonTicks, reflapTick + st(20));
            state.retapTick = reflapTick;
            SimEnd reflapSimulationEnd;
            int reflappedTicks = simulateOrbs(gameLayer, player1, player2, isDual, delayTicks, true, runLengthTicks, releaseAfterTicks, runLengthTicks, &reflapSimulationEnd);
            state.retapTick = -1;
            if (reflappedTicks >= runLengthTicks) {
                if (state.captureKillerObject && state.baseEscapeTicks < 0) {
                    state.baseEscapeTicks = reflapTick;
                    state.baseCrashTicks = baseResult;
                }
                return horizonTicks;
            }
        }
        return baseResult;
    }

    int searchWave(PlayLayer* gameLayer, PlayerObject* player1, PlayerObject* player2, bool isDual, bool isDown, int horizonTicks, int maxDelayTicks, int baseDelay,
        int* landingTick) {
        isMarginUsed = false;
        using clock = std::chrono::steady_clock;
        auto startTime = clock::now();
        int targetTick = std::min(horizonTicks, baseDelay + st(12));
        debugBestDelay = 0;
        debugBestResult = -1;
        for (int candidateDelay = 1; candidateDelay <= maxDelayTicks; ++candidateDelay) {
            SimEnd simulationEnd;
            int candidateResult = waveScore(gameLayer, player1, player2, isDual, candidateDelay, isDown, horizonTicks, targetTick, &simulationEnd);
            if (candidateResult > debugBestResult) {
                debugBestResult = candidateResult;
                debugBestDelay = candidateDelay;
            }
            if (simulationEnd == SimEnd::StopSearch) {
                return 0;
            }
            if (candidateResult >= targetTick) {
                *landingTick = candidateResult;
                if (state.waveMarginEnabled && candidateDelay + 2 <= maxDelayTicks) {
                    SimEnd firstSimulationEnd, secondSimulationEnd;
                    if (waveScore(gameLayer, player1, player2, isDual, candidateDelay + 1, isDown, horizonTicks, targetTick, &firstSimulationEnd) >= targetTick &&
                        waveScore(gameLayer, player1, player2, isDual, candidateDelay + 2, isDown, horizonTicks, targetTick, &secondSimulationEnd) >= targetTick) {
                        isMarginUsed = true;
                        return candidateDelay + 1;
                    }
                }
                return candidateDelay;
            }
            if (std::chrono::duration<double, std::milli>(clock::now() - startTime).count() > state.budgetMilliseconds) {
                say("prediction budget hit at +{} ticks", candidateDelay);
                return 0;
            }
        }
        return 0;
    }

    int searchDelay(PlayLayer* gameLayer, PlayerObject* player1, PlayerObject* player2, bool isDual, bool isDown, bool hasWave, int horizonTicks, int maxDelayTicks,
        int releaseAfterTicks, int baseDelay, int* landingTick, bool needsFullSearch = false) {
        using clock = std::chrono::steady_clock;
        auto startTime = clock::now();
        auto hasExceededBudget = [&] {
            return std::chrono::duration<double, std::milli>(clock::now() - startTime).count() > state.budgetMilliseconds;
        };

        int targetTick = needsFullSearch ? horizonTicks : std::min(horizonTicks, baseDelay + st(12));
        debugBestDelay = 0;
        debugBestResult = -1;
        int sameCrashStreak = 0;
        int waveBestDelay = 0;
        int waveBestResult = baseDelay;
        for (int candidateDelay = 1; candidateDelay <= maxDelayTicks; ++candidateDelay) {
            SimEnd simulationEnd;
            int candidateResult = simulateOrbs(gameLayer, player1, player2, isDual, candidateDelay, isDown, horizonTicks, releaseAfterTicks, hasWave ? horizonTicks : targetTick, &simulationEnd);
            if (candidateResult > debugBestResult) {
                debugBestResult = candidateResult;
                debugBestDelay = candidateDelay;
            }
            if (simulationEnd == SimEnd::StopSearch) {
                break;
            }
            if (hasWave) {

                if (candidateResult >= horizonTicks && candidateResult - baseDelay >= candidateDelay * 2 + 6) {
                    waveBestDelay = candidateDelay;
                    waveBestResult = candidateResult;
                    break;
                }
                if (candidateResult >= baseDelay + candidateDelay * 2 + 10 && candidateResult > waveBestResult + 2) {
                    waveBestDelay = candidateDelay;
                    waveBestResult = candidateResult;
                    if (candidateResult >= horizonTicks) {
                        break;
                    }
                }
            }
            else if (candidateResult < targetTick && sameCrash(candidateDelay, candidateResult, baseDelay, sameCrashStreak)) {
                break;
            }
            else if (candidateResult >= targetTick) {

                SimEnd fullSearchEnd;
                if (candidateResult < horizonTicks && simulateOrbs(gameLayer, player1, player2, isDual, candidateDelay, isDown, horizonTicks, releaseAfterTicks, horizonTicks, &fullSearchEnd) < horizonTicks) {
                    for (int innerIndex = candidateDelay + 1; innerIndex <= maxDelayTicks && !hasExceededBudget(); ++innerIndex) {
                        SimEnd innerSimulationEnd;
                        int innerResult = simulateOrbs(gameLayer, player1, player2, isDual, innerIndex, isDown, horizonTicks, releaseAfterTicks, horizonTicks, &innerSimulationEnd);
                        if (innerSimulationEnd == SimEnd::StopSearch) {
                            break;
                        }
                        if (innerResult >= horizonTicks) {
                            *landingTick = innerResult;
                            return innerIndex;
                        }
                    }
                }
                *landingTick = candidateResult;
                return candidateDelay;
            }
            if (hasExceededBudget()) {
                say("prediction budget hit at +{} ticks", candidateDelay);
                break;
            }
        }
        if (hasWave && waveBestDelay > 0) {
            *landingTick = waveBestResult;
            return waveBestDelay;
        }
        return 0;
    }

    void queueInput(int tickCount, bool isDown) {
        state.queuedInputs.push_back({ tickCount, isDown });
    }

    bool intercept(GJBaseGameLayer* baseGameLayer, bool isDown, bool isFirstPlayer) {
        clickStartTime = std::chrono::steady_clock::now();
        clickBudget();
        auto* playLayer = GameManager::get()->m_playLayer;
        if (!playLayer) {
            return false;
        }

        if (playLayer->m_isPaused) {
            state.hasAppliedInput = isDown;
            return false;
        }

        auto* gameLevel = playLayer->m_level;
        bool isTwoPlayer = gameLevel && gameLevel->m_twoPlayerMode;
        if (isTwoPlayer && !isFirstPlayer && playLayer->m_gameState.m_isDualMode) {
            return false;
        }
        bool isDual = !isTwoPlayer && playLayer->m_gameState.m_isDualMode && playLayer->m_player2;

        state.isFirstPlayer = isFirstPlayer;
        state.hasRealInput = isDown;
        state.intendedOrbObject = nullptr;
        if (!state.isEnabled) {
            state.hasAppliedInput = isDown;
            return false;
        }
        if (playLayer->m_isPlatformer) {
            state.hasAppliedInput = isDown;
            return false;
        }

        auto* player1 = baseGameLayer->m_player1;
        auto* player2 = isDual ? playLayer->m_player2 : nullptr;

        float midTickFraction = 0.f;
        if (state.clickBetweenFramesSync && state.isInRealUpdate && player1 && std::abs(state.tickDeltaX) > 0.01f) {
            midTickFraction = std::clamp((player1->getPosition().x - state.tickStartPositionX) / state.tickDeltaX, 0.f, 0.999f);
        }
        state.firstStepScale = 1.f - midTickFraction;
        struct FirstStepReset {
            ~FirstStepReset() {
                state.firstStepScale = 1.f;
            }
        } firstStepResetState;
        int earlyTicks = midTickFraction > 0.f ? 1 : 0;

        bool hasShip = player1 && (player1->m_isShip || (player2 && player2->m_isShip));
        if (!player1 || player1->m_isDead || (hasShip && !state.shipBufferEnabled)) {
            resetPending();
            state.hasAppliedInput = isDown;
            return false;
        }

        bool isOrbUnderClick = player1->m_touchingRings && player1->m_touchingRings->count() > 0;
        if (isDual && player2 && player2->m_touchingRings && player2->m_touchingRings->count() > 0) {
            isOrbUnderClick = true;
        }
        if (isDown && isOrbUnderClick && state.queuedInputs.empty()) {
            state.ticksSinceInput = 0;
            state.hasAppliedInput = isDown;
            return false;
        }

        if (isDown && state.queuedInputs.empty()) {
            state.releaseShiftTicks = 0;
        }

        bool isPlayer1Wave = player1->m_isDart;
        bool hasWave = isPlayer1Wave || (player2 && player2->m_isDart);
        bool isMini = (isPlayer1Wave && player1->m_vehicleSize < 1.f) || (player2 && player2->m_isDart && player2->m_vehicleSize < 1.f);

        int spamGapTicks = st(hasShip && !hasWave ? std::clamp(state.shipSpamGapTicks, 2, 40) : std::clamp(state.spamGapTicks, 4, 40));
        if (state.ticksSinceInput < spamGapTicks) {
            ++state.rapidInputTicks;
        }
        else {
            state.rapidInputTicks = 0;
        }
        state.ticksSinceInput = 0;
        bool hasUfo = player1->m_isBird || (player2 && player2->m_isBird);
        bool hasBall = player1->m_isBall || (player2 && player2->m_isBall);
        bool hasSwing = player1->m_isSwing || (player2 && player2->m_isSwing);
        if ((hasWave || hasShip || (hasUfo && state.ufoSmartEnabled) || (hasBall && state.ballSmartEnabled) || (hasSwing && state.swingSmartEnabled)) &&
            state.rapidInputTicks >= SPAM_STREAK_COUNT) {
            if (!state.queuedInputs.empty()) {
                for (size_t index = 0; index < state.queuedInputs.size(); ++index) {
                    state.queuedInputs[index].ticksRemaining = static_cast<int>(index);
                }
                if (state.queuedInputs.back().isDown != isDown) {
                    queueInput(static_cast<int>(state.queuedInputs.size()), isDown);
                }
                state.inputShiftTicks = 0;
                return true;
            }
            state.hasAppliedInput = isDown;
            return false;
        }

        if (!state.queuedInputs.empty()) {
            int afterTicks = state.queuedInputs.back().ticksRemaining;
            queueInput(std::max(state.inputShiftTicks - earlyTicks, afterTicks), isDown);
            return true;
        }

        if (!isPlayer1Wave) {
            bool hasRobot = player1->m_isRobot;
            bool hasSecondWave = false;
            if (player2 && !hasRobot) {
                hasSecondWave = player2->m_isDart;
                if (!hasSecondWave) {
                    hasRobot = player2->m_isRobot;
                }
            }
            bool isDashing = player1->m_isDashing || (player2 && player2->m_isDashing);
            if (!hasSecondWave && !isDown && !hasRobot && !isDashing && !hasShip) {
                if (state.releaseShiftTicks > 0) {
                    state.inputShiftTicks = state.releaseShiftTicks;
                    state.releaseShiftTicks = 0;
                    queueInput(state.inputShiftTicks, false);
                    return true;
                }
                state.hasAppliedInput = false;
                return false;
            }
        }

        bool hasFlyer = isFlying(player1) || (player2 && isFlying(player2));
        int horizonTicks = st(hasFlyer ? 48 : std::clamp(state.groundHorizonTicks, 48, 240));
        if (hasWave) {
            horizonTicks = fullWaveHorizon(isMini);
        }
        int maxDelayTicks = st(hasWave ? state.ticksWaveMode : (hasShip ? std::clamp(state.shipMaxDelayTicks, 0, 16) : state.ticksAllModes));
        if (maxDelayTicks <= 0) {
            state.hasAppliedInput = isDown;
            return false;
        }

        bool hasAnyRobot = player1->m_isRobot || (player2 && player2->m_isRobot);
        int releaseAfterTicks = -1;
        if (isDown && !hasWave && !hasShip) {
            releaseAfterTicks = hasAnyRobot ? robotHoldHistory.pick(-1, 50) : holdEstimate();
        }

        bool isGrounded = player1->m_isOnGround && (!player2 || player2->m_isOnGround);
        if (isDown && !hasWave && !hasShip && state.orbAlignEnabled && state.smartEnabled && (!isGrounded || state.orbAlignToGround)) {
            int orbLandingTick = 0;
            int candidateDelay = alignOrb(playLayer, player1, player2, isDual, horizonTicks, releaseAfterTicks, 0, &orbLandingTick);
            if (candidateDelay > 0) {
                say("orb {} +{}t ({:g}f) -> {}t", state.wasOrbTiming ? "timing" : "press", candidateDelay, candidateDelay * TICK_DURATION, orbLandingTick);
                state.inputShiftTicks = candidateDelay;
                queueInput(candidateDelay - earlyTicks, isDown);
                return true;
            }
        }

        SimEnd simulationEnd;
        bool isCubeStyleWave = hasWave;
        bool isRobotPress = isDown && hasAnyRobot && !hasWave && state.robotSmartEnabled;
        bool hasSpider = player1->m_isSpider || (player2 && player2->m_isSpider);
        bool isSpiderPress = isDown && hasSpider && !hasWave && !isRobotPress && state.spiderSmartEnabled;
        bool isUfoPress = isDown && hasUfo && !hasWave && !hasShip && state.ufoSmartEnabled;
        bool isSwingPress = isDown && hasSwing && !hasWave && !hasShip && !isUfoPress && state.swingSmartEnabled;
        isUfoPress = isUfoPress || isSwingPress;
        bool isShipInput = hasShip && !hasWave;

        state.hasBaseTouchedDown = false;
        int baseDelay = isCubeStyleWave ? waveScore(playLayer, player1, player2, isDual, 0, isDown, horizonTicks, horizonTicks, &simulationEnd)
                 : isRobotPress    ? robotScore(playLayer, player1, player2, isDual, 0, horizonTicks, horizonTicks, &simulationEnd)
                 : isSpiderPress   ? spiderScore(playLayer, player1, player2, isDual, 0, horizonTicks, releaseAfterTicks, horizonTicks, &simulationEnd)
                 : isUfoPress      ? ufoScore(playLayer, player1, player2, isDual, 0, horizonTicks, releaseAfterTicks, horizonTicks, &simulationEnd)
                 : isShipInput     ? shipScore(playLayer, player1, player2, isDual, 0, isDown, horizonTicks, horizonTicks, &simulationEnd)
                                 : simulateOrbs(playLayer, player1, player2, isDual, 0, isDown, horizonTicks, releaseAfterTicks, horizonTicks, &simulationEnd);

        std::string tickTagText = midTickFraction > 0.f ? fmt::format(" @{:.2f}", midTickFraction) : std::string();
        if (baseDelay >= horizonTicks) {
            state.hasAppliedInput = isDown;
            return false;
        }

        bool isGroundPress = isDown && !hasFlyer && !hasAnyRobot && !hasSpider && !hasWave;
        if (isGroundPress && state.lateCrashTicks > 0 && baseDelay >= st(state.lateCrashTicks) && state.hasBaseTouchedDown) {
            state.hasAppliedInput = isDown;
            return false;
        }

        int landingTick = 0;
        int savedTick = 0;
        isCrashFixed = false;
        int neededTicks = (isGroundPress || isRobotPress || isUfoPress) ? horizonTicks : std::min(horizonTicks, baseDelay + st(12));

        savedTick = isCubeStyleWave ? searchWave(playLayer, player1, player2, isDual, isDown, horizonTicks, maxDelayTicks, baseDelay, &landingTick)
              : isRobotPress    ? searchTwoPass(horizonTicks, maxDelayTicks, baseDelay, &landingTick, [&](int candidateDelay, int targetTick, SimEnd* simulationEnd) {
                    return robotScore(playLayer, player1, player2, isDual, candidateDelay, horizonTicks, targetTick, simulationEnd);
                }, true)
              : isSpiderPress   ? searchTwoPass(horizonTicks, maxDelayTicks, baseDelay, &landingTick, [&](int candidateDelay, int targetTick, SimEnd* simulationEnd) {
                    return spiderScore(playLayer, player1, player2, isDual, candidateDelay, horizonTicks, releaseAfterTicks, targetTick, simulationEnd);
                })
              : isShipInput     ? searchSmallest(horizonTicks, maxDelayTicks, baseDelay, &landingTick, [&](int candidateDelay, int targetTick, SimEnd* simulationEnd) {
                    return shipScore(playLayer, player1, player2, isDual, candidateDelay, isDown, horizonTicks, targetTick, simulationEnd);
                })
              : isUfoPress      ? searchTwoPass(horizonTicks, maxDelayTicks, baseDelay, &landingTick, [&](int candidateDelay, int targetTick, SimEnd* simulationEnd) {
                    return ufoScore(playLayer, player1, player2, isDual, candidateDelay, horizonTicks, releaseAfterTicks, targetTick, simulationEnd);
                }, true)
                              : searchDelay(playLayer, player1, player2, isDual, isDown, hasWave, horizonTicks, maxDelayTicks, releaseAfterTicks, baseDelay, &landingTick,
                                    isGroundPress);

        if (savedTick <= 0) {
            say("no save: {}{} dies in {}t (max {}t, best +{}t -> {}t of {})", isDown ? "press" : "release", tickTagText, baseDelay, maxDelayTicks,
                debugBestDelay, debugBestResult, horizonTicks);
            state.hasAppliedInput = isDown;
            return false;
        }

        if (isDown && !state.skipPressHandling) {
            SimEnd verificationEnd;
            int verificationHorizonTicks = st(8);
            state.retapTick = -1;
            state.orbClickEnabled = false;
            simulate(playLayer, player1, player2, isDual, savedTick, isDown, verificationHorizonTicks, -1, verificationHorizonTicks, &verificationEnd);
            if (state.pressOrbRingObject != state.intendedOrbObject) {
                if (state.explainEnabled) {
                    say("orb save +{}t rejected: press would use {}, not the clicked orb", savedTick,
                        state.pressOrbRingObject ? "a different ring" : "no ring");
                }
                state.hasAppliedInput = isDown;
                return false;
            }
        }

        if (isRobotPress) {
            releaseAfterTicks = robotUsedHoldTicks;
        }
        say("saved {}{} +{}t ({:g}f)  {}->{}  [max {}]{}", isDown ? "press" : "release", tickTagText, savedTick, savedTick * TICK_DURATION, baseDelay, landingTick, maxDelayTicks,
            isCubeStyleWave && isMarginUsed ? std::string("  +margin")
            : isRobotPress ? fmt::format("  hold {}t", robotUsedHoldTicks) : std::string());
        state.inputShiftTicks = savedTick;
        queueInput(savedTick - earlyTicks, isDown);
        return true;
    }

    void cycleStage(int stageDirection = 1) {
        state.stageIndex = (state.stageIndex + stageDirection + STAGE_TABLE_SIZE) % STAGE_TABLE_SIZE;
          Mod::get()->setSavedValue("stage", state.stageIndex);
        resetPending();
        auto* gameLayer = GameManager::get()->m_playLayer;
        if (gameLayer && !state.wasAutoReleased && state.hasAppliedInput != state.hasRealInput) {
            sendButton(gameLayer, state.hasRealInput);
        }
        applyStage();
        say("{} cycle -> {}", stageKey(), stageBanner());
    }

    void startCycleWatcher() {
        std::error_code errorCode;
        std::filesystem::remove(cycleTriggerPath(), errorCode);
        try {
            std::thread([] {
                while (true) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    auto triggerPath = cycleTriggerPath();
                    std::error_code errorCode;
                    if (!std::filesystem::exists(triggerPath, errorCode) || errorCode) {
                        continue;
                    }
                    std::filesystem::remove(triggerPath, errorCode);
                    geode::queueInMainThread([] {
                        if (GameManager::get()->m_playLayer) {
                            cycleStage();
                        }
                    });
                }
            }).detach();
        }
        catch (...) {
            log::warn("could not start cycle watcher");
        }
    }

    void deliverPending(PlayLayer* gameLayer, PlayerObject* playerObject) {
        if (gameLayer->m_isPaused) {
            return;
        }
        if (state.ticksSinceInput <= 998) {
            ++state.ticksSinceInput;
        }
        bool isTwoPlayer = gameLayer->m_level && gameLayer->m_level->m_twoPlayerMode;
        bool isDual = !isTwoPlayer && gameLayer->m_gameState.m_isDualMode && gameLayer->m_player2;
        if (!state.shipBufferEnabled && (playerObject->m_isShip || (isDual && gameLayer->m_player2->m_isShip))) {
            resetPending();
            if (state.hasAppliedInput != state.hasRealInput) {
                sendButton(gameLayer, state.hasRealInput);
            }
            return;
        }

        if (state.queuedInputs.empty()) {
            if (state.hasAppliedInput != state.hasRealInput) {
                sendButton(gameLayer, state.hasRealInput);
            }
            sweepGhostLayer();
            return;
        }

        while (!state.queuedInputs.empty() && state.queuedInputs.front().ticksRemaining <= 0) {
            bool isDown = state.queuedInputs.front().isDown;
            state.queuedInputs.pop_front();
            state.releaseShiftTicks = isDown ? state.inputShiftTicks : 0;
            if (isDown != state.hasAppliedInput) {
                sendButton(gameLayer, isDown);
            }
        }
        for (auto& queuedInput : state.queuedInputs) {
            --queuedInput.ticksRemaining;
        }
        if (state.queuedInputs.empty()) {
            state.inputShiftTicks = 0;
        }
    }
}

$on_mod(Loaded) {

    constexpr int STAGE_SCHEMA_VERSION = 2;
    int savedStageIndex = std::clamp(Mod::get()->getSavedValue<int>("stage", 0), 0, STAGE_TABLE_SIZE - 1);
    if (Mod::get()->getSavedValue<int>("stage-schema", 1) < STAGE_SCHEMA_VERSION) {
        if (savedStageIndex == 6) {
            savedStageIndex = STAGE_TABLE_SIZE - 1;
        }
        Mod::get()->setSavedValue("stage", savedStageIndex);
        Mod::get()->setSavedValue("stage-schema", STAGE_SCHEMA_VERSION);
    }
    state.stageIndex = savedStageIndex;
    readSettings();
    applyStage();
    removeLegacyTerminalLogs();
    resetTerminalLog();
    startCycleWatcher();
    setupKeybinds();
    listenForSettingChanges<bool>("terminal-enabled", [](bool enabled) {
        writeTerminalFlags();
        if (!enabled) {
            closeTerminal();
        }
    });
    say("starting mode: {}", stageBanner());
    say("{} in-level cycles: 2/2 -> 4/4 -> 8/8 -> 16/8 -> 24/12 -> 32/16 -> 40/20 -> off", stageKey());
    say("{} toggles the terminal", terminalKey());
    say("host os: {} ({})", hostOSName(), hostOSDescription());
    checkForUpdates(true);
}

class $modify(BufPlayLayer, PlayLayer) {
    static void onModify(auto& self) {
        (void)self.setHookPriority("PlayLayer::destroyPlayer", MOD_HOOK_PRIORITY);
        (void)self.setHookPriority("PlayLayer::playEndAnimationToPos", MOD_HOOK_PRIORITY);
        (void)self.setHookPriority("PlayLayer::pauseGame", MOD_HOOK_PRIORITY);
        (void)self.setHookPriority("PlayLayer::resume", MOD_HOOK_PRIORITY);
    }

    void destroyPlayer(PlayerObject* playerObject, GameObject* gameObject) {

        if (state.isSimulating || isGhost(playerObject)) {
            if (state.isSimulating) {
                state.hasGhostDied = true;
            }
            return;
        }
        bool wasAlreadyDead = playerObject && playerObject->m_isDead;
        PlayLayer::destroyPlayer(playerObject, gameObject);
        if (!playerObject || wasAlreadyDead || !playerObject->m_isDead) {
            return;
        }
        resetPending();
        state.hasAppliedInput = false;
        state.rapidInputTicks = 0;
        forgetPress();
    }

    bool init(GJGameLevel* gameLevel, bool useReplayData, bool skipObjectCreation) {
        if (!PlayLayer::init(gameLevel, useReplayData, skipObjectCreation)) {
            return false;
        }
        dropGhosts();
        readSettings();
        applyStage();
        state.ghostPlayer1 = makeGhost(this);
        state.ghostPlayer2 = makeGhost(this);
        state.ghostLayerBaseIndex = state.ghostRenderLayer->getChildrenCount();
        say("buffer ready | {} | {} cycles", stageText(), stageKey());
        checkForUpdates(false);
        state.hasRealInput = false;
        state.hasAppliedInput = false;
        state.isFirstPlayer = true;
        state.ticksSinceInput = 999;
        state.rapidInputTicks = 0;
        state.wasAutoReleased = false;
        forgetPress();
        resetPending();
#ifdef GEODE_IS_ANDROID
        if (!this->getChildByID("afterflow-overlay"_spr)) {
            if (auto* overlay = AfterflowOverlay::create()) {
                this->addChild(overlay, 1000);
                log::info("[afterflow] overlay attached to PlayLayer");
            }
            else {
                log::warn("[afterflow] overlay creation failed");
            }
        }
#endif
        return true;
    }

    void onQuit() {
        resetPending();
        dropGhosts();
        PlayLayer::onQuit();
    }

    void resetLevel() {
        resetPending();
        forgetPress();
        state.hasAppliedInput = false;
        state.wasAutoReleased = false;
        PlayLayer::resetLevel();
    }

    void pauseGame(bool isUnfocused) {
        resetPending();
        state.rapidInputTicks = 0;
        state.ticksSinceInput = 999;
        state.wasAutoReleased = false;
        forgetPress();
        sweepGhostLayer();
        PlayLayer::pauseGame(isUnfocused);
    }

    void resume() {
        resetPending();
        state.rapidInputTicks = 0;
        state.ticksSinceInput = 999;
        state.wasAutoReleased = false;
        forgetPress();
        sweepGhostLayer();
        PlayLayer::resume();
    }

    void playEndAnimationToPos(CCPoint targetPosition) {
        if (state.isSimulating) {
            return;
        }
        PlayLayer::playEndAnimationToPos(targetPosition);
    }
};

class $modify(BufMenuLayer, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) {
            return false;
        }

        this->runAction(CCSequence::create(
            CCDelayTime::create(0.2f),
            CallFuncExt::create([] {
                if (!showUpdatePopup()) {
                    showKeybindIntro();
                }
            }),
            nullptr
        ));
        return true;
    }
};

class $modify(BufPauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();
#ifdef GEODE_IS_ANDROID
        auto menu = CCMenu::create();
        menu->setPosition({ 0.f, 0.f });
        menu->setID("afterflow-mobile-menu"_spr);

        auto stageButton = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("stage", "goldFont.fnt", "GJ_button_05.png", .7f),
            this,
            menu_selector(BufPauseLayer::onCycleStage)
        );
        stageButton->setPosition({ 40.f, 40.f });
        menu->addChild(stageButton);

        auto infoButton = CCMenuItemSpriteExtra::create(
            ButtonSprite::create("info", "goldFont.fnt", "GJ_button_05.png", .7f),
            this,
            menu_selector(BufPauseLayer::onShowStage)
        );
        infoButton->setPosition({ 40.f, 95.f });
        menu->addChild(infoButton);

        this->addChild(menu, 100);
#endif
    }

#ifdef GEODE_IS_ANDROID
    void onCycleStage(CCObject*) {
        cycleStage(1);
        Notification::create(stageText(), NotificationIcon::Info, 1.5f)->show();
    }

    void onShowStage(CCObject*) {
        Notification::create(stageText(), NotificationIcon::Info, 2.f)->show();
    }
#endif
};

class $modify(BufEditorLayer, LevelEditorLayer) {
    bool init(GJGameLevel* gameLevel, bool withoutUI) {
        bool isSuccess = LevelEditorLayer::init(gameLevel, withoutUI);
        dropGhosts();
        return isSuccess;
    }
};

class $modify(BufHardStreak, HardStreak) {
    static void onModify(auto& self) {
        (void)self.setHookPriority("HardStreak::addPoint", MOD_HOOK_PRIORITY);
    }

    void addPoint(CCPoint touchPoint) {
        if (state.isSimulating) {
            return;
        }
        HardStreak::addPoint(touchPoint);
    }
};

class $modify(BufEffectGameObject, EffectGameObject) {
    static void onModify(auto& self) {
        (void)self.setHookPriority("EffectGameObject::triggerObject", MOD_HOOK_PRIORITY);
    }

    void triggerObject(GJBaseGameLayer* gameLayer, int uniqueId, gd::vector<int> const* remapKeyVector) {
        if (state.isSimulating) {
            return;
        }
        EffectGameObject::triggerObject(gameLayer, uniqueId, remapKeyVector);
    }
};

class $modify(BufGameObject, GameObject) {
    static void onModify(auto& self) {
        (void)self.setHookPriority("GameObject::playShineEffect", MOD_HOOK_PRIORITY);
    }

    void playShineEffect() {
        if (state.isSimulating) {
            return;
        }
        GameObject::playShineEffect();
    }
};

class $modify(BufRingObject, RingObject) {
    static void onModify(auto& self) {
        (void)self.setHookPriority("RingObject::spawnCircle", MOD_HOOK_PRIORITY);
    }

    void spawnCircle() {
        if (state.isSimulating) {
            return;
        }
        RingObject::spawnCircle();
    }
};

class $modify(BufPlayerObject, PlayerObject) {
    static void onModify(auto& self) {
        (void)self.setHookPriority("PlayerObject::playSpiderDashEffect", MOD_HOOK_PRIORITY);

        (void)self.setHookPriority("PlayerObject::update", MOD_HOOK_PRIORITY);
        (void)self.setHookPriority("PlayerObject::incrementJumps", MOD_HOOK_PRIORITY);
        (void)self.setHookPriority("PlayerObject::ringJump", MOD_HOOK_PRIORITY);
        (void)self.setHookPriority("PlayerObject::playDeathEffect", MOD_HOOK_PRIORITY);
        (void)self.setHookPriority("PlayerObject::playerDestroyed", MOD_HOOK_PRIORITY);
    }

    void update(float deltaTime) {
        if (state.isSimulating) {
            PlayerObject::update(deltaTime);
            return;
        }
        auto* gameLayer = GameManager::get()->m_playLayer;
        if (gameLayer && this == gameLayer->m_player1 && !gameLayer->m_isPaused) {
            ++currentTick;
            if (state.isEnabled) {
                deliverPending(gameLayer, this);
            }
        }

        bool isFirstPlayer = gameLayer && this == gameLayer->m_player1;
        if (isFirstPlayer) {

            state.tickStartPositionX = getPosition().x;
            state.isInRealUpdate = true;
        }
        PlayerObject::update(deltaTime);
        if (isFirstPlayer) {
            state.isInRealUpdate = false;
            float deltaX = getPosition().x - state.tickStartPositionX;
            if (std::abs(deltaX) > 0.01f) {
                state.tickDeltaX = deltaX;
            }
            if (deltaTime > 0.f) {
                float average = state.tickLengthSeconds;
                state.tickLengthSeconds = (average > 0.f && std::abs(deltaTime - average) < average * 0.3f) ? average * 0.9f + deltaTime * 0.1f : deltaTime;
            }
        }
    }

    void incrementJumps() {
        if (state.isSimulating) {
            return;
        }
        PlayerObject::incrementJumps();
    }

    bool ghostEffect() {
        return state.isSimulating && isGhost(this);
    }

    void spawnCircle() {
        if (!ghostEffect()) PlayerObject::spawnCircle();
    }

    void spawnDualCircle() {
        if (!ghostEffect()) PlayerObject::spawnDualCircle();
    }

    void spawnPortalCircle(ccColor3B portalColor, float initialRadius) {
        if (!ghostEffect()) PlayerObject::spawnPortalCircle(portalColor, initialRadius);
    }

    void spawnScaleCircle() {
        if (!ghostEffect()) PlayerObject::spawnScaleCircle();
    }

    void playBumpEffect(int gameObjectType, GameObject* gameObject) {
        if (!ghostEffect()) PlayerObject::playBumpEffect(gameObjectType, gameObject);
    }

    void playSpawnEffect() {
        if (!ghostEffect()) PlayerObject::playSpawnEffect();
    }

    void createFadeOutDartStreak() {
        if (!ghostEffect()) PlayerObject::createFadeOutDartStreak();
    }

    void fadeOutStreak2(float fadeDuration) {
        if (!ghostEffect()) PlayerObject::fadeOutStreak2(fadeDuration);
    }

    void playDynamicSpiderRun() {
        if (!ghostEffect()) PlayerObject::playDynamicSpiderRun();
    }

    void animatePlatformerJump(float jumpScale) {
        if (!ghostEffect()) PlayerObject::animatePlatformerJump(jumpScale);
    }

    void runBallRotation(float speedValue) {
        if (!ghostEffect()) PlayerObject::runBallRotation(speedValue);
    }

    void runBallRotation2() {
        if (!ghostEffect()) PlayerObject::runBallRotation2();
    }

    void runNormalRotation(bool isNotNormalMode, float speedValue) {
        if (!ghostEffect()) PlayerObject::runNormalRotation(isNotNormalMode, speedValue);
    }

    void toggleGhostEffect(GhostType ghostType) {
        if (!ghostEffect()) PlayerObject::toggleGhostEffect(ghostType);
    }

    void activateStreak() {
        if (!ghostEffect()) PlayerObject::activateStreak();
    }

    void playSpiderDashEffect(CCPoint fromPoint, CCPoint toPoint) {
        if (state.isSimulating) {
            return;
        }
        PlayerObject::playSpiderDashEffect(fromPoint, toPoint);
    }

    void playDeathEffect() {
        if (state.isSimulating || isGhost(this)) {
            return;
        }
        PlayerObject::playDeathEffect();
    }

    void playerDestroyed(bool suppressEffects) {
        if (state.isSimulating || isGhost(this)) {
            if (state.isSimulating) {
                state.hasGhostDied = true;
            }
            return;
        }
        PlayerObject::playerDestroyed(suppressEffects);
    }

    void playCompleteEffect(bool suppressEffects, bool isInstant) {
        if (!ghostEffect()) PlayerObject::playCompleteEffect(suppressEffects, isInstant);
    }

    void ringJump(RingObject* ringObject, bool skipCollisionCheck) {
        if (state.isSimulating) {
            if (!state.smartEnabled || !isGhost(this)) {
                return;
            }
            if (state.usedOrbs.contains(ringObject)) {
                return;
            }
            snapshotObject(ringObject);

            double verticalVelocity = m_yVelocity;
            bool isUpsideDown = m_isUpsideDown;
            bool isDashing = m_isDashing;
            PlayerObject::ringJump(ringObject, skipCollisionCheck);
            if (m_yVelocity != verticalVelocity || m_isUpsideDown != isUpsideDown || m_isDashing != isDashing) {
                state.usedOrbs.insert(ringObject);
                if (state.orbUseTick < 0) {
                    state.orbUseTick = state.simulationTick;
                }

                if (state.isInputDown && state.simulationTick >= state.inputTickIndex && !state.pressOrbRingObject) {
                    state.pressOrbRingObject = ringObject;
                }
            }
            return;
        }
        PlayerObject::ringJump(ringObject, skipCollisionCheck);
    }

    void startDashing(DashRingObject* dashRing) {
        if (state.isSimulating && isGhost(this)) {
            state.usedOrbs.insert(dashRing);
            if (state.orbUseTick < 0) {
                state.orbUseTick = state.simulationTick;
            }
            if (state.isInputDown && state.simulationTick >= state.inputTickIndex && !state.pressOrbRingObject) {
                state.pressOrbRingObject = dashRing;
            }
        }
        PlayerObject::startDashing(dashRing);
    }
};

class $modify(BufBaseGameLayer, GJBaseGameLayer) {
    static void onModify(auto& self) {
        (void)self.setHookPriority("GJBaseGameLayer::canBeActivatedByPlayer", MOD_HOOK_PRIORITY);
        (void)self.setHookPriority("GJBaseGameLayer::handleButton", MOD_HOOK_PRIORITY);
        (void)self.setHookPriority("GJBaseGameLayer::collisionCheckObjects", MOD_HOOK_PRIORITY);
        (void)self.setHookPriority("GJBaseGameLayer::playerTouchedRing", MOD_HOOK_PRIORITY);
        (void)self.setHookPriority("GJBaseGameLayer::flipGravity", MOD_HOOK_PRIORITY);
        (void)self.setHookPriority("GJBaseGameLayer::gameEventTriggered", MOD_HOOK_PRIORITY);
        (void)self.setHookPriority("GJBaseGameLayer::playerWillSwitchMode", MOD_HOOK_PRIORITY);
    }

    bool canBeActivatedByPlayer(PlayerObject* playerObject, EffectGameObject* effectObject) {
        if (state.isSimulating) {
            if (!state.smartEnabled || !isGhost(playerObject) || !effectObject) {
                return false;
            }

            if (typeinfo_cast<RingObject*>(effectObject)) {
                if (state.usedOrbs.contains(effectObject)) {
                    return false;
                }
                snapshotObject(effectObject);
                return true;
            }
            if (state.simulationUsedObjects.contains(effectObject)) {
                return false;
            }
            snapshotObject(effectObject);
            return true;
        }
        return GJBaseGameLayer::canBeActivatedByPlayer(playerObject, effectObject);
    }

    void collisionCheckObjects(PlayerObject* playerObject, gd::vector<GameObject*>* gameObjects, int gameObjectCount, float deltaTime) {
        if (!state.isSimulating) {
            GJBaseGameLayer::collisionCheckObjects(playerObject, gameObjects, gameObjectCount, deltaTime);
            return;
        }
        static gd::vector<GameObject*> relevantObjects;
        relevantObjects.clear();
        if (static_cast<int>(relevantObjects.capacity()) < gameObjectCount) {
            relevantObjects.reserve(gameObjectCount);
        }

        uint64_t keptTypesMask = state.smartEnabled ? (COLLIDING_OBJECT_TYPES | INTERACTABLE_OBJECT_TYPES) : COLLIDING_OBJECT_TYPES;
        bool hasRectangle = false;
        CCRect ghostRectangle;
        auto& objectList = *gameObjects;
        for (int index = 0; index < gameObjectCount; ++index) {
            auto* gameObject = objectList[index];
            if (!gameObject) {
                continue;
            }
            auto objectType = static_cast<unsigned>(gameObject->m_objectType);
            uint64_t typeBitMask = objectType < 64 ? (1ull << objectType) : 0;
            if (keptTypesMask & typeBitMask) {

                if (!state.simulationDisabledObjects.empty() && state.simulationDisabledObjects.contains(gameObject)) {
                    continue;
                }
                if (state.hasRealRectangle && (typeBitMask & HAZARD_OBJECT_TYPES)) {
                    if (!state.harmlessObjects.empty() && state.harmlessObjects.contains(gameObject)) {
                        continue;
                    }
                    if (state.simulationTick <= 1) {
                        auto const& rectangle = gameObject->getObjectRect();
                        if (rectangle.intersectsRect(state.realRectanglePlayer1) || rectangle.intersectsRect(state.realRectanglePlayer2)) {
                            state.harmlessObjects.insert(gameObject);
                            continue;
                        }
                    }
                }
                relevantObjects.push_back(gameObject);
                continue;
            }
            if (!state.smartEnabled) {
                continue;
            }
            bool isStopPortal = (MODE_PORTAL_OBJECT_TYPES & typeBitMask) != 0;
            bool isSizePortal = (SIZE_PORTAL_OBJECT_TYPES & typeBitMask) != 0;
            float speedValue = (isStopPortal || isSizePortal) ? 0.f : speedPortalValue(gameObject->m_objectID);
            if (!isStopPortal && !isSizePortal && speedValue <= 0.f) {
                continue;
            }
            if (!hasRectangle) {
                ghostRectangle = playerObject->getObjectRect();
                hasRectangle = true;
            }
            if (ghostRectangle.intersectsRect(gameObject->getObjectRect())) {
                if (isStopPortal) {
                    state.hasGhostHitPortal = true;
                }
                else if (isSizePortal) {
                    bool isMini = gameObject->m_objectType == GameObjectType::MiniSizePortal;
                    if ((playerObject->m_vehicleSize < 1.f) != isMini) {
                        playerObject->togglePlayerScale(isMini, true);
                    }
                }
                else {
                    playerObject->m_playerSpeed = speedValue;
                }
            }
        }
        GJBaseGameLayer::collisionCheckObjects(playerObject, &relevantObjects, static_cast<int>(relevantObjects.size()), deltaTime);
    }

    void handleButton(bool isDown, int buttonId, bool isFirstPlayer) {
        static int debugCounter = 0;
        if (debugCounter < 40) {
            ++debugCounter;
            log::info("[afterflow] handleButton down={} id={} p1={} enabled={} stage={}", isDown, buttonId, isFirstPlayer, state.isEnabled, state.stageIndex);
        }
        if (!state.isSimulating && buttonId == 1 && !state.isInjecting) {
            auto* player1 = this->m_player1;
            recordRealInput(isDown, player1);
            bool isHeld = intercept(this, isDown, isFirstPlayer);
            sweepGhostLayer();
            if (isHeld) {
                return;
            }
        }
        GJBaseGameLayer::handleButton(isDown, buttonId, isFirstPlayer);
    }

    void playerTouchedRing(PlayerObject* playerObject, RingObject* ringObject) {
        if (state.isSimulating) {
            if (!state.smartEnabled || !isGhost(playerObject)) {
                return;
            }
            snapshotObject(ringObject);
            state.hasTouchedOrb = true;
            if (state.firstOrbTouchTick < 0) {
                state.firstOrbTouchTick = state.simulationTick;
                state.firstOrbRingObject = ringObject;
                state.firstOrbPlayerObject = playerObject;
                state.orbTouchDistanceSquared = orbDistanceSq(playerObject, ringObject);
                state.orbCloseSampleTick = state.simulationTick;
                state.orbCloseDistanceSquared = state.orbTouchDistanceSquared;
            }

            if (state.isProbing) {
                bool isKnown = false;
                for (auto const& existingProbe : state.probeOrbCandidates) {
                    if (existingProbe.ringObject == ringObject) {
                        isKnown = true;
                        break;
                    }
                }
                if (!isKnown) {
                    float touchDistanceSquared = orbDistanceSq(playerObject, ringObject);
                    ProbeOrb newProbe;
                    newProbe.ringObject = ringObject;
                    newProbe.playerObject = playerObject;
                    newProbe.touchDistanceSquared = touchDistanceSquared;
                    newProbe.closeDistanceSquared = touchDistanceSquared;
                    newProbe.touchSampleTick = state.simulationTick;
                    newProbe.closeSampleTick = state.simulationTick;
                    state.probeOrbCandidates.push_back(newProbe);
                }
            }
        }
        GJBaseGameLayer::playerTouchedRing(playerObject, ringObject);

        if (state.isSimulating && state.orbClickEnabled) {
            if (typeinfo_cast<DashRingObject*>(ringObject)) {
                return;
            }
            bool isFirst = playerObject == state.ghostPlayer1;
            bool& isHeld = isFirst ? state.isHeld1 : state.isHeld2;
            bool& isAutoClick = isFirst ? state.isAuto1 : state.isAuto2;
            int& autoReleaseTick = isFirst ? state.autoReleaseTick1 : state.autoReleaseTick2;
            if (state.orbClickDelayTicks > 0) {
                if (!isHeld && autoReleaseTick == -1) {
                    autoReleaseTick = state.simulationTick + state.orbClickDelayTicks;
                }
            }
            else if (!isHeld && !isAutoClick) {
                playerObject->pushButton(PlayerButton::Jump);
                isAutoClick = true;
            }
        }
    }

    void flipGravity(PlayerObject* playerObject, bool shouldFlip, bool suppressEffects) {
        if (state.isSimulating) {
            if (state.smartEnabled && isGhost(playerObject)) {
                playerObject->flipGravity(shouldFlip, true);
            }
            return;
        }
        GJBaseGameLayer::flipGravity(playerObject, shouldFlip, suppressEffects);
    }

    void gameEventTriggered(GJGameEvent gameEvent, int materialId, int playerId) {
        if (state.isSimulating) {
            return;
        }
        GJBaseGameLayer::gameEventTriggered(gameEvent, materialId, playerId);
    }

    void toggleGroupTriggered(int groupId, bool shouldActivate, gd::vector<int> const& remapKeyVector, int triggerId, int controlId) {
        if (state.isSimulating) {

            if (!shouldActivate) {
                if (auto* gameObjects = this->getGroup(groupId)) {
                    for (auto* gameObject : CCArrayExt<GameObject*>(gameObjects)) {
                        state.simulationDisabledObjects.insert(gameObject);
                    }
                }
            }
            return;
        }
        GJBaseGameLayer::toggleGroupTriggered(groupId, shouldActivate, remapKeyVector, triggerId, controlId);
    }

    void spawnGroup(int groupId, bool isOrdered, double delaySeconds, gd::vector<int> const& remapKeyVector, int triggerId, int controlId) {
        if (state.isSimulating) {
            return;
        }
        GJBaseGameLayer::spawnGroup(groupId, isOrdered, delaySeconds, remapKeyVector, triggerId, controlId);
    }

    void toggleGroup(int groupId, bool shouldActivate) {
        if (state.isSimulating) {
            return;
        }
        GJBaseGameLayer::toggleGroup(groupId, shouldActivate);
    }

    void pickupItem(EffectGameObject* effectObject) {
        if (state.isSimulating) {
            return;
        }
        GJBaseGameLayer::pickupItem(effectObject);
    }

    void updateCounters(int counterItemId, int counterValue) {
        if (state.isSimulating) {
            return;
        }
        GJBaseGameLayer::updateCounters(counterItemId, counterValue);
    }

    void activateItemEditTrigger(ItemTriggerGameObject* itemTrigger) {
        if (state.isSimulating) {
            return;
        }
        GJBaseGameLayer::activateItemEditTrigger(itemTrigger);
    }

    void activateItemCompareTrigger(ItemTriggerGameObject* itemTrigger, gd::vector<int> const& remapKeyVector) {
        if (state.isSimulating) {
            return;
        }
        GJBaseGameLayer::activateItemCompareTrigger(itemTrigger, remapKeyVector);
    }

    void playerWillSwitchMode(PlayerObject* playerObject, GameObject* gameObject) {
        if (state.isSimulating) {
            state.hasGhostHitPortal = true;
            return;
        }
        GJBaseGameLayer::playerWillSwitchMode(playerObject, gameObject);
    }
};

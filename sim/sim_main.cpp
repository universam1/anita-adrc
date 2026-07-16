// Host simulator: runs the real ControllerCore + SsrModulator against the
// BoilerModel and dumps one CSV row per control step.
//
//   pio run -e native
//   .pio/build/native/program --scenario full --csv out.csv
//   python tools/plot_sim.py out.csv
//
// Scenarios:
//   full             cold start, single espresso, warm-up flush, max draw
//   cold_start       20 C -> setpoint with delta-T boost (default params)
//   cold_start_noboost   same but kBoost = 0 (baseline for comparison)
//   espresso         from steady state: 30 ml over 30 s
//   maxdraw          from steady state: 250 ml over 30 s (full boiler volume)
//   flush            from mid warm-up: 60 ml over 15 s

#include <cstdio>
#include <cstring>
#include <string>

#include "SimHarness.h"

using namespace anita;

namespace {

FILE* gCsv = nullptr;

void csvHeader() {
    if (!gCsv) return;
    std::fprintf(gCsv,
                 "t,boiler_sens,group_sens,brass,water,group,boiler_set,"
                 "group_set,duty,z1,z2,boost,offset,state,draw\n");
}

std::function<void(const SimHarness::Snapshot&)> logger(ControllerCore& core) {
    return [&core](const SimHarness::Snapshot& s) {
        if (!gCsv) return;
        std::fprintf(gCsv,
                     "%.1f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.4f,%.3f,%.4f,"
                     "%.3f,%.3f,%s,%d\n",
                     s.tS, s.boilerSensC, s.groupSensC, s.brassC, s.waterC,
                     s.groupC, s.out.boilerSetC, core.setpoint(), s.out.duty,
                     s.out.z1, s.out.z2, s.out.boostC, s.out.offsetSsC,
                     stateName(s.out.state), s.out.drawActive ? 1 : 0);
    };
}

struct Overrides {
    float wc = -1, wo = -1, b0 = -1, pred = -1, kboost = -1;
};

int runScenario(const std::string& name, const Overrides& ov) {
    CoreConfig cfg;
    if (ov.wc > 0) cfg.adrc.wc = ov.wc;
    if (ov.wo > 0) cfg.adrc.wo = ov.wo;
    if (ov.b0 > 0) cfg.adrc.b0 = ov.b0;
    if (ov.pred >= 0) cfg.adrc.predS = ov.pred;
    if (ov.kboost >= 0) cfg.groupComp.kBoost = ov.kboost;
    if (name == "cold_start_noboost") cfg.groupComp.kBoost = 0.0f;

    const bool fromSteady =
        (name == "espresso" || name == "maxdraw");
    SimHarness h(cfg, BoilerModelParams{}, fromSteady ? 90.0f : 20.0f);
    auto log = logger(h.core());

    if (name == "cold_start" || name == "cold_start_noboost") {
        h.run(2400.0f, log);
    } else if (name == "espresso") {
        h.run(900.0f, log);  // settle
        h.model().startDraw(30.0f, 30.0f);
        h.run(300.0f, log);
    } else if (name == "maxdraw") {
        h.run(900.0f, log);
        h.model().startDraw(250.0f, 30.0f);
        h.run(600.0f, log);
    } else if (name == "flush") {
        h.run(300.0f, log);  // mid warm-up
        h.model().startDraw(60.0f, 15.0f);
        h.run(600.0f, log);
    } else if (name == "full") {
        h.run(2400.0f, log);  // cold start to ready
        h.model().startDraw(30.0f, 30.0f);  // espresso
        h.run(300.0f, log);
        h.model().startDraw(60.0f, 15.0f);  // flush
        h.run(300.0f, log);
        h.model().startDraw(250.0f, 30.0f);  // two big cups
        h.run(600.0f, log);
    } else {
        std::fprintf(stderr, "unknown scenario: %s\n", name.c_str());
        return 2;
    }

    const auto& s = h.last();
    std::fprintf(stderr,
                 "[%s] t=%.0fs boiler=%.2fC group=%.2fC duty=%.3f state=%s "
                 "fault=%s\n",
                 name.c_str(), s.tS, s.boilerSensC, s.groupSensC, s.out.duty,
                 stateName(s.out.state), faultName(s.out.fault));
    return s.out.fault == Fault::None ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    std::string scenario = "full";
    std::string csvPath;
    Overrides ov;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--scenario") && i + 1 < argc) scenario = argv[++i];
        else if (!std::strcmp(argv[i], "--csv") && i + 1 < argc) csvPath = argv[++i];
        else if (!std::strcmp(argv[i], "--wc") && i + 1 < argc) ov.wc = std::stof(argv[++i]);
        else if (!std::strcmp(argv[i], "--wo") && i + 1 < argc) ov.wo = std::stof(argv[++i]);
        else if (!std::strcmp(argv[i], "--b0") && i + 1 < argc) ov.b0 = std::stof(argv[++i]);
        else if (!std::strcmp(argv[i], "--pred") && i + 1 < argc) ov.pred = std::stof(argv[++i]);
        else if (!std::strcmp(argv[i], "--kboost") && i + 1 < argc) ov.kboost = std::stof(argv[++i]);
        else {
            std::fprintf(stderr,
                         "usage: %s [--scenario name] [--csv out.csv] "
                         "[--wc x] [--wo x] [--b0 x] [--pred x] [--kboost x]\n",
                         argv[0]);
            return 2;
        }
    }
    if (!csvPath.empty()) {
        gCsv = std::fopen(csvPath.c_str(), "w");
        if (!gCsv) {
            std::fprintf(stderr, "cannot open %s\n", csvPath.c_str());
            return 2;
        }
        csvHeader();
    }
    const int rc = runScenario(scenario, ov);
    if (gCsv) std::fclose(gCsv);
    return rc;
}

# Host build of the lap-detection replay harness.
# Compiles the real lib/LapManager against a shimmed Arduino.h — no PlatformIO,
# no hardware, nothing that touches the firmware build.
#
#   make && ./replay GH011973.csv -23.60488969 -46.83622658 -23.60493793 -46.83641597
#
# make VERBOSE=1   surfaces LapManager's own log_d() gate diagnostics.

ROOT    := ../..
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra \
            -Ishim -I$(ROOT)/lib/LapManager -I$(ROOT)/lib/Shared \
            -I$(ROOT)/lib/DemoTrack

ifdef VERBOSE
CXXFLAGS += -DREPLAY_VERBOSE=1
endif

# Headers as explicit prerequisites. Without these, make sees only the .cpp
# files and happily reuses a binary built against an older header — editing
# LapManager.h or DemoTrack.h and re-running gives you the previous build's
# numbers with no warning, which is a genuinely expensive way to be misled.
LAP_H  := $(ROOT)/lib/LapManager/LapManager.h $(ROOT)/lib/Shared/EspNowProtocol.h
DEMO_H := $(ROOT)/lib/DemoTrack/DemoTrack.h

all: replay sectors delta demo navpvt

replay: replay.cpp $(ROOT)/lib/LapManager/LapManager.cpp $(LAP_H)
	$(CXX) $(CXXFLAGS) -o $@ replay.cpp $(ROOT)/lib/LapManager/LapManager.cpp

# Same harness with split gates, for validating sector timing against a
# circuit's official sheet.
sectors: sectors.cpp $(ROOT)/lib/LapManager/LapManager.cpp $(LAP_H)
	$(CXX) $(CXXFLAGS) -o $@ sectors.cpp $(ROOT)/lib/LapManager/LapManager.cpp

# Live lap delta: replays a session and checks the continuously matched delta
# against the arithmetic one at every line crossing. Run it after touching the
# reference-trace matching: a wrong match produces a plausible number, not a
# crash, and the track is a poor place to find that out.
delta: delta.cpp $(ROOT)/lib/LapManager/LapManager.cpp $(LAP_H)
	$(CXX) $(CXXFLAGS) -o $@ delta.cpp $(ROOT)/lib/LapManager/LapManager.cpp

# DEMO mode's synthetic track, driven through the real LapManager. Catches the
# one failure that is invisible on the bench: a gate wound the wrong way round
# is not an error, it is a lap counter stuck at zero.
demo: demo.cpp $(ROOT)/lib/LapManager/LapManager.cpp $(LAP_H) $(DEMO_H)
	$(CXX) $(CXXFLAGS) -o $@ demo.cpp $(ROOT)/lib/LapManager/LapManager.cpp

# Offset-level check on the real UBX-NAV-PVT reader. Run it after touching
# UbloxGpsProvider — a wrong byte offset produces plausible telemetry, not a crash.
navpvt: navpvt.cpp $(ROOT)/lib/GpsManager/providers/UbloxGpsProvider.cpp \
        $(ROOT)/lib/GpsManager/providers/UbloxGpsProvider.h $(ROOT)/lib/GpsManager/IGpsProvider.h
	$(CXX) $(CXXFLAGS) -I$(ROOT)/lib/GpsManager/providers -o $@ \
	    navpvt.cpp $(ROOT)/lib/GpsManager/providers/UbloxGpsProvider.cpp

clean:
	rm -f replay sectors delta demo navpvt

.PHONY: clean

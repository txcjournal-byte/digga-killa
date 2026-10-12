// Standalone checks of single UI / DSP components (no plugin around them).
// Usage: TrapWinComponentTest <waveform|midi|fx> <outDir> [args...]
// Each part lives in its own file (tools/test_<part>.cpp) and returns 0 when all checks pass.
#include <juce_gui_basics/juce_gui_basics.h>

#include <cstdio>

int runWaveformTest (int argc, char** argv);
int runMidiTest (int argc, char** argv);
int runFxTest (int argc, char** argv);

int main (int argc, char** argv)
{
    const juce::ScopedJuceInitialiser_GUI gui;
    const juce::String part (argc > 1 ? argv[1] : "");
    if (part == "waveform") return runWaveformTest (argc, argv);
    if (part == "midi")     return runMidiTest (argc, argv);
    if (part == "fx")       return runFxTest (argc, argv);
    std::printf ("usage: TrapWinComponentTest <waveform|midi|fx> <outDir> [args]\n");
    return 1;
}

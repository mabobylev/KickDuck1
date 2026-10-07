#include "DuckingCurve.h"

// DuckingCurve is intentionally header-only.
// It is GUI/state-side only; the audio thread uses PluginProcessor's
// fixed-size runtime curve snapshot instead.

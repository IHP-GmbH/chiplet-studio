#pragma once

// GenericLayers.h - canonical "black-box / pads-only" layer vocabulary.
//
// Layer numbers for chiplets from commercial / closed PDK nodes whose GDS
// carries only metal pads + pad names and no .lyp. SOURCE OF TRUTH is
// adk/config/chiplet_pads.json; this header mirrors it by hand (chiplet-studio
// intentionally has no ADK runtime coupling). Keep the two in sync.
//
// The role strings are used as stackup-layer names by the black-box stackup
// augmentation (AssemblyView::augmentStackupForBlackBox) so the generic color
// scheme (configs/stackups/colors/generic/blackbox.yaml) and the
// LayerMeshBuilder fallback can color the die body and pads by role.

namespace chiplet {
namespace GenericLayers {

constexpr int PAD_LAYER         = 205;
constexpr int PAD_DATATYPE      = 0;
constexpr int PAD_TEXT_LAYER    = 205;
constexpr int PAD_TEXT_DATATYPE = 25;
constexpr int OUTLINE_LAYER     = 206;
constexpr int OUTLINE_DATATYPE  = 0;

constexpr const char* PAD_ROLE     = "pad";
constexpr const char* OUTLINE_ROLE = "outline";

}  // namespace GenericLayers
}  // namespace chiplet

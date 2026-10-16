# BB2V1 Thin Enclosure v3: Printed-Fit Revision

This revision addresses the user's actual v2 print. All units are millimeters.
The v2 base is geometrically unchanged. Reprint ONLY the front cover, screen
retaining frame, and four key caps. Keep the existing PCB mounting and base.

## Changes

- Screen pocket: 60.2 x 31.2 for the approximately 59 x 30 screen, nominal
  clearance 0.6 per side instead of 0.2. The visible window stays 55 x 26.
- Retainer: no rectangular insertion channels and no tuck tabs. Place the
  frame directly onto four round screw seats from inside the front cover.
- Fastening: four M1 screws driven into plain plastic pilots. No printed
  threads, metal nuts, or inserts are required. Retainer through-holes are
  1.4 diameter. Cover pilots are 0.95 with 1.4 entry mouths, tapering over 0.35.
  A loose through-hole and a screw-gripping pilot deliberately differ in size.
- Key skirts: bottom raised 0.3, removing material underneath. Captive flange
  thickness changes from 0.85 to 0.55. The actuator-contact face is unchanged,
  remaining 0.2 above the modeled switch actuator at nominal CAD rest.
- Key guide: diameter 6.6 instead of 7.0, in the unchanged 7.6 cover openings.
  Nominal radial running gap is 0.5. The cup inside diameter is 4.8, giving
  extra room around the switch actuator without changing its contact height.
- Key tops: nominal protrusion 1.4 instead of 0.8. After taking up the 0.2
  actuator gap, approximately 1.2 remains above the faceplate.
- Legends: font size 4 instead of 3, actual uppercase glyph height about 2.9.
  Engraving depth is 0.6 instead of 0.4. At least 0.6 of plate remains below it.
  Labels are still NEXT / PREV / OK / BACK, for SW4 / SW8 / SW5 / SW2.

## What to Print

Use `front_lid_print.stl`, `screen_retainer_print.stl`, and the four
`button_SW*_print.stl` files, at 100% scale in millimeters. The separate
replacement-only archive excludes the unchanged base. `*_assembly.stl`
files use assembly coordinates, not individual print orientation.

The main body remains approximately 87.1 x 56.4 x 14.8, excluding the side eye.
Including raised buttons, maximum nominal thickness is about 16.2.
USB-C and switch openings, ventilation, mounting pillars, eight BBV3 lid snaps,
left-side hanging eye and its 5 mm hole remain unchanged. No fit coupon is supplied.

## Assembly

1. Insert the four new caps from inside the faceplate. Confirm each moves freely.
2. Retain 0.2 compliant insulating pads on the screen OUTER BORDER above and
   below the screen. Keep pads away from the active area and FPC bond.
3. Put the screen into the enlarged pocket. Maintain the existing left ribbon
   route; do not crease or stretch it, and do not press on the glass.
4. Place the new retainer straight down onto its four seats. It does not need
   to slide through any square channel. The seats limit how far the retainer
   can be tightened toward the glass.
5. Use four M1 screws, preferably 2 long (maximum 2.5), with non-countersunk heads at
   least 1.8 diameter. The modeled clearance envelope allows a 2.0-diameter,
   0.7-high head. Do not use screws longer than 2.5 without checking the depth.
   A 2.5-long screw leaves about 1.0 before the exterior face in the CAD model.
6. Form the plain pilot carefully by hand, ideally before fitting the screen.
   No modeled thread or nut is used. Tighten only gently until seated; do not
   torque an M1 screw against glass or force an undersized/blocked printed hole.
   If extrusion has closed the tiny pilot, carefully clear it rather than
   forcing the screw. Widening the entire pilot to the through-hole size would
   remove the M1 thread grip. Actual print shrink and screw type affect grip.
7. Reuse the mounted PCB and existing base, arrange ribbon slack, and snap the
   new faceplate onto the unchanged eight BBV3 locations.

## Validation and Limits

Each of seven complete-assembly parts is a valid single CAD solid and a connected,
watertight, consistently oriented positive-volume STL. Base equality against v2
is checked by symmetric-difference volume. Static component intersections, screen
translations up to 0.5 X/Y, key travel up to 0.6 down with up to 0.4 lateral play,
and the screw-head clearance envelopes are checked digitally.

The modeled switch actuator is excluded from key-motion intersections because
it is intentionally depressed. Thread engagement in the plain pilot is also
intentional and is not modeled as a printable thread. Digital checks do not
guarantee the user's printer tolerances, screw grip, switch stroke or screen fit.
The screen dimensions remain approximate; this v3 has not been physically printed.
Battery cavity and all original battery insulation/clearance requirements remain
unchanged. Never force the lid closed against the cell or screen.

Editable STEP assemblies and CadQuery sources are included. Run
`build_enclosure.py`, then `verify_and_preview.py` with the bundled requirements.

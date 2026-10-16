"""Build a thin printable enclosure from the user's header-free BB2V1 STEP."""
import json
import re
from pathlib import Path

import cadquery as cq
from OCP.BRepAdaptor import BRepAdaptor_Surface
from OCP.GeomAbs import GeomAbs_Cylinder

from inspect_models import box_data, read_parts, v2_reference_file

ROOT = Path(__file__).parent
OUT = ROOT / "deliverables"
PARAMS = {
    "source": "../../../hardware/reference/BB2V1.step",
    "snap_reference": "reference_BBV3.step",
    "wall": 1.6,
    "floor": 1.2,
    "lid": 1.2,
    "rear_clearance": 0.6,
    "fit_gap": 0.2,
    "pcb_xy_clearance": 0.6,
    "left_flex_clearance": 3.3,
    "screen_x": 3.0,
    "screen_y": 17.2,
    "screen_length": 59.0,
    "screen_width": 30.0,
    "screen_thickness": 1.0,
    "screen_foam": 0.2,
    "screen_window_length": 55.0,
    "screen_window_width": 26.0,
    "screw_pilot_m2": 1.7,
    "screw_pilot_m1": 0.95,
    "screw_clearance_m1": 1.4,
    "screen_screw_entry_diameter": 1.4,
    "screen_xy_clearance": 0.6,
    "button_protrusion": 1.4,
    "button_skirt_shortening": 0.3,
    "button_guide_radius": 3.3,
    "button_cup_radius": 2.4,
    "label_size": 4.0,
    "label_depth": 0.6,
    "lanyard_projection": 8.0,
    "lanyard_hole_diameter": 5.0,
}


def rect(x, y, w, h, z, depth, radius=0):
    wp = cq.Workplane("XY").workplane(offset=z).center(x, y)
    if radius:
        return wp.sketch().rect(w, h).vertices().fillet(radius).finalize().extrude(depth).val()
    return wp.rect(w, h).extrude(depth).val()


def block(x0, x1, y0, y1, z0, z1, radius=0):
    return rect((x0+x1)/2, (y0+y1)/2, x1-x0, y1-y0, z0, z1-z0, radius)


def cylinder(x, y, z, radius, height):
    return cq.Workplane("XY").workplane(offset=z).center(x, y).circle(radius).extrude(height).val()


def join(a, b):
    return a.fuse(b).clean()


def build():
    OUT.mkdir(exist_ok=True)
    config = ROOT / "parameters.json"
    if config.exists():
        PARAMS.update(json.loads(config.read_text(encoding="utf-8")))
    config.write_text(json.dumps(PARAMS, indent=2), encoding="utf-8")
    p = PARAMS
    reference = read_parts(ROOT / p["source"])
    old_parts = read_parts(ROOT / p["snap_reference"])
    old_lid = next(s for _, s in old_parts if s.BoundingBox().ymin < -10)
    old_shell = next(s for _, s in old_parts if s.BoundingBox().xlen > 95)
    old_clips = old_lid.intersect(block(-100,150,-100,100,-6.9999,0)).Solids()
    old_clip = next(s for s in old_clips if 36 < s.BoundingBox().xmin < 37 and s.BoundingBox().ymin > -20 and s.BoundingBox().zmax > -5)
    # Copy the actual rounded snap profile, rather than approximating it.
    clip_template = old_clip.rotate((0,0,0),(1,0,0),180).translate((-38.190344806396, -12.22892873877976, -6.9999))
    old_groove = block(36.28745999492,39.68745999492,-3.0,-1.933302006604,5.0,7.5).cut(old_shell)
    assert len(old_groove.Solids()) == 1 and old_groove.isValid()
    groove_template = old_groove.translate((-37.98745999492,1.933302006604,-8.0))
    pcb = next(shape for name, shape in reference if "Board~" in name)
    bb = pcb.BoundingBox()
    buttons = {}
    for name, shape in reference:
        match = re.search(r" / (SW[124568])~", name)
        if match:
            b = shape.BoundingBox()
            buttons[match[1]] = (b.center.x, b.center.y, b.zmax)
    holes = []
    for face in pcb.Faces():
        surface = BRepAdaptor_Surface(face.wrapped)
        if surface.GetType() == GeomAbs_Cylinder:
            c = surface.Cylinder()
            if abs(c.Radius()-1.1) < .01 and abs(c.Axis().Direction().Z()) > .99:
                holes.append((c.Location().X(), c.Location().Y()))
    assert len(holes) == 4, holes
    rear_min = min(shape.BoundingBox().zmin for _, shape in reference)
    front_max = max(shape.BoundingBox().zmax for _, shape in reference)
    inner_bottom = rear_min-p["rear_clearance"]
    bottom = inner_bottom-p["floor"]
    underside = front_max+p["fit_gap"]
    top = underside+p["lid"]
    ix0, ix1 = bb.xmin-p["left_flex_clearance"], bb.xmax+p["pcb_xy_clearance"]
    iy0, iy1 = bb.ymin-p["pcb_xy_clearance"], bb.ymax+p["pcb_xy_clearance"]
    ox0, ox1 = ix0-p["wall"], ix1+p["wall"]
    oy0, oy1 = iy0-p["wall"], iy1+p["wall"]
    outer_r, inner_r = 3.4, 1.8
    base = block(ox0, ox1, oy0, oy1, bottom, underside, outer_r)
    base = base.cut(block(ix0, ix1, iy0, iy1, inner_bottom, underside+1, inner_r))

    for x, y in holes:
        boss = cylinder(x, y, inner_bottom-.1, 2.5, -inner_bottom+.1)
        base = join(base, boss)
        # Blind pilots, open on the PCB seating face; no exposed screws near cell.
        base = base.cut(cylinder(x, y, inner_bottom+.65, p["screw_pilot_m2"]/2, 8))

    # The USB-C mouth faces +X, on the rear of the PCB.
    usb = next(s for n, s in reference if " / USB1~" in n).BoundingBox()
    usb_y = usb.center.y
    usb_z = -1.25
    # BBV3's actual rounded opening is 13.94 x 7.66 mm, corner radius 1.75.
    # Move that opening to the USB-C position in the current, rear-mounted PCB.
    port = cq.Workplane("YZ", origin=(ix1-.6, usb_y, usb_z)).sketch().rect(13.94, 7.66).vertices().fillet(1.75).finalize().extrude(p["wall"]+1.3).val()
    base = base.cut(port)
    # Full travel and fingertip access; only a side opening, not a top hole.
    base = base.cut(block(4.85, 15.0, iy1-.75, oy1+1, 1.35, 5.7, .65))

    # Side slots sit behind the charging section, not beneath the cell pouch.
    for y in (27.0, 32.0, 37.0, 42.0):
        base = base.cut(block(ix1-.2, ox1+.3, y-.55, y+.55, -4.7, -1.2))
    for x in (51.0, 56.0, 61.0, 66.0):
        base = base.cut(block(x-.55, x+.55, oy0-.3, iy0+.2, -4.7, -1.2))

    # Move the eye to the opposite, left-hand wall; keep it outside the flex
    # channel. The larger opening retains 1.5 mm of material at its outer end.
    lug_y = 45.0
    lug = block(ox0-p["lanyard_projection"], ox0+.8, lug_y-4.5, lug_y+4.5, bottom, bottom+3.2, 2.0)
    lug = lug.cut(cylinder(ox0-4.0, lug_y, bottom-.5, p["lanyard_hole_diameter"]/2, 4.2))
    base = join(base, lug)

    lid = block(ox0, ox1, oy0, oy1, underside, top, outer_r)
    latch_positions = [(20,iy0,0), (53,iy0,0), (20,iy1,180), (53,iy1,180),
                       (ix0,10,-90), (ix0,46,-90), (ix1,24,90), (ix1,46,90)]
    for x, y, angle in latch_positions:
        clip = clip_template.rotate((0,0,0),(0,0,1),angle)
        groove = groove_template.rotate((0,0,0),(0,0,1),angle)
        inward = {0:(0,.3),180:(0,-.3),-90:(.3,0),90:(-.3,0)}[angle]
        lid = join(lid, clip.translate((x+inward[0],y+inward[1],underside+.02)))
        base = base.cut(groove.translate((x,y,underside+.02)))

    # Window is smaller than the measured outer frame, not the LCD's active area.
    sx, sy = p["screen_x"], p["screen_y"]
    sw, sh = p["screen_length"], p["screen_width"]
    cx, cy = sx+sw/2, sy+sh/2
    face_z = underside-p["screen_foam"]
    back_z = face_z-p["screen_thickness"]
    ring_top = back_z-p["screen_foam"]
    ring_bottom = ring_top-.9
    lid = lid.cut(rect(cx, cy, p["screen_window_length"], p["screen_window_width"], underside-.1, p["lid"]+.2, .7))
    pocket_w = sw+2*p["screen_xy_clearance"]
    pocket_h = sh+2*p["screen_xy_clearance"]
    socket = rect(cx, cy, pocket_w+1.6, pocket_h+1.6, back_z-.2, underside-back_z+.25, .8)
    socket = socket.cut(rect(cx, cy, pocket_w, pocket_h, back_z-.3, underside-back_z+.4, .3))
    # Ribbon exits the left edge; leave both the socket and retainer open here.
    socket = socket.cut(block(sx-2, sx+2, 26.0, 39.0, ring_bottom-1, top+1))
    lid = join(lid, socket)
    retainer = rect(cx, cy, sw+1.2, sh+1.2, ring_bottom, .9, .6)
    retainer = retainer.cut(rect(cx, cy, sw-2.8, sh-2.8, ring_bottom-.1, 1.1, .5))
    retainer = retainer.cut(block(sx-2, sx+2, 26.0, 39.0, ring_bottom-1, top+1))

    # Drop-in retainer: replace the two tight printed square channels with
    # two additional plain M1 screw seats. No printed threads or nuts.
    frame_screws = ((sx-2.7, 22.0), (sx-2.7, 42.0),
                    (sx+sw+3.4, 18.0), (sx+sw+3.4, 46.2))
    for x, y in frame_screws:
        # Bearing faces intentionally touch: this stop keeps tightening the
        # screws from moving the retainer upward and crushing the glass pads.
        lid = join(lid, cylinder(x, y, ring_top, 2.1, underside-ring_top+.05))
        lid = lid.cut(cylinder(x, y, ring_top-.1, p["screw_pilot_m1"]/2, top-.65-(ring_top-.1)))
        entry = cq.Solid.makeCone(p["screen_screw_entry_diameter"]/2, p["screw_pilot_m1"]/2,
                                 .35, cq.Vector(x,y,ring_top-.01), cq.Vector(0,0,1))
        lid = lid.cut(entry)
        if x < sx:
            ear = block(x-.8, sx+.1, y-1.8, y+1.8, ring_bottom, ring_top)
        else:
            ear = block(sx+sw-.1, x+.8, y-1.8, y+1.8, ring_bottom, ring_top)
        retainer = join(retainer, ear)
        retainer = join(retainer, cylinder(x, y, ring_bottom, 2.1, .9))
        retainer = retainer.cut(cylinder(x, y, ring_bottom-.1, p["screw_clearance_m1"]/2, 1.1))

    parts = {"base": base.clean(), "front_lid": lid.clean(), "screen_retainer": retainer.clean()}
    active = ("SW4", "SW8", "SW5", "SW2")
    legends = dict(zip(active, ("NEXT", "PREV", "OK", "BACK")))
    label_report = []
    for label in active:
        x, y, z = buttons[label]
        lid = lid.cut(cylinder(x, y, underside-.2, 3.8, p["lid"]+.4))
        letters = (cq.Workplane("XY").workplane(offset=top-p["label_depth"])
                   .center(x,y-5.8).text(legends[label],p["label_size"],p["label_depth"]+.1,
                                        combine=False, fontPath="C:/Windows/Fonts/arialbd.ttf",
                                        kind="bold",halign="center",valign="center").val())
        lb = letters.BoundingBox()
        assert lb.ymax < y-3.8-.2, (label,"legend too close to opening")
        assert lb.ymin > oy0+.6, (label,"legend outside faceplate")
        lid = lid.cut(letters)
        label_report.append({"switch":label,"legend":legends[label],"depth":p["label_depth"],
                             "bounds":box_data(letters)})
        # Shorten only the skirt, not the actuator-contact face at z+0.2.
        flange_bottom = z-.95+p["button_skirt_shortening"]
        flange_top = z-.1
        assert flange_top-flange_bottom >= .5
        cap = cylinder(x, y, flange_bottom, 4.5, flange_top-flange_bottom)
        cap = join(cap, cylinder(x, y, flange_top, p["button_guide_radius"], top+p["button_protrusion"]-flange_top))
        cap = cap.cut(cylinder(x, y, flange_bottom-.1, p["button_cup_radius"], z+.2-(flange_bottom-.1)))
        parts["button_"+label] = cap.clean()
    parts["front_lid"] = lid.clean()

    screen = block(sx, sx+sw, sy, sy+sh, back_z, face_z)
    report = {
        "units": "mm",
        "pcb": box_data(pcb),
        "pcb_mount_holes": holes,
        "button_centers": buttons,
        "body_without_lanyard": [ox1-ox0, oy1-oy0, top-bottom],
        "maximum_height_with_buttons": top+p["button_protrusion"]-bottom,
        "lanyard_projection": p["lanyard_projection"],
        "lanyard_side": "left (-X), opposite USB-C",
        "lanyard_hole_diameter": p["lanyard_hole_diameter"],
        "maintenance_area_ribs": 0,
        "recessed_legends": label_report,
        "screen_fit": {"pocket_size":[pocket_w,pocket_h],
                       "screen_xy_clearance_per_side":p["screen_xy_clearance"],
                       "square_insertion_channels":0,"fixing":"Four M1 screws, plain holes; no modeled threads or nuts",
                       "screw_positions":frame_screws,"screw_pilot_diameter":p["screw_pilot_m1"],
                       "screw_entry_diameter":p["screen_screw_entry_diameter"],
                       "retainer_through_hole_diameter":p["screw_clearance_m1"],
                       "recommended_screw_length":2.0,"maximum_screw_length":2.5,
                       "minimum_tip_to_front_surface":top-(ring_bottom+2.5),
                       "screen_shift_collision_checks":[],"fastener_clearance_collisions":[]},
        "button_fit": {"switch_height_above_pcb":front_max-bb.zmax,
                       "cap_actuator_gap":.2,"guide_radial_gap":3.8-p["button_guide_radius"],
                       "skirt_shortening":p["button_skirt_shortening"],
                       "flange_thickness":.85-p["button_skirt_shortening"],
                       "guide_diameter":2*p["button_guide_radius"],
                       "cup_diameter":2*p["button_cup_radius"],
                       "nominal_protrusion":p["button_protrusion"],
                       "protrusion_after_taking_up_actuator_gap":p["button_protrusion"]-.2,
                       "motion_check_downward":.6,"lateral_motion_check":.4,
                       "motion_non_actuator_collisions":[]},
        "floor_inner_z": inner_bottom,
        "rear_component_min_z": rear_min,
        "screen_proxy": box_data(screen),
        "screen_dimensions_status": "Approximate user measurements; physical assembly not tested. No fit coupon requested.",
        "battery_status": "No battery thickness/outline supplied. Rear cavity is open; do not compress pouch or cover copper with an uninsulated cell.",
        "minimum_running_gap": .2,
        "snap_profile_source": "Actual BBV3 clip and mating pocket, repositioned without scaling; 8 latches.",
        "usb_opening": {"width":13.94,"height":7.66,"corner_radius":1.75,"source":"BBV3"},
        "parts": {},
        "collisions": [],
    }
    assy = cq.Assembly(name="BB2V1_thin_shell")
    colors = {"base": (.18,.22,.25), "front_lid": (.72,.78,.78), "screen_retainer": (.4,.44,.45)}
    for name, shape in parts.items():
        assert shape.isValid(), name+" invalid"
        assert len(shape.Solids()) == 1, (name, len(shape.Solids()))
        assy.add(shape, name=name, color=cq.Color(*colors.get(name, (.16,.52,.46))))
        cq.exporters.export(shape, str(OUT/(name+"_assembly.stl")), tolerance=.035, angularTolerance=.12)
        oriented = shape
        if name in ("front_lid", "screen_retainer"):
            oriented = oriented.rotate((0,0,0), (1,0,0), 180)
        bounds = oriented.BoundingBox()
        oriented = oriented.translate((-bounds.xmin, -bounds.ymin, -bounds.zmin))
        cq.exporters.export(oriented, str(OUT/(name+"_print.stl")), tolerance=.035, angularTolerance=.12)
        cq.exporters.export(shape, str(OUT/(name+".step")))
        report["parts"][name] = {"solid_count": len(shape.Solids()), "valid": shape.isValid(), "volume": shape.Volume(), **box_data(shape)}
    assy.export(str(OUT/"BB2V1_thin_shell.step"))
    for name, shape in parts.items():
        for refname, refshape in reference:
            # AABB rejection avoids expensive intersections for remote parts.
            a, b = shape.BoundingBox(), refshape.BoundingBox()
            if a.xmax < b.xmin or a.xmin > b.xmax or a.ymax < b.ymin or a.ymin > b.ymax or a.zmax < b.zmin or a.zmin > b.zmax:
                continue
            volume = shape.intersect(refshape).Volume()
            if volume > .001:
                report["collisions"].append({"part": name, "reference": refname, "volume_mm3": volume})
        overlap = shape.intersect(screen).Volume()
        if overlap > .001:
            report["collisions"].append({"part": name, "reference": "screen_proxy", "volume_mm3": overlap})
    # Check key travel and lateral play against stationary geometry. The switch's
    # moving actuator is excluded because it is intentionally pressed.
    for label in active:
        switch_shape = next(s for n,s in reference if " / "+label+"~" in n)
        x,y,z = buttons[label]
        fixed_body = switch_shape.intersect(block(x-6,x+6,y-6,y+6,bb.zmax-.1,z-1.5))
        for down in (0,.2,.4,.6):
            for dx,dy in ((0,0),(.4,0),(-.4,0),(0,.4),(0,-.4)):
                pressed = parts["button_"+label].translate((dx,dy,-down))
                targets = [(other,parts[other]) for other in ("front_lid","screen_retainer","base")]
                targets.append(("fixed_switch_body",fixed_body))
                for other,shape in targets:
                    vol = pressed.intersect(shape).Volume()
                    if vol > .001:
                        report["button_fit"]["motion_non_actuator_collisions"].append(
                            {"button":label,"part":other,"offset":[dx,dy,-down],"volume":vol})
    assert not report["button_fit"]["motion_non_actuator_collisions"], report["button_fit"]
    for dx in (-.5,0,.5):
        for dy in (-.5,0,.5):
            shifted_screen = screen.translate((dx,dy,0))
            for name,shape in parts.items():
                vol = shape.intersect(shifted_screen).Volume()
                if vol > .001:
                    report["screen_fit"]["screen_shift_collision_checks"].append(
                        {"part":name,"screen_shift":[dx,dy],"volume":vol})
    assert not report["screen_fit"]["screen_shift_collision_checks"], report["screen_fit"]
    # Only head/tip clearance is modeled; thread engagement in the pilot is intentional.
    for x,y in frame_screws:
        head = cylinder(x,y,ring_bottom-.7,1.0,.7)
        for name,shape in [(n,s) for n,s in reference]+[("base",parts["base"]),("front_lid",parts["front_lid"])]:
            a,b = head.BoundingBox(),shape.BoundingBox()
            if a.xmax<b.xmin or a.xmin>b.xmax or a.ymax<b.ymin or a.ymin>b.ymax or a.zmax<b.zmin or a.zmin>b.zmax:
                continue
            vol = head.intersect(shape).Volume()
            if vol > .001:
                report["screen_fit"]["fastener_clearance_collisions"].append(
                    {"position":[x,y],"reference":name,"volume":vol})
    assert not report["screen_fit"]["fastener_clearance_collisions"], report["screen_fit"]
    previous_base = cq.importers.importStep(str(v2_reference_file("base.step"))).val()
    delta = parts["base"].cut(previous_base).Volume()+previous_base.cut(parts["base"]).Volume()
    assert delta < .001, ("base changed",delta)
    report["base_compatibility_with_v2"] = {"unchanged":True,"symmetric_difference_volume_mm3":delta}
    names = list(parts)
    for i, name in enumerate(names):
        for other in names[i+1:]:
            vol = parts[name].intersect(parts[other]).Volume()
            if vol > .001:
                report["collisions"].append({"part": name, "reference": other, "volume_mm3": vol})
    (OUT/"fit_report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    # Assembly with the measured PCB is supplied for inspection, not printing.
    check = cq.Assembly(name="BB2V1_fit_check")
    check.add(assy, name="enclosure")
    for i, (name, shape) in enumerate(reference):
        color = (.09,.42,.24) if "Board~" in name else (.38,.38,.4)
        check.add(shape, name=f"reference_{i:02d}", color=cq.Color(*color))
    check.add(screen, name="screen_measured_approximate", color=cq.Color(.82,.83,.78))
    check.export(str(OUT/"BB2V1_with_PCB_fit_check.step"))
    print(json.dumps({"body": report["body_without_lanyard"], "parts": list(parts), "collisions": report["collisions"]}, indent=2), flush=True)
    return parts, reference, screen, report


if __name__ == "__main__":
    build()

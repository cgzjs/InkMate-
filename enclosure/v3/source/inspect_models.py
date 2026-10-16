import json
from pathlib import Path

import cadquery as cq
from OCP.STEPCAFControl import STEPCAFControl_Reader
from OCP.TCollection import TCollection_ExtendedString
from OCP.TDataStd import TDataStd_Name
from OCP.TDF import TDF_Label, TDF_LabelSequence
from OCP.TDocStd import TDocStd_Document
from OCP.TopLoc import TopLoc_Location
from OCP.XCAFDoc import XCAFDoc_DocumentTool


def name_of(label):
    attr = TDataStd_Name()
    if label.FindAttribute(TDataStd_Name.GetID_s(), attr):
        return attr.Get().ToExtString()
    return "unnamed"


def read_parts(path):
    doc = TDocStd_Document(TCollection_ExtendedString("enclosure_reference"))
    reader = STEPCAFControl_Reader()
    reader.SetNameMode(True)
    reader.SetColorMode(True)
    reader.ReadFile(str(path))
    if not reader.Transfer(doc):
        raise RuntimeError(f"Unable to import {path}")
    tool = XCAFDoc_DocumentTool.ShapeTool_s(doc.Main())
    roots = TDF_LabelSequence()
    tool.GetFreeShapes(roots)
    parts = []

    def walk(label, parent_location, ancestry):
        location = parent_location.Multiplied(tool.GetLocation_s(label))
        target = label
        if tool.IsReference_s(label):
            target = TDF_Label()
            tool.GetReferredShape_s(label, target)
        name = name_of(target)
        children = TDF_LabelSequence()
        if tool.IsAssembly_s(target):
            tool.GetComponents_s(target, children)
        if children.Length():
            for i in range(1, children.Length() + 1):
                walk(children.Value(i), location, ancestry + [name])
        else:
            raw = tool.GetShape_s(target)
            if not raw.IsNull():
                parts.append((" / ".join(ancestry + [name]), cq.Shape.cast(raw.Moved(location))))

    for i in range(1, roots.Length() + 1):
        walk(roots.Value(i), TopLoc_Location(), [])
    return parts


def box_data(shape):
    b = shape.BoundingBox()
    return {"min": [b.xmin, b.ymin, b.zmin], "max": [b.xmax, b.ymax, b.zmax],
            "size": [b.xlen, b.ylen, b.zlen], "center": [b.center.x, b.center.y, b.center.z]}


def v2_reference_file(filename):
    root = Path(__file__).parent
    for directory in (root/"reference_v2",root.parent/"enclosure_BB_front_v2/deliverables"):
        path = directory/filename
        if path.exists():
            return path
    raise FileNotFoundError(f"Missing v2 comparison reference: {filename}")


if __name__ == "__main__":
    root = Path(__file__).parent
    params = json.loads((root / "parameters.json").read_text(encoding="utf-8"))
    results = {}
    for filename, key in (("BB2V1.step", "source"), ("BBV3.step", "snap_reference")):
        parts = read_parts(root / params[key])
        rows = [{"name": name, **box_data(shape), "solids": len(shape.Solids())}
                for name, shape in parts]
        results[filename] = rows
        print(filename, len(rows), "parts", flush=True)
        for row in rows:
            if filename == "BBV3.step" or any(key in row["name"] for key in ("Board", "SW", "USB", "FPC", "U1~", "L2~")):
                print(json.dumps(row, ensure_ascii=True), flush=True)
    Path(__file__).with_name("model_measurements.json").write_text(json.dumps(results, indent=2), encoding="utf-8")

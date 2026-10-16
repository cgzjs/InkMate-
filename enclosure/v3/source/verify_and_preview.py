"""Mesh validation and actual-geometry previews for the enclosure exports."""
import json
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
import trimesh
import cadquery as cq
import vtk
from vtk.util.numpy_support import numpy_to_vtk, numpy_to_vtkIdTypeArray

from inspect_models import read_parts, v2_reference_file

ROOT = Path(__file__).parent
OUT = ROOT / "deliverables"


def render_scene(scene, path, camera, focal, scale=48, view_up=(0,0,1)):
    renderer = vtk.vtkRenderer()
    renderer.SetBackground(244/255,246/255,245/255)
    renderer.UseFXAAOn()
    for mesh, color, offset in scene:
        points = vtk.vtkPoints()
        points.SetData(numpy_to_vtk(np.asarray(mesh.vertices),deep=True))
        cells = vtk.vtkCellArray()
        data = np.column_stack((np.full(len(mesh.faces),3),mesh.faces)).astype(np.int64).ravel()
        cells.SetCells(len(mesh.faces),numpy_to_vtkIdTypeArray(data,deep=True))
        poly = vtk.vtkPolyData()
        poly.SetPoints(points)
        poly.SetPolys(cells)
        normals = vtk.vtkPolyDataNormals()
        normals.SetInputData(poly)
        normals.SetFeatureAngle(40)
        normals.ConsistencyOn()
        mapper = vtk.vtkPolyDataMapper()
        mapper.SetInputConnection(normals.GetOutputPort())
        actor = vtk.vtkActor()
        actor.SetMapper(mapper)
        actor.SetPosition(*offset)
        prop = actor.GetProperty()
        prop.SetColor(*matplotlib.colors.to_rgb(color))
        prop.SetAmbient(.3)
        prop.SetDiffuse(.7)
        prop.SetSpecular(.15)
        prop.SetSpecularPower(22)
        renderer.AddActor(actor)
    cam = renderer.GetActiveCamera()
    cam.SetPosition(*camera)
    cam.SetFocalPoint(*focal)
    cam.SetViewUp(*view_up)
    cam.ParallelProjectionOn()
    cam.SetParallelScale(scale)
    renderer.ResetCameraClippingRange()
    window = vtk.vtkRenderWindow()
    window.SetOffScreenRendering(1)
    window.SetSize(1100,850)
    window.AddRenderer(renderer)
    window.Render()
    capture = vtk.vtkWindowToImageFilter()
    capture.SetInput(window)
    capture.ReadFrontBufferOff()
    capture.Update()
    writer = vtk.vtkPNGWriter()
    writer.SetFileName(str(path))
    writer.SetInputConnection(capture.GetOutputPort())
    writer.Write()
    window.Finalize()


def shape_mesh(shape):
    vertices, faces = shape.tessellate(.12,.18)
    return trimesh.Trimesh(vertices=[v.toTuple() for v in vertices], faces=faces, process=False)


def panel(ax, path, title):
    ax.imshow(plt.imread(path))
    ax.set_axis_off()
    ax.set_title(title,fontsize=14,color="#203232")


def main():
    report = json.loads((OUT/"fit_report.json").read_text())
    p = json.loads((ROOT/"parameters.json").read_text())
    meshes = {path.name.removesuffix("_assembly.stl"):trimesh.load_mesh(path)
              for path in OUT.glob("*_assembly.stl")}
    results = {}
    for path in OUT.glob("*_print.stl"):
        mesh = trimesh.load_mesh(path)
        results[path.name] = {"watertight":bool(mesh.is_watertight),
                              "winding_consistent":bool(mesh.is_winding_consistent),
                              "positive_volume":bool(mesh.volume > 0),
                              "connected_bodies":int(mesh.body_count),
                              "triangles":len(mesh.faces),
                              "bed_minimum_z":float(mesh.bounds[0,2])}
        assert mesh.is_watertight and mesh.is_winding_consistent and mesh.volume > 0, path.name
        assert mesh.body_count == 1, path.name
        assert abs(mesh.bounds[0,2]) < .01, path.name
    assert not report["collisions"], report["collisions"]
    results["cad_collision_count"] = len(report["collisions"])
    assert report["base_compatibility_with_v2"]["unchanged"]
    assert not report["button_fit"]["motion_non_actuator_collisions"]
    assert not report["screen_fit"]["screen_shift_collision_checks"]
    assert not report["screen_fit"]["fastener_clearance_collisions"]
    cover = cq.importers.importStep(str(OUT/"front_lid.step")).val()
    frame = cq.importers.importStep(str(OUT/"screen_retainer.step")).val()
    insertion_checks = []
    for distance in (0,.25,.5,1,2,3):
        overlap = frame.translate((0,0,-distance)).intersect(cover).Volume()
        assert overlap < .001, ("retainer insertion blocked",distance,overlap)
        insertion_checks.append({"distance_below_seated_position":distance,"overlap_volume_mm3":overlap})
    report["screen_fit"]["straight_insertion_checks"] = insertion_checks
    (OUT/"fit_report.json").write_text(json.dumps(report,indent=2))
    results["v2_base_unchanged"] = True
    results["button_motion_checks"] = "0..0.6 mm down, up to 0.4 mm lateral"
    results["screen_pocket_checks"] = "Screen translations up to 0.5 mm X/Y"
    results["retainer_insertion_checks"] = "0..3 mm straight approach, no square insertion slots"
    (OUT/"mesh_validation.json").write_text(json.dumps(results,indent=2))

    screen = report["screen_proxy"]
    screen_mesh = trimesh.creation.box(extents=screen["size"],
                                        transform=trimesh.transformations.translation_matrix(screen["center"]))
    active = trimesh.creation.box(extents=(48.5,23.7,.04),
                                   transform=trimesh.transformations.translation_matrix((35,32.2,screen["max"][2]+.02)))
    front_scene = [(meshes["front_lid"],"#c9d4d2",(0,0,0)),
                   (screen_mesh,"#8b9590",(0,0,0)), (active,"#dadfd5",(0,0,0))]
    for name, mesh in meshes.items():
        if name.startswith("button_"):
            front_scene.append((mesh,"#287568",(0,0,0)))
    render_scene([(meshes["base"],"#313d43",(0,0,0))]+front_scene,
                 OUT/"assembled.png",(-120,-145,120),(32,26,0),46)
    render_scene([(meshes["base"],"#313d43",(0,0,0))],
                 OUT/"lanyard_detail.png",(-42,12,26),(-7,44,-4),12)
    exploded_scene = [(meshes["base"],"#313d43",(0,0,0))]
    references = read_parts(ROOT / p["source"])
    for name, shape in references:
        if "Board~" in name or any(token in name for token in (" / SW", " / USB", " / U1~", " / L2~", " / FPC")):
            exploded_scene.append((shape_mesh(shape),"#38825a" if "Board~" in name else "#697171",(0,0,8)))
    exploded_scene.extend([(meshes["screen_retainer"],"#b78640",(0,0,16)),
                           (screen_mesh,"#8b9590",(0,0,20)),
                           (active,"#dadfd5",(0,0,20)),
                           (meshes["front_lid"],"#c9d4d2",(0,0,25))])
    for name, mesh in meshes.items():
        if name.startswith("button_"):
            exploded_scene.append((mesh,"#287568",(0,0,25)))
    render_scene(exploded_scene,OUT/"exploded.png",(145,-150,158),(40,26,15),50)
    fig, axes = plt.subplots(1,2,figsize=(16,8),facecolor="#f4f6f5")
    panel(axes[0],OUT/"assembled.png","ASSEMBLED / front buttons")
    panel(axes[1],OUT/"exploded.png","EXPLODED / measured PCB reference")
    w,h,d = report["body_without_lanyard"]
    fig.suptitle(f"BB2V1 THIN ENCLOSURE v3     {w:.1f} x {h:.1f} x {d:.1f} mm",fontsize=20,color="#203232",y=.96)
    fig.text(.5,.08,"8 BBV3 snap profiles  |  USB-C opening from BBV3  |  Screen: 59 x 30 x 1 mm (approx.)",ha="center",fontsize=12,color="#344847")
    fig.text(.5,.045,"60.2 x 31.2 mm screen pocket  |  Drop-in M1 retainer  |  Taller caps + larger recessed legends",ha="center",fontsize=11,color="#344847")
    fig.subplots_adjust(left=.01,right=.99,bottom=.10,top=.87,wspace=0)
    fig.savefig(OUT/"enclosure_preview.png",dpi=150)
    plt.close(fig)

    render_scene(front_scene,OUT/"front.png",(40,26,160),(40,26,0),36,(0,1,0))
    underside_scene = [(meshes["front_lid"],"#c9d4d2",(0,0,0)),
                       (meshes["screen_retainer"],"#b78640",(0,0,0))]
    for name,mesh in meshes.items():
        if name.startswith("button_"):
            underside_scene.append((mesh,"#287568",(0,0,0)))
    render_scene(underside_scene,OUT/"underside.png",(145,-150,-125),(40,26,5),46)
    fig, axes = plt.subplots(1,2,figsize=(14,7),facecolor="#f4f6f5")
    panel(axes[0],OUT/"front.png","FRONT / 55 x 26 mm screen window")
    panel(axes[1],OUT/"underside.png","LID UNDERSIDE / drop-in four-screw retainer")
    fig.text(.5,.07,"No square insertion slots. Screen gap: 0.6 mm per side. M1 plain pilots + lead-in mouths.",ha="center",fontsize=11,color="#344847")
    fig.subplots_adjust(left=.01,right=.99,bottom=.1,top=.9,wspace=0)
    fig.savefig(OUT/"front_and_retainer.png",dpi=150)
    plt.close(fig)
    render_scene([(meshes["front_lid"],"#c9d4d2",(0,0,0))],
                 OUT/"lid_screen_pocket.png",(90,-50,-80),(32,32,5),36)
    old = trimesh.load_mesh(v2_reference_file("button_SW4_assembly.stl"))
    new = meshes["button_SW4"]
    old = old.copy(); new = new.copy()
    old.apply_translation((-old.centroid[0]-7,-old.centroid[1],0))
    new.apply_translation((-new.centroid[0]+7,-new.centroid[1],0))
    render_scene([(old,"#909d9a",(0,0,0)),(new,"#287568",(0,0,0))],
                 OUT/"button_comparison.png",(16,-32,18),(0,0,7),12)
    old = trimesh.load_mesh(v2_reference_file("button_SW4_assembly.stl"))
    new = meshes["button_SW4"]
    x,y,z = report["button_centers"]["SW4"]
    face_z = report["parts"]["front_lid"]["max"][2]
    fig,axes = plt.subplots(1,2,figsize=(12,5),sharex=True,sharey=True,facecolor="#f4f6f5")
    for ax,mesh,color,title in ((axes[0],old,"#909d9a","v2 / original"),
                                (axes[1],new,"#287568","v3 / revised")):
        section = mesh.section(plane_origin=(x,y,z),plane_normal=(0,1,0))
        assert section is not None
        for contour in section.discrete:
            ax.fill(contour[:,0]-x,contour[:,2],color=color,edgecolor="#243735",linewidth=1)
        ax.axhline(face_z,color="#58798d",linestyle="--",linewidth=1)
        ax.axhline(z+.2,color="#a95a3e",linestyle=":",linewidth=1)
        ax.set(xlim=(-5,5),ylim=(z-1.3,z+3.3),title=title,xlabel="X from key center (mm)")
        ax.set_aspect("equal")
        ax.grid(alpha=.12)
    axes[0].set_ylabel("Assembly Z (mm)")
    fig.suptitle("ACTUAL KEY SECTIONS / contact height unchanged",fontsize=16,color="#203232")
    fig.text(.5,.035,"Skirt shorter by 0.3 mm  |  Top taller by 0.6 mm  |  Blue: faceplate  /  Orange: actuator-contact face",ha="center",fontsize=10)
    fig.tight_layout(rect=(0,.07,1,.94))
    fig.savefig(OUT/"button_section_comparison.png",dpi=160)
    plt.close(fig)
    print(json.dumps(results,indent=2),flush=True)


if __name__ == "__main__":
    main()

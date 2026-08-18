#ifndef __MESH_VIEW_H__
#define __MESH_VIEW_H__

#include "lvgl/lvgl.h"
#include "event_guard.h"

#include <vector>

// Draws a Klipper bed mesh as a shaded surface you can drag to rotate, or as a
// flat interpolated heatmap, both on one diverging colour scale with a legend
// down the left edge.
//
// This is the first use of lv_canvas in the project, so the reason: the mesh
// has to be drawn as a surface and neither lv_table nor lv_chart can do that.
// The canvas also hands us a plain lv_color_t buffer, which the flat view
// writes into directly rather than paying lv_canvas_draw_*'s per-call draw
// context allocation once per pixel.
class MeshView {
 public:
  enum class Mode { Surface, Flat };

  MeshView(lv_obj_t *parent, lv_coord_t w, lv_coord_t h);
  ~MeshView();

  lv_obj_t *get_canvas();

  // Replace the mesh and redraw. An empty matrix clears the view.
  void set_mesh(const std::vector<std::vector<double>> &m);
  void clear();

  // The bed area the mesh covers, in millimetres, used to label the corners of
  // the surface view. Stored rather than drawn: every path that changes the
  // extents goes on to replace the mesh, and set_mesh renders, so rendering
  // here as well would only draw the same frame twice.
  void set_extents(double min_x, double min_y, double max_x, double max_y);

  void set_mode(Mode m);
  Mode get_mode() const { return mode; }

  bool has_mesh() const { return !mesh.empty() && !mesh[0].empty(); }
  double min_z() const { return zmin; }
  double max_z() const { return zmax; }

  void handle_drag(lv_event_t *e);

  static void _handle_drag(lv_event_t *e) {
    KGuard::event("MeshView::_handle_drag", [&] {
      static_cast<MeshView *>(e->user_data)->handle_drag(e);
    });
  };

  // Deleting the panel's root container deletes the canvas with it, and that
  // can happen before this object is destroyed. Without this the destructor
  // would delete an object LVGL has already freed.
  static void _handle_delete(lv_event_t *e) {
    KGuard::event("MeshView::_handle_delete", [&] {
      static_cast<MeshView *>(e->user_data)->canvas = NULL;
    });
  };

 private:
  // A mesh point after projection: pixel position, plus depth toward the
  // camera so quads can be drawn back to front.
  struct Projected {
    double x;
    double y;
    double depth;
  };

  // The orthographic camera, plus the scale and offset chosen to fit the
  // projected mesh into the plot rect at the current angles.
  struct Camera {
    double sin_yaw;
    double cos_yaw;
    double sin_elev;
    double cos_elev;
    double scale;
    double off_x;
    double off_y;
  };

  void render();
  void render_surface();
  void render_flat();
  void render_legend();
  // Bed coordinates on the corners of the z=0 plane, so a rotated surface can
  // still be matched to the machine.
  void render_axes(const Camera &cam);
  void fill_background();

  // A degenerate rectangle means the extents are unknown, which is what a mesh
  // whose bed_mesh update carried no mesh_min leaves behind.
  bool has_extents() const { return ext_max_x > ext_min_x && ext_max_y > ext_min_y; }
  // Whichever ink contrasts with the canvas background, which follows the
  // theme and so cannot be assumed dark.
  lv_color_t ink() const;

  // Projects every mesh point and returns the camera that was fitted to them,
  // so anything else drawn in the same space can reuse it.
  Camera project(std::vector<Projected> &out) const;
  static lv_point_t to_screen(const Camera &cam, double wx, double wy, double wz);

  lv_color_t color_at(double z) const;
  lv_color_t shaded_color_at(double z, double shade) const;
  void set_px(lv_coord_t x, lv_coord_t y, lv_color_t c);

  lv_obj_t *canvas;
  lv_color_t *buf;
  lv_coord_t canvas_w;
  lv_coord_t canvas_h;
  // Plot rectangle inside the canvas, to the right of the colour scale.
  lv_coord_t plot_x0;
  lv_coord_t plot_y0;
  lv_coord_t plot_x1;
  lv_coord_t plot_y1;
  lv_color_t bg;

  std::vector<std::vector<double>> mesh;
  // The bed area the mesh spans, in millimetres. Row 0 column 0 of the matrix
  // is the min x, min y corner.
  double ext_min_x;
  double ext_min_y;
  double ext_max_x;
  double ext_max_y;
  double zmin;
  double zmax;
  // Half width of the colour scale, max(|zmin|, |zmax|). Zero for a mesh of
  // all zeroes, which every scaling division has to guard against.
  double zref;

  Mode mode;
  double yaw;
  double elev;
  uint32_t last_render;
};

#endif // __MESH_VIEW_H__

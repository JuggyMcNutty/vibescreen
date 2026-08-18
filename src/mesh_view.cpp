#include "mesh_view.h"
#include "spdlog/spdlog.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

// ColorBrewer RdYlBu reversed, the eleven stop ramp fluidd uses for its bed
// mesh surface. Diverging, so blue reads as below the plane and red as above
// once the scale is centred on zero.
static const uint8_t PALETTE[][3] = {
  {0x31, 0x36, 0x95}, {0x45, 0x75, 0xb4}, {0x74, 0xad, 0xd1}, {0xab, 0xd9, 0xe9},
  {0xe0, 0xf3, 0xf8}, {0xff, 0xff, 0xbf}, {0xfe, 0xe0, 0x90}, {0xfd, 0xae, 0x61},
  {0xf4, 0x6d, 0x43}, {0xd7, 0x30, 0x27}, {0xa5, 0x00, 0x26},
};
static constexpr int PALETTE_N = 11;

// Width reserved on the left of the canvas for the colour scale and its
// labels. The bar itself sits at BAR_X0..BAR_X1 and the labels to its right.
static constexpr lv_coord_t LEGEND_W = 56;
static constexpr lv_coord_t BAR_X0 = 2;
static constexpr lv_coord_t BAR_X1 = 16;

// Vertical extent of the surface in world units, against 2.0 across in x and
// y. Heights are divided by zref first, so the relief fills this much whatever
// the mesh range.
static constexpr double HEIGHT_SPAN = 0.35;

// Floor under the colour scale's half range, in mm. Both the colour and the
// height are normalised by zref, so without this a bed that is flat to within
// probe noise gets 0.008mm stretched across the full palette and the full
// relief, and reads as dramatically worse than a bed that is off by half a
// millimetre. A bed inside +/-0.05mm is trammed, and should look it.
static constexpr double MIN_SCALE_HALF_RANGE = 0.05;

// Outlining every quad costs one more canvas draw call each, so it is only
// worth it while the grid is coarse enough for the lines to mean something. A
// dense interpolated mesh reads as a surface from the shading alone.
static constexpr size_t MAX_OUTLINED_QUADS = 120;
static constexpr size_t MAX_LABELLED_POINTS = 49;

static void palette_rgb(double t, int rgb[3]) {
  if (t < 0.0) t = 0.0;
  if (t > 1.0) t = 1.0;
  double p = t * (PALETTE_N - 1);
  int i = static_cast<int>(p);
  if (i > PALETTE_N - 2) i = PALETTE_N - 2;
  double f = p - i;
  for (int k = 0; k < 3; k++) {
    int a = PALETTE[i][k];
    int b = PALETTE[i + 1][k];
    rgb[k] = static_cast<int>(a + (b - a) * f + 0.5);
  }
}

static lv_color_t palette_at(double t) {
  int rgb[3];
  palette_rgb(t, rgb);
  return lv_color_make(rgb[0], rgb[1], rgb[2]);
}

// `lv_canvas_draw_polygon` only supports convex polygons, and does not merely
// draw a non-convex one badly: `lv_draw_sw_polygon`'s mask loop advances
// neither of its two chains, `mask_cnt` never reaches `point_cnt`, and it
// spins forever with the UI lock held. That is the unrecoverable failure this
// project goes to some length to avoid, so it has to be impossible rather than
// unlikely.
//
// A heightfield quad projects non-convex whenever the relief across one cell
// is large next to that cell's footprint on screen, which a near flat mesh
// produces as soon as the height is normalised. Rounding to whole pixels can
// tip a marginal one over as well, so test the integers actually being handed
// over. Callers split a failing quad into triangles, which are convex by
// construction.
static bool is_convex_quad(const lv_point_t p[4]) {
  int sign = 0;
  for (int i = 0; i < 4; i++) {
    const lv_point_t &a = p[i];
    const lv_point_t &b = p[(i + 1) % 4];
    const lv_point_t &c = p[(i + 2) % 4];
    const int32_t cross = (int32_t)(b.x - a.x) * (int32_t)(c.y - b.y) -
                          (int32_t)(b.y - a.y) * (int32_t)(c.x - b.x);
    if (cross == 0) {
      continue;  // collinear corner, still convex
    }
    const int s = cross > 0 ? 1 : -1;
    if (sign == 0) {
      sign = s;
    } else if (s != sign) {
      return false;
    }
  }
  return true;
}

MeshView::MeshView(lv_obj_t *parent, lv_coord_t w, lv_coord_t h)
  : canvas(lv_canvas_create(parent))
  , buf(NULL)
  , canvas_w(w)
  , canvas_h(h)
  , plot_x0(LEGEND_W)
  , plot_y0(2)
  , plot_x1(w - 3)
  , plot_y1(h - 3)
  , bg(lv_color_black())
  , ext_min_x(0.0)
  , ext_min_y(0.0)
  , ext_max_x(0.0)
  , ext_max_y(0.0)
  , zmin(0.0)
  , zmax(0.0)
  , zref(0.0)
  , mode(Mode::Surface)
  // Looking at the bed from its front left corner, which is where you stand.
  // 225 degrees of yaw puts min x, min y nearest the viewer, and 32 degrees of
  // elevation gives the three quarter view without flattening the relief.
  , yaw(225.0 * M_PI / 180.0)
  , elev(32.0 * M_PI / 180.0)
  , last_render(0)
{
  buf = static_cast<lv_color_t *>(
      malloc(static_cast<size_t>(w) * static_cast<size_t>(h) * sizeof(lv_color_t)));
  if (buf == NULL) {
    spdlog::error("mesh view could not allocate a {}x{} canvas buffer", w, h);
    return;
  }

  // TRUE_COLOR and not TRUE_COLOR_ALPHA on purpose: the alpha format installs
  // a set_px_cb, which drops every blend off lv_draw_sw's fast path.
  lv_canvas_set_buffer(canvas, buf, w, h, LV_IMG_CF_TRUE_COLOR);
  lv_obj_set_size(canvas, w, h);
  lv_obj_clear_flag(canvas, LV_OBJ_FLAG_SCROLLABLE);
  // lv_canvas derives from lv_img, which is not clickable, so dragging to
  // rotate needs this turned on explicitly.
  lv_obj_add_flag(canvas, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(canvas, &MeshView::_handle_drag, LV_EVENT_PRESSING, this);
  lv_obj_add_event_cb(canvas, &MeshView::_handle_drag, LV_EVENT_RELEASED, this);
  lv_obj_add_event_cb(canvas, &MeshView::_handle_delete, LV_EVENT_DELETE, this);

  fill_background();
  lv_obj_invalidate(canvas);
}

MeshView::~MeshView() {
  if (canvas != NULL) {
    lv_obj_del(canvas);
    canvas = NULL;
  }
  // lv_canvas_set_buffer does not take ownership, so the buffer outlives the
  // widget and has to be released here. After the delete, so nothing can draw
  // into freed memory.
  if (buf != NULL) {
    free(buf);
    buf = NULL;
  }
}

lv_obj_t *MeshView::get_canvas() {
  return canvas;
}

void MeshView::set_px(lv_coord_t x, lv_coord_t y, lv_color_t c) {
  buf[static_cast<size_t>(y) * canvas_w + x] = c;
}

lv_color_t MeshView::color_at(double z) const {
  return palette_at(zref > 0.0 ? (z + zref) / (2.0 * zref) : 0.5);
}

lv_color_t MeshView::shaded_color_at(double z, double shade) const {
  int rgb[3];
  palette_rgb(zref > 0.0 ? (z + zref) / (2.0 * zref) : 0.5, rgb);
  for (int k = 0; k < 3; k++) {
    int v = static_cast<int>(rgb[k] * shade + 0.5);
    rgb[k] = v < 0 ? 0 : (v > 255 ? 255 : v);
  }
  return lv_color_make(rgb[0], rgb[1], rgb[2]);
}

void MeshView::set_mesh(const std::vector<std::vector<double>> &m) {
  mesh.clear();
  // Drop ragged or empty rows rather than trusting the matrix, since it comes
  // straight off the websocket and an index error here would unwind into LVGL.
  if (!m.empty() && !m[0].empty()) {
    const size_t cols = m[0].size();
    for (const auto &row : m) {
      if (row.size() != cols) {
        spdlog::warn("mesh view got a ragged matrix, ignoring it");
        mesh.clear();
        break;
      }
      mesh.push_back(row);
    }
  }

  zmin = 0.0;
  zmax = 0.0;
  zref = 0.0;
  if (has_mesh()) {
    zmin = zmax = mesh[0][0];
    for (const auto &row : mesh) {
      for (double v : row) {
        zmin = std::min(zmin, v);
        zmax = std::max(zmax, v);
      }
    }
    zref = std::max({std::abs(zmin), std::abs(zmax), MIN_SCALE_HALF_RANGE});
  }

  render();
}

void MeshView::clear() {
  mesh.clear();
  zmin = zmax = zref = 0.0;
  // Or the next mesh to arrive without extents would be labelled with the
  // cleared one's bed area.
  ext_min_x = ext_min_y = ext_max_x = ext_max_y = 0.0;
  render();
}

void MeshView::set_extents(double min_x, double min_y, double max_x, double max_y) {
  ext_min_x = min_x;
  ext_min_y = min_y;
  ext_max_x = max_x;
  ext_max_y = max_y;
}

void MeshView::set_mode(Mode m) {
  if (m == mode) {
    return;
  }
  mode = m;
  render();
}

lv_color_t MeshView::ink() const {
  return lv_color_brightness(bg) > 140 ? lv_color_black() : lv_color_white();
}

void MeshView::fill_background() {
  // Read it per render rather than caching it, so the canvas keeps matching
  // the container it sits in even if the theme changes underneath.
  lv_obj_t *parent = lv_obj_get_parent(canvas);
  bg = lv_obj_get_style_bg_color(parent != NULL ? parent : canvas, LV_PART_MAIN);
  std::fill_n(buf, static_cast<size_t>(canvas_w) * canvas_h, bg);
}

void MeshView::render() {
  if (buf == NULL || canvas == NULL) {
    return;
  }

  uint32_t started = lv_tick_get();

  // Every lv_canvas_draw_* call ends in lv_obj_invalidate, and a 16x16 mesh is
  // 225 quads, so a frame would queue hundreds of overlapping invalid areas
  // for one buffer we are going to redraw whole anyway. Hiding the canvas
  // makes lv_obj_area_is_visible reject them immediately; clearing the flag
  // afterwards queues the single invalidation that is actually wanted.
  //
  // Nothing can repaint in between: render only ever runs on the LVGL thread,
  // or under lv_lock with that thread parked in the main loop.
  const bool was_hidden = lv_obj_has_flag(canvas, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(canvas, LV_OBJ_FLAG_HIDDEN);

  fill_background();
  if (has_mesh()) {
    render_legend();
    if (mode == Mode::Surface) {
      render_surface();
    } else {
      render_flat();
    }
  }

  if (!was_hidden) {
    lv_obj_clear_flag(canvas, LV_OBJ_FLAG_HIDDEN);
  }
  last_render = lv_tick_get();
  spdlog::trace("mesh view rendered {}x{} in {} ms", mesh.size(),
                mesh.empty() ? 0 : mesh[0].size(), lv_tick_elaps(started));
}

void MeshView::render_legend() {
  const double span = plot_y1 - plot_y0;
  if (span <= 0.0) {
    return;
  }

  for (lv_coord_t y = plot_y0; y <= plot_y1; y++) {
    lv_color_t c = palette_at((plot_y1 - y) / span);
    for (lv_coord_t x = BAR_X0; x <= BAR_X1; x++) {
      set_px(x, y, c);
    }
  }

  lv_draw_label_dsc_t dsc;
  lv_draw_label_dsc_init(&dsc);
  dsc.font = &lv_font_montserrat_8;
  dsc.color = ink();

  // The scale is symmetric, so its ends are the reference and its middle is
  // always zero. Labelling all three makes both facts visible.
  const lv_coord_t label_x = BAR_X1 + 3;
  const lv_coord_t label_w = LEGEND_W - label_x - 2;
  char txt[16];

  snprintf(txt, sizeof(txt), "+%.3f", zref);
  lv_canvas_draw_text(canvas, label_x, plot_y0, label_w, &dsc, txt);

  lv_canvas_draw_text(canvas, label_x, (plot_y0 + plot_y1) / 2 - 5, label_w, &dsc, "0");

  snprintf(txt, sizeof(txt), "-%.3f", zref);
  lv_canvas_draw_text(canvas, label_x, plot_y1 - 10, label_w, &dsc, txt);
}

lv_point_t MeshView::to_screen(const Camera &cam, double wx, double wy, double wz) {
  const double xa = -wx * cam.sin_yaw + wy * cam.cos_yaw;
  const double ya = wx * cam.cos_yaw + wy * cam.sin_yaw;
  lv_point_t out;
  out.x = static_cast<lv_coord_t>(std::lround(cam.off_x + xa * cam.scale));
  out.y = static_cast<lv_coord_t>(
      std::lround(cam.off_y + (ya * cam.sin_elev - wz * cam.cos_elev) * cam.scale));
  return out;
}

MeshView::Camera MeshView::project(std::vector<Projected> &out) const {
  const size_t rows = mesh.size();
  const size_t cols = mesh[0].size();

  Camera cam;
  cam.sin_yaw = std::sin(yaw);
  cam.cos_yaw = std::cos(yaw);
  cam.sin_elev = std::sin(elev);
  cam.cos_elev = std::cos(elev);

  out.resize(rows * cols);
  double bx0 = 1e30, bx1 = -1e30, by0 = 1e30, by1 = -1e30;

  for (size_t r = 0; r < rows; r++) {
    const double wy = rows > 1 ? -1.0 + 2.0 * r / (rows - 1) : 0.0;
    for (size_t c = 0; c < cols; c++) {
      const double wx = cols > 1 ? -1.0 + 2.0 * c / (cols - 1) : 0.0;
      const double wz = zref > 0.0 ? mesh[r][c] / zref * HEIGHT_SPAN : 0.0;

      // Orthographic camera at azimuth yaw, elevation elev. xa runs along the
      // screen's right axis, ya into the screen before the elevation squash.
      const double xa = -wx * cam.sin_yaw + wy * cam.cos_yaw;
      const double ya = wx * cam.cos_yaw + wy * cam.sin_yaw;

      Projected &p = out[r * cols + c];
      p.x = xa;
      p.y = ya * cam.sin_elev - wz * cam.cos_elev;
      p.depth = ya * cam.cos_elev + wz * cam.sin_elev;

      bx0 = std::min(bx0, p.x);
      bx1 = std::max(bx1, p.x);
      by0 = std::min(by0, p.y);
      by1 = std::max(by1, p.y);
    }
  }

  // Fit whatever the current angles produced into the plot rect, so rotating
  // never runs the surface off the canvas or leaves it swimming in space.
  const double pw = plot_x1 - plot_x0;
  const double ph = plot_y1 - plot_y0;
  cam.scale = std::min(pw / std::max(bx1 - bx0, 1e-6),
                       ph / std::max(by1 - by0, 1e-6)) * 0.94;
  cam.off_x = (plot_x0 + plot_x1) / 2.0 - (bx0 + bx1) / 2.0 * cam.scale;
  cam.off_y = (plot_y0 + plot_y1) / 2.0 - (by0 + by1) / 2.0 * cam.scale;

  for (auto &p : out) {
    p.x = cam.off_x + p.x * cam.scale;
    p.y = cam.off_y + p.y * cam.scale;
  }
  return cam;
}

void MeshView::render_surface() {
  const size_t rows = mesh.size();
  const size_t cols = mesh[0].size();
  if (rows < 2 || cols < 2) {
    // A single row or column has no quads to fill. Fall back to the flat view
    // rather than drawing nothing.
    render_flat();
    return;
  }

  std::vector<Projected> pts;
  const Camera cam = project(pts);

  auto point_at = [&](size_t r, size_t c) {
    const Projected &p = pts[r * cols + c];
    lv_point_t out;
    out.x = static_cast<lv_coord_t>(std::lround(p.x));
    out.y = static_cast<lv_coord_t>(std::lround(p.y));
    return out;
  };

  // Quads back to front. A heightfield can be drawn in grid order most of the
  // time, but not once the relief is steep enough to overlap, and sorting a
  // couple of hundred keys costs nothing next to the fills.
  struct Quad {
    size_t r;
    size_t c;
    double depth;
  };
  std::vector<Quad> quads;
  quads.reserve((rows - 1) * (cols - 1));
  for (size_t r = 0; r + 1 < rows; r++) {
    for (size_t c = 0; c + 1 < cols; c++) {
      const double d = (pts[r * cols + c].depth + pts[r * cols + c + 1].depth +
                        pts[(r + 1) * cols + c].depth + pts[(r + 1) * cols + c + 1].depth) / 4.0;
      quads.push_back({r, c, d});
    }
  }
  std::sort(quads.begin(), quads.end(),
            [](const Quad &a, const Quad &b) { return a.depth < b.depth; });

  lv_draw_line_dsc_t frame_dsc;
  lv_draw_line_dsc_init(&frame_dsc);
  frame_dsc.color = lv_palette_darken(LV_PALETTE_GREY, 2);
  frame_dsc.width = 1;

  // Outline of the z=0 plane, drawn before the surface so the parts of it the
  // surface covers stay hidden. Gives the relief something to be read against.
  {
    static const double corner_x[4] = {-1.0, 1.0, 1.0, -1.0};
    static const double corner_y[4] = {-1.0, -1.0, 1.0, 1.0};
    lv_point_t frame[5];
    for (int i = 0; i < 4; i++) {
      frame[i] = to_screen(cam, corner_x[i], corner_y[i], 0.0);
    }
    frame[4] = frame[0];
    lv_canvas_draw_line(canvas, frame, 5, &frame_dsc);
  }

  lv_draw_rect_dsc_t fill_dsc;
  lv_draw_rect_dsc_init(&fill_dsc);
  fill_dsc.bg_opa = LV_OPA_COVER;
  fill_dsc.radius = 0;

  lv_draw_line_dsc_t edge_dsc;
  lv_draw_line_dsc_init(&edge_dsc);
  edge_dsc.width = 1;

  const bool outline = quads.size() <= MAX_OUTLINED_QUADS;

  // Fixed light, so rotating the mesh moves the highlight across it the way a
  // real surface under a real lamp would, which is what makes the shape read.
  const double lx = -0.45, ly = -0.35, lz = 0.82;
  const double dx = 2.0 / (cols - 1);
  const double dy = 2.0 / (rows - 1);
  const double hscale = zref > 0.0 ? HEIGHT_SPAN / zref : 0.0;

  for (const Quad &q : quads) {
    const double z00 = mesh[q.r][q.c];
    const double z01 = mesh[q.r][q.c + 1];
    const double z10 = mesh[q.r + 1][q.c];
    const double z11 = mesh[q.r + 1][q.c + 1];

    const double dzdx = ((z01 + z11) - (z00 + z10)) * hscale / (2.0 * dx);
    const double dzdy = ((z10 + z11) - (z00 + z01)) * hscale / (2.0 * dy);
    const double nlen = std::sqrt(dzdx * dzdx + dzdy * dzdy + 1.0);
    const double ndotl = (-dzdx * lx - dzdy * ly + lz) / nlen;
    const double shade = 0.72 + 0.28 * std::max(0.0, ndotl);

    const double zavg = (z00 + z01 + z10 + z11) / 4.0;
    lv_color_t c = shaded_color_at(zavg, shade);

    lv_point_t poly[5];
    poly[0] = point_at(q.r, q.c);
    poly[1] = point_at(q.r, q.c + 1);
    poly[2] = point_at(q.r + 1, q.c + 1);
    poly[3] = point_at(q.r + 1, q.c);

    fill_dsc.bg_color = c;
    if (is_convex_quad(poly)) {
      lv_canvas_draw_polygon(canvas, poly, 4, &fill_dsc);
    } else {
      lv_point_t tri[3] = {poly[0], poly[1], poly[2]};
      lv_canvas_draw_polygon(canvas, tri, 3, &fill_dsc);
      tri[1] = poly[2];
      tri[2] = poly[3];
      lv_canvas_draw_polygon(canvas, tri, 3, &fill_dsc);
    }

    if (outline) {
      poly[4] = poly[0];
      edge_dsc.color = shaded_color_at(zavg, shade * 0.6);
      lv_canvas_draw_line(canvas, poly, 5, &edge_dsc);
    }
  }

  // Last, so nothing is drawn over the labels.
  render_axes(cam);
}

// Which corner of the render is which corner of the bed, which the surface
// alone cannot say and dragging it makes worse. Three corners of the z=0 plane
// get labelled: the one nearest the viewer with both its coordinates, and the
// two either side of it with the axis running along the edge between. The
// fourth is the far one, behind the surface, where a label would read as
// belonging to whatever it landed on.
void MeshView::render_axes(const Camera &cam) {
  // The same corners in the same order as the frame drawn above.
  static const double corner_x[4] = {-1.0, 1.0, 1.0, -1.0};
  static const double corner_y[4] = {-1.0, -1.0, 1.0, 1.0};

  // Nearest corner by the depth the quads are sorted on, evaluated at z=0.
  // Whichever it turns out to be, its neighbours in this order are the far
  // ends of the two edges meeting there, and each differs from it in exactly
  // one axis: the axis that edge runs along.
  int near_i = 0;
  double nearest = -1e30;
  for (int i = 0; i < 4; i++) {
    const double d =
      (corner_x[i] * cam.cos_yaw + corner_y[i] * cam.sin_yaw) * cam.cos_elev;
    if (d > nearest) {
      nearest = d;
      near_i = i;
    }
  }

  lv_draw_label_dsc_t dsc;
  lv_draw_label_dsc_init(&dsc);
  dsc.font = &lv_font_montserrat_8;
  dsc.color = ink();

  // Tipped towards plan view the plane fills the plot and there is no longer
  // an outside to put a label in, so each one carries a little of the
  // background with it. Otherwise white text lands on the pale end of the
  // colour scale and disappears.
  lv_draw_rect_dsc_t chip_dsc;
  lv_draw_rect_dsc_init(&chip_dsc);
  chip_dsc.bg_color = bg;
  chip_dsc.bg_opa = LV_OPA_70;
  chip_dsc.radius = 2;

  const lv_point_t mid = to_screen(cam, 0.0, 0.0, 0.0);

  auto draw_at = [&](int i, const char *txt) {
    lv_point_t size;
    lv_txt_get_size(&size, txt, dsc.font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);

    const lv_point_t p = to_screen(cam, corner_x[i], corner_y[i], 0.0);
    const double dx = p.x - mid.x;
    const double dy = p.y - mid.y;
    const double len = std::sqrt(dx * dx + dy * dy);
    const double push = len > 1.0 ? 8.0 / len : 0.0;
    // Away from the plane, then down. All three labelled corners are in the
    // lower half of the projection, so down is further out for each of them,
    // and the camera only leaves a few pixels of horizontal slack to work
    // with while there is always room below.
    lv_coord_t x = static_cast<lv_coord_t>(std::lround(p.x + dx * push)) - size.x / 2;
    lv_coord_t y = static_cast<lv_coord_t>(std::lround(p.y + dy * push + 10.0)) - size.y / 2;
    // Measured rather than guessed at, so this keeps the text itself inside
    // the plot and off the colour scale.
    x = std::max<lv_coord_t>(plot_x0, std::min<lv_coord_t>(x, plot_x1 - size.x));
    y = std::max<lv_coord_t>(plot_y0, std::min<lv_coord_t>(y, plot_y1 - size.y));
    lv_canvas_draw_rect(canvas, x - 2, y - 1, size.x + 4, size.y + 2, &chip_dsc);
    lv_canvas_draw_text(canvas, x, y, size.x, &dsc, txt);
  };

  if (!has_extents()) {
    // No mesh_min from the printer. Which way the axes run is still worth
    // having, even without the millimetres.
    for (int step : {1, 3}) {
      const int i = (near_i + step) % 4;
      draw_at(i, corner_x[i] != corner_x[near_i] ? "X" : "Y");
    }
    return;
  }

  auto mm_x = [&](int i) {
    return ext_min_x + (corner_x[i] + 1.0) / 2.0 * (ext_max_x - ext_min_x);
  };
  auto mm_y = [&](int i) {
    return ext_min_y + (corner_y[i] + 1.0) / 2.0 * (ext_max_y - ext_min_y);
  };

  // Whole millimetres. A tenth of a millimetre says nothing about which way
  // round the bed is and costs the width of two more digits.
  char txt[24];
  snprintf(txt, sizeof(txt), "%.0f, %.0f", mm_x(near_i), mm_y(near_i));
  draw_at(near_i, txt);

  for (int step : {1, 3}) {
    const int i = (near_i + step) % 4;
    if (corner_x[i] != corner_x[near_i]) {
      snprintf(txt, sizeof(txt), "X %.0f", mm_x(i));
    } else {
      snprintf(txt, sizeof(txt), "Y %.0f", mm_y(i));
    }
    draw_at(i, txt);
  }
}

void MeshView::render_flat() {
  const size_t rows = mesh.size();
  const size_t cols = mesh[0].size();
  const lv_coord_t pw = plot_x1 - plot_x0;
  const lv_coord_t ph = plot_y1 - plot_y0;
  if (pw < 2 || ph < 2) {
    return;
  }

  // Column mapping is the same for every scanline, so work it out once.
  std::vector<size_t> col_idx(pw + 1);
  std::vector<double> col_frac(pw + 1);
  for (lv_coord_t i = 0; i <= pw; i++) {
    const double f = cols > 1 ? static_cast<double>(i) / pw * (cols - 1) : 0.0;
    size_t c0 = static_cast<size_t>(f);
    if (cols > 1 && c0 > cols - 2) {
      c0 = cols - 2;
    }
    col_idx[i] = c0;
    col_frac[i] = f - c0;
  }

  for (lv_coord_t j = 0; j <= ph; j++) {
    // Row 0 of a Klipper matrix is at min y, and min y belongs at the bottom.
    const double f = rows > 1 ? static_cast<double>(ph - j) / ph * (rows - 1) : 0.0;
    size_t r0 = static_cast<size_t>(f);
    if (rows > 1 && r0 > rows - 2) {
      r0 = rows - 2;
    }
    const double tr = f - r0;
    const std::vector<double> &row_a = mesh[r0];
    const std::vector<double> &row_b = mesh[rows > 1 ? r0 + 1 : r0];

    for (lv_coord_t i = 0; i <= pw; i++) {
      const size_t c0 = col_idx[i];
      const double tc = col_frac[i];
      const size_t c1 = cols > 1 ? c0 + 1 : c0;
      const double top = row_a[c0] * (1.0 - tc) + row_a[c1] * tc;
      const double bot = row_b[c0] * (1.0 - tc) + row_b[c1] * tc;
      set_px(plot_x0 + i, plot_y0 + j, color_at(top * (1.0 - tr) + bot * tr));
    }
  }

  if (rows * cols > MAX_LABELLED_POINTS) {
    return;
  }

  // Grid on the probe lines, then the value at each point. Only worth doing
  // while the points are far enough apart for the numbers to fit.
  lv_draw_line_dsc_t grid_dsc;
  lv_draw_line_dsc_init(&grid_dsc);
  grid_dsc.color = lv_color_black();
  grid_dsc.opa = LV_OPA_30;
  grid_dsc.width = 1;

  auto point_x = [&](size_t c) {
    return static_cast<lv_coord_t>(plot_x0 + (cols > 1 ? pw * c / (cols - 1) : pw / 2));
  };
  auto point_y = [&](size_t r) {
    return static_cast<lv_coord_t>(plot_y1 - (rows > 1 ? ph * r / (rows - 1) : ph / 2));
  };

  lv_point_t seg[2];
  for (size_t c = 0; c < cols; c++) {
    seg[0] = {point_x(c), plot_y0};
    seg[1] = {point_x(c), plot_y1};
    lv_canvas_draw_line(canvas, seg, 2, &grid_dsc);
  }
  for (size_t r = 0; r < rows; r++) {
    seg[0] = {plot_x0, point_y(r)};
    seg[1] = {plot_x1, point_y(r)};
    lv_canvas_draw_line(canvas, seg, 2, &grid_dsc);
  }

  lv_draw_label_dsc_t dsc;
  lv_draw_label_dsc_init(&dsc);
  dsc.font = &lv_font_montserrat_8;
  dsc.align = LV_TEXT_ALIGN_CENTER;

  const lv_coord_t label_w = 34;
  // Font 8 has a line height of 10. Points on the outer rows and columns sit
  // on the plot edge, so their labels have to be pulled back inside it.
  const lv_coord_t label_h = 10;
  char txt[16];
  for (size_t r = 0; r < rows; r++) {
    for (size_t c = 0; c < cols; c++) {
      const double z = mesh[r][c];
      // Pale bands need dark text and dark bands need light, or half the
      // numbers vanish into the surface they are labelling.
      dsc.color = lv_color_brightness(color_at(z)) > 140
          ? lv_color_black() : lv_color_white();
      snprintf(txt, sizeof(txt), "%.3f", z);
      lv_coord_t x = point_x(c) - label_w / 2;
      lv_coord_t y = point_y(r) - label_h / 2;
      x = std::max<lv_coord_t>(plot_x0, std::min<lv_coord_t>(x, plot_x1 - label_w));
      y = std::max<lv_coord_t>(plot_y0, std::min<lv_coord_t>(y, plot_y1 - label_h));
      lv_canvas_draw_text(canvas, x, y, label_w, &dsc, txt);
    }
  }
}

void MeshView::handle_drag(lv_event_t *e) {
  const lv_event_code_t code = lv_event_get_code(e);
  if (mode != Mode::Surface || !has_mesh()) {
    return;
  }

  if (code == LV_EVENT_RELEASED) {
    // The last move before the finger lifts is usually thrown away by the
    // throttle below, so settle on the final angles here.
    render();
    return;
  }

  if (code != LV_EVENT_PRESSING) {
    return;
  }

  lv_indev_t *indev = lv_indev_get_act();
  if (indev == NULL) {
    return;
  }
  lv_point_t v;
  lv_indev_get_vect(indev, &v);
  if (v.x == 0 && v.y == 0) {
    return;
  }

  // Turntable feel: the surface follows the finger. Dragging right swings the
  // near face right, dragging down tips the view towards looking straight
  // down.
  yaw -= v.x * 0.012;
  elev += v.y * 0.012;
  // Stay off both poles. Near zero the surface collapses to a line, and near
  // ninety the relief disappears into a flat plan view, so neither end is a
  // place the user can usefully end up.
  elev = std::max(15.0 * M_PI / 180.0, std::min(85.0 * M_PI / 180.0, elev));

  // Angles accumulate whether or not this event redraws, so a throttled move
  // is not a lost one. There is no point rendering faster than the display
  // refreshes.
  if (lv_tick_elaps(last_render) < LV_DISP_DEF_REFR_PERIOD) {
    return;
  }
  render();
}

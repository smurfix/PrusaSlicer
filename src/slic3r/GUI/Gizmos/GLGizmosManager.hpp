#ifndef slic3r_GUI_GLGizmosManager_hpp_
#define slic3r_GUI_GLGizmosManager_hpp_

#include "slic3r/GUI/GLTexture.hpp"
#include "slic3r/GUI/GLToolbar.hpp"
#include "slic3r/GUI/Gizmos/GLGizmos.hpp"
#include "libslic3r/ObjectID.hpp"

#include <map>

namespace Slic3r {

namespace UndoRedo {
struct Snapshot;
}

namespace GUI {

class GLCanvas3D;
class ClippingPlane;

class Rect
{
    float m_left;
    float m_top;
    float m_right;
    float m_bottom;

public:
    Rect() : m_left(0.0f) , m_top(0.0f) , m_right(0.0f) , m_bottom(0.0f) {}

    Rect(float left, float top, float right, float bottom) : m_left(left) , m_top(top) , m_right(right) , m_bottom(bottom) {}

    float get_left() const { return m_left; }
    void set_left(float left) { m_left = left; }

    float get_top() const { return m_top; }
    void set_top(float top) { m_top = top; }

    float get_right() const { return m_right; }
    void set_right(float right) { m_right = right; }

    float get_bottom() const { return m_bottom; }
    void set_bottom(float bottom) { m_bottom = bottom; }

    float get_width() const { return m_right - m_left; }
    float get_height() const { return m_top - m_bottom; }
};

class GLGizmosManager : public Slic3r::ObjectBase
{
public:
    static const float Default_Icons_Size;

    enum EType : unsigned char
    {
        // Order must match index in m_gizmos!
        Move,
        Scale,
        Rotate,
        Flatten,
        Cut,
        Hollow,
        SlaSupports,
        Undefined
    };

private:
    struct Layout
    {
        float scale{ 1.0f };
        float icons_size{ Default_Icons_Size };
        float border{ 5.0f };
        float gap_y{ 5.0f };

        float stride_y() const { return icons_size + gap_y;}

        float scaled_icons_size() const { return scale * icons_size; }
        float scaled_border() const { return scale * border; }
        float scaled_gap_y() const { return scale * gap_y; }
        float scaled_stride_y() const { return scale * stride_y(); }
    };

    GLCanvas3D& m_parent;
    bool m_enabled;
    std::vector<std::unique_ptr<GLGizmoBase>> m_gizmos;
    mutable GLTexture m_icons_texture;
    mutable bool m_icons_texture_dirty;
    BackgroundTexture m_background_texture;
    Layout m_layout;
    EType m_current;
    EType m_hover;

    std::vector<size_t> get_selectable_idxs() const;
    std::vector<size_t> get_activable_idxs() const;
    size_t get_gizmo_idx_from_mouse(const Vec2d& mouse_pos) const;

    void activate_gizmo(EType type);

    struct MouseCapture
    {
        bool left;
        bool middle;
        bool right;
        GLCanvas3D* parent;

        MouseCapture() { reset(); }

        bool any() const { return left || middle || right; }
        void reset() { left = middle = right = false; parent = nullptr; }
    };

    MouseCapture m_mouse_capture;
    std::string m_tooltip;
    bool m_serializing;
    std::unique_ptr<CommonGizmosData> m_common_gizmos_data;

public:
    explicit GLGizmosManager(GLCanvas3D& parent);

    bool init();

    template<class Archive>
    void load(Archive& ar)
    {
        if (!m_enabled)
            return;

        m_serializing = true;

        // Following is needed to know which to be turn on, but not actually modify
        // m_current prematurely, so activate_gizmo is not confused.
        EType old_current = m_current;
        ar(m_current);
        EType new_current = m_current;
        m_current = old_current;

        // activate_gizmo call sets m_current and calls set_state for the gizmo
        // it does nothing in case the gizmo is already activated
        // it can safely be called for Undefined gizmo
        activate_gizmo(new_current);
        if (m_current != Undefined)
            m_gizmos[m_current]->load(ar);
    }

    template<class Archive>
    void save(Archive& ar) const
    {
        if (!m_enabled)
            return;

        ar(m_current);

        if (m_current != Undefined && !m_gizmos.empty())
            m_gizmos[m_current]->save(ar);
    }

    bool is_enabled() const { return m_enabled; }
    void set_enabled(bool enable) { m_enabled = enable; }

    void set_overlay_icon_size(float size);
    void set_overlay_scale(float scale);

    void refresh_on_off_state();
    void reset_all_states();

    void set_hover_id(int id);
    void enable_grabber(EType type, unsigned int id, bool enable);

    void update(const Linef3& mouse_ray, const Point& mouse_pos);
    void update_data();

    EType get_current_type() const { return m_current; }
    GLGizmoBase* get_current() const;

    bool is_running() const;
    bool handle_shortcut(int key);

    bool is_dragging() const;
    void start_dragging();
    void stop_dragging();

    Vec3d get_displacement() const;

    Vec3d get_scale() const;
    void set_scale(const Vec3d& scale);

    Vec3d get_scale_offset() const;

    Vec3d get_rotation() const;
    void set_rotation(const Vec3d& rotation);

    Vec3d get_flattening_normal() const;

    void set_flattening_data(const ModelObject* model_object);

    void set_sla_support_data(ModelObject* model_object);
    bool gizmo_event(SLAGizmoEventType action, const Vec2d& mouse_position = Vec2d::Zero(), bool shift_down = false, bool alt_down = false, bool control_down = false);
    ClippingPlane get_sla_clipping_plane() const;
    bool wants_reslice_supports_on_undo() const;

    void render_current_gizmo() const;
    void render_current_gizmo_for_picking_pass() const;

    void render_overlay() const;

    std::string get_tooltip() const;

    bool on_mouse(wxMouseEvent& evt);
    bool on_mouse_wheel(wxMouseEvent& evt);
    bool on_char(wxKeyEvent& evt);
    bool on_key(wxKeyEvent& evt);

    void update_after_undo_redo(const UndoRedo::Snapshot& snapshot);

private:
    void render_background(float left, float top, float right, float bottom, float border) const;
    void do_render_overlay() const;

    float get_scaled_total_height() const;
    float get_scaled_total_width() const;

    bool generate_icons_texture() const;

    void update_on_off_state(const Vec2d& mouse_pos);
    std::string update_hover_state(const Vec2d& mouse_pos);
    bool grabber_contains_mouse() const;
};



class MeshRaycaster;
class MeshClipper;

// This class is only for sharing SLA related data between SLA gizmos
// and its synchronization with backend data. It should not be misused
// for anything else.
class CommonGizmosData {
public:
    CommonGizmosData();
    const TriangleMesh* mesh() const {
        return (! m_mesh ? nullptr : m_mesh); //(m_cavity_mesh ? m_cavity_mesh.get() : m_mesh));
    }

    bool update_from_backend(GLCanvas3D& canvas, ModelObject* model_object);
    bool recent_update = false;
    static constexpr float HoleStickOutLength = 1.f;

    ModelObject* m_model_object = nullptr;
    const TriangleMesh* m_mesh;
    std::unique_ptr<MeshRaycaster> m_mesh_raycaster;
    std::unique_ptr<MeshClipper> m_object_clipper;
    std::unique_ptr<MeshClipper> m_supports_clipper;

    //std::unique_ptr<TriangleMesh> m_cavity_mesh;
    //std::unique_ptr<GLVolume> m_volume_with_cavity;

    int m_active_instance = -1;
    float m_active_instance_bb_radius = 0;
    ObjectID m_model_object_id = 0;
    int m_print_object_idx = -1;
    int m_print_objects_count = -1;
    int m_old_timestamp = -1;

    float m_clipping_plane_distance = 0.f;
    std::unique_ptr<ClippingPlane> m_clipping_plane;
    bool m_clipping_plane_was_moved = false;

    void stash_clipping_plane() {
        m_clipping_plane_distance_stash = m_clipping_plane_distance;
    }

    void unstash_clipping_plane() {
        m_clipping_plane_distance = m_clipping_plane_distance_stash;
    }

    bool has_drilled_mesh() const { return m_has_drilled_mesh; }

    void build_AABB_if_needed();

private:
    const TriangleMesh* m_old_mesh;
    TriangleMesh m_backend_mesh_transformed;
    float m_clipping_plane_distance_stash = 0.f;
    bool m_has_drilled_mesh = false;
    bool m_schedule_aabb_calculation = false;
};

} // namespace GUI
} // namespace Slic3r

namespace cereal
{
    template <class Archive> struct specialize<Archive, Slic3r::GUI::GLGizmosManager, cereal::specialization::member_load_save> {};
}

#endif // slic3r_GUI_GLGizmosManager_hpp_

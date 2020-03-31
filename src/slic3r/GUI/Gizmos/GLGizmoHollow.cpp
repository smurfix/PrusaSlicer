#include "GLGizmoHollow.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/Gizmos/GLGizmos.hpp"

#include <GL/glew.h>

#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/GUI_ObjectSettings.hpp"
#include "slic3r/GUI/GUI_ObjectList.hpp"
#include "slic3r/GUI/MeshUtils.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/PresetBundle.hpp"
#include "libslic3r/SLAPrint.hpp"
#include "libslic3r/TriangleMesh.hpp"


namespace Slic3r {
namespace GUI {

GLGizmoHollow::GLGizmoHollow(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id)
    : GLGizmoBase(parent, icon_filename, sprite_id)
    , m_quadric(nullptr)
{
    m_quadric = ::gluNewQuadric();
    if (m_quadric != nullptr)
        // using GLU_FILL does not work when the instance's transformation
        // contains mirroring (normals are reverted)
        ::gluQuadricDrawStyle(m_quadric, GLU_FILL);
}

GLGizmoHollow::~GLGizmoHollow()
{
    if (m_quadric != nullptr)
        ::gluDeleteQuadric(m_quadric);
}

bool GLGizmoHollow::on_init()
{
    m_shortcut_key = WXK_CONTROL_H;
    m_desc["enable"]           = _(L("Hollow this object"));
    m_desc["preview"]          = _(L("Preview hollowed and drilled model"));
    m_desc["offset"]           = _(L("Offset")) + ": ";
    m_desc["quality"]          = _(L("Quality")) + ": ";
    m_desc["closing_distance"] = _(L("Closing distance")) + ": ";
    m_desc["hole_diameter"]    = _(L("Hole diameter")) + ": ";
    m_desc["hole_depth"]       = _(L("Hole depth")) + ": ";
    m_desc["remove_selected"]  = _(L("Remove selected holes"));
    m_desc["remove_all"]       = _(L("Remove all holes"));
    m_desc["clipping_of_view"] = _(L("Clipping of view"))+ ": ";
    m_desc["reset_direction"]  = _(L("Reset direction"));
    m_desc["show_supports"]    = _(L("Show supports"));

    return true;
}

void GLGizmoHollow::set_sla_support_data(ModelObject*, const Selection&)
{
    if (m_c->recent_update) {

        if (m_state == On)
            m_c->build_AABB_if_needed();

        update_clipping_plane(m_c->m_clipping_plane_was_moved);

        // This is a temporary and not very nice hack, to make sure that
        // if the cp was moved by the data returned by backend, it will
        // remember its direction. FIXME: Refactor this mess and make
        // the clipping plane itself part of the shared data.
        if (! m_c->m_clipping_plane_was_moved && m_c->m_clipping_plane_distance == 0.25f)
            m_c->m_clipping_plane_was_moved = true;


        if (m_c->m_model_object) {
            reload_cache();
            if (m_c->has_drilled_mesh())
                m_holes_in_drilled_mesh = m_c->m_model_object->sla_drain_holes;
        }
    }

    if (m_state == On) {
        m_parent.toggle_model_objects_visibility(false);
        m_parent.toggle_model_objects_visibility(true, m_c->m_model_object, m_c->m_active_instance);
        m_parent.toggle_sla_auxiliaries_visibility(m_show_supports, m_c->m_model_object, m_c->m_active_instance);
    }
}



void GLGizmoHollow::on_render() const
{
    const Selection& selection = m_parent.get_selection();

    // If current m_c->m_model_object does not match selection, ask GLCanvas3D to turn us off
    if (m_state == On
     && (m_c->m_model_object != selection.get_model()->objects[selection.get_object_idx()]
      || m_c->m_active_instance != selection.get_instance_idx()
      || m_c->m_model_object_id != m_c->m_model_object->id())) {
        m_parent.post_event(SimpleEvent(EVT_GLCANVAS_RESETGIZMOS));
        return;
    }

    // !!! is it necessary?
    //const_cast<GLGizmoHollow*>(this)->m_c->update_from_backend(m_parent, m_c->m_model_object);

    glsafe(::glEnable(GL_BLEND));
    glsafe(::glEnable(GL_DEPTH_TEST));

    m_z_shift = selection.get_volume(*selection.get_volume_idxs().begin())->get_sla_shift_z();

    render_hollowed_mesh();

    if (m_quadric != nullptr && selection.is_from_single_instance())
        render_points(selection, false);

    m_selection_rectangle.render(m_parent);
    render_clipping_plane(selection);

    glsafe(::glDisable(GL_BLEND));
}



void GLGizmoHollow::render_hollowed_mesh() const
{
    /*if (m_c->m_volume_with_cavity) {
        m_c->m_volume_with_cavity->set_sla_shift_z(m_z_shift);
        m_parent.get_shader().start_using();

        GLint current_program_id;
        glsafe(::glGetIntegerv(GL_CURRENT_PROGRAM, &current_program_id));
        GLint color_id = (current_program_id > 0) ? ::glGetUniformLocation(current_program_id, "uniform_color") : -1;
        GLint print_box_detection_id = (current_program_id > 0) ? ::glGetUniformLocation(current_program_id, "print_box.volume_detection") : -1;
        GLint print_box_worldmatrix_id = (current_program_id > 0) ? ::glGetUniformLocation(current_program_id, "print_box.volume_world_matrix") : -1;
        glcheck();
        m_c->m_volume_with_cavity->set_render_color();
        const Geometry::Transformation& volume_trafo = m_c->m_model_object->volumes.front()->get_transformation();
        m_c->m_volume_with_cavity->set_volume_transformation(volume_trafo);
        m_c->m_volume_with_cavity->set_instance_transformation(m_c->m_model_object->instances[size_t(m_c->m_active_instance)]->get_transformation());
        m_c->m_volume_with_cavity->render(color_id, print_box_detection_id, print_box_worldmatrix_id);
        m_parent.get_shader().stop_using();
    }*/
}


void GLGizmoHollow::render_clipping_plane(const Selection& selection) const
{
    if (m_c->m_clipping_plane_distance == 0.f)
        return;

    // Get transformation of the instance
    const GLVolume* vol = selection.get_volume(*selection.get_volume_idxs().begin());
    Geometry::Transformation trafo = vol->get_instance_transformation();
    trafo.set_offset(trafo.get_offset() + Vec3d(0., 0., m_z_shift));

    // Get transformation of supports
    Geometry::Transformation supports_trafo;
    supports_trafo.set_offset(Vec3d(trafo.get_offset()(0), trafo.get_offset()(1), vol->get_sla_shift_z()));
    supports_trafo.set_rotation(Vec3d(0., 0., trafo.get_rotation()(2)));
    // I don't know why, but following seems to be correct.
    supports_trafo.set_mirror(Vec3d(trafo.get_mirror()(0) * trafo.get_mirror()(1) * trafo.get_mirror()(2),
                                    1,
                                    1.));

    // Now initialize the TMS for the object, perform the cut and save the result.
    if (! m_c->m_object_clipper) {
        m_c->m_object_clipper.reset(new MeshClipper);
        m_c->m_object_clipper->set_mesh(*m_c->mesh());
    }
    m_c->m_object_clipper->set_plane(*m_c->m_clipping_plane);
    m_c->m_object_clipper->set_transformation(trafo);


    // Next, ask the backend if supports are already calculated. If so, we are gonna cut them too.
    //if (m_c->m_print_object_idx < 0)
    //    m_c->update_from_backend(m_parent, m_c->m_model_object);

    if (m_c->m_print_object_idx >= 0) {
        const SLAPrintObject* print_object = m_parent.sla_print()->objects()[m_c->m_print_object_idx];

        if (print_object->is_step_done(slaposSupportTree) && !print_object->get_mesh(slaposSupportTree).empty()) {
            // If the supports are already calculated, save the timestamp of the respective step
            // so we can later tell they were recalculated.
            size_t timestamp = print_object->step_state_with_timestamp(slaposSupportTree).timestamp;

            if (! m_c->m_supports_clipper || (int)timestamp != m_c->m_old_timestamp) {
                // The timestamp has changed.
                m_c->m_supports_clipper.reset(new MeshClipper);
                // The mesh should already have the shared vertices calculated.
                m_c->m_supports_clipper->set_mesh(print_object->support_mesh());
                m_c->m_old_timestamp = timestamp;
            }
            m_c->m_supports_clipper->set_plane(*m_c->m_clipping_plane);
            m_c->m_supports_clipper->set_transformation(supports_trafo);
        }
        else
            // The supports are not valid. We better dump the cached data.
            m_c->m_supports_clipper.reset();
    }

    // At this point we have the triangulated cuts for both the object and supports - let's render.
    if (! m_c->m_object_clipper->get_triangles().empty()) {
        ::glPushMatrix();
        ::glColor3f(1.0f, 0.37f, 0.0f);
        ::glBegin(GL_TRIANGLES);
        for (const Vec3f& point : m_c->m_object_clipper->get_triangles())
            ::glVertex3f(point(0), point(1), point(2));
        ::glEnd();
        ::glPopMatrix();
    }

    if (m_show_supports && m_c->m_supports_clipper && ! m_c->m_supports_clipper->get_triangles().empty()) {
        ::glPushMatrix();
        ::glColor3f(1.0f, 0.f, 0.37f);
        ::glBegin(GL_TRIANGLES);
        for (const Vec3f& point : m_c->m_supports_clipper->get_triangles())
            ::glVertex3f(point(0), point(1), point(2));
        ::glEnd();
        ::glPopMatrix();
    }
}


void GLGizmoHollow::on_render_for_picking() const
{
    const Selection& selection = m_parent.get_selection();
#if ENABLE_RENDER_PICKING_PASS
    m_z_shift = selection.get_volume(*selection.get_volume_idxs().begin())->get_sla_shift_z();
#endif

    glsafe(::glEnable(GL_DEPTH_TEST));
    render_points(selection, true);
    render_hollowed_mesh();
}

void GLGizmoHollow::render_points(const Selection& selection, bool picking) const
{
    if (!picking)
        glsafe(::glEnable(GL_LIGHTING));

    const GLVolume* vol = selection.get_volume(*selection.get_volume_idxs().begin());
    const Transform3d& instance_scaling_matrix_inverse = vol->get_instance_transformation().get_matrix(true, true, false, true).inverse();
    const Transform3d& instance_matrix = vol->get_instance_transformation().get_matrix();

    glsafe(::glPushMatrix());
    glsafe(::glTranslated(0.0, 0.0, m_z_shift));
    glsafe(::glMultMatrixd(instance_matrix.data()));

    float render_color[4];
    size_t cache_size = m_c->m_model_object->sla_drain_holes.size();
    for (size_t i = 0; i < cache_size; ++i)
    {
        const sla::DrainHole& drain_hole = m_c->m_model_object->sla_drain_holes[i];
        const bool& point_selected = m_selected[i];

        if (is_mesh_point_clipped((drain_hole.pos+m_c->HoleStickOutLength*drain_hole.normal).cast<double>()))
            continue;

        // First decide about the color of the point.
        if (picking) {
            std::array<float, 4> color = picking_color_component(i);
            render_color[0] = color[0];
            render_color[1] = color[1];
            render_color[2] = color[2];
            render_color[3] = color[3];
        }
        else {
            render_color[3] = 1.f;
            if (size_t(m_hover_id) == i) {
                render_color[0] = 0.f;
                render_color[1] = 1.0f;
                render_color[2] = 1.0f;
            }
            else { // neigher hover nor picking
                render_color[0] = point_selected ? 1.0f : 0.7f;
                render_color[1] = point_selected ? 0.3f : 0.7f;
                render_color[2] = point_selected ? 0.3f : 0.7f;
                render_color[3] = 0.5f;
            }
        }
        glsafe(::glColor4fv(render_color));
        float render_color_emissive[4] = { 0.5f * render_color[0], 0.5f * render_color[1], 0.5f * render_color[2], 1.f};
        glsafe(::glMaterialfv(GL_FRONT, GL_EMISSION, render_color_emissive));

        // Inverse matrix of the instance scaling is applied so that the mark does not scale with the object.
        glsafe(::glPushMatrix());
        glsafe(::glTranslatef(drain_hole.pos(0), drain_hole.pos(1), drain_hole.pos(2)));
        glsafe(::glMultMatrixd(instance_scaling_matrix_inverse.data()));

        if (vol->is_left_handed())
            glFrontFace(GL_CW);

        // Matrices set, we can render the point mark now.

        Eigen::Quaterniond q;
        q.setFromTwoVectors(Vec3d{0., 0., 1.}, instance_scaling_matrix_inverse * (-drain_hole.normal).cast<double>());
        Eigen::AngleAxisd aa(q);
        glsafe(::glRotated(aa.angle() * (180. / M_PI), aa.axis()(0), aa.axis()(1), aa.axis()(2)));
        glsafe(::glPushMatrix());
        glsafe(::glTranslated(0., 0., -drain_hole.height));
        ::gluCylinder(m_quadric, drain_hole.radius, drain_hole.radius, drain_hole.height, 24, 1);
        glsafe(::glTranslated(0., 0., drain_hole.height));
        ::gluDisk(m_quadric, 0.0, drain_hole.radius, 24, 1);
        glsafe(::glTranslated(0., 0., -drain_hole.height));
        glsafe(::glRotatef(180.f, 1.f, 0.f, 0.f));
        ::gluDisk(m_quadric, 0.0, drain_hole.radius, 24, 1);
        glsafe(::glPopMatrix());

        if (vol->is_left_handed())
            glFrontFace(GL_CCW);
        glsafe(::glPopMatrix());
    }

    {
        // Reset emissive component to zero (the default value)
        float render_color_emissive[4] = { 0.f, 0.f, 0.f, 1.f };
        glsafe(::glMaterialfv(GL_FRONT, GL_EMISSION, render_color_emissive));
    }

    if (!picking)
        glsafe(::glDisable(GL_LIGHTING));

    glsafe(::glPopMatrix());
}



bool GLGizmoHollow::is_mesh_point_clipped(const Vec3d& point) const
{
    if (m_c->m_clipping_plane_distance == 0.f)
        return false;

    Vec3d transformed_point = m_c->m_model_object->instances[m_c->m_active_instance]->get_transformation().get_matrix() * point;
    transformed_point(2) += m_z_shift;
    return m_c->m_clipping_plane->is_point_clipped(transformed_point);
}



// Unprojects the mouse position on the mesh and saves hit point and normal of the facet into pos_and_normal
// Return false if no intersection was found, true otherwise.
bool GLGizmoHollow::unproject_on_mesh(const Vec2d& mouse_pos, std::pair<Vec3f, Vec3f>& pos_and_normal)
{
    if (! m_c->m_mesh_raycaster)
        return false;

    // if the gizmo doesn't have the V, F structures for igl, calculate them first:
    // !!! is it really necessary?
    //m_c->update_from_backend(m_parent, m_c->m_model_object);

    const Camera& camera = m_parent.get_camera();
    const Selection& selection = m_parent.get_selection();
    const GLVolume* volume = selection.get_volume(*selection.get_volume_idxs().begin());
    Geometry::Transformation trafo = volume->get_instance_transformation();
    trafo.set_offset(trafo.get_offset() + Vec3d(0., 0., m_z_shift));

    // The raycaster query
    Vec3f hit;
    Vec3f normal;
    if (m_c->m_mesh_raycaster->unproject_on_mesh(
            mouse_pos,
            trafo.get_matrix(),
            camera,
            hit,
            normal,
            m_c->m_clipping_plane_distance != 0.f ? m_c->m_clipping_plane.get() : nullptr))
    {
        if (m_c->has_drilled_mesh()) {
            // in this case the raycaster sees the hollowed and drilled mesh.
            // if the point lies on the surface created by the hole, we want
            // to ignore it.
            for (const sla::DrainHole& hole : m_holes_in_drilled_mesh) {
                sla::DrainHole outer(hole);
                outer.radius *= 1.001f;
                outer.height *= 1.001f;
                if (outer.is_inside(hit))
                    return false;
            }
        }

        // Return both the point and the facet normal.
        pos_and_normal = std::make_pair(hit, normal);
        return true;
    }
    else
        return false;
}

// Following function is called from GLCanvas3D to inform the gizmo about a mouse/keyboard event.
// The gizmo has an opportunity to react - if it does, it should return true so that the Canvas3D is
// aware that the event was reacted to and stops trying to make different sense of it. If the gizmo
// concludes that the event was not intended for it, it should return false.
bool GLGizmoHollow::gizmo_event(SLAGizmoEventType action, const Vec2d& mouse_position, bool shift_down, bool alt_down, bool control_down)
{
    // left down with shift - show the selection rectangle:
    if (action == SLAGizmoEventType::LeftDown && (shift_down || alt_down || control_down)) {
        if (m_hover_id == -1) {
            if (shift_down || alt_down) {
                m_selection_rectangle.start_dragging(mouse_position, shift_down ? GLSelectionRectangle::Select : GLSelectionRectangle::Deselect);
            }
        }
        else {
            if (m_selected[m_hover_id])
                unselect_point(m_hover_id);
            else {
                if (!alt_down)
                    select_point(m_hover_id);
            }
        }

        return true;
    }

    // left down without selection rectangle - place point on the mesh:
    if (action == SLAGizmoEventType::LeftDown && !m_selection_rectangle.is_dragging() && !shift_down) {
        // If any point is in hover state, this should initiate its move - return control back to GLCanvas:
        if (m_hover_id != -1)
            return false;

        // If there is some selection, don't add new point and deselect everything instead.
        if (m_selection_empty) {
            std::pair<Vec3f, Vec3f> pos_and_normal;
            if (unproject_on_mesh(mouse_position, pos_and_normal)) { // we got an intersection
                Plater::TakeSnapshot snapshot(wxGetApp().plater(), _(L("Add drainage hole")));

                Vec3d scaling = m_c->m_model_object->instances[m_c->m_active_instance]->get_scaling_factor();
                Vec3f normal_transformed(pos_and_normal.second(0)/scaling(0),
                                         pos_and_normal.second(1)/scaling(1),
                                         pos_and_normal.second(2)/scaling(2));

                m_c->m_model_object->sla_drain_holes.emplace_back(pos_and_normal.first + m_c->HoleStickOutLength * pos_and_normal.second/* normal_transformed.normalized()*/,
                                                             -pos_and_normal.second, m_new_hole_radius, m_new_hole_height);
                m_selected.push_back(false);
                assert(m_selected.size() == m_c->m_model_object->sla_drain_holes.size());
                m_parent.set_as_dirty();
                m_wait_for_up_event = true;
            }
            else
                return false;
        }
        else
            select_point(NoPoints);

        return true;
    }

    // left up with selection rectangle - select points inside the rectangle:
    if ((action == SLAGizmoEventType::LeftUp || action == SLAGizmoEventType::ShiftUp || action == SLAGizmoEventType::AltUp) && m_selection_rectangle.is_dragging()) {
        // Is this a selection or deselection rectangle?
        GLSelectionRectangle::EState rectangle_status = m_selection_rectangle.get_state();

        // First collect positions of all the points in world coordinates.
        Geometry::Transformation trafo = m_c->m_model_object->instances[m_c->m_active_instance]->get_transformation();
        trafo.set_offset(trafo.get_offset() + Vec3d(0., 0., m_z_shift));
        std::vector<Vec3d> points;
        for (unsigned int i=0; i<m_c->m_model_object->sla_drain_holes.size(); ++i)
            points.push_back(trafo.get_matrix() * m_c->m_model_object->sla_drain_holes[i].pos.cast<double>());

        // Now ask the rectangle which of the points are inside.
        std::vector<Vec3f> points_inside;
        std::vector<unsigned int> points_idxs = m_selection_rectangle.stop_dragging(m_parent, points);
        for (size_t idx : points_idxs)
            points_inside.push_back(points[idx].cast<float>());

        // Only select/deselect points that are actually visible
        for (size_t idx :  m_c->m_mesh_raycaster->get_unobscured_idxs(trafo, m_parent.get_camera(), points_inside, m_c->m_clipping_plane.get()))
        {
            if (rectangle_status == GLSelectionRectangle::Deselect)
                unselect_point(points_idxs[idx]);
            else
                select_point(points_idxs[idx]);
        }
        return true;
    }

    // left up with no selection rectangle
    if (action == SLAGizmoEventType::LeftUp) {
        if (m_wait_for_up_event) {
            m_wait_for_up_event = false;
            return true;
        }
    }

    // dragging the selection rectangle:
    if (action == SLAGizmoEventType::Dragging) {
        if (m_wait_for_up_event)
            return true; // point has been placed and the button not released yet
                         // this prevents GLCanvas from starting scene rotation

        if (m_selection_rectangle.is_dragging())  {
            m_selection_rectangle.dragging(mouse_position);
            return true;
        }

        return false;
    }

    if (action == SLAGizmoEventType::Delete) {
        // delete key pressed
        delete_selected_points();
        return true;
    }

    if (action == SLAGizmoEventType::RightDown) {
        if (m_hover_id != -1) {
            select_point(NoPoints);
            select_point(m_hover_id);
            delete_selected_points();
            return true;
        }
        return false;
    }

    if (action == SLAGizmoEventType::SelectAll) {
        select_point(AllPoints);
        return true;
    }

    if (action == SLAGizmoEventType::MouseWheelUp && control_down) {
        m_c->m_clipping_plane_distance = std::min(1.f, m_c->m_clipping_plane_distance + 0.01f);
        update_clipping_plane(m_c->m_clipping_plane_was_moved);
        m_c->m_clipping_plane_was_moved = true;
        return true;
    }

    if (action == SLAGizmoEventType::MouseWheelDown && control_down) {
        m_c->m_clipping_plane_distance = std::max(0.f, m_c->m_clipping_plane_distance - 0.01f);
        update_clipping_plane(true);
        return true;
    }

    if (action == SLAGizmoEventType::ResetClippingPlane) {
        update_clipping_plane();
        return true;
    }

    return false;
}

void GLGizmoHollow::delete_selected_points()
{
    Plater::TakeSnapshot snapshot(wxGetApp().plater(), _(L("Delete drainage hole")));

    for (unsigned int idx=0; idx<m_c->m_model_object->sla_drain_holes.size(); ++idx) {
        if (m_selected[idx]) {
            m_selected.erase(m_selected.begin()+idx);
            m_c->m_model_object->sla_drain_holes.erase(m_c->m_model_object->sla_drain_holes.begin() + (idx--));
        }
    }

    select_point(NoPoints);
}

void GLGizmoHollow::on_update(const UpdateData& data)
{
    if (m_hover_id != -1) {
        std::pair<Vec3f, Vec3f> pos_and_normal;
        if (! unproject_on_mesh(data.mouse_pos.cast<double>(), pos_and_normal))
            return;
        m_c->m_model_object->sla_drain_holes[m_hover_id].pos = pos_and_normal.first + m_c->HoleStickOutLength * pos_and_normal.second;
        m_c->m_model_object->sla_drain_holes[m_hover_id].normal = -pos_and_normal.second;
    }
}

std::pair<const TriangleMesh *, sla::HollowingConfig> GLGizmoHollow::get_hollowing_parameters() const
{
    // FIXME this function is probably obsolete, caller could
    // get the data from model config himself
    auto opts = get_config_options({"hollowing_min_thickness", "hollowing_quality", "hollowing_closing_distance"});
    double offset = static_cast<const ConfigOptionFloat*>(opts[0].first)->value;
    double quality = static_cast<const ConfigOptionFloat*>(opts[1].first)->value;
    double closing_d = static_cast<const ConfigOptionFloat*>(opts[2].first)->value;
    return std::make_pair(m_c->m_mesh, sla::HollowingConfig{offset, quality, closing_d});
}

void GLGizmoHollow::update_mesh_raycaster(std::unique_ptr<MeshRaycaster> &&rc)
{
    m_c->m_mesh_raycaster = std::move(rc);
    m_c->m_object_clipper.reset();
    //m_c->m_volume_with_cavity.reset();
}

void GLGizmoHollow::hollow_mesh(bool postpone_error_messages)
{
    // Trigger a UI job to hollow the mesh.
   // wxGetApp().plater()->hollow();

    wxGetApp().CallAfter([this, postpone_error_messages]() {
        wxGetApp().plater()->reslice_SLA_hollowing(*m_c->m_model_object, postpone_error_messages);
    });
}


void GLGizmoHollow::update_hollowed_mesh(std::unique_ptr<TriangleMesh> &&mesh)
{
    // Called from Plater when the UI job finishes
    /*m_c->m_cavity_mesh = std::move(mesh);

    if(m_c->m_cavity_mesh) {
        // First subtract the holes:
        if (! m_c->m_model_object->sla_drain_holes.empty()) {
            TriangleMesh holes_mesh;
            for (const sla::DrainHole& hole : m_c->m_model_object->sla_drain_holes) {
                TriangleMesh hole_mesh = make_cylinder(hole.radius, hole.height, 2*M_PI/32);

                Vec3d scaling = m_c->m_model_object->instances[m_c->m_active_instance]->get_scaling_factor();
                Vec3d normal_transformed = Vec3d(hole.normal(0)/scaling(0), hole.normal(1)/scaling(1), hole.normal(2)/scaling(2));
                normal_transformed.normalize();

                // Rotate the cylinder appropriately
                Eigen::Quaterniond q;
                Transform3d m = Transform3d::Identity();
                m.matrix().block(0, 0, 3, 3) = q.setFromTwoVectors(Vec3d::UnitZ(), normal_transformed).toRotationMatrix();
                hole_mesh.transform(m);

                // If the instance is scaled, undo the scaling of the hole
                hole_mesh.scale(Vec3d(1/scaling(0), 1/scaling(1), 1/scaling(2)));

                // Translate the hole into position and merge with the others
                hole_mesh.translate(hole.pos);
                holes_mesh.merge(hole_mesh);
                holes_mesh.repair();
            }
            MeshBoolean::minus(*m_c->m_cavity_mesh.get(), holes_mesh);
        }

        // create a new GLVolume that only has the cavity inside
        m_c->m_volume_with_cavity.reset(new GLVolume(GLVolume::MODEL_COLOR[2]));
        m_c->m_volume_with_cavity->indexed_vertex_array.load_mesh(*m_c->m_cavity_mesh.get());
        m_c->m_volume_with_cavity->finalize_geometry(true);
        m_c->m_volume_with_cavity->force_transparent = false;

        m_parent.toggle_model_objects_visibility(false, m_c->m_model_object, m_c->m_active_instance);
        m_parent.toggle_sla_auxiliaries_visibility(true, m_c->m_model_object, m_c->m_active_instance);

        // Reset raycaster so it works with the new mesh:
        m_c->m_mesh_raycaster.reset(new MeshRaycaster(*m_c->mesh()));
    }

    if (m_c->m_clipping_plane_distance == 0.f) {
        m_c->m_clipping_plane_distance = 0.5f;
        update_clipping_plane();
    }*/
}

std::vector<std::pair<const ConfigOption*, const ConfigOptionDef*>> GLGizmoHollow::get_config_options(const std::vector<std::string>& keys) const
{
    std::vector<std::pair<const ConfigOption*, const ConfigOptionDef*>> out;

    if (!m_c->m_model_object)
        return out;

    const DynamicPrintConfig& object_cfg = m_c->m_model_object->config;
    const DynamicPrintConfig& print_cfg = wxGetApp().preset_bundle->sla_prints.get_edited_preset().config;
    std::unique_ptr<DynamicPrintConfig> default_cfg = nullptr;

    for (const std::string& key : keys) {
        if (object_cfg.has(key))
            out.emplace_back(object_cfg.option(key), &object_cfg.def()->options.at(key)); // at() needed for const map
        else
            if (print_cfg.has(key))
                out.emplace_back(print_cfg.option(key), &print_cfg.def()->options.at(key));
            else { // we must get it from defaults
                if (default_cfg == nullptr)
                    default_cfg.reset(DynamicPrintConfig::new_from_defaults_keys(keys));
                out.emplace_back(default_cfg->option(key), &default_cfg->def()->options.at(key));
            }
    }

    return out;
}


ClippingPlane GLGizmoHollow::get_sla_clipping_plane() const
{
    if (!m_c->m_model_object || m_state == Off || m_c->m_clipping_plane_distance == 0.f)
        return ClippingPlane::ClipsNothing();
    else
        return ClippingPlane(-m_c->m_clipping_plane->get_normal(), m_c->m_clipping_plane->get_data()[3]);
}


void GLGizmoHollow::on_render_input_window(float x, float y, float bottom_limit)
{
    if (! m_c->m_model_object)
        return;

    bool first_run = true; // This is a hack to redraw the button when all points are removed,
                           // so it is not delayed until the background process finishes.

    ConfigOptionMode current_mode = wxGetApp().get_mode();

    std::vector<std::string> opts_keys = {"hollowing_min_thickness", "hollowing_quality", "hollowing_closing_distance"};
    auto opts = get_config_options(opts_keys);
    auto* offset_cfg = static_cast<const ConfigOptionFloat*>(opts[0].first);
    float offset = offset_cfg->value;
    double offset_min = opts[0].second->min;
    double offset_max = opts[0].second->max;

    auto* quality_cfg = static_cast<const ConfigOptionFloat*>(opts[1].first);
    float quality = quality_cfg->value;
    double quality_min = opts[1].second->min;
    double quality_max = opts[1].second->max;
    ConfigOptionMode quality_mode = opts[1].second->mode;

    auto* closing_d_cfg = static_cast<const ConfigOptionFloat*>(opts[2].first);
    float closing_d = closing_d_cfg->value;
    double closing_d_min = opts[2].second->min;
    double closing_d_max = opts[2].second->max;
    ConfigOptionMode closing_d_mode = opts[2].second->mode;

    m_desc["offset"] = _(opts[0].second->label) + ":";
    m_desc["quality"] = _(opts[1].second->label) + ":";
    m_desc["closing_distance"] = _(opts[2].second->label) + ":";


RENDER_AGAIN:
    const float approx_height = m_imgui->scaled(20.0f);
    y = std::min(y, bottom_limit - approx_height);
    m_imgui->set_next_window_pos(x, y, ImGuiCond_Always);

    m_imgui->begin(on_get_name(), ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse);

    // First calculate width of all the texts that are could possibly be shown. We will decide set the dialog width based on that:
    const float settings_sliders_left =
      std::max({m_imgui->calc_text_size(m_desc.at("offset")).x,
               m_imgui->calc_text_size(m_desc.at("quality")).x,
               m_imgui->calc_text_size(m_desc.at("closing_distance")).x,
               m_imgui->calc_text_size(m_desc.at("hole_diameter")).x,
               m_imgui->calc_text_size(m_desc.at("hole_depth")).x})
           + m_imgui->scaled(1.f);

    const float clipping_slider_left = std::max(m_imgui->calc_text_size(m_desc.at("clipping_of_view")).x, m_imgui->calc_text_size(m_desc.at("reset_direction")).x) + m_imgui->scaled(1.5f);
    const float diameter_slider_left = settings_sliders_left; //m_imgui->calc_text_size(m_desc.at("hole_diameter")).x + m_imgui->scaled(1.f);
    const float minimal_slider_width = m_imgui->scaled(4.f);

    float window_width = minimal_slider_width + std::max({settings_sliders_left, clipping_slider_left, diameter_slider_left});
    window_width = std::max(window_width, m_imgui->calc_text_size(m_desc.at("preview")).x);

    if (m_imgui->button(m_desc["preview"]))
        hollow_mesh();
    
    bool config_changed = false;

    ImGui::Separator();

    {
        auto opts = get_config_options({"hollowing_enable"});
        m_enable_hollowing = static_cast<const ConfigOptionBool*>(opts[0].first)->value;
        if (m_imgui->checkbox(m_desc["enable"], m_enable_hollowing)) {
            m_c->m_model_object->config.opt<ConfigOptionBool>("hollowing_enable", true)->value = m_enable_hollowing;
            wxGetApp().obj_list()->update_and_show_object_settings_item();
            config_changed = true;
        }
    }

    m_imgui->disabled_begin(! m_enable_hollowing);
    float max_tooltip_width = ImGui::GetFontSize() * 20.0f;
    m_imgui->text(m_desc.at("offset"));
    ImGui::SameLine(settings_sliders_left);
    ImGui::PushItemWidth(window_width - settings_sliders_left);
    ImGui::SliderFloat("   ", &offset, offset_min, offset_max, "%.1f mm");
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(max_tooltip_width);
        ImGui::TextUnformatted((_utf8(opts[0].second->tooltip)).c_str());
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
    bool slider_clicked = ImGui::IsItemClicked(); // someone clicked the slider
    bool slider_edited = ImGui::IsItemEdited(); // someone is dragging the slider
    bool slider_released = ImGui::IsItemDeactivatedAfterEdit(); // someone has just released the slider

    if (current_mode >= quality_mode) {
        m_imgui->text(m_desc.at("quality"));
        ImGui::SameLine(settings_sliders_left);
        ImGui::SliderFloat("    ", &quality, quality_min, quality_max, "%.1f");
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::PushTextWrapPos(max_tooltip_width);
            ImGui::TextUnformatted((_utf8(opts[1].second->tooltip)).c_str());
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
        slider_clicked |= ImGui::IsItemClicked();
        slider_edited |= ImGui::IsItemEdited();
        slider_released |= ImGui::IsItemDeactivatedAfterEdit();
    }

    if (current_mode >= closing_d_mode) {
        m_imgui->text(m_desc.at("closing_distance"));
        ImGui::SameLine(settings_sliders_left);
        ImGui::SliderFloat("      ", &closing_d, closing_d_min, closing_d_max, "%.1f mm");
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::PushTextWrapPos(max_tooltip_width);
            ImGui::TextUnformatted((_utf8(opts[2].second->tooltip)).c_str());
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
        slider_clicked |= ImGui::IsItemClicked();
        slider_edited |= ImGui::IsItemEdited();
        slider_released |= ImGui::IsItemDeactivatedAfterEdit();
    }

    if (slider_clicked) {
        m_offset_stash = offset;
        m_quality_stash = quality;
        m_closing_d_stash = closing_d;
    }
    if (slider_edited || slider_released) {
        if (slider_released) {
            m_c->m_model_object->config.opt<ConfigOptionFloat>("hollowing_min_thickness", true)->value = m_offset_stash;
            m_c->m_model_object->config.opt<ConfigOptionFloat>("hollowing_quality", true)->value = m_quality_stash;
            m_c->m_model_object->config.opt<ConfigOptionFloat>("hollowing_closing_distance", true)->value = m_closing_d_stash;
            Plater::TakeSnapshot snapshot(wxGetApp().plater(), _(L("Hollowing parameter change")));
        }
        m_c->m_model_object->config.opt<ConfigOptionFloat>("hollowing_min_thickness", true)->value = offset;
        m_c->m_model_object->config.opt<ConfigOptionFloat>("hollowing_quality", true)->value = quality;
        m_c->m_model_object->config.opt<ConfigOptionFloat>("hollowing_closing_distance", true)->value = closing_d;
        if (slider_released) {
            wxGetApp().obj_list()->update_and_show_object_settings_item();
            config_changed = true;
        }
    }

    m_imgui->disabled_end();

    bool force_refresh = false;
    bool remove_selected = false;
    bool remove_all = false;

   // m_imgui->text(" "); // vertical gap
    ImGui::Separator();

    float diameter_upper_cap = 15.;
    if (m_new_hole_radius > diameter_upper_cap)
        m_new_hole_radius = diameter_upper_cap;
    m_imgui->text(m_desc.at("hole_diameter"));
    ImGui::SameLine(diameter_slider_left);
    ImGui::PushItemWidth(window_width - diameter_slider_left);

    float diam = 2.f * m_new_hole_radius;
    ImGui::SliderFloat("", &diam, 1.f, diameter_upper_cap, "%.1f mm");
    m_new_hole_radius = diam / 2.f;
    bool clicked = ImGui::IsItemClicked();
    bool edited = ImGui::IsItemEdited();
    bool deactivated = ImGui::IsItemDeactivatedAfterEdit();

    m_imgui->text(m_desc["hole_depth"]);
    ImGui::SameLine(diameter_slider_left);
    m_new_hole_height -= m_c->HoleStickOutLength;
    ImGui::SliderFloat("  ", &m_new_hole_height, 0.f, 10.f, "%.1f mm");
    m_new_hole_height += m_c->HoleStickOutLength;

    clicked |= ImGui::IsItemClicked();
    edited |= ImGui::IsItemEdited();
    deactivated |= ImGui::IsItemDeactivatedAfterEdit();

    // Following is a nasty way to:
    //  - save the initial value of the slider before one starts messing with it
    //  - keep updating the head radius during sliding so it is continuosly refreshed in 3D scene
    //  - take correct undo/redo snapshot after the user is done with moving the slider
    if (! m_selection_empty) {
        if (clicked) {
            m_holes_stash = m_c->m_model_object->sla_drain_holes;
        }
        if (edited) {
            for (size_t idx=0; idx<m_selected.size(); ++idx)
                if (m_selected[idx]) {
                    m_c->m_model_object->sla_drain_holes[idx].radius = m_new_hole_radius;
                    m_c->m_model_object->sla_drain_holes[idx].height = m_new_hole_height;
                }
        }
        if (deactivated) {
            // momentarily restore the old value to take snapshot
            sla::DrainHoles new_holes = m_c->m_model_object->sla_drain_holes;
            m_c->m_model_object->sla_drain_holes = m_holes_stash;
            float backup_rad = m_new_hole_radius;
            float backup_hei = m_new_hole_height;
            for (size_t i=0; i<m_holes_stash.size(); ++i) {
                if (m_selected[i]) {
                    m_new_hole_radius = m_holes_stash[i].radius;
                    m_new_hole_height = m_holes_stash[i].height;
                    break;
                }
            }
            Plater::TakeSnapshot snapshot(wxGetApp().plater(), _(L("Change drainage hole diameter")));
            m_new_hole_radius = backup_rad;
            m_new_hole_height = backup_hei;
            m_c->m_model_object->sla_drain_holes = new_holes;
        }
    }

    m_imgui->disabled_begin(m_selection_empty);
    remove_selected = m_imgui->button(m_desc.at("remove_selected"));
    m_imgui->disabled_end();

    m_imgui->disabled_begin(m_c->m_model_object->sla_drain_holes.empty());
    remove_all = m_imgui->button(m_desc.at("remove_all"));
    m_imgui->disabled_end();

    // Following is rendered in both editing and non-editing mode:
   // m_imgui->text("");
    ImGui::Separator();
    if (m_c->m_clipping_plane_distance == 0.f)
        m_imgui->text(m_desc.at("clipping_of_view"));
    else {
        if (m_imgui->button(m_desc.at("reset_direction"))) {
            wxGetApp().CallAfter([this](){
                    update_clipping_plane();
                });
        }
    }

    ImGui::SameLine(clipping_slider_left);
    ImGui::PushItemWidth(window_width - clipping_slider_left);
    if (ImGui::SliderFloat("     ", &m_c->m_clipping_plane_distance, 0.f, 1.f, "%.2f")) {
        update_clipping_plane(m_c->m_clipping_plane_was_moved);
        m_c->m_clipping_plane_was_moved = true;
    }

    // make sure supports are shown/hidden as appropriate
    if (m_imgui->checkbox(m_desc["show_supports"], m_show_supports)) {
        m_parent.toggle_sla_auxiliaries_visibility(m_show_supports, m_c->m_model_object, m_c->m_active_instance);
        force_refresh = true;
    }

    m_imgui->end();


    if (remove_selected || remove_all) {
        force_refresh = false;
        m_parent.set_as_dirty();

        if (remove_all) {
            select_point(AllPoints);
            delete_selected_points();
        }
        if (remove_selected)
            delete_selected_points();

        if (first_run) {
            first_run = false;
            goto RENDER_AGAIN;
        }
    }

    if (force_refresh)
        m_parent.set_as_dirty();
    
    if (config_changed)
        m_parent.post_event(SimpleEvent(EVT_GLCANVAS_FORCE_UPDATE));
}

bool GLGizmoHollow::on_is_activable() const
{
    const Selection& selection = m_parent.get_selection();

    if (wxGetApp().preset_bundle->printers.get_edited_preset().printer_technology() != ptSLA
        || !selection.is_from_single_instance())
        return false;

    // Check that none of the selected volumes is outside. Only SLA auxiliaries (supports) are allowed outside.
    const Selection::IndicesList& list = selection.get_volume_idxs();
    for (const auto& idx : list)
        if (selection.get_volume(idx)->is_outside && selection.get_volume(idx)->composite_id.volume_id >= 0)
            return false;

    return true;
}

bool GLGizmoHollow::on_is_selectable() const
{
    return (wxGetApp().preset_bundle->printers.get_edited_preset().printer_technology() == ptSLA);
}

std::string GLGizmoHollow::on_get_name() const
{
    return (_(L("Hollow and drill")) + " [H]").ToUTF8().data();
}



void GLGizmoHollow::on_set_state()
{
    // m_c->m_model_object pointer can be invalid (for instance because of undo/redo action),
    // we should recover it from the object id
    m_c->m_model_object = nullptr;
    for (const auto mo : wxGetApp().model().objects) {
        if (mo->id() == m_c->m_model_object_id) {
            m_c->m_model_object = mo;
            break;
        }
    }

    if (m_state == m_old_state)
        return;

    if (m_state == On && m_old_state != On) { // the gizmo was just turned on
        //Plater::TakeSnapshot snapshot(wxGetApp().plater(), _(L("SLA gizmo turned on")));
        //m_c->update_from_backend(m_parent, m_c->m_model_object);
        m_c->unstash_clipping_plane();
        update_clipping_plane(m_c->m_clipping_plane_was_moved);

        m_c->build_AABB_if_needed();

        // we'll now reload support points:
        if (m_c->m_model_object)
            reload_cache();

        m_parent.toggle_model_objects_visibility(false);
        if (m_c->m_model_object) {
            m_parent.toggle_model_objects_visibility(true, m_c->m_model_object, m_c->m_active_instance);
            m_parent.toggle_sla_auxiliaries_visibility(m_show_supports, m_c->m_model_object, m_c->m_active_instance);
        }

        // Set default head diameter from config.
        //const DynamicPrintConfig& cfg = wxGetApp().preset_bundle->sla_prints.get_edited_preset().config;
        //m_new_hole_radius = static_cast<const ConfigOptionFloat*>(cfg.option("support_head_front_diameter"))->value;
    }
    if (m_state == Off && m_old_state != Off) { // the gizmo was just turned Off
        //Plater::TakeSnapshot snapshot(wxGetApp().plater(), _(L("SLA gizmo turned off")));
        m_parent.post_event(SimpleEvent(EVT_GLCANVAS_FORCE_UPDATE));
        m_parent.toggle_model_objects_visibility(true);
        m_c->stash_clipping_plane();
        m_c->m_clipping_plane_distance = 0.f;
        update_clipping_plane(true);
        // Release clippers and the AABB raycaster.
        m_c->m_object_clipper.reset();
        m_c->m_supports_clipper.reset();
        //m_c->m_mesh_raycaster.reset();
        //m_c->m_cavity_mesh.reset();
        //m_c->m_volume_with_cavity.reset();
    }
    m_old_state = m_state;
}



void GLGizmoHollow::on_start_dragging()
{
    if (m_hover_id != -1) {
        select_point(NoPoints);
        select_point(m_hover_id);
        m_hole_before_drag = m_c->m_model_object->sla_drain_holes[m_hover_id].pos;
    }
    else
        m_hole_before_drag = Vec3f::Zero();
}


void GLGizmoHollow::on_stop_dragging()
{
    if (m_hover_id != -1) {
        Vec3f backup = m_c->m_model_object->sla_drain_holes[m_hover_id].pos;

        if (m_hole_before_drag != Vec3f::Zero() // some point was touched
         && backup != m_hole_before_drag) // and it was moved, not just selected
        {
            m_c->m_model_object->sla_drain_holes[m_hover_id].pos = m_hole_before_drag;
            Plater::TakeSnapshot snapshot(wxGetApp().plater(), _(L("Move drainage hole")));
            m_c->m_model_object->sla_drain_holes[m_hover_id].pos = backup;
        }
    }
    m_hole_before_drag = Vec3f::Zero();
}



void GLGizmoHollow::on_load(cereal::BinaryInputArchive& ar)
{
    ar(m_c->m_clipping_plane_distance,
       *m_c->m_clipping_plane,
       m_c->m_model_object_id,
       m_new_hole_radius,
       m_new_hole_height,
       m_selected,
       m_selection_empty
    );
}



void GLGizmoHollow::on_save(cereal::BinaryOutputArchive& ar) const
{
    ar(m_c->m_clipping_plane_distance,
       *m_c->m_clipping_plane,
       m_c->m_model_object_id,
       m_new_hole_radius,
       m_new_hole_height,
       m_selected,
       m_selection_empty
    );
}



void GLGizmoHollow::select_point(int i)
{
    if (i == AllPoints || i == NoPoints) {
        m_selected.assign(m_selected.size(), i == AllPoints);
        m_selection_empty = (i == NoPoints);

        if (i == AllPoints) {
            m_new_hole_radius = m_c->m_model_object->sla_drain_holes[0].radius;
            m_new_hole_height = m_c->m_model_object->sla_drain_holes[0].height;
        }
    }
    else {
        while (size_t(i) >= m_selected.size())
            m_selected.push_back(false);
        m_selected[i] = true;
        m_selection_empty = false;
        m_new_hole_radius = m_c->m_model_object->sla_drain_holes[i].radius;
        m_new_hole_height = m_c->m_model_object->sla_drain_holes[i].height;
    }
}


void GLGizmoHollow::unselect_point(int i)
{
    m_selected[i] = false;
    m_selection_empty = true;
    for (const bool sel : m_selected) {
        if (sel) {
            m_selection_empty = false;
            break;
        }
    }
}

void GLGizmoHollow::reload_cache()
{
    m_selected.clear();
    m_selected.assign(m_c->m_model_object->sla_drain_holes.size(), false);
}

void GLGizmoHollow::update_clipping_plane(bool keep_normal) const
{
    if (! m_c->m_model_object)
        return;
    Vec3d normal = (keep_normal && m_c->m_clipping_plane->get_normal() != Vec3d::Zero() ?
                        m_c->m_clipping_plane->get_normal() : -m_parent.get_camera().get_dir_forward());

    const Vec3d& center = m_c->m_model_object->instances[m_c->m_active_instance]->get_offset() + Vec3d(0., 0., m_z_shift);
    float dist = normal.dot(center);
    *m_c->m_clipping_plane = ClippingPlane(normal, (dist - (-m_c->m_active_instance_bb_radius) - m_c->m_clipping_plane_distance * 2*m_c->m_active_instance_bb_radius));
    m_parent.set_as_dirty();
}


void GLGizmoHollow::on_set_hover_id()
{
    if (int(m_c->m_model_object->sla_drain_holes.size()) <= m_hover_id)
        m_hover_id = -1;
}




} // namespace GUI
} // namespace Slic3r

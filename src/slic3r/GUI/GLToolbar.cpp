#include "libslic3r/Point.hpp"
#include "libslic3r/libslic3r.h"

#include "GLToolbar.hpp"

#include "../../slic3r/GUI/GLCanvas3D.hpp"

#include <GL/glew.h>

#include <wx/event.h>
#include <wx/bitmap.h>
#include <wx/dcmemory.h>
#include <wx/settings.h>
#include <wx/glcanvas.h>

namespace Slic3r {
namespace GUI {


wxDEFINE_EVENT(EVT_GLTOOLBAR_ADD, SimpleEvent);
wxDEFINE_EVENT(EVT_GLTOOLBAR_DELETE, SimpleEvent);
wxDEFINE_EVENT(EVT_GLTOOLBAR_DELETE_ALL, SimpleEvent);
wxDEFINE_EVENT(EVT_GLTOOLBAR_ARRANGE, SimpleEvent);
wxDEFINE_EVENT(EVT_GLTOOLBAR_COPY, SimpleEvent);
wxDEFINE_EVENT(EVT_GLTOOLBAR_PASTE, SimpleEvent);
wxDEFINE_EVENT(EVT_GLTOOLBAR_MORE, SimpleEvent);
wxDEFINE_EVENT(EVT_GLTOOLBAR_FEWER, SimpleEvent);
wxDEFINE_EVENT(EVT_GLTOOLBAR_SPLIT_OBJECTS, SimpleEvent);
wxDEFINE_EVENT(EVT_GLTOOLBAR_SPLIT_VOLUMES, SimpleEvent);
wxDEFINE_EVENT(EVT_GLTOOLBAR_LAYERSEDITING, SimpleEvent);

wxDEFINE_EVENT(EVT_GLVIEWTOOLBAR_3D, SimpleEvent);
wxDEFINE_EVENT(EVT_GLVIEWTOOLBAR_PREVIEW, SimpleEvent);

const GLToolbarItem::ActionCallback GLToolbarItem::Default_Action_Callback = [](){};
const GLToolbarItem::VisibilityCallback GLToolbarItem::Default_Visibility_Callback = []()->bool { return true; };
const GLToolbarItem::EnablingCallback GLToolbarItem::Default_Enabling_Callback = []()->bool { return true; };
const GLToolbarItem::RenderCallback GLToolbarItem::Default_Render_Callback = [](float, float, float, float){};

GLToolbarItem::Data::Option::Option()
    : toggable(false)
    , action_callback(Default_Action_Callback)
    , render_callback(nullptr)
{
}

GLToolbarItem::Data::Data()
    : name("")
    , icon_filename("")
    , tooltip("")
    , additional_tooltip("")
    , sprite_id(-1)
    , visible(true)
    , visibility_callback(Default_Visibility_Callback)
    , enabling_callback(Default_Enabling_Callback)
{
}

GLToolbarItem::GLToolbarItem(GLToolbarItem::EType type, const GLToolbarItem::Data& data)
    : m_type(type)
    , m_state(Normal)
    , m_data(data)
    , m_last_action_type(Undefined)
{
}

bool GLToolbarItem::update_visibility()
{
    bool visible = m_data.visibility_callback();
    bool ret = (m_data.visible != visible);
    if (ret)
        m_data.visible = visible;

    return ret;
}

bool GLToolbarItem::update_enabled_state()
{
    bool enabled = m_data.enabling_callback();
    bool ret = (is_enabled() != enabled);
    if (ret)
        m_state = enabled ? GLToolbarItem::Normal : GLToolbarItem::Disabled;

    return ret;
}

void GLToolbarItem::render(unsigned int tex_id, float left, float right, float bottom, float top, unsigned int tex_width, unsigned int tex_height, unsigned int icon_size) const
{
    GLTexture::render_sub_texture(tex_id, left, right, bottom, top, get_uvs(tex_width, tex_height, icon_size));

    if (is_pressed())
    {
        if ((m_last_action_type == Left) && m_data.left.can_render())
            m_data.left.render_callback(left, right, bottom, top);
        else if ((m_last_action_type == Right) && m_data.right.can_render())
            m_data.right.render_callback(left, right, bottom, top);
    }
}

GLTexture::Quad_UVs GLToolbarItem::get_uvs(unsigned int tex_width, unsigned int tex_height, unsigned int icon_size) const
{
    GLTexture::Quad_UVs uvs;

    float inv_tex_width = (tex_width != 0) ? 1.0f / (float)tex_width : 0.0f;
    float inv_tex_height = (tex_height != 0) ? 1.0f / (float)tex_height : 0.0f;

    float scaled_icon_width = (float)icon_size * inv_tex_width;
    float scaled_icon_height = (float)icon_size * inv_tex_height;
    float left = (float)m_state * scaled_icon_width;
    float right = left + scaled_icon_width;
    float top = (float)m_data.sprite_id * scaled_icon_height;
    float bottom = top + scaled_icon_height;

    uvs.left_top = { left, top };
    uvs.left_bottom = { left, bottom };
    uvs.right_bottom = { right, bottom };
    uvs.right_top = { right, top };
    
    return uvs;
}

BackgroundTexture::Metadata::Metadata()
    : filename("")
    , left(0)
    , right(0)
    , top(0)
    , bottom(0)
{
}

const float GLToolbar::Default_Icons_Size = 40.0f;

GLToolbar::Layout::Layout()
    : type(Horizontal)
    , horizontal_orientation(HO_Center)
    , vertical_orientation(VO_Center)
    , top(0.0f)
    , left(0.0f)
    , border(0.0f)
    , separator_size(0.0f)
    , gap_size(0.0f)
    , icons_size(Default_Icons_Size)
    , scale(1.0f)
    , width(0.0f)
    , height(0.0f)
    , dirty(true)
{
}

GLToolbar::GLToolbar(GLToolbar::EType type, const std::string& name)
    : m_type(type)
    , m_name(name)
    , m_enabled(false)
    , m_icons_texture_dirty(true)
    , m_tooltip("")
    , m_pressed_toggable_id(-1)
{
}

GLToolbar::~GLToolbar()
{
    for (GLToolbarItem* item : m_items)
    {
        delete item;
    }
}

bool GLToolbar::init(const BackgroundTexture::Metadata& background_texture)
{
    if (m_background_texture.texture.get_id() != 0)
        return true;

    std::string path = resources_dir() + "/icons/";
    bool res = false;

    if (!background_texture.filename.empty())
        res = m_background_texture.texture.load_from_file(path + background_texture.filename, false, GLTexture::SingleThreaded, false);

    if (res)
        m_background_texture.metadata = background_texture;

    return res;
}

GLToolbar::Layout::EType GLToolbar::get_layout_type() const
{
    return m_layout.type;
}

void GLToolbar::set_layout_type(GLToolbar::Layout::EType type)
{
    m_layout.type = type;
    m_layout.dirty = true;
}

void GLToolbar::set_position(float top, float left)
{
    m_layout.top = top;
    m_layout.left = left;
}

void GLToolbar::set_border(float border)
{
    m_layout.border = border;
    m_layout.dirty = true;
}

void GLToolbar::set_separator_size(float size)
{
    m_layout.separator_size = size;
    m_layout.dirty = true;
}

void GLToolbar::set_gap_size(float size)
{
    m_layout.gap_size = size;
    m_layout.dirty = true;
}

void GLToolbar::set_icons_size(float size)
{
    if (m_layout.icons_size != size)
    {
        m_layout.icons_size = size;
        m_layout.dirty = true;
        m_icons_texture_dirty = true;
    }
}

void GLToolbar::set_scale(float scale)
{
    if (m_layout.scale != scale)
    {
        m_layout.scale = scale;
        m_layout.dirty = true;
        m_icons_texture_dirty = true;
    }
}

bool GLToolbar::is_enabled() const
{
    return m_enabled;
}

void GLToolbar::set_enabled(bool enable)
{
    m_enabled = true;
}

bool GLToolbar::add_item(const GLToolbarItem::Data& data)
{
    GLToolbarItem* item = new GLToolbarItem(GLToolbarItem::Action, data);
    if (item == nullptr)
        return false;

    m_items.push_back(item);
    m_layout.dirty = true;
    return true;
}

bool GLToolbar::add_separator()
{
    GLToolbarItem::Data data;
    GLToolbarItem* item = new GLToolbarItem(GLToolbarItem::Separator, data);
    if (item == nullptr)
        return false;

    m_items.push_back(item);
    m_layout.dirty = true;
    return true;
}

float GLToolbar::get_width() const
{
    if (m_layout.dirty)
        calc_layout();

    return m_layout.width;
}

float GLToolbar::get_height() const
{
    if (m_layout.dirty)
        calc_layout();

    return m_layout.height;
}

void GLToolbar::select_item(const std::string& name)
{
    if (is_item_disabled(name))
        return;

    for (GLToolbarItem* item : m_items)
    {
        if (!item->is_disabled())
        {
            bool hover = item->is_hovered();
            item->set_state((item->get_name() == name) ? (hover ? GLToolbarItem::HoverPressed : GLToolbarItem::Pressed) : (hover ? GLToolbarItem::Hover : GLToolbarItem::Normal));
        }
    }
}

bool GLToolbar::is_item_pressed(const std::string& name) const
{
    for (const GLToolbarItem* item : m_items)
    {
        if (item->get_name() == name)
            return item->is_pressed();
    }

    return false;
}

bool GLToolbar::is_item_disabled(const std::string& name) const
{
    for (const GLToolbarItem* item : m_items)
    {
        if (item->get_name() == name)
            return item->is_disabled();
    }

    return false;
}

bool GLToolbar::is_item_visible(const std::string& name) const
{
    for (const GLToolbarItem* item : m_items)
    {
        if (item->get_name() == name)
            return item->is_visible();
    }

    return false;
}

bool GLToolbar::is_any_item_pressed() const
{
    for (const GLToolbarItem* item : m_items)
    {
        if (item->is_pressed())
            return true;
    }

    return false;
}

int GLToolbar::get_item_id(const std::string& name) const
{
    for (int i = 0; i < (int)m_items.size(); ++i)
    {
        if (m_items[i]->get_name() == name)
            return i;
    }

    return -1;
}

void GLToolbar::get_additional_tooltip(int item_id, std::string& text)
{
    if ((0 <= item_id) && (item_id < (int)m_items.size()))
    {
        GLToolbarItem* item = m_items[item_id];
        if (item != nullptr)
        {
            text = item->get_additional_tooltip();
            return;
        }
    }

    text.clear();
}

void GLToolbar::set_additional_tooltip(int item_id, const std::string& text)
{
    if ((0 <= item_id) && (item_id < (int)m_items.size()))
    {
        GLToolbarItem* item = m_items[item_id];
        if (item != nullptr)
            item->set_additional_tooltip(text);
    }
}

bool GLToolbar::update_items_state()
{
    bool ret = false;
    ret |= update_items_visibility();
    ret |= update_items_enabled_state();
    if (!is_any_item_pressed())
        m_pressed_toggable_id = -1;

    return ret;
}

void GLToolbar::render(const GLCanvas3D& parent) const
{
    if (!m_enabled || m_items.empty())
        return;

    if (m_icons_texture_dirty)
        generate_icons_texture();

    switch (m_layout.type)
    {
    default:
    case Layout::Horizontal: { render_horizontal(parent); break; }
    case Layout::Vertical: { render_vertical(parent); break; }
    }
}

bool GLToolbar::on_mouse(wxMouseEvent& evt, GLCanvas3D& parent)
{
    if (!m_enabled)
        return false;

    Vec2d mouse_pos((double)evt.GetX(), (double)evt.GetY());
    bool processed = false;

    // mouse anywhere
    if (!evt.Dragging() && !evt.Leaving() && !evt.Entering() && (m_mouse_capture.parent != nullptr))
    {
        if (m_mouse_capture.any() && (evt.LeftUp() || evt.MiddleUp() || evt.RightUp()))
            // prevents loosing selection into the scene if mouse down was done inside the toolbar and mouse up was down outside it,
            // as when switching between views
            processed = true;

        m_mouse_capture.reset();
    }

    if (evt.Moving())
        m_tooltip = update_hover_state(mouse_pos, parent);
    else if (evt.LeftUp())
        m_mouse_capture.left = false;
    else if (evt.MiddleUp())
        m_mouse_capture.middle = false;
    else if (evt.RightUp())
        m_mouse_capture.right = false;
    else if (evt.Dragging() && m_mouse_capture.any())
        // if the button down was done on this toolbar, prevent from dragging into the scene
        processed = true;

    int item_id = contains_mouse(mouse_pos, parent);
    if (item_id == -1)
    {
        // mouse is outside the toolbar
        m_tooltip.clear();
    }
    else
    {
        // mouse inside toolbar
        if (evt.LeftDown() || evt.LeftDClick())
        {
            m_mouse_capture.left = true;
            m_mouse_capture.parent = &parent;
            processed = true;
            if ((item_id != -2) && !m_items[item_id]->is_separator() && ((m_pressed_toggable_id == -1) || (m_items[item_id]->get_last_action_type() == GLToolbarItem::Left)))
            {
                // mouse is inside an icon
                do_action(GLToolbarItem::Left, item_id, parent, true);
                parent.set_as_dirty();
            }
        }
        else if (evt.MiddleDown())
        {
            m_mouse_capture.middle = true;
            m_mouse_capture.parent = &parent;
        }
        else if (evt.RightDown())
        {
            m_mouse_capture.right = true;
            m_mouse_capture.parent = &parent;
            processed = true;
            if ((item_id != -2) && !m_items[item_id]->is_separator() && ((m_pressed_toggable_id == -1) || (m_items[item_id]->get_last_action_type() == GLToolbarItem::Right)))
            {
                // mouse is inside an icon
                do_action(GLToolbarItem::Right, item_id, parent, true);
                parent.set_as_dirty();
            }
        }
        else if (evt.LeftUp())
            processed = true;
    }

    return processed;
}

void GLToolbar::calc_layout() const
{
    switch (m_layout.type)
    {
    default:
    case Layout::Horizontal:
    {
        m_layout.width = get_width_horizontal();
        m_layout.height = get_height_horizontal();
        break;
    }
    case Layout::Vertical:
    {
        m_layout.width = get_width_vertical();
        m_layout.height = get_height_vertical();
        break;
    }
    }

    m_layout.dirty = false;
}

float GLToolbar::get_width_horizontal() const
{
    return get_main_size();
}

float GLToolbar::get_width_vertical() const
{
    return (2.0f * m_layout.border + m_layout.icons_size) * m_layout.scale;
}

float GLToolbar::get_height_horizontal() const
{
    return (2.0f * m_layout.border + m_layout.icons_size) * m_layout.scale;
}

float GLToolbar::get_height_vertical() const
{
    return get_main_size();
}

float GLToolbar::get_main_size() const
{
    float size = 2.0f * m_layout.border;
    for (unsigned int i = 0; i < (unsigned int)m_items.size(); ++i)
    {
        if (!m_items[i]->is_visible())
            continue;

        if (m_items[i]->is_separator())
            size += m_layout.separator_size;
        else
            size += (float)m_layout.icons_size;
    }

    if (m_items.size() > 1)
        size += ((float)m_items.size() - 1.0f) * m_layout.gap_size;

    return size * m_layout.scale;
}

void GLToolbar::do_action(GLToolbarItem::EActionType type, int item_id, GLCanvas3D& parent, bool check_hover)
{
    if ((m_pressed_toggable_id == -1) || (m_pressed_toggable_id == item_id))
    {
        if ((0 <= item_id) && (item_id < (int)m_items.size()))
        {
            GLToolbarItem* item = m_items[item_id];
            if ((item != nullptr) && !item->is_separator() && (!check_hover || item->is_hovered()))
            {
                if (((type == GLToolbarItem::Right) && item->is_right_toggable()) ||
                    ((type == GLToolbarItem::Left) && item->is_left_toggable()))
                {
                    GLToolbarItem::EState state = item->get_state();
                    if (state == GLToolbarItem::Hover)
                        item->set_state(GLToolbarItem::HoverPressed);
                    else if (state == GLToolbarItem::HoverPressed)
                        item->set_state(GLToolbarItem::Hover);
                    else if (state == GLToolbarItem::Pressed)
                        item->set_state(GLToolbarItem::Normal);
                    else if (state == GLToolbarItem::Normal)
                        item->set_state(GLToolbarItem::Pressed);

                    m_pressed_toggable_id = item->is_pressed() ? item_id : -1;
                    item->reset_last_action_type();

                    parent.render();
                    switch (type)
                    {
                    default:
                    case GLToolbarItem::Left: { item->do_left_action(); break; }
                    case GLToolbarItem::Right: { item->do_right_action(); break; }
                    }
                }
                else
                {
                    if (m_type == Radio)
                        select_item(item->get_name());
                    else
                        item->set_state(item->is_hovered() ? GLToolbarItem::HoverPressed : GLToolbarItem::Pressed);

                    item->reset_last_action_type();
                    parent.render();
                    switch (type)
                    {
                    default:
                    case GLToolbarItem::Left: { item->do_left_action(); break; }
                    case GLToolbarItem::Right: { item->do_right_action(); break; }
                    }

                    if ((m_type == Normal) && (item->get_state() != GLToolbarItem::Disabled))
                    {
                        // the item may get disabled during the action, if not, set it back to hover state
                        item->set_state(GLToolbarItem::Hover);
                        parent.render();
                    }
                }
            }
        }
    }
}

std::string GLToolbar::update_hover_state(const Vec2d& mouse_pos, GLCanvas3D& parent)
{
    if (!m_enabled)
        return "";

    switch (m_layout.type)
    {
    default:
    case Layout::Horizontal: { return update_hover_state_horizontal(mouse_pos, parent); }
    case Layout::Vertical: { return update_hover_state_vertical(mouse_pos, parent); }
    }
}

std::string GLToolbar::update_hover_state_horizontal(const Vec2d& mouse_pos, GLCanvas3D& parent)
{
    // NB: mouse_pos is already scaled appropriately

    float zoom = (float)parent.get_camera().get_zoom();
    float inv_zoom = (zoom != 0.0f) ? 1.0f / zoom : 0.0f;
    float factor = m_layout.scale * inv_zoom;

    Size cnv_size = parent.get_canvas_size();
    Vec2d scaled_mouse_pos((mouse_pos(0) - 0.5 * (double)cnv_size.get_width()) * inv_zoom, (0.5 * (double)cnv_size.get_height() - mouse_pos(1)) * inv_zoom);

    float scaled_icons_size = m_layout.icons_size * factor;
    float scaled_separator_size = m_layout.separator_size * factor;
    float scaled_gap_size = m_layout.gap_size * factor;
    float scaled_border = m_layout.border * factor;

    float separator_stride = scaled_separator_size + scaled_gap_size;
    float icon_stride = scaled_icons_size + scaled_gap_size;

    float left = m_layout.left + scaled_border;
    float top = m_layout.top - scaled_border;

    std::string tooltip = "";
        
    for (GLToolbarItem* item : m_items)
    {
        if (!item->is_visible())
            continue;

        if (item->is_separator())
            left += separator_stride;
        else
        {
            float right = left + scaled_icons_size;
            float bottom = top - scaled_icons_size;

            GLToolbarItem::EState state = item->get_state();
            bool inside = (left <= (float)scaled_mouse_pos(0)) && ((float)scaled_mouse_pos(0) <= right) && (bottom <= (float)scaled_mouse_pos(1)) && ((float)scaled_mouse_pos(1) <= top);
            if (inside)
            {
                tooltip = item->get_tooltip();
                if (!item->is_pressed())
                {
                    const std::string& additional_tooltip = item->get_additional_tooltip();
                    if (!additional_tooltip.empty())
                        tooltip += "\n" + additional_tooltip;
                }
            }

            switch (state)
            {
            case GLToolbarItem::Normal:
            {
                if (inside)
                {
                    item->set_state(GLToolbarItem::Hover);
                    parent.set_as_dirty();
                }

                break;
            }
            case GLToolbarItem::Hover:
            {
                if (!inside)
                {
                    item->set_state(GLToolbarItem::Normal);
                    parent.set_as_dirty();
                }

                break;
            }
            case GLToolbarItem::Pressed:
            {
                if (inside)
                {
                    item->set_state(GLToolbarItem::HoverPressed);
                    parent.set_as_dirty();
                }

                break;
            }
            case GLToolbarItem::HoverPressed:
            {
                if (!inside)
                {
                    item->set_state(GLToolbarItem::Pressed);
                    parent.set_as_dirty();
                }

                break;
            }
            default:
            case GLToolbarItem::Disabled:
            {
                break;
            }
            }

            left += icon_stride;
        }
    }

    return tooltip;
}

std::string GLToolbar::update_hover_state_vertical(const Vec2d& mouse_pos, GLCanvas3D& parent)
{
    // NB: mouse_pos is already scaled appropriately

    float zoom = (float)parent.get_camera().get_zoom();
    float inv_zoom = (zoom != 0.0f) ? 1.0f / zoom : 0.0f;
    float factor = m_layout.scale * inv_zoom;

    Size cnv_size = parent.get_canvas_size();
    Vec2d scaled_mouse_pos((mouse_pos(0) - 0.5 * (double)cnv_size.get_width()) * inv_zoom, (0.5 * (double)cnv_size.get_height() - mouse_pos(1)) * inv_zoom);

    float scaled_icons_size = m_layout.icons_size * factor;
    float scaled_separator_size = m_layout.separator_size * factor;
    float scaled_gap_size = m_layout.gap_size * factor;
    float scaled_border = m_layout.border * factor;
    float separator_stride = scaled_separator_size + scaled_gap_size;
    float icon_stride = scaled_icons_size + scaled_gap_size;

    float left = m_layout.left + scaled_border;
    float top = m_layout.top - scaled_border;

    std::string tooltip = "";

    for (GLToolbarItem* item : m_items)
    {
        if (!item->is_visible())
            continue;

        if (item->is_separator())
            top -= separator_stride;
        else
        {
            float right = left + scaled_icons_size;
            float bottom = top - scaled_icons_size;

            GLToolbarItem::EState state = item->get_state();
            bool inside = (left <= (float)scaled_mouse_pos(0)) && ((float)scaled_mouse_pos(0) <= right) && (bottom <= (float)scaled_mouse_pos(1)) && ((float)scaled_mouse_pos(1) <= top);
            if (inside)
            {
                tooltip = item->get_tooltip();
                if (!item->is_pressed())
                {
                    const std::string& additional_tooltip = item->get_additional_tooltip();
                    if (!additional_tooltip.empty())
                        tooltip += "\n" + additional_tooltip;
                }
            }

            switch (state)
            {
            case GLToolbarItem::Normal:
            {
                if (inside)
                {
                    item->set_state(GLToolbarItem::Hover);
                    parent.set_as_dirty();
                }

                break;
            }
            case GLToolbarItem::Hover:
            {
                if (!inside)
                {
                    item->set_state(GLToolbarItem::Normal);
                    parent.set_as_dirty();
                }

                break;
            }
            case GLToolbarItem::Pressed:
            {
                if (inside)
                {
                    item->set_state(GLToolbarItem::HoverPressed);
                    parent.set_as_dirty();
                }

                break;
            }
            case GLToolbarItem::HoverPressed:
            {
                if (!inside)
                {
                    item->set_state(GLToolbarItem::Pressed);
                    parent.set_as_dirty();
                }

                break;
            }
            default:
            case GLToolbarItem::Disabled:
            {
                break;
            }
            }

            top -= icon_stride;
        }
    }

    return tooltip;
}

int GLToolbar::contains_mouse(const Vec2d& mouse_pos, const GLCanvas3D& parent) const
{
    if (!m_enabled)
        return -1;

    switch (m_layout.type)
    {
    default:
    case Layout::Horizontal: { return contains_mouse_horizontal(mouse_pos, parent); }
    case Layout::Vertical: { return contains_mouse_vertical(mouse_pos, parent); }
    }
}

int GLToolbar::contains_mouse_horizontal(const Vec2d& mouse_pos, const GLCanvas3D& parent) const
{
    // NB: mouse_pos is already scaled appropriately

    float zoom = (float)parent.get_camera().get_zoom();
    float inv_zoom = (zoom != 0.0f) ? 1.0f / zoom : 0.0f;
    float factor = m_layout.scale * inv_zoom;

    Size cnv_size = parent.get_canvas_size();
    Vec2d scaled_mouse_pos((mouse_pos(0) - 0.5 * (double)cnv_size.get_width()) * inv_zoom, (0.5 * (double)cnv_size.get_height() - mouse_pos(1)) * inv_zoom);

    float scaled_icons_size = m_layout.icons_size * factor;
    float scaled_separator_size = m_layout.separator_size * factor;
    float scaled_gap_size = m_layout.gap_size * factor;
    float scaled_border = m_layout.border * factor;

    float left = m_layout.left + scaled_border;
    float top = m_layout.top - scaled_border;


    for (size_t id=0; id<m_items.size(); ++id)
    {
        GLToolbarItem* item = m_items[id];
        
        if (!item->is_visible())
            continue;

        if (item->is_separator())
        {
            float right = left + scaled_separator_size;
            float bottom = top - scaled_icons_size;

            // mouse inside the separator
            if ((left <= (float)scaled_mouse_pos(0)) && ((float)scaled_mouse_pos(0) <= right) && (bottom <= (float)scaled_mouse_pos(1)) && ((float)scaled_mouse_pos(1) <= top))
                return id;

            left = right;
            right += scaled_gap_size;

            if (id < m_items.size() - 1)
            {
                // mouse inside the gap
                if ((left <= (float)scaled_mouse_pos(0)) && ((float)scaled_mouse_pos(0) <= right) && (bottom <= (float)scaled_mouse_pos(1)) && ((float)scaled_mouse_pos(1) <= top))
                    return -2;
            }

            left = right;
        }
        else
        {
            float right = left + scaled_icons_size;
            float bottom = top - scaled_icons_size;

            // mouse inside the icon
            if ((left <= (float)scaled_mouse_pos(0)) && ((float)scaled_mouse_pos(0) <= right) && (bottom <= (float)scaled_mouse_pos(1)) && ((float)scaled_mouse_pos(1) <= top))
                return id;
            
            left = right;
            right += scaled_gap_size;

            if (id < m_items.size() - 1)
            {
                // mouse inside the gap
                if ((left <= (float)scaled_mouse_pos(0)) && ((float)scaled_mouse_pos(0) <= right) && (bottom <= (float)scaled_mouse_pos(1)) && ((float)scaled_mouse_pos(1) <= top))
                    return -2;
            }

            left = right;
        }
    }
    
    return -1;
}

int GLToolbar::contains_mouse_vertical(const Vec2d& mouse_pos, const GLCanvas3D& parent) const
{
    // NB: mouse_pos is already scaled appropriately

    float zoom = (float)parent.get_camera().get_zoom();
    float inv_zoom = (zoom != 0.0f) ? 1.0f / zoom : 0.0f;
    float factor = m_layout.scale * inv_zoom;

    Size cnv_size = parent.get_canvas_size();
    Vec2d scaled_mouse_pos((mouse_pos(0) - 0.5 * (double)cnv_size.get_width()) * inv_zoom, (0.5 * (double)cnv_size.get_height() - mouse_pos(1)) * inv_zoom);

    float scaled_icons_size = m_layout.icons_size * factor;
    float scaled_separator_size = m_layout.separator_size * factor;
    float scaled_gap_size = m_layout.gap_size * factor;
    float scaled_border = m_layout.border * factor;

    float left = m_layout.left + scaled_border;
    float top = m_layout.top - scaled_border;

    for (size_t id=0; id<m_items.size(); ++id)
    {
        GLToolbarItem* item = m_items[id];

        if (!item->is_visible())
            continue;

        if (item->is_separator())
        {
            float right = left + scaled_icons_size;
            float bottom = top - scaled_separator_size;

            // mouse inside the separator
            if ((left <= (float)scaled_mouse_pos(0)) && ((float)scaled_mouse_pos(0) <= right) && (bottom <= (float)scaled_mouse_pos(1)) && ((float)scaled_mouse_pos(1) <= top))
                return id;

            top = bottom;
            bottom -= scaled_gap_size;

            if (id < m_items.size() - 1)
            {
                // mouse inside the gap
                if ((left <= (float)scaled_mouse_pos(0)) && ((float)scaled_mouse_pos(0) <= right) && (bottom <= (float)scaled_mouse_pos(1)) && ((float)scaled_mouse_pos(1) <= top))
                    return -2;
            }

            top = bottom;
        }
        else
        {
            float right = left + scaled_icons_size;
            float bottom = top - scaled_icons_size;

            // mouse inside the icon
            if ((left <= (float)scaled_mouse_pos(0)) && ((float)scaled_mouse_pos(0) <= right) && (bottom <= (float)scaled_mouse_pos(1)) && ((float)scaled_mouse_pos(1) <= top))
                return id;

            top = bottom;
            bottom -= scaled_gap_size;

            if (id < m_items.size() - 1)
            {
                // mouse inside the gap
                if ((left <= (float)scaled_mouse_pos(0)) && ((float)scaled_mouse_pos(0) <= right) && (bottom <= (float)scaled_mouse_pos(1)) && ((float)scaled_mouse_pos(1) <= top))
                    return -2;
            }

            top = bottom;
        }
    }

    return -1;
}

void GLToolbar::render_background(float left, float top, float right, float bottom, float border) const
{
    unsigned int tex_id = m_background_texture.texture.get_id();
    float tex_width = (float)m_background_texture.texture.get_width();
    float tex_height = (float)m_background_texture.texture.get_height();
    if ((tex_id != 0) && (tex_width > 0) && (tex_height > 0))
    {
        float inv_tex_width = (tex_width != 0.0f) ? 1.0f / tex_width : 0.0f;
        float inv_tex_height = (tex_height != 0.0f) ? 1.0f / tex_height : 0.0f;

        float internal_left = left + border;
        float internal_right = right - border;
        float internal_top = top - border;
        float internal_bottom = bottom + border;

        float left_uv = 0.0f;
        float right_uv = 1.0f;
        float top_uv = 1.0f;
        float bottom_uv = 0.0f;

        float internal_left_uv = (float)m_background_texture.metadata.left * inv_tex_width;
        float internal_right_uv = 1.0f - (float)m_background_texture.metadata.right * inv_tex_width;
        float internal_top_uv = 1.0f - (float)m_background_texture.metadata.top * inv_tex_height;
        float internal_bottom_uv = (float)m_background_texture.metadata.bottom * inv_tex_height;

        // top-left corner
        if ((m_layout.horizontal_orientation == Layout::HO_Left) || (m_layout.vertical_orientation == Layout::VO_Top))
            GLTexture::render_sub_texture(tex_id, left, internal_left, internal_top, top, { { internal_left_uv, internal_bottom_uv }, { internal_right_uv, internal_bottom_uv }, { internal_right_uv, internal_top_uv }, { internal_left_uv, internal_top_uv } });
        else
            GLTexture::render_sub_texture(tex_id, left, internal_left, internal_top, top, { { left_uv, internal_top_uv }, { internal_left_uv, internal_top_uv }, { internal_left_uv, top_uv }, { left_uv, top_uv } });

        // top edge
        if (m_layout.vertical_orientation == Layout::VO_Top)
            GLTexture::render_sub_texture(tex_id, internal_left, internal_right, internal_top, top, { { internal_left_uv, internal_bottom_uv }, { internal_right_uv, internal_bottom_uv }, { internal_right_uv, internal_top_uv }, { internal_left_uv, internal_top_uv } });
        else
            GLTexture::render_sub_texture(tex_id, internal_left, internal_right, internal_top, top, { { internal_left_uv, internal_top_uv }, { internal_right_uv, internal_top_uv }, { internal_right_uv, top_uv }, { internal_left_uv, top_uv } });

        // top-right corner
        if ((m_layout.horizontal_orientation == Layout::HO_Right) || (m_layout.vertical_orientation == Layout::VO_Top))
            GLTexture::render_sub_texture(tex_id, internal_right, right, internal_top, top, { { internal_left_uv, internal_bottom_uv }, { internal_right_uv, internal_bottom_uv }, { internal_right_uv, internal_top_uv }, { internal_left_uv, internal_top_uv } });
        else
            GLTexture::render_sub_texture(tex_id, internal_right, right, internal_top, top, { { internal_right_uv, internal_top_uv }, { right_uv, internal_top_uv }, { right_uv, top_uv }, { internal_right_uv, top_uv } });

        // center-left edge
        if (m_layout.horizontal_orientation == Layout::HO_Left)
            GLTexture::render_sub_texture(tex_id, left, internal_left, internal_bottom, internal_top, { { internal_left_uv, internal_bottom_uv }, { internal_right_uv, internal_bottom_uv }, { internal_right_uv, internal_top_uv }, { internal_left_uv, internal_top_uv } });
        else
            GLTexture::render_sub_texture(tex_id, left, internal_left, internal_bottom, internal_top, { { left_uv, internal_bottom_uv }, { internal_left_uv, internal_bottom_uv }, { internal_left_uv, internal_top_uv }, { left_uv, internal_top_uv } });

        // center
        GLTexture::render_sub_texture(tex_id, internal_left, internal_right, internal_bottom, internal_top, { { internal_left_uv, internal_bottom_uv }, { internal_right_uv, internal_bottom_uv }, { internal_right_uv, internal_top_uv }, { internal_left_uv, internal_top_uv } });

        // center-right edge
        if (m_layout.horizontal_orientation == Layout::HO_Right)
            GLTexture::render_sub_texture(tex_id, internal_right, right, internal_bottom, internal_top, { { internal_left_uv, internal_bottom_uv }, { internal_right_uv, internal_bottom_uv }, { internal_right_uv, internal_top_uv }, { internal_left_uv, internal_top_uv } });
        else
            GLTexture::render_sub_texture(tex_id, internal_right, right, internal_bottom, internal_top, { { internal_right_uv, internal_bottom_uv }, { right_uv, internal_bottom_uv }, { right_uv, internal_top_uv }, { internal_right_uv, internal_top_uv } });

        // bottom-left corner
        if ((m_layout.horizontal_orientation == Layout::HO_Left) || (m_layout.vertical_orientation == Layout::VO_Bottom))
            GLTexture::render_sub_texture(tex_id, left, internal_left, bottom, internal_bottom, { { internal_left_uv, internal_bottom_uv }, { internal_right_uv, internal_bottom_uv }, { internal_right_uv, internal_top_uv }, { internal_left_uv, internal_top_uv } });
        else
            GLTexture::render_sub_texture(tex_id, left, internal_left, bottom, internal_bottom, { { left_uv, bottom_uv }, { internal_left_uv, bottom_uv }, { internal_left_uv, internal_bottom_uv }, { left_uv, internal_bottom_uv } });

        // bottom edge
        if (m_layout.vertical_orientation == Layout::VO_Bottom)
            GLTexture::render_sub_texture(tex_id, internal_left, internal_right, bottom, internal_bottom, { { internal_left_uv, internal_bottom_uv }, { internal_right_uv, internal_bottom_uv }, { internal_right_uv, internal_top_uv }, { internal_left_uv, internal_top_uv } });
        else
            GLTexture::render_sub_texture(tex_id, internal_left, internal_right, bottom, internal_bottom, { { internal_left_uv, bottom_uv }, { internal_right_uv, bottom_uv }, { internal_right_uv, internal_bottom_uv }, { internal_left_uv, internal_bottom_uv } });

        // bottom-right corner
        if ((m_layout.horizontal_orientation == Layout::HO_Right) || (m_layout.vertical_orientation == Layout::VO_Bottom))
            GLTexture::render_sub_texture(tex_id, internal_right, right, bottom, internal_bottom, { { internal_left_uv, internal_bottom_uv }, { internal_right_uv, internal_bottom_uv }, { internal_right_uv, internal_top_uv }, { internal_left_uv, internal_top_uv } });
        else
            GLTexture::render_sub_texture(tex_id, internal_right, right, bottom, internal_bottom, { { internal_right_uv, bottom_uv }, { right_uv, bottom_uv }, { right_uv, internal_bottom_uv }, { internal_right_uv, internal_bottom_uv } });
    }
}

void GLToolbar::render_horizontal(const GLCanvas3D& parent) const
{
    unsigned int tex_id = m_icons_texture.get_id();
    int tex_width = m_icons_texture.get_width();
    int tex_height = m_icons_texture.get_height();

    float zoom = (float)parent.get_camera().get_zoom();
    float inv_zoom = (zoom != 0.0f) ? 1.0f / zoom : 0.0f;
    float factor = inv_zoom * m_layout.scale;

    float scaled_icons_size = m_layout.icons_size * factor;
    float scaled_separator_size = m_layout.separator_size * factor;
    float scaled_gap_size = m_layout.gap_size * factor;
    float scaled_border = m_layout.border * factor;
    float scaled_width = get_width() * inv_zoom;
    float scaled_height = get_height() * inv_zoom;

    float separator_stride = scaled_separator_size + scaled_gap_size;
    float icon_stride = scaled_icons_size + scaled_gap_size;

    float left = m_layout.left;
    float top = m_layout.top;
    float right = left + scaled_width;
    float bottom = top - scaled_height;

    render_background(left, top, right, bottom, scaled_border);

    left += scaled_border;
    top -= scaled_border;

    if ((tex_id == 0) || (tex_width <= 0) || (tex_height <= 0))
        return;

    // renders icons
    for (const GLToolbarItem* item : m_items)
    {
        if (!item->is_visible())
            continue;

        if (item->is_separator())
            left += separator_stride;
        else
        {
            item->render(tex_id, left, left + scaled_icons_size, top - scaled_icons_size, top, (unsigned int)tex_width, (unsigned int)tex_height, (unsigned int)(m_layout.icons_size * m_layout.scale));
            left += icon_stride;
        }
    }
}

void GLToolbar::render_vertical(const GLCanvas3D& parent) const
{
    unsigned int tex_id = m_icons_texture.get_id();
    int tex_width = m_icons_texture.get_width();
    int tex_height = m_icons_texture.get_height();

    float zoom = (float)parent.get_camera().get_zoom();
    float inv_zoom = (zoom != 0.0f) ? 1.0f / zoom : 0.0f;
    float factor = inv_zoom * m_layout.scale;

    float scaled_icons_size = m_layout.icons_size * factor;
    float scaled_separator_size = m_layout.separator_size * factor;
    float scaled_gap_size = m_layout.gap_size * factor;
    float scaled_border = m_layout.border * factor;
    float scaled_width = get_width() * inv_zoom;
    float scaled_height = get_height() * inv_zoom;

    float separator_stride = scaled_separator_size + scaled_gap_size;
    float icon_stride = scaled_icons_size + scaled_gap_size;

    float left = m_layout.left;
    float top = m_layout.top;
    float right = left + scaled_width;
    float bottom = top - scaled_height;

    render_background(left, top, right, bottom, scaled_border);

    left += scaled_border;
    top -= scaled_border;

    if ((tex_id == 0) || (tex_width <= 0) || (tex_height <= 0))
        return;

    // renders icons
    for (const GLToolbarItem* item : m_items)
    {
        if (!item->is_visible())
            continue;

        if (item->is_separator())
            top -= separator_stride;
        else
        {
            item->render(tex_id, left, left + scaled_icons_size, top - scaled_icons_size, top, (unsigned int)tex_width, (unsigned int)tex_height, (unsigned int)(m_layout.icons_size * m_layout.scale));
            top -= icon_stride;
        }
    }
}

bool GLToolbar::generate_icons_texture() const
{
    std::string path = resources_dir() + "/icons/";
    std::vector<std::string> filenames;
    for (GLToolbarItem* item : m_items)
    {
        const std::string& icon_filename = item->get_icon_filename();
        if (!icon_filename.empty())
            filenames.push_back(path + icon_filename);
    }

    std::vector<std::pair<int, bool>> states;
    if (m_name == "Top")
    {
        states.push_back(std::make_pair(1, false));
        states.push_back(std::make_pair(0, false));
        states.push_back(std::make_pair(2, false));
        states.push_back(std::make_pair(0, false));
        states.push_back(std::make_pair(0, false));
    }
    else if (m_name == "View")
    {
        states.push_back(std::make_pair(1, false));
        states.push_back(std::make_pair(1, true));
        states.push_back(std::make_pair(1, false));
        states.push_back(std::make_pair(0, false));
        states.push_back(std::make_pair(1, true));
    }

    bool res = m_icons_texture.load_from_svg_files_as_sprites_array(filenames, states, (unsigned int)(m_layout.icons_size * m_layout.scale), true);
    if (res)
        m_icons_texture_dirty = false;

    return res;
}

bool GLToolbar::update_items_visibility()
{
    bool ret = false;

    for (GLToolbarItem* item : m_items)
    {
        ret |= item->update_visibility();
    }

    if (ret)
        m_layout.dirty = true;

    // updates separators visibility to avoid having two of them consecutive
    bool any_item_visible = false;
    for (GLToolbarItem* item : m_items)
    {
        if (!item->is_separator())
            any_item_visible |= item->is_visible();
        else
        {
            item->set_visible(any_item_visible);
            any_item_visible = false;
        }
    }

    return ret;
}

bool GLToolbar::update_items_enabled_state()
{
    bool ret = false;

    for (int i = 0; i < (int)m_items.size(); ++i)
    {
        GLToolbarItem* item = m_items[i];
        ret |= item->update_enabled_state();
        if (item->is_enabled() && (m_pressed_toggable_id != -1) && (m_pressed_toggable_id != i))
        {
            ret = true;
            item->set_state(GLToolbarItem::Disabled);
        }
    }

    if (ret)
        m_layout.dirty = true;

    return ret;
}

} // namespace GUI
} // namespace Slic3r

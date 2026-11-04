/*
 * Copyright (C) 2013-2025 Graeme Gott <graeme@gottcode.org>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this library.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "window.h"

#include "applications-page.h"
#include "category-button.h"
#include "command.h"
#include "favorites-page.h"
#include "launcher.h"
#include "launcher-view.h"
#include "plugin.h"
#include "profile.h"
#include "recent-page.h"
#include "resizer.h"
#include "search-page.h"
#include "settings.h"
#include "slot.h"

#include <libxfce4ui/libxfce4ui.h>
#include <gdk/gdkkeysyms.h>

#include <glib/gi18n-lib.h>

#ifdef HAVE_GTK_LAYER_SHELL
#include <gtk-layer-shell.h>
#endif

#include <ctime>

using namespace WhiskerMenu;

//-----------------------------------------------------------------------------

WhiskerMenu::Window::Window(Plugin* plugin) :
	m_plugin(plugin),
	m_window(nullptr),
	m_position(PositionAtButton),
	m_sidebar_strip(nullptr),
	m_apps_scroll(nullptr),
	m_apps_list(nullptr),
	m_sidebar_size_group(nullptr),
	m_geometry{0,0,1,1},
	m_layout_ltr(true),
	m_layout_categories_horizontal(false),
	m_layout_categories_alternate(false),
	m_layout_search_alternate(false),
	m_layout_commands_alternate(false),
	m_layout_profile_alternate(false),
	m_profile_shape(0),
	m_supports_alpha(false),
	m_child_has_focus(false),
	m_resizing(false)
{
	// Create the window
	m_window = GTK_WINDOW(gtk_window_new(GTK_WINDOW_TOPLEVEL));
	gtk_widget_set_name(GTK_WIDGET(m_window), "whiskermenu-window");
	// Untranslated window title to allow window managers to identify it; not visible to users.
	gtk_window_set_title(m_window, "Whisker Menu");
#ifdef HAVE_GTK_LAYER_SHELL
	if (!gtk_layer_is_supported())
#endif
	{
		gtk_window_set_modal(m_window, true);
	}
	gtk_window_set_decorated(m_window, false);
	gtk_window_set_skip_taskbar_hint(m_window, true);
	gtk_window_set_skip_pager_hint(m_window, true);
	gtk_window_set_type_hint(m_window, GDK_WINDOW_TYPE_HINT_MENU);
	gtk_window_stick(m_window);
	gtk_widget_add_events(GTK_WIDGET(m_window), GDK_FOCUS_CHANGE_MASK | GDK_STRUCTURE_MASK);

#ifdef HAVE_GTK_LAYER_SHELL
	if (gtk_layer_is_supported())
	{
		gtk_layer_init_for_window(m_window);

		// Position from top left, and excludes other windows
		gtk_layer_set_exclusive_zone(m_window, -1);
		gtk_layer_set_anchor(m_window, GTK_LAYER_SHELL_EDGE_TOP, true);
		gtk_layer_set_anchor(m_window, GTK_LAYER_SHELL_EDGE_BOTTOM, false);
		gtk_layer_set_anchor(m_window, GTK_LAYER_SHELL_EDGE_LEFT, true);
		gtk_layer_set_anchor(m_window, GTK_LAYER_SHELL_EDGE_RIGHT, false);

		// Grab keyboard focus when shown
		gtk_layer_set_keyboard_mode(m_window, GTK_LAYER_SHELL_KEYBOARD_MODE_ON_DEMAND);

		// Position menu above other windows
		gtk_layer_set_layer(m_window, GTK_LAYER_SHELL_LAYER_OVERLAY);
	}
#endif

	connect(m_window, "enter-notify-event",
		[this](GtkWidget*, GdkEvent*) -> gboolean
		{
			m_child_has_focus = false;
			return GDK_EVENT_PROPAGATE;
		});

	connect(m_window, "focus-in-event",
		[this](GtkWidget*, GdkEvent*) -> gboolean
		{
			m_child_has_focus = false;
			return GDK_EVENT_PROPAGATE;
		});

	connect(m_window, "focus-out-event",
		[this](GtkWidget* widget, GdkEvent*) -> gboolean
		{
			if (wm_settings->stay_on_focus_out || m_child_has_focus || !gtk_widget_get_visible(widget))
			{
				return GDK_EVENT_PROPAGATE;
			}

			// Needed to make focus out event happen after button press event,
			// otherwise it is impossible to toggle panel button.
			g_idle_add(
				+[](gpointer user_data) -> gboolean
				{
					static_cast<Window*>(user_data)->hide(true);
					return G_SOURCE_REMOVE;
				},
			this);

			return GDK_EVENT_PROPAGATE;
		});

	connect(m_window, "key-press-event",
		[this](GtkWidget* widget, GdkEvent* event) -> gboolean
		{
			return on_key_press_event(widget, reinterpret_cast<GdkEventKey*>(event));
		});

	connect(m_window, "key-press-event",
		[this](GtkWidget* widget, GdkEvent* event) -> gboolean
		{
			return on_key_press_event_after(widget, reinterpret_cast<GdkEventKey*>(event));
		},
		Connect::After);

	connect(m_window, "map-event",
		[this](GtkWidget*, GdkEvent*) -> gboolean
		{
			return on_map_event();
		});

	connect(m_window, "state-flags-changed",
		[this](GtkWidget* widget, GtkStateFlags)
		{
			on_state_flags_changed(widget);
		});

	g_signal_connect(G_OBJECT(m_window), "delete-event", G_CALLBACK(&gtk_widget_hide_on_delete), nullptr);

	// Create the border of the window
	GtkWidget* frame = gtk_frame_new(nullptr);
	gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_NONE);
	gtk_container_add(GTK_CONTAINER(m_window), frame);

	// Create window contents stack
	m_window_stack = GTK_STACK(gtk_stack_new());
	gtk_container_add(GTK_CONTAINER(frame), GTK_WIDGET(m_window_stack));

	// Create loading message
	m_window_load_spinner = GTK_SPINNER(gtk_spinner_new());
	gtk_widget_set_halign(GTK_WIDGET(m_window_load_spinner), GTK_ALIGN_CENTER);
	gtk_widget_set_valign(GTK_WIDGET(m_window_load_spinner), GTK_ALIGN_CENTER);
	gtk_stack_add_named(m_window_stack, GTK_WIDGET(m_window_load_spinner), "load");

	// Create resizers
	m_resize[Resizer::TopLeft] = new Resizer(Resizer::TopLeft, this);
	m_resize[Resizer::Top] = new Resizer(Resizer::Top, this);
	m_resize[Resizer::TopRight] = new Resizer(Resizer::TopRight, this);
	m_resize[Resizer::Left] = new Resizer(Resizer::Left, this);
	m_resize[Resizer::Right] = new Resizer(Resizer::Right, this);
	m_resize[Resizer::BottomLeft] = new Resizer(Resizer::BottomLeft, this);
	m_resize[Resizer::Bottom] = new Resizer(Resizer::Bottom, this);
	m_resize[Resizer::BottomRight] = new Resizer(Resizer::BottomRight, this);

	// Create the profile picture and username label
	m_profile = new Profile(this);

	// Create action buttons
	for (int i = 0; i < 9; ++i)
	{
		m_commands_button[i] = wm_settings->command[i]->get_button();
		m_command_slots[i] = connect(m_commands_button[i], "clicked",
			[this](GtkButton*)
			{
				hide();
			});
	}

	// Create search entry
	m_search_entry = GTK_ENTRY(gtk_search_entry_new());

	connect(m_search_entry, "changed",
		[this](GtkEditable*)
		{
			search();
		});

	connect(m_search_entry, "populate-popup",
		[this](GtkEntry*, GtkWidget*)
		{
			set_child_has_focus();
		});

	// Create favorites
	m_favorites = new FavoritesPage(this);

	CategoryButton* favorites_button = m_favorites->get_button();
	connect(favorites_button->get_widget(), "toggled",
		[this](GtkToggleButton*)
		{
			favorites_toggled();
		});

	// Create recent
	m_recent = new RecentPage(this);

	CategoryButton* recent_button = m_recent->get_button();
	recent_button->join_group(favorites_button);
	connect(recent_button->get_widget(), "toggled",
		[this](GtkToggleButton*)
		{
			recent_toggled();
		});

	// Create applications
	m_applications = new ApplicationsPage(this);

	CategoryButton* applications_button = m_applications->get_button();
	applications_button->join_group(recent_button);
	connect(applications_button->get_widget(), "toggled",
		[this](GtkToggleButton*)
		{
			category_toggled();
		});

	// Create search results
	m_search_results = new SearchPage(this);

	GtkBox* search_results = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 0));
	gtk_box_pack_start(search_results, m_search_results->get_message(), false, false, 0);
	gtk_box_pack_start(search_results, m_search_results->get_widget(), true, true, 0);
	gtk_container_set_border_width(GTK_CONTAINER(search_results), 0);

	// Create grid for packing resizers
	GtkGrid* grid = GTK_GRID(gtk_grid_new());
	gtk_grid_attach(grid, m_resize[Resizer::TopLeft]->get_widget(), 0, 0, 1, 1);
	gtk_grid_attach(grid, m_resize[Resizer::Top]->get_widget(), 1, 0, 1, 1);
	gtk_grid_attach(grid, m_resize[Resizer::TopRight]->get_widget(), 2, 0, 1, 1);
	gtk_grid_attach(grid, m_resize[Resizer::Left]->get_widget(), 0, 1, 1, 1);
	gtk_grid_attach(grid, m_resize[Resizer::Right]->get_widget(), 2, 1, 1, 1);
	gtk_grid_attach(grid, m_resize[Resizer::BottomLeft]->get_widget(), 0, 2, 1, 1);
	gtk_grid_attach(grid, m_resize[Resizer::Bottom]->get_widget(), 1, 2, 1, 1);
	gtk_grid_attach(grid, m_resize[Resizer::BottomRight]->get_widget(), 2, 2, 1, 1);
	gtk_stack_add_named(m_window_stack, GTK_WIDGET(grid), "contents");

	// Create box for packing children
	m_vbox = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 6));
	gtk_container_set_border_width(GTK_CONTAINER(m_vbox), 0);
	gtk_grid_attach(grid, GTK_WIDGET(m_vbox), 1, 1, 1, 1);

	// Create box for packing commands
	m_commands_box = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0));
	m_commands_spacer = gtk_label_new(nullptr);
	gtk_box_pack_start(m_commands_box, m_commands_spacer, true, true, 0);
	for (auto command : m_commands_button)
	{
		gtk_box_pack_start(m_commands_box, command, false, false, 0);
	}

	// Create box for packing username and commands
	m_title_box = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6));
	gtk_box_pack_start(m_vbox, GTK_WIDGET(m_title_box), false, false, 0);
	gtk_box_pack_start(m_title_box, m_profile->get_picture(), false, false, 0);
	gtk_box_pack_start(m_title_box, m_profile->get_username(), true, true, 0);
	gtk_box_pack_start(m_title_box, GTK_WIDGET(m_commands_box), false, false, 0);

	// Add search to layout
	m_search_box = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6));
	gtk_box_pack_start(m_vbox, GTK_WIDGET(m_search_box), false, true, 0);
	gtk_box_pack_start(m_search_box, GTK_WIDGET(m_search_entry), true, true, 0);

	// Create box for packing launcher pages and sidebar
	m_contents_stack = GTK_STACK(gtk_stack_new());
	m_contents_box = GTK_GRID(gtk_grid_new());
	gtk_grid_set_column_spacing(m_contents_box, 6);
	gtk_grid_set_row_spacing(m_contents_box, 0);
	gtk_stack_add_named(m_contents_stack, GTK_WIDGET(m_contents_box), "contents");
	gtk_stack_add_named(m_contents_stack, GTK_WIDGET(search_results), "search");
	gtk_box_pack_start(m_vbox, GTK_WIDGET(m_contents_stack), true, true, 0);

	// Create the Aero-style three-column layout:
	// [icon strip 77px][all applications 409px][pinned favorites]
	gtk_grid_set_column_spacing(m_contents_box, 0);

	m_sidebar_strip = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 6));
	gtk_widget_set_size_request(GTK_WIDGET(m_sidebar_strip), 77, -1);
	gtk_widget_set_vexpand(GTK_WIDGET(m_sidebar_strip), true);
	gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(m_sidebar_strip)), "whiskermenu-strip");
	gtk_grid_attach(m_contents_box, GTK_WIDGET(m_sidebar_strip), 0, 1, 1, 1);

	m_apps_scroll = GTK_SCROLLED_WINDOW(gtk_scrolled_window_new(nullptr, nullptr));
	gtk_widget_set_size_request(GTK_WIDGET(m_apps_scroll), 409, -1);
	gtk_widget_set_hexpand(GTK_WIDGET(m_apps_scroll), false);
	gtk_widget_set_vexpand(GTK_WIDGET(m_apps_scroll), true);
	gtk_scrolled_window_set_propagate_natural_width(m_apps_scroll, false);
	gtk_scrolled_window_set_policy(m_apps_scroll, GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_shadow_type(m_apps_scroll, GTK_SHADOW_NONE);
	gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(m_apps_scroll)), "whiskermenu-apps");
	m_apps_list = GTK_LIST_BOX(gtk_list_box_new());
	gtk_list_box_set_selection_mode(m_apps_list, GTK_SELECTION_BROWSE);
	gtk_list_box_set_activate_on_single_click(m_apps_list, true);
	gtk_container_add(GTK_CONTAINER(m_apps_scroll), GTK_WIDGET(m_apps_list));
	gtk_grid_attach(m_contents_box, GTK_WIDGET(m_apps_scroll), 1, 1, 1, 1);
	connect(m_apps_list, "row-activated",
		[this](GtkListBox*, GtkListBoxRow* row)
		{
			Launcher* launcher = static_cast<Launcher*>(g_object_get_data(G_OBJECT(row), "launcher"));
			if (!launcher)
			{
				return;
			}
			m_recent->add(launcher);
			hide();
			launcher->run(gtk_widget_get_screen(GTK_WIDGET(m_window)));
		});

	// Create box for packing categories horizontally (kept for compatibility, hidden)
	m_categories_box = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0));
	gtk_grid_attach(m_contents_box, GTK_WIDGET(m_categories_box), 0, 0, 2, 1);

	// Create box for packing launcher pages; the pinned favorites fill the right column
	m_panels_stack = GTK_STACK(gtk_stack_new());
	gtk_grid_attach(m_contents_box, GTK_WIDGET(m_panels_stack), 2, 1, 1, 1);
	gtk_widget_set_hexpand(GTK_WIDGET(m_panels_stack), true);
	gtk_widget_set_vexpand(GTK_WIDGET(m_panels_stack), true);
	gtk_stack_add_named(m_panels_stack, m_favorites->get_widget(), "favorites");
	gtk_stack_add_named(m_panels_stack, m_recent->get_widget(), "recent");
	gtk_stack_add_named(m_panels_stack, m_applications->get_widget(), "applications");

	// Create box for packing sidebar (kept for compatibility, hidden)
	m_category_buttons = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 0));
	gtk_box_pack_start(m_category_buttons, favorites_button->get_widget(), false, false, 0);
	gtk_box_pack_start(m_category_buttons, recent_button->get_widget(), false, false, 0);
	gtk_box_pack_start(m_category_buttons, applications_button->get_widget(), false, false, 0);
	gtk_box_pack_start(m_category_buttons, gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), false, false, 4);

	m_sidebar = GTK_SCROLLED_WINDOW(gtk_scrolled_window_new(nullptr, nullptr));
	gtk_grid_attach(m_contents_box, GTK_WIDGET(m_sidebar), 3, 1, 1, 1);
	gtk_scrolled_window_set_propagate_natural_height(m_sidebar, true);
	gtk_scrolled_window_set_shadow_type(m_sidebar, GTK_SHADOW_NONE);
	gtk_scrolled_window_set_policy(m_sidebar, GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_container_add(GTK_CONTAINER(m_sidebar), GTK_WIDGET(m_category_buttons));

	// Build the left icon strip. The buttons sit at the bottom, like the reference.
	gtk_box_pack_start(m_sidebar_strip, gtk_label_new(nullptr), true, true, 0);

	auto add_folder_button = [this](GUserDirectory directory, const gchar* icon, const gchar* tooltip, const gchar* fallback)
	{
		GtkWidget* button = gtk_button_new();
		gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
		gtk_widget_set_tooltip_text(button, tooltip);
		gtk_container_add(GTK_CONTAINER(button),
			gtk_image_new_from_icon_name(icon, GTK_ICON_SIZE_LARGE_TOOLBAR));
		add_strip_button(button);
		gtk_box_pack_start(m_sidebar_strip, button, false, false, 0);
		connect(button, "clicked",
			[this, directory, fallback](GtkButton*)
			{
				open_user_directory(directory, fallback);
			});
	};
	add_folder_button(G_USER_DIRECTORY_VIDEOS, "folder-videos", _("Videos"), "Videos");
	add_folder_button(G_USER_DIRECTORY_MUSIC, "folder-music", _("Music"), "Music");
	add_folder_button(G_USER_DIRECTORY_DOCUMENTS, "folder-documents", _("Documents"), "Documents");

	GtkWidget* settings_button = m_commands_button[Settings::CommandSettings];
	gtk_container_remove(GTK_CONTAINER(m_commands_box), settings_button);
	add_strip_button(settings_button);
	gtk_box_pack_start(m_sidebar_strip, settings_button, false, false, 0);

	GtkWidget* power_button = m_commands_button[Settings::CommandLogOut];
	gtk_container_remove(GTK_CONTAINER(m_commands_box), power_button);
	add_strip_button(power_button);
	gtk_box_pack_start(m_sidebar_strip, power_button, false, false, 0);

	// The old title bar, search box, category sidebar and category row are not part of the layout.
	gtk_widget_set_no_show_all(GTK_WIDGET(m_title_box), true);
	gtk_widget_set_visible(GTK_WIDGET(m_title_box), false);
	gtk_widget_set_no_show_all(GTK_WIDGET(m_search_box), true);
	gtk_widget_set_visible(GTK_WIDGET(m_search_box), false);
	gtk_widget_set_no_show_all(GTK_WIDGET(m_categories_box), true);
	gtk_widget_set_visible(GTK_WIDGET(m_categories_box), false);
	gtk_widget_set_no_show_all(GTK_WIDGET(m_sidebar), true);
	gtk_widget_set_visible(GTK_WIDGET(m_sidebar), false);

	// Make the pinned favorites use the full right column without a theme border.
	gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(m_favorites->get_widget()), GTK_SHADOW_NONE);

	// Handle default page
	reset_default_button();

	// Add CSS classes
	gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(m_window)), "whiskermenu");
	gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(m_search_box)), "search-area");
	gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(m_title_box)), "title-area");
	gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(m_commands_box)), "commands-area");
	gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(m_contents_stack)), "contents");

	GtkStyleContext* context = gtk_widget_get_style_context(GTK_WIDGET(m_category_buttons));
	gtk_style_context_add_class(context, "categories");
	gtk_style_context_add_class(context, "right");

	// Draw the translucent background ourselves; keep theme containers out of the way.
	GtkCssProvider* css_provider = gtk_css_provider_new();
	gtk_css_provider_load_from_data(css_provider,
		"#whiskermenu-window {"
		"  background-color: transparent;"
		"  border-radius: 0;"
		"}"
		"#whiskermenu-window frame {"
		"  background-color: transparent;"
		"  background-image: none;"
		"  border-style: none;"
		"  border-width: 0;"
		"  border-radius: 0;"
		"  box-shadow: none;"
		"  padding: 0;"
		"}"
		"#whiskermenu-window stack,"
		"#whiskermenu-window grid,"
		"#whiskermenu-window box {"
		"  background-color: transparent;"
		"}"
		"#whiskermenu-window scrolledwindow,"
		"#whiskermenu-window viewport,"
		"#whiskermenu-window list,"
		"#whiskermenu-window iconview,"
		"#whiskermenu-window treeview,"
		"#whiskermenu-window .launchers-pane {"
		"  background-color: transparent;"
		"  background-image: none;"
		"  border-style: none;"
		"  box-shadow: none;"
		"}"
		"#whiskermenu-window .whiskermenu-apps row {"
		"  background-color: transparent;"
		"  border-radius: 4px;"
		"}"
		"#whiskermenu-window .whiskermenu-apps row:hover {"
		"  background-color: rgba(255,255,255,0.10);"
		"}"
		"#whiskermenu-window .whiskermenu-apps row:selected {"
		"  background-color: rgba(255,255,255,0.16);"
		"}"
		"#whiskermenu-window label,"
		"#whiskermenu-window iconview,"
		"#whiskermenu-window treeview,"
		"#whiskermenu-window .whiskermenu-apps row {"
		"  color: #ffffff;"
		"}"
		"#whiskermenu-window .whiskermenu-apps .app-section {"
		"  color: #ffffff;"
		"  font-weight: bold;"
		"  font-size: 14px;"
		"  opacity: 1.0;"
		"}"
		"#whiskermenu-window .whiskermenu-strip button {"
		"  background-color: transparent;"
		"  background-image: none;"
		"  border-style: none;"
		"  box-shadow: none;"
		"  padding: 0;"
		"  border-radius: 4px;"
		"}"
		"#whiskermenu-window .whiskermenu-strip button:hover {"
		"  background-color: rgba(255,255,255,0.12);"
		"}"
		"#whiskermenu-window .whiskermenu-strip button:active {"
		"  background-color: rgba(255,255,255,0.20);"
		"}",
		-1, nullptr);
	gtk_style_context_add_provider_for_screen(gtk_widget_get_screen(GTK_WIDGET(m_window)),
			GTK_STYLE_PROVIDER(css_provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	g_object_unref(css_provider);

	// Show widgets
	gtk_widget_show_all(frame);
	m_default_button->set_active(true);

	// Handle transparency
	gtk_widget_set_app_paintable(GTK_WIDGET(m_window), true);

	connect(m_window, "draw",
		[this](GtkWidget* widget, cairo_t* cr) -> gboolean
		{
			return on_draw_event(widget, cr);
		});

	connect(m_window, "screen-changed",
		[this](GtkWidget* widget, GdkScreen*)
		{
			on_screen_changed(widget);
		});
	on_screen_changed(GTK_WIDGET(m_window));

	// Load applications
	m_applications->load();

	g_object_ref_sink(m_window);
}

//-----------------------------------------------------------------------------

WhiskerMenu::Window::~Window()
{
	for (int i = 0; i < 9; ++i)
	{
		g_signal_handler_disconnect(m_commands_button[i], m_command_slots[i]);
		GtkWidget* parent = gtk_widget_get_parent(m_commands_button[i]);
		if (parent == GTK_WIDGET(m_commands_box) || parent == GTK_WIDGET(m_sidebar_strip))
		{
			gtk_container_remove(GTK_CONTAINER(parent), m_commands_button[i]);
		}
	}

	delete m_applications;
	delete m_search_results;
	delete m_recent;
	delete m_favorites;

	delete m_profile;

	for (Resizer* resizer : m_resize)
	{
		delete resizer;
	}

	gtk_widget_destroy(GTK_WIDGET(m_window));
	g_object_unref(m_window);
}

//-----------------------------------------------------------------------------

Page* WhiskerMenu::Window::get_active_page()
{
	Page* page = nullptr;
	if (g_strcmp0(gtk_stack_get_visible_child_name(m_contents_stack), "search") == 0)
	{
		page = m_search_results;
	}
	else if (m_favorites->get_button()->get_active())
	{
		page = m_favorites;
	}
	else if (m_recent->get_button()->get_active())
	{
		page = m_recent;
	}
	else
	{
		page = m_applications;
	}
	return page;
}

//-----------------------------------------------------------------------------

void WhiskerMenu::Window::build_app_list()
{
	if (!m_apps_list)
	{
		return;
	}

	// Remove old rows
	GList* rows = gtk_container_get_children(GTK_CONTAINER(m_apps_list));
	g_list_free_full(rows, (GDestroyNotify)gtk_widget_destroy);

	// Add applications grouped by their first letter
	gunichar last_group = 0;
	for (Launcher* launcher : m_applications->find_all())
	{
		const gchar* name = launcher->get_text();
		if (!name || !*name)
		{
			continue;
		}

		const gunichar first = g_utf8_get_char(name);
		gunichar group = g_unichar_toupper(first);
		if (!g_unichar_isalpha(group))
		{
			group = '#';
		}

		if (group != last_group)
		{
			last_group = group;

			gchar letter[8] = {0};
			const gint length = g_unichar_to_utf8(group, letter);
			letter[length] = 0;

			GtkWidget* row = gtk_list_box_row_new();
			GtkWidget* label = gtk_label_new(letter);
			gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
			gtk_widget_set_margin_start(label, 16);
			gtk_widget_set_margin_end(label, 12);
			gtk_widget_set_margin_top(label, 14);
			gtk_widget_set_margin_bottom(label, 4);
			gtk_style_context_add_class(gtk_widget_get_style_context(label), "app-section");
			gtk_container_add(GTK_CONTAINER(row), label);
			gtk_list_box_row_set_selectable(GTK_LIST_BOX_ROW(row), false);
			gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(row), false);
			gtk_list_box_insert(m_apps_list, row, -1);
		}

		GtkWidget* row = gtk_list_box_row_new();
		GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
		gtk_widget_set_margin_start(box, 16);
		gtk_widget_set_margin_end(box, 12);
		gtk_widget_set_margin_top(box, 4);
		gtk_widget_set_margin_bottom(box, 4);

		GIcon* icon = launcher->get_icon();
		GtkWidget* image = icon
				? gtk_image_new_from_gicon(icon, GTK_ICON_SIZE_DND)
				: gtk_image_new_from_icon_name("application-x-executable", GTK_ICON_SIZE_DND);
		gtk_image_set_pixel_size(GTK_IMAGE(image), 32);
		gtk_widget_set_valign(image, GTK_ALIGN_CENTER);

		GtkWidget* label = gtk_label_new(name);
		gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
		gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
		gtk_widget_set_hexpand(label, true);

		gtk_box_pack_start(GTK_BOX(box), image, false, false, 0);
		gtk_box_pack_start(GTK_BOX(box), label, true, true, 0);
		gtk_container_add(GTK_CONTAINER(row), box);
		gtk_widget_set_tooltip_markup(row, launcher->get_tooltip());
		g_object_set_data(G_OBJECT(row), "launcher", launcher);
		gtk_list_box_insert(m_apps_list, row, -1);
	}

	gtk_widget_show_all(GTK_WIDGET(m_apps_list));

	GtkAdjustment* adjustment = gtk_scrolled_window_get_vadjustment(m_apps_scroll);
	gtk_adjustment_set_value(adjustment, gtk_adjustment_get_lower(adjustment));
}

//-----------------------------------------------------------------------------

void WhiskerMenu::Window::open_user_directory(GUserDirectory directory, const gchar* fallback_name)
{
	const gchar* path = g_get_user_special_dir(directory);
	gchar* fallback = nullptr;
	if (!path || !*path)
	{
		fallback = g_build_filename(g_get_home_dir(), fallback_name, nullptr);
		path = fallback;
	}

	gchar* uri = g_filename_to_uri(path, nullptr, nullptr);
	hide();
	if (uri)
	{
		GError* error = nullptr;
		if (!gtk_show_uri_on_window(m_window, uri, GDK_CURRENT_TIME, &error))
		{
			g_warning("Failed to open %s: %s", path, error ? error->message : "unknown error");
			if (error)
			{
				g_error_free(error);
			}
		}
		g_free(uri);
	}
	g_free(fallback);
}

//-----------------------------------------------------------------------------

void WhiskerMenu::Window::add_strip_button(GtkWidget* button)
{
	gtk_widget_set_halign(button, GTK_ALIGN_CENTER);
	gtk_widget_set_valign(button, GTK_ALIGN_CENTER);
	gtk_widget_set_size_request(button, 48, 48);
	gtk_style_context_add_class(gtk_widget_get_style_context(button), "strip-button");
}

//-----------------------------------------------------------------------------

void WhiskerMenu::Window::hide(bool lost_focus)
{
	// Save settings
	wm_settings->favorites.save();
	wm_settings->recent.save();

	// Hide window
	gtk_widget_hide(GTK_WIDGET(m_window));

	// Switch back to default page
	show_default_page();

	// Inform plugin that window is hidden
	if (!lost_focus)
	{
		m_plugin->menu_hidden();
	}
}

//-----------------------------------------------------------------------------

void WhiskerMenu::Window::show(const Position position)
{
	m_position = position;

	// Handle switching view types
	m_search_results->update_view();
	m_favorites->update_view();
	m_recent->update_view();
	m_applications->update_view();

	// Handle showing tooltips
	if (wm_settings->launcher_show_tooltip)
	{
		m_search_results->get_view()->show_tooltips();
		m_favorites->get_view()->show_tooltips();
		m_recent->get_view()->show_tooltips();
		m_applications->get_view()->show_tooltips();
	}
	else
	{
		m_search_results->get_view()->hide_tooltips();
		m_favorites->get_view()->hide_tooltips();
		m_recent->get_view()->hide_tooltips();
		m_applications->get_view()->hide_tooltips();
	}
	m_profile->reset_tooltip();

	// Make sure commands are valid and visible
	for (auto command : wm_settings->command)
	{
		command->check();
	}

	// Make sure recent item count is within max
	m_recent->enforce_item_count();

	// Make sure recent button is only visible when tracked
	gtk_widget_set_visible(m_recent->get_button()->get_widget(), wm_settings->recent_items_max);

	// Make sure applications list is current; does nothing unless list has changed
	if (m_applications->load())
	{
		set_loaded();
	}
	else
	{
		m_plugin->set_loaded(false);
		gtk_stack_set_visible_child_name(m_window_stack, "load");
		gtk_spinner_start(m_window_load_spinner);
	}

	// Update default page
	reset_default_button();
	show_default_page();

	// Clear any previous selection
	m_favorites->reset_selection();
	m_recent->reset_selection();
	m_applications->reset_selection();

	// Make sure icon sizes are correct
	m_favorites->get_button()->reload_icon_size();
	m_recent->get_button()->reload_icon_size();
	m_applications->get_button()->reload_icon_size();

	m_applications->reload_category_icon_size();

	m_search_results->get_view()->reload_icon_size();
	m_favorites->get_view()->reload_icon_size();
	m_recent->get_view()->reload_icon_size();
	m_applications->get_view()->reload_icon_size();

	GdkMonitor* monitor_gdk = nullptr;
	if (position == PositionAtButton)
	{
		// Wait up to half a second for auto-hidden panels to be shown
		int parent_x = 0, parent_y = 0;
		clock_t end = clock() + (CLOCKS_PER_SEC / 2);
		GtkWidget* parent = m_plugin->get_button();
		GtkWindow* parent_window = GTK_WINDOW(gtk_widget_get_toplevel(parent));
		gtk_window_get_position(parent_window, &parent_x, &parent_y);
		while ((parent_x == -9999) && (parent_y == -9999) && (clock() < end))
		{
			while (gtk_events_pending())
			{
				gtk_main_iteration();
			}
			gtk_window_get_position(parent_window, &parent_x, &parent_y);
		}

		// Fetch position
		m_plugin->get_menu_position(&m_geometry.x, &m_geometry.y);
		monitor_gdk = gdk_display_get_monitor_at_window(gtk_widget_get_display(parent), gtk_widget_get_window(parent));
	}
	else
	{
		// Fetch cursor position
		GdkDisplay* display = gdk_display_get_default();
		GdkSeat* seat = gdk_display_get_default_seat(display);
		GdkDevice* device = gdk_seat_get_pointer(seat);
		gdk_device_get_position(device, nullptr, &m_geometry.x, &m_geometry.y);
		monitor_gdk = gdk_display_get_monitor_at_point(display, m_geometry.x, m_geometry.y);
	}

	// Resize window if necessary, and also prevent it from being larger than screen
#ifdef HAVE_GTK_LAYER_SHELL
	if (gtk_layer_is_supported())
	{
		gtk_layer_set_monitor(m_window, monitor_gdk);
	}
#endif
	gdk_monitor_get_geometry(monitor_gdk, &m_monitor);
	const bool resized = set_size(wm_settings->menu_width, wm_settings->menu_height);

	// Center window if requested
	if (position == PositionAtCenter)
	{
		center_window();
	}

	// Move window
	move_window();

	// Relayout window if necessary
	const bool layout_ltr = gtk_widget_get_default_direction() != GTK_TEXT_DIR_RTL;
	if ((m_layout_ltr != layout_ltr)
			|| (m_layout_categories_horizontal != wm_settings->position_categories_horizontal)
			|| (m_layout_categories_alternate != wm_settings->position_categories_alternate)
			|| (m_layout_search_alternate != wm_settings->position_search_alternate)
			|| (m_layout_commands_alternate != wm_settings->position_commands_alternate)
			|| (m_layout_profile_alternate != wm_settings->position_profile_alternate)
			|| (m_profile_shape != wm_settings->profile_shape))
	{
		m_layout_ltr = layout_ltr;
		m_layout_categories_horizontal = wm_settings->position_categories_horizontal;
		m_layout_categories_alternate = wm_settings->position_categories_alternate;
		m_layout_search_alternate = wm_settings->position_search_alternate;
		m_layout_commands_alternate = wm_settings->position_commands_alternate;
		m_layout_profile_alternate = wm_settings->position_profile_alternate;
		m_profile->update_picture();
		m_profile_shape = wm_settings->profile_shape;
		update_layout();
	}

	// Show window
	gtk_window_present(m_window);

	if (resized)
	{
		check_scrollbar_needed();
	}

	// Fetch actual window size
	GtkRequisition size;
	gtk_widget_get_preferred_size(GTK_WIDGET(m_window), &size, nullptr);
	m_geometry.width = std::max(size.width, m_geometry.width);
	m_geometry.height = std::max(size.height, m_geometry.height);

	// Fetch position again to make sure window does not overlap panel
	if (position == PositionAtButton)
	{
		m_plugin->get_menu_position(&m_geometry.x, &m_geometry.y);
	}
	else if (position == PositionAtCenter)
	{
		center_window();
	}

	// Move window
	move_window();
}

//-----------------------------------------------------------------------------

void WhiskerMenu::Window::resize(int delta_x, int delta_y, int delta_width, int delta_height)
{
	if (set_size(m_geometry.width + delta_width, m_geometry.height + delta_height))
	{
		check_scrollbar_needed();
	}

	if (delta_x || delta_y)
	{
		m_geometry.x += delta_x;
		m_geometry.y += delta_y;
		move_window();
	}
}

//-----------------------------------------------------------------------------

void WhiskerMenu::Window::resize_start()
{
	m_resizing = true;
	set_child_has_focus();
}

//-----------------------------------------------------------------------------

void WhiskerMenu::Window::resize_end()
{
	// Store new size
	wm_settings->menu_width = m_geometry.width;
	wm_settings->menu_height = m_geometry.height;

	// Move window back to panel button or center of screen
	if (m_position == PositionAtButton)
	{
		m_plugin->get_menu_position(&m_geometry.x, &m_geometry.y);
	}
	else if (m_position == PositionAtCenter)
	{
		center_window();
	}
	move_window();

	// Allow menu to hide
	m_resizing = false;
	m_child_has_focus = false;
}

//-----------------------------------------------------------------------------

void WhiskerMenu::Window::set_child_has_focus()
{
	m_child_has_focus = true;
}

//-----------------------------------------------------------------------------

void WhiskerMenu::Window::set_categories(const std::vector<CategoryButton*>& categories)
{
	CategoryButton* last_button = m_applications->get_button();
	for (auto button : categories)
	{
		button->join_group(last_button);
		last_button = button;
		gtk_box_pack_start(m_category_buttons, button->get_widget(), false, false, 0);
		connect(button->get_widget(), "toggled",
			[this](GtkToggleButton*)
			{
				category_toggled();
			});
	}

	show_default_page();
}

//-----------------------------------------------------------------------------

void WhiskerMenu::Window::set_items()
{
	m_search_results->set_menu_items();
	m_favorites->set_menu_items();
	m_recent->set_menu_items();

	// Handle switching to favorites are added
	connect(m_favorites->get_view()->get_model(), "row-inserted",
		[this](GtkTreeModel*, GtkTreePath*, GtkTreeIter*)
		{
			show_favorites();
		});
}

//-----------------------------------------------------------------------------

void WhiskerMenu::Window::set_loaded()
{
	// Hide loading spinner
	gtk_spinner_stop(m_window_load_spinner);
	gtk_stack_set_visible_child_name(m_window_stack, "contents");

	// Build the letter-grouped application list
	build_app_list();
	gtk_widget_grab_focus(GTK_WIDGET(m_apps_list));

	// Show panel button
	m_plugin->set_loaded(true);
}

//-----------------------------------------------------------------------------

void WhiskerMenu::Window::unset_items()
{
	m_search_results->unset_menu_items();
	m_favorites->unset_menu_items();
	m_recent->unset_menu_items();

	// Drop rows that point at launchers which are about to be reloaded.
	if (m_apps_list)
	{
		GList* rows = gtk_container_get_children(GTK_CONTAINER(m_apps_list));
		g_list_free_full(rows, (GDestroyNotify)gtk_widget_destroy);
	}
}

//-----------------------------------------------------------------------------

GtkWidget* WhiskerMenu::Window::get_active_category_button()
{
	GtkWidget* widget = m_default_button->get_widget();

	GList* children = gtk_container_get_children(GTK_CONTAINER(m_category_buttons));
	for (GList* li = children; li; li = li->next)
	{
		GtkToggleButton* button = GTK_TOGGLE_BUTTON(li->data);
		if (button && gtk_toggle_button_get_active(button))
		{
			widget = GTK_WIDGET(button);
			break;
		}
	}
	g_list_free(children);

	return widget;
}

//-----------------------------------------------------------------------------

gboolean WhiskerMenu::Window::on_key_press_event(GtkWidget* widget, GdkEventKey* key_event)
{
	if (key_event->keyval == GDK_KEY_Escape)
	{
		// Cancel resize
		if (m_resizing)
		{
			for (Resizer* resizer : m_resize)
			{
				resizer->cancel();
			}
			set_size(wm_settings->menu_width, wm_settings->menu_height);
			resize_end();
		}
		else
		{
			hide();
		}
		return GDK_EVENT_STOP;
	}

	return GDK_EVENT_PROPAGATE;
}

//-----------------------------------------------------------------------------

gboolean WhiskerMenu::Window::on_key_press_event_after(GtkWidget*, GdkEventKey*)
{
	// There is no search entry in the three-column layout; let the focused widget handle keys.
	return GDK_EVENT_PROPAGATE;
}

//-----------------------------------------------------------------------------

gboolean WhiskerMenu::Window::on_map_event()
{
	gtk_window_set_keep_above(m_window, true);

	return GDK_EVENT_PROPAGATE;
}

//-----------------------------------------------------------------------------

void WhiskerMenu::Window::on_state_flags_changed(GtkWidget* widget)
{
	// Refocus and raise window if visible
	if (gtk_widget_get_visible(widget))
	{
		gtk_window_present(m_window);
	}
}

//-----------------------------------------------------------------------------

void WhiskerMenu::Window::on_screen_changed(GtkWidget* widget)
{
	GdkScreen* screen = gtk_widget_get_screen(widget);
	GdkVisual* visual = gdk_screen_get_rgba_visual(screen);
	if (!visual || (wm_settings->background_alpha >= 0.999))
	{
		visual = gdk_screen_get_system_visual(screen);
		m_supports_alpha = false;
	}
	else
	{
		m_supports_alpha = true;
	}
	gtk_widget_set_visual(widget, visual);
}

//-----------------------------------------------------------------------------

gboolean WhiskerMenu::Window::on_draw_event(GtkWidget* widget, cairo_t* cr)
{
	if (!gtk_widget_get_realized(widget))
	{
		gtk_widget_realize(widget);
	}

	GdkScreen* screen = gtk_widget_get_screen(widget);
	const bool enabled = gdk_screen_is_composited(screen);

	cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
	if (enabled && m_supports_alpha)
	{
		cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, wm_settings->background_alpha);
	}
	else
	{
		cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
	}
	cairo_paint(cr);

	return GDK_EVENT_PROPAGATE;
}

//-----------------------------------------------------------------------------

void WhiskerMenu::Window::check_scrollbar_needed()
{
	// Find height of sidebar and buttons
	int buttons_height = 0;
	gtk_widget_get_preferred_height(GTK_WIDGET(m_category_buttons), nullptr, &buttons_height);

	int sidebar_height = 0;
	gtk_widget_get_preferred_height(GTK_WIDGET(m_sidebar), nullptr, &sidebar_height);

	// Always show scrollbar if sidebar is shorter than buttons
	if (sidebar_height >= buttons_height)
	{
		gtk_scrolled_window_set_policy(m_sidebar, GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	}
	else
	{
		gtk_scrolled_window_set_policy(m_sidebar, GTK_POLICY_NEVER, GTK_POLICY_ALWAYS);
	}
}

//-----------------------------------------------------------------------------

void WhiskerMenu::Window::favorites_toggled()
{
	m_favorites->reset_selection();
	gtk_stack_set_visible_child_name(m_panels_stack, "favorites");
	gtk_widget_grab_focus(GTK_WIDGET(m_apps_list));
}

//-----------------------------------------------------------------------------

void WhiskerMenu::Window::recent_toggled()
{
	m_recent->reset_selection();
	gtk_stack_set_visible_child_name(m_panels_stack, "recent");
	gtk_widget_grab_focus(GTK_WIDGET(m_apps_list));
}

//-----------------------------------------------------------------------------

void WhiskerMenu::Window::category_toggled()
{
	m_applications->reset_selection();
	gtk_stack_set_visible_child_name(m_panels_stack, "applications");
	gtk_widget_grab_focus(GTK_WIDGET(m_apps_list));
}

//-----------------------------------------------------------------------------

void WhiskerMenu::Window::center_window()
{
	m_geometry.x = (m_monitor.width - m_geometry.width) / 2;
	m_geometry.y = (m_monitor.height - m_geometry.height) / 2;
}

//-----------------------------------------------------------------------------

void WhiskerMenu::Window::move_window()
{
	// Prevent window from leaving screen
	m_geometry.x = CLAMP(m_geometry.x, m_monitor.x, m_monitor.x + m_monitor.width - m_geometry.width);
	m_geometry.y = CLAMP(m_geometry.y, m_monitor.y, m_monitor.y + m_monitor.height - m_geometry.height);

	// Move window
#ifdef HAVE_GTK_LAYER_SHELL
	if (gtk_layer_is_supported())
	{
		gtk_layer_set_margin(m_window, GTK_LAYER_SHELL_EDGE_LEFT, m_geometry.x - m_monitor.x);
		gtk_layer_set_margin(m_window, GTK_LAYER_SHELL_EDGE_TOP, m_geometry.y - m_monitor.y);
	}
	else
#endif
	{
		gtk_window_move(m_window, m_geometry.x, m_geometry.y);
	}
}

//-----------------------------------------------------------------------------

bool WhiskerMenu::Window::set_size(int width, int height)
{
	bool resized = false;
	width = CLAMP(width, 10, m_monitor.width);
	height = CLAMP(height, 10, m_monitor.height);
	if ((m_geometry.width != width) || (m_geometry.height != height))
	{
		m_geometry.width = width;
		m_geometry.height = height;
		gtk_widget_set_size_request(GTK_WIDGET(m_window), m_geometry.width, m_geometry.height);
		gtk_window_resize(m_window, 1, 1);
		resized = true;
	}
	return resized;
}

//-----------------------------------------------------------------------------

void WhiskerMenu::Window::reset_default_button()
{
	// The right column always displays the pinned favorites.
	m_default_button = m_favorites->get_button();
}

//-----------------------------------------------------------------------------

void WhiskerMenu::Window::show_favorites()
{
	gtk_stack_set_visible_child_name(m_panels_stack, "favorites");
}

//-----------------------------------------------------------------------------

void WhiskerMenu::Window::show_default_page()
{
	if (m_default_button)
	{
		m_default_button->set_active(true);
	}
	gtk_stack_set_visible_child_name(m_panels_stack, "favorites");
	gtk_widget_grab_focus(GTK_WIDGET(m_apps_list));
}

//-----------------------------------------------------------------------------

void WhiskerMenu::Window::search()
{
	// Search is not part of the three-column layout.
}

//-----------------------------------------------------------------------------

void WhiskerMenu::Window::update_layout()
{
	// The three-column layout is fixed.
}

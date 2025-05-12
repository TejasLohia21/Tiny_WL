//9th April
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200112L
#endif

#include <assert.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>
#include <xkbcommon/xkbcommon.h>
#include <wlr/util/box.h>
#include <limits.h> 
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_xdg_output_v1.h>
#include <wlr/types/wlr_output_layout.h>
#include <signal.h>


//added 11th feb for layers;
#include "toplevelmanagement/wlr_foreign_toplevel_management_v1_server.h"
#include <wlr/types/wlr_foreign_toplevel_management_v1.h>

//added 20th feb for DMABUF;
#include <wlr/types/wlr_linux_dmabuf_v1.h>
#include <drm/drm_fourcc.h>
#include <wlr/render/drm_format_set.h>
#include <unistd.h>

// #include <wlr/types/wlr_drm_format.h>


#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

//adding following for time based implementation
#ifndef PROXIMITY_THRESHOLD
#define PROXIMITY_THRESHOLD 10
#endif

#ifndef TIME_THRESHOLD_MS
#define TIME_THRESHOLD_MS 1500
#endif

//z-addition

typedef void (*keybinding_handler_t)(xkb_keysym_t);

// --- NEW: Define a global modifier (by default ALT) used for compositor keybindings
static uint32_t global_modifier = WLR_MODIFIER_ALT;

void set_modifier_key(uint32_t modifier);

// static void spawn_fuzzel(void) {
//     if (fork() == 0) {
//         setsid();  // Detach from the current session
//         execlp("fuzzel", "fuzzel", NULL);
//         _exit(127); // Exit if exec fails
//     }
// }

static void spawn_fuzzel(void) {
    pid_t pid = fork();
    if (pid == 0) {
        setsid(); // Create new session
        
        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);
        
        execlp("fuzzel", "fuzzel", NULL);
        
        _exit(127);
    } else if (pid < 0) {
        // Fork failed
        wlr_log(WLR_ERROR, "Failed to fork for fuzzel");
    }
    // Parent process continues normally
}

static void spawn_sfwbar(void) {

    pid_t pid = fork();
    if (pid == 0) {
        // Child process: Detach from the current session
        setsid();

        // Optionally close standard file descriptors to detach completely
        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);

        // Execute SFWBar. Make sure it’s installed and in your PATH.
        execlp("sfwbar", "sfwbar", NULL);

        // If exec fails, exit with a nonzero status.
        _exit(127);
    } else if (pid < 0) {
        // Fork failed; log an error message.
        wlr_log(WLR_ERROR, "Failed to fork for sfwbar");
    }
    // Parent process: Continue normally.
}


extern char **environ;

static void take_screenshot(void) {
    if (fork() == 0) {
        execle("/usr/bin/grim", "grim", "screenshot.png", NULL, environ);
        perror("execle failed");
        _exit(127);
    }
}

#define MAX_TILING_HISTORY 10

struct tiling_state {
    int x;
    int y;
    int width;
    int height;
    char last_horizontal; // 'L' or 'R' or 0
    char last_vertical;   // 'U' or 'D' or 0
    bool tiled;
    bool h_subdivided;
    bool v_subdivided;
};


struct wlr_xdg_output_manager_v1 *xdg_output_manager;

struct tinywl_group {
    struct wl_list toplevels;
};


/* For brevity's sake, struct members are annotated where they are used. */
enum tinywl_cursor_mode {
	TINYWL_CURSOR_PASSTHROUGH,
	TINYWL_CURSOR_MOVE,
	TINYWL_CURSOR_RESIZE,
};

struct tinywl_server {
	// following 2 comments added
	struct wlr_layer_shell_v1 *layer_shell;
    struct wl_listener new_layer_surface;

	struct wl_display *wl_display;
	struct wlr_backend *backend;
	struct wlr_renderer *renderer;
	struct wlr_allocator *allocator;
	struct wlr_scene *scene;
	struct wlr_scene_output_layout *scene_layout;

	struct wlr_xdg_shell *xdg_shell;
	struct wl_listener new_xdg_toplevel;
	struct wl_listener new_xdg_popup;
	struct wl_list toplevels;

	struct wlr_cursor *cursor;
	struct wlr_xcursor_manager *cursor_mgr;
	struct wl_listener cursor_motion;
	struct wl_listener cursor_motion_absolute;
	struct wl_listener cursor_button;
	struct wl_listener cursor_axis;
	struct wl_listener cursor_frame;

	struct wlr_seat *seat;
	struct wl_listener new_input;
	struct wl_listener request_cursor;
	struct wl_listener request_set_selection;
	struct wl_list keyboards;
	enum tinywl_cursor_mode cursor_mode;
	struct tinywl_toplevel *grabbed_toplevel;
	double grab_x, grab_y;
	struct wlr_box grab_geobox;
	
	struct wlr_foreign_toplevel_manager_v1 *toplevel_manager; //added on 11th feb.
	struct wl_list popups;
	// struct wl_listener request_activate;
	// struct wl_listener request_close;
	uint32_t resize_edges;



// added this newly
	struct wlr_output_layout *output_layout;
	struct wl_list outputs;
	struct wl_listener new_output;

	//added 20th feb
	struct wlr_linux_dmabuf_v1 *linux_dmabuf;

	//z-added
	struct wlr_xdg_output_manager_v1 *xdg_output_manager;

    // --- NEW: Custom keybinding handler callback (if non-null, called in default case)
    keybinding_handler_t keybinding_handler;

};

struct tinywl_output {
	struct wl_list link;
	struct tinywl_server *server;
	struct wlr_output *wlr_output;
	struct wl_listener frame;
	struct wl_listener request_state;
	struct wl_listener destroy;
	struct wlr_scene_output *scene_output; // Added this line
};

struct tinywl_toplevel {
	//25th jan edited
	struct tinywl_group *group;
    struct wl_list group_link;
	//24th jan added
	bool maximized;
    struct wlr_box saved_geometry;


	struct wl_list link;
	struct tinywl_server *server;
	struct wlr_xdg_toplevel *xdg_toplevel;
	struct wlr_scene_tree *scene_tree;
	struct wl_listener map;
	struct wl_listener unmap;
	struct wl_listener commit;
	struct wl_listener destroy;
	struct wl_listener request_move;
	struct wl_listener request_resize;
	struct wl_listener request_maximize;
	struct wl_listener request_fullscreen;

	struct wlr_foreign_toplevel_handle_v1 *foreign_handle;
    struct wl_listener request_activate;
    struct wl_listener request_close;

	//following chanes are being amde
	time_t proximity_timestamp;

	/* NEW: Tiling mode flag. false = stacked (default), true = tiled */
    bool tiled;

    /* NEW: The current geometry used as the container for recursive tiling */
    struct wlr_box tiled_geometry;

     /* New: store last tiling directions for vertical and horizontal */
    char last_vertical;   /* 'U' for up, 'D' for down, 0 if not set */
    char last_horizontal; /* 'L' for left, 'R' for right, 0 if */

    // NEW: Flags to mark that an additional subdivision (to 1/8th) has already been performed.
    bool h_subdivided;
    bool v_subdivided;

    /* NEW: History of tiling states for reversion */
    struct tiling_state history[MAX_TILING_HISTORY];
    int history_len;
};

struct tinywl_popup {
	struct wlr_xdg_popup *xdg_popup;
	struct wl_listener commit;
	struct wl_listener destroy;
	struct wl_list link;
};

struct tinywl_keyboard {
	struct wl_list link;
	struct tinywl_server *server;
	struct wlr_keyboard *wlr_keyboard;

	struct wl_listener modifiers;
	struct wl_listener key;
	struct wl_listener destroy;
};

//added 20th feb
#include <linux/dma-buf.h>
#include <wlr/backend.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/render/egl.h>
#include <wlr/types/wlr_linux_dmabuf_v1.h>



struct wlr_foreign_toplevel_handle_v1 *create_toplevel_handle(
    struct wlr_foreign_toplevel_manager_v1 *manager,
    const char *title,
    const char *app_id);

void destroy_toplevel_handle(struct wlr_foreign_toplevel_handle_v1 *toplevel_handle);
static void handle_activate(struct wl_listener *listener, void *data);
static void handle_close(struct wl_listener *listener, void *data);


static struct wlr_linux_dmabuf_feedback_v1_tranche default_tranche = {
    .target_device = 0,  // Auto-detect GPU
    .flags = 0,          // Default flags
    .formats = (struct wlr_drm_format_set) {0},
};


static struct wlr_linux_dmabuf_feedback_v1 default_feedback = {
    .main_device = 0,           
    .tranches.data = &default_tranche,  
    .tranches.size = 1,         
};




void setup_toplevel_manager(struct tinywl_server *server);
static struct tinywl_toplevel *get_toplevel_for_surface(struct tinywl_server *server, struct wlr_surface *surface);
// static void handle_global_activate(struct wl_listener *listener, void *data);

struct wlr_foreign_toplevel_handle_v1 *create_toplevel_handle(
    struct wlr_foreign_toplevel_manager_v1 *manager,
    const char *title,
    const char *app_id) {
    
    struct wlr_foreign_toplevel_handle_v1 *handle = 
        wlr_foreign_toplevel_handle_v1_create(manager);
    
    if (!handle) {
        wlr_log(WLR_ERROR, "Failed to create toplevel handle");
        return NULL;
    }
    
    wlr_foreign_toplevel_handle_v1_set_title(handle, title);
    wlr_foreign_toplevel_handle_v1_set_app_id(handle, app_id);
    return handle;
}

//8th April Added
static struct tinywl_toplevel *snap_window1 = NULL;
static bool snap_mode = false;
static struct wl_event_source *snap_timer = NULL;


void destroy_toplevel_handle(struct wlr_foreign_toplevel_handle_v1 *toplevel_handle) {
    if (toplevel_handle) {
        wlr_foreign_toplevel_handle_v1_destroy(toplevel_handle);
    }
}


//adding a global count holder
int count_windows = 0;

//z-added
// Add a global flag (or add a parameter) to indicate if we're cycling.
static bool cycling_mode = false;

void set_keybinding_handler(struct tinywl_server *server, keybinding_handler_t handler);
bool cycle_windows(struct tinywl_server *server);
void server_run_in_stdout(struct tinywl_server *server);
void server_set_autostart_cmd(const char *cmd);
static void focus_toplevel(struct tinywl_toplevel *toplevel);
void tile_top_two(struct tinywl_server *server);

// --- NEW: Provide a setter for the custom keybinding handler.
void set_keybinding_handler(struct tinywl_server *server, keybinding_handler_t handler) {
    server->keybinding_handler = handler;
}
void server_set_autostart_cmd(const char *cmd) {
    // TODO: Store or process the autostart command as needed.
    wlr_log(WLR_INFO, "Autostart command set to: %s", cmd);
}

// Example stub for set_modifier_key:
void set_modifier_key(uint32_t modifier) {
    // If global_modifier is defined externally, update it here.
    global_modifier = modifier;
    wlr_log(WLR_INFO, "Global modifier set to: 0x%x", modifier);
}

void server_run_in_stdout(struct tinywl_server *server) {
    // Here’s where you actually implement the function
    // For now, you could simply do something like:
    wlr_log(WLR_INFO, "server_run_in_stdout was called!");

    // Or actually run a command, etc.
}

// Prototypes for new interactive functions and helper:
static void process_cursor_move(struct tinywl_server *server);
static void process_cursor_resize(struct tinywl_server *server);
static struct wlr_surface *desktop_surface_at(struct tinywl_server *server,
        double lx, double ly, double *sx, double *sy);








//following has been added for explicit declaratioj
void merge_groups(struct tinywl_toplevel *toplevel, struct tinywl_toplevel *other);

void remove_from_group(struct tinywl_toplevel *toplevel);


static void push_tiling_state(struct tinywl_toplevel *toplevel) {
    if (toplevel->history_len < MAX_TILING_HISTORY) {
        struct tiling_state *s = &toplevel->history[toplevel->history_len++];
        s->x = toplevel->scene_tree->node.x;
        s->y = toplevel->scene_tree->node.y;
        s->width = toplevel->xdg_toplevel->current.width;
        s->height = toplevel->xdg_toplevel->current.height;
        s->last_horizontal = toplevel->last_horizontal;
        s->last_vertical = toplevel->last_vertical;
        s->tiled = toplevel->tiled;
        s->h_subdivided = toplevel->h_subdivided;
        s->v_subdivided = toplevel->v_subdivided;
        wlr_log(WLR_DEBUG, "Pushed tiling state. New history_len: %d", toplevel->history_len);
    }
}

static bool pop_tiling_state(struct tinywl_toplevel *toplevel, struct tiling_state *state_out) {
    if (toplevel->history_len > 0) {
        *state_out = toplevel->history[--toplevel->history_len];
        wlr_log(WLR_DEBUG, "Popped tiling state. New history_len: %d", toplevel->history_len);
        return true;
    }
    return false;
}

bool cycle_windows(struct tinywl_server *server) {
    int len = wl_list_length(&server->toplevels);
    if (len < 2) {
        return false;
    }
    
    cycling_mode = true;
    
    struct wlr_surface *focused = server->seat->keyboard_state.focused_surface;
    struct tinywl_toplevel *current = NULL, *first = NULL, *next = NULL;
    struct tinywl_toplevel *toplevel;
    
    wl_list_for_each(toplevel, &server->toplevels, link) {
        if (!first)
            first = toplevel;
        if (current && !next) {
            next = toplevel;
            break;
        }
        if (toplevel->xdg_toplevel->base->surface == focused) {
            current = toplevel;
        }
    }
    if (!next)
        next = first;
    
    if (next)
        focus_toplevel(next);
    
    cycling_mode = false;
    return (next != NULL);
}


static void create_group(struct tinywl_toplevel *a, struct tinywl_toplevel *b) {
    struct tinywl_group *group = calloc(1, sizeof(*group));
    wl_list_init(&group->toplevels);
    a->group = group;
    b->group = group;
    wl_list_insert(&group->toplevels, &a->group_link);
    wl_list_insert(&group->toplevels, &b->group_link);
}

//25th jan edited
static void add_to_group(struct tinywl_group *group, struct tinywl_toplevel *toplevel) {
    toplevel->group = group;
    wl_list_insert(&group->toplevels, &toplevel->group_link);
}

void merge_groups(struct tinywl_toplevel *toplevel, struct tinywl_toplevel *other) {
    if (toplevel->group == other->group) {
        // The windows are already in the same group, no need to merge
        return;
    }

    if (!toplevel->group || !other->group) {
        // One of the windows doesn't have a group, just create one
        create_group(toplevel, other);
        wlr_log(WLR_DEBUG, "Created a new group for windows without groups.");
        return;
    }

    // Merge the groups
    struct tinywl_group *group_a = toplevel->group;
    struct tinywl_group *group_b = other->group;

    // If groups are already different, merge them
    struct tinywl_toplevel *member;
    wl_list_for_each(member, &group_b->toplevels, group_link) {
        member->group = group_a;
        wl_list_remove(&member->group_link);
        wl_list_insert(&group_a->toplevels, &member->group_link);
    }

    free(group_b); // Free the old group once all members have been moved
    wlr_log(WLR_DEBUG, "Merged two window groups successfully.");
}


void remove_from_group(struct tinywl_toplevel *toplevel) {
    if (toplevel->group) {
        // Remove the window from its group's list of toplevels
        wl_list_remove(&toplevel->group_link);

        // If the group becomes empty after removing this window, free the group
        if (wl_list_empty(&toplevel->group->toplevels)) {
            free(toplevel->group);
        }

        // Set the group's reference to NULL, since the window is no longer part of any group
        toplevel->group = NULL;
        wlr_log(WLR_DEBUG, "Window removed from group: %p", toplevel);
    } else {
        wlr_log(WLR_DEBUG, "Window is not part of any group: %p", toplevel);
    }
}


static void manual_snap_windows(struct tinywl_toplevel *window1, struct tinywl_toplevel *window2) {
    if (!window1 || !window2 || window1 == window2) {
        wlr_log(WLR_DEBUG, "Manual snap skipped: Invalid window arguments.");
        return;
    }

    struct wlr_box layout_box;
    wlr_output_layout_get_box(window1->server->output_layout, NULL, &layout_box);
    if (layout_box.width <= 0 || layout_box.height <= 0) {
        wlr_log(WLR_ERROR, "Manual snap failed: Invalid layout dimensions (%dx%d).", layout_box.width, layout_box.height);
        return;
    }

    struct tinywl_toplevel *left_win = window1;
    struct tinywl_toplevel *right_win = window2;
    // Use scene node coordinates which reflect the current compositor position
    if (window1->scene_tree->node.x > window2->scene_tree->node.x) {
        left_win = window2;
        right_win = window1;
    }
    wlr_log(WLR_DEBUG, "Left window identified as %p, right window as %p", (void*)left_win, (void*)right_win);

    int new_width_left = layout_box.width / 2;
    int new_width_right = layout_box.width / 2;


    int new_height = MAX(left_win->xdg_toplevel->current.height,
                        right_win->xdg_toplevel->current.height);

    int new_x_left = layout_box.x;
    int new_x_right = layout_box.x + new_width_left; // <-- KEY CHANGE
    int new_y = layout_box.y;


    wlr_xdg_toplevel_set_size(left_win->xdg_toplevel, new_width_left, new_height);
    wlr_xdg_toplevel_set_size(right_win->xdg_toplevel, new_width_right, new_height);

    // Position windows *after* resizing
    wlr_scene_node_set_position(&left_win->scene_tree->node, new_x_left, new_y);
    wlr_scene_node_set_position(&right_win->scene_tree->node, new_x_right, new_y); // Use calculated adjacent position

    if (!window1->group && !window2->group) {
        create_group(window1, window2);
    } else if (window1->group && window2->group && window1->group != window2->group) {
        merge_groups(window1, window2);
    } else if (!window2->group) {
        add_to_group(window1->group, window2);
    } else if (!window1->group) {
        add_to_group(window2->group, window1);
    }


    wlr_log(WLR_INFO, "Manually snapped windows side-by-side:");
    wlr_log(WLR_INFO, "  Left : %p at (%d, %d) size (%d x %d)", (void*)left_win, new_x_left, new_y, new_width_left, new_height);
    wlr_log(WLR_INFO, "  Right: %p at (%d, %d) size (%d x %d)", (void*)right_win, new_x_right, new_y, new_width_right, new_height);
}


#include <time.h>  // Make sure this is included

static bool check_proximity(struct tinywl_toplevel *toplevel) {
    struct tinywl_server *server = toplevel->server;
    const int proximity = PROXIMITY_THRESHOLD; // pixels
    
    // Skip newly created windows with no dimensions
    if (toplevel->xdg_toplevel->base->surface->current.width == 0 ||
        toplevel->xdg_toplevel->base->surface->current.height == 0) {
        return false;
    }

    if (toplevel->tiled) {
        return false;
    }

    // Get screen coordinates of the current window
    int ax, ay;
    wlr_scene_node_coords(&toplevel->scene_tree->node, &ax, &ay);
    
    struct wlr_box a = {
        .x = ax,  // Use absolute X
        .y = ay,  // Use absolute Y
        .width = toplevel->xdg_toplevel->current.width,
        .height = toplevel->xdg_toplevel->current.height
    };

    struct tinywl_toplevel *other;
    bool in_proximity = false;

    wl_list_for_each(other, &server->toplevels, link) {
        if (other == toplevel) continue;

        if (toplevel->tiled) {
            return false;
        }

        int bx, by;
        wlr_scene_node_coords(&other->scene_tree->node, &bx, &by);
        
        struct wlr_box b = {
            .x = bx,
            .y = by,
            .width = other->xdg_toplevel->current.width,
            .height = other->xdg_toplevel->current.height
        };

        // Calculate proximity
        bool horizontal_proximity = 
            (abs(a.x - (b.x + b.width)) < proximity) ||
            (abs((a.x + a.width) - b.x) < proximity) ||
            (abs(a.x - b.x) < proximity);

        bool vertical_proximity =
            (abs(a.y - (b.y + b.height)) < proximity) ||
            (abs((a.y + a.height) - b.y) < proximity) ||
            (abs(a.y - b.y) < proximity);

        if (horizontal_proximity && vertical_proximity) {
            in_proximity = true;
            
            // Get current time using monotonic clock
            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            uint64_t current_time_ms = now.tv_sec * 1000 + now.tv_nsec / 1000000;

            if (toplevel->proximity_timestamp == 0) {
                // Set initial timestamp
                toplevel->proximity_timestamp = current_time_ms;
                wlr_log(WLR_DEBUG, "Starting proximity timer for windows");
                continue;
            }

            uint64_t elapsed_time = current_time_ms - toplevel->proximity_timestamp;
            // bool state1 = !toplevel->history->tiled;
            // bool state2 = !other->history->tiled;

            if (elapsed_time >= TIME_THRESHOLD_MS) {
                if (!toplevel->group && !other->group) {
                    create_group(toplevel, other);
                    wlr_log(WLR_DEBUG, "Created new group with windows");
                    return true;
                } else if (toplevel->group && other->group && toplevel->group != other->group) {
                    merge_groups(toplevel, other);
                    wlr_log(WLR_DEBUG, "Merged window groups");
                    return true;
                } else if (!other->group) {
                    add_to_group(toplevel->group, other);
                    wlr_log(WLR_DEBUG, "Added window to existing group");
                    return true;
                }
            }
        }
    }

    // Only reset timestamp if not in proximity with any window
    if (!in_proximity) {
        toplevel->proximity_timestamp = 0;
    }

    return false;
}

// the following function is correctly maximising the window
static void xdg_toplevel_request_maximize(
        struct wl_listener *listener, void *data) {
    struct tinywl_toplevel *toplevel =
        wl_container_of(listener, toplevel, request_maximize);
    struct tinywl_server *server = toplevel->server;

    if (!toplevel->xdg_toplevel->base->initialized) return;

    // Toggle maximized state
    bool maximize = !toplevel->maximized;
    toplevel->maximized = maximize;

    if (maximize) {
        // Save current geometry
        toplevel->saved_geometry.x = toplevel->scene_tree->node.x;
        toplevel->saved_geometry.y = toplevel->scene_tree->node.y;
        toplevel->saved_geometry.width = toplevel->xdg_toplevel->current.width;
        toplevel->saved_geometry.height = toplevel->xdg_toplevel->current.height;

        // Get output dimensions
        struct wlr_box layout_box;
        wlr_output_layout_get_box(server->output_layout, NULL, &layout_box);

        // Set maximized state and new size
        wlr_xdg_toplevel_set_maximized(toplevel->xdg_toplevel, true);
        wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel, 
            layout_box.width, layout_box.height);
        wlr_scene_node_set_position(&toplevel->scene_tree->node,
            layout_box.x, layout_box.y);
    } else {
        // Restore original state
        wlr_xdg_toplevel_set_maximized(toplevel->xdg_toplevel, false);
        wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel,
            toplevel->saved_geometry.width,
            toplevel->saved_geometry.height);
        wlr_scene_node_set_position(&toplevel->scene_tree->node,
            toplevel->saved_geometry.x,
            toplevel->saved_geometry.y);
    }

    // Send configure event immediately
    wlr_xdg_surface_schedule_configure(toplevel->xdg_toplevel->base);

    // Force immediate redraw
    struct tinywl_output *output;
    wl_list_for_each(output, &server->outputs, link) {
        wlr_scene_output_commit(output->scene_output, NULL);
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        wlr_scene_output_send_frame_done(output->scene_output, &now);
    }
}

/* --- Helper: Get the currently focused toplevel --- */
static struct tinywl_toplevel *get_focused_toplevel(struct tinywl_server *server) {
    struct wlr_surface *focused = server->seat->keyboard_state.focused_surface;
    struct tinywl_toplevel *toplevel;
    wl_list_for_each(toplevel, &server->toplevels, link) {
        if (toplevel->xdg_toplevel->base->surface == focused)
            return toplevel;
    }
    return NULL;
}

/* --- Helper: get full output geometry (assumes single output for simplicity) --- */
static void get_full_output_geometry(struct tinywl_server *server, struct wlr_box *box) {
    wlr_output_layout_get_box(server->output_layout, NULL, box);
}


static int reset_snap_mode(void *data) {
   struct tinywl_server *server = data;
   
   wlr_log(WLR_DEBUG, "Snap mode timed out");
   snap_mode = false;
   snap_window1 = NULL;
   snap_timer = NULL;
   
   // Reset cursor
   struct tinywl_toplevel *toplevel = get_focused_toplevel(server);
   if (toplevel) {
       focus_toplevel(toplevel);
   }
   
   return 0; // Return 0 to indicate the timer should be destroyed
}
 
static void focus_toplevel(struct tinywl_toplevel *toplevel) {
    if (!toplevel) return;

    struct tinywl_server *server = toplevel->server;
    struct wlr_seat *seat = server->seat;
    struct wlr_surface *surface = toplevel->xdg_toplevel->base->surface;
    struct wlr_surface *prev = seat->keyboard_state.focused_surface;

    if (prev == surface) return;

    // Deactivate previous window
    if (prev) {
        struct wlr_xdg_toplevel *prev_toplevel =
            wlr_xdg_toplevel_try_from_wlr_surface(prev);
        if (prev_toplevel) {
            wlr_xdg_toplevel_set_activated(prev_toplevel, false);
            
            // Update foreign toplevel for window bar
            struct tinywl_toplevel *prev_tinywl = get_toplevel_for_surface(server, prev);
            if (prev_tinywl && prev_tinywl->foreign_handle) {
                wlr_foreign_toplevel_handle_v1_set_activated(
                    prev_tinywl->foreign_handle, false);
            }
        }
    }

    // Activate new window
    wlr_xdg_toplevel_set_activated(toplevel->xdg_toplevel, true);
    
    // Update foreign toplevel for window bar
    if (toplevel->foreign_handle) {
        wlr_foreign_toplevel_handle_v1_set_activated(
            toplevel->foreign_handle, true);
        wlr_foreign_toplevel_handle_v1_set_maximized(
            toplevel->foreign_handle, toplevel->maximized);
    }

    /* Update cursor */
    if (snap_mode) {
        wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "crosshair");
    } else if (toplevel->group) {
        wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "grabbing");
    } else {
        wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "default");
    }

    /* Reorder window list */
    if (!cycling_mode) {
        wl_list_remove(&toplevel->link);
        wl_list_insert(&server->toplevels, &toplevel->link);
    }

    /* Raise window and focus */
    wlr_scene_node_raise_to_top(&toplevel->scene_tree->node);
    
    struct wlr_keyboard *kbd = wlr_seat_get_keyboard(seat);
    if (kbd) {
        wlr_seat_keyboard_notify_enter(seat, surface,
            kbd->keycodes, kbd->num_keycodes,
            &kbd->modifiers);
    }
}


static void keyboard_handle_modifiers(
		struct wl_listener *listener, void *data) {
	/* This event is raised when a modifier key, such as shift or alt, is
	 * pressed. We simply communicate this to the client. */
	struct tinywl_keyboard *keyboard =
		wl_container_of(listener, keyboard, modifiers);
	/*
	 * A seat can only have one keyboard, but this is a limitation of the
	 * Wayland protocol - not wlroots. We assign all connected keyboards to the
	 * same seat. You can swap out the underlying wlr_keyboard like this and
	 * wlr_seat handles this transparently.
	 */
	wlr_seat_set_keyboard(keyboard->server->seat, keyboard->wlr_keyboard);
	/* Send modifiers to the client. */
	wlr_seat_keyboard_notify_modifiers(keyboard->server->seat,
		&keyboard->wlr_keyboard->modifiers);
}


void tile_top_two(struct tinywl_server *server) {
    int count = wl_list_length(&server->toplevels);
    if (count < 2) {
        wlr_log(WLR_INFO, "Not enough windows to tile top two.");
        return;
    }

    /* Retrieve the first two windows in the list */
    struct tinywl_toplevel *first = NULL, *second = NULL;
    struct tinywl_toplevel *toplevel;
    int i = 0;
    wl_list_for_each(toplevel, &server->toplevels, link) {
        if (i == 0)
            first = toplevel;
        else if (i == 1)
            second = toplevel;
        i++;
        if (i >= 2)
            break;
    }
    if (!first || !second)
        return;
    
    struct wlr_box layout_box;
    wlr_output_layout_get_box(server->output_layout, NULL, &layout_box);
    
    int half_width = layout_box.width / 2;
    
    /* Resize and position first window to the left half */
    wlr_xdg_toplevel_set_size(first->xdg_toplevel, half_width, layout_box.height);
    wlr_scene_node_set_position(&first->scene_tree->node, layout_box.x, layout_box.y);
    
    /* Resize and position second window to the right half */
    wlr_xdg_toplevel_set_size(second->xdg_toplevel, half_width, layout_box.height);
    wlr_scene_node_set_position(&second->scene_tree->node,
                                layout_box.x + half_width, layout_box.y);
    
    /* Optionally set focus on the left window */
    focus_toplevel(first);
    
    wlr_log(WLR_INFO, "Tiled top two windows side by side.");
}

static void tile_window(struct tinywl_server *server, xkb_keysym_t sym) {
    struct tinywl_toplevel *toplevel = get_focused_toplevel(server);
    if (!toplevel)
        return;

    struct wlr_box full;
    get_full_output_geometry(server, &full);
    int full_x = full.x, full_y = full.y, full_w = full.width, full_h = full.height;

    int cx = toplevel->scene_tree->node.x;
    int cy = toplevel->scene_tree->node.y;
    int cw = toplevel->xdg_toplevel->current.width;
    int ch = toplevel->xdg_toplevel->current.height;

    char h = toplevel->last_horizontal;
    char v = toplevel->last_vertical;

    #define APPLY_GEOM(nx, ny, nw, nh, nhoriz, nvert)  \
        do {                                          \
            push_tiling_state(toplevel);              \
            wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel, (nw), (nh)); \
            wlr_scene_node_set_position(&toplevel->scene_tree->node, (nx), (ny)); \
            toplevel->tiled = true;                   \
            toplevel->last_horizontal = (nhoriz);     \
            toplevel->last_vertical = (nvert);          \
            toplevel->tiled_geometry.x = (nx);          \
            toplevel->tiled_geometry.y = (ny);          \
            toplevel->tiled_geometry.width = (nw);      \
            toplevel->tiled_geometry.height = (nh);     \
            wlr_log(WLR_DEBUG, "Tiled: (%d,%d,%d,%d) state: %c %c", (nx), (ny), (nw), (nh), (nhoriz), (nvert)); \
        } while (0)

    /* The following tiling logic is the same as before.
       (Initial state, half state, quadrant state with extra subdivisions.)
       For each change, the macro APPLY_GEOM pushes the current state into the history. */

    if (!toplevel->tiled) {
        switch (sym) {
            case XKB_KEY_Left:
                APPLY_GEOM(full_x, full_y, full_w / 2, full_h, 'L', 0);
                break;
            case XKB_KEY_Right:
                APPLY_GEOM(full_x + full_w / 2, full_y, full_w / 2, full_h, 'R', 0);
                break;
            case XKB_KEY_Up:
                APPLY_GEOM(full_x, full_y, full_w, full_h / 2, 0, 'U');
                break;
            case XKB_KEY_Down:
                APPLY_GEOM(full_x, full_y + full_h / 2, full_w, full_h / 2, 0, 'D');
                break;
        }
        return;
    }

    bool isHalf = (((h == 'L' || h == 'R') && v == 0) || ((v == 'U' || v == 'D') && h == 0));
    bool isQuadrant = ((h == 'L' || h == 'R') && (v == 'U' || v == 'D'));

    if (isHalf) {
        if (h == 'R' && v == 0) {
            switch (sym) {
                case XKB_KEY_Up:
                    APPLY_GEOM(full_x + full_w / 2, full_y, full_w / 2, full_h / 2, 'R', 'U');
                    break;
                case XKB_KEY_Down:
                    APPLY_GEOM(full_x + full_w / 2, full_y + full_h / 2, full_w / 2, full_h / 2, 'R', 'D');
                    break;
                case XKB_KEY_Left:
                    APPLY_GEOM(full_x, full_y, full_w / 2, full_h, 'L', 0);
                    break;
                case XKB_KEY_Right:
                    APPLY_GEOM(full_x + full_w / 2, full_y, full_w / 2, full_h, 'R', 0);
                    break;
            }
        } else if (h == 'L' && v == 0) {
            switch (sym) {
                case XKB_KEY_Up:
                    APPLY_GEOM(full_x, full_y, full_w / 2, full_h / 2, 'L', 'U');
                    break;
                case XKB_KEY_Down:
                    APPLY_GEOM(full_x, full_y + full_h / 2, full_w / 2, full_h / 2, 'L', 'D');
                    break;
                case XKB_KEY_Right:
                    APPLY_GEOM(full_x + full_w / 2, full_y, full_w / 2, full_h, 'R', 0);
                    break;
                case XKB_KEY_Left:
                    APPLY_GEOM(full_x, full_y, full_w / 2, full_h, 'L', 0);
                    break;
            }
        } else if (v == 'U' && h == 0) {
            switch (sym) {
                case XKB_KEY_Up: {
                    int top_half_height = full_h / 2;
                    APPLY_GEOM(full_x, full_y, full_w, top_half_height / 2, 0, 'U');
                    break;
                }
                case XKB_KEY_Down:
                    APPLY_GEOM(full_x, full_y + full_h / 2, full_w, full_h / 2, 0, 'D');
                    break;
                case XKB_KEY_Left:
                    APPLY_GEOM(full_x, full_y, full_w / 2, full_h, 'L', 0);
                    break;
                case XKB_KEY_Right:
                    APPLY_GEOM(full_x + full_w / 2, full_y, full_w / 2, full_h, 'R', 0);
                    break;
            }
        } else if (v == 'D' && h == 0) {
            switch (sym) {
                case XKB_KEY_Down: {
                    int bottom_half_y = full_y + full_h / 2;
                    int bottom_half_height = full_h / 2;
                    APPLY_GEOM(full_x, bottom_half_y + bottom_half_height / 2, full_w, bottom_half_height / 2, 0, 'D');
                    break;
                }
                case XKB_KEY_Up:
                    APPLY_GEOM(full_x, full_y, full_w, full_h / 2, 0, 'U');
                    break;
                case XKB_KEY_Left:
                    APPLY_GEOM(full_x, full_y, full_w / 2, full_h, 'L', 0);
                    break;
                case XKB_KEY_Right:
                    APPLY_GEOM(full_x + full_w / 2, full_y, full_w / 2, full_h, 'R', 0);
                    break;
            }
        }
        return;
    }

    if (isQuadrant) {
        if (h == 'R' && v == 'U') {
            switch (sym) {
                case XKB_KEY_Right:
                    if (!toplevel->h_subdivided) {
                        APPLY_GEOM(cx + cw / 2, cy, cw / 2, ch, 'R', 'U');
                        toplevel->h_subdivided = true;
                    }
                    break;
                case XKB_KEY_Up:
                    if (!toplevel->v_subdivided) {
                        APPLY_GEOM(cx, cy, cw, ch / 2, 'R', 'U');
                        toplevel->v_subdivided = true;
                    }
                    break;
                case XKB_KEY_Left:
                    APPLY_GEOM(full_x, full_y, full_w / 2, full_h, 'L', 0);
                    toplevel->h_subdivided = false;
                    toplevel->v_subdivided = false;
                    break;
                case XKB_KEY_Down:
                    APPLY_GEOM(full_x + full_w / 2, full_y, full_w / 2, full_h, 'R', 0);
                    toplevel->h_subdivided = false;
                    toplevel->v_subdivided = false;
                    break;
            }
        } else if (h == 'L' && v == 'U') {
            switch (sym) {
                case XKB_KEY_Left:
                    if (!toplevel->h_subdivided) {
                        APPLY_GEOM(cx, cy, cw / 2, ch, 'L', 'U');
                        toplevel->h_subdivided = true;
                    }
                    break;
                case XKB_KEY_Up:
                    if (!toplevel->v_subdivided) {
                        APPLY_GEOM(cx, cy, cw, ch / 2, 'L', 'U');
                        toplevel->v_subdivided = true;
                    }
                    break;
                case XKB_KEY_Right:
                    APPLY_GEOM(full_x + full_w / 2, full_y, full_w / 2, full_h, 'R', 0);
                    toplevel->h_subdivided = false;
                    toplevel->v_subdivided = false;
                    break;
                case XKB_KEY_Down:
                    APPLY_GEOM(full_x, full_y, full_w / 2, full_h, 'L', 0);
                    toplevel->h_subdivided = false;
                    toplevel->v_subdivided = false;
                    break;
            }
        } else if (h == 'L' && v == 'D') {
            switch (sym) {
                case XKB_KEY_Left:
                    if (!toplevel->h_subdivided) {
                        APPLY_GEOM(cx, cy, cw / 2, ch, 'L', 'D');
                        toplevel->h_subdivided = true;
                    }
                    break;
                case XKB_KEY_Down:
                    if (!toplevel->v_subdivided) {
                        APPLY_GEOM(cx, cy + ch / 2, cw, ch / 2, 'L', 'D');
                        toplevel->v_subdivided = true;
                    }
                    break;
                case XKB_KEY_Right:
                    APPLY_GEOM(full_x + full_w / 2, full_y, full_w / 2, full_h, 'R', 0);
                    toplevel->h_subdivided = false;
                    toplevel->v_subdivided = false;
                    break;
                case XKB_KEY_Up:
                    APPLY_GEOM(full_x, full_y, full_w / 2, full_h, 'L', 0);
                    toplevel->h_subdivided = false;
                    toplevel->v_subdivided = false;
                    break;
            }
        } else if (h == 'R' && v == 'D') {
            switch (sym) {
                case XKB_KEY_Right:
                    if (!toplevel->h_subdivided) {
                        APPLY_GEOM(cx + cw / 2, cy, cw / 2, ch, 'R', 'D');
                        toplevel->h_subdivided = true;
                    }
                    break;
                case XKB_KEY_Down:
                    if (!toplevel->v_subdivided) {
                        APPLY_GEOM(cx, cy + ch / 2, cw, ch / 2, 'R', 'D');
                        toplevel->v_subdivided = true;
                    }
                    break;
                case XKB_KEY_Left:
                    APPLY_GEOM(full_x, full_y, full_w / 2, full_h, 'L', 0);
                    toplevel->h_subdivided = false;
                    toplevel->v_subdivided = false;
                    break;
                case XKB_KEY_Up:
                    APPLY_GEOM(full_x + full_w / 2, full_y, full_w / 2, full_h, 'R', 0);
                    toplevel->h_subdivided = false;
                    toplevel->v_subdivided = false;
                    break;
            }
        }
        return;
    }
    #undef APPLY_GEOM
}


static void revert_tile_window(struct tinywl_server *server) {
    struct tinywl_toplevel *toplevel = get_focused_toplevel(server);
    if (!toplevel)
        return;

    struct tiling_state prev_state;
    if (pop_tiling_state(toplevel, &prev_state)) {
        wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel, prev_state.width, prev_state.height);
        wlr_scene_node_set_position(&toplevel->scene_tree->node, prev_state.x, prev_state.y);
        toplevel->last_horizontal = prev_state.last_horizontal;
        toplevel->last_vertical = prev_state.last_vertical;
        toplevel->tiled = prev_state.tiled;
        toplevel->h_subdivided = prev_state.h_subdivided;
        toplevel->v_subdivided = prev_state.v_subdivided;
        wlr_log(WLR_DEBUG, "Reverted to previous tiling state.");
    } else {
        // No previous state: revert to the initial stacked configuration.
        // Instead of reverting to full output dimensions (i.e. maximized),
        // we restore a default "stacked" configuration.
        struct wlr_box full;
        get_full_output_geometry(server, &full);

        int default_width = full.width/2;    // Set your desired stacked window width
        int default_height = full.height;   // Set your desired stacked window height

        // Center the default stacked window on the output.
        int x = full.x + (full.width - default_width) / 2;
        int y = full.y + (full.height - default_height) / 2;

        wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel, default_width, default_height);
        wlr_scene_node_set_position(&toplevel->scene_tree->node, x, y);

        toplevel->tiled = false;
        toplevel->last_horizontal = 0;
        toplevel->last_vertical = 0;
        toplevel->h_subdivided = false;
        toplevel->v_subdivided = false;
        toplevel->history_len = 0;
        wlr_log(WLR_DEBUG, "Reverted to initial stacked configuration.");
    }
}

// #undef APPLY_GEOM

// --- Modified keybinding handler: call tile_window() for arrow keys.
static bool handle_keybinding(struct tinywl_server *server, xkb_keysym_t sym) {
    switch (sym) {
    case XKB_KEY_Escape:
        wl_display_terminate(server->wl_display);
        break;
    // For the four arrow keys, use our new tiling logic:
    case XKB_KEY_Up:
    case XKB_KEY_Down:
    case XKB_KEY_Left:
    case XKB_KEY_Right:
        tile_window(server, sym);
        return true;
    case XKB_KEY_r:
        revert_tile_window(server);
        return true;
    /* Other keybindings remain unchanged */
    case XKB_KEY_b:
        system("firefox &");
        break;
        
    case XKB_KEY_5:
        take_screenshot();
        return true;
        
    case XKB_KEY_t:
        system("foot &");
        break;
        
    case XKB_KEY_c:
        system("calendar &");
        break;
    
    case XKB_KEY_m:
        system("thunderbird &");
        break;

    case XKB_KEY_BackSpace:
        struct tinywl_toplevel *toplevel;
        wl_list_for_each(toplevel, &server->toplevels, link) {
            if(toplevel->group){
                remove_from_group(toplevel);
                wlr_log(WLR_DEBUG, "Desnapped window: %p", toplevel);
            }
        }
        break;
    case XKB_KEY_Tab:
    case XKB_KEY_F1:
        if (!cycle_windows(server))
            wlr_log(WLR_INFO, "Window cycle failed: not enough windows");
        break;
    case XKB_KEY_p:
        wlr_log(WLR_DEBUG, "Keybinding Alt+P triggered: spawning fuzzel");
        spawn_fuzzel();
        break;
    case XKB_KEY_plus:
       wlr_log(WLR_DEBUG, "Keybinding Alt+P triggered: spawning fuzzel");
       spawn_sfwbar();
       break;
    
    case XKB_KEY_f:
    {
        struct tinywl_toplevel *toplevel = get_focused_toplevel(server);
        if (toplevel) {
            // Toggle maximize state
            bool maximize = !toplevel->maximized;
            
            // Set the maximized state
            wlr_xdg_toplevel_set_maximized(toplevel->xdg_toplevel, maximize);
            
            if (maximize) {
                // Save current geometry
                toplevel->saved_geometry.x = toplevel->scene_tree->node.x;
                toplevel->saved_geometry.y = toplevel->scene_tree->node.y;
                toplevel->saved_geometry.width = toplevel->xdg_toplevel->current.width;
                toplevel->saved_geometry.height = toplevel->xdg_toplevel->current.height;
                
                // Get output dimensions
                struct wlr_box box;
                wlr_output_layout_get_box(server->output_layout, NULL, &box);
                
                // Maximize window
                wlr_scene_node_set_position(&toplevel->scene_tree->node, box.x, box.y);
                wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel, box.width, box.height);
            } else {
                // Restore original geometry
                wlr_scene_node_set_position(&toplevel->scene_tree->node,
                    toplevel->saved_geometry.x,
                    toplevel->saved_geometry.y);
                wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel,
                    toplevel->saved_geometry.width,
                    toplevel->saved_geometry.height);
            }
            
            toplevel->maximized = maximize;
            return true;
        }
        return false;
        }


    case XKB_KEY_1: // Option+1 to select first window
        if (snap_mode) {
            wlr_log(WLR_DEBUG, "Option+1 pressed while already in snap mode, spawning sfwbar.");
            spawn_sfwbar(); // Keep existing behavior when already in snap mode
        } else {
            struct tinywl_toplevel *toplevel = get_focused_toplevel(server);
            if (toplevel) {
                // Cancel any existing timer if re-selecting the first window quickly
                if (snap_timer) {
                    wl_event_source_remove(snap_timer);
                    snap_timer = NULL;
                    wlr_log(WLR_DEBUG, "Cancelled previous snap timer.");
                }

                snap_window1 = toplevel;
                snap_mode = true;
                wlr_log(WLR_DEBUG, "Selected window %p for snapping (press Option+2 on target)", (void*)toplevel);

                // Set new timer (e.g., 5 seconds) to automatically cancel snap mode
                struct wl_event_loop *loop = wl_display_get_event_loop(server->wl_display);
                snap_timer = wl_event_loop_add_timer(loop, reset_snap_mode, server);
                if(snap_timer) {
                    wl_event_source_timer_update(snap_timer, 5000); // 5000 ms = 5 seconds
                    wlr_log(WLR_DEBUG, "Set snap mode timer for 5 seconds.");
                } else {
                    wlr_log(WLR_ERROR, "Failed to create snap mode timer.");
                    // Reset state if timer creation failed
                    snap_mode = false;
                    snap_window1 = NULL;
                    // No need to reset cursor here, focus_toplevel below handles it if toplevel exists
                }

                // Update cursor to indicate snap mode is active
                wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "crosshair");

            } else {
                wlr_log(WLR_DEBUG, "Option+1 pressed but no window focused.");
            }
        }
        return true; // Key press handled

    case XKB_KEY_2: // Option+2 to select second window and snap
        if (snap_mode && snap_window1) {
            struct tinywl_toplevel *toplevel2 = get_focused_toplevel(server); // Renamed to avoid confusion

            if (toplevel2 && toplevel2 != snap_window1) {
                // *** ADDED CHECK: Prevent snapping if either window is already tiled ***
                if (snap_window1->tiled || toplevel2->tiled) { // Check the 'tiled' flag [1]
                    wlr_log(WLR_INFO, "Manual snap skipped: Window %p (tiled: %d) or %p (tiled: %d) is already in a tiled state.",
                            (void*)snap_window1, snap_window1->tiled, (void*)toplevel2, toplevel2->tiled);
                } else {
                    // Only call manual_snap_windows if NEITHER window is currently tiled
                    wlr_log(WLR_DEBUG, "Attempting to manually snap %p and %p.", (void*)snap_window1, (void*)toplevel2);
                    manual_snap_windows(snap_window1, toplevel2);
                }
            } else {
                    if (!toplevel2) {
                    wlr_log(WLR_DEBUG, "Option+2 pressed in snap mode, but no second window focused.");
                    } else { // toplevel2 == snap_window1
                    wlr_log(WLR_DEBUG, "Option+2 pressed in snap mode, but target window is the same as the first selected window.");
                    }
            }

            // --- Cleanup logic (always runs after Option+2 in snap mode) ---

            // Clean up the timer regardless of whether snapping occurred
            if (snap_timer) {
                wl_event_source_remove(snap_timer);
                snap_timer = NULL;
                wlr_log(WLR_DEBUG, "Removed snap timer after Option+2.");
            }

            // Reset snap mode state
            snap_mode = false;
            snap_window1 = NULL;
            wlr_log(WLR_DEBUG, "Exited snap mode.");

            // Reset cursor by refocusing (focus_toplevel checks snap_mode)
            // If toplevel2 exists, focus it. Otherwise, try to refocus whatever is currently focused.
            struct tinywl_toplevel *focus_target = toplevel2 ? toplevel2 : get_focused_toplevel(server);
            if (focus_target) {
                focus_toplevel(focus_target); // This will reset the cursor based on the now false snap_mode
            } else {
                    // Fallback if no window can be focused
                    wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "default");
            }
            // --- End cleanup logic ---

        } else {
            // If Option+2 is pressed when *not* in snap mode, run fuzzel (existing behavior)
            wlr_log(WLR_DEBUG, "Option+2 pressed outside snap mode, spawning fuzzel.");
            spawn_fuzzel();
        }
        return true; // Key press handled

    // ... other case statements like default ... [1]
    // Indicate key was not handled by this compositor-level handler


    default:
        if (server->keybinding_handler) {
            server->keybinding_handler(sym);
            return true;
        }
        return false;
    }
    return true;
}


static void keyboard_handle_key(
		struct wl_listener *listener, void *data) {
	/* This event is raised when a key is pressed or released. */
	struct tinywl_keyboard *keyboard =
		wl_container_of(listener, keyboard, key);
	struct tinywl_server *server = keyboard->server;
	struct wlr_keyboard_key_event *event = data;
	struct wlr_seat *seat = server->seat;

	/* Translate libinput keycode -> xkbcommon */
	uint32_t keycode = event->keycode + 8;
	/* Get a list of keysyms based on the keymap for this keyboard */
	const xkb_keysym_t *syms;
	int nsyms = xkb_state_key_get_syms(
			keyboard->wlr_keyboard->xkb_state, keycode, &syms);

	bool handled = false;
	uint32_t modifiers = wlr_keyboard_get_modifiers(keyboard->wlr_keyboard);
	if ((modifiers & WLR_MODIFIER_ALT) &&
			event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
		/* If alt is held down and this button was _pressed_, we attempt to
		 * process it as a compositor keybinding. */
		for (int i = 0; i < nsyms; i++) {
			handled = handle_keybinding(server, syms[i]);
		}
	}

	if (!handled) {
		/* Otherwise, we pass it along to the client. */
		wlr_seat_set_keyboard(seat, keyboard->wlr_keyboard);
		wlr_seat_keyboard_notify_key(seat, event->time_msec,
			event->keycode, event->state);
	}
}


static void keyboard_handle_destroy(struct wl_listener *listener, void *data) {
	/* This event is raised by the keyboard base wlr_input_device to signal
	 * the destruction of the wlr_keyboard. It will no longer receive events
	 * and should be destroyed.
	 */
	struct tinywl_keyboard *keyboard =
		wl_container_of(listener, keyboard, destroy);
	wl_list_remove(&keyboard->modifiers.link);
	wl_list_remove(&keyboard->key.link);
	wl_list_remove(&keyboard->destroy.link);
	wl_list_remove(&keyboard->link);
	free(keyboard);
}

static void server_new_keyboard(struct tinywl_server *server,
		struct wlr_input_device *device) {
	struct wlr_keyboard *wlr_keyboard = wlr_keyboard_from_input_device(device);

	struct tinywl_keyboard *keyboard = calloc(1, sizeof(*keyboard));
	keyboard->server = server;
	keyboard->wlr_keyboard = wlr_keyboard;

	/* We need to prepare an XKB keymap and assign it to the keyboard. This
	 * assumes the defaults (e.g. layout = "us"). */
	struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	struct xkb_keymap *keymap = xkb_keymap_new_from_names(context, NULL,
		XKB_KEYMAP_COMPILE_NO_FLAGS);

	wlr_keyboard_set_keymap(wlr_keyboard, keymap);
	xkb_keymap_unref(keymap);
	xkb_context_unref(context);
	wlr_keyboard_set_repeat_info(wlr_keyboard, 25, 600);

	/* Here we set up listeners for keyboard events. */
	keyboard->modifiers.notify = keyboard_handle_modifiers;
	wl_signal_add(&wlr_keyboard->events.modifiers, &keyboard->modifiers);
	keyboard->key.notify = keyboard_handle_key;
	wl_signal_add(&wlr_keyboard->events.key, &keyboard->key);
	keyboard->destroy.notify = keyboard_handle_destroy;
	wl_signal_add(&device->events.destroy, &keyboard->destroy);

	wlr_seat_set_keyboard(server->seat, keyboard->wlr_keyboard);
	//z-added
	wlr_seat_set_capabilities(server->seat,
    WL_SEAT_CAPABILITY_POINTER | WL_SEAT_CAPABILITY_KEYBOARD);
	/* And add the keyboard to our list of keyboards */
	wl_list_insert(&server->keyboards, &keyboard->link);
}

static void server_new_pointer(struct tinywl_server *server,
		struct wlr_input_device *device) {
	/* We don't do anything special with pointers. All of our pointer handling
	 * is proxied through wlr_cursor. On another compositor, you might take this
	 * opportunity to do libinput configuration on the device to set
	 * acceleration, etc. */
	wlr_cursor_attach_input_device(server->cursor, device);
}

static void server_new_input(struct wl_listener *listener, void *data) {
	/* This event is raised by the backend when a new input device becomes
	 * available. */
	struct tinywl_server *server =
		wl_container_of(listener, server, new_input);
	struct wlr_input_device *device = data;
	switch (device->type) {
	case WLR_INPUT_DEVICE_KEYBOARD:
		server_new_keyboard(server, device);
		break;
	case WLR_INPUT_DEVICE_POINTER:
		server_new_pointer(server, device);
		break;
	default:
		break;
	}
	/* We need to let the wlr_seat know what our capabilities are, which is
	 * communiciated to the client. In TinyWL we always have a cursor, even if
	 * there are no pointer devices, so we always include that capability. */
	uint32_t caps = WL_SEAT_CAPABILITY_POINTER;
	if (!wl_list_empty(&server->keyboards)) {
		caps |= WL_SEAT_CAPABILITY_KEYBOARD;
	}
	wlr_seat_set_capabilities(server->seat, caps);
}

static void seat_request_cursor(struct wl_listener *listener, void *data) {
	struct tinywl_server *server = wl_container_of(
			listener, server, request_cursor);
	/* This event is raised by the seat when a client provides a cursor image */
	struct wlr_seat_pointer_request_set_cursor_event *event = data;
	struct wlr_seat_client *focused_client =
		server->seat->pointer_state.focused_client;
	/* This can be sent by any client, so we check to make sure this one is
	 * actually has pointer focus first. */
	if (focused_client == event->seat_client) {
		/* Once we've vetted the client, we can tell the cursor to use the
		 * provided surface as the cursor image. It will set the hardware cursor
		 * on the output that it's currently on and continue to do so as the
		 * cursor moves between outputs. */
		wlr_cursor_set_surface(server->cursor, event->surface,
				event->hotspot_x, event->hotspot_y);
	}
}

static void seat_request_set_selection(struct wl_listener *listener, void *data) {
	/* This event is raised by the seat when a client wants to set the selection,
	 * usually when the user copies something. wlroots allows compositors to
	 * ignore such requests if they so choose, but in tinywl we always honor
	 */
	struct tinywl_server *server = wl_container_of(
			listener, server, request_set_selection);
	struct wlr_seat_request_set_selection_event *event = data;
	wlr_seat_set_selection(server->seat, event->source, event->serial);
}

static struct tinywl_toplevel *desktop_toplevel_at(
    struct tinywl_server *server, double lx, double ly,
    struct wlr_surface **surface, double *sx, double *sy) {
    /* This returns the topmost node in the scene at the given layout coords.
     * We only care about surface nodes as we are specifically looking for a
     * surface in the surface tree of a tinywl_toplevel. */
    struct wlr_scene_node *node = wlr_scene_node_at(
        &server->scene->tree.node, lx, ly, sx, sy);
    
    if (node == NULL || node->type != WLR_SCENE_NODE_BUFFER) {
        return NULL;
    }

    struct wlr_scene_buffer *scene_buffer = wlr_scene_buffer_from_node(node);
    struct wlr_scene_surface *scene_surface =
        wlr_scene_surface_try_from_buffer(scene_buffer);
    
    if (!scene_surface) {
        return NULL;
    }

    *surface = scene_surface->surface;

    /* Skip layer surfaces (window decorations) */
    if (wlr_layer_surface_v1_try_from_wlr_surface(*surface)) {
        return NULL;
    }

    /* Find the node corresponding to the tinywl_toplevel at the root of this
     * surface tree, it is the only one for which we set the data field. */
    struct wlr_scene_tree *tree = node->parent;
    while (tree != NULL && tree->node.data == NULL) {
        tree = tree->node.parent;
    }

    return tree->node.data;
}


static void reset_cursor_mode(struct tinywl_server *server) {
	/* Reset the cursor mode to passthrough. */
	server->cursor_mode = TINYWL_CURSOR_PASSTHROUGH;
	server->grabbed_toplevel = NULL;
}

// modified
static void process_cursor_resize(struct tinywl_server *server) {
    struct tinywl_toplevel *toplevel = server->grabbed_toplevel;
	if (!toplevel) {
        return;
    }
    double border_x = server->cursor->x - server->grab_x;
    double border_y = server->cursor->y - server->grab_y;
    
    // Calculate group-aware resize parameters
    struct wlr_box group_geo;
    if (toplevel->group) {
        // Calculate group bounding box
        group_geo.x = INT_MAX;
        group_geo.y = INT_MAX;
        int right = INT_MIN;
        int bottom = INT_MIN;
        
        struct tinywl_toplevel *member;
        wl_list_for_each(member, &toplevel->group->toplevels, group_link) {
            struct wlr_box m_geo = {
                .x = member->scene_tree->node.x,
                .y = member->scene_tree->node.y,
                .width = member->xdg_toplevel->current.width,
                .height = member->xdg_toplevel->current.height
            };
            
            group_geo.x = MIN(group_geo.x, m_geo.x);
            group_geo.y = MIN(group_geo.y, m_geo.y);
            right = MAX(right, m_geo.x + m_geo.width);
            bottom = MAX(bottom, m_geo.y + m_geo.height);
        }
        group_geo.width = right - group_geo.x;
        group_geo.height = bottom - group_geo.y;
    } else {
        group_geo = server->grab_geobox;
    }

    // Calculate resize deltas based on group geometry
    int new_left = group_geo.x;
    int new_right = group_geo.x + group_geo.width;
    int new_top = group_geo.y;
    int new_bottom = group_geo.y + group_geo.height;

    if (server->resize_edges & WLR_EDGE_TOP) {
        new_top = border_y;
        if (new_top >= new_bottom) new_top = new_bottom - 1;
    }
    if (server->resize_edges & WLR_EDGE_BOTTOM) {
        new_bottom = border_y;
        if (new_bottom <= new_top) new_bottom = new_top + 1;
    }
    if (server->resize_edges & WLR_EDGE_LEFT) {
        new_left = border_x;
        if (new_left >= new_right) new_left = new_right - 1;
    }
    if (server->resize_edges & WLR_EDGE_RIGHT) {
        new_right = border_x;
        if (new_right <= new_left) new_right = new_left + 1;
    }

    // Calculate group transformation
    const float width_scale = (new_right - new_left) / (float)group_geo.width;
    const float height_scale = (new_bottom - new_top) / (float)group_geo.height;


    if (toplevel->group) {
        struct tinywl_toplevel *member;
        wl_list_for_each(member, &toplevel->group->toplevels, group_link) {
            // Calculate member's relative position within group
            float rel_x = (member->scene_tree->node.x - group_geo.x) / (float)group_geo.width;
            float rel_y = (member->scene_tree->node.y - group_geo.y) / (float)group_geo.height;
            
            // Apply scaled transformation
            int new_x = new_left + rel_x * (new_right - new_left);
            int new_y = new_top + rel_y * (new_bottom - new_top);
            int new_width = member->xdg_toplevel->current.width * width_scale;
            int new_height = member->xdg_toplevel->current.height * height_scale;

            wlr_scene_node_set_position(&member->scene_tree->node, new_x, new_y);
            wlr_xdg_toplevel_set_size(member->xdg_toplevel, new_width, new_height);
        }
    } else {
        // Existing single window resize logic
        wlr_scene_node_set_position(&toplevel->scene_tree->node,
            new_left - toplevel->xdg_toplevel->base->geometry.x,
            new_top - toplevel->xdg_toplevel->base->geometry.y);
        wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel, 
            new_right - new_left, new_bottom - new_top);
    }

    check_proximity(toplevel);
}

//differing from zainab

static void process_cursor_motion(struct tinywl_server *server, uint32_t time) {
    /* Handle interactive modes first */
    if (server->cursor_mode == TINYWL_CURSOR_MOVE) {
        process_cursor_move(server);
        return;
    } else if (server->cursor_mode == TINYWL_CURSOR_RESIZE) {
        process_cursor_resize(server);
        return;
    }

    double sx, sy;
    struct wlr_seat *seat = server->seat;
    struct wlr_surface *surface = NULL;

    /* First check for layer surfaces (like window decorations) */
    struct wlr_scene_node *node = wlr_scene_node_at(
        &server->scene->tree.node, server->cursor->x, server->cursor->y, &sx, &sy);
    
    if (node && node->type == WLR_SCENE_NODE_BUFFER) {
        struct wlr_scene_surface *scene_surface = wlr_scene_surface_try_from_buffer(
            wlr_scene_buffer_from_node(node));
        
        if (scene_surface) {
            surface = scene_surface->surface;
            struct wlr_layer_surface_v1 *layer_surface =
                wlr_layer_surface_v1_try_from_wlr_surface(surface);
            
            if (layer_surface) {
                /* Handle layer surface focus */
                bool has_focus = seat->pointer_state.focused_surface == surface;
                if (!has_focus) {
                    wlr_seat_pointer_notify_enter(seat, surface, sx, sy);
                }
                wlr_seat_pointer_notify_motion(seat, time, sx, sy);
                return;
            }
        }
    }

    /* Handle regular toplevel windows */
    struct tinywl_toplevel *toplevel = desktop_toplevel_at(server,
        server->cursor->x, server->cursor->y, &surface, &sx, &sy);

    if (surface) {
        /* Notify seat of pointer enter/motion */
        wlr_seat_pointer_notify_enter(seat, surface, sx, sy);
        wlr_seat_pointer_notify_motion(seat, time, sx, sy);
        
        /* Only change keyboard focus if clicking or explicitly focusing */
        if (seat->pointer_state.focused_surface != surface) {
            /* Update foreign toplevel state for window bar */
            if (toplevel && toplevel->foreign_handle) {
                wlr_foreign_toplevel_handle_v1_set_activated(
                    toplevel->foreign_handle, true);
            }
        }
    } else {
        /* No surface - clear pointer focus */
        wlr_seat_pointer_clear_focus(seat);
    }

    /* Continuous proximity checks for automatic snapping */
    struct tinywl_toplevel *any_toplevel;
    wl_list_for_each(any_toplevel, &server->toplevels, link) {
        check_proximity(any_toplevel);
    }
}


static void process_cursor_move(struct tinywl_server *server) {
    struct tinywl_toplevel *toplevel = server->grabbed_toplevel;
    double base_dx = server->cursor->x - server->grab_x;
    double base_dy = server->cursor->y - server->grab_y;

    if (toplevel->group) {
        // Move all windows in the group
        double dx = base_dx - toplevel->scene_tree->node.x;
        double dy = base_dy - toplevel->scene_tree->node.y;

        struct tinywl_toplevel *member;
        wl_list_for_each(member, &toplevel->group->toplevels, group_link) {
            wlr_scene_node_set_position(&member->scene_tree->node,
                member->scene_tree->node.x + dx,
                member->scene_tree->node.y + dy);
        }
    } else {
        // Move a single window
        wlr_scene_node_set_position(&toplevel->scene_tree->node, base_dx, base_dy);
    }

    // Check proximity for all toplevels
    struct tinywl_toplevel *any_toplevel;
    wl_list_for_each(any_toplevel, &server->toplevels, link) {
        check_proximity(any_toplevel);
    }
}


// z-added
static struct wlr_surface *desktop_surface_at(struct tinywl_server *server,
        double lx, double ly, double *sx, double *sy) {
    struct wlr_scene_node *node = wlr_scene_node_at(&server->scene->tree.node, lx, ly, sx, sy);
    if (!node) {
        return NULL;
    }
    // Attempt to get a scene buffer from the node.
    struct wlr_scene_buffer *scene_buffer = wlr_scene_buffer_from_node(node);
    if (!scene_buffer) {
        return NULL;
    }
    // Try to get the scene surface from the buffer.
    struct wlr_scene_surface *scene_surface = wlr_scene_surface_try_from_buffer(scene_buffer);
    if (!scene_surface) {
        return NULL;
    }
    return scene_surface->surface;
}



static void server_cursor_motion(struct wl_listener *listener, void *data) {
	/* This event is forwarded by the cursor when a pointer emits a _relative_
	 * pointer motion event (i.e. a delta) */
	struct tinywl_server *server =
		wl_container_of(listener, server, cursor_motion);
	struct wlr_pointer_motion_event *event = data;
	/* The cursor doesn't move unless we tell it to. The cursor automatically
	 * handles constraining the motion to the output layout, as well as any
	 * special configuration applied for the specific input device which
	 * generated the event. You can pass NULL for the device if you want to move
	 * the cursor around without any input. */
	wlr_cursor_move(server->cursor, &event->pointer->base,
			event->delta_x, event->delta_y);
	process_cursor_motion(server, event->time_msec);
}

static void server_cursor_motion_absolute(
		struct wl_listener *listener, void *data) {
	/* This event is forwarded by the cursor when a pointer emits an _absolute_
	 * motion event, from 0..1 on each axis. This happens, for example, when
	 * wlroots is running under a Wayland window rather than KMS+DRM, and you
	 * move the mouse over the window. You could enter the window from any edge,
	 * so we have to warp the mouse there. There is also some hardware which
	 * emits these events. */
	struct tinywl_server *server =
		wl_container_of(listener, server, cursor_motion_absolute);
	struct wlr_pointer_motion_absolute_event *event = data;
	wlr_cursor_warp_absolute(server->cursor, &event->pointer->base, event->x,
		event->y);
	process_cursor_motion(server, event->time_msec);
}

static void server_cursor_button(struct wl_listener *listener, void *data) {
    struct tinywl_server *server =
        wl_container_of(listener, server, cursor_button);
    struct wlr_pointer_button_event *event = data;

    /* Notify the client with pointer focus that a button press has occurred */
    wlr_seat_pointer_notify_button(server->seat,
        event->time_msec, event->button, event->state);

    if (event->state == WL_POINTER_BUTTON_STATE_RELEASED) {
        /* If you released any buttons, we exit interactive move/resize mode. */
        reset_cursor_mode(server);
    } else {
        /* Focus that client if the button was _pressed_ */
        double sx, sy;
        struct wlr_surface *surface = desktop_surface_at(server,
            server->cursor->x, server->cursor->y, &sx, &sy);

        if (surface) {
            /* Check if it's a layer surface (window decorations) */
            struct wlr_layer_surface_v1 *layer_surface =
                wlr_layer_surface_v1_try_from_wlr_surface(surface);
            
            if (layer_surface) {
                /* Handle layer surface focus */
                wlr_seat_pointer_notify_enter(server->seat, surface, sx, sy);
            } else {
                /* Handle regular toplevel focus */
                struct tinywl_toplevel *toplevel = desktop_toplevel_at(server,
                    server->cursor->x, server->cursor->y, &surface, &sx, &sy);
                focus_toplevel(toplevel);
            }
        }
    }
}



static void server_cursor_axis(struct wl_listener *listener, void *data) {
	/* This event is forwarded by the cursor when a pointer emits an axis event,
	 * for example when you move the scroll wheel. */
	struct tinywl_server *server =
		wl_container_of(listener, server, cursor_axis);
	struct wlr_pointer_axis_event *event = data;
	/* Notify the client with pointer focus of the axis event. */
	wlr_seat_pointer_notify_axis(server->seat,
			event->time_msec, event->orientation, -event->delta,
			-event->delta_discrete, event->source, event->relative_direction);
}

static void server_cursor_frame(struct wl_listener *listener, void *data) {
	/* This event is forwarded by the cursor when a pointer emits an frame
	 * event. Frame events are sent after regular pointer events to group
	 * multiple events together. For instance, two axis events may happen at the
	 * same time, in which case a frame event won't be sent in between. */
	struct tinywl_server *server =
		wl_container_of(listener, server, cursor_frame);
	/* Notify the client with pointer focus of the frame event. */
	wlr_seat_pointer_notify_frame(server->seat);
}

static void output_frame(struct wl_listener *listener, void *data) {
	/* This function is called every time an output is ready to display a frame,
	 * generally at the output's refresh rate (e.g. 60Hz). */
	struct tinywl_output *output = wl_container_of(listener, output, frame);
	struct wlr_scene *scene = output->server->scene;

	struct wlr_scene_output *scene_output = wlr_scene_get_scene_output(
		scene, output->wlr_output);

	/* Render the scene if needed and commit the output */
	wlr_scene_output_commit(scene_output, NULL);

	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	wlr_scene_output_send_frame_done(scene_output, &now);
}

static void output_request_state(struct wl_listener *listener, void *data) {
	/* This function is called when the backend requests a new state for
	 * the output. For example, Wayland and X11 backends request a new mode
	 * when the output window is resized. */
	struct tinywl_output *output = wl_container_of(listener, output, request_state);
	const struct wlr_output_event_request_state *event = data;
	wlr_output_commit_state(output->wlr_output, event->state);
}

static void output_destroy(struct wl_listener *listener, void *data) {
	struct tinywl_output *output = wl_container_of(listener, output, destroy);

	wl_list_remove(&output->frame.link);
	wl_list_remove(&output->request_state.link);
	wl_list_remove(&output->destroy.link);
	wl_list_remove(&output->link);
	free(output);
}

static void server_new_output(struct wl_listener *listener, void *data) {
	/* This event is raised by the backend when a new output (aka a display or
	 * monitor) becomes available. */
	struct tinywl_server *server =
		wl_container_of(listener, server, new_output);
	struct wlr_output *wlr_output = data;

	/* Configures the output created by the backend to use our allocator
	 * and our renderer. Must be done once, before commiting the output */
	wlr_output_init_render(wlr_output, server->allocator, server->renderer);

	/* The output may be disabled, switch it on. */
	struct wlr_output_state state;
	wlr_output_state_init(&state);
	wlr_output_state_set_enabled(&state, true);

	/* Some backends don't have modes. DRM+KMS does, and we need to set a mode
	 * before we can use the output. The mode is a tuple of (width, height,
	 * refresh rate), and each monitor supports only a specific set of modes. We
	 * just pick the monitor's preferred mode, a more sophisticated compositor
	 * would let the user configure it. */
	struct wlr_output_mode *mode = wlr_output_preferred_mode(wlr_output);
	if (mode != NULL) {
		wlr_output_state_set_mode(&state, mode);
	}

	/* Atomically applies the new output state. */
	wlr_output_commit_state(wlr_output, &state);
	wlr_output_state_finish(&state);

	/* Allocates and configures our state for this output */
	struct tinywl_output *output = calloc(1, sizeof(*output));
	output->wlr_output = wlr_output;
	output->server = server;

	/* Sets up a listener for the frame event. */
	output->frame.notify = output_frame;
	wl_signal_add(&wlr_output->events.frame, &output->frame);

	/* Sets up a listener for the state request event. */
	output->request_state.notify = output_request_state;
	wl_signal_add(&wlr_output->events.request_state, &output->request_state);

	/* Sets up a listener for the destroy event. */
	output->destroy.notify = output_destroy;
	wl_signal_add(&wlr_output->events.destroy, &output->destroy);

	wl_list_insert(&server->outputs, &output->link);

	/* Adds this to the output layout. The add_auto function arranges outputs
	 * from left-to-right in the order they appear. A more sophisticated
	 * compositor would let the user configure the arrangement of outputs in the
	 * layout.
	 *
	 * The output layout utility automatically adds a wl_output global to the
	 * display, which Wayland clients can see to find out information about the
	 * output (such as DPI, scale factor, manufacturer, etc).
	 */
	struct wlr_output_layout_output *l_output = wlr_output_layout_add_auto(server->output_layout,
		wlr_output);

	
	// struct wlr_scene_output *scene_output = wlr_scene_output_create(server->scene, wlr_output);...changed this
	// wlr_scene_output_layout_add_output(server->scene_layout, l_output, scene_output);

	output->scene_output = wlr_scene_output_create(server->scene, wlr_output);
    wlr_scene_output_layout_add_output(server->scene_layout, l_output, output->scene_output); 
	// updated the output layout connection:

    //edited 1stapr
    uint32_t caps = WL_SEAT_CAPABILITY_POINTER | WL_SEAT_CAPABILITY_KEYBOARD;
    wlr_seat_set_capabilities(server->seat, caps);
}

static void xdg_toplevel_map(struct wl_listener *listener, void *data) {
    struct tinywl_toplevel *toplevel = wl_container_of(listener, toplevel, map);
    struct tinywl_server *server = toplevel->server;

    // Ensure this is an actual XDG toplevel before continuing
    if (!toplevel->xdg_toplevel) {
        wlr_log(WLR_ERROR, "xdg_toplevel_map: Skipping non-xdg_toplevel surface");
        return;
    }

    wlr_log(WLR_DEBUG, "Mapping XDG Toplevel: %p (title: %s)", toplevel, toplevel->xdg_toplevel->title);

    // Insert the toplevel into the list of managed windows
    wl_list_insert(&server->toplevels, &toplevel->link);

    // Create Foreign Toplevel Handle (Only if Foreign Toplevel Management is active)
    if (server->toplevel_manager && !toplevel->foreign_handle) {
        toplevel->foreign_handle = 
            wlr_foreign_toplevel_handle_v1_create(server->toplevel_manager);
        
        if (toplevel->foreign_handle) {
            // Set window properties
            wlr_foreign_toplevel_handle_v1_set_title(
                toplevel->foreign_handle,
                toplevel->xdg_toplevel->title ? toplevel->xdg_toplevel->title : "Untitled");
            
            wlr_foreign_toplevel_handle_v1_set_app_id(
                toplevel->foreign_handle,
                toplevel->xdg_toplevel->app_id ? toplevel->xdg_toplevel->app_id : "unknown");

            // Set up listeners
            toplevel->request_activate.notify = handle_activate;
            wl_signal_add(&toplevel->foreign_handle->events.request_activate,
                &toplevel->request_activate);
            
            toplevel->request_close.notify = handle_close;
            wl_signal_add(&toplevel->foreign_handle->events.request_close,
                &toplevel->request_close);
        }
    }

    // Assign window to output
    struct wlr_output *output = wlr_output_layout_output_at(
        server->output_layout, toplevel->scene_tree->node.x, toplevel->scene_tree->node.y
    );
    if (output) {
        wlr_foreign_toplevel_handle_v1_output_enter(toplevel->foreign_handle, output);
    } else {
        wlr_log(WLR_ERROR, "xdg_toplevel_map: Failed to assign output for %s", toplevel->xdg_toplevel->title);
    }

    // Window layout logic (preserved)
    struct wlr_box layout_box;
    wlr_output_layout_get_box(server->output_layout, NULL, &layout_box);

    int window_count = 0;
    struct tinywl_toplevel *win;
    wl_list_for_each(win, &server->toplevels, link) window_count++;

    struct wlr_surface *focused_surface = server->seat->keyboard_state.focused_surface;
    struct tinywl_toplevel *reference_window = NULL;

    if (focused_surface) {
        // Skip layer-shell surfaces (like Fuzzel)
        if (!wlr_layer_surface_v1_try_from_wlr_surface(focused_surface)) {
            reference_window = get_toplevel_for_surface(server, focused_surface);
        }
    }

    if (!reference_window) {
        struct tinywl_toplevel *win;
        wl_list_for_each_reverse(win, &server->toplevels, link) {
            if (win != toplevel) { 
                reference_window = win;
                break;
            }
        }
    }

    if (reference_window) {
        // Get both windows' sizes
        struct wlr_box *new_geo = &toplevel->xdg_toplevel->base->geometry;
        struct wlr_box *ref_geo = &reference_window->xdg_toplevel->base->geometry;
        
        int new_width = (new_geo->width > 0) ? new_geo->width : 800;
        int new_height = (new_geo->height > 0) ? new_geo->height : 600;
        int ref_width = (ref_geo->width > 0) ? ref_geo->width : 800;
        int ref_height = (ref_geo->height > 0) ? ref_geo->height : 600;

        // Only apply offset if sizes match
        if (new_width > ref_width - 30 && new_width < ref_width + 30 && new_height > ref_height - 30 && new_height < ref_height + 30) {
            int new_x = reference_window->scene_tree->node.x + 40;
            int new_y = reference_window->scene_tree->node.y + 40;

            // Ensure window stays within screen bounds
            new_x = MAX(layout_box.x, MIN(new_x, 
                layout_box.x + layout_box.width - new_width));
            new_y = MAX(layout_box.y, MIN(new_y, 
                layout_box.y + layout_box.height - new_height));

            wlr_scene_node_set_position(&toplevel->scene_tree->node, new_x, new_y);
            goto focus; // Skip normal placement
        }
    }

    // Default placement
    struct wlr_box *geo = &toplevel->xdg_toplevel->base->geometry;
    int safe_width = (geo->width > 0) ? geo->width : 800;
    int safe_height = (geo->height > 0) ? geo->height : 600;

    wlr_scene_node_set_position(&toplevel->scene_tree->node,
        layout_box.x + (layout_box.width - safe_width) / 2,
        layout_box.y + (layout_box.height - safe_height) / 2
    );

focus:

    // Finalize window creation
    wlr_scene_node_raise_to_top(&toplevel->scene_tree->node);
    focus_toplevel(toplevel);
}

static void xdg_toplevel_unmap(struct wl_listener *listener, void *data) {
    struct tinywl_toplevel *toplevel = wl_container_of(listener, toplevel, unmap);
    struct tinywl_server *server = toplevel->server;

    // Remove from list first to prevent re-tiling logic from seeing this window
    wl_list_remove(&toplevel->link);

    if (toplevel->foreign_handle) {
        // Remove listeners first to prevent dangling pointers
        wl_list_remove(&toplevel->request_activate.link);
        wl_list_remove(&toplevel->request_close.link);
        
        // Destroy the foreign toplevel handle
        wlr_foreign_toplevel_handle_v1_destroy(toplevel->foreign_handle);
        toplevel->foreign_handle = NULL;
		toplevel->foreign_handle = NULL; // Critical NULL assignment
    }

    if (toplevel->group) {
        // Remove from group before destroying
        remove_from_group(toplevel);
        wlr_log(WLR_DEBUG, "Removed window from group during unmap");
    }

    toplevel->proximity_timestamp = 0;

    struct wlr_box layout_box;
    wlr_output_layout_get_box(server->output_layout, NULL, &layout_box);

    int window_count = 0;
    struct tinywl_toplevel *win;
    wl_list_for_each(win, &server->toplevels, link) window_count++;


    struct tinywl_output *output;
    wl_list_for_each(output, &server->outputs, link) {
        wlr_scene_output_commit(output->scene_output, NULL);
    }

	//error-chance, z-added
	if (toplevel == server->grabbed_toplevel) {
        reset_cursor_mode(server);
    }


}


static void xdg_toplevel_commit(struct wl_listener *listener, void *data) {
	/* Called when a new surface state is committed. */
	struct tinywl_toplevel *toplevel = wl_container_of(listener, toplevel, commit);

	if (toplevel->xdg_toplevel->base->initial_commit) {
		/* When an xdg_surface performs an initial commit, the compositor must
		 * reply with a configure so the client can map the surface. tinywl
		 * configures the xdg_toplevel with 0,0 size to let the client pick the
		 * dimensions itself. */
		wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel, 0, 0);
	}
}


static void xdg_toplevel_destroy(struct wl_listener *listener, void *data) {
	/* Called when the xdg_toplevel is destroyed. */
	struct tinywl_toplevel *toplevel = wl_container_of(listener, toplevel, destroy);

	wl_list_remove(&toplevel->map.link);
	wl_list_remove(&toplevel->unmap.link);
	wl_list_remove(&toplevel->commit.link);
	wl_list_remove(&toplevel->destroy.link);
	wl_list_remove(&toplevel->request_move.link);
	wl_list_remove(&toplevel->request_resize.link);
	wl_list_remove(&toplevel->request_maximize.link);
	wl_list_remove(&toplevel->request_fullscreen.link);

	// newly added
	if (toplevel->group) {
        wl_list_remove(&toplevel->group_link);
        if (wl_list_empty(&toplevel->group->toplevels)) {
            free(toplevel->group);
        }
	}
	free(toplevel);
	}


static void begin_interactive(struct tinywl_toplevel *toplevel,
		enum tinywl_cursor_mode mode, uint32_t edges) {
	/* This function sets up an interactive move or resize operation, where the
	 * compositor stops propegating pointer events to clients and instead
	 * consumes them itself, to move or resize windows. */
	struct tinywl_server *server = toplevel->server;

	server->grabbed_toplevel = toplevel;
	server->cursor_mode = mode;

	if (mode == TINYWL_CURSOR_MOVE) {
		server->grab_x = server->cursor->x - toplevel->scene_tree->node.x;
		server->grab_y = server->cursor->y - toplevel->scene_tree->node.y;
	} else {
		struct wlr_box *geo_box = &toplevel->xdg_toplevel->base->geometry;

		double border_x = (toplevel->scene_tree->node.x + geo_box->x) +
			((edges & WLR_EDGE_RIGHT) ? geo_box->width : 0);
		double border_y = (toplevel->scene_tree->node.y + geo_box->y) +
			((edges & WLR_EDGE_BOTTOM) ? geo_box->height : 0);
		server->grab_x = server->cursor->x - border_x;
		server->grab_y = server->cursor->y - border_y;

		server->grab_geobox = *geo_box;
		server->grab_geobox.x += toplevel->scene_tree->node.x;
		server->grab_geobox.y += toplevel->scene_tree->node.y;

		server->resize_edges = edges;
	}
	if (toplevel->group) {
        // Store group geometry instead of individual window geometry
        server->grab_geobox.x = INT_MAX;
        server->grab_geobox.y = INT_MAX;
        int right = INT_MIN;
        int bottom = INT_MIN;
        
        struct tinywl_toplevel *member;
        wl_list_for_each(member, &toplevel->group->toplevels, group_link) {
            server->grab_geobox.x = MIN(server->grab_geobox.x, member->scene_tree->node.x);
            server->grab_geobox.y = MIN(server->grab_geobox.y, member->scene_tree->node.y);
            right = MAX(right, member->scene_tree->node.x + member->xdg_toplevel->current.width);
            bottom = MAX(bottom, member->scene_tree->node.y + member->xdg_toplevel->current.height);
        }
        server->grab_geobox.width = right - server->grab_geobox.x;
        server->grab_geobox.height = bottom - server->grab_geobox.y;
    }
}



static void xdg_toplevel_request_move(
		struct wl_listener *listener, void *data) {
	/* This event is raised when a client would like to begin an interactive
	 * move, typically because the user clicked on their client-side
	 * decorations. Note that a more sophisticated compositor should check the
	 * provided serial against a list of button press serials sent to this
	 * client, to prevent the client from requesting this whenever they want. */
	struct tinywl_toplevel *toplevel = wl_container_of(listener, toplevel, request_move);
	begin_interactive(toplevel, TINYWL_CURSOR_MOVE, 0);
}


// newly added // Add this helper function
static void normalize_resize_edges(struct tinywl_server *server) {
    if ((server->resize_edges & (WLR_EDGE_LEFT|WLR_EDGE_RIGHT)) == 
        (WLR_EDGE_LEFT|WLR_EDGE_RIGHT)) {
        server->resize_edges &= ~(WLR_EDGE_LEFT|WLR_EDGE_RIGHT);
    }
    if ((server->resize_edges & (WLR_EDGE_TOP|WLR_EDGE_BOTTOM)) == 
        (WLR_EDGE_TOP|WLR_EDGE_BOTTOM)) {
        server->resize_edges &= ~(WLR_EDGE_TOP|WLR_EDGE_BOTTOM);
    }
}

static void xdg_toplevel_request_resize(
		struct wl_listener *listener, void *data) {
	/* This event is raised when a client would like to begin an interactive
	 * resize, typically because the user clicked on their client-side
	 * decorations. Note that a more sophisticated compositor should check the
	 * provided serial against a list of button press serials sent to this
	 * client, to prevent the client from requesting this whenever they want. */
	struct wlr_xdg_toplevel_resize_event *event = data;
	struct tinywl_toplevel *toplevel = wl_container_of(listener, toplevel, request_resize);
	// begin_interactive(toplevel, TINYWL_CURSOR_RESIZE, event->edges);

	// newly added
	toplevel->server->resize_edges = event->edges;
    normalize_resize_edges(toplevel->server);
    begin_interactive(toplevel, TINYWL_CURSOR_RESIZE, toplevel->server->resize_edges);
}

static void xdg_toplevel_request_fullscreen(
		struct wl_listener *listener, void *data) {
	/* Just as with request_maximize, we must send a configure here. */
	struct tinywl_toplevel *toplevel =
		wl_container_of(listener, toplevel, request_fullscreen);
	if (toplevel->xdg_toplevel->base->initialized) {
		wlr_xdg_surface_schedule_configure(toplevel->xdg_toplevel->base);
	}
}

static void server_new_xdg_toplevel(struct wl_listener *listener, void *data) {
	/* This event is raised when a client creates a new toplevel (application window). */
	struct tinywl_server *server = wl_container_of(listener, server, new_xdg_toplevel);
	struct wlr_xdg_toplevel *xdg_toplevel = data;

	/* Allocate a tinywl_toplevel for this surface */
	struct tinywl_toplevel *toplevel = calloc(1, sizeof(*toplevel));
	toplevel->server = server;
	toplevel->xdg_toplevel = xdg_toplevel;
	toplevel->scene_tree =
		wlr_scene_xdg_surface_create(&toplevel->server->scene->tree, xdg_toplevel->base);
	toplevel->scene_tree->node.data = toplevel;
	xdg_toplevel->base->data = toplevel->scene_tree;


		// newly added
	wl_list_init(&toplevel->group_link);
	toplevel->group = NULL; // Explicit initialization

	/* Listen to the various events it can emit */
	toplevel->map.notify = xdg_toplevel_map;
	wl_signal_add(&xdg_toplevel->base->surface->events.map, &toplevel->map);
	toplevel->unmap.notify = xdg_toplevel_unmap;
	wl_signal_add(&xdg_toplevel->base->surface->events.unmap, &toplevel->unmap);
	toplevel->commit.notify = xdg_toplevel_commit;
	wl_signal_add(&xdg_toplevel->base->surface->events.commit, &toplevel->commit);

	toplevel->destroy.notify = xdg_toplevel_destroy;
	wl_signal_add(&xdg_toplevel->events.destroy, &toplevel->destroy);

	/* cotd */
	toplevel->request_move.notify = xdg_toplevel_request_move;
	wl_signal_add(&xdg_toplevel->events.request_move, &toplevel->request_move);
	toplevel->request_resize.notify = xdg_toplevel_request_resize;
	wl_signal_add(&xdg_toplevel->events.request_resize, &toplevel->request_resize);
	toplevel->request_maximize.notify = xdg_toplevel_request_maximize;
	wl_signal_add(&xdg_toplevel->events.request_maximize, &toplevel->request_maximize);
	toplevel->request_fullscreen.notify = xdg_toplevel_request_fullscreen;
	wl_signal_add(&xdg_toplevel->events.request_fullscreen, &toplevel->request_fullscreen);
}

static void xdg_popup_commit(struct wl_listener *listener, void *data) {
	/* Called when a new surface state is committed. */
	struct tinywl_popup *popup = wl_container_of(listener, popup, commit);

	if (popup->xdg_popup->base->initial_commit) {
		/* When an xdg_surface performs an initial commit, the compositor must
		 * reply with a configure so the client can map the surface.
		 * tinywl sends an empty configure. A more sophisticated compositor
		 * might change an xdg_popup's geometry to ensure it's not positioned
		 * off-screen, for example. */
		wlr_xdg_surface_schedule_configure(popup->xdg_popup->base);
	}
}

static void xdg_popup_destroy(struct wl_listener *listener, void *data) {
	/* Called when the xdg_popup is destroyed. */
	struct tinywl_popup *popup = wl_container_of(listener, popup, destroy);

	wl_list_remove(&popup->commit.link);
	wl_list_remove(&popup->destroy.link);

	free(popup);
}

static void server_new_xdg_popup(struct wl_listener *listener, void *data) {
	/* This event is raised when a client creates a new popup. */
	struct wlr_xdg_popup *xdg_popup = data;

	struct tinywl_popup *popup = calloc(1, sizeof(*popup));
	popup->xdg_popup = xdg_popup;

	/* We must add xdg popups to the scene graph so they get rendered. The
	 * wlroots scene graph provides a helper for this, but to use it we must
	 * provide the proper parent scene node of the xdg popup. To enable this,
	 * we always set the user data field of xdg_surfaces to the corresponding
	 * scene node. */
	struct wlr_xdg_surface *parent = wlr_xdg_surface_try_from_wlr_surface(xdg_popup->parent);
	assert(parent != NULL);
	struct wlr_scene_tree *parent_tree = parent->data;
	xdg_popup->base->data = wlr_scene_xdg_surface_create(parent_tree, xdg_popup->base);

	popup->commit.notify = xdg_popup_commit;
	wl_signal_add(&xdg_popup->base->surface->events.commit, &popup->commit);

	popup->destroy.notify = xdg_popup_destroy;
	wl_signal_add(&xdg_popup->events.destroy, &popup->destroy);
}


// z-added
/* === [MODIFIED] Layer-shell support for fuzzel === */

/* Define a helper structure to store layer-surface related listeners and scene node */
struct tinywl_layer_surface {
    struct wl_listener *destroy_listener;
    struct wl_listener *commit_listener;
    struct wlr_scene_layer_surface_v1 *scene_layer_surface;
};

// z-added
/* Modified handle_layer_surface_destroy:
   It now retrieves our tinywl_layer_surface structure, frees its listeners,
   destroys the scene node, and frees the structure. */
static void handle_layer_surface_destroy(struct wl_listener *listener, void *data) {
    struct wlr_layer_surface_v1 *layer_surface = data;
    struct tinywl_layer_surface *lsurf = layer_surface->data;
    if (!lsurf) {
        // free(lsurf->commit_listener);
        // free(lsurf->destroy_listener);
        // if (lsurf->scene_layer_surface) {
        //     wlr_scene_node_destroy((struct wlr_scene_node *)lsurf->scene_layer_surface);
        // }
        // free(lsurf);
        // layer_surface->data = NULL;
        return ;
    }

    wl_list_remove(&lsurf->destroy_listener->link);
    wl_list_remove(&lsurf->commit_listener->link);

    // Free allocated memory
    free(lsurf->destroy_listener);
    free(lsurf->commit_listener);
    free(lsurf);

    layer_surface->data = NULL;
}

static void handle_popup_destroy(struct wl_listener *listener, void *data) {
    struct tinywl_popup *popup = wl_container_of(listener, popup, destroy);
    wl_list_remove(&popup->link);
    free(popup);
}

/* Modified handle_layer_surface_commit:
   It uses our tinywl_layer_surface structure to configure the surface if needed. */
static void handle_layer_surface_commit(struct wl_listener *listener, void *data) {
    struct wlr_surface *surface = data;
    struct wlr_layer_surface_v1 *layer_surface = wlr_layer_surface_v1_try_from_wlr_surface(surface);
    if (!layer_surface) {
        return;
    }
    struct tinywl_layer_surface *lsurf = layer_surface->data;
    if (!lsurf) {
        return;
    }
    int buffer_width = surface->current.width;
    int buffer_height = surface->current.height;
    wlr_log(WLR_DEBUG, "Layer surface commit: w=%d h=%d configured=%s",
        buffer_width, buffer_height, layer_surface->configured ? "true" : "false");

    // Only configure if not yet configured and we have a valid non-zero size
    if (!layer_surface->configured && buffer_width > 0 && buffer_height > 0) {
        wlr_layer_surface_v1_configure(layer_surface, buffer_width, buffer_height);
        wlr_log(WLR_DEBUG, "Configured layer surface to %dx%d", buffer_width, buffer_height);
    }
}

static void handle_new_layer_surface(struct wl_listener *listener, void *data) {
    struct tinywl_server *server = wl_container_of(listener, server, new_layer_surface);
    struct wlr_layer_surface_v1 *layer_surface = data;

    struct tinywl_layer_surface *lsurf = calloc(1, sizeof(*lsurf));
    if (!lsurf) {
        wlr_log(WLR_ERROR, "Failed to allocate memory for tinywl_layer_surface");
        return;
    }

    lsurf->scene_layer_surface =
        wlr_scene_layer_surface_v1_create(&server->scene->tree, layer_surface);
    if (!lsurf->scene_layer_surface) {
        wlr_log(WLR_ERROR, "Failed to create scene node for layer surface");
        free(lsurf);
        return;
    }

    //1st april

    //edited
    struct wlr_output *output = wlr_output_layout_output_at(
        server->output_layout, server->cursor->x, server->cursor->y);
    if (!output) {
        // Fall back to first output if cursor isn't on any output
        struct tinywl_output *tinywl_output;
        wl_list_for_each(tinywl_output, &server->outputs, link) {
            output = tinywl_output->wlr_output;
            break;
        }
    }

    if (output) {
        struct wlr_box box;
        wlr_output_layout_get_box(server->output_layout, output, &box);
        
        // For interactive layers (like panels/menus), use client's desired size
        if (layer_surface->pending.layer >= ZWLR_LAYER_SHELL_V1_LAYER_TOP) {
            wlr_layer_surface_v1_configure(layer_surface,
                layer_surface->pending.desired_width,
                layer_surface->pending.desired_height);
        } else {
            // For background/bottom layers, use full output size
            wlr_layer_surface_v1_configure(layer_surface, box.width, box.height);
        }
    }

    lsurf->destroy_listener = calloc(1, sizeof(struct wl_listener));
    if (!lsurf->destroy_listener) {
        wlr_log(WLR_ERROR, "Failed to allocate memory for destroy listener");
        free(lsurf);
        return;
    }


    lsurf->destroy_listener->notify = handle_layer_surface_destroy;
    wl_signal_add(&layer_surface->events.destroy, lsurf->destroy_listener);

    lsurf->commit_listener = calloc(1, sizeof(struct wl_listener));
    if (!lsurf->commit_listener) {
        wlr_log(WLR_ERROR, "Failed to allocate memory for commit listener");
        free(lsurf->destroy_listener);
        free(lsurf);
        return;
    }
    lsurf->commit_listener->notify = handle_layer_surface_commit;
    wl_signal_add(&layer_surface->surface->events.commit, lsurf->commit_listener);

    layer_surface->data = lsurf;

    if (layer_surface->pending.layer >= ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY) {
        struct wlr_keyboard *kbd = wlr_seat_get_keyboard(server->seat);
        if (kbd) {
            wlr_seat_keyboard_notify_enter(
                server->seat,
                layer_surface->surface,
                kbd->keycodes,
                kbd->num_keycodes,
                &kbd->modifiers
            );
        }
    }
    wlr_log(WLR_DEBUG, "New layer surface created (layer: %d)", layer_surface->pending.layer);

}

static void handle_activate(struct wl_listener *listener, void *data) {
    struct tinywl_toplevel *toplevel = wl_container_of(listener, toplevel, request_activate);
    struct wlr_foreign_toplevel_handle_v1 *foreign_handle = data;

    if (!toplevel || !foreign_handle || toplevel->foreign_handle != foreign_handle) {
        wlr_log(WLR_ERROR, "Invalid activation request");
        return;
    }

    /* Focus the window */
    focus_toplevel(toplevel);

    /* Ensure it's raised and visible */
    wlr_scene_node_raise_to_top(&toplevel->scene_tree->node);

    /* Move pointer to window if needed */
    struct tinywl_server *server = toplevel->server;
    wlr_cursor_warp(server->cursor, NULL,
        toplevel->scene_tree->node.x + toplevel->xdg_toplevel->current.width/2,
        toplevel->scene_tree->node.y + 10); // Position near top of window
}





static void handle_close(struct wl_listener *listener, void *data) {
    struct tinywl_toplevel *toplevel = wl_container_of(listener, toplevel, request_close);

    if (!toplevel) {
        wlr_log(WLR_ERROR, "Close request failed: No toplevel found");
        return;
    }

    wlr_xdg_toplevel_send_close(toplevel->xdg_toplevel);
}
static void handle_new_popup(struct wl_listener *listener, void *data) {
    struct tinywl_server *server = wl_container_of(listener, server, new_xdg_popup);
    struct wlr_xdg_popup *xdg_popup = data;
    
    // Create popup container
    struct tinywl_popup *popup = calloc(1, sizeof(*popup));
    popup->xdg_popup = xdg_popup;
    
    // Add to popup list
    wl_list_insert(&server->popups, &popup->link);

    // Get parent surface
    struct wlr_surface *parent = xdg_popup->parent;
    struct wlr_scene_tree *parent_tree = parent->data;
    
    // Create scene node
    xdg_popup->base->data = wlr_scene_xdg_surface_create(parent_tree, xdg_popup->base);
    
    // Configure popup
    struct wlr_output *output = wlr_output_layout_output_at(server->output_layout,
		server->cursor->x, server->cursor->y);
	if (output) {
		struct wlr_box box;
		wlr_output_layout_get_box(server->output_layout, output, &box);
		wlr_xdg_popup_unconstrain_from_box(xdg_popup, &box);
	}

    // Set up listeners
    popup->destroy.notify = handle_popup_destroy;
    wl_signal_add(&xdg_popup->base->events.destroy, &popup->destroy);
}



void setup_toplevel_manager(struct tinywl_server *server) {
    server->toplevel_manager =
        wlr_foreign_toplevel_manager_v1_create(server->wl_display);

    if (!server->toplevel_manager) {
        wlr_log(WLR_ERROR, "Failed to create foreign toplevel manager");
        exit(EXIT_FAILURE);
    }
}



static struct tinywl_toplevel *get_toplevel_for_surface(struct tinywl_server *server, struct wlr_surface *surface) {
    struct tinywl_toplevel *toplevel;
    wl_list_for_each(toplevel, &server->toplevels, link) {
        if (toplevel->xdg_toplevel->base->surface == surface) {
            return toplevel;
        }
    }
    return NULL;
}

void set_keybinding_handler(struct tinywl_server *server, keybinding_handler_t handler);


int main(int argc, char *argv[]) {
	wlr_log_init(WLR_DEBUG, NULL);
	char *startup_cmds[argc]; // Array to hold multiple startup commands
    int startup_cmd_count = 0;

    int c;
    while ((c = getopt(argc, argv, "s:h")) != -1) {
        switch (c) {
        case 's':
            if (startup_cmd_count < argc - 1) {
                startup_cmds[startup_cmd_count++] = optarg;
            }
            break;
        default:
            printf("Usage: %s [-s startup command]\n", argv[0]);
            return 0;
        }
    }
    if (optind < argc) {
        printf("Usage: %s [-s startup command]\n", argv[0]);
        return 0;
    }
	

	struct tinywl_server server = {0};
	/* The Wayland display is managed by libwayland. It handles accepting
	 * clients from the Unix socket, manging Wayland globals, and so on. */
	server.wl_display = wl_display_create();

	setup_toplevel_manager(&server);

	/* The backend is a wlroots feature which abstracts the underlying input and
	 * output hardware. The autocreate option will choose the most suitable
	 * backend based on the current environment, such as opening an X11 window
	 * if an X11 server is running. */
	server.backend = wlr_backend_autocreate(wl_display_get_event_loop(server.wl_display), NULL);
	if (server.backend == NULL) {
		wlr_log(WLR_ERROR, "failed to create wlr_backend");
		return 1;
	}

	/* Autocreates a renderer, either Pixman, GLES2 or Vulkan for us. The user
	 * can also specify a renderer using the WLR_RENDERER env var.
	 * The renderer is responsible for defining the various pixel formats it
	 * supports for shared memory, this configures that for clients. */
	server.renderer = wlr_renderer_autocreate(server.backend);
	if (server.renderer == NULL) {
		wlr_log(WLR_ERROR, "failed to create wlr_renderer");
		return 1;
	}

	wlr_renderer_init_wl_display(server.renderer, server.wl_display);



	/* Autocreates an allocator for us.
	 * The allocator is the bridge between the renderer and the backend. It
	 * handles the buffer creation, allowing wlroots to render onto the
	 * screen */
	server.allocator = wlr_allocator_autocreate(server.backend,
		server.renderer);
	if (server.allocator == NULL) {
		wlr_log(WLR_ERROR, "failed to create wlr_allocator");
		return 1;
	}

	/* This creates some hands-off wlroots interfaces. The compositor is
	 * necessary for clients to allocate surfaces, the subcompositor allows to
	 * assign the role of subsurfaces to surfaces and the data device manager
	 * handles the clipboard. Each of these wlroots interfaces has room for you
	 * to dig your fingers in and play with their behavior if you want. Note that
	 * the clients cannot set the selection directly without compositor approval,
	 * see the handling of the request_set_selection event below.*/
	wlr_compositor_create(server.wl_display, 5, server.renderer);
	wlr_subcompositor_create(server.wl_display);
	wlr_data_device_manager_create(server.wl_display); //for clip board



	/* Creates an output layout, which a wlroots utility for working with an
	 * arrangement of screens in a physical layout. */
	server.output_layout = wlr_output_layout_create(server.wl_display);
	server.xdg_output_manager = wlr_xdg_output_manager_v1_create(server.wl_display, server.output_layout);
    if (!server.xdg_output_manager) {
        wlr_log(WLR_ERROR, "Failed to create xdg_output_manager_v1");
    } else {
        wlr_log(WLR_DEBUG, "xdg_output_manager_v1 created: %p", server.xdg_output_manager);
    }

	/* Configure a listener to be notified when new outputs are available on the
	 * backend. */
	wl_list_init(&server.outputs);
	server.new_output.notify = server_new_output;
	wl_signal_add(&server.backend->events.new_output, &server.new_output);

	/* Create a scene graph. This is a wlroots abstraction that handles all
	 * rendering and damage tracking. All the compositor author needs to do
	 * is add things that should be rendered to the scene graph at the proper
	 * positions and then call wlr_scene_output_commit() to render a frame if
	 * necessary.
	 */
	server.scene = wlr_scene_create();
	server.scene_layout = wlr_scene_attach_output_layout(server.scene, server.output_layout);

	/* Set up xdg-shell version 3. The xdg-shell is a Wayland protocol which is
	 * used for application windows. For more detail on shells, refer to
	 * https://drewdevault.com/2018/07/29/Wayland-shells.html.
	 */
	wl_list_init(&server.toplevels);
	server.xdg_shell = wlr_xdg_shell_create(server.wl_display, 3);
	server.new_xdg_popup.notify = handle_new_popup;
	wl_signal_add(&server.xdg_shell->events.new_popup, &server.new_xdg_popup);
	server.new_xdg_toplevel.notify = server_new_xdg_toplevel;
	wl_signal_add(&server.xdg_shell->events.new_toplevel, &server.new_xdg_toplevel);
	server.new_xdg_popup.notify = server_new_xdg_popup;
	wl_signal_add(&server.xdg_shell->events.new_popup, &server.new_xdg_popup);

	// 7th feb
	server.layer_shell = wlr_layer_shell_v1_create(server.wl_display, 4);
	server.new_layer_surface.notify = handle_new_layer_surface;
	wl_signal_add(&server.layer_shell->events.new_surface, &server.new_layer_surface);

	/*
	 * Creates a cursor, which is a wlroots utility for tracking the cursor
	 * image shown on screen.
	 */
	server.cursor = wlr_cursor_create();
	wlr_cursor_attach_output_layout(server.cursor, server.output_layout);

	/* Creates an xcursor manager, another wlroots utility which loads up
	 * Xcursor themes to source cursor images from and makes sure that cursor
	 * images are available at all scale factors on the screen (necessary for
	 * HiDPI support). */
	server.cursor_mgr = wlr_xcursor_manager_create(NULL, 24);

	/*
	 * wlr_cursor *only* displays an image on screen. It does not move around
	 * when the pointer moves. However, we can attach input devices to it, and
	 * it will generate aggregate events for all of them. In these events, we
	 * can choose how we want to process them, forwarding them to clients and
	 * moving the cursor around. More detail on this process is described in
	 * https://drewdevault.com/2018/07/17/Input-handling-in-wlroots.html.
	 *
	 * And more comments are sprinkled throughout the notify functions above.
	 */
	server.cursor_mode = TINYWL_CURSOR_PASSTHROUGH;
	server.cursor_motion.notify = server_cursor_motion;
	wl_signal_add(&server.cursor->events.motion, &server.cursor_motion);
	server.cursor_motion_absolute.notify = server_cursor_motion_absolute;
	wl_signal_add(&server.cursor->events.motion_absolute,
			&server.cursor_motion_absolute);
	server.cursor_button.notify = server_cursor_button;
	wl_signal_add(&server.cursor->events.button, &server.cursor_button);
	server.cursor_axis.notify = server_cursor_axis;
	wl_signal_add(&server.cursor->events.axis, &server.cursor_axis);
	server.cursor_frame.notify = server_cursor_frame;
	wl_signal_add(&server.cursor->events.frame, &server.cursor_frame);

	/*
	 * Configures a seat, which is a single "seat" at which a user sits and
	 * operates the computer. This conceptually includes up to one keyboard,
	 * pointer, touch, and drawing tablet device. We also rig up a listener to
	 * let us know when new input devices are available on the backend.
	 */
	wl_list_init(&server.keyboards);
	server.new_input.notify = server_new_input;
	wl_signal_add(&server.backend->events.new_input, &server.new_input);
	server.seat = wlr_seat_create(server.wl_display, "seat0");
	server.request_cursor.notify = seat_request_cursor;
	wl_signal_add(&server.seat->events.request_set_cursor,
			&server.request_cursor);
	server.request_set_selection.notify = seat_request_set_selection;
	wl_signal_add(&server.seat->events.request_set_selection,
			&server.request_set_selection);

	/* Add a Unix socket to the Wayland display. */
	const char *socket = wl_display_add_socket_auto(server.wl_display);
	if (!socket) {
		wlr_backend_destroy(server.backend);
		return 1;
	}

	// /* Start the backend. This will enumerate outputs and inputs, become the DRM
	//  * master, etc */
	
	/* Start the backend */


		/* Start the backend */
	if (!wlr_backend_start(server.backend)) {
		wlr_log(WLR_ERROR, "Failed to start backend");
		wlr_backend_destroy(server.backend);
		wl_display_destroy(server.wl_display);
		return 1;
	}

	{
		struct wlr_drm_format_set formats = {0}; // Initialize DRM format set

		// Add a valid DRM format and modifier
		if (!wlr_drm_format_set_add(&formats, DRM_FORMAT_XRGB8888, DRM_FORMAT_MOD_INVALID)) {
			wlr_log(WLR_ERROR, "Failed to add DRM_FORMAT_XRGB8888 to DMABUF feedback");
			return 1;
		} else {
			wlr_log(WLR_DEBUG, "Successfully added DRM_FORMAT_XRGB8888 with modifier DRM_FORMAT_MOD_INVALID");
		}

		// Assign formats to default tranche
		default_tranche.formats = formats;

		// Validate tranche setup
		if (default_tranche.formats.len == 0) {
			wlr_log(WLR_ERROR, "Default tranche has no valid formats!");
			return 1;
		}

		// Log feedback details before creating DMABUF object
		wlr_log(WLR_DEBUG, "DMABUF feedback setup:");
		wlr_log(WLR_DEBUG, "Main device: %lx", (unsigned long)default_feedback.main_device);
		wlr_log(WLR_DEBUG, "Tranches size: %zu", default_feedback.tranches.size);

		server.linux_dmabuf = wlr_linux_dmabuf_v1_create_with_renderer(server.wl_display, 3, server.renderer);
		if (!server.linux_dmabuf) {
			wlr_log(WLR_ERROR, "Failed to create Linux DMABUF object");
			return 1;
		}
	}

	wlr_log(WLR_DEBUG, "Initialized DMA-BUF support");


	/* Set the WAYLAND_DISPLAY environment variable to our socket and run the
	 * startup command if requested. */
	setenv("WAYLAND_DISPLAY", socket, true);


        // --- START SWAYBG AUTOLAUNCH ---
    if (fork() == 0) {
        execl("/usr/bin/swaybg", "swaybg",
            "-i", "/home/tejas/Desktop/tinywl/wlroots/tinywl/img2.jpg",
            "--mode", "stretch", NULL);
        _exit(127);  // Only reached if execl fails
        
    }

    spawn_sfwbar();  


    if (fork() == 0) {
        setsid();
        execlp("mako", "mako", NULL);
        _exit(127);
    }

	for (int i = 0; i < startup_cmd_count; i++) {
        if (fork() == 0) {
            execl("/bin/sh", "/bin/sh", "-c", startup_cmds[i], (void *)NULL);
            perror("execl failed"); // Should not return
            exit(EXIT_FAILURE);     // Exit if exec fails
        }
    }

	/* Run the Wayland event loop. This does not return until you exit the
	 * compositor. Starting the backend rigged up all of the necessary event
	 * loop configuration to listen to libinput events, DRM events, generate
	 * frame events at the refresh rate, and so on. */
	// wlr_log(WLR_INFO, "Running Wayland compositor on WAYLAND_DISPLAY=%s",
	// 		socket);
	// wl_display_run(server.wl_display);

	wlr_log(WLR_INFO, "Running Wayland compositor on WAYLAND_DISPLAY=%s", socket);
    wl_display_run(server.wl_display);

	/* Once wl_display_run returns, we destroy all clients then shut down the
	 * server. */
	wl_display_destroy_clients(server.wl_display);
	wlr_scene_node_destroy(&server.scene->tree.node);
	wlr_xcursor_manager_destroy(server.cursor_mgr);
	wlr_cursor_destroy(server.cursor);
	wlr_allocator_destroy(server.allocator);
	wlr_renderer_destroy(server.renderer);
	wlr_backend_destroy(server.backend);

    if (snap_timer) {
        wl_event_source_remove(snap_timer);
        snap_timer = NULL;
    }
	wl_display_destroy(server.wl_display);
	return 0;
}



#include <stdlib.h>
#include <string.h>

#include <wlr/util/log.h>

#include "qubes_backend.h"
#include "qubes_xwayland.h"
#include "main.h"

static bool xwayland_get_box(struct wlr_xwayland_surface *surface, struct wlr_box *box)
{
	if (surface->width <= 0 ||
		 surface->height <= 0 ||
		 surface->width > MAX_WINDOW_WIDTH ||
		 surface->height > MAX_WINDOW_HEIGHT)
		return false; /* cannot handle this */
	*box = (struct wlr_box) {
		.x = surface->x,
		.y = surface->y,
		.width = surface->width,
		.height = surface->height,
	};
	return true;
}

static void xwayland_surface_destroy(struct wl_listener *listener, void *data __attribute__((unused)))
{
	struct qubes_xwayland_view *view = wl_container_of(listener, view, destroy);

	wlr_log(WLR_ERROR, "freeing view at %p", view);

	assert(QUBES_XWAYLAND_MAGIC == view->output.magic);

	wl_list_remove(&view->destroy.link);
	wl_list_remove(&view->map.link);
	wl_list_remove(&view->unmap.link);
	wl_list_remove(&view->request_configure.link);
	wl_list_remove(&view->request_move.link);
	wl_list_remove(&view->request_resize.link);
	wl_list_remove(&view->request_minimize.link);
	wl_list_remove(&view->request_maximize.link);
	wl_list_remove(&view->request_fullscreen.link);
	wl_list_remove(&view->set_title.link);
	wl_list_remove(&view->set_class.link);
	wl_list_remove(&view->set_parent.link);
	wl_list_remove(&view->set_hints.link);
	wl_list_remove(&view->set_override_redirect.link);
	qubes_output_deinit(&view->output);
	memset(view, 0xFF, sizeof *view);
	free(view);
}
static void xwayland_surface_map(struct wl_listener *listener, void *data) {
	/* Called when the surface is mapped, or ready to display on-screen. */
	/* QUBES HOOK: MSG_MAP: map the corresponding window */
	struct qubes_xwayland_view *view = wl_container_of(listener, view, map);
	wlr_log(WLR_ERROR, "mapping surface at %p", view);
	struct wlr_xwayland_surface *surface = view->xwayland_surface;
	assert(surface == data);
	assert(surface);
	assert(surface->surface);
	struct qubes_output *output = &view->output;
	struct wlr_box box;

	assert(QUBES_XWAYLAND_MAGIC == output->magic);
	if (!xwayland_get_box(surface, &box))
		return;
	if (!qubes_output_mapped(output)) {
		qubes_output_ensure_created(output, box);
		output->flags |= QUBES_OUTPUT_MAPPED;
	}

	qubes_output_set_surface(output, surface->surface);
	qubes_output_map(output, 0, surface->override_redirect);
}

static void xwayland_surface_unmap(struct wl_listener *listener, void *data)
{
	struct qubes_xwayland_view *view = wl_container_of(listener, view, unmap);

	wlr_log(WLR_ERROR, "unmapping surface at %p", view);
	assert(QUBES_XWAYLAND_MAGIC == view->output.magic);
	qubes_output_unmap(&view->output);
}

static void xwayland_surface_request_configure(struct wl_listener *listener, void *data)
{
	struct qubes_xwayland_view *view = wl_container_of(listener, view, request_configure);
	struct qubes_output *output = &view->output;
	struct wlr_xwayland_surface_configure_event *event = data;

	wlr_log(WLR_ERROR, "configuring surface at %p", view);
	assert(QUBES_XWAYLAND_MAGIC == view->output.magic);
	if (event->width <= 0 ||
		 event->height <= 0 ||
		 event->width > MAX_WINDOW_WIDTH ||
		 event->height > MAX_WINDOW_HEIGHT) {
		wlr_log(WLR_ERROR, "Bad message from client: width %" PRIu16 " height %" PRIu16, event->width, event->height);
		return; /* cannot handle this */
	}
	qubes_output_configure(output, (struct wlr_box) {
		.x = event->x,
		.y = event->y,
		.width = event->width,
		.height = event->height,
	});
}

static void xwayland_surface_request_move(struct wl_listener *listener, void *data) {
	struct qubes_xwayland_view *view = wl_container_of(listener, view, request_move);
	struct wlr_xwayland_move_event *event = data;
	(void)event, (void)view;
	assert(view->destroy.link.next);
	wlr_log(WLR_DEBUG, "Move request for XWayland window %" PRIu32 " not yet implemented",
	        view->output.window_id);
}

static void xwayland_surface_request_resize(struct wl_listener *listener, void *data) {
	struct qubes_xwayland_view *view = wl_container_of(listener, view, request_resize);
	struct wlr_xwayland_resize_event *event = data;
	(void)event, (void)view;
	assert(view->destroy.link.next);
	wlr_log(WLR_DEBUG, "Resize request for XWayland window %" PRIu32 " not yet implemented",
	        view->output.window_id);
}

static void xwayland_surface_request_minimize(struct wl_listener *listener, void *data) {
	struct qubes_xwayland_view *view = wl_container_of(listener, view, request_minimize);
	struct wlr_xwayland_minimize_event *event = data;
	(void)event, (void)view;
	assert(view->destroy.link.next);
	wlr_log(WLR_DEBUG, "Minimize request for XWayland window %" PRIu32 " not yet implemented",
	        view->output.window_id);
}
static void xwayland_surface_request_maximize(struct wl_listener *listener, void *data) {
	struct qubes_xwayland_view *view = wl_container_of(listener, view, request_maximize);
	struct wlr_xwayland_surface *surface = data;
	(void)view, (void)surface;
	assert(view->destroy.link.next);
	wlr_log(WLR_DEBUG, "Maximize request for XWayland window %" PRIu32 " not yet implemented",
	        view->output.window_id);
}
static void xwayland_surface_request_fullscreen(struct wl_listener *listener, void *data) {
	struct qubes_xwayland_view *view = wl_container_of(listener, view, request_fullscreen);
	struct wlr_xwayland_surface *surface = data;
	(void)view, (void)surface;
	assert(view->destroy.link.next);
	wlr_log(WLR_DEBUG, "Fullscreen request for XWayland window %" PRIu32 " not yet implemented",
	        view->output.window_id);
}
static void xwayland_surface_set_title(struct wl_listener *listener, void *data) {
	struct qubes_xwayland_view *view = wl_container_of(listener, view, set_title);
	struct wlr_xwayland_surface *surface = data;
	(void)view, (void)surface;
	assert(view->destroy.link.next);
	wlr_log(WLR_DEBUG, "Set-title request for XWayland window %" PRIu32 " not yet implemented",
	        view->output.window_id);
}
static void xwayland_surface_set_class(struct wl_listener *listener, void *data) {
	struct qubes_xwayland_view *view = wl_container_of(listener, view, set_class);
	struct wlr_xwayland_surface *surface = data;
	(void)view, (void)surface;
	assert(view->destroy.link.next);
	wlr_log(WLR_DEBUG, "Set-class request for XWayland window %" PRIu32 " not yet implemented",
	        view->output.window_id);
}
static void xwayland_surface_set_parent(struct wl_listener *listener, void *data) {
	struct qubes_xwayland_view *view = wl_container_of(listener, view, set_parent);
	struct wlr_xwayland_surface *surface = data;
	(void)view, (void)surface;
	assert(view->destroy.link.next);
	wlr_log(WLR_DEBUG, "Set-parent request for XWayland window %" PRIu32 " not yet implemented",
	        view->output.window_id);
}
static void xwayland_surface_set_hints(struct wl_listener *listener, void *data) {
	struct qubes_xwayland_view *view = wl_container_of(listener, view, set_hints);
	struct wlr_xwayland_surface *surface = data;
	(void)view, (void)surface;
	assert(view->destroy.link.next);
	wlr_log(WLR_DEBUG, "Set-hints request for XWayland window %" PRIu32 " not yet implemented",
	        view->output.window_id);
}
static void xwayland_surface_set_override_redirect(struct wl_listener *listener, void *data) {
	struct qubes_xwayland_view *view = wl_container_of(listener, view, set_override_redirect);
	struct wlr_xwayland_surface *surface = data;
	(void)view, (void)surface;
	assert(view->destroy.link.next);
	wlr_log(WLR_DEBUG, "Set-override-redirect request for XWayland window %" PRIu32 " not yet implemented",
	        view->output.window_id);
}

void qubes_xwayland_new_xwayland_surface(struct wl_listener *listener, void *data)
{
	struct tinywl_server *server = wl_container_of(listener, server, new_xwayland_surface);
	struct wlr_xwayland_surface *surface = data;
	assert(surface);

	assert(QUBES_SERVER_MAGIC == server->magic);

	struct qubes_xwayland_view *view = calloc(1, sizeof(*view));
	if (!view)
		return;

	struct qubes_output *output = &view->output;

	if (!qubes_output_init(output, server,
	                       surface->override_redirect, surface->surface,
	                       QUBES_XWAYLAND_MAGIC))
		goto cleanup;

	view->xwayland_surface = surface;

	view->destroy.notify = xwayland_surface_destroy;
	wl_signal_add(&surface->events.destroy, &view->destroy);
	view->map.notify = xwayland_surface_map;
	wl_signal_add(&surface->events.map, &view->map);
	view->unmap.notify = xwayland_surface_unmap;
	wl_signal_add(&surface->events.unmap, &view->unmap);
	view->request_configure.notify = xwayland_surface_request_configure;
	wl_signal_add(&surface->events.request_configure, &view->request_configure);
	view->request_move.notify = xwayland_surface_request_move;
	wl_signal_add(&surface->events.request_move, &view->request_move);
	view->request_resize.notify = xwayland_surface_request_resize;
	wl_signal_add(&surface->events.request_resize, &view->request_resize);
	view->request_minimize.notify = xwayland_surface_request_minimize;
	wl_signal_add(&surface->events.request_minimize, &view->request_minimize);
	view->request_maximize.notify = xwayland_surface_request_maximize;
	wl_signal_add(&surface->events.request_maximize, &view->request_maximize);
	view->request_fullscreen.notify = xwayland_surface_request_fullscreen;
	wl_signal_add(&surface->events.request_fullscreen, &view->request_fullscreen);
	view->set_title.notify = xwayland_surface_set_title;
	wl_signal_add(&surface->events.set_title, &view->set_title);
	view->set_class.notify = xwayland_surface_set_class;
	wl_signal_add(&surface->events.set_class, &view->set_class);
	view->set_parent.notify = xwayland_surface_set_parent;
	wl_signal_add(&surface->events.set_parent, &view->set_parent);
	view->set_hints.notify = xwayland_surface_set_hints;
	wl_signal_add(&surface->events.set_hints, &view->set_hints);
	view->set_override_redirect.notify = xwayland_surface_set_override_redirect;
	wl_signal_add(&surface->events.set_override_redirect, &view->set_override_redirect);
	wlr_log(WLR_ERROR, "created surface at %p", view);
	return;

cleanup:
	if (view) {
		qubes_output_deinit(&view->output);
		free(view);
	}
}
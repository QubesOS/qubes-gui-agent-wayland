// wlr_output implementation and redraw code

#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include <wayland-server-core.h>

#include <wlr/interfaces/wlr_keyboard.h>
#include <wlr/render/drm_format_set.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>

#include <drm/drm_fourcc.h>
#include "qubes_output.h"
#include "qubes_allocator.h"
#include "qubes_backend.h"
#include "main.h"

/* Qubes OS doesn’t support gamma LUTs */
static size_t qubes_get_gamma_size(
		struct wlr_output *output __attribute__((unused))) {
	return 0;
}

static void qubes_unlink_buffer(struct qubes_output *buffer) {
	if (buffer->buffer)
		wl_list_remove(&buffer->buffer_destroy.link);
	buffer->buffer = NULL;
}

static void qubes_unlink_buffer_listener(struct wl_listener *listener,
                                         void *data __attribute__((unused))) {
	struct qubes_output *output = wl_container_of(listener, output, buffer_destroy);
	qubes_unlink_buffer(output);
}

static const struct wlr_output_impl qubes_wlr_output_impl;

static void qubes_output_deinit_raw(struct wlr_output *raw_output) {
	assert(raw_output->impl == &qubes_wlr_output_impl);
	struct qubes_output *output = wl_container_of(raw_output, output, output);
	wl_list_remove(&output->frame.link);
	qubes_unlink_buffer(output);
	wlr_buffer_unlock(output->buffer);
}

static bool qubes_output_test(struct wlr_output *raw_output) {
	assert(raw_output->impl == &qubes_wlr_output_impl);
	struct qubes_output *output = wl_container_of(raw_output, output, output);
	if ((raw_output->pending.committed & WLR_OUTPUT_STATE_BUFFER) &&
	    (raw_output->pending.buffer != NULL) &&
	    (raw_output->pending.buffer->impl != qubes_buffer_impl_addr))
		return false;
	return true;
}

static void qubes_output_damage(struct qubes_output *output, struct wlr_box box) {
	wlr_log(WLR_DEBUG, "X is %d Y is %d Width is %" PRIu32 " height is %" PRIu32, (int)box.x, (int)box.y, (uint32_t)box.width, (uint32_t)box.height);
	int n_rects = 0;
	if (!(output->output.pending.committed & WLR_OUTPUT_STATE_DAMAGE))
		return;
	pixman_box32_t *rects = pixman_region32_rectangles(&output->output.pending.damage, &n_rects);
	if (n_rects <= 0 || !rects) {
		wlr_log(WLR_DEBUG, "No damage!");
		return;
	}
	wlr_log(WLR_DEBUG, "Sending MSG_SHMIMAGE (0x%x) to window %" PRIu32, MSG_SHMIMAGE, output->window_id);
	for (int i = 0; i < n_rects; ++i) {
		int32_t width, height;
		// this is the correct approach ― the alternative leads to glitches
		static const bool use_delta = false;
		int32_t x1 = use_delta ? QUBES_MAX(rects[i].x1, box.x) : rects[i].x1;
		int32_t y1 = use_delta ? QUBES_MAX(rects[i].y1, box.y) : rects[i].y1;
		if (__builtin_sub_overflow(rects[i].x2, x1, &width) ||
		    __builtin_sub_overflow(rects[i].y2, y1, &height)) {
			wlr_log(WLR_ERROR, "Overflow in damage calc");
			return;
		}
		if (width <= 0 || height <= 0) {
			wlr_log(WLR_ERROR, "Negative width or height - skipping");
			continue;
		}
		int32_t const x = use_delta ? x1 - box.x : x1, y = use_delta ? y1 - box.y : y1;
		wlr_log(WLR_DEBUG, "Submitting damage to GUI daemon: x %" PRIi32 " y %" PRIi32 " width %" PRIu32 " height %" PRIu32, x, y, width, height);
		struct {
			struct msg_hdr header;
			struct msg_shmimage shmimage;
		} new_msg = {
			.header = {
				.type = MSG_SHMIMAGE,
				.window = output->window_id,
				.untrusted_len = sizeof(struct msg_shmimage),
			},
			.shmimage = { .x = x1, .y = y1, .width = width, .height = height },
		};
		QUBES_STATIC_ASSERT(sizeof new_msg == sizeof new_msg.header + sizeof new_msg.shmimage);
		// Created above
		qubes_rust_send_message(output->server->backend->rust_backend, (struct msg_hdr *)&new_msg);
	}
}

void qubes_output_dump_buffer(struct qubes_output *output, struct wlr_box box)
{
	assert(output->buffer->impl == qubes_buffer_impl_addr);
	wl_signal_add(&output->buffer->events.destroy, &output->buffer_destroy);
	wlr_log(WLR_DEBUG, "Sending MSG_WINDOW_DUMP (0x%x) to window %" PRIu32, MSG_WINDOW_DUMP, output->window_id);
	struct qubes_buffer *buffer = wl_container_of(output->buffer, buffer, inner);
	buffer->header.window = output->window_id;
	buffer->header.type = MSG_WINDOW_DUMP;
	buffer->header.untrusted_len = sizeof(buffer->qubes) + NUM_PAGES(buffer->size) * SIZEOF_GRANT_REF;
	qubes_rust_send_message(output->server->backend->rust_backend, &buffer->header);
	qubes_output_damage(output, box);
}

void qubes_output_ensure_created(struct qubes_output *output, struct wlr_box box)
{
	// implemented in Rust
	extern uint32_t qubes_rust_generate_id(void *backend, void *data) __attribute__((warn_unused_result));
	if (qubes_output_created(output))
		return;
	if (!output->window_id)
		output->window_id = qubes_rust_generate_id(output->server->backend->rust_backend, output);
	wlr_log(WLR_DEBUG, "Sending MSG_CREATE (0x%x) to window %" PRIu32, MSG_CREATE, output->window_id);
	struct {
		struct msg_hdr header;
		struct msg_create create;
	} msg = {
		.header = {
			.type = MSG_CREATE,
			.window = output->window_id,
			.untrusted_len = sizeof(struct msg_create),
		},
		.create = {
			.x = output->left,
			.y = output->top,
			.width = box.width,
			.height = box.height,
			.parent = 0,
			.override_redirect = (output->flags & QUBES_OUTPUT_OVERRIDE_REDIRECT) ? 1 : 0,
		},
	};
	QUBES_STATIC_ASSERT(sizeof msg == sizeof msg.header + sizeof msg.create);
	// This is MSG_CREATE
	qubes_rust_send_message(output->server->backend->rust_backend, (struct msg_hdr *)&msg);
	output->flags |= QUBES_OUTPUT_CREATED;
}

static bool qubes_output_commit(struct wlr_output *raw_output) {
	assert(raw_output->impl == &qubes_wlr_output_impl);
	struct qubes_output *output = wl_container_of(raw_output, output, output);
	assert(QUBES_VIEW_MAGIC == output->magic);
	struct tinywl_view *view = wl_container_of(output, view, output);

	struct wlr_box box;
	if (!qubes_view_ensure_created(view, &box))
		return false;
	qubes_output_ensure_created(output, box);

	if (raw_output->pending.committed & WLR_OUTPUT_STATE_MODE) {
		assert(raw_output->pending.mode_type == WLR_OUTPUT_STATE_MODE_CUSTOM);
		wlr_output_update_custom_mode(raw_output,
			raw_output->pending.custom_mode.width,
			raw_output->pending.custom_mode.height,
			raw_output->pending.custom_mode.refresh);
	}

	if ((raw_output->pending.committed & WLR_OUTPUT_STATE_BUFFER) &&
	    (output->buffer != raw_output->pending.buffer)) {
		if (output->buffer) {
			wl_list_remove(&output->buffer_destroy.link);
			wlr_buffer_unlock(output->buffer);
		}

		if ((output->buffer = raw_output->pending.buffer)) {
			wlr_buffer_lock(output->buffer);
			qubes_output_dump_buffer(output, box);
		}
	}
	if (raw_output->pending.committed & WLR_OUTPUT_STATE_ENABLED)
		wlr_output_update_enabled(raw_output, raw_output->pending.enabled);
	return true;
}

static const struct wlr_drm_format xrgb8888 = {
	.format = DRM_FORMAT_XRGB8888,
	.len = 2,
	.capacity = 0,
	.modifiers = { DRM_FORMAT_MOD_INVALID, DRM_FORMAT_MOD_LINEAR },
};
static const struct wlr_drm_format argb8888 = {
	.format = DRM_FORMAT_ARGB8888,
	.len = 2,
	.capacity = 0,
	.modifiers = { DRM_FORMAT_MOD_INVALID, DRM_FORMAT_MOD_LINEAR },
};

static const struct wlr_drm_format *const global_pointer_array[2] = {
	&xrgb8888,
	&argb8888,
};

static const struct wlr_drm_format_set global_formats = {
	.len = 2,
	.capacity = 0,
	.formats = (struct wlr_drm_format **)global_pointer_array,
};

static const struct wlr_drm_format_set *qubes_output_get_primary_formats(
		struct wlr_output *output __attribute__((unused)), uint32_t buffer_caps) {
	return &global_formats;
}

static const struct wlr_output_impl qubes_wlr_output_impl = {
	.set_cursor = NULL,
	.move_cursor = NULL,
	.destroy = qubes_output_deinit_raw,
	.test = qubes_output_test,
	.commit = qubes_output_commit,
	.get_gamma_size = qubes_get_gamma_size,
	.get_cursor_formats = NULL,
	.get_cursor_size = NULL,
	.get_primary_formats = qubes_output_get_primary_formats,
};

static void qubes_output_frame(struct wl_listener *listener, void *data __attribute__((unused))) {
	struct qubes_output *output = wl_container_of(listener, output, frame);
	// HACK HACK
	//
	// This fixes a *nasty* bug: without it, really fast resizes can cause the
	// wlr_output to lose sync with the qubes_output, causing parts of the
	// window to *never* be displayed until the next window resize.  This bug
	// took more than three days to fix.
	wlr_output_update_custom_mode(&output->output, output->last_width, output->last_height, 60000);
	if (QUBES_VIEW_MAGIC == output->magic) {
		struct tinywl_view *view = wl_container_of(output, view, output);
		if (wlr_scene_output_commit(view->scene_output)) {
			output->output.frame_pending = true;
			if (!output->server->frame_pending) {
				// Schedule another timer callback
				wl_event_source_timer_update(output->server->timer, 16);
				output->server->frame_pending = true;
			}
		}
	} else {
		assert(QUBES_XWAYLAND_MAGIC == output->magic);
	}
}

void qubes_output_init(struct qubes_output *output, struct wlr_backend *backend,
		                 struct tinywl_server *server, bool is_override_redirect) {
	memset(output, 0, sizeof *output);

	wlr_output_init(&output->output, backend, &qubes_wlr_output_impl, server->wl_display);
	wlr_output_update_custom_mode(&output->output, 1280, 720, 0);
	wlr_output_update_enabled(&output->output, true);
	wlr_output_set_description(&output->output, "Qubes OS virtual output");

	output->buffer = NULL;
	output->buffer_destroy.notify = qubes_unlink_buffer_listener;
	output->formats = &global_formats;
	output->frame.notify = qubes_output_frame;
	output->magic = QUBES_VIEW_MAGIC;
	output->flags = is_override_redirect ? QUBES_OUTPUT_OVERRIDE_REDIRECT : 0,
	output->server = server;
	wl_signal_add(&output->output.events.frame, &output->frame);

	wl_list_insert(&server->views, &output->link);
}

void qubes_send_configure(struct qubes_output *output, uint32_t width, uint32_t height)
{
	if (!qubes_output_created(output))
		return;
	if (width <= 0 || height <= 0)
		return;
	wlr_log(WLR_DEBUG, "Sending MSG_CONFIGURE (0x%x) to window %" PRIu32, MSG_CONFIGURE, output->window_id);
	struct {
		struct msg_hdr header;
		struct msg_configure configure;
	} msg = {
		.header = {
			.type = MSG_CONFIGURE,
			.window = output->window_id,
			.untrusted_len = sizeof(struct msg_configure),
		},
		.configure = {
			.x = output->left,
			.y = output->top,
			.width = width,
			.height = height,
			/* override_redirect is (thankfully) ignored in MSG_CONFIGURE */
			.override_redirect = false,
		},
	};
	QUBES_STATIC_ASSERT(sizeof msg == sizeof msg.header + sizeof msg.configure);
	qubes_rust_send_message(output->server->backend->rust_backend, (struct msg_hdr*)&msg);
}

void qubes_output_deinit(struct qubes_output *output) {
	wl_list_remove(&output->link);
	assert(output->magic == QUBES_VIEW_MAGIC || output->magic == QUBES_XWAYLAND_MAGIC);
	struct msg_hdr header = {
		.type = MSG_DESTROY,
		.window = output->window_id,
		.untrusted_len = 0,
	};
	if (qubes_output_created(output)) {
		wlr_log(WLR_DEBUG, "Sending MSG_DESTROY (0x%x) to window %" PRIu32, MSG_DESTROY, output->window_id);
		qubes_rust_send_message(output->server->backend->rust_backend, &header);
		qubes_rust_delete_id(output->server->backend->rust_backend, output->window_id);
	}
	wlr_output_destroy(&output->output);
}

void qubes_change_window_flags(struct qubes_output *output, uint32_t flags_set, uint32_t flags_unset)
{
	assert(qubes_output_created(output));
	struct {
		struct msg_hdr header;
		struct msg_window_flags flags;
	} msg = {
		.header = {
			.type = MSG_WINDOW_FLAGS,
			.window = output->window_id,
			.untrusted_len = sizeof(struct msg_window_flags),
		},
		.flags = {
			.flags_set = flags_set,
			.flags_unset = flags_unset,
		},
	};
	QUBES_STATIC_ASSERT(sizeof msg == sizeof msg.header + sizeof msg.flags);
	// Asserted above, checked at call sites
	qubes_rust_send_message(output->server->backend->rust_backend, (struct msg_hdr *)&msg);
}

void qubes_output_unmap(struct qubes_output *output) {
	output->flags &= ~(__typeof__(output->flags))QUBES_OUTPUT_MAPPED;
	wlr_output_enable(&output->output, false);
	wlr_log(WLR_DEBUG, "Sending MSG_UNMAP (0x%x) to window %" PRIu32, MSG_UNMAP, output->window_id);
	struct msg_hdr header = {
		.type = MSG_UNMAP,
		.window = output->window_id,
		.untrusted_len = 0,
	};
	if (qubes_output_created(output))
		qubes_rust_send_message(output->server->backend->rust_backend, &header);
}

void qubes_output_configure(struct qubes_output *output, struct wlr_box box) {
	qubes_output_ensure_created(output, box);
	if ((output->last_width != box.width || output->last_height != box.height) &&
	    !(output->flags & QUBES_OUTPUT_IGNORE_CLIENT_RESIZE)) {
		qubes_send_configure(output, box.width, box.height);
		wlr_log(WLR_DEBUG,
		        "Resized window %u: old size %u %u, new size %u %u",
		        (unsigned)output->window_id, output->last_width,
		        output->last_height, box.width, box.height);
		wlr_output_set_custom_mode(&output->output, box.width, box.height, 60000);
		output->last_width = box.width;
		output->last_height = box.height;
	}
	wlr_output_send_frame(&output->output);
}

/* vim: set noet ts=3 sts=3 sw=3 ft=c fenc=UTF-8: */

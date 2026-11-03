#include "wl.h"

#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>

#include "xdg-shell-client-protocol.h"

// Default size used until the compositor tells
// us otherwise via xdg_toplevel's configure
// event (a size of 0 from the compositor means
// "no preference").
static constexpr int32_t WL_DEFAULT_WIDTH = 1280;
static constexpr int32_t WL_DEFAULT_HEIGHT = 720;

// Give up waiting for the first xdg_surface
// configure after this many round-trips, rather
// than blocking forever against a compositor
// that never responds.
static constexpr int
    WL_CONFIGURE_ROUNDTRIP_LIMIT = 100;

// registry_global fires once for every global object the compositor advertises.
static void registry_global(void* data,
                             struct wl_registry* registry,
                             uint32_t name,                 // Unique ID of the global object
                             const char* interface,         // Name of the interface
                             uint32_t version) {            // Version of the interface
    struct Wl* wl = data;

    // Flint only needs the compositor and the xdg_wm_base shell,
    // so every other interface is ignored. Clamp the requested
    // version to the one the compositor advertises.
    if (strcmp(interface, wl_compositor_interface.name) ==
        0) {
        uint32_t bind_version =
            version < 4 ? version : 4;
        wl->compositor = wl_registry_bind(
            registry, name, &wl_compositor_interface,
            bind_version);
    } else if (strcmp(interface,
                       xdg_wm_base_interface.name) == 0) {
        uint32_t bind_version =
            version < 1 ? version : 1;
        wl->wm_base = wl_registry_bind(
            registry, name, &xdg_wm_base_interface,
            bind_version);
    }
}

// registry_global_remove fires when a global
// goes away. Nothing in Flint reacts to that
// yet, but the listener struct requires it.
static void registry_global_remove(
    void* data, struct wl_registry* registry,
    uint32_t name) {
    (void)data;
    (void)registry;
    (void)name;
}

// wm_base_ping answers the compositor's
// liveness check. Failing to pong in time gets
// the window treated as unresponsive and
// greyed out or killed.
static void wm_base_ping(void* data,
                          struct xdg_wm_base* wm_base,      // Object that receives the ping event
                          uint32_t serial) {                // Random serial identifier assigned by compositor
    (void)data;
    xdg_wm_base_pong(wm_base, serial);                      // Respond from the client
}

// xdg_surface_configure fires whenever the
// compositor finishes negotiating a change to
// the window's role state (the first one is
// what makes it legal to start presenting).
// Acking it is mandatory, and must happen
// before any later configure arrives.
static void xdg_surface_configure(
    void* data, struct xdg_surface* xdg_surface,
    uint32_t serial) {                                      // Identifier of the configure event
    struct Wl* wl = data;
    xdg_surface_ack_configure(xdg_surface, serial);         // Response that means that configure is acknowledged
    wl->configured = true;
}

// xdg_toplevel_configure suggests a size for
// the window. Either dimension can be 0,
// meaning the compositor has no preference and
// the client should pick one itself.
static void xdg_toplevel_configure(
    void* data, struct xdg_toplevel* toplevel,
    int32_t width, int32_t height,
    struct wl_array* states) {
    (void)toplevel;
    (void)states;                                           // State of the window: fullscreen, resized, etc.
    struct Wl* wl = data;

    // Either dimension can be 0, meaning no
    // preference for that axis; apply each
    // nonzero dimension independently.
    if (width > 0 && width != wl->width) {
        wl->width = width;
        wl->resized = true;
    }
    if (height > 0 && height != wl->height) {
        wl->height = height;
        wl->resized = true;
    }
}

// xdg_toplevel_close fires when the compositor
// wants the window closed, e.g. the user clicked
// the window's close button. Flint doesn't tear
// anything down here directly; it just flags it
// so the main loop can exit and unwind cleanly.
static void xdg_toplevel_close(
    void* data, struct xdg_toplevel* toplevel) {
    (void)toplevel;
    struct Wl* wl = data;
    wl->closed = true;
}

// These listener structs are kept alive for as
// long as their objects exist, since Wayland
// stores a pointer to them rather than copying
// them at registration time.
static const struct wl_registry_listener
    registry_listener = {
        .global = registry_global,
        .global_remove = registry_global_remove,
};
static const struct xdg_wm_base_listener
    wm_base_listener = {
        .ping = wm_base_ping,
};
static const struct xdg_surface_listener
    xdg_surface_listener = {
        .configure = xdg_surface_configure,
};
static const struct xdg_toplevel_listener
    xdg_toplevel_listener = {
        .configure = xdg_toplevel_configure,
        .close = xdg_toplevel_close,
};

// wl_init connects to the compositor,
// binds globals, and creates the window.
// On failure it releases partial state
// via wl_finish and returns false.
bool wl_init(struct Wl* wl) {
    memset(wl, 0, sizeof(*wl));
    wl->width = WL_DEFAULT_WIDTH;
    wl->height = WL_DEFAULT_HEIGHT;

    // Connect to the compositor named by
    // WAYLAND_DISPLAY, or the default socket
    // when unset.
    wl->display = wl_display_connect(nullptr);                 // Connects to the standard compositor when parameter is NULL
    if (!wl->display) {
        fprintf(stderr, "wl_display_connect failed\n");
        wl_finish(wl);
        return false;
    }
    fprintf(stderr, "Connected to compositor\n");

    wl->registry = wl_display_get_registry(wl->display);
    if (!wl->registry) {
        fprintf(stderr,
                "wl_display_get_registry failed\n");
        wl_finish(wl);
        return false;
    }

    wl_registry_add_listener(wl->registry,
                              &registry_listener, wl);

    // Round-trip so the global announcements
    // handled above are dispatched before we
    // check what got bound.
    if (wl_display_roundtrip(wl->display) < 0) {
        fprintf(stderr, "wl_display_roundtrip failed\n");
        wl_finish(wl);
        return false;
    }

    if (!wl->compositor) {
        fprintf(stderr,
                "wl_compositor global not found\n");
        wl_finish(wl);
        return false;
    }
    if (!wl->wm_base) {
        fprintf(stderr,
                "xdg_wm_base global not found\n");
        wl_finish(wl);
        return false;
    }

    xdg_wm_base_add_listener(wl->wm_base,
                              &wm_base_listener, wl);

    wl->surface =
        wl_compositor_create_surface(wl->compositor);
    if (!wl->surface) {
        fprintf(stderr,
                "wl_compositor_create_surface failed\n");
        wl_finish(wl);
        return false;
    }

    // The xdg_surface role is what lets this
    // plain wl_surface take part in window
    // management at all.
    wl->xdg_surface = xdg_wm_base_get_xdg_surface(
        wl->wm_base, wl->surface);
    if (!wl->xdg_surface) {
        fprintf(stderr,
                "xdg_wm_base_get_xdg_surface failed\n");
        wl_finish(wl);
        return false;
    }

    xdg_surface_add_listener(
        wl->xdg_surface, &xdg_surface_listener, wl);

    // The toplevel role on top of that is what
    // actually makes it a normal, decoratable
    // desktop window rather than e.g. a popup.
    wl->xdg_toplevel =
        xdg_surface_get_toplevel(wl->xdg_surface);
    if (!wl->xdg_toplevel) {
        fprintf(stderr,
                "xdg_surface_get_toplevel failed\n");
        wl_finish(wl);
        return false;
    }

    xdg_toplevel_add_listener(
        wl->xdg_toplevel, &xdg_toplevel_listener, wl);

    xdg_toplevel_set_title(wl->xdg_toplevel, "Flint");      // Human readable title
    xdg_toplevel_set_app_id(wl->xdg_toplevel, "flint");     // App id for desktop environment

    // Commit with no buffer attached yet. This
    // is what asks the compositor to start the
    // configure sequence.
    wl_surface_commit(wl->surface);

    // Block until the first xdg_surface
    // configure has arrived and been acked.
    // Presenting anything before this point is
    // not allowed by the protocol.
    int guard = 0;                                          // Count the number of attempts were made to finish the configuration
    while (!wl->configured) {
        if (wl_display_roundtrip(wl->display) < 0) {
            fprintf(stderr,
                    "wl_display_roundtrip failed while "
                    "waiting for configure\n");
            wl_finish(wl);
            return false;
        }
        guard++;
        if (guard > WL_CONFIGURE_ROUNDTRIP_LIMIT) {
            fprintf(stderr,
                    "compositor never sent an "
                    "xdg_surface configure\n");
            wl_finish(wl);
            return false;
        }
    }

    return true;
}

bool wl_dispatch(struct Wl* wl) {
    // Drain anything already queued locally
    // before attempting to read more off the
    // socket, as required by the prepare_read /
    // read_events protocol.
    while (wl_display_prepare_read(wl->display) != 0) {
        if (wl_display_dispatch_pending(wl->display) <
            0) {
            return false;
        }
    }

    if (wl_display_flush(wl->display) < 0 &&                // Send Wayland requests to compositor
        errno != EAGAIN) {
        wl_display_cancel_read(wl->display);
        return false;
    }

    // Zero timeout: this integrates with a
    // render loop rather than blocking waiting
    // for input between frames.
    struct pollfd pfd;
    memset(&pfd, 0, sizeof(pfd));
    pfd.fd = wl_display_get_fd(wl->display);                // Get file descriptor using by Wayland connection
    pfd.events = POLLIN;                                    // Checking for events from the compositor

    int n = poll(&pfd, 1, 0);                   //  Monitor descriptors one by one with no timeout
    if (n < 0) {
        wl_display_cancel_read(wl->display);
        return false;
    }

    if (n == 0 || !(pfd.revents & POLLIN)) {
        wl_display_cancel_read(wl->display);
    } else if (wl_display_read_events(wl->display) < 0) {
        return false;
    }

    if (wl_display_dispatch_pending(wl->display) < 0) {
        return false;
    }

    return !wl->closed;
}

void wl_finish(struct Wl* wl) {
    // Reverse order of creation: toplevel, xdg
    // surface, surface, then the two bound
    // globals, then the registry, then the
    // connection itself.
    if (wl->xdg_toplevel) {
        xdg_toplevel_destroy(wl->xdg_toplevel);
        wl->xdg_toplevel = nullptr;
    }
    if (wl->xdg_surface) {
        xdg_surface_destroy(wl->xdg_surface);
        wl->xdg_surface = nullptr;
    }
    if (wl->surface) {
        wl_surface_destroy(wl->surface);
        wl->surface = nullptr;
    }
    if (wl->wm_base) {
        xdg_wm_base_destroy(wl->wm_base);
        wl->wm_base = nullptr;
    }
    if (wl->compositor) {
        wl_compositor_destroy(wl->compositor);
        wl->compositor = nullptr;
    }
    if (wl->registry) {
        wl_registry_destroy(wl->registry);
        wl->registry = nullptr;
    }
    if (wl->display) {
        wl_display_disconnect(wl->display);
        wl->display = nullptr;
    }
}

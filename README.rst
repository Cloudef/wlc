FEATURES
--------

+------------------+-----------------------+
| Backends         | DRM, X11, Wayland     |
+------------------+-----------------------+
| Renderers        | EGL, GLESv2           |
+------------------+-----------------------+
| Buffer API       | GBM, EGL streams      |
+------------------+-----------------------+
| TTY session      | logind, legacy (suid) |
+------------------+-----------------------+
| Input            | libinput, xkb         |
+------------------+-----------------------+
| Monitor          | Multi-monitor, DPMS   |
+------------------+-----------------------+
| Hotplugging      | udev                  |
+------------------+-----------------------+
| Xwayland         | Supported             |
+------------------+-----------------------+
| Clipboard        | Partially working     |
+------------------+-----------------------+
| Drag'n'Drop      | Not implemented       |
+------------------+-----------------------+

EXAMPLE
-------

.. code:: c

    // For more functional example see example/example.c

    #include <stdlib.h>
    #include <wlc/wlc.h>

    static bool
    view_created(wlc_handle view)
    {
       wlc_view_set_mask(view, wlc_output_get_mask(wlc_view_get_output(view)));
       wlc_view_bring_to_front(view);
       wlc_view_focus(view);
       return true;
    }

    static void
    view_focus(wlc_handle view, bool focus)
    {
       wlc_view_set_state(view, WLC_BIT_ACTIVATED, focus);
    }

    int
    main(int argc, char *argv[])
    {
       wlc_set_view_created_cb(view_created);
       wlc_set_view_focus_cb(view_focus);

       if (!wlc_init())
          return EXIT_FAILURE;

       wlc_run();
       return EXIT_SUCCESS;
    }

ENV VARIABLES
-------------

``wlc`` reads the following env variables.

+-----------------------+-----------------------------------------------------+
| ``WLC_DRM_DEVICE``    | Device to use in DRM mode. (card0 default)          |
+-----------------------+-----------------------------------------------------+
| ``WLC_BUFFER_API``    | Force buffer API to ``GBM`` or ``EGL``.             |
+-----------------------+-----------------------------------------------------+
| ``WLC_SHM``           | Set 1 to force EGL clients to use shared memory.    |
+-----------------------+-----------------------------------------------------+
| ``WLC_OUTPUTS``       | Number of fake outputs in X11/Wayland mode.         |
+-----------------------+-----------------------------------------------------+
| ``WLC_XWAYLAND``      | Set 0 to disable Xwayland.                          |
+-----------------------+-----------------------------------------------------+
| ``WLC_LIBINPUT``      | Set 1 to force libinput. (Even on X11/Wayland)      |
+-----------------------+-----------------------------------------------------+
| ``WLC_REPEAT_DELAY``  | Keyboard repeat delay.                              |
+-----------------------+-----------------------------------------------------+
| ``WLC_REPEAT_RATE``   | Keyboard repeat rate.                               |
+-----------------------+-----------------------------------------------------+
| ``WLC_DEBUG``         | Enable debug channels (comma separated)             |
+-----------------------+-----------------------------------------------------+

KEYBOARD LAYOUT
---------------

You can set your preferred keyboard layout using ``XKB_DEFAULT_LAYOUT``.

See xkb documentation for more details.

RUNNING ON TTY
--------------

If you have ``logind``, you don't have to do anything.

Without ``logind`` you need to suid your binary to root user.
The permissions will be dropped runtime.

BUFFER API
----------

``wlc`` supports both ``GBM`` and ``EGL`` streams buffer APIs. The buffer API is auto-detected based on the driver used by the DRM device.

- ``GBM`` is supported by most GPU drivers except the NVIDIA proprietary driver.
- ``EGL`` is only supported by the NVIDIA proprietary. If you have a NVIDIA GPU using the proprietary driver you need to enable DRM KMS using the ``nvidia-drm.modeset=1`` kernel parameter.

You can force a given buffer API by setting the ``WLC_BUFFER_API`` environment variable to ``GBM`` or ``EGL``.

ISSUES
------

Submit issues on this repo if you are developing with ``wlc``.

As a user of compositor, report issues to their corresponding issue trackers.

BUILDING
--------

You will need following makedepends:

- cmake
- git

And the following depends:

- pixman
- wayland 1.7+
- wayland-protocols 1.7+ [1]
- libxkbcommon
- udev
- libinput
- libx11 (X11-xcb, Xfixes)
- libxcb (xcb-ewmh, xcb-composite, xcb-xkb, xcb-image, xcb-xfixes)
- libgbm (usually provided by mesa in most distros)
- libdrm
- libEGL (GPU drivers and mesa provide this)
- libGLESv2 (GPU drivers and mesa provide this)

1: Also bundled as submodule. To build from submodule use -DSOURCE_WLPROTO=ON.

And optionally:

- dbus (for logind support)
- systemd (for logind support)

For weston-terminal and other wayland clients for testing, you might also want to build weston from git.

You can build bootstrapped version of ``wlc`` with the following steps.

.. code:: sh

    git submodule update --init --recursive # - initialize and fetch submodules
    mkdir target && cd target               # - create build target directory
    cmake -DCMAKE_BUILD_TYPE=Upstream ..    # - run CMake
    make                                    # - compile

    # You can now run (Ctrl-Esc to quit)
    ./example/example

PACKAGING
---------

For now you can look at the `AUR recipe <https://aur.archlinux.org/packages/wlc-git/>`_ for a example.

Releases are signed with `1AF6D26A <http://pgp.mit.edu/pks/lookup?op=vindex&search=0xF769BB961AF6D26A>`_ and published `on GitHub <https://github.com/Cloudef/wlc/releases>`_.

All 0.0.x releases are considered unstable.

CONTRIBUTING
------------

See the `CONTRIBUTING <CONTRIBUTING.rst>`_ for more information.

BINDINGS
--------

- `ocaml-wlc <https://github.com/Armael/ocaml-wlc>`_ - OCaml (experimental)
- `go-wlc <https://github.com/mikkeloscar/go-wlc>`_ - Go
- `rust-wlc <https://github.com/Immington-Industries/rust-wlc>`_ - Rust
- `wlc.rs <https://github.com/Drakulix/wlc.rs>`_ - Rust
- `jwlc <https://github.com/Enerccio/jwlc>`_ - Java - work in progress

SOFTWARE USING WLC
------------------

- `orbment <https://github.com/Cloudef/orbment>`_ - Modular Wayland compositor
- `ocaml-loliwm <https://github.com/Armael/ocaml-loliwm>`_ - Translation of loliwm to OCaml
- `sway <https://github.com/SirCmpwn/sway>`_ - i3-compatible window manager for Wayland
- `way-cooler <https://github.com/Immington-Industries/way-cooler>`_ - customizeable window manager written in Rust
- `fireplace <https://github.com/Drakulix/fireplace>`_ - Modular wayland window manager written in Rust

SIMILAR SOFTWARE
----------------

- `ewlc <https://github.com/Enerccio/ewlc>`_ - A separately maintained fork of wlc
- `swc <https://github.com/michaelforney/swc>`_ - A library for making a simple Wayland compositor
- `libwlb <https://github.com/jekstrand/libwlb>`_ - A Wayland back-end library
- `libweston <https://github.com/giucam/weston/tree/libweston>`_ - Weston as a library

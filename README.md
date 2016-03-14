# librdpmux

## Table of contents
1. [Introduction](#introduction)
2. [Rationale](#rationale)
3. [API Overview](#api-overview)
4. [Protocol](#protocol)
5. [FAQ](#faq)

## Introduction
librdpmux provides an interface for interacting with virtual machine guests on a very low level. It provides access to the current framebuffer of the VM via shared memory, and exposes the ability to programmatically send and receive keyboard and mouse events.

It was designed and built in concert with [RDPMux](http://github.com/datto/rdpmux), a project that provides multiplexed RDP servers for virtual machines. 

For build and installation instructions, see [INSTALL.md](./INSTALL.md).

## Rationale

librdpmux was initially intended to be part of a project to build support for the RDP protocol into QEMU, in much the same way as SPICE. However, licensing incompatibilities necessitated the decision to split the RDP server functionality into [its own project](http://github.com/datto/rdpmux), and maintain the hypervisor interface as its own library. 

## API Overview

Complete documentation is available in the [Doxygen documentation](./doc/html/index.html). This section is a general overview of how to get started integrating support for the library into your code.

### General Flow

In general, the library API is divided into three main parts: 
1. Initialization
2. Outbound communication
3. Inbound communication

#### Initialization

The initialization functions are responsible for starting up all the data structures internally, registering with the RDPMux server, and connecting to the communication socket. The functions in this category are:

1. `mux_get_socket_path()` registers the VM with the RDPMux server
2. `mux_connect()` connects to the nanomsg socket created for this VM
3. `mux_init_display_struct()` initializes all the internal data structures used by the library

There are also three loop functions. The library is mainly driven by these loops, and all three need to be running for the library to be working. These loops are:

1. `mux_mainloop()` receives events from the server
2. `mux_display_buffer_update_loop()` manages display updates and coalesces them for transmission
3. `mux_out_loop()` sends messages to the server

#### Inbound Communication

Inbound communication functions are registered as callbacks in the `InputEventCallbacks` struct and passed into the library. These functions are called when the library receives an event and needs to pass it up to the hypervisor. Right now, the only two things that the library supports are mouse and keyboard events. Currently, `InputEventCallbacks` looks like this:
```C
typedef struct InputEventCallbacks {
    void (*mux_receive_kb)(uint32_t keycode, uint32_t flags);
    void (*mux_receive_mouse)(uint32_t x, uint32_t y, uint32_t flags);
} InputEventCallbacks;
```

Hopefully this looks pretty self-explanatory. Further information is available in the Doxygen documentation.

#### Outbound Communication
Outbound communication functions are used to transfer information and state from the hypervisor to the library for transmission to the RDPMux server. Currently the library supports sending framebuffer changes and startup/shutdown messages. As the feature set grows, expect more functions to be added in the future. Currently, the functions are:

1. `mux_display_update()` is meant to be called when a region of the framebuffer updates
2. `mux_display_refresh()` is meant to be called every time the virtual display refreshes.
3. `mux_display_switch()` is meant to be called when the framebuffer changes is a big way: subpixel layout change, resolution change, etc.

### Quickstart

#### Library Initialization
First, initialize the library's data structures by calling `mux_init_display_struct()`. This sets up all the internal data structures, but doesn't start anything up yet. This function returns a pointer to the main display struct set up by the library for its internal usage. You can choose to keep it around, though that provides very little advantage currently.

#### Service Registration
Registration and initialization of the communications portion of the library is done in two parts. You first get your socket path from the RDPMux server by calling `mux_get_socket_path()`. This gives you a file path to the private Nanomsg socket used for communication with your VM's personal RDP server.

Next, you want to call `mux_connect()` to actually connect to the Nanomsg socket.

#### Register Callback Functions
Mouse and keyboard events are delivered to the backend service via callback functions set via `mux_register_event_callbacks()`. The caller needs to create their own callback functions to handle incoming mouse and keyboard events, and pass them in via an `InputEventCallbacks` struct.

#### Starting the loops
To actually start the library's functionality, you need to spin up the three loop functions. These are: `mux_mainloop()`, `mux_out_loop()`, and `mux_display_buffer_update_loop()`. As a caveat: these functions all contain infinite loops that block until they are needed. If you see that one of these loops is using 100% CPU, chances are you missed one of the steps above.
Once you start these three loops up, the library will be fully operational and should require no other babysitting. 

## Protocol
RDPMux uses DBus for service registration, and Msgpack-encoded messages over a private socket for service communication.

### DBus Registration
RDPMux takes the well-known name `org.RDP.RDPMux` on the system bus, and exposes a method `Register` under the object `/org/RDPMux/Server`. 

Services that wish to expose a backend to the RDPMux server should call `Register` with an integer value between 0 and INT_MAX. RDPMux uses this number as your VM's ID internally. In return, the caller will receive a path to the private nanomsg socket that should be used for communicating updates back and forth between the RDP server and the VM. 

librdpmux implements this flow as part of its exposed API, in case you don't want to do it yourself.

### Normal VM communication
Messages can be one of the following types:
```C
enum message_type {
    DISPLAY_UPDATE,
    DISPLAY_SWITCH,
    MOUSE,
    KEYBOARD,
    DISPLAY_UPDATE_COMPLETE
};
```

#### General message flow

After a VM is registered using the DBus endpoint discussed above, two things have happened: an RDP server has been created and is listening for client connections, and a private socket has been created for communication between RDPMux and the backend service. 

The VM/hypervisor should connect to this socket and begin listening for messages on it. Nanomsg sockets are full duplex, so messages should also be sent using this socket.

Messages are encoded as Messagepack arrays of ints over the wire. The first element of the array is always going to be the type of message, and then the rest of the elements in the array will be specific to the message type. More about that below.

In general, MOUSE and KEYBOARD messages are usually sent _from_ the RDPMux server (passed on from the RDP client) _to_ the hypervisor. DISPLAY_UPDATE, DISPLAY_SWITCH, and DISPLAY_UPDATE_COMPLETE messages are sent _from_ the hypervisor _to_ the RDPMux server for handling and communication to the RDP clients connected to that VM's RDP frontend. 

#### DISPLAY_UPDATE

DISPLAY_UPDATE messages are used to communicate normal screen region updates. These are usually sent once every refresh tick by the hypervisor and contain information about the region of the screen that needs updating. They have four fields:
```C
typedef struct display_update {
    /**
     * @brief X-coordinate of top left corner of region
     */
    int x;
    /**
     * @brief Y-coordinate of top left corner of region
     */
    int y;
    /**
     * @brief width of region
     */
    int w;
    /**
     * @brief height of region
     */
    int h;
} display_update;
```

#### DISPLAY_SWITCH

DISPLAY_SWITCH messages are used to communicate that the VM's backing framebuffer has changed in a frontend-facing way. Typically these messages are sent when the subpixel layout or resolution (or both!) of the framebuffer has changed. They have three fields:
```C
typedef struct display_switch {
    /**
     * @brief file descriptor for the shared memory region.
     */
    int shm_fd;
    /**
     * @brief format code describing the pixel layout of the framebuffer.
     */
    pixman_format_code_t format;
    /**
     * @brief width of framebuffer in px.
     */
    int w;
    /**
     * @brief height of framebuffer in px.
     */
    int h;
} display_switch;
```

#### MOUSE

Mouse events communicate changes in the mouse cursor state. Things like mouse clicks and cursor moves are communicated via this message type. They have three fields:
```C
typedef struct mouse_update {
    /**
     * @brief flags associated with the mouse update event. Usually used to denote extended buttons and/or what type
     * of mouse event it is (movement, click, etc.)
     */
    uint32_t flags;
    /**
     * @brief X-coordinate of the mouse cursor in px.
     */
    uint32_t x;
    /**
     * @brief Y-coordinate of the mouse cursor in px.
     */
    uint32_t y;
} mouse_update;
```

#### KEYBOARD

Keyboard events communicate keyboard key press and release events. They have two fields:
```C
typedef struct kb_update {
    /**
     * @brief flags associated with the event. Could be anything, really. Commonly used for things like keyup/keydown
     * events and any special flags associated with the keypress.
     */
    uint32_t flags;
    /**
     * @brief The keycode. Could be anything, depends on what the client side application sends.
     */
    uint32_t keycode;
} kb_update;
```

#### DISPLAY_UPDATE_COMPLETE

This update is meant to aid in the synchronization of the display buffer between the VM and the RDPMux server. During the display update cycle, the framebuffer is being concurrently accessed by both the VM (to write new framebuffer information) and RDPMux (to read framebuffer information back out). Because of this concurrent access, there is a possibility that RDPMux will read out inconsistent or corrupt framebuffer data and render that to the clients. 

To prevent this, we use DISPLAY_UPDATE_COMPLETE messages to communicate that RDPMux has finished copying out framebuffer information. The intention is that after sending a DISPLAY_UPDATE message, the hypervisor should not attempt to write to the framebuffer until it has received this message.

It has one field:
```C
typedef struct update_ack {
    /**
     * @brief whether the update succeeded.
     */
    bool success;
} update_ack;
```

## FAQ

**Why didn't you build this into QEMU/Xen/another hypervisor?**

That was the original plan. However, this library was developed in concert with [RDPMux](https://github.com/datto/rdpmux), and the decision was made to split this out into a library so that we could ship updates to our software without being tied to upstream's release cadence. SPICE actually ships their server implementation in the same way for similar reasons, so this is not without precedent.

**Why are you using Nanomsg? That project seems to be abandoned!**

Well, possibly. The situation with Nanomsg is still up in the air. As it stands, we don't use Nanomsg for very much more than a small full-duplex socket library anyway, so it's a fairly minimal concern to us if it's unsupported, and the functionality it provides can be easily duplicated if necessary.

**I want more documentation than just this!**

There is plenty more documentation both in the code and as [Doxygen documentation](./doc/html/index.html). If you have questions after that, file an issue with the tag "question" and it'll be answered. 

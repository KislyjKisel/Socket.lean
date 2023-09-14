import Socket.Basic

namespace Socket
namespace Socket

/--
  Create a new `Socket` using the specified domain and type.
-/
@[extern "lean_socket_mk"] opaque mk (d : @& AddressFamily) (t : @& SockType) : IO Socket

/--
  Close the `Socket`.

  *NOTE:* Although Socket is designed to be automatically closed when garbage collected,
  it's a good practice to manually close it beforehand.
-/
@[extern "lean_socket_close"] opaque close (s : @& Socket) : IO Unit

/--
  Initiate a connection on a socket.
-/
@[extern "lean_socket_connect"] opaque connect (s : @& Socket) (a : @& SockAddr) : IO Unit

/--
  Bind a name to a socket.
-/
@[extern "lean_socket_bind"] opaque bind (s : @& Socket) (a : @& SockAddr) : IO Unit

/--
  Listen for connections on a socket.
-/
@[extern "lean_socket_listen"] opaque listen (s : @& Socket) (n : @& UInt8) : IO Unit

/--
  Accept a connection on a socket.
-/
@[extern "lean_socket_accept"] opaque accept (s : @& Socket) : IO (SockAddr × Socket)

/--
  Send a message from a socket.
-/
@[extern "lean_socket_send"] opaque send (s : @& Socket) (b : @& ByteArray) : IO USize

/--
  Receive a message from a socket.
-/
@[extern "lean_socket_recv"] opaque recv (s : @& Socket) (n : @& USize) : IO (Option ByteArray)

/--
  Send a message from a socket.
-/
@[extern "lean_socket_sendto"] opaque sendto (s : @& Socket) (b : @& ByteArray) (a : @& SockAddr) : IO USize

/--
  Receive a message from a socket.
-/
@[extern "lean_socket_recvfrom"] opaque recvfrom (s : @& Socket) (n : @& USize) : IO (SockAddr × Option ByteArray)

/--
  Shut down part of a full-duplex connection.
-/
@[extern "lean_socket_shutdown"] opaque shutdown (s : @& Socket) (h : ShutdownHow) : IO Unit 

/--
  Get address of connected peer.
-/
@[extern "lean_socket_peer"] opaque peer (s : @& Socket) : IO SockAddr

/--
  Mark socket as blocking or non-blocking.
-/
@[extern "lean_socket_setblocking"] opaque setBlocking (s : @& Socket) (blocking : Bool) : IO Unit

/--
  Check if the socket is blocking. Returns an error on Windows.
-/
@[extern "lean_socket_getblocking"] opaque blocking (s : @& Socket) : IO Bool

structure Poll where
  sock : Socket
  events : UInt16
  revents : UInt16
  ignore : Bool

@[extern "lean_socket_poll_in"]
private opaque Poll.in' : Unit → UInt16

@[extern "lean_socket_poll_pri"]
private opaque Poll.pri' : Unit → UInt16

@[extern "lean_socket_poll_out"]
private opaque Poll.out' : Unit → UInt16

@[extern "lean_socket_poll_err"]
private opaque Poll.err' : Unit → UInt16

@[extern "lean_socket_poll_hup"]
private opaque Poll.hup' : Unit → UInt16

@[extern "lean_socket_poll_nval"]
private opaque Poll.nval' : Unit → UInt16

/-- There is data to read. -/
def Poll.in := Poll.in' ()

/--
There is some exceptional condition on the socket.
Possibilities include:
* There is out-of-band data on a TCP socket.
* A `cgroup.events` file has been modified.
-/
def Poll.pri := Poll.pri' ()

/--
Writing is now possible,
though a write larger than the available space in a socket will still block.
-/
def Poll.out := Poll.out' ()

/-- Error condition (only returned in `revents`; ignored in `events`). -/
def Poll.err := Poll.err' ()

/--
Hang up (only returned in `revents`; ignored in `events`).
Note that when reading from a channel such as a stream socket,
this event merely indicates that the peer closed its end of the channel.
Subsequent reads from the channel will return 0 (EOF) only
after all outstanding data in the channel has been consumed.
-/
def Poll.hup := Poll.hup' ()

/-- Invalid request: socket not open (only returned in `revents`, ignored in `events`). -/
def Poll.nval := Poll.nval' ()

/--
  Wait for one of a set of sockets to become ready to perform I/O.
  Updates `revents` fields of the structs in the passed array, where the `ignore` field is set to false.
  NOTE: `timeout` is used as `Int32`; negative value means inifnite timeout, zero means return immediately.
-/
@[extern "lean_socket_poll"] opaque poll (s : Array Poll) (timeout : UInt32) : IO (Array Poll)

end Socket
end Socket

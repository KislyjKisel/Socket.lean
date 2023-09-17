import Socket.Basic

namespace Socket
namespace SockAddr

/-- Create a [`SockAddr`](##Socket.SockAddr). -/
@[extern "lean_sockaddr_mk"]
opaque mk
  (host : @& String)
  (port : @& String)
  (family : AddressFamily := AddressFamily.unspecified)
  (type : SockType := SockType.unspecified)
  : IO SockAddr

/-- Get family of the [`SockAddr`](##Socket.SockAddr). -/
@[extern "lean_sockaddr_family"] opaque family (a : @& SockAddr) : Option AddressFamily

/-- Get family of the [`SockAddr`](##Socket.SockAddr). -/
@[extern "lean_sockaddr_port"] opaque port (a : @& SockAddr) : Option UInt16

/-- Get family of the [`SockAddr`](##Socket.SockAddr). -/
@[extern "lean_sockaddr_host"] opaque host (a : @& SockAddr) : Option String

/--
Equality of socket addresses.
Supports only inet and inet6 address families, always returns false for others.
-/
@[extern "lean_sockaddr_beq"] opaque beq (a1 a2 : @& SockAddr) : Bool

end SockAddr

/-- Convert [`SockAddr`](##Socket.SockAddr) to `String`. -/
instance : ToString SockAddr where
  toString a := 
    let host := a.host.getD "none"
    let port := a.port.map (s!"{·}") |>.getD "none"
    let family := a.family.map (s!"{·}") |>.getD "none"
    s!"({host}, {port}, {family})"

instance : BEq SockAddr := ⟨SockAddr.beq⟩

end Socket

// ==============================================================================
// # Includes
// ==============================================================================

#include <lean/lean.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <winbase.h>
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "advapi32.lib")

#else

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>

#endif

// ==============================================================================
// # Statics and Types
// ==============================================================================

typedef struct sockaddr sockaddr;
typedef struct sockaddr_in sockaddr_in;
typedef struct sockaddr_in6 sockaddr_in6;
typedef struct sockaddr_storage sockaddr_storage;
typedef struct addrinfo addrinfo;

/**
 * Group sockaddr and its length together as a external class.
 */
typedef struct sockaddr_len
{
    socklen_t address_len;
    sockaddr_storage address;
} sockaddr_len;

/**
 * External class for Socket.
 * 
 * This class register `SOCKET *` (which is `int *` on *nix) as a lean external class.
 */
static lean_external_class *g_socket_external_class = NULL;

/**
 * External class for SockAddr.
 * 
 * This class register `sockaddr_len *` as a lean external class.
 */
static lean_external_class *g_sockaddr_external_class = NULL;

// ==============================================================================
// # Utilities
// ==============================================================================

// ## Socket Types and Functions

#ifdef _WIN32

#define ISVALIDSOCKET(s) ((s) != INVALID_SOCKET)
#define CLOSESOCKET(s) closesocket(s)

#ifndef IPV6_V6ONLY
#define IPV6_V6ONLY 27
#endif

static void cleanup()
{
    WSACleanup();
}

#else

#define ISVALIDSOCKET(s) ((s) >= 0)
#define CLOSESOCKET(s) close(s)
#define SOCKET int

#endif

// ## Conversion

lean_object *lean_option_mk_some(lean_object *v)
{
    lean_object *option = lean_alloc_ctor(1, 1, 0);
    lean_ctor_set(option, 0, v);
    return option;
}

lean_object *lean_option_mk_none()
{
    return lean_alloc_ctor(0, 0, 0);
}

static int address_family_unbox(uint8_t af)
{
    switch (af)
    {
    case 0:
        return AF_UNSPEC;
    case 1:
        return AF_INET;
    case 2:
        return AF_INET6;
    default:
        return AF_UNSPEC;
    }
}

static uint8_t sock_type_box(int type)
{
    switch (type)
    {
    case 0:
        return 0;
    case SOCK_STREAM:
        return 1;
    case SOCK_DGRAM:
        return 2;
    default:
        return 0;
    }
}

static int sock_type_unbox(uint8_t type)
{
    switch (type)
    {
    case 0:
        return 0;
    case 1:
        return SOCK_STREAM;
    case 2:
        return SOCK_DGRAM;
    default:
        return -1;
    }
}

/**
 * `SOCKET *` -> `lean_object *`(`Socket`) conversion
 * use macro instead of function to avoid foward declaration
 */
lean_object *socket_box(SOCKET *s)
{
    return lean_alloc_external(g_socket_external_class, s);
}

/**
 * `lean_object *`(`Socket`) -> `SOCKET *` conversion
 * use macro instead of function to avoid foward declaration
 */
SOCKET *socket_unbox(lean_object *s) { return (SOCKET *)(lean_get_external_data(s)); }

/**
 * `sockaddr_len` -> `lean_object *`(`SockAddr`) conversion
 * use macro instead of function to avoid foward declaration
 */
lean_object *sockaddr_len_box(sockaddr_len *s)
{
    return lean_alloc_external(g_sockaddr_external_class, s);
}

/**
 * `lean_object *`(`SockAddr`) -> `sockaddr_len *` conversion
 * use macro instead of function to avoid foward declaration
 */
sockaddr_len *sockaddr_len_unbox(lean_object *s)
{
    return (sockaddr_len *)(lean_get_external_data(s));
}

// ## Errors

extern lean_obj_res lean_mk_io_user_error(lean_obj_arg);

static lean_obj_res get_socket_error()
{
#ifdef _WIN32
    int errnum = WSAGetLastError();
    wchar_t *s = NULL;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   NULL, errnum,
                   MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                   (LPWSTR)&s, 0, NULL);
    char *err_str;
    wcstombs(err_str, s, 100);
    lean_object *details = lean_mk_string(err_str);
    LocalFree(s);
#else
    int errnum = errno;
    lean_object *details = lean_mk_string(strerror(errnum));
#endif
    return lean_mk_io_user_error(details);
}

// ==============================================================================
// # Initialization
// ==============================================================================

// ## Finalizers

/**
 * `Socket` destructor, which ensures that the socket is closed when garbage collected.
 */
inline static void socket_finalizer(void *socket_ptr)
{
    SOCKET *converted = (SOCKET *)socket_ptr;
    CLOSESOCKET(*converted);
    free(converted);
}

/**
 * `SockAddr` destructor, which ensures that the socket is closed when garbage collected.
 */
inline static void sockaddr_finalizer(void *sal)
{
    free((sockaddr_len *)sal);
}

// ## Foreach iterators

/**
 * A do nothing iterator.
 */
inline static void noop_foreach(void *mod, b_lean_obj_arg fn) {}

// ## Initialization Entry

/**
 * Initialize socket environment.
 * 
 * This function does the following things:
 * 1. register `Socket` and `SockAddr` class
 * 2. WSAStartup on windows
 * 3. register WSACleanup on windows
 * 
 * This function should always be called with `initialize`.
 */
lean_obj_res lean_socket_initialize()
{
    g_socket_external_class = lean_register_external_class(socket_finalizer, noop_foreach);
    g_sockaddr_external_class = lean_register_external_class(sockaddr_finalizer, noop_foreach);
#ifdef _WIN32
    WSADATA d;
    if (WSAStartup(MAKEWORD(2, 2), &d))
    {
        return lean_io_result_mk_error(get_socket_error());
    }
    if (atexit(cleanup))
    {
        int errnum = errno;
        lean_object *details = lean_mk_string(strerror(errnum));
        return lean_io_result_mk_error(lean_mk_io_user_error(details));
    }
#endif
    return lean_io_result_mk_ok(lean_box(0));
}

// ==============================================================================
// # External Implementation
// ==============================================================================

// ## Socket

/**
 * opaque Socket.mk (d : AddressFamily) (t : SockType) : IO Socket
 */
lean_obj_res lean_socket_mk(uint8_t af_obj, uint8_t type_obj, lean_obj_arg w)
{
    int family = address_family_unbox(af_obj);
    int type = sock_type_unbox(type_obj);
    SOCKET *socket_fd = malloc(sizeof(SOCKET));
    *socket_fd = socket(family, type, 0);
    if (ISVALIDSOCKET(*socket_fd))
    {
        return lean_io_result_mk_ok(socket_box(socket_fd));
    }
    else
    {
        return lean_io_result_mk_error(get_socket_error());
    }
}

/**
 * opaque Socket.close (s : @& Socket) : IO Unit
 */
lean_obj_res lean_socket_close(b_lean_obj_arg s, lean_obj_arg w)
{
    if (CLOSESOCKET(*socket_unbox(s)) == 0)
    {
        return lean_io_result_mk_ok(lean_box(0));
    }
    else
    {
        return lean_io_result_mk_error(get_socket_error());
    }
}

/**
 * opaque Socket.connect (s : @& Socket) (a : @& SockAddr) : IO Unit
 */
lean_obj_res lean_socket_connect(b_lean_obj_arg s, b_lean_obj_arg a, lean_obj_arg w)
{
    sockaddr_len *sa = sockaddr_len_unbox(a);
    if (connect(*socket_unbox(s), (sockaddr *)&(sa->address), sa->address_len) == 0)
    {
        return lean_io_result_mk_ok(lean_box(0));
    }
    else
    {
        return lean_io_result_mk_error(get_socket_error());
    }
}

/**
 * opaque Socket.bind (s : @& Socket) (a : @& SockAddr) : IO Unit
 */
lean_obj_res lean_socket_bind(b_lean_obj_arg s, b_lean_obj_arg a, lean_obj_arg w)
{
    sockaddr_len *sa = sockaddr_len_unbox(a);
    if (bind(*socket_unbox(s), (sockaddr *)&(sa->address), sa->address_len) == 0)
    {
        return lean_io_result_mk_ok(lean_box(0));
    }
    else
    {
        return lean_io_result_mk_error(get_socket_error());
    }
}

/**
 * opaque Socket.listen (s : @& Socket) (n : @& UInt8) : IO Unit
 */
lean_obj_res lean_socket_listen(b_lean_obj_arg s, uint8_t n, lean_obj_arg w)
{
    if (listen(*socket_unbox(s), (int)n) == 0)
    {
        return lean_io_result_mk_ok(lean_box(0));
    }
    else
    {
        return lean_io_result_mk_error(get_socket_error());
    }
}

/**
 * opaque Socket.accept (s : @& Socket) : IO (SockAddr × Socket)
 */
lean_obj_res lean_socket_accept(b_lean_obj_arg s, lean_obj_arg w)
{
    sockaddr_len *sal = malloc(sizeof(sockaddr_len));
    SOCKET *new_fd = malloc(sizeof(SOCKET));
    sal->address_len = sizeof(sockaddr);
    *new_fd = accept(*socket_unbox(s), (sockaddr *)(&(sal->address)), &(sal->address_len));
    if (ISVALIDSOCKET(*new_fd))
    {
        // printf("%d\n", sal->address_len);
        lean_object *o = lean_alloc_ctor(0, 2, 0);
        lean_ctor_set(o, 0, sockaddr_len_box(sal));
        lean_ctor_set(o, 1, socket_box(new_fd));
        return lean_io_result_mk_ok(o);
    }
    else
    {
        printf("fail\n");
        return lean_io_result_mk_error(get_socket_error());
    }
}

/**
 * opaque Socket.shutdown (s : @& Socket) (h : ShutdownHow) : IO Unit 
 */
lean_obj_res lean_socket_shutdown(b_lean_obj_arg s, uint8_t h, lean_obj_arg w)
{
    if (listen(*socket_unbox(s), h))
    {
        return lean_io_result_mk_ok(lean_box(0));
    }
    else
    {
        return lean_io_result_mk_error(get_socket_error());
    }
}

/**
 * opaque Socket.send (s : @& Socket) (b : @& ByteArray) : IO USize
 */
lean_obj_res lean_socket_send(b_lean_obj_arg s, b_lean_obj_arg b, lean_obj_arg w)
{
    lean_sarray_object *arr = lean_to_sarray(b);
    ssize_t bytes = send(*socket_unbox(s), arr->m_data, arr->m_size, 0);
    if (bytes >= 0)
    {
        return lean_io_result_mk_ok(lean_box_usize(bytes));
    }
    else
    {
        int errnum = errno;
        if (errnum == EAGAIN || errnum == EWOULDBLOCK)
        {
            return lean_io_result_mk_ok(lean_box_usize(0));
        }
        else
        {
            return lean_io_result_mk_error(get_socket_error());
        }
    }
}

/**
 * opaque Socket.sendto (s : @& Socket) (b : @& ByteArray) (a : @& SockAddr) : IO USize
 */
lean_obj_res lean_socket_sendto(b_lean_obj_arg s, b_lean_obj_arg b, b_lean_obj_arg a, lean_obj_arg w)
{
    lean_sarray_object *arr = lean_to_sarray(b);
    sockaddr_len *sal = sockaddr_len_unbox(a);
    ssize_t bytes = sendto(*socket_unbox(s), arr->m_data, arr->m_size, 0, (sockaddr *)&(sal->address), sal->address_len);
    if (bytes >= 0)
    {
        return lean_io_result_mk_ok(lean_box_usize(bytes));
    }
    else
    {
        int errnum = errno;
        if (errnum == EAGAIN || errnum == EWOULDBLOCK)
        {
            return lean_io_result_mk_ok(lean_box_usize(0));
        }
        else
        {
            return lean_io_result_mk_error(get_socket_error());
        }
    }
}

/**
 * opaque Socket.recv (s : @& Socket) (n : @& USize) : IO (Option ByteArray)
 */
lean_obj_res lean_socket_recv(b_lean_obj_arg s, size_t n, lean_obj_arg w)
{
    lean_object *arr = lean_alloc_sarray(1, 0, n);
    ssize_t bytes = recv(*socket_unbox(s), lean_sarray_cptr(arr), n, 0);
    if (bytes >= 0)
    {
        lean_to_sarray(arr)->m_size = bytes;
        return lean_io_result_mk_ok(lean_option_mk_some(arr));
    }
    else
    {
        lean_dec_ref(arr);
        int errnum = errno;
        if (errnum == EAGAIN || errnum == EWOULDBLOCK)
        {
            return lean_io_result_mk_ok(lean_option_mk_none());
        }
        else
        {
            return lean_io_result_mk_error(get_socket_error());
        }
    }
}

/**
 * opaque Socket.recvfrom (s : @& Socket) (n : @& USize) : IO (Option (SockAddr × ByteArray))
 */
lean_obj_res lean_socket_recvfrom(b_lean_obj_arg s, size_t n, lean_obj_arg w)
{
    sockaddr_len *sal = malloc(sizeof(sockaddr_len));
    lean_object *arr = lean_alloc_sarray(1, 0, n);
    ssize_t bytes = recvfrom(*socket_unbox(s), lean_sarray_cptr(arr), n, 0, (sockaddr *)&(sal->address), &(sal->address_len));
    if (bytes >= 0)
    {
        lean_to_sarray(arr)->m_size = bytes;
        lean_object *o = lean_alloc_ctor(0, 2, 0);
        lean_ctor_set(o, 0, sockaddr_len_box(sal));
        lean_ctor_set(o, 1, arr);
        return lean_io_result_mk_ok(lean_option_mk_some(o));
    }
    else
    {
        lean_dec_ref(arr);
        free(sal);
        int errnum = errno;
        if (errnum == EAGAIN || errnum == EWOULDBLOCK)
        {
            return lean_io_result_mk_ok(lean_option_mk_none());
        }
        else
        {
            return lean_io_result_mk_error(get_socket_error());
        }
    }
}

/**
 * opaque Socket.peer (s : @& Socket) : IO SockAddr
 */
lean_obj_res lean_socket_peer(b_lean_obj_arg s, lean_obj_arg w)
{
    sockaddr_len *sal = malloc(sizeof(sockaddr_len));
    int status = getpeername(*socket_unbox(s), (sockaddr *)&(sal->address), &(sal->address_len));
    if (status == 0)
    {
        return lean_io_result_mk_ok(sockaddr_len_box(sal));
    }
    else
    {
        return lean_io_result_mk_error(get_socket_error());
    }
}

/**
 * opaque setBlocking (s : @& Socket) (blocking : Bool) : IO Unit
 */
lean_obj_res lean_socket_setblocking(b_lean_obj_arg s, uint8_t blocking, lean_obj_arg w)
{
#ifdef _WIN32
    SOCKET s = *socket_unbox(s);
    unsigned long mode = blocking ? 0 : 1;
    int status = ioctlsocket(*socket_unbox(s), FIONBIO, &mode) == 0;
    if (status == 0)
    {
        return lean_io_result_mk_ok(lean_box(0));
    }
    else
    {
        return lean_io_result_mk_error(get_socket_error());
    }
#else
    SOCKET fd = *socket_unbox(s);
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1)
    {
        return lean_io_result_mk_error(get_socket_error());
    }
    flags = blocking ? (flags & ~O_NONBLOCK) : (flags | O_NONBLOCK);
    int status = fcntl(fd, F_SETFL, flags);
    if (status == 0)
    {
        return lean_io_result_mk_ok(lean_box(0));
    }
    else
    {
        return lean_io_result_mk_error(get_socket_error());
    }
#endif
}

/**
 * opaque blocking (s : @& Socket) : IO Bool
 */
lean_obj_res lean_socket_getblocking(b_lean_obj_arg s, lean_obj_arg w)
{
#ifdef _WIN32
    return return lean_io_result_mk_error(lean_mk_string("Can't check if the socket is blocking on Windows"));
#else
    int flags = fcntl(*socket_unbox(s), F_GETFL, 0);
    if (flags == -1)
    {
        return lean_io_result_mk_error(get_socket_error());
    }
    else
    {
        return lean_io_result_mk_ok(lean_box((flags & O_NONBLOCK) == 0));
    }
#endif
}

uint16_t lean_socket_poll_in(lean_obj_arg unit) { return POLLIN; };
uint16_t lean_socket_poll_pri(lean_obj_arg unit) { return POLLPRI; };
uint16_t lean_socket_poll_out(lean_obj_arg unit) { return POLLOUT; };
uint16_t lean_socket_poll_err(lean_obj_arg unit) { return POLLERR; };
uint16_t lean_socket_poll_hup(lean_obj_arg unit) { return POLLHUP; };
uint16_t lean_socket_poll_nval(lean_obj_arg unit) { return POLLNVAL; };

/**
 * opaque poll (s : Array Poll) (timeout : UInt32) : IO (Array Poll)
 */
lean_obj_res lean_socket_poll(lean_obj_arg s, uint32_t timeout, lean_obj_arg w) {
#ifdef _WIN32
#error TODO
#else
    size_t n = lean_array_size(s);
    struct pollfd * pollfds = malloc(n * sizeof(struct pollfd));
    for (size_t i = 0; i < n; ++i) {
        lean_object* lP = lean_array_get_core(s, i);
        pollfds[i].fd = *socket_unbox(lean_ctor_get(lP, 0));
        if (lean_unbox(lean_ctor_get(lP, 3)) == 1) {
            pollfds[i].fd = ~pollfds[i].fd;
        }
        pollfds[i].events = (uint16_t)lean_unbox(lean_ctor_get(lP, 1));
        pollfds[i].revents = 0;
    }
    int res = poll(pollfds, n, (int32_t)timeout);
    if (res < 0) {
        lean_dec_ref(s);
        free(pollfds);
        return lean_io_result_mk_error(get_socket_error());
    }
    if (lean_is_exclusive(s)) {
        for (size_t i = 0; i < n; ++i) {
            lean_object* lP = lean_array_get_core(s, i);
            if (lean_unbox(lean_ctor_get(lP, 3)) == 0) {
                lean_ctor_set(lP, 2, lean_box((uint16_t)pollfds[i].revents));
            }
        }
        free(pollfds);
        return lean_io_result_mk_ok(s);
    }
    else {
        lean_object* sCpy = lean_alloc_array(n, n);
        for (size_t i = 0; i < n; ++i) {
            lean_object* lP = lean_array_get_core(s, i);
            lean_object* lPCpy = lean_alloc_ctor(0, 4, 0);
            lean_object* lSock = lean_ctor_get(lP, 0);
            size_t ignore = lean_unbox(lean_ctor_get(lP, 3));
            uint16_t revents = ignore == 1 ?
                lean_unbox(lean_ctor_get(lP, 2))
                : (uint16_t)pollfds[i].revents;
            lean_inc_ref(lSock);
            lean_ctor_set(lPCpy, 0, lSock);
            lean_ctor_set(lPCpy, 1, lean_ctor_get(lP, 1));
            lean_ctor_set(lPCpy, 2, lean_box(revents));
            lean_ctor_set(lPCpy, 3, lean_box(ignore));
            lean_array_set_core(sCpy, i, lPCpy);
        }
        lean_dec_ref(s);
        free(pollfds);
        return lean_io_result_mk_ok(sCpy);
    }
#endif
}

// ## SockAddr

/**
 * opaque mk
  (host : @& String)
  (port : @& String)
  (family : AddressFamily := AddressFamily.unspecified)
  (type : SockType := SockType.unspecified)
  : IO SockAddr
 */
lean_obj_res lean_sockaddr_mk(b_lean_obj_arg h, b_lean_obj_arg p, uint8_t f, uint8_t t, lean_obj_arg w)
{
    const char *host = lean_string_cstr(h);
    const char *port = lean_string_cstr(p);
    int family = address_family_unbox(f);
    int type = sock_type_unbox(t);
    addrinfo hints;
    addrinfo *ai_res;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = family;
    hints.ai_socktype = type;
    sockaddr_len *sal = malloc(sizeof(sockaddr_len));
    int status = getaddrinfo(host, port, &hints, &ai_res);
    if (status < 0)
    {
#ifdef _WIN32
        int errnum = WSAGetLastError();
        wchar_t *s = NULL;
        FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                       NULL, errnum,
                       MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                       (LPWSTR)&s, 0, NULL);
        char *err_str;
        wcstombs(err_str, s, 100);
        lean_object *details = lean_mk_string(err_str);
        LocalFree(s);
#else
        lean_object *details = lean_mk_string(gai_strerror(status));
#endif
        return lean_io_result_mk_error(lean_mk_io_user_error(details));
    }
    sal->address_len = ai_res->ai_addrlen;
    sal->address = *(sockaddr_storage *)(ai_res->ai_addr);
    freeaddrinfo(ai_res);
    return lean_io_result_mk_ok(sockaddr_len_box(sal));
}

/**
 * opaque Sock.family (a : @&SockAddr) : Option AddressFamily
 */
lean_obj_res lean_sockaddr_family(b_lean_obj_arg a, lean_obj_arg w)
{
    switch (sockaddr_len_unbox(a)->address.ss_family)
    {
    case AF_UNSPEC:
        return lean_option_mk_some(lean_box(0));
    case AF_INET:
        return lean_option_mk_some(lean_box(1));
    case AF_INET6:
        return lean_option_mk_some(lean_box(2));
    default:
        return lean_option_mk_none();
    }
}

/**
 * opaque Sock.port (a : @&SockAddr) : Option UInt16
 */
lean_obj_res lean_sockaddr_port(b_lean_obj_arg a, lean_obj_arg w)
{
    lean_object *o;
    sockaddr_len *sa = sockaddr_len_unbox(a);
    if (sa->address.ss_family == AF_INET)
    {
        o = lean_alloc_ctor(1, 1, 0);
        lean_ctor_set(o, 0, lean_box(ntohs(((sockaddr_in *)&(sa->address))->sin_port)));
    }
    else if (sa->address.ss_family == AF_INET6)
    {
        o = lean_alloc_ctor(1, 1, 0);
        lean_ctor_set(o, 0, lean_box(ntohs(((sockaddr_in6 *)&(sa->address))->sin6_port)));
    }
    else
    {
        o = lean_alloc_ctor(0, 0, 0);
    }
    return o;
}

/**
 * opaque SockAddr.host (a : @&SockAddr) : Option String
 */
lean_obj_res lean_sockaddr_host(b_lean_obj_arg a, lean_obj_arg w)
{
    lean_object *o;
    sockaddr_len *sa = sockaddr_len_unbox(a);
    if (sa->address.ss_family == AF_INET)
    {
        char ip4[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(((sockaddr_in *)&(sa->address))->sin_addr), ip4, INET_ADDRSTRLEN);
        o = lean_alloc_ctor(1, 1, 0);
        lean_ctor_set(o, 0, lean_mk_string(ip4));
    }
    else if (sa->address.ss_family == AF_INET6)
    {
        char ip6[INET6_ADDRSTRLEN];
        inet_ntop(AF_INET6, &(((sockaddr_in6 *)&(sa->address))->sin6_addr), ip6, INET6_ADDRSTRLEN);
        o = lean_alloc_ctor(1, 1, 0);
        lean_ctor_set(o, 0, lean_mk_string(ip6));
    }
    else
    {
        o = lean_alloc_ctor(0, 0, 0);
    }
    return o;
}

/**
 * opaque beq (a1 a2 : @& SockAddr) : Bool
 */
uint8_t lean_sockaddr_beq(b_lean_obj_arg a1, b_lean_obj_arg a2) {
    sockaddr_len* a1s = sockaddr_len_unbox(a1);
    sockaddr_len* a2s = sockaddr_len_unbox(a2);
    if (a1s->address.ss_family != a2s->address.ss_family)
        return 0;
    switch (a1s->address.ss_family) {
        case AF_INET: {
            sockaddr_in* a1i = (sockaddr_in *)&(a1s->address);
            sockaddr_in* a2i = (sockaddr_in *)&(a2s->address);
            return a1i->sin_addr.s_addr == a2i->sin_addr.s_addr &&
                a1i->sin_port == a2i->sin_port;
        }
        case AF_INET6: {
            sockaddr_in6* a1i = (sockaddr_in6 *)&(a1s->address);
            sockaddr_in6* a2i = (sockaddr_in6 *)&(a2s->address);
            return a1i->sin6_port == a2i->sin6_port &&
                memcmp(&a1i->sin6_addr, &a2i->sin6_addr, sizeof(a1i->sin6_addr)) == 0;
        }
        default:
            return 0;
    }
}

// ## Other Functions

/**
 * opaque hostName : IO String
 */
lean_obj_res lean_gethostname()
{
    char buffer[100];
    int status = gethostname(buffer, 100);
    if (status < 0)
    {
        return lean_io_result_mk_error(get_socket_error());
    }
    return lean_io_result_mk_ok(lean_mk_string(buffer));
}